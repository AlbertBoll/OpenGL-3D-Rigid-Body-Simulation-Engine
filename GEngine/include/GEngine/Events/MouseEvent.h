#pragma once


namespace GEngine
{
	namespace Event
	{


		class MouseEvent
		{

		public:
			static void OnMouseButtonClick(SDL_MouseButtonEvent& e);
			static void OnMouseButtonRelease(SDL_MouseButtonEvent& e);
			static void OnMouseMove(SDL_MouseMotionEvent& e);
			static void OnMouseWheel(SDL_MouseWheelEvent& e);
		};
	}

}
