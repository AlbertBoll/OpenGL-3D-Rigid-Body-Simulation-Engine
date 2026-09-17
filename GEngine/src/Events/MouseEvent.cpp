#include "gepch.h"
#include "Events/MouseEvent.h"
#include "Core/BaseApp.h"

namespace GEngine
{

	namespace Event
	{

		void MouseEvent::OnMouseButtonClick(Manager::MouseButtonParam& e)
		{
			auto& engine = BaseApp::GetEngine();
			auto* windowsManager = engine.GetWindowManager();
			if (auto p = windowsManager->GetWindows().find(e.ID); p != windowsManager->GetWindows().end())
			{
				if (e.Button == SDL_BUTTON_LEFT)
				{
					if (e.Clicks == 1)
						GENGINE_CORE_INFO("Left mouse button was clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.X, e.Y);
					else
						GENGINE_CORE_INFO("Left mouse button was double clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.X, e.Y);
				}

				else if (e.Button == SDL_BUTTON_MIDDLE)
				{
					if (e.Clicks == 1)
						GENGINE_CORE_INFO("Middle mouse was clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.X, e.Y);
					else
						GENGINE_CORE_INFO("Middle mouse was double clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.X, e.Y);
				}

				else
				{
					if (e.Clicks == 1)
						GENGINE_CORE_INFO("Right mouse was clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.X, e.Y);
					else
						GENGINE_CORE_INFO("Right mouse was double clicked at {}. Window coords ({}, {})", p->second->GetTitle(), e.X, e.Y);
				}
			}

		}


		void MouseEvent::OnMouseButtonRelease(Manager::MouseButtonParam& e)
		{
			auto& engine = BaseApp::GetEngine();
			auto* windowsManager = engine.GetWindowManager();

			if (auto p = windowsManager->GetWindows().find(e.ID); p != windowsManager->GetWindows().end())
			{

				GENGINE_CORE_INFO("Mouse button was release at {}. Window coords ({}, {})", p->second->GetTitle(), e.X, e.Y);
			}

		}


		void MouseEvent::OnMouseMove(Manager::MouseMoveParam& e)
		{
			auto& engine = BaseApp::GetEngine();
			auto* windowsManager = engine.GetWindowManager();
			if (auto p = windowsManager->GetWindows().find(e.ID); p != windowsManager->GetWindows().end())
			{
				GENGINE_CORE_INFO("Mouse moves to the {}. xPos: {}   yPos: {}", p->second->GetTitle(), e.XPos, e.YPos);
			}

		}


		void MouseEvent::OnMouseWheel(Manager::MouseScrollWheelParam& e)
		{
			using namespace Input;

			auto& engine = BaseApp::GetEngine();
			auto* inputManager = engine.GetInputManager();
			auto& inputState = inputManager->GetInputState();

			inputState.m_Mouse.SetScrollWheel(Math::Vector2::Vector2(
				static_cast<float>(e.X),
				static_cast<float>(e.Y)));
		}


	}

}