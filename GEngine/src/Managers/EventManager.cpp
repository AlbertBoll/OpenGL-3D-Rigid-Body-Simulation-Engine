#include "gepch.h"

#include "Managers/EventManager.h"
#include "Managers/WindowManager.h"
#include "Events/MouseEvent.h"
#include "Events/KeyboardEvent.h"
#include "Events/ApplicationEvent.h"
#include "Core/BaseApp.h"
#include "Windows/SDLWindow.h"


//using namespace GEngine::Event;

//namespace GEngine
//{
//	class Event;
//}


namespace GEngine::Manager
{

	

	ScopedPtr<EventManager> EventManager::GetScopedInstance()
	{
		struct MkUniEnablr : public EventManager {};
		auto instance = CreateScopedPtr<MkUniEnablr>();

		return instance;
	}


	void EventManager::Initialize()
	{	
		using namespace GEngine;

		//auto AppQuitEvent = new Events<void()>("ApplicationQuit");
		//auto WindowCloseEvent = new Events<void(WindowCloseParam)>("WindowClose");
		//auto KeyPressEvent = new Events<void(const char*)>("KeyPress");
		//auto KeyRepeatEvent = new Events<void(const char*)>("KeyRepeat");
		//auto KeyReleaseEvent = new Events<void(const char*)>("KeyRelease");
		//auto MouseButtonPressEvent = new Events<void(MouseButtonParam)>("MouseButtonPress");
		//auto MouseButtonReleaseEvent = new Events<void(MouseButtonParam)>("MouseButtonRelease");
		auto MouseMoveEvent = new Events<void(MouseMoveParam)>("MouseMove");
	


		//AppQuitEvent->Subscribe([this]()
		//	{
		//		auto& engine = BaseApp::GetEngine();
		//		engine.ShutDown();

		//	}); 


		MouseMoveEvent->Subscribe([](const MouseMoveParam& moveParam)
			{
				auto& mouseState = BaseApp::GetEngine().GetInputManager()->GetMouseState();
				//mouseState.SetCursorMode(CursorMode::HIDDEN);
				mouseState.m_XRel = moveParam.XRel;
				mouseState.m_YRel = moveParam.YRel;	
				mouseState.m_MousePos.x = (float)moveParam.XPos;
				mouseState.m_MousePos.y = (float)moveParam.YPos;
				//GENGINE_INFO("x: {}, y: {}", mouseState.m_XRel, mouseState.m_YRel);

			});


		/*MouseButtonReleaseEvent->Subscribe([](const MouseButtonParam& buttonParam)
			{
				auto& engine = BaseApp::GetEngine();
				if (auto p = engine.GetWindowManager()->GetWindows().find(buttonParam.ID); p != engine.GetWindowManager()->GetWindows().end())
				{
					if (buttonParam.Button == SDL_BUTTON_LEFT)
						GENGINE_CORE_INFO("Left Mouse button was release at {}. Window coords ({}, {})", p->second->GetTitle(), buttonParam.X, buttonParam.Y);
					else if (buttonParam.Button == SDL_BUTTON_MIDDLE)
						GENGINE_CORE_INFO("Middle Mouse button was release at {}. Window coords ({}, {})", p->second->GetTitle(), buttonParam.X, buttonParam.Y);
					else
						GENGINE_CORE_INFO("Right Mouse button was release at {}. Window coords ({}, {})", p->second->GetTitle(), buttonParam.X, buttonParam.Y);
				}
			});*/



		/*MouseButtonPressEvent->Subscribe([](const MouseButtonParam& buttonParam)
			{
				auto& engine = BaseApp::GetEngine();
				if (auto p = engine.GetWindowManager()->GetWindows().find(buttonParam.ID); p != engine.GetWindowManager()->GetWindows().end())
				{
					if (buttonParam.Button == SDL_BUTTON_LEFT)
					{
						if (buttonParam.Clicks == 1)
							GENGINE_CORE_INFO("Left mouse button was clicked at {}. Window coords ({}, {})", p->second->GetTitle(), buttonParam.X, buttonParam.Y);
						else
							GENGINE_CORE_INFO("Left mouse button was double clicked at {}. Window coords ({}, {})", p->second->GetTitle(), buttonParam.X, buttonParam.Y);
					}

					else if (buttonParam.Button == SDL_BUTTON_MIDDLE)
					{
						if (buttonParam.Clicks == 1)
							GENGINE_CORE_INFO("Middle mouse button was clicked at {}. Window coords ({}, {})", p->second->GetTitle(), buttonParam.X, buttonParam.Y);
						else
							GENGINE_CORE_INFO("Middle mouse button was double clicked at {}. Window coords ({}, {})", p->second->GetTitle(), buttonParam.X, buttonParam.Y);
					}

					else
					{
						if (buttonParam.Clicks == 1)
							GENGINE_CORE_INFO("Right mouse button was clicked at {}. Window coords ({}, {})", p->second->GetTitle(), buttonParam.X, buttonParam.Y);
						else
							GENGINE_CORE_INFO("Right mouse button was double clicked at {}. Window coords ({}, {})", p->second->GetTitle(), buttonParam.X, buttonParam.Y);
					}
				}
			});*/


	/*	KeyPressEvent->Subscribe([](const char* keyName)
			{
				GENGINE_CORE_INFO("Key {} was pressed", keyName);
			});

		KeyRepeatEvent->Subscribe([](const char* keyName)
			{
				GENGINE_CORE_INFO("Key {} was repeated", keyName);
			});

		KeyReleaseEvent->Subscribe([](const char* keyName)
			{
				GENGINE_CORE_INFO("Key {} was released", keyName);
			});*/



		//WindowCloseEvent->Subscribe(([](WindowCloseParam windowParam)
		//	{
		//		auto windows = BaseApp::GetWindowManager();
		//		if (auto p = windows->GetWindows().find(windowParam.ID); p != windows->GetWindows().end())
		//		{
		//			//GENGINE_CORE_INFO("Window with ID {} was closed", windowParam.ID);
		//			windows->RemoveWindow(windowParam.ID);
		//			//GENGINE_CORE_INFO("Window Manager Size {}", windows->GetWindows().size());

		//			//auto& numWindows = windows->GetNumOfWindows();
		//			//if (numWindows == 0) {
		//				//BaseApp::GetEngine().ShutDown();
		//				//GENGINE_CORE_INFO("ShutDown!");

		//			//}
		//		}
		//	}));

	

		//m_EventDispatcher.RegisterEvent(WindowCloseEvent);
		//m_EventDispatcher.RegisterEvent(KeyPressEvent);
		//m_EventDispatcher.RegisterEvent(KeyRepeatEvent);
		//m_EventDispatcher.RegisterEvent(KeyReleaseEvent);
		//m_EventDispatcher.RegisterEvent(MouseButtonPressEvent);
		//m_EventDispatcher.RegisterEvent(MouseButtonReleaseEvent);
		m_EventDispatcher.RegisterEvent(MouseMoveEvent);
	
		
	}



