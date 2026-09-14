#pragma once

#include "Utility.h"
#include "Managers/InputManager.h"
#include "Managers/WindowManager.h"
#include "Managers/EventManager.h"




namespace GEngine
{

		  
	class GEngine
	{

	public:

		//Mark GEngine is not copyable and not movable
		NONCOPYMOVABLE(GEngine);
		~GEngine() = default;
		static GEngine& Get();
		void Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList);
		Manager::WindowManager* GetWindowManager() { return m_WindowManager.get(); }
		Manager::InputManager* GetInputManager() { return m_InputManager.get(); }
		Manager::EventManager* GetEventManager() { return m_EventManager.get(); }
		//void Run();
		void ShutDown();
		// Entry point calls this after all application/resource destructors finish.
		void ReleasePlatform();
		bool IsRunning()const { return m_Running; }

	private:
		friend class BaseApp;
		GEngine() = default;
		void GetEnvironmentInfo() const;

	private:
	
		ScopedPtr<Manager::InputManager> m_InputManager{};
		ScopedPtr<Manager::WindowManager> m_WindowManager{};
		ScopedPtr<Manager::EventManager> m_EventManager{};

		bool m_Running{ true };

	};


}
