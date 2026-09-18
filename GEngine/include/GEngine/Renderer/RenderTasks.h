#pragma once

#include "Renderer/RenderFrame.h"
#include "Scene/_Scene.h"
#include <atomic>
#include <mutex>
#include <system_error>
#include <variant>

namespace GEngine
{
    enum class RenderWorkCode
    {
        Allocation, Capacity, FrameActive, InvalidInput, InvalidWorkers, InvalidScratch,
        InvalidCallback, Cancelled, TaskFailed, MutationFailed, WorkerLaunch
    };
    using RenderWorkCause = std::variant<std::monostate, TransformError, RenderEcsError,
        FrameError, Asset::RegistryError, GpuMeshError, MaterialBindingError, std::error_code>;
    struct RenderWorkError
    {
        RenderWorkCode code;
        std::size_t element{}; // Input/queue/scratch ordinal or deterministic failing task lane.
        RenderWorkCause cause;
        std::size_t boundary = SIZE_MAX; // Failing lane/drain ordinal; preserves callback element/cause.
    };
    struct FrozenRenderEntity
    {
        EntityRenderState state; // Retained versions, copied presentation/bounds/light data.
        std::optional<Component::MeshRendererComponent> mesh;
        std::optional<Component::RenderCameraComponent> camera;
        Component::VisibilityComponent visibility;
    };
    struct RenderTaskConfig
    {
        std::uint32_t workers = 0; // 0 and 1 explicitly select the owner-thread serial path.
        bool serialFallback = true; // On thread creation failure, before ANY callback starts.
    };
    struct RenderTaskStats
    {
        std::size_t lanes{}, executionThreads{}, entities{};
        bool serialFallback{};
        std::error_code launchDiagnostic;
    };
    struct RenderTaskScratch { std::span<std::byte> bytes; };
    struct RenderTaskRange
    {
        std::span<const FrozenRenderEntity> entities;
        std::size_t first{}, lane{};
        std::span<std::byte> scratch;
        const std::atomic<bool>& cancelled;
        bool StopRequested() const noexcept { return cancelled.load(std::memory_order_relaxed); }
    };
    using RenderTaskFunction = std::expected<void, RenderWorkError> (*)(const RenderTaskRange&, void*) noexcept;

    // Application-owned boundary until renderer orchestration takes over. Publish
    // completed uploads/replacements/destruction first. Hold resources.access from
    // before Prepare until workers, merge and CPU submission all finish. Prepare
    // evaluates every lazy scene/presentation cache on the owner, freezes ECS, then
    // copies all task-visible values and retains exact asset versions. Mutable ECS
    // borrows must end before Prepare; workers get snapshots, never live scene access.
    //
    // Run is synchronous. One bounded batch of joinable CRT threads is the selected
    // Windows mechanism; no retained pool, async jobs, nested scheduling or worker GL.
    // All exits join every launched thread. RequestCancel is the sole cross-thread
    // frame operation; callers keep this owner alive through cancellation requests.
    // Cancellation is cooperative: callbacks poll, return typed errors, and do not
    // spawn detached work. Destroy the frame before its scene/FrameAccess/root.
    //
    // Logical lanes own disjoint contiguous input ranges and nonoverlapping scratch;
    // scratch/output storage is caller-owned through Run and merge. Each lane may
    // write only its scratch/output. Run success is the merge barrier; merge in lane
    // order on the owner. Discard partial output on cancellation/error. Zero/one
    // workers use the identical callback contract serially. No extraction algorithm
    // or merge of DrawItems is introduced here.
    class RenderTaskFrame final
    {
    public:
        static constexpr std::uint32_t MaxWorkers = 64;
        [[nodiscard]] static std::expected<std::unique_ptr<RenderTaskFrame>, RenderWorkError> Prepare(
            _Scene&, const RenderStateResources&);
        ~RenderTaskFrame();
        RenderTaskFrame(const RenderTaskFrame&) = delete;
        RenderTaskFrame& operator=(const RenderTaskFrame&) = delete;
        std::span<const FrozenRenderEntity> Inputs() const & noexcept { return {m_Entities.get(), m_Count}; }
        std::span<const FrozenRenderEntity> Inputs() const && = delete;
        RenderRevisions Revisions() const noexcept { return m_Revisions; }
        RenderTargetRevision Target() const noexcept { return m_Target; }
        void RequestCancel() noexcept { m_Cancelled.store(true, std::memory_order_relaxed); }
        std::size_t LaneCount(RenderTaskConfig) const noexcept;
        [[nodiscard]] std::expected<RenderTaskStats, RenderWorkError> Run(
            RenderTaskConfig, std::span<const RenderTaskScratch>, RenderTaskFunction, void* user);
    private:
        RenderTaskFrame() = default;
        const std::thread::id m_Owner = std::this_thread::get_id();
        std::optional<Asset::AssetPublication::ReadPin> m_AccessPin;
        std::optional<RenderEcs::ExtractionScope> m_Freeze;
        std::unique_ptr<FrozenRenderEntity[]> m_Entities;
        std::size_t m_Count{};
        RenderRevisions m_Revisions;
        RenderTargetRevision m_Target;
        std::atomic<bool> m_Cancelled{false};
        bool m_Running{};
    };

    // Owns CPU-only command payload. Apply executes on the render owner under its
    // publication token; uploads/replacement/destruction and structural/value edits
    // belong here, never in workers. Failed Apply must preserve its own transaction
    // and retain structured diagnostics. Destruction/cancellation must perform no GL.
    class RenderMutation
    {
    public:
        virtual ~RenderMutation() = default;
        virtual std::expected<void, RenderWorkError> Apply(_Scene&, const Asset::AssetPublication::Publication&) noexcept = 0;
    };
    class RenderMutationQueue final
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<RenderMutationQueue>, RenderWorkError> Create(std::size_t capacity);
        ~RenderMutationQueue();
        RenderMutationQueue(const RenderMutationQueue&) = delete;
        RenderMutationQueue& operator=(const RenderMutationQueue&) = delete;
        // Thread-safe enqueue; consumes only on success. Full queues return Capacity.
        std::expected<void, RenderWorkError> Enqueue(std::unique_ptr<RenderMutation>&);
        // Owner only, before BeginFrame and after the previous CPU submission.
        // Exactly the entry batch is drained; later completions wait for next frame.
        // On Apply failure the successful prefix and failed command are consumed;
        // the untouched suffix remains ahead of newly queued commands. No retry loop.
        std::expected<std::size_t, RenderWorkError> Drain(_Scene&, Asset::AssetPublication&);
        void CancelPending(); // Owner only; discards the entry batch. Stop/join producers before destruction.
        std::size_t Pending() const;
    private:
        RenderMutationQueue() = default;
        const std::thread::id m_Owner = std::this_thread::get_id();
        mutable std::mutex m_Mutex;
        std::unique_ptr<std::unique_ptr<RenderMutation>[]> m_Commands;
        std::size_t m_Capacity{}, m_Head{}, m_Count{};
        bool m_Draining{};
    };
}
