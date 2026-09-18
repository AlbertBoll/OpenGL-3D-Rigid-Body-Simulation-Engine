#include "gepch.h"
#include "Renderer/SceneRenderResources.h"
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Managers/ShapeManager.h"
#include "Managers/AssetsManager.h"
#include <fstream>
#include <format>
#include <new>

namespace GEngine
{
    namespace
    {
        // The old vertex files use legacy Geometry insertion-order locations.
        // These bounded stage adapters preserve their equations/varyings while
        // reading the approved MeshAsset semantic slots (UV=1, normal=2, color=8).
        constexpr const char* LitVertex = R"(#version 450 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoords;
layout(location=2) in vec3 aNormal;
out VS_OUT { vec3 FragPos; vec3 Normal; vec2 TexCoords; } vs_out;
uniform mat4 u_projection, u_view, u_model;
uniform bool reverse_normals;
void main() {
vs_out.FragPos = vec3(u_model * vec4(aPos, 1.0));
vs_out.Normal = transpose(inverse(mat3(u_model))) * aNormal;
if (reverse_normals) vs_out.Normal = -vs_out.Normal;
vs_out.TexCoords = aTexCoords;
gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
})";
        constexpr const char* HelperVertex = R"(#version 460
layout(location=0) in vec3 vertexPosition;
layout(location=8) in vec3 vertexColor;
uniform mat4 u_model, u_view, u_projection;
out vec3 color;
void main() { gl_Position = u_projection * u_view * u_model * vec4(vertexPosition, 1.0); color = vertexColor; }
)";

        std::expected<Asset::Shader, SceneResourceError> Program(SceneMaterialKind kind)
        {
            const char* fragment = kind == SceneMaterialKind::Lit ? "pbr_cascade_shadow.frag"
                : kind == SceneMaterialKind::Helper ? "basic.frag"
                : kind == SceneMaterialKind::PointLight ? "point_light_sphere_visual.frag" : "skybox_environment.frag";
            auto path = RuntimeAssets::TryFile(std::string("Shaders/") + fragment);
            if (!path) return std::unexpected(SceneResourceError{"shader path", path.error()});
            std::ifstream input(*path, std::ios::binary);
            if (!input) return std::unexpected(SceneResourceError{"shader read", Asset::ShaderError{
                Asset::ShaderErrorCode::FileRead, Asset::ShaderStage::Fragment, *path, "Cannot open shader source"}});
            std::string source((std::istreambuf_iterator<char>(input)), {});
            if (input.bad()) return std::unexpected(SceneResourceError{"shader read", Asset::ShaderError{
                Asset::ShaderErrorCode::FileRead, Asset::ShaderStage::Fragment, *path, "Cannot read shader source"}});
            std::string sky;
            const char* vertex = kind == SceneMaterialKind::Helper ? HelperVertex : LitVertex;
            if (kind == SceneMaterialKind::Sky)
            {
                auto skyPath = RuntimeAssets::TryFile("Shaders/skybox_environment.vert");
                if (!skyPath) return std::unexpected(SceneResourceError{"sky shader path", skyPath.error()});
                std::ifstream skyInput(*skyPath, std::ios::binary);
                if (!skyInput) return std::unexpected(SceneResourceError{"sky shader read", Asset::ShaderError{
                    Asset::ShaderErrorCode::FileRead, Asset::ShaderStage::Vertex, *skyPath, "Cannot open shader source"}});
                sky.assign(std::istreambuf_iterator<char>(skyInput), {});
                if (skyInput.bad()) return std::unexpected(SceneResourceError{"sky shader read", Asset::ShaderError{
                    Asset::ShaderErrorCode::FileRead, Asset::ShaderStage::Vertex, *skyPath, "Cannot read shader source"}});
                vertex = sky.c_str();
            }
            const Asset::ShaderSource stages[]{
                {Asset::ShaderStage::Vertex, vertex, "Phase42 semantic vertex input"},
                {Asset::ShaderStage::Fragment, source, fragment}};
            auto shader = Asset::Shader::Create({stages});
            if (!shader) return std::unexpected(SceneResourceError{"program creation", shader.error()});
            return std::move(*shader);
        }
    }

    std::string DescribeSceneResourceError(const SceneResourceError& error)
    {
        return error.operation + ": " + std::visit([](const auto& cause) -> std::string {
            using T = std::decay_t<decltype(cause)>;
            if constexpr (std::is_enum_v<T>) return std::format("domain={} code={}",
                std::same_as<T, SceneResourceCode> ? "scene-resource" : "registry", static_cast<int>(cause));
            else if constexpr (std::same_as<T, Asset::ShaderError>)
                return std::format("shader code={} stage={} source={} log={}", static_cast<int>(cause.code),
                    cause.shaderType ? static_cast<int>(*cause.shaderType) : 0, cause.source, cause.log);
            else if constexpr (std::same_as<T, Asset::TextureError>)
                return std::format("texture code={} source={} message={} system={} registry={}",
                    static_cast<int>(cause.code), cause.source, cause.message, cause.system.message(), static_cast<int>(cause.registry));
            else if constexpr (std::same_as<T, MeshError>)
                return std::format("mesh code={} element={}", static_cast<int>(cause.code), cause.element);
            else if constexpr (std::same_as<T, GpuMeshError>)
                return std::format("gpu-mesh code={} element={} registry={} message={}", static_cast<int>(cause.code),
                    cause.element, static_cast<int>(cause.registry), cause.message);
            else if constexpr (std::same_as<T, Asset::SamplerError>)
                return std::format("sampler code={} registry={} message={}", static_cast<int>(cause.code), static_cast<int>(cause.registry), cause.message);
            else if constexpr (std::same_as<T, PlatformError>)
                return std::format("platform code={} operation={} message={}", static_cast<int>(cause.code), cause.operation, cause.message);
            else return std::format("material domain={} code={} binding={} message={}",
                std::same_as<T, MaterialDeclarationError> ? "declaration" : "instance",
                static_cast<int>(cause.code), cause.binding, cause.message);
        }, error.cause);
    }

    SceneRenderResources::SceneRenderResources(Asset::AssetPublication& publication, Manager::ShapeManager& shapes)
        : m_Publication(publication), m_Shapes(shapes), m_Programs(publication), m_Pipelines(publication),
          m_Templates(publication), m_Materials(publication), m_Meshes(publication) {}

    std::expected<std::unique_ptr<SceneRenderResources>, SceneResourceError> SceneRenderResources::Create(EngineContext& root)
    {
        auto services = root.SceneServices();
        if (!services) return std::unexpected(SceneResourceError{"scene services", services.error()});
        std::unique_ptr<SceneRenderResources> result(new (std::nothrow) SceneRenderResources(services->publication, services->shapes));
        if (!result) return std::unexpected(SceneResourceError{"scene resource owner", SceneResourceCode::Allocation});
        auto bindings = Manager::AssetsManager::FrameBindings(result->m_Programs);
        if (!bindings) return std::unexpected(SceneResourceError{"frame bindings", bindings.error()});
        result->m_Bindings.emplace(*bindings);
        return result;
    }

    RenderStateResources SceneRenderResources::ForFrame(const Asset::AssetPublication::FrameAccess& access) const
    { return {access, m_Meshes, m_Materials, *m_Bindings, {}}; }

    std::expected<void, SceneResourceError> SceneRenderResources::AttachPhysicsShape(_Entity& entity, std::string_view name)
    { return m_Shapes.AttachPhysicsShape(entity, name); }

    std::expected<Asset::MeshHandle, SceneResourceError> SceneRenderResources::PublishShape(std::string_view name)
    {
        for (const auto& [key, handle] : m_SharedShapes) if (key == name) return handle;
        auto mesh = m_Shapes.ExportMesh(name);
        if (!mesh) return std::unexpected(mesh.error());
        auto access = m_Publication.BeginPublication();
        auto handle = PublishMesh(m_Meshes, access, *mesh);
        if (!handle) return std::unexpected(SceneResourceError{std::string(name), handle.error()});
        struct Rollback
        {
            MeshRegistry& registry; const Asset::AssetPublication::Publication& access;
            Asset::MeshHandle handle; bool committed = false;
            ~Rollback() { if (!committed) { (void)registry.Destroy(access, handle); registry.Collect(access); } }
        } rollback{m_Meshes, access, *handle};
        m_SharedShapes.emplace_back(name, *handle);
        rollback.committed = true;
        return *handle;
    }

    std::expected<Asset::MaterialInstanceHandle, SceneResourceError> SceneRenderResources::PublishMaterial(const SceneMaterialDesc& desc)
    {
        if (desc.kind < SceneMaterialKind::Lit || desc.kind > SceneMaterialKind::Sky
            || !std::isfinite(desc.lineWidth) || desc.lineWidth <= 0)
            return std::unexpected(SceneResourceError{"material description", SceneResourceCode::InvalidMaterial});
        auto shader = Program(desc.kind);
        if (!shader) return std::unexpected(shader.error());
        // Every failure (including standard container unwinding) retires only this
        // transaction's versions. Dependent local leases end before rollback runs.
        struct Rollback
        {
            SceneRenderResources& owner;
            Asset::ShaderProgramHandle program;
            Asset::PipelineHandle pipeline;
            Asset::MaterialTemplateHandle declaration;
            Asset::MaterialInstanceHandle material;
            bool committed = false;
            ~Rollback()
            {
                if (committed) return;
                auto access = owner.m_Publication.BeginPublication();
                if (material) (void)owner.m_Materials.Destroy(access, material);
                owner.m_Materials.Collect(access);
                if (declaration) (void)owner.m_Templates.Destroy(access, declaration);
                owner.m_Templates.Collect(access);
                if (pipeline) (void)owner.m_Pipelines.Destroy(access, pipeline);
                owner.m_Pipelines.Collect(access);
                if (program) (void)owner.m_Programs.Destroy(access, program);
                owner.m_Programs.Collect(access);
            }
        } rollback{*this};
        {
            auto access = m_Publication.BeginPublication();
            auto program = m_Programs.Create(access, std::move(*shader));
            if (!program) return std::unexpected(SceneResourceError{"program publication", program.error()});
            rollback.program = *program;
            PipelineDesc pipeline;
            pipeline.program = *program; pipeline.programRevision = 1;
            pipeline.cull = desc.doubleSided ? CullMode::None : CullMode::Back;
            if (desc.kind == SceneMaterialKind::Sky) pipeline.depthCompare = DepthCompare::LessEqual;
            auto state = PipelineState::Create(pipeline);
            if (!state) return std::unexpected(SceneResourceError{"pipeline", state.error()});
            auto handle = m_Pipelines.Create(access, std::move(*state));
            if (!handle) return std::unexpected(SceneResourceError{"pipeline publication", handle.error()});
            rollback.pipeline = *handle;
        }
        PipelineView pipeline;
        {
            auto access = m_Publication.BeginFrame();
            auto view = m_Pipelines.Acquire(access, rollback.pipeline);
            if (!view) return std::unexpected(SceneResourceError{"pipeline resolution", view.error()});
            pipeline = std::move(*view);
        }
        std::vector<MaterialTextureSlotDecl> textures;
        for (const auto& texture : desc.textures) textures.push_back({std::string(texture.name), true, texture.value});
        const bool shadow = desc.kind == SceneMaterialKind::Lit;
        auto declaration = MaterialTemplate::Create({pipeline, desc.parameters, textures, shadow, shadow});
        if (!declaration) return std::unexpected(SceneResourceError{"material template", declaration.error()});
        {
            auto access = m_Publication.BeginPublication();
            auto handle = m_Templates.Create(access, std::move(*declaration));
            if (!handle) return std::unexpected(SceneResourceError{"template publication", handle.error()});
            rollback.declaration = *handle;
        }
        MaterialTemplateView view;
        {
            auto access = m_Publication.BeginFrame();
            auto acquired = m_Templates.Acquire(access, rollback.declaration);
            if (!acquired) return std::unexpected(SceneResourceError{"template resolution", acquired.error()});
            view = std::move(*acquired);
        }
        auto instance = MaterialInstance::Create(view);
        if (!instance) return std::unexpected(SceneResourceError{"material instance", instance.error()});
        {
            auto access = m_Publication.BeginPublication();
            auto handle = m_Materials.Create(access, std::move(*instance));
            if (!handle) return std::unexpected(SceneResourceError{"material publication", handle.error()});
            rollback.material = *handle;
        }
        m_Roles.push_back({rollback.pipeline, desc.kind, desc.lineWidth});
        rollback.committed = true;
        return rollback.material;
    }
}
