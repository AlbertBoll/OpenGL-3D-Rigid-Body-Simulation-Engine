#include "gepch.h"
#include "Events/MouseEvent.h"
#include "Core/BaseApp.h"

namespace GEngine
{

	namespace Event
	{

		void MouseEvent::OnMouseButtonClick(SDL_MouseButtonEvent& e)
		{
			auto& engine = BaseApp::GetEngine();
			auto* windowsManager = engine.GetWindowManager();
			if (auto p = windowsManager->GetWindows().find(e.windowID); p != windowsManager->GetWindows().end())
			{
				if (e.button == SDL_BUTTON_LEFT)
				{
					if (e.clicks == 1)
						GENGINE_CORE_INFO("Left mouse button was clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.x, e.y);
					else
						GENGINE_CORE_INFO("Left mouse button was double clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.x, e.y);
				}

				else if (e.button == SDL_BUTTON_MIDDLE)
				{
					if (e.clicks == 1)
						GENGINE_CORE_INFO("Middle mouse was clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.x, e.y);
					else
						GENGINE_CORE_INFO("Middle mouse was double clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.x, e.y);
				}

				else
				{
					if (e.clicks == 1)
						GENGINE_CORE_INFO("Right mouse was clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.x, e.y);
					else
						GENGINE_CORE_INFO("Right mouse was double clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.x, e.y);
				}
			}

		}


		void MouseEvent::OnMouseButtonRelease(SDL_MouseButtonEvent& e)
		{
			auto& engine = BaseApp::GetEngine();
			auto* windowsManager = engine.GetWindowManager();

			if (auto p = windowsManager->GetWindows().find(e.windowID); p != windowsManager->GetWindows().end())
			{

				GENGINE_CORE_INFO("Mouse button was release at {}. Window coords ({}, {})", p->second->GetTitle(), e.x, e.y);
			}

		}


		void MouseEvent::OnMouseMove(SDL_MouseMotionEvent& e)
		{
			auto& engine = BaseApp::GetEngine();
			auto* windowsManager = engine.GetWindowManager();
			if (auto p = windowsManager->GetWindows().find(e.windowID); p != windowsManager->GetWindows().end())
			{
				GENGINE_CORE_INFO("Mouse moves to the {}. xPos: {}   yPos: {}", p->second->GetTitle(), e.x, e.y);
			}

		}


		void MouseEvent::OnMouseWheel(SDL_MouseWheelEvent& e)
		{
			using namespace Input;

			auto& engine = BaseApp::GetEngine();
			auto* inputManager = engine.GetInputManager();
			auto& inputState = inputManager->GetInputState();

			inputState.m_Mouse.SetScrollWheel(Math::Vector2::Vector2(
				static_cast<float>(e.x),
				static_cast<float>(e.y)));
		}


	}

}