
#include "gepch.h"
#include "Core/RenderBaseline.h"
#include "Windows/SDLWindow.h"
#include "Core/GLDebug.h"
#include "Windows/ImGuiWindow.h"
#include <imgui/imgui.h>
//#include <Core/Renderer.h>
#include "Core/BaseApp.h"
#include <new>
#include <limits>

namespace GEngine
{

	extern SDL_DisplayMode mode;
	

	SDLWindow::~SDLWindow()
	{
		ShutDown();
	}

	PlatformResult SDLWindow::Initialize(const WindowProperties& winProp)
	{
		GLContextThread::RequireOwner(m_OwnerThread, "SDLWindow::Initialize");
		if (m_Window || m_Context || m_ImGuiWindow)
			return std::unexpected(PlatformError{PlatformErrorCode::InvalidState, "window initialization", "Window is already initialized"});
        if (!winProp.m_Width || !winProp.m_Height || winProp.m_Width > INT_MAX || winProp.m_Height > INT_MAX
            || winProp.m_MinWidth > INT_MAX || winProp.m_MinHeight > INT_MAX)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidSize, "window initialization", "Invalid native logical extent"});
        struct Rollback { SDLWindow* owner; bool committed = false; ~Rollback() { if (!committed) owner->ShutDown(); } } rollback{this};
		uint32_t flag = GetWindowFlag(winProp);
	

		// Set OpenGL attributes
		// Use the core OpenGL profile
		//SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
        if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});
		// Specify version 4.6
        if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});
        if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});
		GLDebug::ConfigureContext();
		// Request a color buffer with 8-bits per RGBA channel
        if (SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});
        if (SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});
        if (SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});
        if (SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});
		// Enable double buffering
        if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});
        if (SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});

		// Force OpenGL to use hardware acceleration
        if (SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});

        if (SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});
        if (SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 16) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context attributes", SDL_GetError()});


		//create SDL window
		//GENGINE_CORE_INFO(SDL_WINDOWPOS_CENTERED);
		//m_Window = SDL_CreateWindow(winProp.m_Title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winProp.m_Width, winProp.m_Height, flag);
		//m_Window = SDL_CreateWindow(winProp.m_Title.c_str(), winProp.m_TopLeftX, winProp.m_TopLeftY, winProp.m_Width, winProp.m_Height, flag);
		//m_Window = SDL_CreateWindow(winProp.m_Title.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, winProp.m_Width, winProp.m_Height, flag);

		SetWindow(mode.w, mode.h, flag, winProp);
		
		if (!m_Window)
			return std::unexpected(PlatformError{PlatformErrorCode::WindowCreation, "window creation", SDL_GetError()});

		m_ScreenWidth = winProp.m_Width;
		m_ScreenHeight = winProp.m_Height;
		m_AspectRatio = winProp.m_AspectRatio;

		//Set Window Minimum Size
		SDL_SetWindowMinimumSize(m_Window, winProp.m_MinWidth, winProp.m_MinHeight);

		m_Context = GLDebug::CreateContext(m_Window);
		if (!m_Context)
			return std::unexpected(PlatformError{PlatformErrorCode::ContextCreation, "context creation", SDL_GetError()});

		SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "1", SDL_HINT_OVERRIDE);
		//SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_SCALING, "1", SDL_HINT_OVERRIDE);

		int success = gladLoadGL();

		//Load OpenGL Context
		if (!success)
			return std::unexpected(PlatformError{PlatformErrorCode::FunctionLoading, "graphics functions", "Graphics functions could not be loaded"});
		GLDebug::Initialize();
		const GLDebug::Group initialization("Window initialization");

		//winProp::SetCornFlowerBlue();
		glClearColor(winProp.m_Red, winProp.m_Green, winProp.m_Blue, 1.0f);
		
		////Enable depth test
		//glEnable(GL_DEPTH);
		//glDepthFunc(GL_LEQUAL);

		////Enable blending
		//glEnable(GL_BLEND);
		//glEnable(GL_MULTISAMPLE);
		//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		

		// Apply both choices explicitly; the driver/context default is not policy.
		// The application loop uses the actual interval if this request fails.
		if (SDL_GL_SetSwapInterval(winProp.m_IsVsync ? 1 : 0) != 0)
			GENGINE_CORE_WARN("Swap interval request failed: {} (actual {})",
				SDL_GetError(), SDL_GL_GetSwapInterval());

		m_ImGuiWindow = new (std::nothrow) ImGuiWindow_();
        if (!m_ImGuiWindow) return std::unexpected(PlatformError{PlatformErrorCode::Allocation, "UI allocation", "UI owner allocation failed"});
        if (auto initialized = m_ImGuiWindow->Initialize(this, winProp.ImGuiWindowProperties); !initialized) return initialized;
        RefreshDimensions();
        rollback.committed = true;
        return {};
		

	}

	void SDLWindow::SwapBuffer()
	{
		GLContextThread::RequireOwner(m_OwnerThread, "SDLWindow::SwapBuffer");
		GLContextThread::AssertCurrent("SDLWindow::SwapBuffer");
#ifdef GENGINE_RENDER_BASELINE
        RenderBaseline::Capture(m_Window);
#endif
		SDL_GL_SwapWindow(m_Window);
	}

	void SDLWindow::ShutDown() 
	{
		GLContextThread::RequireOwner(m_OwnerThread, "SDLWindow::ShutDown");
		auto* previousWindow = SDL_GL_GetCurrentWindow();
		const auto previousContext = SDL_GL_GetCurrentContext();
		const bool restorePrevious = previousContext && previousContext != m_Context;
		// ImGui's GL backend needs the owning context and SDL window during cleanup.
		if (m_Window && m_Context && SDL_GL_MakeCurrent(m_Window, m_Context) != 0)
		{
			// Continuing would delete GPU resources against the wrong context.
			std::fprintf(stderr, "Cannot make the owning shutdown context current: %s\n", SDL_GetError());
			std::terminate();
		}
		if (m_ImGuiWindow)
		{
			// No diagnostic GL calls before the loader succeeded on partial init.
			const GLDebug::Group teardown("Window teardown");
			delete m_ImGuiWindow;
		}
		m_ImGuiWindow = nullptr;
		if (m_Context) FreeContext();
		if (m_Window) SDL_DestroyWindow(m_Window);
		m_Window = nullptr;
        m_ScreenWidth = m_ScreenHeight = 0; m_PixelSize = {};
		if (restorePrevious) SDL_GL_MakeCurrent(previousWindow, previousContext);
	}

	void SDLWindow::SetTitle(const std::string& title) const
	{
		SDL_SetWindowTitle(m_Window, title.c_str());
	}

	ImGuiWindow_* SDLWindow::GetImGuiWindow() const
	{
		return m_ImGuiWindow;
	}

	uint32_t SDLWindow::GetWindowID() const
	{
		ASSERT(m_Window, "Window can not be null!")
		return SDL_GetWindowID(m_Window);
	}

	void SDLWindow::SetWindow(int DisplayWidth, int DisplayHeight, uint32_t flag, const WindowProperties& winProp)
	{

		int topLeftPosX{};
		int topLeftPosY{};

		switch (winProp.m_WinPos)
		{
		case WindowPos::TopLeft:
			topLeftPosX = DisplayWidth / 2 - winProp.m_Width -   winProp.m_XPaddingToCenterY;
			topLeftPosY = DisplayHeight / 2 - winProp.m_Height - winProp.m_YPaddingToCenterX;
			break;
		
		case WindowPos::TopRight:
			topLeftPosX = DisplayWidth / 2  + winProp.m_XPaddingToCenterY;
			topLeftPosY = DisplayHeight / 2 - winProp.m_Height - winProp.m_YPaddingToCenterX;
			break;

		case WindowPos::ButtomLeft:
			topLeftPosX = DisplayWidth / 2 - winProp.m_Width - winProp.m_XPaddingToCenterY;
			topLeftPosY = DisplayHeight / 2 + winProp.m_YPaddingToCenterX;
			break;

		case WindowPos::ButtomRight:
			topLeftPosX = DisplayWidth / 2  + winProp.m_XPaddingToCenterY;
			topLeftPosY = DisplayHeight / 2 + winProp.m_YPaddingToCenterX;
			break;

		case WindowPos::Center:
			topLeftPosX = DisplayWidth / 2 - winProp.m_Width / 2;
			topLeftPosY = DisplayHeight / 2 - winProp.m_Height / 2;
			break;

		}

		m_Window = SDL_CreateWindow(winProp.m_Title.c_str(), topLeftPosX, topLeftPosY, winProp.m_Width, winProp.m_Height, flag);


	}


	uint32_t SDLWindow::GetWindowFlag(const WindowProperties& winProp)
	{
		uint32_t flag = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;


		if (winProp.flag.IsSet(WindowFlags::INVISIBLE))
		{
			flag |= SDL_WINDOW_HIDDEN;
		}

		if (winProp.flag.IsSet(WindowFlags::FULLSCREEN))
		{
			flag |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		}

		if (winProp.flag.IsSet(WindowFlags::BORDERLESS))
		{
			flag |= SDL_WINDOW_BORDERLESS;
		}

		if (winProp.flag.IsSet(WindowFlags::RESIZABLE))
		{
			flag |= SDL_WINDOW_RESIZABLE;
		}

		return flag;
	}

	PlatformResult SDLWindow::BeginRender()
	{
		GLContextThread::RequireOwner(m_OwnerThread, "SDLWindow::BeginRender");
		if (!m_Context || SDL_GL_MakeCurrent(m_Window, m_Context) != 0)
			return std::unexpected(PlatformError{PlatformErrorCode::ContextActivation, "activate window", SDL_GetError()});
		if (m_ImGuiWindow) ImGui::SetCurrentContext(m_ImGuiWindow->GetContext());
		return {};
		//glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}

	PlatformResult SDLWindow::NullRender()
	{
		GLContextThread::RequireOwner(m_OwnerThread, "SDLWindow::NullRender");
		if (SDL_GL_MakeCurrent(nullptr, nullptr) != 0)
            return std::unexpected(PlatformError{PlatformErrorCode::ContextActivation, "detach context", SDL_GetError()});
        return {};
	}


	void SDLWindow::EndRender(BaseApp* app)
	{
		//m_ImGuiWindow->BeginRender(this);
		//app->ImGuiRender();
		//m_ImGuiWindow->EndRender(this);
		SwapBuffer();
	}


	std::string SDLWindow::GetTitle() const
	{
		return SDL_GetWindowTitle(m_Window);
	}


    void SDLWindow::RefreshDimensions()
    {
        GLContextThread::RequireOwner(m_OwnerThread, "window dimensions");
        int w = 0, h = 0, pw = 0, ph = 0;
        if (m_Window) { SDL_GetWindowSize(m_Window, &w, &h); SDL_GL_GetDrawableSize(m_Window, &pw, &ph); }
        m_ScreenWidth = static_cast<unsigned>(std::max(0, w));
        m_ScreenHeight = static_cast<unsigned>(std::max(0, h));
        m_PixelSize = {static_cast<unsigned>(std::max(0, pw)), static_cast<unsigned>(std::max(0, ph))};
    }
    WindowState SDLWindow::GetState() const
    {
        GLContextThread::RequireOwner(m_OwnerThread, "window state");
        const auto flags = m_Window ? SDL_GetWindowFlags(m_Window) : 0;
        return {(flags & SDL_WINDOW_MINIMIZED) != 0, (flags & SDL_WINDOW_HIDDEN) != 0};
    }
    bool SDLWindow::IsCurrent() const { return m_Context && SDL_GL_GetCurrentContext() == m_Context; }
    int SDLWindow::GetSwapInterval() const
    {
        GLContextThread::RequireCurrent("swap interval query");
        return SDL_GL_GetSwapInterval();
    }
    void SDLWindow::SetMouseGrab(bool grabbed)
    {
        GLContextThread::RequireOwner(m_OwnerThread, "mouse grab");
        if (m_Window) SDL_SetWindowGrab(m_Window, grabbed ? SDL_TRUE : SDL_FALSE);
    }
    PlatformResult SDLWindow::BeginUI()
    {
        if (auto current = BeginRender(); !current) return current;
        if (!m_ImGuiWindow) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState, "begin UI", "UI is unavailable"});
        RefreshDimensions();
        m_ImGuiWindow->BeginRender(this);
        return {};
    }
    PlatformResult SDLWindow::EndUI()
    {
        if (!m_ImGuiWindow) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState, "end UI", "UI is unavailable"});
        return m_ImGuiWindow->EndRender(this);
    }
    bool SDLWindow::WantsMouse() const { return m_ImGuiWindow && m_ImGuiWindow->WantCaptureMouse(); }
    bool SDLWindow::WantsKeyboard() const { return m_ImGuiWindow && m_ImGuiWindow->WantCaptureKeyBoard(); }

	void SDLWindow::FreeContext()
	{
		GLContextThread::RequireOwner(m_OwnerThread, "SDLWindow::FreeContext");
		RenderCounters::ForgetContext(m_Context);
		SDL_GL_DeleteContext(m_Context);
		m_Context = nullptr;
	}


	

}
