#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/RenderTarget.h"
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

//#include "Managers/EventManager.h"

namespace GEngine
{
    using namespace Manager;

    BaseApp::BaseApp(): m_LastFrameTime(0.0f)
    {
      
    }

    BaseApp::~BaseApp()
    {
        AssetsManager::FreeAllResources();
        ShaderManager::FreeShader();
        ShapeManager::FreeShape();
    }

    void BaseApp::Initialize(const WindowProperties& WindowsPropertyList)
    {
        Initialize({ WindowsPropertyList });
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

    void BaseApp::Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)
    {

        using namespace Manager;
        using namespace Camera;

        if (!m_Initialize)
        {
            m_GEngine.Initialize(WindowsPropertyList);
        
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
                        windows->RemoveWindow(windowParam.ID);                 

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
          

            m_SDLWindow = static_cast<SDLWindow*>(GetWindowManager()->GetInternalWindow(1));
            GetInputManager()->SetSDLWindow(m_SDLWindow);
            
            //m_RenderTarget = CreateScopedPtr<RenderTarget>(Vec2f{ m_SDLWindow->GetScreenWidth(), m_SDLWindow->GetScreenHeight() });
          
			m_CascadeShadowFrameBuffer = CreateScopedPtr<CascadeShadowFrameBuffer>(8192, 8192, 5);
			m_PointShadowFrameBuffer = CreateScopedPtr<PointShadowFrameBuffer>(8192, 8192);
			m_MousePickFrameBuffer = CreateScopedPtr<MousePickFrameBuffer>(m_SDLWindow->GetScreenWidth(), m_SDLWindow->GetScreenHeight());
			m_FinalFrameBuffer = CreateScopedPtr<FinalFrameBuffer>(m_SDLWindow->GetScreenWidth(), m_SDLWindow->GetScreenHeight());
			m_UniformBufferObject = CreateScopedPtr<UniformBufferObject<UniformType::MATRIX_4_4>>(16);
            m_Initialize = true;


        }
     
       
    }


    void BaseApp::ProcessInput(Timestep ts)
    {
        auto input = GetInputManager();
        input->Update();

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

        SDL_Event event;

        OnEvent(event);

    }

    void BaseApp::Run()
    {
        auto input = GetInputManager();
        auto windows = GetWindowManager();

        uint64_t now = SDL_GetPerformanceCounter();
        //input->SetRelativeMouseMode(true);
        //GENGINE_CORE_INFO("{}", m_Running);
        while (m_Running)
        {

            if (!m_Minimized)
            {

                //const uint64_t now = SDL_GetPerformanceCounter();
                //const float time = static_cast<float>(SDL_GetPerformanceCounter());
                //const Timestep ts = (time - m_LastFrameTime)*1000 / static_cast<float>(SDL_GetPerformanceFrequency());
                m_LastFrameTime = now;
                now = SDL_GetPerformanceCounter();
                Timestep dt_us = 1000000.f * (now - m_LastFrameTime) / static_cast<double>(SDL_GetPerformanceFrequency());
                Timestep dt_ms = 1000.f * dt_us;
                //std::cout << "time passes " << ts << std::endl;

                if (dt_us < 16000.0f)
                {
                    uint64_t x = 16000 - (uint64_t)dt_us;
                    std::this_thread::sleep_for(std::chrono::microseconds(x));
                    dt_us = 16000.f;
                    now = SDL_GetPerformanceCounter();
                }

                m_LastFrameTime = now;
                //std::cout << "--------------------------------------" << "\n";
                {
                    //Timeit(PrepareForUpdate)
                    input->PrepareForUpdate();
                }

                //process input
                {
                    //Timeit(ProcessInput)
                    ProcessInput(dt_ms);
                }

                if (dt_us > 33000.f)
                {
                    dt_us = 33000.f;
                }

                Timestep dt_sec = dt_us * 0.001f * 0.001f;

                //update
                
                    
                Update(dt_sec);
                

                //m_LastFrameTime = now;
                //Render scene
                {
                    //Timeit(Render)
                    Render();
                }
            }
         
        } 

        
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
      
        Renderer::RenderBegin(m_EditorCamera, m_RenderTarget.get());
        Renderer::Set(param);
        Renderer::RenderScene(m_Scene.get(), m_EditorCamera);

        //PlayerCamera
     /*   Renderer::RenderBegin(m_PlayerCamera, m_RenderTarget.get());
        Renderer::Set(param);
        Renderer::RenderScene(m_Scene.get(), m_PlayerCamera);*/

       if (m_RenderTarget && m_RenderTarget->IsMultiSampled()) m_RenderTarget->BindAndBlitToScreen();

       if(m_RenderTarget)
           m_RenderTarget->UnBind();

     
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
