#include "gepch.h"
#include "Mesh/AsyncMesh.h"
#include <cstdio>
#include <cerrno>
#include <share.h>
#include <algorithm>
#include <array>
#include <format>
#include <io.h>
#include <map>
#include <mutex>
#include <new>
#include <thread>
#include <tuple>
#include <Windows.h>

namespace GEngine::Asset
{
    namespace
    {
        UploadError QueueError(UploadCode code) { return {code}; }
        auto OptionsKey(const MeshImportOptions& o)
        { return std::tuple{o.handedness, o.winding, o.normals, o.uvs, o.tangents, o.flipV}; }
        bool Valid(const MeshImportOptions& o)
        {
            return o.handedness <= MeshSourceHandedness::Left && o.winding <= MeshSourceWinding::Clockwise &&
                o.normals <= MissingMeshNormals::Reject && o.uvs <= MissingMeshUVs::Reject &&
                o.tangents <= MissingMeshTangents::Reject;
        }
    }
    struct AsyncMeshLoader::Impl
    {
        struct Record
        {
            UploadTicket leader;
            std::filesystem::path canonical;
            MeshHandle mesh{};
            std::optional<AsyncMeshError> error;
            bool cancelled{};
        };
        struct Entry
        {
            UploadTicket ticket;
            std::filesystem::path path;
            MeshImportOptions options;
            std::shared_ptr<Record> record;
            std::optional<AsyncMeshError> error;
            bool cancelled{};
        };
        using Options = decltype(OptionsKey(MeshImportOptions{}));
        using RequestKey = std::pair<std::filesystem::path, Options>;
        // The directory/extension preserve the importer's sidecar/format context.
        using FileKey = std::tuple<DWORD, DWORD, DWORD, std::filesystem::path, std::filesystem::path, Options>;
        const std::thread::id owner = std::this_thread::get_id();
        MeshRegistry& meshes;
        std::filesystem::path root;
        AsyncMeshLimits limits;
        std::unique_ptr<AsyncUploadQueue> queue;
        mutable std::mutex mutex;
        std::map<RequestKey, std::shared_ptr<Entry>> requests;
        std::map<UploadTicket, std::shared_ptr<Entry>> tickets;
        std::map<FileKey, std::shared_ptr<Record>> canonical;
        AsyncMeshStats stats;
        bool closed{};
        Impl(MeshRegistry& registry, std::filesystem::path path, AsyncMeshLimits l)
            : meshes(registry), root(std::move(path)), limits(l) {}
        void Owner() const { AssetDetail::RequireInvariant(owner == std::this_thread::get_id()); }

