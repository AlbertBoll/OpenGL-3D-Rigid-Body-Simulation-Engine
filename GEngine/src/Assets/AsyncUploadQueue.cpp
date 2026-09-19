#include "gepch.h"
#include "Assets/AsyncUploadQueue.h"
#include "Core/GLContextThread.h"
#include <array>
#include <cerrno>
#include <condition_variable>
#include <limits>
#include <new>
#include <process.h>
#include <Windows.h>

namespace GEngine::Asset
{
    namespace
    {
        auto Error(UploadCode code, std::size_t detail = 0)
        { return std::unexpected(UploadError{code, {}, {}, detail}); }
        bool Terminal(AsyncAssetState state)
        { return state == AsyncAssetState::Ready || state == AsyncAssetState::Failed || state == AsyncAssetState::Cancelled; }
    }
    struct AsyncUploadQueue::Impl
    {
        struct Slot
        {
            std::uint64_t generation = 1, order{}, queueOrder{};
            bool occupied{}, busy{}, queued{};
            std::atomic<bool> stop{false};
            AsyncAssetStatus status;
            std::size_t reservation{}, bytes{};
            std::unique_ptr<const AssetDecodeJob> job;
            std::unique_ptr<const UploadRequest> upload;
        };
        AssetPublication& publication;
        const std::thread::id owner = std::this_thread::get_id();
        UploadLimits limits;
        std::uint64_t identity{}, nextOrder = 1, nextQueueOrder = 1;
        SDL_GLContext uploadContext{}; // Private identity; CPU-only creation binds at first drain.
        std::unique_ptr<Slot[]> slots;
        std::unique_ptr<std::uint32_t[]> queue;
        std::array<HANDLE, 64> threads{};
        std::size_t launched{}, head{};
        UploadQueueStats stats;
        UploadDrainStats last;
        mutable std::mutex mutex;
        std::condition_variable changed;
        bool closed{}, draining{};

