#pragma once

#include "Assets/AssetPublication.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <system_error>

namespace GEngine { class FrameScheduler; }

namespace GEngine::Asset
{
    struct UploadRequestTag;
    using UploadTicket = AssetHandle<UploadRequestTag>;
    enum class AsyncAssetState { Requested, Loading, CpuReady, Uploading, Ready, Failed, Cancelled };
    enum class UploadCode { InvalidLimits, InvalidRequest, Capacity, Oversized, Allocation,
        WorkerLaunch, Closed, InvalidTicket, Busy, Cancelled, IdentityExhausted,
        DecodeFailed, UploadFailed, PayloadSize, PublicationBusy };
    struct UploadError
    {
        UploadCode code{};
        std::error_code system;
        std::optional<RegistryError> registry;
        std::size_t detail{};
    };
    using UploadResult = std::expected<void, UploadError>;
    struct AsyncAssetStatus
    {
        AsyncAssetState state{};
        std::optional<UploadError> error;
    };
    struct UploadLimits
    {
        std::uint32_t requests = 64;
        std::uint32_t workers = 2; // Maintained CRT workers, 1..64.
        std::size_t decodedPayloads = 8;
        std::size_t decodedBytes = 64 * 1024 * 1024;
        std::size_t queuedRequests = 8;
        std::size_t queuedBytes = 32 * 1024 * 1024;
        std::size_t frameBytes = 8 * 1024 * 1024;
        // Checked between indivisible requests; the first always makes progress.
        // An individual upload may exceed this time limit, never the byte limit.
        std::chrono::nanoseconds frameTime = std::chrono::milliseconds(2);
    };
    struct UploadQueueStats
    {
        std::size_t reservedPayloads{}, reservedBytes{}, queuedRequests{}, queuedBytes{};
    };
    struct UploadDrainStats
    {
        std::size_t completed{}, failed{}, bytes{};
        bool byteBudgetReached{}, timeBudgetReached{};
    };
    class UploadCancellation final
    {
    public:
        bool StopRequested() const noexcept { return m_Stop.load(std::memory_order_acquire); }
    private:
        friend class AsyncUploadQueue;
        explicit UploadCancellation(const std::atomic<bool>& stop) : m_Stop(stop) {}
        const std::atomic<bool>& m_Stop;
    };

    // A CPU-only immutable owning request, transferred once from Decode. Bytes
    // counts all retained decoded storage (not GPU bytes); no external mutable
    // aliases. Apply may create/replace/destroy only via the supplied publication
    // token and existing registries. It must be transactional on failure. Registry
    // leases/fences retain old frame versions and prohibit unsafe GPU storage reuse.
    // Destruction performs no GL and does not reenter this queue.
    class UploadRequest
    {
    public:
        virtual ~UploadRequest() = default;
        virtual std::size_t Bytes() const noexcept = 0;
        virtual UploadResult Apply(const AssetPublication::Publication&) const noexcept = 0;
    };
    // CPU work only; never touches GL, ECS or registries. The reservation is an
    // upper bound on simultaneously resident decoded storage, including temporary
    // decode buffers. Decode must honor it, poll cancellation, and return without
    // detached work or exception transport. Source/descriptor storage is caller
    // controlled; this queue bounds decoded storage, not arbitrary source caches.
    class AssetDecodeJob
    {
    public:
        virtual ~AssetDecodeJob() = default;
        virtual std::expected<std::unique_ptr<const UploadRequest>, UploadError> Decode(
            UploadCancellation, std::size_t reservedBytes) const noexcept = 0;
    };

    // Create/shutdown/destruction belong to the context owner; submission, status,
    // cancellation and release are thread safe. The publication domain and all
    // callback dependencies outlive this queue. Stop external producers, Shutdown
    // (close admission, cancel/wake, join, discard CPU storage), then retire frames,
    // registries/fences, and finally the context. Destruction calls Shutdown.
    // No worker waits for the render owner during shutdown. A callback that does
    // not cooperate can delay join; forcibly terminating workers is unsupported.
    class AsyncUploadQueue final
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<AsyncUploadQueue>, UploadError> Create(
            AssetPublication&, UploadLimits = {});
        ~AsyncUploadQueue();
        AsyncUploadQueue(const AsyncUploadQueue&) = delete;
        AsyncUploadQueue& operator=(const AsyncUploadQueue&) = delete;
        // Reserve before decoding; capacity failure preserves job and superseded
        // request. Oversized requests are rejected, never allowed to block FIFO.
        // Successful supersession cancels active old work before admitting the
        // new ticket; terminal diagnostics remain until Release. Registry
        // replacement happens only if the later Apply succeeds.
        [[nodiscard]] std::expected<UploadTicket, UploadError> Submit(
            std::unique_ptr<const AssetDecodeJob>& job, std::size_t maxDecodedBytes,
            std::optional<UploadTicket> supersedes = {});
        UploadResult Cancel(UploadTicket); // Uploading is too late: typed Busy.
        UploadResult Release(UploadTicket); // Terminal AND worker/queue quiescent.
        [[nodiscard]] std::expected<AsyncAssetStatus, UploadError> Status(UploadTicket) const;
        UploadQueueStats Stats() const;
        UploadDrainStats LastDrain() const; // Owner only, last scheduler update.
        void Shutdown(); // Idempotent owner join; statuses remain queryable.
        AssetPublication& Publication() const noexcept;
    private:
        friend class ::GEngine::FrameScheduler;
        struct Impl;
        explicit AsyncUploadQueue(std::unique_ptr<Impl>);
        // Only FrameScheduler can drain, at UpdateFrameResources before freezing.
        UploadResult DrainUpdateFrameResources();
        std::unique_ptr<Impl> m_Impl;
    };
}
