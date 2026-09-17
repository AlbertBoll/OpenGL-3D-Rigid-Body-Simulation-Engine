#pragma once
#include "Managers/EventManager.h"


namespace GEngine
{
	namespace Event
	{


		class MouseEvent
		{

		public:
			static void OnMouseButtonClick(Manager::MouseButtonParam& e);
			static void OnMouseButtonRelease(Manager::MouseButtonParam& e);
			static void OnMouseMove(Manager::MouseMoveParam& e);
			static void OnMouseWheel(Manager::MouseScrollWheelParam& e);
		};
	}

}
