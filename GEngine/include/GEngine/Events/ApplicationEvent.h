#pragma once
#include "Managers/EventManager.h"


namespace GEngine::Event
{

	class ApplicationEvent
	{

	public:
		static void OnWindowClose(WindowStateEvent& e);
		static void OnWindowResize(WindowStateEvent& e);
		static void OnAppQuit();

	};

}
