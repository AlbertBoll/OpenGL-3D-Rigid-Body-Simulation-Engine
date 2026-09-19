#pragma once

#include "Assets/AsyncUploadQueue.h"
#include "Mesh/GpuMesh.h"
#include "Mesh/MeshImporter.h"
#include <filesystem>
#include <variant>

namespace GEngine::Asset
{
    using AsyncMeshError = std::variant<MeshImportError, GpuMeshError, UploadError>;
    struct AsyncMeshStatus
    {
        AsyncAssetState state = AsyncAssetState::Requested;
        MeshHandle mesh{}; // Ready only; pending/failed requests do not replace a caller's fallback.
        std::filesystem::path source;
        std::optional<AsyncMeshError> error;
    };
    struct AsyncMeshLimits
    {
        // Hard engine-payload bound, including adapter/normalization scratch.
        // Assimp-owned allocations are excluded by the approved Phase 51 exception.
        std::size_t requestBytes = 64 * 1024 * 1024;
        UploadLimits queue{64, 2, 4, 256 * 1024 * 1024, 8, 128 * 1024 * 1024,
            64 * 1024 * 1024, std::chrono::milliseconds(2)};
    };
    struct AsyncMeshStats { std::size_t imports{}, uploads{}; };
    std::string DescribeAsyncMeshError(const AsyncMeshError&);

    // Owner-thread facade. Request/Status perform no filesystem access. Workers
    // canonicalize/read/import/normalize static files into immutable MeshAssets;
    // only FrameScheduler's UpdateFrameResources drain creates/publishes GpuMesh.
    // Queue and registry must use the same publication domain. Close/join this
    // loader before retiring the registry/context. Registry owns all ready meshes.
    //
    // Lexical duplicates reuse a ticket. Workers coalesce open-file identity plus
    // dependency directory, extension and every import option. Sidecar-relative
    // paths in different directories are distinct. Aliases share a terminal result
    // and cancellation; unresolved spellings cancel independently. Ready/Uploading
    // cancellation is Busy. This bounded lifetime cache retains terminal tickets
    // until shutdown; it does not implement reload/eviction or animated import.
    //
    // Static OBJ files first pass a fixed-storage directive scan (v/vt/vn/vp,
    // f/o/g/s, mtllib/usemtl, l/p and comments). Unknown directives are a typed
    // ReadFailed error; this avoids a reproduced malformed-input loop in the
    // bundled reader. The file remains read-locked through scan/import.
    // Assimp import calls may delay cooperative shutdown. Engine preparation polls
    // cancellation; no worker waits for publication and no worker performs GL.
    class AsyncMeshLoader final
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<AsyncMeshLoader>, AsyncMeshError> Create(
            AssetPublication&, MeshRegistry&, std::filesystem::path meshRoot, AsyncMeshLimits = {});
        ~AsyncMeshLoader();
        AsyncMeshLoader(const AsyncMeshLoader&) = delete;
        AsyncMeshLoader& operator=(const AsyncMeshLoader&) = delete;
        [[nodiscard]] std::expected<UploadTicket, AsyncMeshError> Request(
            const std::filesystem::path&, const MeshImportOptions& = {});
        [[nodiscard]] std::expected<AsyncMeshStatus, UploadError> Status(UploadTicket) const;
        UploadResult Cancel(UploadTicket);
        void Shutdown();
        AsyncUploadQueue& Queue() noexcept;
        AsyncMeshStats Stats() const;
    private:
        struct Impl;
        explicit AsyncMeshLoader(std::unique_ptr<Impl>);
        std::unique_ptr<Impl> m_Impl;
    };
}
