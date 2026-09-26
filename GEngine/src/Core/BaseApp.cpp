#include "gepch.h"
#include "Renderer/FrameScheduler.h"
#include "Core/BaseApp.h"
#include "Core/GLDebug.h"
#include "Core/GLContextThread.h"
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
        if (const auto* uniform = std::get_if<UniformBufferError>(&error))
            Log::GetCoreLogger()->error("Application uniform buffer operation={} code={} count={} binding={} stride={}: {}",
                uniform->operation, static_cast<unsigned>(uniform->code), uniform->elementCount,
                uniform->bindingPoint, uniform->elementBytes, uniform->message);
        else if (const auto* model = std::get_if<ModelImportError>(&error))
            Log::GetCoreLogger()->error("Application model import operation={} code={} source={}: {}",
                model->operation, static_cast<unsigned>(model->code), model->source, model->message);
        else if (const auto* shape = std::get_if<ShapeRegistrationError>(&error))
            Log::GetCoreLogger()->error("Application shape registration operation={} code={} name={}: {}",
                shape->operation, static_cast<unsigned>(shape->code), shape->name, shape->message);
        else if (const auto* physics = std::get_if<PhysicsShapeError>(&error))
            Log::GetCoreLogger()->error("Application physics shape operation={} code={} entity={} radius={} points={}: {}",
                physics->operation, static_cast<unsigned>(physics->code), physics->entity,
                physics->radius, physics->pointCount, physics->message);
        else if (const auto* scene = std::get_if<SceneError>(&error))
        {
            if (scene->transform)
                Log::GetCoreLogger()->error("Application scene operation={} code={} entity={}: {} transform-code={} transform-entity={} transform-parent={}",
                    scene->operation, static_cast<unsigned>(scene->code), scene->entity, scene->message,
                    static_cast<unsigned>(scene->transform->code), static_cast<std::uint64_t>(scene->transform->entity),
                    static_cast<std::uint64_t>(scene->transform->parent));
            else
                Log::GetCoreLogger()->error("Application scene operation={} code={} entity={}: {}",
                    scene->operation, static_cast<unsigned>(scene->code), scene->entity, scene->message);
        }
        else if (const auto* framebuffer = std::get_if<FramebufferError>(&error)) ReportFramebufferError("application startup", *framebuffer);
        else if (const auto* platform = std::get_if<PlatformError>(&error)) ReportPlatformError(*platform);
        else if (const auto* shader = std::get_if<Asset::ShaderError>(&error)) Asset::ReportShaderError(*shader);
        else if (const auto* texture = std::get_if<Asset::TextureError>(&error))
            Log::GetCoreLogger()->error("Application texture code={} source={} system-category={} system-code={} system-message={} registry={}: {}",
                static_cast<unsigned>(texture->code), texture->source, texture->system.category().name(),
                texture->system.value(), texture->system.message(), static_cast<unsigned>(texture->registry), texture->message);
    }

    void ReportApplicationError(const ApplicationRuntimeError& error)
    {
        // Runtime failures must remain visible in Release through the engine logger.
        Log::GetCoreLogger()->error("Application runtime failure category={} subsystem={} code={} operation={} context={}: {}",
            static_cast<unsigned>(error.code), error.subsystem, error.subsystemCode,
            error.operation, error.context, error.message);
    }

    void BaseApp::FailRuntime(ApplicationRuntimeError error)
    {
        if (!m_RuntimeFailure) m_RuntimeFailure = std::move(error);
        m_Running = false;
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

    FramebufferResult BaseApp::RequestShadowQuality(const ShadowQualityDesc& desc)
    {
        if(!m_Initialize) return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidOperation,
            "Shadow quality changes require an initialized application"});
        GLContextThread::RequireOwner(m_ShadowOwner,"shadow quality request");
        if(auto plan=PlanShadowQuality(desc);!plan) return std::unexpected(plan.error());
        m_PendingShadowQuality=desc;
        return {};
    }
    void BaseApp::ApplyPendingShadowQuality()
    {
        if(!m_PendingShadowQuality) return;
        const auto desc=*m_PendingShadowQuality;m_PendingShadowQuality.reset();
        const auto plan=PlanShadowQuality(desc);
        if(plan->resolution==m_ShadowQuality.resolution && plan->estimatedBytes<=desc.byteBudget) {
            m_ShadowQuality.requested=desc;m_ShadowQuality.effective=desc.quality;
            m_ShadowQuality.fallbackReason.reset();m_ShadowQualityError.reset();return;
        }
        auto next=CreateShadowTargets(desc);
        if(!next) {m_ShadowQualityError=next.error();ReportFramebufferError("shadow quality (previous targets retained)",next.error());return;}
        next->state.successfulReplacements=m_ShadowQuality.successfulReplacements+1;
        m_CascadeShadowFrameBuffer=std::move(next->cascade);
        m_PointShadowFrameBuffer=std::move(next->point);
        m_ShadowQuality=next->state;m_ShadowQualityError.reset();
        if(m_ShadowQuality.fallbackReason)
            GENGINE_CORE_WARN("Shadow quality fallback {} -> {}: code={}, {}",ShadowQualityLabel(desc.quality),
                ShadowQualityLabel(m_ShadowQuality.effective),int(m_ShadowQuality.fallbackReason->code),m_ShadowQuality.fallbackReason->message);
        GENGINE_CORE_INFO("Shadows: {} {}x{}, allocated depth={} MiB (driver overhead unknown)",
            ShadowQualityLabel(m_ShadowQuality.effective),m_ShadowQuality.resolution,m_ShadowQuality.resolution,
            m_ShadowQuality.memory.allocatedDepthBytes/(1024*1024));
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
            auto shadowQuality=ShadowQualityFromEnvironment();
            if(!shadowQuality) return std::unexpected(shadowQuality.error());
            if (auto initialized = m_EngineContext.Initialize(WindowsPropertyList); !initialized)
                return std::visit([](const auto& error) -> ApplicationInitializationResult
                    { return std::unexpected(error); }, initialized.error());
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
            auto shadows=CreateShadowTargets(*shadowQuality);
            if(!shadows) return std::unexpected(shadows.error());
            auto MousePickFrameBufferCandidate = MousePickFrameBuffer::Create(width, height);
            if (!MousePickFrameBufferCandidate) return std::unexpected(MousePickFrameBufferCandidate.error());
            auto FinalFrameBufferCandidate = FinalFrameBuffer::Create(width, height);
            if (!FinalFrameBufferCandidate) return std::unexpected(FinalFrameBufferCandidate.error());
            ScopedPtr<RenderTarget> RenderTargetCandidateOwner(new (std::nothrow) RenderTarget(std::move(*RenderTargetCandidate)));
            if (!RenderTargetCandidateOwner) return std::unexpected(FramebufferError{FramebufferErrorCode::Allocation, "Framebuffer wrapper allocation failed"});
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
            m_CascadeShadowFrameBuffer = std::move(shadows->cascade);
            m_PointShadowFrameBuffer = std::move(shadows->point);
            m_ShadowQuality=shadows->state;
            GENGINE_CORE_INFO("Shadows: {} {}x{}, estimated={} MiB, allocated depth={} MiB (driver overhead unknown)",
                ShadowQualityLabel(m_ShadowQuality.effective),m_ShadowQuality.resolution,m_ShadowQuality.resolution,
                m_ShadowQuality.memory.estimatedBytes/(1024*1024),m_ShadowQuality.memory.allocatedDepthBytes/(1024*1024));
            m_MousePickFrameBuffer = std::move(MousePickFrameBufferCandidateOwner);
            m_FinalFrameBuffer = std::move(FinalFrameBufferCandidateOwner);

            using ApplicationUniformBuffer = UniformBufferObject<UniformType::MATRIX_4_4>;
            auto uniformBuffer = ApplicationUniformBuffer::Create(16);
            if (!uniformBuffer) return std::unexpected(uniformBuffer.error());
            ScopedPtr<ApplicationUniformBuffer> uniformOwner(new (std::nothrow) ApplicationUniformBuffer(std::move(*uniformBuffer)));
            if (!uniformOwner) return std::unexpected(UniformBufferError{UniformBufferErrorCode::Allocation,
                "application initialization", "Uniform buffer owner allocation failed", 16, 0, sizeof(Math::Mat4)});
            m_UniformBufferObject = std::move(uniformOwner);
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

    ApplicationRunResult BaseApp::Run()
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
                if (auto current = m_Window->BeginRender(); !current) {
                    FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Platform",
                        std::to_string(static_cast<unsigned>(current.error().code)), current.error().operation,
                        {}, current.error().message});
                    break;
                }
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
                ApplyPendingShadowQuality();
                if (auto resized = ResizeViewportTargets(); !resized) ReportFramebufferError("viewport targets", resized.error());
                //update                              
#ifdef GENGINE_RENDER_BASELINE
                Update(baseline.Enabled() ? Timestep(0.0) : updateTime);
                baseline.Updated();
#else
                Update(updateTime);
#endif
                

                if (m_RuntimeFailure) break;

                //Render scene
                {
                    //Timeit(Render)
                    const GLDebug::Group submission("Application render");
                    Render();
                }
                if (m_RuntimeFailure) break;
                RenderCounters::EndFrame();
#ifdef GENGINE_RENDER_BASELINE
                if (auto captured = baseline.CaptureScene(m_RenderTarget.get()); !captured)
                { ReportFramebufferError("scene capture", captured.error()); return {}; }
                if (baseline.End()) ShutDown();
#endif
            }
         
        }
#if GENGINE_RENDER_COUNTERS
        if (reportCounters) RenderCounters::ReportLastFrame();
#endif
        if (m_RuntimeFailure) return std::unexpected(*m_RuntimeFailure);
        return {};
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
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Rendering",
                std::to_string(static_cast<unsigned>(result.error().stage)), "render frame", {},
                DescribeScheduleError(result.error())});
        }
    }

    void BaseApp::ShutDown()
    {
        m_Running = false; 
    }
}
