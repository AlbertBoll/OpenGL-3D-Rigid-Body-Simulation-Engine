#pragma once

union SDL_Event;
struct ImGuiContext;

namespace GEngine
{
	
	struct ImGuiWindowProperties
	{
		bool bMoveFromTitleBarOnly = true;
		bool bDockingEnabled = false;
		bool bViewPortEnabled = false;
	};

	class SDLWindow;

	class ImGuiWindow_
	{
	public:
		ImGuiWindow_() {}
		~ImGuiWindow_() { ShutDown(); };
		ImGuiWindow_(const ImGuiWindow_&) = delete;
		ImGuiWindow_& operator=(const ImGuiWindow_&) = delete;
		ImGuiContext* GetContext() const { return m_Context; }

		void Initialize(SDLWindow* window, const ImGuiWindowProperties& ImGuiWindowProps = ImGuiWindowProperties{});
		void ShutDown();

		bool HandleSDLEvent(SDL_Event& e);

		void BeginRender(SDLWindow* window);
		void EndRender(SDLWindow* window);

		void SetDarkThemeColors();

		bool WantCaptureMouse();
		bool WantCaptureKeyBoard();
	private:
		ImGuiContext* m_Context = nullptr;
	};
	

}
