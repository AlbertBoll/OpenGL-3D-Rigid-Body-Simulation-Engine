#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/GLDebug.h"
#include "Core/RenderBaseline.h"
#include "Core/RenderTarget.h"
#include <new>
#include <Camera/PerspectiveCamera.h>
#include <Camera/OrthographicCamera.h>
#include "Windows/SDLWindow.h"
#include <imgui/imgui.h>
#include <Camera/PlayerCamera.h>
#include "Managers/ShapeManager.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShaderManager.h"
#include <Scene/_Entity.h>
#include <Core/Timer.h>
#include <charconv>
#include <stdexcept>
#include <string_view>

//#include "Managers/EventManager.h"

namespace GEngine
{
    void ReportApplicationError(const ApplicationInitializationError& error)
    {
        if (const auto* framebuffer = std::get_if<FramebufferError>(&error)) ReportFramebufferError("application startup", *framebuffer);
        else if (const auto* texture = std::get_if<Asset::TextureError>(&error))
            GENGINE_CORE_ERROR("Application texture {}: {}", texture->source, texture->message);
    }

    using namespace Manager;

    BaseApp::BaseApp()
    {
      
    }

    BaseApp::~BaseApp()
    {
        // Derived resources are already gone. The first-declared EngineContext
        // outlives these borrowers and retires shared caches/platform last.
        if (m_SDLWindow) m_EngineContext.MakeCurrent();
        m_Scene.reset();
        m_UniformBufferObject.reset();
        m_FinalFrameBuffer.reset();
        m_MousePickFrameBuffer.reset();
        m_PointShadowFrameBuffer.reset();
        m_CascadeShadowFrameBuffer.reset();
        m_RenderTarget.reset();
        m_SDLWindow = nullptr;
        m_Initialize = false;
    }

    ApplicationInitializationResult BaseApp::Initialize(const WindowProperties& WindowsPropertyList)
    {
        return Initialize(std::initializer_list<WindowProperties>{WindowsPropertyList});
    }

    void BaseApp::OnEvent(SDL_Event& e)const
    {
        GetEventManager()->OnEvent(e);
    }

    /*void BaseApp::OnResize(int new_width, int new_height) const
    {

    }

    void BaseApp::OnScroll(float new_zoom) const
    {

    }*/

    SDLWindow* BaseApp::GetSDLWindow()
    {
        return m_SDLWindow;
    }

