#pragma once

#include "Scene/RenderState.h"
#include "Mesh/AsyncMesh.h"
#include "Mesh/GeometryTemplates.h"
#include <string_view>
#include <variant>

namespace GEngine
{
    class EngineContext;
    class _Entity;
    class _Scene;
    enum class SceneAssignmentError;
    struct SceneAssignmentChange;
    namespace Manager
    {
        class ShapeManager;
    }

    enum class SceneResourceCode
    {
        Allocation,
        MissingShape,
        UnsupportedShape,
        InvalidMaterial,
        InvalidEntity,
        PublicationBusy,
        TemplateCapacity
    };
    enum class GeometryAuthoringCode
    {
        Allocation, SourceUnavailable, SourceVersionMismatch, SourceCapacity,
        InvalidEntity, MissingTransform, InvalidBounds, EmptyBounds,
        InvalidDimensions, DegenerateExtent, InvalidScale, UnsupportedLayout,
        InvalidBasis, GeometryNotUnique, OwnershipUnavailable,
        LiveScaleBinding, ScaleCallbackUnsupported
    };
    using SceneResourceCause =
        std::variant<SceneResourceCode, GeometryAuthoringCode, SceneAssignmentError, PlatformError, MeshError,
                     GpuMeshError, GeometryTemplates::Error, Asset::RegistryError,
                     Asset::ShaderError, Asset::TextureError, Asset::SamplerError,
                     MaterialDeclarationError, MaterialInstanceError>;
    struct SceneResourceError
    {
        std::string operation;
        SceneResourceCause cause;
    };
    // Terminal application diagnostics retain the domain/code and original details.
    std::string DescribeSceneResourceError(const SceneResourceError&);

    enum class SceneMaterialKind
    {
        Lit,
        Helper,
        PointLight,
        Sky
    };
    struct ScenePipeline
    {
        Asset::PipelineHandle pipeline;
        SceneMaterialKind kind;
        float lineWidth = 1.f;
        // Lit coverage uses albedoMap alpha and u_tiling in every raster pass.
        float opacity = 1.f;
    };
    struct SceneMaterialDesc
    {
        SceneMaterialKind kind = SceneMaterialKind::Lit;
        std::span<const MaterialParameterDecl> parameters;
        std::span<const MaterialTextureAssignment> textures;
        bool doubleSided = false;
        float lineWidth = 1.f;
        AlphaMode alpha = AlphaMode::Opaque;
        float alphaCutoff = .5f, opacity = 1.f;
        TransparentBlend transparentBlend = TransparentBlend::StraightAlpha;
    };

    struct GeometryIdentity
    {
        Asset::MeshHandle handle;
        std::uint64_t revision = 0;
        LocalBounds bounds;
    };
    // Immutable owner-lifetime borrow. Views/frames are consumers, never Entity owners.
    // Source storage does not move or retire before this resource owner's teardown.
    struct GeometrySourceView
    {
        GeometryIdentity geometry;
        const MeshAsset* source = nullptr;
    };
    struct GeometryOwnership
    {
        std::uint64_t entityOwners = 0;
        bool canonicalCachePinned = false;
        bool IsUnique() const { return entityOwners == 1 && !canonicalCachePinned; }
    };
    enum class GeometryOperation { MakeUnique, Regenerate, Bake };
    struct GeometryChange
    {
        GeometryOperation operation;
        bool changed = false;
        GeometryIdentity previous, geometry;
        std::uint64_t assignmentRevision = 0;
        Math::Vec3f localScale{1.f};
    };
    struct ObjectDimensions
    {
        GeometryIdentity geometry;
        BoundsStatus status = BoundsStatus::Unavailable;
        std::array<double, 3> dimensions{};
        Math::Vec3f localScale{1.f};
    };
    struct ObjectDimensionsChange
    {
        bool changed = false;
        ObjectDimensions value;
    };
    // CPU snapshot calculations; these do not establish registry liveness/ownership.
    std::expected<ObjectDimensions, GeometryAuthoringCode>
        MeasureObjectDimensions(const GeometryIdentity&, const Math::Vec3f&);
    std::expected<Math::Vec3f, GeometryAuthoringCode>
        ResolveDimensionScale(const ObjectDimensions&, const std::array<double, 3>&);
    // CPU preparation only: positive local scale, no publication/identity/assignment.
    std::expected<MeshAsset, SceneResourceError>
        BakeAuthoredGeometry(const MeshAsset&, const Math::Vec3f&);
    struct GeometryAuthoringLimits
    {
        // Lower limits admit bounded failure checks. Defaults are the reviewed policy.
        std::size_t independentSources = 64;
        std::size_t independentPayloadBytes = 64 * 1024 * 1024;
    };
    struct GeometryAuthoringWork
    {
        std::uint64_t ownershipQueries = 0, ownershipKeyComparisons = 0;
        std::uint64_t ownerAdditions = 0, ownerRemovals = 0;
        std::size_t sourceRecords = 0, sourcePayloadBytes = 0;
        std::size_t independentSources = 0, independentPayloadBytes = 0;
        std::size_t peakTemporaryBytes = 0;
    };

