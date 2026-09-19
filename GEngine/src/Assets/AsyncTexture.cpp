#include "gepch.h"
#include "Assets/Textures/AsyncTexture.h"
#include "Core/GLContextThread.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <io.h>
#include <new>
#include <tuple>

// A private instance of the bundled decoder gives this path bounded allocation
// without changing its dependency/version or global legacy decoder settings.
namespace
{
    struct DecodeBudget { std::size_t limit{}, used{}, peak{}; bool exhausted{}; };
    struct alignas(std::max_align_t) DecodeAllocation { DecodeBudget* budget; std::size_t size; };
    thread_local DecodeBudget* activeDecodeBudget{};
    void* DecodeAllocate(std::size_t bytes)
    {
        auto& budget = *activeDecodeBudget;
        if (bytes > SIZE_MAX - sizeof(DecodeAllocation) || bytes + sizeof(DecodeAllocation) > budget.limit - budget.used)
        { budget.exhausted = true; return nullptr; }
        const auto total = bytes + sizeof(DecodeAllocation);
        auto* block = static_cast<DecodeAllocation*>(std::malloc(total));
        if (!block) { budget.exhausted = true; return nullptr; }
        *block = {&budget, total}; budget.used += total; budget.peak = (std::max)(budget.peak, budget.used);
        return block + 1;
    }
    void DecodeFree(void* pointer)
    {
        if (!pointer) return;
        auto* block = static_cast<DecodeAllocation*>(pointer) - 1;
        block->budget->used -= block->size; std::free(block);
    }
    void* DecodeReallocate(void* pointer, std::size_t bytes)
    {
        if (!pointer) return DecodeAllocate(bytes);
        auto* block = static_cast<DecodeAllocation*>(pointer) - 1;
        auto& budget = *block->budget;
        if (bytes > SIZE_MAX - sizeof(DecodeAllocation) || bytes + sizeof(DecodeAllocation) > budget.limit - budget.used + block->size)
        { budget.exhausted = true; return nullptr; }
        const auto total = bytes + sizeof(DecodeAllocation), old = block->size;
        auto* resized = static_cast<DecodeAllocation*>(std::realloc(block, total));
        if (!resized) { budget.exhausted = true; return nullptr; }
        resized->size = total; budget.used = budget.used - old + total; budget.peak = (std::max)(budget.peak, budget.used);
        return resized + 1;
    }
}
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_MALLOC(bytes) DecodeAllocate(bytes)
#define STBI_REALLOC_SIZED(pointer, oldBytes, bytes) DecodeReallocate(pointer, bytes)
#define STBI_FREE(pointer) DecodeFree(pointer)
#include "stb_image/stb_image.h"

