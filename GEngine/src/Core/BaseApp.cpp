#include "gepch.h"
#include "Renderer/FrameScheduler.h"
#include "Core/BaseApp.h"
#include "Core/GLDebug.h"
#include "Core/RenderBaseline.h"
#include "Core/RenderTarget.h"
#include <new>
#include <Camera/PerspectiveCamera.h>
#include <Camera/OrthographicCamera.h>
#include "Core/Window.h"
#include "Core/Renderer.h"
#include "Core/Scene.h"
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
#include <cstdlib>

//#include "Managers/EventManager.h"

namespace GEngine
{
    void ReportApplicationError(const ApplicationInitializationError& error)
    {
        if (const auto* framebuffer = std::get_if<FramebufferError>(&error)) ReportFramebufferError("application startup", *framebuffer);
        else if (const auto* platform = std::get_if<PlatformError>(&error)) ReportPlatformError(*platform);
        else if (const auto* shader = std::get_if<Asset::ShaderError>(&error)) Asset::ReportShaderError(*shader);
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
        if (m_Window)
            if (auto current = m_EngineContext.MakeCurrent(); !current) { ReportPlatformError(current.error()); std::terminate(); }
        m_Scene.reset();
        m_UniformBufferObject.reset();
        m_FinalFrameBuffer.reset();
        m_MousePickFrameBuffer.reset();
        m_PointShadowFrameBuffer.reset();
        m_CascadeShadowFrameBuffer.reset();
        m_RenderTarget.reset();
        m_Window = nullptr;
        m_Initialize = false;
    }

    ApplicationInitializationResult BaseApp::Initialize(const WindowProperties& WindowsPropertyList)
    {
        return Initialize(std::initializer_list<WindowProperties>{WindowsPropertyList});
    }

    void BaseApp::PollEvents() const
    {
        GetEventManager()->PollEvents();
    }

    /*void BaseApp::OnResize(int new_width, int new_height) const
    {

    }

    void BaseApp::OnScroll(float new_zoom) const
    {

    }*/

    PlatformResult BaseApp::SetEditorViewport(EditorViewportLogicalSize logical, FramebufferScale scale)
    {
        auto pixels = ToFramebufferPixels(logical, scale);
        if (!pixels) return std::unexpected(pixels.error());
        m_EditorLogicalSize = logical;
        m_EditorPixelSize = *pixels;
        m_ViewportSize = {logical.Width, logical.Height};
        m_UsesEditorViewport = true;
        return {};
    }

