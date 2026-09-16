#pragma once

#include "Utility.h"
#include "Managers/InputManager.h"
#include "Managers/WindowManager.h"
#include "Managers/EventManager.h"
#include <thread>




namespace GEngine
{

		  
	class GEngine
	{

	public:

		//Mark GEngine is not copyable and not movable
		NONCOPYMOVABLE(GEngine);
		~GEngine() = default;
		static GEngine& Get();
		Manager::WindowManager* GetWindowManager() { return m_WindowManager.get(); }
		Manager::InputManager* GetInputManager() { return m_InputManager.get(); }
		Manager::EventManager* GetEventManager() { return m_EventManager.get(); }
		//void Run();
		void ShutDown();
		bool IsRunning()const { return m_Running; }

	private:
		friend class EngineContext;
		GEngine() = default;
		void Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList);
		// Only the owning root can initialize or release its platform.
		void ReleasePlatform();
		void GetEnvironmentInfo() const;

	private:
	
		ScopedPtr<Manager::InputManager> m_InputManager{};
		ScopedPtr<Manager::WindowManager> m_WindowManager{};
		ScopedPtr<Manager::EventManager> m_EventManager{};

		bool m_Running{ true };

	};

    class SDLWindow;
    class Actor;
    class CameraBase;
    class RenderTarget;
    struct RenderParam;

    // Application-owned service root. Declare before all application GPU owners
    // so it is destroyed last. One live root; all access stays on its owner thread.
    // Manager storage and the static renderer remain compatibility implementations
    // until their dedicated migration; this root owns their platform lifetime.
    class EngineContext final
    {
    public:
        EngineContext();
        ~EngineContext();
        NONCOPYMOVABLE(EngineContext);

        static EngineContext* TryGet() { return s_Current; }
        static EngineContext& Current();
        GEngine& LegacyEngine();
        void Initialize(const std::initializer_list<WindowProperties>& properties);
        SDLWindow* MainWindow() const { return m_MainWindow; }
        bool IsReady() const { return m_MainWindow != nullptr; }
        void MakeCurrent();
        void RenderScene(Actor* scene, CameraBase* camera, RenderTarget* target, const RenderParam& parameters);

    private:
        void RequireOwnerThread() const;
        void Release() noexcept;
        inline static EngineContext* s_Current = nullptr; // Non-owning compatibility lookup.
        const std::thread::id m_OwnerThread;
        GEngine m_LegacyEngine;
        SDLWindow* m_MainWindow = nullptr; // Borrowed from the owned WindowManager.
        bool m_InitializationAttempted = false;
        bool m_PlatformStarted = false;
    };


}
