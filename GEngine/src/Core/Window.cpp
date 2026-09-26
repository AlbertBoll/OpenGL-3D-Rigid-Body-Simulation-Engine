#include "gepch.h"
#include "Core/Window.h"
#include "Windows/SDLWindow.h"
#include <new>

namespace GEngine
{
    void ReportPlatformError(const PlatformError& error)
    {
        Log::GetCoreLogger()->error("Platform operation={} code={}: {}",
            error.operation, static_cast<unsigned>(error.code), error.message);
    }
    std::expected<ScopedPtr<Window>, PlatformError> Window::Create(const WindowProperties& properties)
    {
        ScopedPtr<Window> window(new (std::nothrow) SDLWindow);
        if (!window) return std::unexpected(PlatformError{PlatformErrorCode::Allocation, "window owner", "Window owner allocation failed"});
        if (auto result = window->Initialize(properties); !result) return std::unexpected(result.error());
        return window;
    }
}
