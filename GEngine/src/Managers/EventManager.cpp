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
        auto connected = Subscribe(Event::MouseMove, [](const MouseMoveParam& moveParam)
        {
            auto& mouse = BaseApp::GetEngine().GetInputManager()->GetMouseState();
            mouse.m_XRel = moveParam.XRel; mouse.m_YRel = moveParam.YRel;
            mouse.m_MousePos.x = static_cast<float>(moveParam.XPos);
            mouse.m_MousePos.y = static_cast<float>(moveParam.YPos);
        });
        if (!connected) { ReportSubscriptionError(connected.error()); return; }
        m_MouseMoveConnection = std::move(*connected);
    }

	void EventManager::PollEvents()
	{
        auto drained = m_Completions.Drain();
        if (!drained) { ReportSubscriptionError(drained.error()); return; }
        const auto deliver = [this](auto key, auto... values) {
            if (auto result = m_EventDispatcher.Dispatch(key, values...); !result)
                ReportSubscriptionError(result.error());
        };


		auto* root = EngineContext::TryGet();
        auto* window = root ? static_cast<SDLWindow*>(root->MainWindow()) : nullptr;
        SDL_Event e{};
		while (SDL_PollEvent(&e))
		{
			if (window && window->GetImGuiWindow())
				window->GetImGuiWindow()->HandleSDLEvent(e);

			switch (e.type)
			{
			case SDL_QUIT:
				//case SDL_KEYDOWN:
				deliver(Event::AppClose);
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

				deliver(Event::MouseScrollWheel, MouseScrollWheelParam{ .ID = e.wheel.windowID,
																						   .X = e.wheel.preciseX,
																						   .Y = e.wheel.preciseY });
				break;

				//case SDL_KEYUP:
					//m_EventDispatcher.DispatchEvent("KeyRelease", SDL_GetKeyName(e.key.keysym.sym));
					//break;

			case SDL_MOUSEBUTTONDOWN:
				deliver(Event::MouseButtonPress, MouseButtonParam{ .ID = e.button.windowID,
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
				deliver(Event::MouseMove, MouseMoveParam{ .ID = e.button.windowID,
																			 .XPos = e.motion.x,
																			 .YPos = e.motion.y,
																			 .XRel = e.motion.xrel,
																			 .YRel = e.motion.yrel });

				//std::cout << "X: " << e.motion.x << " Y: " << e.motion.y << std::endl;

				break;

			case SDL_WINDOWEVENT:
			{
				// Preserve the actual SDL state event; resizing and visibility are independent.
				auto& windows = BaseApp::GetWindowManager()->GetWindows();
                auto found = windows.find(e.window.windowID);
                WindowStateChange change = WindowStateChange::Other;
                switch (e.window.event)
                {
                case SDL_WINDOWEVENT_MINIMIZED: change = WindowStateChange::Minimized; break;
                case SDL_WINDOWEVENT_RESTORED: change = WindowStateChange::Restored; break;
                case SDL_WINDOWEVENT_MAXIMIZED: change = WindowStateChange::Maximized; break;
                case SDL_WINDOWEVENT_HIDDEN: change = WindowStateChange::Hidden; break;
                case SDL_WINDOWEVENT_SHOWN: change = WindowStateChange::Shown; break;
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_SIZE_CHANGED: change = WindowStateChange::Resized; break;
                case SDL_WINDOWEVENT_DISPLAY_CHANGED: change = WindowStateChange::DisplayChanged; break;
                }
                NativeWindowLogicalSize logical{};
                NativeFramebufferPixelSize pixels{};
                if (found != windows.end())
                {
                    found->second->RefreshDimensions();
                    logical = found->second->GetLogicalSize(); pixels = found->second->GetFramebufferPixelSize();
                }
                // Preserve zero-size notifications for suspension even on platforms
                // that retain their last nonzero drawable while minimized.
                if (change == WindowStateChange::Resized && (e.window.data1 <= 0 || e.window.data2 <= 0))
                    logical = {};
                deliver(Event::WindowState, WindowStateEvent{e.window.windowID, change, logical, pixels});

				if (e.window.event == SDL_WINDOWEVENT_CLOSE)
				{
					deliver(Event::WindowClose, WindowCloseParam{ .ID = e.window.windowID });
					std::cout << "window close event" << std::endl;
				}

				else if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
				{
					deliver(Event::WindowResize, WindowResizeParam{ .ID = e.window.windowID,
																					   .Width = e.window.data1,
																					   .Height = e.window.data2 });
				}

				break;

			}
			case SDL_KEYDOWN:
				if (e.key.keysym.sym == SDLK_p)
					deliver(Event::AppPause);
				else if (e.key.keysym.sym == SDLK_r)
					deliver(Event::AppResume);
				else if (e.key.keysym.sym == SDLK_SPACE)
					deliver(Event::DebugShow);
				else
					deliver(Event::ViewportChange);
				break;
			}
		}
	}

}
