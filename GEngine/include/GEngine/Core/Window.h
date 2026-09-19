#pragma once
#include "Core/Utility.h"
#include "Core/Platform.h"
#include "Renderer/PassTiming.h"
#include <string>


namespace GEngine
{
	class BaseApp;
	//enum GraphicContext
	//{
	//	OPENGL,
	//	VULKAN,
	//	DIRECTX
	//};

	enum class WindowFlags: uint8_t
	{
		INVISIBLE = 0x1,
		FULLSCREEN = 0X2,
		BORDERLESS = 0x4,
		RESIZABLE = 0x8
	};

	enum class WindowPos: uint8_t
	{
		TopLeft,
		TopRight,
		ButtomLeft,
		ButtomRight,
		Center

	};

	//struct ScreenParams
	//{
	//	int m_DisplayWidth = 2560;
	//	int m_DisplayHeight = 1440;
	//};

	struct WindowProperties
	{
		std::string m_Title = "GEngine Editor App";
		uint32_t m_Width = 800;
		uint32_t m_Height = 600;
		uint32_t m_MinWidth = 480;
		uint32_t m_MinHeight = 320;

		float m_AspectRatio = 16.f / 9.f;
		float m_Red = 0.f;
		float m_Green = 0.f;
		float m_Blue = 0.f;
		BitFlags<WindowFlags, uint8_t>flag{ WindowFlags::RESIZABLE };
		WindowPos m_WinPos = WindowPos::Center;
		int m_XPaddingToCenterY = 20;
		int m_YPaddingToCenterX = 20;
		bool m_IsVsync = true;
		ImGuiWindowProperties ImGuiWindowProperties = {};

		void SetColor(float R, float G, float B) { m_Red = R; m_Green = G; m_Blue = B; }
		void SetCornFlowerBlue() {
			m_Red = static_cast<float>(0x64)/ static_cast<float>(0xFF); 
			m_Green = static_cast<float>(0x95)/ static_cast<float>(0xFF);
			m_Blue = static_cast<float>(0xED)/ static_cast<float>(0xFF);
		}
	};

	//abstract base class of window
	class Window
	{
	public:

		virtual ~Window(){};
        PassTiming& Timings() noexcept { return m_PassTiming; }
		uint32_t GetScreenWidth()const { return m_ScreenWidth; }
		uint32_t GetScreenHeight()const { return m_ScreenHeight; }
		[[nodiscard]] virtual PlatformResult Initialize(const WindowProperties& winProp = {}) = 0;
		virtual void SwapBuffer() = 0;
		virtual void ShutDown() = 0;
		virtual void SetTitle(const std::string& title) const = 0;
		virtual std::string GetTitle() const = 0;
		virtual uint32_t GetWindowID()const = 0;

		[[nodiscard]] virtual PlatformResult NullRender() = 0;
		[[nodiscard]] virtual PlatformResult BeginRender()  = 0;
		virtual void EndRender(BaseApp* app)  = 0;
		// Only native platform state updates these dimensions; panel sizing never does.
        virtual void RefreshDimensions() = 0;
        virtual NativeFramebufferPixelSize GetFramebufferPixelSize() const = 0;
        NativeWindowLogicalSize GetLogicalSize() const { return {m_ScreenWidth, m_ScreenHeight}; }
        virtual WindowState GetState() const = 0;
        virtual bool IsCurrent() const = 0;
        virtual int GetSwapInterval() const = 0;
        virtual void SetMouseGrab(bool grabbed) = 0;
        [[nodiscard]] virtual PlatformResult BeginUI() = 0;
        [[nodiscard]] virtual PlatformResult EndUI() = 0;
        virtual bool WantsMouse() const = 0;
        virtual bool WantsKeyboard() const = 0;

	
		[[nodiscard]] static std::expected<ScopedPtr<Window>, PlatformError> Create(const WindowProperties& winProp = {});


		//virtual Window* GetUnderlyingWindow() = 0;

	protected:
        PassTiming m_PassTiming;
		uint32_t m_ScreenWidth = 0, m_ScreenHeight = 0;
		float m_AspectRatio = 16.f / 9.f;

	};

	/*template<typename T>
	class WindowImpl : public Window
	{
	public:
		T* GetUnderlyingWindow() override
		{
			return static_cast<T*>(this);
		}
	};*/




}
