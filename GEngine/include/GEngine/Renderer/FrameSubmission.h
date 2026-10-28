#pragma once

// Qualification has an application-owned observer. Never compile that observer's
// imports into the ordinary shared library through an ambient CL definition.
#if (defined(GENGINE_PHASE24_OUTLINE_QUALIFICATION) || defined(GENGINE_PHASE24_COLLIDER_VALIDATION)) && !defined(GENGINE_PHASE24_ISOLATED_BUILD)
#error Phase24 qualification requires the isolated build runner; ordinary GEngine outputs must not be instrumented.
#endif
#if defined(GENGINE_PHASE24_ISOLATED_BUILD) && !defined(GENGINE_PHASE24_OUTLINE_QUALIFICATION) && !defined(GENGINE_PHASE24_COLLIDER_VALIDATION)
#error Phase24 isolated mode requires explicit qualification instrumentation.
#endif
#if defined(GENGINE_PHASE24_OUTLINE_QUALIFICATION) && defined(GENGINE_PHASE24_COLLIDER_VALIDATION)
#error Select exactly one isolated Phase24 qualification mode.
#endif
#ifdef _MSC_VER
#ifdef GENGINE_PHASE24_OUTLINE_QUALIFICATION
#pragma detect_mismatch("GEngine.Phase24BuildMode", "qualification")
#elif defined(GENGINE_PHASE24_COLLIDER_VALIDATION)
#pragma detect_mismatch("GEngine.Phase24BuildMode", "collider-qualification")
#else
#pragma detect_mismatch("GEngine.Phase24BuildMode", "ordinary")
#endif
#endif

#include "Renderer/RenderFrame.h"
#include "Renderer/SceneRenderResources.h"
#include "Scene/RenderEcs.h"
#include "Renderer/RenderVisibility.h"

namespace GEngine
{
    enum class FrameStage { BeginFrame, UpdateFrameResources, FreezeFrameInputs, BuildRenderFrame, Pass };
    enum class RenderPass { DirectionalShadow, PointShadow, Picking, Opaque, Masked, Skybox,
        Transparent, Debug, Resolve, EditorUI, Present, LegacyScene,
        PickingReadback, // CPU wait/transfer timing, not a scheduled draw pass.
        SelectionDepth, SelectionMask, SelectionResolve, SelectionComposite, SelectionTotal,
        QualificationFrame, QualificationSubmission };
    enum class PassTarget { None, DirectionalDepth, PointDepth, Picking, SceneColor, ResolvedColor, Window,
        SelectionMask, SelectionResolved };
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
    // Fixed baseline: filled CCW rasterization, unit line width, [0,1] depth range,
    // lower-left/negative-one-to-one clip coordinates, linear framebuffer writes,
    // multisampling and seamless cubes on. Stencil test, user clipping, logic ops,
    // primitive restart, offsets, sample overrides and smoothing are off.
    // Material state is applied after this baseline when materialOverrides is set.
    // External callbacks must finish queries/feedback and return on their context;
    // bindings are established by each draw backend, never inherited cache entries.
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
        // Empty identity means no selection work. Identity is resolved only against
        // this immutable frame; no scene access, retained entity or picking request.
        EntityRenderId selectedEntity;
        glm::vec2 selectionPixelsPerDip{1.f};
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
        std::size_t selectionDrawCalls{}, selectionPasses{}, selectionScratchBytes{};
        PassViewport selectionRegion;
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
#ifdef GENGINE_PHASE24_OUTLINE_QUALIFICATION
    // Qualification-only entry points, identically instrumented in the archived
    // C05 control and revised builds. No production observer service is introduced.
    class PassTiming;
    struct ScheduledFrameStats;
    namespace Phase24Qualification
    {
        void WorkBegin() noexcept;
        void Pacing(bool begin) noexcept;
        void WorkEnd(bool success) noexcept;
        void CollectTimings(const PassTiming&) noexcept;
        void CollectSubmission(const ScheduledFrameStats&) noexcept;
    }
#endif
}
