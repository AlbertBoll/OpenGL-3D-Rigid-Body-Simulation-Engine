#include "gepch.h"
#include "Core/GEngine.h"
#include "Managers/ShapeManager.h"
#include "Core/Renderer.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShaderManager.h"
#include "Windows/SDLWindow.h"
#include <stdexcept>




namespace GEngine
{

	SDL_DisplayMode mode;


	GEngine& GEngine::Get()
	{
		return EngineContext::Current().LegacyEngine();
	}

    EngineContext::EngineContext() : m_OwnerThread(std::this_thread::get_id())
    {
        if (s_Current) throw std::logic_error("Only one live EngineContext is supported");
        s_Current = this;
    }

    EngineContext::~EngineContext()
    {
        if (std::this_thread::get_id() != m_OwnerThread) std::terminate();
        Release();
        s_Current = nullptr;
    }

    EngineContext& EngineContext::Current()
    {
        if (!s_Current) throw std::logic_error("No live application EngineContext");
        s_Current->RequireOwnerThread();
        return *s_Current;
    }

    void EngineContext::RequireOwnerThread() const
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            throw std::logic_error("EngineContext access requires its owner thread");
    }

    GEngine& EngineContext::LegacyEngine()
    {
        RequireOwnerThread();
        return m_LegacyEngine;
    }

    void EngineContext::RequireManagers()
    {
        RequireOwnerThread();
        if ((m_State != State::Ready && m_State != State::Initializing)
            || !m_MainWindow || !m_Assets || !m_Shaders || !m_Shapes)
            throw std::logic_error("EngineContext managers are not available");
        // Compatibility access may follow a secondary window render. Every cache
        // operation still belongs to the main owning context, including uploads.
        if (SDL_GL_GetCurrentContext() != m_MainWindow->GetContext()) m_MainWindow->BeginRender();
        if (SDL_GL_GetCurrentContext() != m_MainWindow->GetContext())
            throw std::runtime_error("Unable to activate manager owning context");
    }

    Manager::AssetsManager& EngineContext::Assets() { RequireManagers(); return *m_Assets; }
    Manager::ShaderManager& EngineContext::Shaders() { RequireManagers(); return *m_Shaders; }
    Manager::ShapeManager& EngineContext::Shapes() { RequireManagers(); return *m_Shapes; }

    void EngineContext::Initialize(const std::initializer_list<WindowProperties>& properties)
    {
        RequireOwnerThread();
        if (m_InitializationAttempted) throw std::logic_error("EngineContext initialization may only be attempted once");
        m_InitializationAttempted = true;
        m_PlatformStarted = true;
        m_State = State::Initializing;
        try
        {
            m_LegacyEngine.Initialize(properties);
            auto& windows = m_LegacyEngine.GetWindowManager()->GetWindows();
            m_MainWindow = static_cast<SDLWindow*>(std::min_element(windows.begin(), windows.end(),
                [](const auto& left, const auto& right) { return left.first < right.first; })->second.get());
            m_MainWindow->BeginRender();
            if (SDL_GL_GetCurrentContext() != m_MainWindow->GetContext())
                throw std::runtime_error("Unable to initialize manager owning context");
            m_Assets.reset(new Manager::AssetsManager);
            m_Shaders.reset(new Manager::ShaderManager);
            m_Shapes.reset(new Manager::ShapeManager);
            m_Shapes->Initialize();
            m_State = State::Ready;
        }
        catch (...)
        {
            Release();
            m_State = State::Failed;
            throw;
        }
    }

    void EngineContext::MakeCurrent()
    {
        RequireOwnerThread();
        if (!IsReady()) throw std::logic_error("EngineContext rendering services are not initialized");
        m_MainWindow->BeginRender();
    }

    void EngineContext::RenderScene(Actor* scene, CameraBase* camera, RenderTarget* target, const RenderParam& parameters)
    {
        MakeCurrent();
        Renderer::RenderBegin(camera, target);
        Renderer::Set(parameters);
        Renderer::RenderScene(scene, camera);
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
            window->BeginRender();
            if (SDL_GL_GetCurrentContext() != window->GetContext()) std::terminate();
        }
        m_Assets.reset();
        m_Shaders.reset();
        m_Shapes.reset();
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

	void GEngine::Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)
	{
			if (WindowsPropertyList.size() == 0)
				throw std::invalid_argument("GEngine requires a main window");
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
				throw std::runtime_error(std::string("SDL initialization failed: ") + SDL_GetError());

			SDL_version version{};
			SDL_VERSION(&version);

			GENGINE_CORE_INFO("SDL {}.{}.{}", (uint32_t)version.major, (uint32_t)version.minor, (uint32_t)version.patch);

			mode = {};
			if (SDL_GetDesktopDisplayMode(0, &mode) != 0)
				throw std::runtime_error(std::string("SDL display query failed: ") + SDL_GetError());
			GENGINE_CORE_INFO("Display width: {}. Display height: {}. Refresh Rate: {}", mode.w, mode.h, mode.refresh_rate);

			GENGINE_CORE_INFO("Initialize Window Manager...");
			
			
			m_WindowManager = Manager::WindowManager::GetScopedInstance();

		    
			GetWindowManager()->AddWindows(WindowsPropertyList);
			// Shared engine resources belong to the first application's GL context.
			auto& windows = GetWindowManager()->GetWindows();
			std::min_element(windows.begin(), windows.end(),
				[](const auto& left, const auto& right) { return left.first < right.first; })->second->BeginRender();
			


			GENGINE_CORE_INFO("Initialize Input Manager...");
			
			m_InputManager = Manager::InputManager::GetScopedInstance();
			m_InputManager->Initialize();

			////m_WindowManager->GetInternalWindow(1)->BeginRender();

			GENGINE_CORE_INFO("Initialize Event Manager...");

			m_EventManager = Manager::EventManager::GetScopedInstance();
			m_EventManager->Initialize();
		

			

			GENGINE_CORE_INFO("Initialize font...");

			if (TTF_Init() != 0)
			{
				throw std::runtime_error(std::string("SDL_ttf initialization failed: ") + TTF_GetError());
			}
			
			
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