    FramebufferResult BaseApp::ResizeViewportTargets()
    {
        if (!m_Window || !m_RenderTarget || !m_MousePickFrameBuffer || !m_FinalFrameBuffer)
            return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidOperation, "Viewport resizing requires initialized window and targets"});
        m_Window->RefreshDimensions();
        const auto native = m_Window->GetFramebufferPixelSize();
        const auto source = m_UsesEditorViewport ? TargetSizeSource::EditorViewport : TargetSizeSource::NativeFramebuffer;
        auto desc = m_RenderTarget->Description();
        desc.SizeSource = source;
        desc.Storage.Width = m_UsesEditorViewport ? m_EditorPixelSize.Width : native.Width;
        desc.Storage.Height = m_UsesEditorViewport ? m_EditorPixelSize.Height : native.Height;
        m_ViewportTargetsReady = false;
        if (auto resized = m_RenderTarget->Reconfigure(desc); !resized) return resized;
        m_MousePickFrameBuffer->SetSizeSource(source);
        if (auto resized = m_MousePickFrameBuffer->ResizeFrom(native, m_EditorPixelSize); !resized) return resized;
        m_FinalFrameBuffer->SetSizeSource(source);
        if (auto resized = m_FinalFrameBuffer->ResizeFrom(native, m_EditorPixelSize); !resized) return resized;
        m_ViewportTargetsReady = true;
        if (m_UsesEditorViewport && m_EditorCamera && m_RenderTarget->GetWidth() && m_RenderTarget->GetHeight())
            m_EditorCamera->OnResize(m_RenderTarget->GetWidth(), m_RenderTarget->GetHeight());
        return {};
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
            if (auto initialized = m_EngineContext.Initialize(WindowsPropertyList); !initialized) return std::unexpected(initialized.error());
            m_Window = m_EngineContext.MainWindow();
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
                        if (p->second.get() == m_Window)
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
          

            const auto state = m_Window->GetState();
            m_Minimized = state.Minimized;
            m_WindowHidden = state.Hidden;
            auto windowState = new Events<void(WindowStateEvent)>("WindowState");
            windowState->Subscribe([this](WindowStateEvent event)
                {
                    if (event.ID != m_Window->GetWindowID()) return;
                    switch (event.Change)
                    {
                    case WindowStateChange::Minimized: m_Minimized = true; break;
                    case WindowStateChange::Restored:
                    case WindowStateChange::Maximized: m_Minimized = false; break;
                    case WindowStateChange::Hidden: m_WindowHidden = true; break;
                    case WindowStateChange::Shown: m_WindowHidden = false; break;
                    case WindowStateChange::Resized:
                        m_WindowZeroSize = event.LogicalSize.Width == 0 || event.LogicalSize.Height == 0;
                        break;
                    default: break;
                    }
                });
            GetEventManager()->GetEventDispatcher().RegisterEvent(windowState);
            GetInputManager()->SetWindow(m_Window);
            const auto nativePixels = m_Window->GetFramebufferPixelSize();
            const auto width = nativePixels.Width, height = nativePixels.Height;
            m_EditorLogicalSize = {float(m_Window->GetScreenWidth()), float(m_Window->GetScreenHeight())};
            m_EditorPixelSize = {width, height};
            m_ViewportSize = {m_EditorLogicalSize.Width, m_EditorLogicalSize.Height};
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
            auto initialDescription = RenderTargetCandidateOwner->Description();
            initialDescription.SizeSource = TargetSizeSource::NativeFramebuffer;
            if (auto configured = RenderTargetCandidateOwner->Reconfigure(initialDescription); !configured) return std::unexpected(configured.error());
            MousePickFrameBufferCandidateOwner->SetSizeSource(TargetSizeSource::NativeFramebuffer);
            FinalFrameBufferCandidateOwner->SetSizeSource(TargetSizeSource::NativeFramebuffer);
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

      
  /*      if (m_Window->GetImGuiWindow()->WantCaptureKeyBoard())
        {
            GENGINE_CORE_INFO("ImGui capture keyboard");
        }

        if (m_Window->GetImGuiWindow()->WantCaptureMouse())
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

        //if (mouseState.isButtonPressed(GENGINE_BUTTON_RIGHT) && !m_Window->GetImGuiWindow()->WantCaptureMouse())  //&& !static_cast<SDLWindow*>(GetWindowManager()->GetInternalWindow(1))->GetImGuiWindow()->WantCaptureMouse())//to do)
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


        //    else if (auto& keyboardstate = input->GetKeyboardState(); keyboardstate.IsKeyPressed(GENGINE_KEY_SPACE) && !m_Window->GetImGuiWindow()->WantCaptureKeyBoard())
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
                PollEvents();
                input->Update();
                clock.Reset();
                m_FrameTime = {};
                if (m_Running && IsRenderingSuspended()) std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

            if (!IsRenderingSuspended())
            {

                // Query the main context on its owning thread. VSYNC (including
                // adaptive -1) owns pacing when active; otherwise use the manual cap.
                // Anchor to measured frame starts, so work/oversleep counts toward
                // the next interval and a missed deadline creates no pacing backlog.
                if (auto current = m_Window->BeginRender(); !current) { ReportPlatformError(current.error()); ShutDown(); break; }
                if (m_Window->GetSwapInterval() == 0 && m_ManualFrameRateLimit != 0)
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
                PollEvents();
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
                if (auto resized = ResizeViewportTargets(); !resized) ReportFramebufferError("viewport targets", resized.error());
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
        RenderContext context{*m_Window,*GetWindowManager(),m_RenderTarget.get(),HasVisibleViewport()};
        context.legacyScene={this,[](void* user)->ScheduleResult {
            auto& app=*static_cast<BaseApp*>(user);
            Renderer::GetRenderStats().m_ArrayDrawCall=0;
            Renderer::GetRenderStats().m_ElementsDrawCall=0;
            RenderParam param;
            param.ClearColor={.1f,.1f,.1f,1.f}; param.bEnableDepthTest=true;
            param.bClearColorBit=true; param.bClearDepthBit=true; param.bClearStencilBit=false;
            auto result=app.m_EngineContext.RenderScene(app.m_Scene.get(),app.m_EditorCamera,app.m_RenderTarget.get(),param);
            if(!result) return std::unexpected(ScheduleError{FrameStage::Pass,result.error()});
            return {};
        }};
        context.editorUI={this,[](void* user)->ScheduleResult { static_cast<BaseApp*>(user)->ImGuiRender(); return {}; }};
        if(auto result=FrameScheduler::Render(context);!result) {
            GENGINE_CORE_ERROR("{}",DescribeScheduleError(result.error())); ShutDown();
        }
    }

    void BaseApp::ShutDown()
    {
        m_Running = false; 
    }
}
