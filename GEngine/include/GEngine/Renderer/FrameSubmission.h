#pragma once

#include "Renderer/RenderFrame.h"
#include "Renderer/SceneRenderResources.h"
#include "Scene/RenderEcs.h"

namespace GEngine
{
    class RenderTarget;
    class PointShadowFrameBuffer;
    class CascadeShadowFrameBuffer;
    class MousePickFrameBuffer;

    enum class SubmissionCode { Allocation, Context, Driver, InvalidCamera, InvalidTarget,
        UnsupportedLights, UnsupportedPipeline, InvalidDraw, InvalidShadowSettings };
    using SubmissionCause = std::variant<SubmissionCode, PlatformError, Asset::ShaderError, GpuMeshError,
        FramebufferError, Asset::TextureError, Asset::SamplerError, RenderEcsError>;
    struct SubmissionError { std::string operation; SubmissionCause cause; };
    std::string DescribeSubmissionError(const SubmissionError&);
    struct FrameSubmissionDesc
    {
        RenderTarget& color;
        const MousePickFrameBuffer& picking;
        const PointShadowFrameBuffer& pointShadow;
        const CascadeShadowFrameBuffer& cascadeShadow;
        std::span<const ScenePipeline> pipelines;
        std::span<const float> cascadeSplits;
        float cameraFov = glm::radians(45.f), cameraAspect = 1.f;
        float cameraNear = .1f, cameraFar = 1000.f;
        float pointNear = .1f, pointFar = 100.f;
    };
    struct FrameSubmissionStats
    {
        std::size_t shadowDraws{}, pickDraws{}, colorDraws{}, helperDraws{}, skyDraws{};
    };
    // Serial owner-context submission only. All scene/resource reads are complete
    // before entry; only immutable frame leases and retained targets are consumed.
    // The existing shader supports at most one directional + one point light;
    // additional lights/spot lights return a typed capability error before drawing.
    // No ECS/manager access, publication, culling, sorting or scheduling occurs.
    class FrameSubmission final
    {
    public:
        static std::expected<FrameSubmission, SubmissionError> Create();
        FrameSubmission(FrameSubmission&&) noexcept;
        FrameSubmission& operator=(FrameSubmission&&) noexcept;
        ~FrameSubmission();
        FrameSubmission(const FrameSubmission&) = delete;
        FrameSubmission& operator=(const FrameSubmission&) = delete;
        std::expected<FrameSubmissionStats, SubmissionError> Submit(
            const RenderFrame&, const FrameSubmissionDesc&, EntityPickTable&);
    private:
        FrameSubmission() = default;
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
}
