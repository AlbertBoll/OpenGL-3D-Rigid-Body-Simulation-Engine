#pragma once

#include "Renderer/RenderFrame.h"
#include "Renderer/SceneRenderResources.h"
#include "Scene/RenderEcs.h"
#include "Renderer/RenderVisibility.h"

namespace GEngine
{
    enum class FrameStage { BeginFrame, UpdateFrameResources, FreezeFrameInputs, BuildRenderFrame, Pass };
    enum class RenderPass { DirectionalShadow, PointShadow, Picking, Opaque, Masked, Skybox,
        Transparent, Debug, Resolve, EditorUI, Present, LegacyScene };
    enum class PassTarget { None, DirectionalDepth, PointDepth, Picking, SceneColor, ResolvedColor, Window };
    enum class PassLoad { Load, Clear, Discard };
    enum class PassBoundary { Internal, RestoreAfterLegacy, EstablishBeforeUI, Presentation };
    struct PassViewport { unsigned x{}, y{}, width{}, height{}; };
    // Semantic contracts consumed by the concrete backend; no native state escapes.
    struct RenderPassDesc
    {
        RenderPass pass{};
        PassTarget input = PassTarget::None, output = PassTarget::None;
        PassTarget secondaryInput = PassTarget::None;
        PassLoad colorLoad = PassLoad::Load, depthLoad = PassLoad::Load;
        PassLoad stencilLoad = PassLoad::Load;
        PassViewport viewport;
        bool scissor = false, depthTest = false, depthWrite = false, colorWrite = true, stencilWrite = false;
        DepthCompare depthCompare = DepthCompare::Less;
        bool blend = false;
        bool materialOverrides = false; // Validated semantic pipeline state, applied per draw.
        CullMode cull = CullMode::None;
        PassBoundary boundary = PassBoundary::Internal;
    };
    struct FrameEvent { FrameStage stage; RenderPass pass{}; RenderPassDesc contract; };
    struct FrameTrace
    {
        std::array<FrameEvent, 32> events{};
        std::size_t count{};
        void Stage(FrameStage stage) { Asset::AssetDetail::RequireInvariant(count < events.size()); events[count++] = {stage}; }
        void Pass(const RenderPassDesc& pass) { Asset::AssetDetail::RequireInvariant(count < events.size()); events[count++] = {FrameStage::Pass, pass.pass, pass}; }
        std::span<const FrameEvent> Events() const { return {events.data(), count}; }
    };
    class RenderTarget;
    class PointShadowFrameBuffer;
    class CascadeShadowFrameBuffer;
    class MousePickFrameBuffer;

    enum class SubmissionCode { Allocation, Context, Driver, InvalidCamera, InvalidTarget,
        UnsupportedLights, UnsupportedPipeline, InvalidDraw, InvalidShadowSettings };
    using SubmissionCause = std::variant<SubmissionCode, PlatformError, Asset::ShaderError, GpuMeshError,
        FramebufferError, Asset::TextureError, Asset::SamplerError, RenderEcsError, VisibilityError>;
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
        bool pickingEnabled = true;
    };
    struct FrameSubmissionStats
    {
        std::size_t shadowDraws{}, pickDraws{}, colorDraws{}, helperDraws{}, skyDraws{};
        std::size_t opaqueDraws{}, maskedDraws{}, transparentDraws{};
        VisibilityStats visibility;
        FrameTrace trace;
    };
    // Serial owner-context submission only. All scene/resource reads are complete
    // before entry; only immutable frame leases and retained targets are consumed.
    // The existing shader supports at most one directional + one point light;
    // additional lights/spot lights return a typed capability error before drawing.
    // No ECS/manager access or publication occurs. Visibility and explicit pass
    // contracts consume the same immutable frame, with sky before transparency.
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
        static RenderPassDesc DescribePass(RenderPass, const FrameSubmissionDesc&);
    private:
        FrameSubmission() = default;
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
}