    ApplicationInitializationResult BaseApp::Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)
    {

        using namespace Manager;
        using namespace Camera;

        if (!m_Initialize)
        {
            // One explicit resolution override; keep layer counts and techniques unchanged.
            unsigned int shadowResolution = 4096;
            const char* configuredShadowResolution = SDL_getenv("GENGINE_SHADOW_RESOLUTION");
            if (configuredShadowResolution)
            {
                const std::string_view value(configuredShadowResolution);
                const auto result = std::from_chars(value.data(), value.data() + value.size(), shadowResolution);
                if (result.ec != std::errc{} || result.ptr != value.data() + value.size()
                    || shadowResolution == 0 || shadowResolution > 8192)
                {
                    return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidDescription, "GENGINE_SHADOW_RESOLUTION must be an integer from 1 to 8192; unset it for the safe 4096 default."});
                }
            }
            std::cout << "[Shadows] resolution=" << shadowResolution << "x" << shadowResolution
                << " source=" << (configuredShadowResolution ? "GENGINE_SHADOW_RESOLUTION (explicit)" : "safe default")
                << " estimated depth storage=" << (12ull * shadowResolution * shadowResolution * 4 / (1024 * 1024))
                << " MiB (six cascade layers + six cube faces, estimated at four bytes/texel)" << std::endl;
            m_EngineContext.Initialize(WindowsPropertyList);
            m_SDLWindow = m_EngineContext.MainWindow();
            const GLDebug::Group initialization("Application render resources");
        
            GENGINE_CORE_INFO("Initialize Scene...");
            m_Scene = CreateScopedPtr<Scene>("Scene");
         

            m_ProjectionType[0] = false;
            m_ProjectionType[1] = true;


            auto AppCloseEvent = new Events<void()>("AppClose");
            auto AppQuitEvent = new Events<void()>("AppQuit");
            auto WindowCloseEvent = new Events<void(WindowCloseParam)>("WindowClose");


            WindowCloseEvent->Subscribe(([this](WindowCloseParam windowParam)
                {
                  
                    auto windows = BaseApp::GetWindowManager();
                    if (auto p = windows->GetWindows().find(windowParam.ID); p != windows->GetWindows().end())
                    {             
                        if (p->second.get() == m_SDLWindow)
                        {
                            // Keep the main context alive through derived/base destruction.
                            m_Running = false;
                        }
                        else
                        {
                            windows->RemoveWindow(windowParam.ID);
                        }

                    }
                }));


         

            AppCloseEvent->Subscribe([this]()
                {
                    m_Running = false;
                 

                });

            AppQuitEvent->Subscribe([this]()
                {
                    auto& KeyboardState = GetInputManager()->GetKeyboardState();
                    if (KeyboardState.IsKeyPressed(GENGINE_KEY_ESCAPE))
                    {
                        m_Running = false;
                    }

                });


            GetEventManager()->GetEventDispatcher().RegisterEvent(WindowCloseEvent);
            GetEventManager()->GetEventDispatcher().RegisterEvent(AppCloseEvent);
            GetEventManager()->GetEventDispatcher().RegisterEvent(AppQuitEvent);
          

            const auto windowFlags = SDL_GetWindowFlags(m_SDLWindow->GetSDLWindow());
            m_Minimized = (windowFlags & SDL_WINDOW_MINIMIZED) != 0;
            m_WindowHidden = (windowFlags & SDL_WINDOW_HIDDEN) != 0;
            auto windowState = new Events<void(SDL_WindowEvent)>("WindowState");
            windowState->Subscribe([this](SDL_WindowEvent event)
                {
                    if (event.windowID != m_SDLWindow->GetWindowID()) return;
                    switch (event.event)
                    {
                    case SDL_WINDOWEVENT_MINIMIZED: m_Minimized = true; break;
                    case SDL_WINDOWEVENT_RESTORED:
                    case SDL_WINDOWEVENT_MAXIMIZED: m_Minimized = false; break;
                    case SDL_WINDOWEVENT_HIDDEN: m_WindowHidden = true; break;
                    case SDL_WINDOWEVENT_SHOWN: m_WindowHidden = false; break;
                    case SDL_WINDOWEVENT_RESIZED:
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                        m_WindowZeroSize = event.data1 <= 0 || event.data2 <= 0;
                        break;
                    }
                });
            GetEventManager()->GetEventDispatcher().RegisterEvent(windowState);
            GetInputManager()->SetSDLWindow(m_SDLWindow);
            
            const auto width = m_SDLWindow->GetScreenWidth(), height = m_SDLWindow->GetScreenHeight();
            auto RenderTargetCandidate = RenderTarget::Create(width, height);
            if (!RenderTargetCandidate) return std::unexpected(RenderTargetCandidate.error());
            auto CascadeShadowFrameBufferCandidate = CascadeShadowFrameBuffer::Create(shadowResolution, shadowResolution, 5);
            if (!CascadeShadowFrameBufferCandidate) return std::unexpected(CascadeShadowFrameBufferCandidate.error());
            auto PointShadowFrameBufferCandidate = PointShadowFrameBuffer::Create(shadowResolution, shadowResolution);
            if (!PointShadowFrameBufferCandidate) return std::unexpected(PointShadowFrameBufferCandidate.error());
            auto MousePickFrameBufferCandidate = MousePickFrameBuffer::Create(width, height);
            if (!MousePickFrameBufferCandidate) return std::unexpected(MousePickFrameBufferCandidate.error());
            auto FinalFrameBufferCandidate = FinalFrameBuffer::Create(width, height);
            if (!FinalFrameBufferCandidate) return std::unexpected(FinalFrameBufferCandidate.error());
            ScopedPtr<RenderTarget> RenderTargetCandidateOwner(new (std::nothrow) RenderTarget(std::move(*RenderTargetCandidate)));
            if (!RenderTargetCandidateOwner) return std::unexpected(FramebufferError{FramebufferErrorCode::Allocation, "Framebuffer wrapper allocation failed"});
            ScopedPtr<CascadeShadowFrameBuffer> CascadeShadowFrameBufferCandidateOwner(new (std::nothrow) CascadeShadowFrameBuffer(std::move(*CascadeShadowFrameBufferCandidate)));
            if (!CascadeShadowFrameBufferCandidateOwner) return std::unexpected(FramebufferError{FramebufferErrorCode::Allocation, "Framebuffer wrapper allocation failed"});
            ScopedPtr<PointShadowFrameBuffer> PointShadowFrameBufferCandidateOwner(new (std::nothrow) PointShadowFrameBuffer(std::move(*PointShadowFrameBufferCandidate)));
            if (!PointShadowFrameBufferCandidateOwner) return std::unexpected(FramebufferError{FramebufferErrorCode::Allocation, "Framebuffer wrapper allocation failed"});
            ScopedPtr<MousePickFrameBuffer> MousePickFrameBufferCandidateOwner(new (std::nothrow) MousePickFrameBuffer(std::move(*MousePickFrameBufferCandidate)));
            if (!MousePickFrameBufferCandidateOwner) return std::unexpected(FramebufferError{FramebufferErrorCode::Allocation, "Framebuffer wrapper allocation failed"});
            ScopedPtr<FinalFrameBuffer> FinalFrameBufferCandidateOwner(new (std::nothrow) FinalFrameBuffer(std::move(*FinalFrameBufferCandidate)));
            if (!FinalFrameBufferCandidateOwner) return std::unexpected(FramebufferError{FramebufferErrorCode::Allocation, "Framebuffer wrapper allocation failed"});
            m_RenderTarget = std::move(RenderTargetCandidateOwner);
            m_CascadeShadowFrameBuffer = std::move(CascadeShadowFrameBufferCandidateOwner);
            m_PointShadowFrameBuffer = std::move(PointShadowFrameBufferCandidateOwner);
            m_MousePickFrameBuffer = std::move(MousePickFrameBufferCandidateOwner);
            m_FinalFrameBuffer = std::move(FinalFrameBufferCandidateOwner);

			m_UniformBufferObject = CreateScopedPtr<UniformBufferObject<UniformType::MATRIX_4_4>>(16);
            m_Initialize = true;


        }
     
       
        return {};
    }


    void BaseApp::ProcessInput(Timestep ts)
    {
        auto input = GetInputManager();

        auto& keyboardState = input->GetKeyboardState();
        auto& mouseState = input->GetMouseState();

        //mouseState.SetCursorMode(CursorMode::LOCKED);
        //Vec2f mousePos = { mouseState.m_MousePos.x, mouseState.m_MousePos.y };
        //std::cout << "X: " << mousePos.x << " Y: " << mousePos.y << std::endl;
   

        /*if (mouseState.m_XRel != m_LastXRel || mouseState.m_YRel != m_LastYRel)
        {
            m_LastXRel = mouseState.m_XRel;
            m_LastYRel = mouseState.m_YRel;
        }

        else
        {
            mouseState.m_XRel = 0;
            mouseState.m_YRel = 0;
        }*/


       /* if (!mouseState.isButtonPressed(GENGINE_BUTTON_LEFT) && mouseState.isButtonHeld(GENGINE_BUTTON_LEFT))
        {
            GENGINE_CORE_INFO("Mouse button press");
        }*/
  /*      if (mouseState.isButtonPressed(GENGINE_BUTTON_LEFT))
        {
            GENGINE_CORE_INFO("Mouse button pressed");
        }*/

      /*  if (mouseState.isButtonHeld(GENGINE_BUTTON_LEFT))
        {
            GENGINE_CORE_INFO("Mouse button held");
        }

        else if (mouseState.isButtonPressed(GENGINE_BUTTON_LEFT))
        {
            GENGINE_CORE_INFO("Mouse button pressed");
        }

      
  /*      if (m_SDLWindow->GetImGuiWindow()->WantCaptureKeyBoard())
        {
            GENGINE_CORE_INFO("ImGui capture keyboard");
        }

        if (m_SDLWindow->GetImGuiWindow()->WantCaptureMouse())
        {
            GENGINE_CORE_INFO("ImGui capture mouse");
        }*/


        /*
        if (keyboardState.IsKeyHeld(GENGINE_KEY_LALT))
        {
            //GENGINE_CORE_INFO("left alt held");
            const Vec2f mouse = { mouseState.GetPosition().x, mouseState.GetPosition().y };
            Vec2f delta = (mouse - m_EditorCamera->GetInitialMousePosition()) * 0.003f;
            m_EditorCamera->GetInitialMousePosition() = mouse;
            if (mouseState.isButtonHeld(GENGINE_BUTTON_LEFT))
                m_EditorCamera->MouseRotate(delta);

            else if (mouseState.isButtonHeld(GENGINE_BUTTON_MIDDLE))
                m_EditorCamera->MousePan(delta);

            //m_EditorCamera->OnUpdateView();
        }
        */

        //if (mouseState.isButtonPressed(GENGINE_BUTTON_RIGHT))
        //{
            //std::cout << "right button is pressed" << std::endl;
            //input->SetRelativeMouseMode(true);
            //mouseState.SetCursorMode(CursorMode::)
        //}

        //if (mouseState.isButtonPressed(GENGINE_BUTTON_RIGHT) && !m_SDLWindow->GetImGuiWindow()->WantCaptureMouse())  //&& !static_cast<SDLWindow*>(GetWindowManager()->GetInternalWindow(1))->GetImGuiWindow()->WantCaptureMouse())//to do)
       // {
          // std::cout << "right button is pressed" << std::endl;
            //input->SetRelativeMouseMode(true);
            
            //GENGINE_CORE_INFO("relative - PosX: {}, PosY: {}", x, y);

            //if (mouseState.isFirstMouse())
            //{
               // x = 0, y = 0;
                //mouseState.m_LastMouseX = mouseState.m_MousePos.x;
                //mouseState.m_LastMouseY = mouseState.m_MousePos.y;

                //GENGINE_CORE_INFO("relative - LastPosX: {}, LastPosY: {}", mouseState.m_LastMouseX, mouseState.m_LastMouseY);
                //mouseState.SetFirstMouse(false);
        //}

        //if ((!m_ViewportForcused && m_ViewportHovered) && !mouseState.IsRelative())
        //{
        //    if (keyboardState.IsKeyPressed(GENGINE_KEY_Q))
        //    {
        //        m_GizmoType = -1;
        //    }

        //    if (keyboardState.IsKeyPressed(GENGINE_KEY_W))
        //    {
        //        m_GizmoType = 0;
        //    }

        //    if (keyboardState.IsKeyPressed(GENGINE_KEY_E))
        //    {
        //        m_GizmoType = 1;
        //    }

        //    if (keyboardState.IsKeyPressed(GENGINE_KEY_R))
        //    {
        //        m_GizmoType = 2;
        //    }


        //    else if (auto& keyboardstate = input->GetKeyboardState(); keyboardstate.IsKeyPressed(GENGINE_KEY_SPACE) && !m_SDLWindow->GetImGuiWindow()->WantCaptureKeyBoard())
        //    {
        //        input->SetRelativeMouseMode(false);
        //        //mouseState.SetFirstMouse(true);
        //        //GENGINE_CORE_INFO("non relative - xPos: {}, yPos: {}", static_cast<float>(x), static_cast<float>(y));
        //       /* mouseState.m_CurrentButtons =
        //            SDL_GetMouseState(&x, &y);*/
        //    }
        //}

        // Run owns event pumping and state sampling before virtual application controls.

    }

    void BaseApp::Run()
    {
#ifdef GENGINE_RENDER_BASELINE
        RenderBaseline::Session baseline;
#endif
#if GENGINE_RENDER_COUNTERS
        const char* counterLog = SDL_getenv("GENGINE_RENDER_COUNTERS_LOG");
        const bool reportCounters = counterLog && std::string_view(counterLog) == "1";
#endif
        auto input = GetInputManager();

        FrameClock clock;
        //input->SetRelativeMouseMode(true);
        //GENGINE_CORE_INFO("{}", m_Running);
        while (m_Running)
        {
#ifdef GENGINE_RENDER_BASELINE
            baseline.Begin();
#endif
            if (IsRenderingSuspended())
            {
                // Keep restore/close events live without running application controls
                // or simulation/render work. Retire transient input while suspended.
                input->PrepareForUpdate();
                SDL_Event event{};
                OnEvent(event);
                input->Update();
                clock.Reset();
                m_FrameTime = {};
                if (m_Running && IsRenderingSuspended()) SDL_Delay(16);
                continue;
            }

            if (!IsRenderingSuspended())
            {

                // Query the main context on its owning thread. VSYNC (including
                // adaptive -1) owns pacing when active; otherwise use the manual cap.
                // Anchor to measured frame starts, so work/oversleep counts toward
                // the next interval and a missed deadline creates no pacing backlog.
                m_SDLWindow->BeginRender();
                if (SDL_GL_GetSwapInterval() == 0 && m_ManualFrameRateLimit != 0)
                    std::this_thread::sleep_until(clock.LastSample() + Seconds(1.0 / m_ManualFrameRateLimit));
                m_FrameTime = clock.Tick();
                const Timestep inputTime(m_FrameTime.renderDelta);
                // Preserve all elapsed time for the scene's existing overload accounting.
                const Timestep updateTime(m_FrameTime.rawDelta);
                //std::cout << "--------------------------------------" << "\n";
                {
                    //Timeit(PrepareForUpdate)
                    input->PrepareForUpdate();
                }

                //process input
                RenderCounters::BeginFrame();
#ifdef GENGINE_RENDER_BASELINE
                baseline.Work();
#endif
                SDL_Event event{};
                OnEvent(event);
                input->Update();
                if (!m_Running) break;
                if (IsRenderingSuspended()) continue;
                {
                    //Timeit(ProcessInput)
                    ProcessInput(inputTime);
                }

                if (!m_Running) break;
                if (IsRenderingSuspended()) continue;

#ifdef GENGINE_RENDER_BASELINE
                baseline.UpdatedInput();
#endif
                //update                              
#ifdef GENGINE_RENDER_BASELINE
                Update(baseline.Enabled() ? Timestep(0.0) : updateTime);
                baseline.Updated();
#else
                Update(updateTime);
#endif
                

                //Render scene
                {
                    //Timeit(Render)
                    const GLDebug::Group submission("Application render");
                    Render();
                }
                RenderCounters::EndFrame();
#ifdef GENGINE_RENDER_BASELINE
                if (auto captured = baseline.CaptureScene(m_RenderTarget.get()); !captured)
                { ReportFramebufferError("scene capture", captured.error()); return; }
                if (baseline.End()) ShutDown();
#endif
            }
         
        }
#if GENGINE_RENDER_COUNTERS
        if (reportCounters) RenderCounters::ReportLastFrame();
#endif

        
    }

    void BaseApp::Render()
    {
        static int i = 0;
        Renderer::GetRenderStats().m_ArrayDrawCall = 0;
        Renderer::GetRenderStats().m_ElementsDrawCall = 0;
        auto& windows = GetWindowManager()->GetWindows();

        RenderParam param;
        param.ClearColor = { 0.1f, 0.1f, 0.1f, 1.f };
        param.bEnableDepthTest = true;
        param.bClearColorBit = true;
        param.bClearDepthBit = true;
        param.bClearStencilBit = false;

        //Renderer::RenderBegin(m_SceneCamera, true, true, m_RenderTarget.get());
        //Renderer::Clear();

        //Editor Camera
      
        if (HasVisibleViewport())
        {
            m_EngineContext.RenderScene(m_Scene.get(), m_EditorCamera, m_RenderTarget.get(), param);

            //PlayerCamera
         /*   Renderer::RenderBegin(m_PlayerCamera, m_RenderTarget.get());
            Renderer::Set(param);
            Renderer::RenderScene(m_Scene.get(), m_PlayerCamera);*/

           if (m_RenderTarget && m_RenderTarget->IsMultiSampled())
               if (auto resolved = m_RenderTarget->BindAndBlitToScreen(); !resolved) { ReportFramebufferError("resolve", resolved.error()); return; }

           if(m_RenderTarget)
               m_RenderTarget->UnBind();


        }

       for (auto& [windowID, window] : windows)
       {
           auto window_ = static_cast<SDLWindow*>(window.get());
           window_->GetImGuiWindow()->BeginRender(window_);
           //ImGuiRender();
           window_->GetImGuiWindow()->EndRender(window_);
           window_->SwapBuffer();
       }
       

    

        //
        //for (auto& [windowID, window] : windows)
        //{
        //    window->BeginRender();
        //
        //    Renderer::RenderBegin(m_EditorCamera, window.get());

        //    //inplemented by derived class inherited from BaseApp
        //   // Update(ts);
        //    Renderer::RenderScene(m_Scene.get(), m_EditorCamera);
        //    window->EndRender(this);
        //
        //}
    }

    void BaseApp::ShutDown()
    {
        m_Running = false; 
    }
}