        Impl(AssetPublication& p, UploadLimits l) : publication(p), limits(l) {}
        void RequireOwner() const { AssetDetail::RequireInvariant(owner == std::this_thread::get_id()); }
        Slot* Find(UploadTicket ticket) const
        {
            if (ticket.registry != identity || ticket.index >= limits.requests) return nullptr;
            auto& slot = slots[ticket.index];
            return slot.occupied && slot.generation == ticket.generation ? &slot : nullptr;
        }
        void Discard(Slot& slot)
        {
            slot.upload.reset(); slot.job.reset();
            if (slot.reservation) {
                --stats.reservedPayloads; stats.reservedBytes -= slot.reservation;
                slot.reservation = 0;
            }
            slot.bytes = 0;
        }
        void CancelSlot(Slot& slot)
        {
            slot.stop.store(true, std::memory_order_release);
            slot.status = {AsyncAssetState::Cancelled, UploadError{UploadCode::Cancelled}};
            if (slot.queued) {
                // Compact in place, preserving completion FIFO and queue accounting.
                std::size_t kept = 0;
                for (std::size_t i = 0; i < stats.queuedRequests; ++i) {
                    const auto index = queue[(head + i) % limits.queuedRequests];
                    if (&slots[index] != &slot) queue[(head + kept++) % limits.queuedRequests] = index;
                }
                stats.queuedRequests = kept; stats.queuedBytes -= slot.bytes;
                slot.queued = false;
            }
            if (!slot.busy) Discard(slot);
        }
        std::uint32_t Next() const
        {
            auto index = UploadTicket::NullIndex;
            auto order = (std::numeric_limits<std::uint64_t>::max)();
            for (std::uint32_t i = 0; i < limits.requests; ++i)
                if (slots[i].occupied && slots[i].status.state == AsyncAssetState::Requested && slots[i].order <= order)
                { index = i; order = slots[i].order; }
            return index;
        }
        static unsigned __stdcall Entry(void* user) noexcept
        { static_cast<Impl*>(user)->Work(); return 0; }
        void Work() noexcept
        {
            for (;;) {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return closed || Next() != UploadTicket::NullIndex; });
                if (closed) return;
                const auto index = Next();
                auto& slot = slots[index];
                const auto generation = slot.generation;
                slot.busy = true; slot.status.state = AsyncAssetState::Loading;
                lock.unlock();
                auto decoded = slot.job->Decode(UploadCancellation(slot.stop), slot.reservation);
                lock.lock();
                // No slot can be recycled while busy. Check generation as well as
                // cancellation so late decode results never publish a replaced job.
                if (closed || slot.stop.load(std::memory_order_acquire) || slot.generation != generation) {
                    decoded = Error(UploadCode::Cancelled);
                    slot.status = {AsyncAssetState::Cancelled, UploadError{UploadCode::Cancelled}};
                }
                else if (!decoded) slot.status = {AsyncAssetState::Failed, decoded.error()};
                else if (!*decoded || (*decoded)->Bytes() > slot.reservation) {
                    const auto bytes = *decoded ? (*decoded)->Bytes() : 0;
                    decoded = Error(UploadCode::PayloadSize, bytes);
                    slot.status = {AsyncAssetState::Failed, decoded.error()};
                }
                if (!decoded) { slot.busy = false; Discard(slot); changed.notify_all(); continue; }
                slot.upload = std::move(*decoded); slot.bytes = slot.upload->Bytes(); slot.job.reset();
                slot.status.state = AsyncAssetState::CpuReady;
                // CPU-ready payload retains its pre-decode reservation while waiting.
                changed.wait(lock, [&] { return closed || slot.stop.load(std::memory_order_acquire) ||
                    (stats.queuedRequests < limits.queuedRequests && slot.bytes <= limits.queuedBytes - stats.queuedBytes); });
                slot.busy = false;
                if (closed || slot.stop.load(std::memory_order_acquire)) Discard(slot);
                else if (!nextQueueOrder) {
                    slot.status = {AsyncAssetState::Failed, UploadError{UploadCode::IdentityExhausted}};
                    Discard(slot);
                }
                else {
                    slot.queueOrder = nextQueueOrder++;
                    queue[(head + stats.queuedRequests) % limits.queuedRequests] = index;
                    ++stats.queuedRequests; stats.queuedBytes += slot.bytes; slot.queued = true;
                }
                changed.notify_all();
            }
        }
    };
    AsyncUploadQueue::AsyncUploadQueue(std::unique_ptr<Impl> impl) : m_Impl(std::move(impl)) {}
    std::expected<std::unique_ptr<AsyncUploadQueue>, UploadError> AsyncUploadQueue::Create(
        AssetPublication& publication, UploadLimits limits)
    {
        if (!limits.requests || limits.requests == UploadTicket::NullIndex || !limits.workers || limits.workers > 64 ||
            !limits.decodedPayloads || !limits.decodedBytes || !limits.queuedRequests || !limits.queuedBytes ||
            !limits.frameBytes || limits.frameTime.count() <= 0 || limits.queuedRequests > limits.requests)
            return Error(UploadCode::InvalidLimits);
        if (!publication.CanPublish()) return Error(UploadCode::PublicationBusy);
        auto identity = AssetDetail::TakeRegistryIdentity(AssetDetail::nextRegistryIdentity);
        if (!identity) return Error(UploadCode::IdentityExhausted);
        auto impl = std::unique_ptr<Impl>(new(std::nothrow) Impl(publication, limits));
        if (!impl) return Error(UploadCode::Allocation);
        impl->slots.reset(new(std::nothrow) Impl::Slot[limits.requests]);
        impl->queue.reset(new(std::nothrow) std::uint32_t[limits.queuedRequests]);
        if (!impl->slots || !impl->queue) return Error(UploadCode::Allocation);
        impl->identity = *identity;
        auto result = std::unique_ptr<AsyncUploadQueue>(new(std::nothrow) AsyncUploadQueue(std::move(impl)));
        if (!result) return Error(UploadCode::Allocation);
        auto& state = *result->m_Impl;
        if (GLContextThread::IsCurrentOwner()) state.uploadContext = SDL_GL_GetCurrentContext();
        for (; state.launched < limits.workers; ++state.launched) {
            const auto thread = _beginthreadex(nullptr, 0, Impl::Entry, &state, 0, nullptr);
            if (!thread) {
                const UploadError error{UploadCode::WorkerLaunch, {errno, std::generic_category()}, {}, state.launched};
                result->Shutdown(); return std::unexpected(error);
            }
            state.threads[state.launched] = reinterpret_cast<HANDLE>(thread);
        }
        return result;
    }
    AsyncUploadQueue::~AsyncUploadQueue() { Shutdown(); }
    std::expected<UploadTicket, UploadError> AsyncUploadQueue::Submit(
        std::unique_ptr<const AssetDecodeJob>& job, std::size_t bytes, std::optional<UploadTicket> supersedes)
    {
        auto& s = *m_Impl;
        const std::lock_guard lock(s.mutex);
        if (s.closed) return Error(UploadCode::Closed);
        if (!job || !bytes) return Error(UploadCode::InvalidRequest);
        if (bytes > s.limits.decodedBytes || bytes > s.limits.queuedBytes || bytes > s.limits.frameBytes)
            return Error(UploadCode::Oversized, bytes);
        Impl::Slot* old = nullptr;
        if (supersedes) {
            old = s.Find(*supersedes);
            if (!old) return Error(UploadCode::InvalidTicket);
            if (old->status.state == AsyncAssetState::Uploading) return Error(UploadCode::Busy);
        }
        if (!s.nextOrder) return Error(UploadCode::IdentityExhausted);
        if (s.stats.reservedPayloads == s.limits.decodedPayloads || bytes > s.limits.decodedBytes - s.stats.reservedBytes)
            return Error(UploadCode::Capacity, bytes);
        for (std::uint32_t i = 0; i < s.limits.requests; ++i) {
            auto& slot = s.slots[i];
            if (slot.occupied || !slot.generation) continue;
            if (old && !Terminal(old->status.state)) s.CancelSlot(*old);
            slot.occupied = true; slot.stop.store(false, std::memory_order_release);
            slot.order = s.nextOrder++;
            slot.status = {AsyncAssetState::Requested, {}};
            slot.reservation = bytes; slot.job = std::move(job);
            ++s.stats.reservedPayloads; s.stats.reservedBytes += bytes;
            s.changed.notify_all();
            return UploadTicket{i, slot.generation, s.identity};
        }
        return Error(UploadCode::Capacity);
    }
    UploadResult AsyncUploadQueue::Cancel(UploadTicket ticket)
    {
        auto& s = *m_Impl;
        const std::lock_guard lock(s.mutex);
        auto* slot = s.Find(ticket);
        if (!slot) return Error(UploadCode::InvalidTicket);
        if (slot->status.state == AsyncAssetState::Uploading || slot->status.state == AsyncAssetState::Ready)
            return Error(UploadCode::Busy);
        if (!Terminal(slot->status.state)) s.CancelSlot(*slot);
        s.changed.notify_all(); return {};
    }
    UploadResult AsyncUploadQueue::Release(UploadTicket ticket)
    {
        auto& s = *m_Impl;
        const std::lock_guard lock(s.mutex);
        auto* slot = s.Find(ticket);
        if (!slot) return Error(UploadCode::InvalidTicket);
        if (!Terminal(slot->status.state) || slot->busy || slot->queued) return Error(UploadCode::Busy);
        s.Discard(*slot); slot->occupied = false;
        ++slot->generation; // Zero permanently quarantines exhausted generations.
        return {};
    }
    std::expected<AsyncAssetStatus, UploadError> AsyncUploadQueue::Status(UploadTicket ticket) const
    {
        auto& s = *m_Impl; const std::lock_guard lock(s.mutex);
        const auto* slot = s.Find(ticket);
        if (!slot) return Error(UploadCode::InvalidTicket);
        return slot->status;
    }
    UploadQueueStats AsyncUploadQueue::Stats() const
    { const std::lock_guard lock(m_Impl->mutex); return m_Impl->stats; }
    UploadDrainStats AsyncUploadQueue::LastDrain() const { m_Impl->RequireOwner(); return m_Impl->last; }
    AssetPublication& AsyncUploadQueue::Publication() const noexcept { return m_Impl->publication; }
    UploadResult AsyncUploadQueue::DrainUpdateFrameResources()
    {
        auto& s = *m_Impl; s.RequireOwner();
        GLContextThread::RequireCurrent("AsyncUploadQueue::UpdateFrameResources");
        if (!s.uploadContext) s.uploadContext = SDL_GL_GetCurrentContext();
        AssetDetail::RequireInvariant(s.uploadContext == SDL_GL_GetCurrentContext());
        if (!s.publication.CanPublish() || s.draining) return Error(UploadCode::PublicationBusy);
        std::unique_lock lock(s.mutex);
        s.last = {};
        if (s.closed) return Error(UploadCode::Closed);
        s.draining = true;
        struct End { bool& draining; ~End() { draining = false; } } end{s.draining};
        // Freeze the entry batch. Later completions cannot join this frame even
        // when cancellation removes an entry; order is captured by ticket cutoff.
        const auto cutoff = s.nextQueueOrder - 1;
        const auto entries = s.stats.queuedRequests;
        const auto start = std::chrono::steady_clock::now();
        auto publication = s.publication.BeginPublication();
        for (std::size_t i = 0; i < entries && s.stats.queuedRequests; ++i) {
            auto& slot = s.slots[s.queue[s.head]];
            if (slot.queueOrder > cutoff) break;
            if (slot.bytes > s.limits.frameBytes - s.last.bytes) { s.last.byteBudgetReached = true; break; }
            // Cooperative time budget: one request always makes progress; a
            // single indivisible upload can overrun, but no next upload starts.
            if (i && std::chrono::steady_clock::now() - start >= s.limits.frameTime) { s.last.timeBudgetReached = true; break; }
            s.head = (s.head + 1) % s.limits.queuedRequests;
            --s.stats.queuedRequests; s.stats.queuedBytes -= slot.bytes;
            slot.queued = false; slot.busy = true; slot.status.state = AsyncAssetState::Uploading;
            s.changed.notify_all();
            const auto bytes = slot.bytes;
            lock.unlock();
            // Cancellation/supersession returns Busy once uploading is claimed.
            // No queue lock surrounds user code; polling/submission stays usable.
            auto uploaded = slot.upload->Apply(publication);
            GLContextThread::RequireCurrent("AsyncUploadQueue::Apply completion");
            AssetDetail::RequireInvariant(s.uploadContext == SDL_GL_GetCurrentContext());
            lock.lock();
            slot.status = uploaded ? AsyncAssetStatus{AsyncAssetState::Ready, {}} :
                AsyncAssetStatus{AsyncAssetState::Failed, uploaded.error()};
            slot.busy = false;
            if (uploaded) ++s.last.completed; else ++s.last.failed;
            s.last.bytes += bytes;
            s.Discard(slot); s.changed.notify_all();
        }
        return {};
    }
    void AsyncUploadQueue::Shutdown()
    {
        auto& s = *m_Impl; s.RequireOwner();
        AssetDetail::RequireInvariant(!s.draining);
        {
            const std::lock_guard lock(s.mutex);
            s.closed = true;
            for (std::uint32_t i = 0; i < s.limits.requests; ++i)
                if (s.slots[i].occupied && !Terminal(s.slots[i].status.state)) s.CancelSlot(s.slots[i]);
            s.changed.notify_all();
        }
        for (std::size_t i = 0; i < s.launched; ++i) {
            AssetDetail::RequireInvariant(WaitForSingleObject(s.threads[i], INFINITE) == WAIT_OBJECT_0);
            AssetDetail::RequireInvariant(CloseHandle(s.threads[i]) != 0);
        }
        s.launched = 0;
        const std::lock_guard lock(s.mutex);
        for (std::uint32_t i = 0; i < s.limits.requests; ++i) s.Discard(s.slots[i]);
    }
}
