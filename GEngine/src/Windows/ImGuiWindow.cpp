#include "gepch.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLDebug.h"
#include "Windows/ImGuiWindow.h"
#include "Windows/SDLWindow.h"
#include <imgui/imgui.h>
#include <imgui/imgui_impl_sdl.h>
#include <imgui/imgui_impl_opengl3.h>
#include <Inputs/KeyCodes.h>
#include<imguizmo/ImGuizmo.h>





namespace GEngine
{
	using namespace Input::Key;
	PlatformResult ImGuiWindow_::Initialize(SDLWindow* window, const ImGuiWindowProperties& ImGuiWindowProps)
	{
		GLContextThread::RequireCurrent("ImGuiWindow::Initialize");
		if (m_Context) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState, "initialize UI", "UI is already initialized"});
        auto bold = RuntimeAssets::TryFile("Fonts/OpenSans-Bold.ttf");
        auto regular = RuntimeAssets::TryFile("Fonts/OpenSans-Regular.ttf");
        if (!bold) return std::unexpected(bold.error());
        if (!regular) return std::unexpected(regular.error());
		IMGUI_CHECKVERSION();
		m_Context = ImGui::CreateContext();
        if (!m_Context) return std::unexpected(PlatformError{PlatformErrorCode::Allocation, "UI context", "UI context allocation failed"});
        struct Rollback { ImGuiWindow_* owner; bool committed = false; ~Rollback() { if (!committed) owner->ShutDown(); } } rollback{this};
		ImGui::SetCurrentContext(m_Context);
		ImGui::StyleColorsDark();
		ImGuiIO& io = ImGui::GetIO();

		if (!io.Fonts->AddFontFromFileTTF(bold->c_str(), 18.f))
            return std::unexpected(PlatformError{PlatformErrorCode::UserInterface, "load UI font", *bold});
		io.FontDefault = io.Fonts->AddFontFromFileTTF(regular->c_str(), 18.f);


		if (!io.FontDefault) return std::unexpected(PlatformError{PlatformErrorCode::UserInterface, "load UI font", *regular});
		io.ConfigWindowsMoveFromTitleBarOnly = ImGuiWindowProps.bMoveFromTitleBarOnly;

		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		if (ImGuiWindowProps.bDockingEnabled)
		{
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		}

		if (ImGuiWindowProps.bViewPortEnabled)
		{
			io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		}
	
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
		
		SetDarkThemeColors();

		io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
		io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

	/*	io.KeyMap[ImGuiKey_Tab] =         GENGINE_KEY_TAB;
		io.KeyMap[ImGuiKey_LeftArrow] =   GENGINE_KEY_LEFT;
		io.KeyMap[ImGuiKey_RightArrow] =  GENGINE_KEY_RIGHT;
		io.KeyMap[ImGuiKey_UpArrow] =     GENGINE_KEY_UP;
		io.KeyMap[ImGuiKey_DownArrow] =   GENGINE_KEY_DOWN;
		io.KeyMap[ImGuiKey_PageUp] =      GENGINE_KEY_PAGEUP;
		io.KeyMap[ImGuiKey_PageDown] =    GENGINE_KEY_PAGEDOWN;
		io.KeyMap[ImGuiKey_Home] =        GENGINE_KEY_HOME;
		io.KeyMap[ImGuiKey_End] =         GENGINE_KEY_END;
		io.KeyMap[ImGuiKey_Insert] =      GENGINE_KEY_INSERT;
		io.KeyMap[ImGuiKey_Delete] =      GENGINE_KEY_DELETE;
		io.KeyMap[ImGuiKey_Backspace] =   GENGINE_KEY_BACKSPACE;
		io.KeyMap[ImGuiKey_Space] =       GENGINE_KEY_SPACE;
		io.KeyMap[ImGuiKey_Enter] =       GENGINE_KEY_RETURN;
		io.KeyMap[ImGuiKey_Escape] =      GENGINE_KEY_ESCAPE;
		io.KeyMap[ImGuiKey_KeyPadEnter] = GENGINE_KEY_KP_ENTER;
		io.KeyMap[ImGuiKey_A] =			  GENGINE_KEY_A; 
		io.KeyMap[ImGuiKey_C] =			  GENGINE_KEY_C; 
		io.KeyMap[ImGuiKey_V] =			  GENGINE_KEY_V;
		io.KeyMap[ImGuiKey_X] =			  GENGINE_KEY_X;
		io.KeyMap[ImGuiKey_Y] =			  GENGINE_KEY_Y;
		io.KeyMap[ImGuiKey_Z] =			  GENGINE_KEY_Z;*/

		if (!ImGui_ImplSDL2_InitForOpenGL(window->GetSDLWindow(), window->GetContext()))
			return std::unexpected(PlatformError{PlatformErrorCode::UserInterface, "UI platform backend", "UI platform initialization failed"});
		if (!ImGui_ImplOpenGL3_Init("#version 430"))
			return std::unexpected(PlatformError{PlatformErrorCode::UserInterface, "UI graphics backend", "UI graphics initialization failed"});
        rollback.committed = true;
        return {};
	
	}

	void ImGuiWindow_::ShutDown()
	{
		if (!m_Context) return;
		GLContextThread::RequireCurrent("ImGuiWindow::ShutDown");
		auto* previous = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_Context);
		// Backend-owned state records which initialization stages actually ran.
		// Keep SDL alive while the GL backend destroys secondary platform windows.
		if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
		if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext(m_Context);
		ImGui::SetCurrentContext(previous == m_Context ? nullptr : previous);
		m_Context = nullptr;
	}

	bool ImGuiWindow_::HandleSDLEvent(SDL_Event& e)
	{
		if (!m_Context) return false;
		ImGui::SetCurrentContext(m_Context);
		return ImGui_ImplSDL2_ProcessEvent(&e);
	}

	void ImGuiWindow_::BeginRender(SDLWindow* window)
	{
		GLContextThread::RequireCurrent("ImGuiWindow::BeginRender");
		ImGui::SetCurrentContext(m_Context);
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2{ static_cast<float>(window->GetScreenWidth()), static_cast<float>(window->GetScreenHeight()) };
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(window->GetSDLWindow());

		ImGui::NewFrame();

		ImGuizmo::BeginFrame();
	}

	PlatformResult ImGuiWindow_::EndRender(SDLWindow* window)
	{
		GLContextThread::RequireCurrent("ImGuiWindow::EndRender");
		ImGui::SetCurrentContext(m_Context);
		ImGui::Render();
		{
			const GLDebug::Group submission("ImGui draw data");
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		}

		auto& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			return window->BeginRender();
		
		}
        return {};
	}

	void ImGuiWindow_::SetDarkThemeColors()
	{
		auto& colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

		// Headers
		colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Buttons
		colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Frame BG
		colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Tabs
		colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
		colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
		colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

		// Title
		colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
	}

	bool ImGuiWindow_::WantCaptureMouse()
	{
		if (!m_Context) return false;
		ImGui::SetCurrentContext(m_Context);
		return ImGui::GetIO().WantCaptureMouse;
	}

	bool ImGuiWindow_::WantCaptureKeyBoard()
	{
		if (!m_Context) return false;
		ImGui::SetCurrentContext(m_Context);
		return ImGui::GetIO().WantCaptureKeyboard;
	}


	
}

namespace GEngine::UI
{
    std::expected<FramebufferScale, PlatformError> CurrentViewportFramebufferScale()
    {
        GLContextThread::RequireCurrent("viewport scale");
        auto* viewport = ImGui::GetWindowViewport();
        auto* window = viewport ? static_cast<SDL_Window*>(viewport->PlatformHandle) : nullptr;
        if (!window) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState, "viewport scale", "Panel backing window is not ready"});
        int w = 0, h = 0, pw = 0, ph = 0;
        SDL_GetWindowSize(window, &w, &h); SDL_GL_GetDrawableSize(window, &pw, &ph);
        if (w <= 0 || h <= 0 || pw <= 0 || ph <= 0)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidSize, "viewport scale", "Panel backing window has no drawable area"});
        return FramebufferScale{float(pw) / float(w), float(ph) / float(h)};
    }
}
