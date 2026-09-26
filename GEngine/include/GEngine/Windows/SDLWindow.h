#pragma once
// Backend-private implementation header. Normal consumers use Core/Window.h.
#include "Core/Window.h"
#include "Windows/ImGuiWindow.h"
#include <thread>
//#include <Core/Renderer.h>


struct SDL_Window;

namespace GEngine
{
    class BaseApp;
    class ImGuiWindow_;

    class SDLWindow: public Window
    {
    public:

        SDLWindow() = default;
        NONCOPYABLE(SDLWindow);
        virtual ~SDLWindow();
        // Inherited via Window
        [[nodiscard]] virtual PlatformResult Initialize(const WindowProperties& winProp = WindowProperties{}) override;
        virtual void SwapBuffer()override;
        virtual void ShutDown() override; 
        virtual void SetTitle(const std::string& title) const override;
        SDL_Window* GetSDLWindow()const { return m_Window; }
        ImGuiWindow_* GetImGuiWindow()const;
        // Inherited via Window
        virtual uint32_t GetWindowID() const override;
        void* GetContext() { return m_Context; }
        //operator SDL_Window* () { return m_Window; }

        void FreeContext();
    private:
        void SetWindow(int DisplayWidth, int DisplayHeight, uint32_t flag, const WindowProperties& winProp = WindowProperties{});
        uint32_t GetWindowFlag(const WindowProperties& winProp);

    private:
        const std::thread::id m_OwnerThread = std::this_thread::get_id();
        SDL_Window* m_Window{};
        ImGuiWindow_* m_ImGuiWindow{};
        void* m_Context{};


    public:
        // Inherited via Window
        [[nodiscard]] virtual PlatformResult BeginRender()override;

        [[nodiscard]] virtual PlatformResult NullRender() override;

        virtual void EndRender(BaseApp* app)override;


        // Inherited via Window
        virtual std::string GetTitle() const override;


        // Inherited via Window
        void RefreshDimensions() override;
        NativeFramebufferPixelSize GetFramebufferPixelSize() const override { return m_PixelSize; }
        WindowState GetState() const override;
        bool IsCurrent() const override;
        int GetSwapInterval() const override;
        void SetMouseGrab(bool grabbed) override;
        [[nodiscard]] PlatformResult BeginUI() override;
        [[nodiscard]] PlatformResult EndUI(UIFrameDisposition disposition = UIFrameDisposition::Submit) override;
        bool WantsMouse() const override;
        bool WantsKeyboard() const override;
    private:
        NativeFramebufferPixelSize m_PixelSize{};

      

    };
}