    // Bounded application resource publisher. The root outlives this owner; scenes
    // (including their presentation caches) and all frames retire before it. All
    // creation occurs at the serial publication safe point, outside FrameAccess.
    // This adapter exports the existing shapes' CPU data; it never reads GPU data.
    class SceneRenderResources final
    {
    public:
        static std::expected<std::unique_ptr<SceneRenderResources>, SceneResourceError>
        Create(EngineContext&, GeometryAuthoringLimits = {}, Asset::AssetRegistryLimits = {});
        ~SceneRenderResources();
        SceneRenderResources(const SceneRenderResources&) = delete;
        SceneRenderResources& operator=(const SceneRenderResources&) = delete;
        std::expected<Asset::MeshHandle, SceneResourceError> PublishShape(std::string_view);
        std::expected<Asset::MeshHandle, SceneResourceError>
        PublishGeometry(const GeometryTemplates::Request&);
        std::expected<GeometrySourceView, SceneResourceError> GeometrySource(Asset::MeshHandle) const;
        std::expected<GeometryOwnership, SceneResourceError> GeometryOwners(Asset::MeshHandle) const;
        std::expected<GeometryIdentity, SceneResourceError> CloneGeometry(Asset::MeshHandle);
        std::expected<GeometryChange, SceneResourceError> MakeGeometryUnique(_Entity);
        std::expected<ObjectDimensions, SceneResourceError> Dimensions(_Entity) const;
        std::expected<ObjectDimensionsChange, SceneResourceError>
            SetDimensions(_Entity, const std::array<double, 3>&);
        std::expected<GeometryChange, SceneResourceError>
            RegenerateGeometry(_Entity, const GeometryTemplates::Request&);
        std::expected<GeometryChange, SceneResourceError> BakeGeometry(_Entity);
        GeometryAuthoringWork GeometryWork() const;
        // Existing box/convex physics startup still reads legacy CPU Geometry.
        // Preserve that input privately; neither extraction nor submission reads it.
        std::expected<void, SceneResourceError> AttachPhysicsShape(_Entity&, std::string_view);
        std::expected<Asset::MaterialInstanceHandle, SceneResourceError>
        PublishMaterial(const SceneMaterialDesc&);
        // Normal authoring passes CPU values/handles. These synchronous operations
        // publish only at the serial safe point; they never advance a frame.
        std::expected<SceneAssignmentChange, SceneResourceError>
        AssignRenderable(_Entity, const Component::MeshRendererComponent&);
        std::expected<MeshAuthoringMetadata, Asset::RegistryError>
            MeshMetadata(Asset::MeshHandle) const;
        std::expected<bool, SceneResourceError> SetMaterialTexture(Asset::MaterialInstanceHandle,
                                                                   std::string_view,
                                                                   MaterialTextureValue);
        std::expected<std::unique_ptr<Asset::AsyncMeshLoader>, Asset::AsyncMeshError>
            CreateMeshLoader(std::filesystem::path, Asset::AsyncMeshLimits = {});

