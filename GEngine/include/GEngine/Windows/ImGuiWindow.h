#pragma once
// Backend-private UI adapter; never included by a normal window contract.
#include "Core/Platform.h"

union SDL_Event;
struct ImGuiContext;

namespace GEngine
{
	
	class SDLWindow;

	class ImGuiWindow_
	{
	public:
		ImGuiWindow_() {}
		~ImGuiWindow_() { ShutDown(); };
		ImGuiWindow_(const ImGuiWindow_&) = delete;
		ImGuiWindow_& operator=(const ImGuiWindow_&) = delete;
		ImGuiContext* GetContext() const { return m_Context; }

		[[nodiscard]] PlatformResult Initialize(SDLWindow* window, const ImGuiWindowProperties& ImGuiWindowProps = ImGuiWindowProperties{});
		void ShutDown();

		bool HandleSDLEvent(SDL_Event& e);

		void BeginRender(SDLWindow* window);
		[[nodiscard]] PlatformResult EndRender(SDLWindow* window);

		void SetDarkThemeColors();

		bool WantCaptureMouse();
		bool WantCaptureKeyBoard();
	private:
		ImGuiContext* m_Context = nullptr;
	};
	

}
