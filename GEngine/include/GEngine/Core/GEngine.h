#pragma once
#include "Assets/Shaders/Shader.h"

#include "Core/Utility.h"
#include "Managers/InputManager.h"
#include "Managers/WindowManager.h"
#include "Managers/EventManager.h"
#include "Managers/ShapeManager.h"
#include "Assets/AssetPublication.h"
#include "Assets/Textures/Texture.h"
#include <thread>




namespace GEngine
{

    namespace Manager { class AssetsManager; class ShaderManager; class ShapeManager; }
    struct SceneResourceServices
    {
        Asset::AssetPublication& publication;
        Manager::ShapeManager& shapes;
    };

		  
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
		[[nodiscard]] PlatformResult Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList);
		// Only the owning root can initialize or release its platform.
		void ReleasePlatform();
		void GetEnvironmentInfo() const;

	private:
	
		ScopedPtr<Manager::InputManager> m_InputManager{};
		ScopedPtr<Manager::WindowManager> m_WindowManager{};
		ScopedPtr<Manager::EventManager> m_EventManager{};

		bool m_Running{ true };

	};

    class Window;
    class Actor;
    class CameraBase;
    class RenderTarget;
    struct RenderParam;

    // Application-owned service root. Declare before all application GPU owners
    // so it is destroyed last. One live root; all access stays on its owner thread.
    // Static manager accessors borrow these owned services. The renderer facade
    // remains transitional; no process-global cache owns rendering resources.
    using EngineInitializationError = std::variant<PlatformError, ShapeRegistrationError>;
    using EngineInitializationResult = std::expected<void, EngineInitializationError>;

    class EngineContext final
    {
    public:
        EngineContext();
        ~EngineContext();
        NONCOPYMOVABLE(EngineContext);

        enum class State { Uninitialized, Initializing, Ready, Releasing, Stopped, Failed };
        State GetState() const { return m_State; }

        static EngineContext* TryGet() { return s_Current; }
        [[nodiscard]] static std::expected<EngineContext*, PlatformError> TryCurrent();
        // Legacy callbacks require a live root on its owner thread. Fallible callers use TryCurrent.
        static EngineContext& Current();
        GEngine& LegacyEngine();
        [[nodiscard]] EngineInitializationResult Initialize(const std::initializer_list<WindowProperties>& properties);
        Window* MainWindow() const { return m_MainWindow; }
        bool IsReady() const { return m_State == State::Ready; }
        [[nodiscard]] std::expected<Manager::AssetsManager*, PlatformError> Assets();
        [[nodiscard]] std::expected<Manager::ShaderManager*, Asset::ShaderError> Shaders();
        [[nodiscard]] std::expected<Manager::ShapeManager*, PlatformError> Shapes();
        [[nodiscard]] std::expected<Asset::AssetPublication*, PlatformError> AssetPublications();
        [[nodiscard]] std::expected<SceneResourceServices, PlatformError> SceneServices();
        [[nodiscard]] PlatformResult MakeCurrent();
        [[nodiscard]] PlatformResult RenderScene(Actor* scene, CameraBase* camera, RenderTarget* target, const RenderParam& parameters);

    private:
        friend class Manager::AssetsManager;
        friend class Manager::ShapeManager;
        std::expected<Manager::ShapeManager*, PlatformError> TryShapes();
        std::expected<Manager::AssetsManager*, Asset::TextureError> TryAssets();
        void RequireOwnerThread() const;
        [[nodiscard]] PlatformResult RequireManagers();
        void Release() noexcept;
        inline static EngineContext* s_Current = nullptr; // Non-owning compatibility lookup.
        const std::thread::id m_OwnerThread;
        Asset::AssetPublication m_AssetPublication;
        GEngine m_LegacyEngine;
        ScopedPtr<Manager::AssetsManager> m_Assets;
        ScopedPtr<Manager::ShaderManager> m_Shaders;
        ScopedPtr<Manager::ShapeManager> m_Shapes;
        Window* m_MainWindow = nullptr; // Borrowed from the owned WindowManager.
        State m_State = State::Uninitialized;
        bool m_InitializationAttempted = false;
        bool m_PlatformStarted = false;
    };


}
