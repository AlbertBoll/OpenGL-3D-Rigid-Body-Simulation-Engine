#pragma once

#include "Component/RenderComponents.h"
#include "Mesh/GpuMesh.h"
#include "Material/MaterialBinding.h"
#include "Math/Math.h"
#include "Core/FrameBuffer.h"
#include "Core/Platform.h"

namespace GEngine
{
    enum class BoundsStatus { Valid, Empty, Unavailable, Invalid };
    struct WorldBounds
    {
        BoundsStatus status = BoundsStatus::Unavailable;
        std::array<double, 3> minimum{}, maximum{}, sphereCenter{};
        double sphereRadius{};
        // Only Valid bounds may reject an object geometrically. Empty means a
        // known empty mesh; missing/invalid metadata must remain conservatively visible.
        bool CanCull() const noexcept { return status == BoundsStatus::Valid; }
        bool operator==(const WorldBounds&) const = default;
    };
    WorldBounds TransformBounds(const LocalBounds&, const Math::Mat4&) noexcept;

    struct RenderTargetTag;
    class RenderTarget;
    enum class RenderTargetUsage : std::uint8_t;
    using RenderTargetHandle = Asset::AssetHandle<RenderTargetTag>;
    struct RenderTargetRevision
    {
        RenderTargetHandle identity;
        std::uint64_t publication{}, storage{};
        FrameBufferSpecification description;
        TargetSizeSource sizeSource = TargetSizeSource::Fixed;
        RenderTargetUsage usage{};
        bool operator==(const RenderTargetRevision&) const = default;
    };
    // Adapter for an already resolved target. The caller supplies its registry
    // identity/version and retains that owner through submission. No native name
    // or address is an identity. No-op resize preserves the storage source.
    RenderTargetRevision CaptureRenderTargetRevision(RenderTargetHandle, std::uint64_t publication, const RenderTarget&);

    // Counters belong to one scene lifetime. Compare the same category within the
    // same scene; entity identity still determines lifetime. Zero is unobserved.
    // Authoring compares effective values. Registry publication is an intentional
    // new resource version; publishers must skip Replace on a no-op authoring edit.
    struct RenderRevisions
    {
        std::uint64_t scene{}, transform{}, mesh{}, material{}, light{}, camera{}, target{}, bounds{};
        bool operator==(const RenderRevisions&) const = default;
    };
    struct RenderStateResources
    {
        const Asset::AssetPublication::FrameAccess& access;
        const MeshRegistry& meshes;
        const MaterialInstanceRegistry& materials;
        MaterialBindingResources bindings;
        RenderTargetRevision target; // Default means no offscreen target.
    };
    struct EntityRenderState
    {
        EntityRenderId entity;
        Math::Mat4 world{1.f}; // Presentation world, including interpolated ancestors.
        WorldBounds bounds;
        RenderRevisions revisions;
        MeshView mesh;
        std::optional<PreparedMaterialBinding> material;
        std::optional<Asset::RegistryError> meshError;
        std::optional<MaterialBindingError> materialError;
    };
    struct SceneRenderState
    {
        RenderRevisions revisions;
        std::vector<EntityRenderState> entities; // Deterministic hierarchy order.
        RenderTargetRevision target;
        // Mesh/material leases retain the sampled versions through submission.
        // The caller retains the target used to capture target revisions.
        // This is an invalidation/bounds snapshot, not a finalized draw RenderFrame.
    };
}
