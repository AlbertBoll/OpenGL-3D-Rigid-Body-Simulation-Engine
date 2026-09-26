#include "gepch.h"
#include "Core/GEngine.h"
#include "Managers/ShapeManager.h"
#include "Core/Renderer.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShaderManager.h"
#include "Windows/SDLWindow.h"
#include <string>




namespace GEngine
{

	SDL_DisplayMode mode;


	GEngine& GEngine::Get()
	{
		return EngineContext::Current().LegacyEngine();
	}

    EngineContext::EngineContext() : m_OwnerThread(std::this_thread::get_id())
    {
        if (!s_Current) s_Current = this;
        else m_State = State::Failed; // Initialize reports registration failure without acquiring ownership.
    }

    EngineContext::~EngineContext()
    {
        if (std::this_thread::get_id() != m_OwnerThread) std::terminate();
        Release();
        if (s_Current == this) s_Current = nullptr;
    }

    std::expected<EngineContext*, PlatformError> EngineContext::TryCurrent()
    {
        if (!s_Current) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
            "root lookup", "No live application EngineContext"});
        if (std::this_thread::get_id() != s_Current->m_OwnerThread)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
                "root lookup", "EngineContext access requires its owner thread"});
        return s_Current;
    }

    EngineContext& EngineContext::Current()
    {
        // This retained reference accessor is only for callbacks inside the owner lifetime.
        Asset::AssetDetail::RequireInvariant(s_Current != nullptr);
        s_Current->RequireOwnerThread();
        return *s_Current;
    }

    void EngineContext::RequireOwnerThread() const
    {
        GLContextThread::RequireOwner(m_OwnerThread, "legacy engine callback access");
    }

    GEngine& EngineContext::LegacyEngine()
    {
        RequireOwnerThread();
        return m_LegacyEngine;
    }

    PlatformResult EngineContext::RequireManagers()
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
                "manager access", "EngineContext access requires its owner thread"});
        if ((m_State != State::Ready && m_State != State::Initializing)
            || !m_MainWindow || !m_Assets || !m_Shaders || !m_Shapes)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
                "manager access", "EngineContext managers are not available"});
        // Compatibility access can follow a secondary window render. Caches still
        // belong to the main context; preserve its complete activation diagnostic.
        if (!m_MainWindow->IsCurrent())
            if (auto current = m_MainWindow->BeginRender(); !current) return current;
        if (!m_MainWindow->IsCurrent())
            return std::unexpected(PlatformError{PlatformErrorCode::ContextActivation,
                "manager access", "Unable to activate manager owning context"});
        return {};
    }

    namespace
    {
        std::string PlatformDiagnostic(const PlatformError& error)
        {
            return "Platform operation=" + error.operation + " code="
                + std::to_string(static_cast<unsigned>(error.code)) + ": " + error.message;
        }
    }

    std::expected<Manager::AssetsManager*, Asset::TextureError> EngineContext::TryAssets()
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            return std::unexpected(Asset::TextureError{Asset::TextureErrorCode::ContextUnavailable, {}, "Texture services require the owner thread"});
        if ((m_State != State::Ready && m_State != State::Initializing) || !m_Assets || !m_MainWindow)
            return std::unexpected(Asset::TextureError{Asset::TextureErrorCode::ContextUnavailable, {}, "Texture services are unavailable"});
        if (auto current = m_MainWindow->BeginRender(); !current)
            return std::unexpected(Asset::TextureError{Asset::TextureErrorCode::ContextUnavailable, {}, PlatformDiagnostic(current.error())});
        return m_Assets.get();
    }

    std::expected<Manager::ShaderManager*, Asset::ShaderError> EngineContext::Shaders()
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            return std::unexpected(Asset::ShaderError{Asset::ShaderErrorCode::WrongThread, {}, {}, "Shader services require the owner thread"});
        if ((m_State != State::Ready && m_State != State::Initializing) || !m_Shaders || !m_MainWindow)
            return std::unexpected(Asset::ShaderError{Asset::ShaderErrorCode::ContextUnavailable, {}, {}, "Shader services are unavailable"});
        if (auto current = m_MainWindow->BeginRender(); !current)
            return std::unexpected(Asset::ShaderError{Asset::ShaderErrorCode::ContextUnavailable, {}, {}, PlatformDiagnostic(current.error())});
        return m_Shaders.get();
    }
    std::expected<Manager::ShapeManager*, PlatformError> EngineContext::TryShapes()
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
                "shape registration", "EngineContext access requires its owner thread"});
        if ((m_State != State::Ready && m_State != State::Initializing) || !m_Shapes || !m_MainWindow)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
                "shape registration", "Shape services are unavailable"});
        if (!m_MainWindow->IsCurrent())
            if (auto current = m_MainWindow->BeginRender(); !current) return std::unexpected(current.error());
        return m_Shapes.get();
    }

    std::expected<Manager::AssetsManager*, PlatformError> EngineContext::Assets()
    {
        if (auto ready = RequireManagers(); !ready) return std::unexpected(ready.error());
        return m_Assets.get();
    }
    std::expected<Manager::ShapeManager*, PlatformError> EngineContext::Shapes()
    {
        if (auto ready = RequireManagers(); !ready) return std::unexpected(ready.error());
        return m_Shapes.get();
    }
    std::expected<Asset::AssetPublication*, PlatformError> EngineContext::AssetPublications()
    {
        if (auto ready = RequireManagers(); !ready) return std::unexpected(ready.error());
        return &m_AssetPublication;
    }

    std::expected<SceneResourceServices, PlatformError> EngineContext::SceneServices()
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
                "scene resources", "Scene resources require the owner thread"});
        if (m_State != State::Ready || !m_MainWindow || !m_Shapes || !m_Assets)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
                "scene resources", "Scene resources require the initialized owner context"});
        if (auto current = m_MainWindow->BeginRender(); !current)
            return std::unexpected(current.error());
        return SceneResourceServices{m_AssetPublication, *m_Shapes};
    }

    EngineInitializationResult EngineContext::Initialize(const std::initializer_list<WindowProperties>& properties)
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
                "platform initialization", "Initialization requires the owner thread"});
        if (!Log::GetCoreLogger()) Log::Initialize();
        if (s_Current != this) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
            "root registration", "Only one live EngineContext is supported"});
        if (m_InitializationAttempted) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
            "platform initialization", "Initialization may only be attempted once"});
        m_InitializationAttempted = true;
        m_PlatformStarted = true;
        m_State = State::Initializing;
        struct Rollback
        {
            EngineContext* owner; bool committed = false;
            ~Rollback() { if (!committed) { owner->Release(); owner->m_State = State::Failed; } }
        } rollback{this};
        if (auto initialized = m_LegacyEngine.Initialize(properties); !initialized) return std::unexpected(initialized.error());
        auto& windows = m_LegacyEngine.GetWindowManager()->GetWindows();
        m_MainWindow = std::min_element(windows.begin(), windows.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; })->second.get();
        if (auto current = m_MainWindow->BeginRender(); !current) return std::unexpected(current.error());
        auto images = RuntimeAssets::TryFile("Images");
        if (!images) return std::unexpected(images.error());
        m_Assets.reset(new Manager::AssetsManager(m_AssetPublication, *images));
        m_Shaders.reset(new Manager::ShaderManager);
        m_Shapes.reset(new Manager::ShapeManager);
        if (auto shapes = m_Shapes->Initialize(); !shapes) return std::unexpected(shapes.error());
        m_State = State::Ready;
        rollback.committed = true;
        return {};
    }

    PlatformResult EngineContext::MakeCurrent()
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
                "activate platform context", "Context activation requires the owner thread"});
        if (!IsReady()) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
            "activate platform context", "Rendering services are not initialized"});
        return m_MainWindow->BeginRender();
    }

    PlatformResult EngineContext::RenderScene(Actor* scene, CameraBase* camera, RenderTarget* target, const RenderParam& parameters)
    {
        if (auto current = MakeCurrent(); !current) return current;
        Renderer::RenderBegin(camera, target);
        Renderer::Set(parameters);
        Renderer::RenderScene(scene, camera);
        return {};
    }

    void EngineContext::Release() noexcept
    {
        if (!m_PlatformStarted) return;
        m_State = State::Releasing;
        // Initialization can fail before MainWindow is published, with earlier
        // windows and shared resources already owned by the compatibility engine.
        if (auto* manager = m_LegacyEngine.GetWindowManager(); manager && !manager->GetWindows().empty())
        {
            auto& windows = manager->GetWindows();
            auto* window = static_cast<SDLWindow*>(std::min_element(windows.begin(), windows.end(),
                [](const auto& left, const auto& right) { return left.first < right.first; })->second.get());
            if (auto current = window->BeginRender(); !current) { ReportPlatformError(current.error()); std::terminate(); }
        }
        m_Assets.reset();
        m_Shaders.reset();
        m_Shapes.reset();
        m_AssetPublication.RequireDrained();
        m_MainWindow = nullptr;
        m_LegacyEngine.ReleasePlatform();
        m_PlatformStarted = false;
        m_State = State::Stopped;
    }

	void GEngine::GetEnvironmentInfo()const
	{
		#if defined(GENGINE_CONFIG_DEBUG)
				GENGINE_CORE_INFO("Configuration: DEBUG");
		
		#elif defined(GENGINE_CONFIG_RELEASE)
				GENGINE_CORE_INFO("Configuration: RELEASE");
		
		#endif
		
		#if defined(GENGINE_PLATFORM_WINDOWS)
				GENGINE_CORE_INFO("Platform: WINDOWS");
		
		#elif defined(GENGINE_PLATFORM_MAC)
				GENGINE_CORE_INFO("Platform: MACOSX");
		
		#else
				GENGINE_CORE_INFO("Platform: LINUX");
		
		#endif
	}

	PlatformResult GEngine::Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)
	{
			if (WindowsPropertyList.size() == 0)
				return std::unexpected(PlatformError{PlatformErrorCode::InvalidState, "platform initialization", "A main window is required"});
			m_Running = true;
			// Logging is process-owned; repeated platform lifetimes reuse its loggers.
			if (!Log::GetCoreLogger()) Log::Initialize();
			GENGINE_CORE_INFO("Initialize Logging...");
			GENGINE_CORE_INFO("GEngine v{}.{}", 1, 0);

			//Get environment Info
			GetEnvironmentInfo();

			//Initialize SDL
			GENGINE_CORE_INFO("Initialize SDL");


			//int code = SDL_Init(SDL_INIT_EVERYTHING);
			int code = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);

			if (code != 0)
				return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "platform initialization", SDL_GetError()});

			SDL_version version{};
			SDL_VERSION(&version);

			GENGINE_CORE_INFO("SDL {}.{}.{}", (uint32_t)version.major, (uint32_t)version.minor, (uint32_t)version.patch);

			mode = {};
			if (SDL_GetDesktopDisplayMode(0, &mode) != 0)
				return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "display query", SDL_GetError()});
			GENGINE_CORE_INFO("Display width: {}. Display height: {}. Refresh Rate: {}", mode.w, mode.h, mode.refresh_rate);

			GENGINE_CORE_INFO("Initialize Window Manager...");
			
			
			m_WindowManager = Manager::WindowManager::GetScopedInstance();

		    
			if (auto added = GetWindowManager()->AddWindows(WindowsPropertyList); !added) return added;
			// Shared engine resources belong to the first application's GL context.
			auto& windows = GetWindowManager()->GetWindows();
			if (auto current = std::min_element(windows.begin(), windows.end(),
				[](const auto& left, const auto& right) { return left.first < right.first; })->second->BeginRender(); !current) return current;
			


			GENGINE_CORE_INFO("Initialize Input Manager...");
			
			m_InputManager = Manager::InputManager::GetScopedInstance();
			if (auto input = m_InputManager->Initialize(); !input) return input;

			////m_WindowManager->GetInternalWindow(1)->BeginRender();

			GENGINE_CORE_INFO("Initialize Event Manager...");

			m_EventManager = Manager::EventManager::GetScopedInstance();
			m_EventManager->Initialize();
		

			

			GENGINE_CORE_INFO("Initialize font...");

			if (TTF_Init() != 0)
			{
				return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "font subsystem", TTF_GetError()});
			}
			
			
            return {};
	}


	//void GEngine::Run()
	//{
	//	
	//	SDL_Event event;

	//	while (m_Running) {

	//		m_EventManager->OnEvent(event);

	//		

	//		for (auto& p : m_WindowManager->GetWindows())
	//		{

	//			p.second->BeginRender();
	//			//p.second->EndRender();
	//		}
	//	}


	//}


	void GEngine::ShutDown()
	{
		m_Running = false;
	}

	void GEngine::ReleasePlatform()
	{
		// Called after application destruction; retire callbacks before their platform.
		ShutDown();
		m_EventManager.reset();
		// Window owners retire ImGui, GL contexts and native windows in that order.
		m_WindowManager.reset();
		if (m_InputManager) m_InputManager->ShutDown();
		m_InputManager.reset();
		if (TTF_WasInit()) TTF_Quit();
		if (SDL_WasInit(0)) SDL_Quit();
	}


}
