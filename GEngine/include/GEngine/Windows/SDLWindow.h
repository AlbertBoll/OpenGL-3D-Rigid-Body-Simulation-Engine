#pragma once
#include "Core/Window.h"
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
        virtual void Initialize(const WindowProperties& winProp = WindowProperties{}) override;
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
        SDL_Window* m_Window{};
        ImGuiWindow_* m_ImGuiWindow{};
        void* m_Context{};


    public:
        // Inherited via Window
        virtual void BeginRender()override;

        virtual void NullRender() override;

        virtual void EndRender(BaseApp* app)override;


        // Inherited via Window
        virtual std::string GetTitle() const override;


        // Inherited via Window
        virtual void OnResize(int new_width, int new_height) override;

      

    };
}
