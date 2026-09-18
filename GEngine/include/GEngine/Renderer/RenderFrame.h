#pragma once

#include "Component/RenderComponents.h"
#include "Mesh/GpuMesh.h"
#include "Material/MaterialBinding.h"
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>

namespace GEngine
{
    // Presentation values only. View/projection use the engine's GLM column-vector
    // convention. Viewport coordinates are pixels relative to the submission target;
    // target selection/retention and camera extraction are caller responsibilities.
    struct FrameCamera
    {
        EntityRenderId entity;
        glm::mat4 view{1.f}, projection{1.f};
        glm::vec3 worldPosition{};
        std::uint32_t viewportX{}, viewportY{}, viewportWidth{}, viewportHeight{};
        std::uint32_t visibleLayers = ~std::uint32_t{0};
    };

    struct DrawItem
    {
        Asset::MeshHandle mesh;
        SubmeshRange submesh;
        Asset::PipelineHandle pipeline;
        Asset::MaterialInstanceHandle material;
        glm::mat4 worldTransform{1.f};
        EntityRenderId entity;
        std::uint64_t sortKey{};
        std::size_t resources{}; // Frame-local table ordinal, never resource identity.
        std::uint32_t layers = ~std::uint32_t{0};
        bool castShadows = true, receiveShadows = true, pickable = true; // Shadow intent is intersected with the template policy.
    };

    enum class DebugDepth { Tested, Overlay };
    // A world-space line, linear RGBA, one-pixel width. No borrowed Geometry/text.
    // Default entity means an unassociated diagnostic, never a picking pixel ID.
    struct FrameDebugLine
    {
        glm::vec3 start{}, end{};
        glm::vec4 color{1.f};
        EntityRenderId entity;
        DebugDepth depth = DebugDepth::Tested;
    };

    // The builder derives identity/range from these exact ready versions. Moving a
    // prepared packet transfers its buffers; no mesh, texture or material is copied.
    class FrameResources final
    {
    public:
        FrameResources(const FrameResources&) = delete;
        FrameResources& operator=(const FrameResources&) = delete;
        FrameResources(FrameResources&&) noexcept = default;
        FrameResources& operator=(FrameResources&&) noexcept = default;
        const MeshView& Mesh() const noexcept { return m_Mesh; }
        const PreparedMaterialBinding& Material() const noexcept { return *m_Material; }
    private:
        friend class RenderFrameBuilder;
        FrameResources() = default;
        MeshView m_Mesh;
        std::optional<PreparedMaterialBinding> m_Material;
    };

    struct FrameCapacity { std::size_t cameras{}, draws{}, debugLines{}, resources{}; };
    struct FrameStorageAccounting
    {
        std::size_t usedBytes{}, capacityBytes{}, storageAllocations{};
        // sizeof(record) accounting for the four frame arrays only, excluding
        // allocator overhead and existing prepared-packet/resource backing storage.
    };
    enum class FrameErrorCode
    {
        CapacityOverflow, AllocationFailed, CapacityExceeded, Finalized,
        InvalidCamera, InvalidResources, InvalidDraw, InvalidSubmesh, InvalidDebugLine
    };
    enum class FrameSection { Cameras, Draws, DebugLines, Resources, Frame };
    struct FrameError
    {
        FrameErrorCode code;
        FrameSection section;
        std::size_t element{}; // Offending ordinal or requested capacity.
    };

    // Immutable payload after Finalize. Move-only storage owner; moving/replacing/
    // destroying it invalidates borrowed views. Concurrent const readers are safe
    // while the owner stays alive and stationary. There is no live ECS reference.
    // Registries outlive all frames and retire GPU owners on the context thread
    // before context teardown. Dropping a frame only drops CPU storage and leases.
    class RenderFrame final
    {
    public:
        RenderFrame(const RenderFrame&) = delete;
        RenderFrame& operator=(const RenderFrame&) = delete;
        RenderFrame(RenderFrame&&) noexcept;
        RenderFrame& operator=(RenderFrame&&) noexcept;
        ~RenderFrame() = default;
        std::span<const FrameCamera> Cameras() const & noexcept { return {m_Cameras.get(), m_Size.cameras}; }
        std::span<const DrawItem> Draws() const & noexcept { return {m_Draws.get(), m_Size.draws}; }
        std::span<const FrameDebugLine> DebugLines() const & noexcept { return {m_Debug.get(), m_Size.debugLines}; }
        std::span<const FrameResources> Resources() const & noexcept { return {m_Resources.get(), m_Size.resources}; }
        std::span<const FrameCamera> Cameras() const && = delete;
        std::span<const DrawItem> Draws() const && = delete;
        std::span<const FrameDebugLine> DebugLines() const && = delete;
        std::span<const FrameResources> Resources() const && = delete;
        FrameStorageAccounting Storage() const noexcept;
    private:
        friend class RenderFrameBuilder;
        RenderFrame() = default;
        void Swap(RenderFrame&) noexcept;
        std::unique_ptr<FrameCamera[]> m_Cameras;
        std::unique_ptr<DrawItem[]> m_Draws;
        std::unique_ptr<FrameDebugLine[]> m_Debug;
        std::unique_ptr<FrameResources[]> m_Resources;
        FrameCapacity m_Size, m_Capacity;
    };

    struct FrameDrawDesc
    {
        std::size_t resources{}, submesh{};
        glm::mat4 worldTransform{1.f};
        EntityRenderId entity;
        std::uint64_t sortKey{};
        std::uint32_t layers = ~std::uint32_t{0};
        bool castShadows = true, receiveShadows = true, pickable = true;
    };

    // Single writer. Append order is the authoritative order for cameras, draws
    // (including transparent draws) and debug lines. Producers must use a stable
    // source order, not hash iteration/task completion/native names. sortKey is
    // advisory here: Finalize never sorts or batches. Any later ordering pass must
    // preserve equal-key source order and respect transparent compositing order.
    class RenderFrameBuilder final
    {
    public:
        RenderFrameBuilder(const RenderFrameBuilder&) = delete;
        RenderFrameBuilder& operator=(const RenderFrameBuilder&) = delete;
        RenderFrameBuilder(RenderFrameBuilder&&) noexcept;
        RenderFrameBuilder& operator=(RenderFrameBuilder&&) noexcept;
        [[nodiscard]] static std::expected<RenderFrameBuilder, FrameError> Create(FrameCapacity = {}) noexcept;
        [[nodiscard]] std::expected<void, FrameError> AddCamera(const FrameCamera&) noexcept;
        // Consumes the packet only on success. Publication/preparation precede this
        // call. One table entry may serve any number of draws/submeshes.
        [[nodiscard]] std::expected<std::size_t, FrameError> AddResources(
            const MeshView&, PreparedMaterialBinding&&) noexcept;
        [[nodiscard]] std::expected<void, FrameError> AddDraw(const FrameDrawDesc&) noexcept;
        [[nodiscard]] std::expected<void, FrameError> AddDebugLine(const FrameDebugLine&) noexcept;
        [[nodiscard]] std::expected<RenderFrame, FrameError> Finalize() && noexcept;
    private:
        RenderFrameBuilder() = default;
        RenderFrame m_Frame;
        bool m_Finalized = false;
    };
}
