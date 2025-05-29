#include "gepch.h"
#include "Windows/ImGuiWindow.h"
#include "Windows/SDLWindow.h"
#include <imgui/imgui.h>
#include <imgui/imgui_impl_sdl.h>
#include <imgui/imgui_impl_opengl3.h>
#include <Inputs/KeyCodes.h>
#include<imguizmo/ImGuizmo.h>

static std::string regular_dir = "../GEngine/include/GEngine/Assets/Fonts/OpenSans-Regular.ttf";
static std::string bold_dir = "../GEngine/include/GEngine/Assets/Fonts/OpenSans-Bold.ttf";

namespace GEngine
{
	using namespace Input::Key;
	void ImGuiWindow_::Initialize(SDLWindow* window, const ImGuiWindowProperties& ImGuiWindowProps)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();
		ImGuiIO& io = ImGui::GetIO();

		io.Fonts->AddFontFromFileTTF(bold_dir.c_str(), 18.f);
		io.FontDefault = io.Fonts->AddFontFromFileTTF(regular_dir.c_str(), 18.f);


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

		ImGui_ImplSDL2_InitForOpenGL(window->GetSDLWindow(), window->GetContext());
		ImGui_ImplOpenGL3_Init("#version 430");
	
	}

	void ImGuiWindow_::ShutDown()
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
	}

	bool ImGuiWindow_::HandleSDLEvent(SDL_Event& e)
	{
		return ImGui_ImplSDL2_ProcessEvent(&e);
	}

	void ImGuiWindow_::BeginRender(SDLWindow* window)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2{ static_cast<float>(window->GetScreenWidth()), static_cast<float>(window->GetScreenHeight()) };
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(window->GetSDLWindow());

		ImGui::NewFrame();

		ImGuizmo::BeginFrame();
	}

	void ImGuiWindow_::EndRender(SDLWindow* window)
	{
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		auto& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			window->BeginRender();
		
		}
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
		return ImGui::GetIO().WantCaptureMouse;
	}

	bool ImGuiWindow_::WantCaptureKeyBoard()
	{
		return ImGui::GetIO().WantCaptureKeyboard;
	}


	
}
