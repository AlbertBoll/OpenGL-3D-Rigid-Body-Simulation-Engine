#pragma once
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace GEngine
{
    enum class PlatformErrorCode
    {
        InvalidSize, InvalidState, Initialization, WindowCreation, ContextCreation,
        ContextActivation, FunctionLoading, UserInterface, InputMode, ResourcePath, Allocation, WindowNotFound
    };
    struct PlatformError
    {
        PlatformErrorCode code;
        std::string operation;
        std::string message;
    };
    using PlatformResult = std::expected<void, PlatformError>;
    void ReportPlatformError(const PlatformError&);

    struct NativeWindowLogicalSize
    {
        std::uint32_t Width = 0, Height = 0;
        bool operator==(const NativeWindowLogicalSize&) const = default;
    };
    struct NativeFramebufferPixelSize
    {
        std::uint32_t Width = 0, Height = 0;
        bool operator==(const NativeFramebufferPixelSize&) const = default;
    };
    struct EditorViewportLogicalSize
    {
        float Width = 0, Height = 0;
        bool operator==(const EditorViewportLogicalSize&) const = default;
    };
    struct EditorViewportPixelSize
    {
        std::uint32_t Width = 0, Height = 0;
        bool operator==(const EditorViewportPixelSize&) const = default;
    };
    struct FramebufferScale { float X = 1, Y = 1; };

    // Round once at the logical/pixel boundary. Fractional logical changes that
    // resolve to the same extent never request another storage allocation.
    inline std::expected<EditorViewportPixelSize, PlatformError> ToFramebufferPixels(
        EditorViewportLogicalSize logical, FramebufferScale scale)
    {
        const double width = double(logical.Width) * scale.X;
        const double height = double(logical.Height) * scale.Y;
        if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(scale.X)
            || !std::isfinite(scale.Y) || logical.Width < 0 || logical.Height < 0
            || scale.X <= 0 || scale.Y <= 0 || width > 8192 || height > 8192)
            return std::unexpected(PlatformError{PlatformErrorCode::InvalidSize,
                "viewport conversion", "Finite nonnegative extent and positive scale within target limits required"});
        if (logical.Width == 0 || logical.Height == 0) return EditorViewportPixelSize{};
        return EditorViewportPixelSize{static_cast<std::uint32_t>(std::floor(width + 0.5)),
            static_cast<std::uint32_t>(std::floor(height + 0.5))};
    }
    struct ViewportPixelPosition { int X, Y; };
    // The input position is relative to the displayed panel's top-left in logical
    // units. Picking uses actual allocated storage, including during resize failure.
    inline std::optional<ViewportPixelPosition> ViewportPixelAt(float x, float y,
        EditorViewportLogicalSize logical, EditorViewportPixelSize storage)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(logical.Width) || !std::isfinite(logical.Height)
            || logical.Width <= 0 || logical.Height <= 0
            || !storage.Width || !storage.Height || x < 0 || y < 0 || x >= logical.Width || y >= logical.Height)
            return std::nullopt;
        return ViewportPixelPosition{static_cast<int>(double(x) * storage.Width / logical.Width),
            static_cast<int>(storage.Height) - 1 - static_cast<int>(double(y) * storage.Height / logical.Height)};
    }

    enum class TargetSizeSource { Fixed, NativeFramebuffer, EditorViewport };
    enum class WindowStateChange { Minimized, Restored, Maximized, Hidden, Shown, Resized, DisplayChanged, Other };
    struct WindowStateEvent
    {
        std::uint32_t ID;
        WindowStateChange Change;
        NativeWindowLogicalSize LogicalSize;
        NativeFramebufferPixelSize PixelSize;
    };
    struct WindowState { bool Minimized = false, Hidden = false; };
    struct ImGuiWindowProperties
    {
        bool bMoveFromTitleBarOnly = true;
        bool bDockingEnabled = false;
        bool bViewPortEnabled = false;
    };
    namespace UI
    {
        // Query the actual backing window of the current panel, including detached
        // panels. Display/monitor DPI alone is not a logical-to-framebuffer ratio.
        std::expected<FramebufferScale, PlatformError> CurrentViewportFramebufferScale();
    }
}
