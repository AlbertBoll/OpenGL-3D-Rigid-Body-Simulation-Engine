#pragma once

#include "Component/RenderComponents.h"
#include "Mesh/GpuMesh.h"
#include "Material/MaterialBinding.h"
#include "Renderer/RenderFrame.h"
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
    // Persistent semantic records contain CPU values/identities only.
    struct RenderSemanticRecord
    {
        EntityRenderId entity;
        Math::Mat4 world{1.f};
        WorldBounds bounds;
        RenderRevisions revisions;
        std::optional<Component::RenderLightComponent> light;
        std::optional<Component::MeshRendererComponent> meshIntent;
        std::optional<Component::VisibilityComponent> visibilityIntent;
        std::optional<Component::RenderCameraComponent> cameraIntent;
        std::size_t meshGroup = SIZE_MAX, materialGroup = SIZE_MAX;
        std::uint64_t epoch{};
    };
    namespace RenderCpu { struct SceneSnapshot; struct CallBindings; struct Domain; }
    // Owning view of immutable CPU rows and this call's bindings. Copies preserve
    // the former standalone row lifetime without copying semantic row values.
    // Resource ownership here never enters persistent CPU state.
    struct EntityRenderState
    {
        std::shared_ptr<const RenderCpu::SceneSnapshot> snapshotOwner;
        std::shared_ptr<RenderCpu::CallBindings> bindingOwner;
        const RenderSemanticRecord& cpu;
        const EntityRenderId& entity;
        const Math::Mat4& world;
        const WorldBounds& bounds;
        const RenderRevisions& revisions;
        const std::optional<Component::RenderLightComponent>& light;
        FrameMeshReference mesh;
        FrameMaterialReference material;
        const std::optional<Asset::RegistryError>& meshError;
        const std::optional<MaterialBindingError>& materialError;
        std::size_t meshGroup{}, materialGroup{};
        EntityRenderState() noexcept;
        EntityRenderState(const RenderSemanticRecord&, const FrameMeshReference&,
            const FrameMaterialReference&, const std::optional<Asset::RegistryError>&,
            const std::optional<MaterialBindingError>&) noexcept;
        EntityRenderState(const EntityRenderState&) noexcept = default;
        EntityRenderState& operator=(const EntityRenderState&) noexcept;
        EntityRenderState WithMaterial(const FrameMaterialReference&) const noexcept;
    };
    class SceneEntityViews
    {
    public:
        EntityRenderState operator[](std::size_t) const noexcept;
        std::size_t size() const noexcept { return m_Count; }
        bool empty() const noexcept { return !m_Count; }
        struct Iterator
        {
            const SceneEntityViews* owner{}; std::size_t index{};
            EntityRenderState operator*() const noexcept { return (*owner)[index]; }
            Iterator& operator++() noexcept { ++index; return *this; }
            bool operator==(const Iterator&) const = default;
        };
        Iterator begin() const noexcept { return {this,0}; }
        Iterator end() const noexcept { return {this,m_Count}; }
        SceneEntityViews() = default;
        SceneEntityViews(std::shared_ptr<const RenderCpu::SceneSnapshot>,
            std::shared_ptr<RenderCpu::CallBindings>, std::size_t) noexcept;
        const std::shared_ptr<const RenderCpu::SceneSnapshot>& Snapshot() const noexcept { return m_Snapshot; }
        const std::shared_ptr<RenderCpu::CallBindings>& Bindings() const noexcept { return m_Bindings; }
    private:
        std::shared_ptr<const RenderCpu::SceneSnapshot> m_Snapshot;
        std::shared_ptr<RenderCpu::CallBindings> m_Bindings;
        std::size_t m_Count{};
    };
    // Complete effective operands of presentation evaluation. No body/resource borrow.
    struct RenderPresentationInput
    {
        Math::Vec3f translation{}, scale{1.f}, previousTranslation{}, currentTranslation{};
        Math::Quat rotation{1.f,0.f,0.f,0.f}, previousRotation{1.f,0.f,0.f,0.f}, currentRotation{1.f,0.f,0.f,0.f};
        float alpha{};
        bool interpolate{};
    };
    struct SceneRenderState
    {
        RenderRevisions revisions;
        SceneEntityViews entities; // Persistent immutable semantic rows plus fresh bindings.
        std::shared_ptr<RenderCpu::Domain> cpuDomain;
        bool fullTransformChange{};
        RenderTargetRevision target;
        // Mesh/material leases retain the sampled versions through submission.
        // The caller retains the target used to capture target revisions.
        // This is an invalidation/bounds snapshot, not a finalized draw RenderFrame.
    };
}
