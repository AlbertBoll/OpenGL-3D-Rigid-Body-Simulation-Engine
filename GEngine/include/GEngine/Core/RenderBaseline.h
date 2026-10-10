#pragma once

// Opt-in Phase 13 measurement protocol; no collector in normal builds.
#ifdef GENGINE_RENDER_BASELINE
#include "Core/FrameBuffer.h"
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace GEngine
{
    struct WindowProperties;
    struct EngineLaunchConfig;
    class RenderTarget;
}
namespace GEngine::RenderBaseline
{
    enum class ErrorCode { Capability, InvalidOutput, OutputOpen, OutputWrite, ContextUnavailable,
        SwapInterval, Dimensions, PhysicsAdvanced, CaptureMissing, CaptureSurface, CaptureWrite,
        CaptureRead, SceneTarget, Allocation, AlreadyActive };
    struct Error
    {
        ErrorCode code;
        const char* operation;
        std::string context;
        std::string message;
        std::error_code system{};
        std::optional<FramebufferError> framebuffer{};
    };
    using Result = std::expected<void, Error>;
    std::string DescribeContext(const Error&);
    bool Requested();
    void Configure(WindowProperties&);
    void Configure(WindowProperties&, const EngineLaunchConfig&);

    namespace Detail { struct State; }
    // A session and all of its calls remain on the creating context thread.
    // Moving transfers collection ownership; destruction closes partial output.
    class Session
    {
    public:
        static constexpr unsigned Warmup = 120, Samples = 240;
        [[nodiscard]] static std::expected<Session, Error> Create();
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
        Session(Session&&) noexcept;
        Session& operator=(Session&&) = delete;
        ~Session();
        bool Enabled() const;
        void Begin();
        void Work();
        void UpdatedInput();
        void Updated();
        [[nodiscard]] std::expected<bool, Error> End();
        [[nodiscard]] Result CaptureScene(const RenderTarget*);
    private:
        explicit Session(std::unique_ptr<Detail::State>);
        std::unique_ptr<Detail::State> m_State;
    };
    // Called by the owning presentation backend before swap. Failure is retained
    // by the active session and returned by CaptureScene/End to the runtime owner.
    void Capture();
}
#endif
