#pragma once
#include "Managers/EventManager.h"

namespace GEngine::Event
{


	class KeyboardEvent
	{

	public:
		static void OnKeyPress(Manager::KeyboardParam& e);
		static void OnKeyRelease(Manager::KeyboardParam& e);
		static void OnKeyRepeat(Manager::KeyboardParam& e);

	};


}
