#include "gepch.h"
#include "Renderer/RenderTasks.h"
#include <algorithm>
#include <cerrno>
#include <limits>
#include <new>
#include <process.h>
#include <Windows.h>
#include "RenderCpuBacking.h"

namespace GEngine
{
    namespace
    {
        auto Error(RenderWorkCode code, std::size_t element = 0)
        { return std::unexpected(RenderWorkError{code, element}); }
        void Owner(std::thread::id owner)
        { Asset::AssetDetail::RequireInvariant(std::this_thread::get_id() == owner); }
        struct Batch
        {
            const RenderTaskFrame& frame;
            std::span<const RenderTaskScratch> scratch;
            RenderTaskFunction callback;
            void* user;
            std::atomic<bool>& cancelled;
            std::size_t lanes;
            std::array<std::optional<RenderWorkError>, RenderTaskFrame::MaxWorkers> errors;
            std::atomic<std::size_t> next{0};
            std::atomic<bool> ready{false};
            bool abortLaunch = false;
            void Work() noexcept
            {
                ready.wait(false, std::memory_order_acquire);
                if (abortLaunch) return;
                for (;;)
                {
                    const auto lane = next.fetch_add(1, std::memory_order_relaxed);
                    if (lane >= lanes || cancelled.load(std::memory_order_relaxed)) return;
                    const auto count = frame.Inputs().size();
                    const auto base = count / lanes, extra = count % lanes;
                    const auto first = lane * base + (std::min)(lane, extra);
                    const auto size = base + (lane < extra);
                    auto result = callback({frame.Inputs().subspan(first, size), first, lane, scratch[lane].bytes, cancelled}, user);
                    if (!result) errors[lane] = result.error();
                }
            }
            static unsigned __stdcall Entry(void* value) noexcept { static_cast<Batch*>(value)->Work(); return 0; }
        };
    }

