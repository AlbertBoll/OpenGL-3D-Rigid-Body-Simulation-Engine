#pragma once
#include "Events/Event.h"
#include "Managers/ManagerBase.h"

union SDL_Event;

namespace GEngine
{
	namespace Manager
	{
		struct MouseScrollWheelParam
		{
			unsigned int ID;
			float X;
			float Y;
		};


		struct MouseMoveParam
		{
			unsigned int ID;
			int32_t XPos;
			int32_t YPos;
			int32_t XRel;
			int32_t YRel;
		};

		struct MouseButtonParam
		{

			unsigned int ID;
			int32_t X;
			int32_t Y;
			uint8_t Button;
			uint8_t Clicks;
		};

		struct WindowCloseParam
		{

			unsigned int ID;
		};


		struct WindowResizeParam
		{
			unsigned int ID;
			int Width;
			int Height;
		};


		class EventManager : ManagerBase<EventManager>
		{
			friend class ManagerBase<EventManager>;


		public:
			EventManager() = default;
			static ScopedPtr<EventManager> GetScopedInstance();
			void Initialize();
			void OnEvent(SDL_Event& e);
			EventDispatcher& GetEventDispatcher() { return m_EventDispatcher; }

		private:
			friend class GEngine;
			//EventManager() = default;

		private:
			EventDispatcher m_EventDispatcher;


		};

	}

}