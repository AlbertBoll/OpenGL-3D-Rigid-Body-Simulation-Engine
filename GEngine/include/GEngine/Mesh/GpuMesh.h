#pragma once

#include "Mesh/MeshAsset.h"
#include "Assets/AssetRegistry.h"

namespace GEngine
{
    enum class GpuMeshErrorCode
    {
        InvalidAsset, DeviceLimit, Allocation, Driver, InvalidMesh,
        Immutable, IncompatibleLayout, InvalidRange, PayloadMismatch,
        NonFinitePosition, InvalidPrimitive, Registry
    };
    struct GpuMeshError
    {
        GpuMeshErrorCode code;
        const char* message;
        std::size_t element = 0;
        Asset::RegistryError registry = Asset::RegistryError::InvalidHandle;
    };
    enum class MeshPrimitive { Points, Lines, Triangles };

    struct VertexRecordUpdate
    {
        VertexLayout layout;
        std::size_t firstVertex = 0;
        std::size_t vertexCount = 0;
        std::span<const std::byte> records;
    };

    // Owns GPU storage, never the source vertex/index payload. All operations and
    // live-owner retirement require the creating context on its registered thread.
    // Metadata views borrow the owner and expire on move/assignment/destruction.
    class GpuMesh final
    {
    public:
        GpuMesh() noexcept;
        ~GpuMesh();
        GpuMesh(const GpuMesh&) = delete;
        GpuMesh& operator=(const GpuMesh&) = delete;
        GpuMesh(GpuMesh&&) noexcept;
        GpuMesh& operator=(GpuMesh&&) noexcept;
        static std::expected<GpuMesh, GpuMeshError> Create(const MeshAsset&);
        explicit operator bool() const noexcept;
        VertexLayout Layout() const noexcept;
        std::span<const SubmeshRange> Submeshes() const noexcept;
        std::size_t VertexCount() const noexcept;
        std::size_t IndexCount() const noexcept;
        MeshIndexFormat IndexFormat() const noexcept;
        MeshUpdateIntent UpdateIntent() const noexcept;
        std::uint32_t MaterialSlotCount() const noexcept;
        LocalBounds Bounds() const noexcept;

        // Static rejects all updates, including empty ones. Dynamic empty updates
        // at a valid offset are no-ops. Layout/order and allocation capacity never
        // change in place; recreation/replacement is required for those changes.
        // Every nonempty update is a complete contiguous range of vertex records.
        // Normal driver synchronization is retained; no mapped/in-flight writes.
        std::expected<void, GpuMeshError> UpdateVertices(const VertexRecordUpdate&);
        // Uses the caller's active pipeline; preserves VAO/array-buffer bindings.
        std::expected<void, GpuMeshError> DrawSubmesh(std::size_t submesh, MeshPrimitive = MeshPrimitive::Triangles) const;

    private:
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };

    using MeshRegistry = Asset::AssetRegistry<Asset::MeshHandle, GpuMesh>;
    using MeshView = MeshRegistry::Lease;
    // Owner keeps the registry until after all leases retire and before context
    // teardown. Publication precedes frame extraction; views live through submission.
    std::expected<Asset::MeshHandle, GpuMeshError> PublishMesh(MeshRegistry&,
        const Asset::AssetPublication::Publication&, const MeshAsset&);
    std::expected<void, GpuMeshError> UpdateMesh(MeshRegistry&,
        const Asset::AssetPublication::Publication&, Asset::MeshHandle, const VertexRecordUpdate&);
}