    namespace { const Component::VisibilityComponent DefaultVisibility; }
    FrozenRenderEntity::FrozenRenderEntity() noexcept
        : state(),mesh(state.cpu.meshIntent),camera(state.cpu.cameraIntent),visibility(DefaultVisibility) {}
    FrozenRenderEntity::FrozenRenderEntity(const EntityRenderState& value) noexcept
        :state(value),
        mesh(state.cpu.meshIntent),camera(state.cpu.cameraIntent),
        visibility(state.cpu.visibilityIntent?*state.cpu.visibilityIntent:DefaultVisibility) {}
    std::expected<std::unique_ptr<RenderTaskFrame>, RenderWorkError> RenderTaskFrame::Prepare(
        _Scene& scene, const RenderStateResources& resources)
    {
        if (scene.RenderData().IsExtracting()) return Error(RenderWorkCode::FrameActive);
        auto state = scene.UpdateRenderState(resources); // All lazy caches finish before the freeze.
        if (!state) return std::unexpected(RenderWorkError{RenderWorkCode::InvalidInput, 0, state.error()});
        auto frozen = scene.RenderData().BeginExtraction();
        if (!frozen) return std::unexpected(RenderWorkError{RenderWorkCode::FrameActive, 0, frozen.error()});
        std::unique_ptr<RenderTaskFrame> frame(new (std::nothrow) RenderTaskFrame);
        if (!frame) return Error(RenderWorkCode::Allocation);
        frame->m_AccessPin.emplace(resources.access.Retain());
        frame->m_Freeze.emplace(std::move(*frozen));
        const auto count = state->entities.size();
        if (count > static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)()) / sizeof(FrozenRenderEntity))
            return Error(RenderWorkCode::Capacity, count);
        if (count)
        {
            frame->m_Entities.reset(new (std::nothrow) FrozenRenderEntity[count]);
            if (!frame->m_Entities) return Error(RenderWorkCode::Allocation, count);
        }
        frame->m_Count = count; frame->m_Revisions = state->revisions; frame->m_Target = state->target;
        frame->m_FullTransformChange=state->fullTransformChange;
        frame->m_Views=std::move(state->entities);frame->m_CpuDomain=std::move(state->cpuDomain);
        for(std::size_t i=0;i<count;++i) {
            std::destroy_at(&frame->m_Entities[i]);
            std::construct_at(&frame->m_Entities[i],frame->m_Views[i]);
        }
        // The input owner references now implement per-input consumption. Keep
        // mesh groups and complete errors; do not keep redundant material owners.
        for(auto& group:frame->m_Views.Bindings()->materials)group.material.reset();
        return frame;
    }
    RenderTaskFrame::~RenderTaskFrame() { Owner(m_Owner); Asset::AssetDetail::RequireInvariant(!m_Running); }
    std::size_t RenderTaskFrame::LaneCount(RenderTaskConfig config) const noexcept
    { return (std::min)(m_Count, std::size_t{(std::max)(1u, config.workers)}); }

    std::expected<RenderTaskStats, RenderWorkError> RenderTaskFrame::Run(
        RenderTaskConfig config, std::span<const RenderTaskScratch> scratch, RenderTaskFunction callback, void* user)
    {
        Owner(m_Owner);
        if (m_Running || m_Merging) return Error(RenderWorkCode::FrameActive);
        if (config.workers > MaxWorkers) return Error(RenderWorkCode::InvalidWorkers, config.workers);
        if (!callback) return Error(RenderWorkCode::InvalidCallback);
        const auto lanes = LaneCount(config);
        if (scratch.size() != lanes) return Error(RenderWorkCode::InvalidScratch, scratch.size());
        for (std::size_t i = 0; i < lanes; ++i)
        {
            const auto start = reinterpret_cast<std::uintptr_t>(scratch[i].bytes.data());
            if ((!start && !scratch[i].bytes.empty()) || scratch[i].bytes.size() > UINTPTR_MAX - start)
                return Error(RenderWorkCode::InvalidScratch, i);
            for (std::size_t j = 0; j < i; ++j)
            {
                const auto other = reinterpret_cast<std::uintptr_t>(scratch[j].bytes.data());
                if (!scratch[i].bytes.empty() && !scratch[j].bytes.empty()
                    && start < other + scratch[j].bytes.size() && other < start + scratch[i].bytes.size())
                    return Error(RenderWorkCode::InvalidScratch, i);
            }
        }
        if (m_Cancelled.load(std::memory_order_relaxed)) return Error(RenderWorkCode::Cancelled);
        if (!lanes) return RenderTaskStats{};
        m_Running = true;
        struct RunningGuard { bool& active; ~RunningGuard() { active = false; } } guard{m_Running};
        Batch batch{*this, scratch, callback, user, m_Cancelled, lanes};
        std::array<HANDLE, MaxWorkers-1> threads{};
        std::size_t launched = 0;
        int launchError = 0;
        for (std::size_t i = 1; i < lanes; ++i)
        {
            const auto handle = _beginthreadex(nullptr, 0, Batch::Entry, &batch, 0, nullptr);
            if (!handle) { launchError = errno; batch.abortLaunch = true; break; }
            threads[launched++] = reinterpret_cast<HANDLE>(handle);
        }
        batch.ready.store(true, std::memory_order_release); batch.ready.notify_all();
        if (!launchError) batch.Work();
        for (std::size_t i = 0; i < launched; ++i)
        {
            Asset::AssetDetail::RequireInvariant(WaitForSingleObject(threads[i], INFINITE) == WAIT_OBJECT_0);
            Asset::AssetDetail::RequireInvariant(CloseHandle(threads[i]) != 0);
        }
        RenderTaskStats stats{lanes, launched + 1, m_Count};
        if (launchError)
        {
            const std::error_code diagnostic{launchError, std::generic_category()};
            if (!config.serialFallback) return std::unexpected(RenderWorkError{RenderWorkCode::WorkerLaunch, launched, diagnostic});
            stats.executionThreads = 1; stats.serialFallback = true; stats.launchDiagnostic = diagnostic;
            batch.abortLaunch = false; batch.Work(); // No callback ran before the failed launch barrier.
        }
        // All tasks have joined. Report the first failing logical lane in source
        // order; failures never travel through exceptions or exception_ptr.
        for (std::size_t i = 0; i < lanes; ++i)
            if (batch.errors[i]) { auto failure = *batch.errors[i]; failure.boundary = i; return std::unexpected(failure); }
        if (m_Cancelled.load(std::memory_order_relaxed)) return Error(RenderWorkCode::Cancelled);
        return stats;
    }

    std::expected<std::size_t, RenderWorkError> RenderTaskFrame::TransferResources(
        RenderFrameBuilder& builder, std::size_t input)
    {
        Owner(m_Owner);
        if (m_Running) return Error(RenderWorkCode::FrameActive);
        if (input >= m_Count) return Error(RenderWorkCode::InvalidInput, input);
        auto& entry = m_Entities[input].state;
        if (!entry.mesh || !entry.material) return Error(RenderWorkCode::InvalidInput, input);
        m_Merging = true;
        auto added = builder.AddSharedResources(entry.mesh, entry.material);
        if (!added) return std::unexpected(RenderWorkError{RenderWorkCode::InvalidInput, input, added.error()});
        entry.material.reset();
        RW_COUNT(resourceRows,1);
        return *added;
    }

    std::expected<std::unique_ptr<RenderMutationQueue>, RenderWorkError> RenderMutationQueue::Create(std::size_t capacity)
    {
        if (capacity > static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)()) / sizeof(std::unique_ptr<RenderMutation>))
            return Error(RenderWorkCode::Capacity, capacity);
        std::unique_ptr<RenderMutationQueue> queue(new (std::nothrow) RenderMutationQueue);
        if (!queue) return Error(RenderWorkCode::Allocation);
        if (capacity)
        {
            queue->m_Commands.reset(new (std::nothrow) std::unique_ptr<RenderMutation>[capacity]);
            if (!queue->m_Commands) return Error(RenderWorkCode::Allocation, capacity);
        }
        queue->m_Capacity = capacity;
        return queue;
    }
    RenderMutationQueue::~RenderMutationQueue() { Owner(m_Owner); Asset::AssetDetail::RequireInvariant(!m_Draining); }
    std::expected<void, RenderWorkError> RenderMutationQueue::Enqueue(std::unique_ptr<RenderMutation>& command)
    {
        if (!command) return Error(RenderWorkCode::InvalidInput);
        const std::lock_guard lock(m_Mutex);
        if (m_Count == m_Capacity) return Error(RenderWorkCode::Capacity, m_Capacity);
        m_Commands[(m_Head + m_Count) % m_Capacity] = std::move(command); ++m_Count;
        return {};
    }
    std::size_t RenderMutationQueue::Pending() const { const std::lock_guard lock(m_Mutex); return m_Count; }
    void RenderMutationQueue::CancelPending()
    {
        Owner(m_Owner); Asset::AssetDetail::RequireInvariant(!m_Draining);
        const auto count = Pending();
        m_Draining = true;
        struct CancelGuard { bool& active; ~CancelGuard() { active = false; } } guard{m_Draining};
        for (std::size_t i = 0; i < count; ++i)
        {
            std::unique_ptr<RenderMutation> command;
            {
                const std::lock_guard lock(m_Mutex);
                command = std::move(m_Commands[m_Head]); m_Head = (m_Head + 1) % m_Capacity; --m_Count;
            } // Destroy CPU payload outside the queue lock.
        }
    }
    std::expected<std::size_t, RenderWorkError> RenderMutationQueue::Drain(_Scene& scene, Asset::AssetPublication& publication)
    {
        Owner(m_Owner);
        if (m_Draining || scene.RenderData().IsExtracting() || !publication.CanPublish()) return Error(RenderWorkCode::FrameActive);
        const auto count = Pending(); // The frame's completion cutoff; subsequent arrivals stay queued.
        m_Draining = true;
        struct DrainGuard { bool& active; ~DrainGuard() { active = false; } } guard{m_Draining};
        auto token = publication.BeginPublication();
        for (std::size_t i = 0; i < count; ++i)
        {
            std::unique_ptr<RenderMutation> command;
            {
                const std::lock_guard lock(m_Mutex);
                command = std::move(m_Commands[m_Head]); m_Head = (m_Head + 1) % m_Capacity; --m_Count;
            }
            auto applied = command->Apply(scene, token);
            if (!applied) { auto failure = applied.error(); failure.boundary = i; return std::unexpected(failure); }
        }
        return count;
    }
}