        struct Upload final : UploadRequest
        {
            Impl& owner;
            std::shared_ptr<Record> record;
            const std::optional<MeshAsset> mesh;
            Upload(Impl& o, std::shared_ptr<Record> r, std::optional<MeshAsset> payload = {})
                : owner(o), record(std::move(r)), mesh(std::move(payload)) {}
            std::size_t Bytes() const noexcept override
            {
                return mesh ? sizeof(Upload) + mesh->Vertices().size() + mesh->Indices().size() +
                    mesh->Submeshes().size_bytes() : 0;
            }
            UploadResult Apply(const AssetPublication::Publication& publication) const noexcept override
            {
                owner.Owner();
                if (!mesh) return {}; // Canonical alias: leader owns publication.
                {
                    const std::lock_guard lock(owner.mutex);
                    if (record->cancelled) return std::unexpected(QueueError(UploadCode::Cancelled));
                }
                auto published = PublishMesh(owner.meshes, publication, *mesh);
                const std::lock_guard lock(owner.mutex);
                if (!published) {
                    record->error = published.error();
                    return std::unexpected(QueueError(UploadCode::UploadFailed));
                }
                record->mesh = *published; ++owner.stats.uploads;
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
            auto Fail(AsyncMeshError error) const
            {
                const std::lock_guard lock(owner.mutex);
                if (entry->record) entry->record->error = std::move(error);
                else entry->error = std::move(error);
                return std::unexpected(QueueError(UploadCode::DecodeFailed));
            }
            std::expected<void, AsyncMeshError> CheckObj(FILE* file, UploadCancellation cancel) const
            {
                // The bundled OBJ reader can loop indefinitely on unknown
                // directives (for example "not a valid mesh file\n"). Validate
                // the selected static OBJ directive vocabulary with fixed stack
                // storage before entering that dependency. Other formats keep
                // their existing Assimp reader. This is not a second OBJ parser.
                constexpr std::string_view directives[]{"v", "vt", "vn", "vp", "f", "o", "g", "s",
                    "mtllib", "usemtl", "l", "p"};
                std::array<char, 16> token{};
                std::size_t length{}, line = 1, bytes{};
                bool body{};
                auto valid = [&] {
                    const std::string_view word(token.data(), length);
                    return !length || std::find(std::begin(directives), std::end(directives), word) != std::end(directives);
                };
                for (int c; (c = std::fgetc(file)) != EOF; ++bytes) {
                    if ((bytes & 4095) == 0 && Stopped(cancel))
                        return std::unexpected(AsyncMeshError{QueueError(UploadCode::Cancelled)});
                    if (c == '\n' || c == '\r') {
                        if (!valid()) return std::unexpected(AsyncMeshError{MeshImportError{MeshImportErrorCode::ReadFailed, 0, line}});
                        length = 0; body = false; if (c == '\n') ++line;
                    }
                    else if (!body) {
                        if (c == '#' && !length) body = true;
                        else if (c == ' ' || c == '\t') {
                            if (!valid()) return std::unexpected(AsyncMeshError{MeshImportError{MeshImportErrorCode::ReadFailed, 0, line}});
                            if (length) body = true;
                        }
                        else {
                            if (length == token.size()) return std::unexpected(AsyncMeshError{MeshImportError{MeshImportErrorCode::ReadFailed, 0, line}});
                            token[length++] = static_cast<char>(c);
                        }
                    }
                }
                if (std::ferror(file) || !valid())
                    return std::unexpected(AsyncMeshError{MeshImportError{MeshImportErrorCode::ReadFailed, 0, line}});
                return {};
            }
            std::expected<std::unique_ptr<const UploadRequest>, UploadError> Decode(
                UploadCancellation cancel, std::size_t reservation) const noexcept override
            {
                AssetDetail::RequireInvariant(std::this_thread::get_id() != owner.owner);
                if (Stopped(cancel)) return std::unexpected(QueueError(UploadCode::Cancelled));
                std::error_code ec;
                auto path = std::filesystem::canonical(entry->path, ec);
                if (ec) return Fail(UploadError{UploadCode::DecodeFailed, ec});
                FILE* raw = _wfsopen(path.c_str(), L"rb", _SH_DENYWR);
                if (!raw) return Fail(UploadError{UploadCode::DecodeFailed, {errno, std::generic_category()}});
                std::unique_ptr<FILE, decltype(&std::fclose)> file(raw, std::fclose);
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(raw))), &info))
                    return Fail(UploadError{UploadCode::DecodeFailed, {int(GetLastError()), std::system_category()}});
                const FileKey key{info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow,
                    path.parent_path(), path.extension(), OptionsKey(entry->options)};
                {
                    const std::lock_guard lock(owner.mutex);
                    if (entry->cancelled || cancel.StopRequested()) return std::unexpected(QueueError(UploadCode::Cancelled));
                    if (auto found = owner.canonical.find(key); found != owner.canonical.end()) {
                        entry->record = found->second;
                        auto alias = std::unique_ptr<const UploadRequest>(new(std::nothrow) Upload(owner, entry->record));
                        if (!alias) return std::unexpected(QueueError(UploadCode::Allocation));
                        return alias;
                    }
                    entry->record = std::make_shared<Record>();
                    entry->record->leader = entry->ticket; entry->record->canonical = path;
                    owner.canonical.emplace(key, entry->record);
                    ++owner.stats.imports;
                }
                if (reservation <= sizeof(Upload)) return Fail(MeshImportError{MeshImportErrorCode::MemoryBudgetExceeded});
                struct Cancellation { const Job& job; UploadCancellation token; } cancellation{*this, cancel};
                const MeshImportControl control{reservation - sizeof(Upload), &cancellation,
                    [](const void* context) noexcept {
                        const auto& c = *static_cast<const Cancellation*>(context);
                        return c.job.Stopped(c.token);
                    }};
                if (_wcsicmp(path.extension().c_str(), L".obj") == 0)
                    if (auto checked = CheckObj(raw, cancel); !checked) return Fail(checked.error());
                // Assimp owns file/sidecar I/O and its exempt internal scratch.
                // The Phase 33 adapter enforces the remaining engine-payload bound
                // before its copies and normalization allocations are made.
                auto mesh = ImportMeshFile(path.string().c_str(), entry->options, control);
                if (Stopped(cancel)) return std::unexpected(QueueError(UploadCode::Cancelled));
                if (!mesh) return Fail(mesh.error());
                auto upload = std::unique_ptr<const UploadRequest>(new(std::nothrow) Upload(owner, entry->record, std::move(*mesh)));
                if (!upload) return std::unexpected(QueueError(UploadCode::Allocation));
                return upload;
            }
        };
    };
    std::string DescribeAsyncMeshError(const AsyncMeshError& error)
    {
        return std::visit([](const auto& cause) {
            using T = std::decay_t<decltype(cause)>;
            if constexpr (std::same_as<T, MeshImportError>)
                return std::format("mesh import code={} part={} element={} field={} mesh-code={} mesh-element={}",
                    int(cause.code), cause.part, cause.element, int(cause.field), int(cause.meshError.code), cause.meshError.element);
            else if constexpr (std::same_as<T, GpuMeshError>)
                return std::format("gpu mesh code={} element={} registry={} message={}",
                    int(cause.code), cause.element, int(cause.registry), cause.message ? cause.message : "");
            else return std::format("mesh queue code={} detail={} system={} registry={}",
                int(cause.code), cause.detail, cause.system.message(), cause.registry ? int(*cause.registry) : -1);
        }, error);
    }
    AsyncMeshLoader::AsyncMeshLoader(std::unique_ptr<Impl> impl) : m_Impl(std::move(impl)) {}
    AsyncMeshLoader::~AsyncMeshLoader() { Shutdown(); }
    std::expected<std::unique_ptr<AsyncMeshLoader>, AsyncMeshError> AsyncMeshLoader::Create(
        AssetPublication& publication, MeshRegistry& meshes, std::filesystem::path root, AsyncMeshLimits limits)
    {
        if (!root.is_absolute() || !limits.requestBytes || limits.requestBytes > limits.queue.decodedBytes ||
            limits.requestBytes > limits.queue.queuedBytes || limits.requestBytes > limits.queue.frameBytes)
            return std::unexpected(AsyncMeshError{QueueError(UploadCode::InvalidLimits)});
        auto impl = std::unique_ptr<Impl>(new(std::nothrow) Impl(meshes, std::move(root), limits));
        if (!impl) return std::unexpected(AsyncMeshError{QueueError(UploadCode::Allocation)});
        auto queue = AsyncUploadQueue::Create(publication, limits.queue);
        if (!queue) return std::unexpected(AsyncMeshError{queue.error()});
        impl->queue = std::move(*queue);
        auto result = std::unique_ptr<AsyncMeshLoader>(new(std::nothrow) AsyncMeshLoader(std::move(impl)));
        if (!result) return std::unexpected(AsyncMeshError{QueueError(UploadCode::Allocation)});
        return result;
    }
    std::expected<UploadTicket, AsyncMeshError> AsyncMeshLoader::Request(
        const std::filesystem::path& source, const MeshImportOptions& options)
    {
        auto& s = *m_Impl; s.Owner();
        if (source.empty() || !Valid(options)) return std::unexpected(AsyncMeshError{MeshImportError{
            source.empty() ? MeshImportErrorCode::InvalidPath : MeshImportErrorCode::InvalidOptions}});
        auto path = (source.is_absolute() ? source : s.root / source).lexically_normal();
        const Impl::RequestKey key{path, OptionsKey(options)};
        const std::lock_guard lock(s.mutex);
        if (s.closed) return std::unexpected(AsyncMeshError{QueueError(UploadCode::Closed)});
        if (auto found = s.requests.find(key); found != s.requests.end()) return found->second->ticket;
        if (s.requests.size() >= s.limits.queue.requests) return std::unexpected(AsyncMeshError{QueueError(UploadCode::Capacity)});
        auto entry = std::make_shared<Impl::Entry>(); entry->path = std::move(path); entry->options = options;
        std::unique_ptr<const AssetDecodeJob> job(new(std::nothrow) Impl::Job(s, entry));
        if (!job) return std::unexpected(AsyncMeshError{QueueError(UploadCode::Allocation)});
        auto ticket = s.queue->Submit(job, s.limits.requestBytes);
        if (!ticket) return std::unexpected(AsyncMeshError{ticket.error()});
        entry->ticket = *ticket; // Worker takes this mutex before observing the ticket.
        s.requests.emplace(key, entry); s.tickets.emplace(*ticket, std::move(entry));
        return *ticket;
    }
    std::expected<AsyncMeshStatus, UploadError> AsyncMeshLoader::Status(UploadTicket ticket) const
    {
        auto& s = *m_Impl; s.Owner(); const std::lock_guard lock(s.mutex);
        const auto found = s.tickets.find(ticket);
        if (found == s.tickets.end()) return std::unexpected(QueueError(UploadCode::InvalidTicket));
        const auto& entry = *found->second;
        const auto* record = entry.record.get();
        const auto queue = s.queue->Status(record ? record->leader : ticket);
        if (!queue) return std::unexpected(queue.error());
        AsyncMeshStatus result{queue->state, {}, record ? record->canonical : entry.path};
        if (entry.cancelled || (record && record->cancelled)) result.state = AsyncAssetState::Cancelled;
        if (record && result.state == AsyncAssetState::Ready) result.mesh = record->mesh;
        if (result.state == AsyncAssetState::Cancelled) result.error = QueueError(UploadCode::Cancelled);
        else if (record && record->error) result.error = *record->error;
        else if (entry.error) result.error = *entry.error;
        else if (queue->error) result.error = *queue->error;
        return result;
    }
    UploadResult AsyncMeshLoader::Cancel(UploadTicket ticket)
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
    void AsyncMeshLoader::Shutdown()
    {
        auto& s = *m_Impl; s.Owner();
        { const std::lock_guard lock(s.mutex); s.closed = true; }
        s.queue->Shutdown(); // Wake blocked CPU-ready producers, join outside facade mutex.
    }
    AsyncUploadQueue& AsyncMeshLoader::Queue() noexcept { return *m_Impl->queue; }
    AsyncMeshStats AsyncMeshLoader::Stats() const
    { m_Impl->Owner(); const std::lock_guard lock(m_Impl->mutex); return m_Impl->stats; }
}
