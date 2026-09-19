#pragma once

#include "Renderer/FrameSubmission.h"
#include "Renderer/RenderExtraction.h"
#include "Assets/AsyncUploadQueue.h"

namespace GEngine
{
    class Window;
    namespace Manager { class WindowManager; }
    enum class ScheduleCode { Context, InvalidInput, FrameActive };
    using ScheduleCause = std::variant<ScheduleCode, SubmissionError, RenderExtractionError,
        FramebufferError, PlatformError, SceneResourceError, Asset::UploadError>;
    struct ScheduleError { FrameStage stage; ScheduleCause cause; };
    using ScheduleResult = std::expected<void, ScheduleError>;
    std::string DescribeScheduleError(const ScheduleError&);
    struct FrameCallback
    {
        void* user{};
        ScheduleResult (*invoke)(void*){};
        ScheduleResult operator()() const { return invoke ? invoke(user) : ScheduleResult{}; }
    };
    struct RenderContext
    {
        Window& window;
        Manager::WindowManager& windows;
        RenderTarget* color{};
        bool visible = true;
        FrameCallback updateResources; // Synchronous publication only; runs before FrameAccess.
        FrameCallback legacyScene;     // Isolated compatibility draw body, no resolve/UI/present.
        FrameCallback editorUI;        // UI authoring only; native backend calls stay in Window.
        Asset::AsyncUploadQueue* uploads{}; // Optional generic queue, drained before updateResources.
    };
    struct FrameSceneInput
    {
        _Scene& scene;
        SceneRenderResources& resources;
        FrameSubmission& submission;
        FrameSubmissionDesc targets;
        EntityPickTable& picks;
        std::span<const FrameCamera> cameras;
        RenderExtractionConfig extraction;
        // Consumes this submission's picking target/table once, on the owner
        // thread, after frame leases retire and before BeginUI advances input.
        // Invoked only for a visible, successful frame with pickingEnabled.
        FrameCallback pickingReadback;
    };
    struct ScheduledFrameStats
    {
        FrameTrace trace;
        FrameSubmissionStats submission;
        RenderExtractionStats extraction;
    };
    // Owns the full serial frame boundary. Resource access and frame leases remain
    // alive through submission, then retire before UI can author the next frame.
    // Extraction freezes ECS before either serial or parallel builder work. The
    // finalized frame is never changed by visibility or any render pass.
    class FrameScheduler final
    {
    public:
        [[nodiscard]] static std::expected<ScheduledFrameStats, ScheduleError> Render(
            const RenderContext&, const FrameSceneInput* scene = nullptr);
        static RenderPassDesc DescribeBoundary(RenderPass, const RenderContext&);
    };
}
