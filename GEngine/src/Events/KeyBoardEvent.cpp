#include "gepch.h"
#include "Events/KeyboardEvent.h"


namespace GEngine::Event
{
	void KeyboardEvent::OnKeyPress(Manager::KeyboardParam& e)
	{
		GENGINE_CORE_INFO("Key {} was pressed", SDL_GetKeyName(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(e.Key))));
	}


	void KeyboardEvent::OnKeyRelease(Manager::KeyboardParam& e)
	{
		GENGINE_CORE_INFO("Key {} was released", SDL_GetKeyName(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(e.Key))));
	}


	void KeyboardEvent::OnKeyRepeat(Manager::KeyboardParam& e)
	{
		GENGINE_CORE_INFO("Key {} was repeated", SDL_GetKeyName(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(e.Key))));
	}
}