	void EventManager::OnEvent(SDL_Event& e)
	{

		auto window = static_cast<SDLWindow*>(BaseApp::GetWindowManager()->GetInternalWindow(1));
		while (SDL_PollEvent(&e))
		{
			if (BaseApp::GetWindowManager()->GetNumOfWindows() != 0)
				window->GetImGuiWindow()->HandleSDLEvent(e);

			switch (e.type)
			{
			case SDL_QUIT:
				//case SDL_KEYDOWN:
				m_EventDispatcher.DispatchEvent("AppClose");
				std::cout << "App close event" << std::endl;
				break;

				//case SDL_KEYDOWN:
				//	if (!e.key.repeat)
				//	{
				//		m_EventDispatcher.DispatchEvent("AppQuit");
				//		//std::cout << "App quit event" << std::endl;
				//		//m_EventDispatcher.DispatchEvent("KeyPress", SDL_GetKeyName(e.key.keysym.sym));
				//	}

				//	//else 
				//		//m_EventDispatcher.DispatchEvent("KeyRepeat", SDL_GetKeyName(e.key.keysym.sym));
				//	break;

			case SDL_MOUSEWHEEL:

				m_EventDispatcher.DispatchEvent("MouseScrollWheel", MouseScrollWheelParam{ .ID = e.wheel.windowID,
																						   .X = e.wheel.preciseX,
																						   .Y = e.wheel.preciseY });
				break;

				//case SDL_KEYUP:
					//m_EventDispatcher.DispatchEvent("KeyRelease", SDL_GetKeyName(e.key.keysym.sym));
					//break;

			case SDL_MOUSEBUTTONDOWN:
				m_EventDispatcher.DispatchEvent("MouseButtonPress", MouseButtonParam{ .ID = e.button.windowID,
																						.X = e.button.x,
																						.Y = e.button.y ,
																						.Button = e.button.button,
																						.Clicks = e.button.clicks });

				break;

					//break;

				//case SDL_MOUSEBUTTONUP:
					/*m_EventDispatcher.DispatchEvent("MouseButtonRelease", MouseButtonParam{ .ID = e.button.windowID,
																							.X = e.button.x,
																							.Y = e.button.y ,
																							.Button = e.button.button,
																							.Clicks = e.button.clicks });*/

																							//break;




			case SDL_MOUSEMOTION:
				m_EventDispatcher.DispatchEvent("MouseMove", MouseMoveParam{ .ID = e.button.windowID,
																			 .XPos = e.motion.x,
																			 .YPos = e.motion.y,
																			 .XRel = e.motion.xrel,
																			 .YRel = e.motion.yrel });

				//std::cout << "X: " << e.motion.x << " Y: " << e.motion.y << std::endl;

				break;

			case SDL_WINDOWEVENT:
			{

				if (e.window.event == SDL_WINDOWEVENT_CLOSE)
				{
					m_EventDispatcher.DispatchEvent("WindowClose", WindowCloseParam{ .ID = e.window.windowID });
					std::cout << "window close event" << std::endl;
				}

				else if (e.window.event == SDL_WINDOWEVENT_RESIZED)
				{
					m_EventDispatcher.DispatchEvent("WindowResize", WindowResizeParam{ .ID = e.window.windowID,
																					   .Width = e.window.data1,
																					   .Height = e.window.data2 });
				}

				break;

			}
			case SDL_KEYDOWN:
				if (e.key.keysym.sym == SDLK_p)
					m_EventDispatcher.DispatchEvent("AppPause");
				else if (e.key.keysym.sym == SDLK_r)
					m_EventDispatcher.DispatchEvent("AppResume");
				break;
			}
		}
	}

}