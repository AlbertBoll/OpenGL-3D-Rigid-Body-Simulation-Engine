#pragma once

#include "Scene/RenderState.h"
#include <string_view>
#include <variant>

namespace GEngine
{
    class EngineContext;
    class _Entity;
    namespace Manager { class ShapeManager; }

    enum class SceneResourceCode { Allocation, MissingShape, UnsupportedShape, InvalidMaterial, InvalidEntity };
    using SceneResourceCause = std::variant<SceneResourceCode, PlatformError, MeshError,
        GpuMeshError, Asset::RegistryError, Asset::ShaderError, Asset::TextureError, Asset::SamplerError,
        MaterialDeclarationError, MaterialInstanceError>;
    struct SceneResourceError { std::string operation; SceneResourceCause cause; };
    // Terminal application diagnostics retain the domain/code and original details.
    std::string DescribeSceneResourceError(const SceneResourceError&);

    enum class SceneMaterialKind { Lit, Helper, PointLight, Sky };
    struct ScenePipeline
    {
        Asset::PipelineHandle pipeline;
        SceneMaterialKind kind;
        float lineWidth = 1.f;
    };
    struct SceneMaterialDesc
    {
        SceneMaterialKind kind = SceneMaterialKind::Lit;
        std::span<const MaterialParameterDecl> parameters;
        std::span<const MaterialTextureAssignment> textures;
        bool doubleSided = false;
        float lineWidth = 1.f;
    };

    // Bounded application resource publisher. The root outlives this owner; scenes
    // (including their presentation caches) and all frames retire before it. All
    // creation occurs at the serial publication safe point, outside FrameAccess.
    // This adapter exports the existing shapes' CPU data; it never reads GPU data.
    class SceneRenderResources final
    {
    public:
        static std::expected<std::unique_ptr<SceneRenderResources>, SceneResourceError> Create(EngineContext&);
        ~SceneRenderResources() = default;
        SceneRenderResources(const SceneRenderResources&) = delete;
        SceneRenderResources& operator=(const SceneRenderResources&) = delete;
        std::expected<Asset::MeshHandle, SceneResourceError> PublishShape(std::string_view);
        // Existing box/convex physics startup still reads legacy CPU Geometry.
        // Preserve that input privately; neither extraction nor submission reads it.
        std::expected<void, SceneResourceError> AttachPhysicsShape(_Entity&, std::string_view);
        std::expected<Asset::MaterialInstanceHandle, SceneResourceError> PublishMaterial(const SceneMaterialDesc&);
        Asset::AssetPublication& Publication() noexcept { return m_Publication; }
        RenderStateResources ForFrame(const Asset::AssetPublication::FrameAccess& access) const;
        std::span<const ScenePipeline> Pipelines() const noexcept { return m_Roles; }
        MeshRegistry& Meshes() noexcept { return m_Meshes; }
        MaterialInstanceRegistry& Materials() noexcept { return m_Materials; }
    private:
        SceneRenderResources(Asset::AssetPublication&, Manager::ShapeManager&);
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
    };
}