namespace GEngine::Asset
{
    namespace
    {
        UploadError QueueError(UploadCode code) { return {code}; }
        TextureError ImageError(TextureErrorCode code, const std::filesystem::path& path, std::string message,
            std::error_code system = {}) { return {code, path.string(), std::move(message), system}; }
        using DecodedBytes = std::unique_ptr<void, decltype(&DecodeFree)>;
        struct CpuImage
        {
            // The allocator header refers to this budget through worker/owner transfer.
            // Reverse destruction frees the pixels before their accounting owner.
            std::unique_ptr<DecodeBudget> budget;
            DecodedBytes pixels{nullptr, DecodeFree};
            TextureDesc desc;
            std::size_t bytes{}, stride{};
        };
        bool ValidDescription(const TextureDesc& d)
        {
            return d.kind == TextureKind::Image2D && d.usage == TextureUsage::Sampled && !d.width && !d.height &&
                (d.format == TextureFormat::RGB8 || d.format == TextureFormat::RGBA8 ||
                    (d.format == TextureFormat::R8 && d.colorSpace == TextureColorSpace::Linear)) &&
                (d.colorSpace == TextureColorSpace::Linear || d.colorSpace == TextureColorSpace::SRGB) &&
                (d.mips == TextureMipIntent::None || d.mips == TextureMipIntent::Generate) &&
                (d.orientation == ImageOrientation::TopLeft || d.orientation == ImageOrientation::BottomLeft);
        }
    }
    struct AsyncTextureLoader::Impl
    {
        struct Record
        {
            UploadTicket leader;
            std::filesystem::path canonical;
            TextureDesc description;
            TextureHandle image{};
            std::optional<TextureError> error;
            bool cancelled{};
        };
        struct Entry
        {
            UploadTicket ticket;
            std::filesystem::path path;
            TextureDesc requested;
            std::shared_ptr<Record> record;
            std::optional<TextureError> error;
            bool cancelled{};
        };
        // Native open-file identity is confined to this implementation. It coalesces
        // hard links without blocking owner Status on filesystem equivalence calls.
        using FileKey = std::tuple<DWORD, DWORD, DWORD, TextureDesc>;
        using RequestKey = std::pair<std::filesystem::path, TextureDesc>;
        const std::thread::id owner = std::this_thread::get_id();
        TextureRegistry& images;
        std::filesystem::path root;
        AsyncTextureLimits limits;
        std::unique_ptr<AsyncUploadQueue> queue;
        mutable std::mutex mutex;
        std::map<RequestKey, std::shared_ptr<Entry>> requests;
        std::map<UploadTicket, std::shared_ptr<Entry>> tickets;
        std::map<FileKey, std::shared_ptr<Record>> canonical;
        AsyncTextureStats stats;
        bool closed{};
        Impl(TextureRegistry& registry, std::filesystem::path path, AsyncTextureLimits l)
            : images(registry), root(std::move(path)), limits(l) {}
        void Owner() const { AssetDetail::RequireInvariant(owner == std::this_thread::get_id()); }

