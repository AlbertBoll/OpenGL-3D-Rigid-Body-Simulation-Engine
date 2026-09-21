#pragma once

#include "Renderer/RenderFrame.h"
#include "Renderer/SceneRenderResources.h"
#include "Scene/RenderEcs.h"
#include "Renderer/RenderVisibility.h"

namespace GEngine
{
    enum class FrameStage { BeginFrame, UpdateFrameResources, FreezeFrameInputs, BuildRenderFrame, Pass };
    enum class RenderPass { DirectionalShadow, PointShadow, Picking, Opaque, Masked, Skybox,
        Transparent, Debug, Resolve, EditorUI, Present, LegacyScene,
        PickingReadback }; // CPU wait/transfer timing, not a scheduled draw pass.
    enum class PassTarget { None, DirectionalDepth, PointDepth, Picking, SceneColor, ResolvedColor, Window };
    enum class PassLoad { Load, Clear, Discard };
    enum class PassBoundary { Internal, RestoreAfterLegacy, EstablishBeforeUI, Presentation };
    enum class PassDirtyReason : std::uint32_t
    {
        None = 0, InitialContent = 1, TargetStorage = 2, SceneMembership = 4,
        Transform = 8, Mesh = 16, Material = 32, Camera = 64, Light = 128,
        ShadowSettings = 256, ExternalWrite = 512, RetryAfterFailure = 1024
    };
    constexpr PassDirtyReason operator|(PassDirtyReason a, PassDirtyReason b) noexcept
    { return static_cast<PassDirtyReason>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b)); }
    constexpr bool HasDirtyReason(PassDirtyReason reasons, PassDirtyReason reason) noexcept
    { return (static_cast<std::uint32_t>(reasons) & static_cast<std::uint32_t>(reason)) != 0; }
    struct PassDecision
    {
        RenderPass pass{};
        PassDirtyReason reasons = PassDirtyReason::None;
        bool requested = false, executed = false;
    };
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
        // Requests a current picking image/readback, independently of image dirtiness.
        // A repeated request may reuse the image and rebuild its matching CPU table.
        bool pickingEnabled = true;
        // Serial fallback/reference uses the same sorted immutable frame.
        bool instancingEnabled = true;
        // Conservative shadow relevance; false retains the broadcast reference.
        bool shadowCullingEnabled = true;
    };
    inline constexpr std::size_t MinimumRenderInstances = 4;
    struct ShadowCasterStats
    {
        // candidates = visible + culled; conservative is a subset of visible.
        // submittedCasters is zero for a reused pass, otherwise equals visible.
        std::size_t candidates{}, visible{}, culled{}, conservative{}, submittedCasters{};
    };
    struct ShadowVisibilityStats
    {
        EntityRenderId light;
        std::size_t layerCount{};
        ShadowCasterStats total; // Union: a caster appears at most once per light.
        std::array<ShadowCasterStats,6> layers{}; // Cascades or cube faces.
    };
    struct FrameSubmissionStats
    {
        std::size_t shadowDraws{}, pickDraws{}, colorDraws{}, helperDraws{}, skyDraws{};
        std::size_t opaqueDraws{}, maskedDraws{}, transparentDraws{};
        // Existing draw fields count logical items; these count actual API draws.
        std::size_t submittedDrawCalls{}, instancedDrawCalls{}, submittedInstances{};
        VisibilityStats visibility;
        std::array<ShadowVisibilityStats,2> shadowVisibility; // Directional, point.
        FrameTrace trace;
        // Directional shadow, point shadow, picking; includes deferred/clean passes.
        std::array<PassDecision, 3> decisions{};
    };
    // Serial owner-context submission only. All scene/resource reads are complete
    // before entry; only immutable frame leases and retained targets are consumed.
    // Supports one directional, one point and one unshadowed spot light.
    // Additional lights or spot shadows return a typed capability error before drawing.
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
        // This submitter owns cached target contents. External writes (including
        // another submitter) must notify it before reuse. Resizes detect themselves.
        void InvalidatePassContents() noexcept;
    private:
        FrameSubmission() = default;
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
}