        // Renderer/diagnostic compatibility only. FrameScheduler owns normal frame
        // preparation; application authoring uses the operations above. Root service
        // access stays in construction/legacy adapters, not in the authoring chain.
        Asset::AssetPublication& Publication() noexcept
        {
            return m_Publication;
        }
        RenderStateResources ForFrame(const Asset::AssetPublication::FrameAccess& access) const;
        std::span<const ScenePipeline> Pipelines() const noexcept
        {
            return m_Roles;
        }
        MeshRegistry& Meshes() noexcept
        {
            return m_Meshes;
        }
        MaterialInstanceRegistry& Materials() noexcept
        {
            return m_Materials;
        }

    private:
        friend class _Scene;
        enum class SourceKind { Template, Legacy, Independent };
        struct SourceRecord
        {
            Asset::MeshHandle handle;
            std::uint64_t revision = 1, owners = 0;
            SourceKind kind = SourceKind::Independent;
            std::unique_ptr<MeshAsset> source;
        };
        static constexpr std::size_t MaximumSources = 128 + 32 + 64;
        static constexpr std::size_t MaximumPayloadBytes = 8 * 1024 * 1024;
        static constexpr std::size_t MaximumTemporaryBytes = 24 * 1024 * 1024;
        SceneRenderResources(Asset::AssetPublication&, Manager::ShapeManager&,
                             GeometryAuthoringLimits, Asset::AssetRegistryLimits);
        SourceRecord* FindSource(Asset::MeshHandle, bool ownershipQuery = false) const;
        std::expected<void, SceneResourceError> CheckSourceBudget(const MeshAsset&, SourceKind) const;
        std::expected<GeometryIdentity, SceneResourceError> PublishAuthored(MeshAsset, SourceKind);
        void DiscardCandidate(Asset::MeshHandle);
        std::expected<GeometrySourceView, SceneResourceError> EntityGeometry(_Entity) const;
        std::expected<void, SceneResourceError> PrepareGeometryAssignment(_Entity, std::size_t);
        void RegisterGeometryObserver(std::uint64_t);
        static SceneRenderResources* GeometryPublisher(Asset::MeshHandle);
        static bool CanAddGeometryOwner(Asset::MeshHandle);
        static bool AddGeometryOwner(Asset::MeshHandle);
        static void RemoveGeometryOwner(Asset::MeshHandle);
        static void IncompleteGeometryScene(bool entering);
        // Auxiliary routing only: existing registry domain -> publisher observer.
        // No resource identity/liveness/free list is maintained here.
        inline static thread_local std::array<SceneRenderResources*, 257> s_GeometryObservers{};
        inline static thread_local std::size_t s_IncompleteGeometryScenes = 0;
        SceneRenderResources* m_NextGeometryObserver = nullptr;
        std::uint64_t m_GeometryDomain = 0;
        GeometryAuthoringLimits m_GeometryLimits;
        mutable std::array<SourceRecord, MaximumSources> m_Sources{};
        std::size_t m_SourceCount = 0, m_LegacySourceCount = 0;
        mutable GeometryAuthoringWork m_GeometryWork;
        Asset::AssetPublication& m_Publication;
        Manager::ShapeManager& m_Shapes;
        // Reverse destruction follows dependency order; no cached leases here.
        ShaderProgramRegistry m_Programs;
        PipelineRegistry m_Pipelines;
        MaterialTemplateRegistry m_Templates;
        MaterialInstanceRegistry m_Materials;
        MeshRegistry m_Meshes;
        std::optional<MaterialBindingResources> m_Bindings;
        std::vector<std::pair<std::string, Asset::MeshHandle>> m_SharedShapes;
        std::vector<ScenePipeline> m_Roles;
        std::array<std::pair<GeometryTemplates::Key, Asset::MeshHandle>,
                   GeometryTemplates::MaximumSharedTemplates>
            m_SharedTemplates{};
        std::size_t m_SharedTemplateCount = 0;
    };
}