        struct Upload final : UploadRequest
        {
            Impl& owner;
            std::shared_ptr<Record> record;
            CpuImage image;
            Upload(Impl& o, std::shared_ptr<Record> r, CpuImage payload)
                : owner(o), record(std::move(r)), image(std::move(payload)) {}
            std::size_t Bytes() const noexcept override { return image.budget ? image.budget->used : 0; }
            UploadResult Apply(const AssetPublication::Publication& publication) const noexcept override
            {
                if (!image.pixels) return {}; // Canonical alias: leader owns publication.
                const std::lock_guard lock(owner.mutex);
                if (record->cancelled) return std::unexpected(QueueError(UploadCode::Cancelled));
                auto gpu = TextureResource::Create(image.desc, {{static_cast<const std::byte*>(image.pixels.get()), image.bytes}, image.stride});
                if (!gpu) { record->error = gpu.error(); record->error->source = record->canonical.string(); return std::unexpected(QueueError(UploadCode::UploadFailed)); }
                auto published = owner.images.Create(publication, std::move(*gpu));
                if (!published) {
                    record->error = TextureError{TextureErrorCode::Registry, record->canonical.string(), "Async texture publication failed", {}, published.error()};
                    return std::unexpected(UploadError{UploadCode::UploadFailed, {}, published.error()});
                }
                record->image = *published; record->description = image.desc; ++owner.stats.uploads;
                return {};
            }
        };
        struct Job final : AssetDecodeJob
        {
            Impl& owner; std::shared_ptr<Entry> entry;
            Job(Impl& o, std::shared_ptr<Entry> e) : owner(o), entry(std::move(e)) {}
            bool Stopped(UploadCancellation cancel) const
            {
                const std::lock_guard lock(owner.mutex);
                return cancel.StopRequested() || entry->cancelled || (entry->record && entry->record->cancelled);
            }
            auto Fail(TextureError error) const
            {
                const std::lock_guard lock(owner.mutex);
                if (entry->record) entry->record->error = std::move(error);
                else entry->error = std::move(error);
                return std::unexpected(QueueError(UploadCode::DecodeFailed));
            }
            std::expected<std::unique_ptr<const UploadRequest>, UploadError> Decode(
                UploadCancellation cancel, std::size_t reservation) const noexcept override
            {
                AssetDetail::RequireInvariant(std::this_thread::get_id() != owner.owner);
                if (Stopped(cancel)) return std::unexpected(QueueError(UploadCode::Cancelled));
                std::error_code ec;
                auto path = std::filesystem::canonical(entry->path, ec);
                if (ec) return Fail(ImageError(TextureErrorCode::FileSystem, entry->path, ec.message(), ec));
                FILE* raw{};
                const auto opened = _wfopen_s(&raw, path.c_str(), L"rb");
                if (opened || !raw) return Fail(ImageError(TextureErrorCode::FileSystem, path, "Opening image failed", {opened, std::generic_category()}));
                std::unique_ptr<FILE, decltype(&std::fclose)> file(raw, std::fclose);
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(raw))), &info))
                    return Fail(ImageError(TextureErrorCode::FileSystem, path, "Reading file identity failed", {int(GetLastError()), std::system_category()}));
                const FileKey key{info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow, entry->requested};
                {
                    const std::lock_guard lock(owner.mutex);
                    if (entry->cancelled || cancel.StopRequested()) return std::unexpected(QueueError(UploadCode::Cancelled));
                    if (auto found = owner.canonical.find(key); found != owner.canonical.end()) {
                        entry->record = found->second;
                        auto alias = std::unique_ptr<const UploadRequest>(new(std::nothrow) Upload(owner, entry->record, {}));
                        if (!alias) return std::unexpected(QueueError(UploadCode::Allocation));
                        return alias;
                    }
                    entry->record = std::make_shared<Record>();
                    entry->record->leader = entry->ticket; entry->record->canonical = path;
                    entry->record->description = entry->requested;
                    owner.canonical.emplace(key, entry->record);
                }
                if (_fseeki64(raw, 0, SEEK_END) != 0) return Fail(ImageError(TextureErrorCode::FileSystem, path, "Seeking image failed", {errno, std::generic_category()}));
                const auto length = _ftelli64(raw);
                if (length < 0 || _fseeki64(raw, 0, SEEK_SET) != 0)
                    return Fail(ImageError(TextureErrorCode::FileSystem, path, "Measuring image failed", {errno, std::generic_category()}));
                if (!length) return Fail(ImageError(TextureErrorCode::Decode, path, "Empty image file"));
                if (std::uint64_t(length) > INT_MAX || std::uint64_t(length) > reservation)
                    return Fail(ImageError(TextureErrorCode::Allocation, path, "Encoded image exceeds reserved request bytes"));
                CpuImage image;
                image.budget.reset(new(std::nothrow) DecodeBudget{reservation});
                if (!image.budget) return Fail(ImageError(TextureErrorCode::Allocation, path, "Decode budget allocation failed"));
                struct BudgetScope
                {
                    DecodeBudget* previous;
                    explicit BudgetScope(DecodeBudget& budget) : previous(std::exchange(activeDecodeBudget, &budget)) {}
                    ~BudgetScope() { activeDecodeBudget = previous; }
                } budget(*image.budget);
                DecodedBytes encoded(DecodeAllocate(std::size_t(length)), DecodeFree);
                if (!encoded) return Fail(ImageError(TextureErrorCode::Allocation, path, "Encoded image exceeds decoder budget"));
                std::size_t offset = 0;
                while (offset < std::size_t(length)) {
                    if (Stopped(cancel)) return std::unexpected(QueueError(UploadCode::Cancelled));
                    const auto chunk = (std::min)(std::size_t(length) - offset, std::size_t{64 * 1024});
                    if (std::fread(static_cast<std::byte*>(encoded.get()) + offset, 1, chunk, raw) != chunk)
                        return Fail(ImageError(TextureErrorCode::FileSystem, path, "Short or failed image read", {errno ? errno : EIO, std::generic_category()}));
                    offset += chunk;
                }
                if (Stopped(cancel)) return std::unexpected(QueueError(UploadCode::Cancelled));
                {
                    const std::lock_guard lock(owner.mutex); ++owner.stats.fileReads; ++owner.stats.decodes;
                }
                image.desc = entry->requested;
                const int channels = image.desc.format == TextureFormat::R8 ? 1 : image.desc.format == TextureFormat::RGB8 ? 3 : 4;
                int sourceChannels{};
                stbi_set_flip_vertically_on_load_thread(image.desc.orientation == ImageOrientation::BottomLeft);
                image.pixels.reset(stbi_load_from_memory(static_cast<const stbi_uc*>(encoded.get()), int(length),
                    &image.desc.width, &image.desc.height, &sourceChannels, channels));
                encoded.reset(); // Only the immutable decoded buffer crosses the boundary.
                {
                    const std::lock_guard lock(owner.mutex);
                    owner.stats.peakRequestBytes = (std::max)(owner.stats.peakRequestBytes, image.budget->peak);
                }
                if (Stopped(cancel)) return std::unexpected(QueueError(UploadCode::Cancelled));
                if (!image.pixels) return Fail(ImageError(image.budget->exhausted ? TextureErrorCode::Allocation : TextureErrorCode::Decode,
                    path, image.budget->exhausted ? "Image decode exceeded reserved memory" : stbi_failure_reason() ? stbi_failure_reason() : "Image decode failed"));
                if (image.desc.width <= 0 || image.desc.height <= 0 || std::size_t(image.desc.width) > SIZE_MAX / std::size_t(channels) / std::size_t(image.desc.height))
                    return Fail(ImageError(TextureErrorCode::Decode, path, "Invalid decoded image extent"));
                image.stride = std::size_t(image.desc.width) * std::size_t(channels);
                image.bytes = image.stride * std::size_t(image.desc.height);
                auto upload = std::unique_ptr<const UploadRequest>(new(std::nothrow) Upload(owner, entry->record, std::move(image)));
                if (!upload) return Fail(ImageError(TextureErrorCode::Allocation, path, "Upload request allocation failed"));
                return upload;
            }
        };
    };
    std::string DescribeAsyncTextureError(const AsyncTextureError& error)
    {
        return std::visit([](const auto& cause) {
            if constexpr (std::same_as<std::decay_t<decltype(cause)>, TextureError>)
                return std::format("texture code={} source={} message={} system={} registry={}", int(cause.code), cause.source, cause.message, cause.system.message(), int(cause.registry));
            else return std::format("texture queue code={} detail={} system={} registry={}", int(cause.code), cause.detail, cause.system.message(), cause.registry ? int(*cause.registry) : -1);
        }, error);
    }
    AsyncTextureLoader::AsyncTextureLoader(std::unique_ptr<Impl> impl) : m_Impl(std::move(impl)) {}
    AsyncTextureLoader::~AsyncTextureLoader() { Shutdown(); }
    std::expected<std::unique_ptr<AsyncTextureLoader>, AsyncTextureError> AsyncTextureLoader::Create(
        AssetPublication& publication, TextureRegistry& images, std::filesystem::path root, AsyncTextureLimits limits)
    {
        if (!root.is_absolute() || !limits.requestBytes || limits.requestBytes > limits.queue.decodedBytes ||
            limits.requestBytes > limits.queue.queuedBytes || limits.requestBytes > limits.queue.frameBytes)
            return std::unexpected(AsyncTextureError{QueueError(UploadCode::InvalidLimits)});
        auto impl = std::unique_ptr<Impl>(new(std::nothrow) Impl(images, std::move(root), limits));
        if (!impl) return std::unexpected(AsyncTextureError{QueueError(UploadCode::Allocation)});
        auto queue = AsyncUploadQueue::Create(publication, limits.queue);
        if (!queue) return std::unexpected(AsyncTextureError{queue.error()});
        impl->queue = std::move(*queue);
        auto result = std::unique_ptr<AsyncTextureLoader>(new(std::nothrow) AsyncTextureLoader(std::move(impl)));
        if (!result) return std::unexpected(AsyncTextureError{QueueError(UploadCode::Allocation)});
        return result;
    }
    std::expected<UploadTicket, AsyncTextureError> AsyncTextureLoader::Request(
        const std::filesystem::path& source, const TextureDesc& desc, const std::string& extension)
    {
        auto& s = *m_Impl; s.Owner();
        if (source.empty() || !ValidDescription(desc))
            return std::unexpected(AsyncTextureError{ImageError(TextureErrorCode::InvalidDescription, source, "Async file requests require 2D sampled R8/RGB8/RGBA8 semantics and decoded extents")});
        auto path = source.is_absolute() ? source : s.root / source;
        if (!path.has_extension()) path += extension;
        path = path.lexically_normal();
        const Impl::RequestKey key{path, desc};
        const std::lock_guard lock(s.mutex);
        if (s.closed) return std::unexpected(AsyncTextureError{QueueError(UploadCode::Closed)});
        if (auto found = s.requests.find(key); found != s.requests.end()) return found->second->ticket;
        if (s.requests.size() >= s.limits.queue.requests) return std::unexpected(AsyncTextureError{QueueError(UploadCode::Capacity)});
        auto entry = std::make_shared<Impl::Entry>(); entry->path = std::move(path); entry->requested = desc;
        std::unique_ptr<const AssetDecodeJob> job(new(std::nothrow) Impl::Job(s, entry));
        if (!job) return std::unexpected(AsyncTextureError{QueueError(UploadCode::Allocation)});
        auto ticket = s.queue->Submit(job, s.limits.requestBytes);
        if (!ticket) return std::unexpected(AsyncTextureError{ticket.error()});
        entry->ticket = *ticket; // Worker resolves its record only after this lock ends.
        s.requests.emplace(key, entry); s.tickets.emplace(*ticket, std::move(entry));
        return *ticket;
    }
    std::expected<AsyncTextureStatus, UploadError> AsyncTextureLoader::Status(UploadTicket ticket) const
    {
        auto& s = *m_Impl; s.Owner(); const std::lock_guard lock(s.mutex);
        const auto found = s.tickets.find(ticket);
        if (found == s.tickets.end()) return std::unexpected(QueueError(UploadCode::InvalidTicket));
        const auto& entry = *found->second;
        const auto* record = entry.record.get();
        const auto queue = s.queue->Status(record ? record->leader : ticket);
        if (!queue) return std::unexpected(queue.error());
        AsyncTextureStatus result{queue->state, {}, record ? record->description : entry.requested};
        if (entry.cancelled || (record && record->cancelled)) result.state = AsyncAssetState::Cancelled;
        if (record && result.state == AsyncAssetState::Ready) result.image = record->image;
        if (result.state == AsyncAssetState::Cancelled) result.error = QueueError(UploadCode::Cancelled);
        else if (record && record->error) result.error = *record->error;
        else if (entry.error) result.error = *entry.error;
        else if (queue->error) result.error = *queue->error;
        return result;
    }
    UploadResult AsyncTextureLoader::Cancel(UploadTicket ticket)
    {
        auto& s = *m_Impl; s.Owner(); const std::lock_guard lock(s.mutex);
        const auto found = s.tickets.find(ticket);
        if (found == s.tickets.end()) return std::unexpected(QueueError(UploadCode::InvalidTicket));
        auto& entry = *found->second;
        auto* record = entry.record.get();
        auto state = s.queue->Status(record ? record->leader : ticket);
        if (!state) return std::unexpected(state.error());
        if (state->state == AsyncAssetState::Failed || state->state == AsyncAssetState::Cancelled) return {};
        auto cancelled = s.queue->Cancel(record ? record->leader : ticket);
        if (!cancelled) return cancelled;
        entry.cancelled = true;
        if (record) record->cancelled = true;
        return {};
    }
    void AsyncTextureLoader::Shutdown()
    {
        auto& s = *m_Impl; s.Owner();
        { const std::lock_guard lock(s.mutex); s.closed = true; }
        s.queue->Shutdown(); // Never hold the facade mutex while joining decoder jobs.
    }
    AsyncUploadQueue& AsyncTextureLoader::Queue() noexcept { return *m_Impl->queue; }
    AsyncTextureStats AsyncTextureLoader::Stats() const
    { m_Impl->Owner(); const std::lock_guard lock(m_Impl->mutex); return m_Impl->stats; }
}
