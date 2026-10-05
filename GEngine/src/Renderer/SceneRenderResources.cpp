#include "gepch.h"
#include "Renderer/SceneRenderResources.h"
#include "Scene/_Entity.h"
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Managers/ShapeManager.h"
#include "Managers/AssetsManager.h"
#include <fstream>
#include <format>
#include <new>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include "SubmissionGpuLayout.h"
// Focused checks execute only under the RBS validation opt-in. Their native
// observation/shader probes compile here so normal application includes stay semantic.
#define GENGINE_MATERIAL_AUTHORING_BACKEND
#include "../../../RigidBodySimulation/tests/MaterialAuthoringChecks.h"
#undef GENGINE_MATERIAL_AUTHORING_BACKEND
#define GENGINE_SHADER_DESCRIPTION_BACKEND
#include "../../../RigidBodySimulation/tests/ShaderDescriptionChecks.h"
#undef GENGINE_SHADER_DESCRIPTION_BACKEND

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
out VS_OUT { vec3 FragPos; vec3 Normal; vec2 TexCoords;
#ifdef GENGINE_TYPED_MATERIAL
vec3 LocalPosition; vec3 LocalNormal; mat3 NormalMatrix;
#endif
} vs_out;
uniform mat4 u_projection, u_view, u_model;
uniform bool reverse_normals;
void main() {
vs_out.FragPos = vec3(u_model * vec4(aPos, 1.0));
vs_out.Normal = transpose(inverse(mat3(u_model))) * aNormal;
if (reverse_normals) vs_out.Normal = -vs_out.Normal;
vs_out.TexCoords = aTexCoords;
#ifdef GENGINE_TYPED_MATERIAL
vs_out.LocalPosition = aPos; vs_out.LocalNormal = aNormal;
vs_out.NormalMatrix = transpose(inverse(mat3(u_model)));
#endif
gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
})";
        constexpr const char* HelperVertex = R"(#version 460
layout(location=0) in vec3 vertexPosition;
layout(location=8) in vec3 vertexColor;
uniform mat4 u_model, u_view, u_projection;
out vec3 color;
void main() { gl_Position = u_projection * u_view * u_model * vec4(vertexPosition, 1.0); color = vertexColor; }
)";

        void ApplyLitCoverage(std::string& source)
        {
            // Preserve the lighting implementation and wrap only fragment coverage.
            source.insert(source.find('\n') + 1, "#define main frameLightingMain\n");
            source += R"(
#undef main
uniform int frameAlphaMode;
uniform float frameAlphaCutoff, frameOpacity;
uniform bool framePremultiplied;
void main() {
    float coverage = geBaseSample().a * frameOpacity * frameBaseAlpha;
    if (frameAlphaMode == 1 && coverage < frameAlphaCutoff) discard;
    frameLightingMain();
    FragColor.a = frameAlphaMode == 2 ? coverage : 1.0;
    if (frameAlphaMode == 2 && framePremultiplied) FragColor.rgb *= coverage;
}
)";
        }

        std::expected<Asset::ShaderDescription, SceneResourceError>
        DescribePackedProgram(const char* vertex, const std::string& source, const char* fragment,
                            SceneMaterialKind kind,
                            std::span<const MaterialParameterDecl> parameters, bool typed = false)
        {
            std::string vertexSource(vertex), fragmentSource(source);
            if (typed)
            {
                vertexSource.insert(vertexSource.find('\n') + 1, "#define GENGINE_TYPED_MATERIAL 1\n");
                fragmentSource.insert(fragmentSource.find('\n') + 1, "#define GENGINE_TYPED_MATERIAL 1\n");
            }
            auto packedVertex = RenderBackend::PackedStage(vertexSource, kind == SceneMaterialKind::Sky,
                                                           parameters, Asset::ShaderStage::Vertex);
            if (!packedVertex)
                return std::unexpected(packedVertex.error());
            auto packedFragment = RenderBackend::PackedStage(fragmentSource, false, parameters);
            if (!packedFragment)
                return std::unexpected(packedFragment.error());
            const Asset::ShaderSource stages[]{
                {Asset::ShaderStage::Vertex, *packedVertex,
                 "semantic vertex / packed frame-material input"},
                {Asset::ShaderStage::Fragment, *packedFragment, fragment}};
            auto shader = Asset::ShaderDescription::Create({stages, {}, {}});
            if (!shader)
                return std::unexpected(SceneResourceError{"program creation", shader.error()});
            return std::move(*shader);
        }

        std::expected<Asset::ShaderDescription, SceneResourceError>
        DescribeSceneProgram(SceneMaterialKind kind,
                           std::span<const MaterialParameterDecl> parameters, bool typed = false)
        {
            if (typed && (kind == SceneMaterialKind::Unlit || kind == SceneMaterialKind::Helper))
            {
                const char* unlit = R"(#version 450 core
in VS_OUT { vec3 FragPos; vec3 Normal; vec2 TexCoords;
vec3 LocalPosition; vec3 LocalNormal; mat3 NormalMatrix; } fs_in;
layout(location=0) out vec4 FragColor;
uniform sampler2D albedoMap;
uniform vec4 baseColor;
void main() {
vec4 texel = frameHasBaseColor ? geSampleSurface(albedoMap, fs_in.TexCoords,
    fs_in.LocalPosition, fs_in.LocalNormal) : vec4(1.0);
FragColor = vec4(texel.rgb * baseColor.rgb, 1.0);
})";
                const char* debug = R"(#version 450 core
in vec3 color;
layout(location=0) out vec4 FragColor;
uniform vec4 baseColor;
void main() { FragColor = vec4(baseColor.rgb, 1.0); }
)";
                return DescribePackedProgram(kind == SceneMaterialKind::Unlit ? LitVertex : HelperVertex,
                    kind == SceneMaterialKind::Unlit ? unlit : debug, "typed built-in unlit/debug",
                    kind, parameters, true);
            }
            const char* fragment = kind == SceneMaterialKind::Lit      ? "pbr_cascade_shadow.frag"
                                   : kind == SceneMaterialKind::Helper ? "basic.frag"
                                   : kind == SceneMaterialKind::PointLight
                                       ? "point_light_sphere_visual.frag"
                                       : "skybox_environment.frag";
            auto path = RuntimeAssets::TryFile(std::string("Shaders/") + fragment);
            if (!path)
                return std::unexpected(SceneResourceError{"shader path", path.error()});
            std::ifstream input(*path, std::ios::binary);
            if (!input)
                return std::unexpected(SceneResourceError{
                    "shader read", Asset::ShaderError{Asset::ShaderErrorCode::FileRead,
                                                      Asset::ShaderStage::Fragment, *path,
                                                      "Cannot open shader source"}});
            std::string source((std::istreambuf_iterator<char>(input)), {});
            if (input.bad())
                return std::unexpected(SceneResourceError{
                    "shader read", Asset::ShaderError{Asset::ShaderErrorCode::FileRead,
                                                      Asset::ShaderStage::Fragment, *path,
                                                      "Cannot read shader source"}});
            if (kind == SceneMaterialKind::Lit)
                ApplyLitCoverage(source);
            std::string sky;
            const char* vertex = kind == SceneMaterialKind::Helper ? HelperVertex : LitVertex;
            if (kind == SceneMaterialKind::Sky)
            {
                auto skyPath = RuntimeAssets::TryFile("Shaders/skybox_environment.vert");
                if (!skyPath)
                    return std::unexpected(SceneResourceError{"sky shader path", skyPath.error()});
                std::ifstream skyInput(*skyPath, std::ios::binary);
                if (!skyInput)
                    return std::unexpected(SceneResourceError{
                        "sky shader read", Asset::ShaderError{Asset::ShaderErrorCode::FileRead,
                                                              Asset::ShaderStage::Vertex, *skyPath,
                                                              "Cannot open shader source"}});
                sky.assign(std::istreambuf_iterator<char>(skyInput), {});
                if (skyInput.bad())
                    return std::unexpected(SceneResourceError{
                        "sky shader read", Asset::ShaderError{Asset::ShaderErrorCode::FileRead,
                                                              Asset::ShaderStage::Vertex, *skyPath,
                                                              "Cannot read shader source"}});
                vertex = sky.c_str();
            }
            return DescribePackedProgram(vertex, source, fragment, kind, parameters, typed);
        }

        PipelineDesc DescribePipeline(const SceneMaterialDesc& desc,
                                      Asset::ShaderProgramHandle program)
        {
            PipelineDesc pipeline;
            pipeline.program = program;
            pipeline.programRevision = 1;
            pipeline.cull = desc.doubleSided ? CullMode::None : CullMode::Back;
            pipeline.alpha = desc.alpha;
            pipeline.alphaCutoff = desc.alphaCutoff;
            pipeline.transparentBlend = desc.transparentBlend;
            if (desc.kind == SceneMaterialKind::Sky)
                pipeline.depthCompare = DepthCompare::LessEqual;
            return pipeline;
        }
    }

    std::string DescribeSceneResourceError(const SceneResourceError& error)
    {
        return error.operation + ": " +
               std::visit(
                   [](const auto& cause) -> std::string
                   {
                       using T = std::decay_t<decltype(cause)>;
                       if constexpr (std::is_enum_v<T>)
                           return std::format("domain={} code={}",
                                              std::same_as<T, SceneResourceCode> ? "scene-resource"
                                              : std::same_as<T, SceneAssignmentError>
                                                  ? "scene-assignment"
                                              : std::same_as<T, GeometryAuthoringCode>
                                                  ? "geometry-authoring"
                                                  : "registry",
                                              static_cast<int>(cause));
                       else if constexpr (std::same_as<T, Asset::ShaderError>)
                           return std::format("shader code={} stage={} source={} log={}",
                                              static_cast<int>(cause.code),
                                              cause.shaderType ? static_cast<int>(*cause.shaderType)
                                                               : 0,
                                              cause.source, cause.log);
                       else if constexpr (std::same_as<T, Asset::TextureError>)
                           return std::format(
                               "texture code={} source={} message={} system={} registry={}",
                               static_cast<int>(cause.code), cause.source, cause.message,
                               cause.system.message(), static_cast<int>(cause.registry));
                       else if constexpr (std::same_as<T, MeshError>)
                           return std::format("mesh code={} element={}",
                                              static_cast<int>(cause.code), cause.element);
                       else if constexpr (std::same_as<T, GeometryTemplates::Error>)
                           return std::format(
                               "geometry code={} element={} mesh-code={} mesh-element={}",
                               static_cast<int>(cause.code), cause.element,
                               cause.mesh ? static_cast<int>(cause.mesh->code) : -1,
                               cause.mesh ? cause.mesh->element : 0);
                       else if constexpr (std::same_as<T, GpuMeshError>)
                           return std::format("gpu-mesh code={} element={} registry={} message={}",
                                              static_cast<int>(cause.code), cause.element,
                                              static_cast<int>(cause.registry), cause.message);
                       else if constexpr (std::same_as<T, Asset::SamplerError>)
                           return std::format("sampler code={} registry={} message={}",
                                              static_cast<int>(cause.code),
                                              static_cast<int>(cause.registry), cause.message);
                       else if constexpr (std::same_as<T, PlatformError>)
                           return std::format("platform code={} operation={} message={}",
                                              static_cast<int>(cause.code), cause.operation,
                                              cause.message);
                       else
                           return std::format(
                               "material domain={} code={} binding={} message={}",
                               std::same_as<T, MaterialDeclarationError> ? "declaration"
                                                                         : "instance",
                               static_cast<int>(cause.code), cause.binding, cause.message);
                   },
                   error.cause);
    }

    SceneRenderResources::SceneRenderResources(Asset::AssetPublication& publication,
                                               Manager::ShapeManager& shapes,
                                               GeometryAuthoringLimits limits,
                                               Asset::AssetRegistryLimits registryLimits)
        : m_GeometryLimits(limits), m_Publication(publication), m_Shapes(shapes), m_Programs(publication),
          m_Pipelines(publication), m_Templates(publication), m_Materials(publication),
          m_Meshes(publication, registryLimits)
    {
    }

    std::expected<std::unique_ptr<SceneRenderResources>, SceneResourceError>
    SceneRenderResources::Create(EngineContext& root, GeometryAuthoringLimits limits,
                                 Asset::AssetRegistryLimits registryLimits)
    {
        if (limits.independentSources > 64 || limits.independentPayloadBytes > 64 * 1024 * 1024)
            return std::unexpected(SceneResourceError{"geometry limits", GeometryAuthoringCode::SourceCapacity});
        auto services = root.SceneServices();
        if (!services)
            return std::unexpected(SceneResourceError{"scene services", services.error()});
        std::unique_ptr<SceneRenderResources> result(
            new (std::nothrow) SceneRenderResources(services->publication, services->shapes, limits, registryLimits));
        if (!result)
            return std::unexpected(
                SceneResourceError{"scene resource owner", SceneResourceCode::Allocation});
        auto bindings = Manager::AssetsManager::FrameBindings(result->m_Programs);
        if (!bindings)
            return std::unexpected(SceneResourceError{"frame bindings", bindings.error()});
        result->m_Bindings.emplace(*bindings);
        return result;
    }

    SceneRenderResources::~SceneRenderResources()
    {
        (void)m_Publication.CanPublish(); // Existing owner-thread lifetime requirement.
        for (std::size_t i = 0; i < m_SourceCount; ++i)
            Asset::AssetDetail::RequireInvariant(m_Sources[i].owners == 0);
        if (m_GeometryDomain)
        {
            auto** link = &s_GeometryObservers[m_GeometryDomain % s_GeometryObservers.size()];
            while (*link && *link != this) link = &(*link)->m_NextGeometryObserver;
            Asset::AssetDetail::RequireInvariant(*link == this);
            *link = m_NextGeometryObserver;
        }
        // No per-Entity reclamation: successful CPU source remains until teardown.
    }

    void SceneRenderResources::RegisterGeometryObserver(std::uint64_t domain)
    {
        if (m_GeometryDomain)
        {
            Asset::AssetDetail::RequireInvariant(m_GeometryDomain == domain);
            return;
        }
        auto& bucket = s_GeometryObservers[domain % s_GeometryObservers.size()];
        m_GeometryDomain = domain;
        m_NextGeometryObserver = bucket;
        bucket = this;
    }

    SceneRenderResources* SceneRenderResources::GeometryPublisher(Asset::MeshHandle handle)
    {
        if (!handle) return nullptr;
        for (auto* owner = s_GeometryObservers[handle.registry % s_GeometryObservers.size()];
             owner; owner = owner->m_NextGeometryObserver)
            if (owner->m_GeometryDomain == handle.registry) return owner;
        return nullptr;
    }

    SceneRenderResources::SourceRecord*
    SceneRenderResources::FindSource(Asset::MeshHandle handle, bool ownershipQuery) const
    {
        std::size_t first = 0, last = m_SourceCount;
        while (first < last)
        {
            const auto middle = first + (last - first) / 2;
            if (ownershipQuery) ++m_GeometryWork.ownershipKeyComparisons;
            if (m_Sources[middle].handle < handle) first = middle + 1;
            else last = middle;
        }
        if (first == m_SourceCount) return nullptr;
        if (ownershipQuery) ++m_GeometryWork.ownershipKeyComparisons;
        return m_Sources[first].handle == handle ? &m_Sources[first] : nullptr;
    }

    bool SceneRenderResources::CanAddGeometryOwner(Asset::MeshHandle handle)
    {
        auto* owner = GeometryPublisher(handle);
        if (!owner) return true; // Unsupported source cannot later qualify as unique.
        (void)owner->m_Publication.CanPublish();
        const auto* record = owner->FindSource(handle);
        return !record || record->owners != UINT64_MAX;
    }

    bool SceneRenderResources::AddGeometryOwner(Asset::MeshHandle handle)
    {
        auto* owner = GeometryPublisher(handle);
        if (!owner) return true;
        (void)owner->m_Publication.CanPublish();
        if (auto* record = owner->FindSource(handle))
        {
            if (record->owners == UINT64_MAX) return false;
            ++record->owners;
            ++owner->m_GeometryWork.ownerAdditions;
        }
        return true;
    }

    void SceneRenderResources::RemoveGeometryOwner(Asset::MeshHandle handle)
    {
        if (auto* owner = GeometryPublisher(handle))
        {
            (void)owner->m_Publication.CanPublish();
            if (auto* record = owner->FindSource(handle))
            {
                Asset::AssetDetail::RequireInvariant(record->owners != 0);
                --record->owners;
                ++owner->m_GeometryWork.ownerRemovals;
            }
        }
    }

    void SceneRenderResources::IncompleteGeometryScene(bool entering)
    {
        if (entering)
        {
            Asset::AssetDetail::RequireInvariant(s_IncompleteGeometryScenes != SIZE_MAX);
            ++s_IncompleteGeometryScenes;
        }
        else
        {
            Asset::AssetDetail::RequireInvariant(s_IncompleteGeometryScenes != 0);
            --s_IncompleteGeometryScenes;
        }
    }

    GeometryAuthoringWork SceneRenderResources::GeometryWork() const
    {
        (void)m_Publication.CanPublish();
        return m_GeometryWork;
    }

    std::expected<GeometrySourceView, SceneResourceError>
    SceneRenderResources::GeometrySource(Asset::MeshHandle handle) const
    {
        auto snapshot = m_Meshes.ReadAuthoringSnapshot(handle);
        if (!snapshot) return std::unexpected(SceneResourceError{"geometry identity", snapshot.error()});
        const auto* record = FindSource(handle);
        if (!record) return std::unexpected(SceneResourceError{"geometry source", GeometryAuthoringCode::SourceUnavailable});
        if (record->revision != snapshot->revision)
            return std::unexpected(SceneResourceError{"geometry source", GeometryAuthoringCode::SourceVersionMismatch});
        return GeometrySourceView{{handle, record->revision, record->source->Bounds()}, record->source.get()};
    }

    std::expected<GeometryOwnership, SceneResourceError>
    SceneRenderResources::GeometryOwners(Asset::MeshHandle handle) const
    {
        auto snapshot = m_Meshes.ReadAuthoringSnapshot(handle);
        if (!snapshot) return std::unexpected(SceneResourceError{"geometry identity", snapshot.error()});
        ++m_GeometryWork.ownershipQueries;
        if (s_IncompleteGeometryScenes)
            return std::unexpected(SceneResourceError{"geometry ownership", GeometryAuthoringCode::OwnershipUnavailable});
        const auto* record = FindSource(handle, true);
        if (!record) return std::unexpected(SceneResourceError{"geometry source", GeometryAuthoringCode::SourceUnavailable});
        if (record->revision != snapshot->revision)
            return std::unexpected(SceneResourceError{"geometry source", GeometryAuthoringCode::SourceVersionMismatch});
        return GeometryOwnership{record->owners, record->kind != SourceKind::Independent};
    }

    std::expected<void, SceneResourceError>
    SceneRenderResources::CheckSourceBudget(const MeshAsset& mesh, SourceKind kind) const
    {
        const auto vertices = mesh.Vertices().size(), indices = mesh.Indices().size();
        if (vertices > MaximumPayloadBytes || indices > MaximumPayloadBytes - vertices ||
            mesh.Submeshes().size() > 64 ||
            sizeof(MeshAsset) + mesh.Submeshes().size() * sizeof(SubmeshRange) > 4096 ||
            m_SourceCount == MaximumSources)
            return std::unexpected(SceneResourceError{"geometry source limit", GeometryAuthoringCode::SourceCapacity});
        const auto bytes = vertices + indices;
        if ((kind == SourceKind::Legacy && m_LegacySourceCount == 32) ||
            (kind == SourceKind::Template && m_SharedTemplateCount == 128) ||
            (kind == SourceKind::Independent &&
             (m_GeometryWork.independentSources >= m_GeometryLimits.independentSources ||
              bytes > m_GeometryLimits.independentPayloadBytes - m_GeometryWork.independentPayloadBytes)))
            return std::unexpected(SceneResourceError{"geometry source limit", GeometryAuthoringCode::SourceCapacity});
        return {};
    }

    std::expected<GeometryIdentity, SceneResourceError>
    SceneRenderResources::PublishAuthored(MeshAsset mesh, SourceKind kind)
    {
        if (auto budget = CheckSourceBudget(mesh, kind); !budget) return std::unexpected(budget.error());
        std::unique_ptr<MeshAsset> retained(new (std::nothrow) MeshAsset(std::move(mesh)));
        if (!retained) return std::unexpected(SceneResourceError{"geometry source", GeometryAuthoringCode::Allocation});
        auto publication = m_Publication.BeginPublication();
        auto handle = PublishMesh(m_Meshes, publication, *retained);
        if (!handle) return std::unexpected(SceneResourceError{"geometry publication", handle.error()});
        // Fixed record storage, no fallible step after publication. Sorted insertion
        // moves owners of CPU payload; immutable MeshAsset addresses never move.
        std::size_t position = 0;
        while (position < m_SourceCount && m_Sources[position].handle < *handle) ++position;
        for (std::size_t i = m_SourceCount; i > position; --i) m_Sources[i] = std::move(m_Sources[i - 1]);
        const auto bytes = retained->Vertices().size() + retained->Indices().size();
        const GeometryIdentity result{*handle, 1, retained->Bounds()};
        m_Sources[position] = {*handle, 1, 0, kind, std::move(retained)};
        ++m_SourceCount;
        m_GeometryWork.sourceRecords = m_SourceCount;
        m_GeometryWork.sourcePayloadBytes += bytes;
        if (kind == SourceKind::Legacy) ++m_LegacySourceCount;
        if (kind == SourceKind::Independent)
        {
            ++m_GeometryWork.independentSources;
            m_GeometryWork.independentPayloadBytes += bytes;
        }
        RegisterGeometryObserver(handle->registry);
        return result;
    }

    void SceneRenderResources::DiscardCandidate(Asset::MeshHandle handle)
    {
        auto* record = FindSource(handle);
        Asset::AssetDetail::RequireInvariant(record && record->owners == 0);
        const auto position = std::size_t(record - m_Sources.data());
        const auto bytes = record->source->Vertices().size() + record->source->Indices().size();
        if (record->kind == SourceKind::Independent)
        {
            --m_GeometryWork.independentSources;
            m_GeometryWork.independentPayloadBytes -= bytes;
        }
        if (record->kind == SourceKind::Legacy) --m_LegacySourceCount;
        m_GeometryWork.sourcePayloadBytes -= bytes;
        for (std::size_t i = position; i + 1 < m_SourceCount; ++i) m_Sources[i] = std::move(m_Sources[i + 1]);
        m_Sources[--m_SourceCount] = {};
        m_GeometryWork.sourceRecords = m_SourceCount;
        auto publication = m_Publication.BeginPublication();
        auto destroyed = m_Meshes.Destroy(publication, handle);
        Asset::AssetDetail::RequireInvariant(bool(destroyed));
        m_Meshes.Collect(publication);
    }

    std::expected<MeshAuthoringMetadata, Asset::RegistryError>
    SceneRenderResources::MeshMetadata(Asset::MeshHandle handle) const
    {
        return m_Meshes.ReadMetadata(handle);
    }

    namespace
    {
        MeshSourceData AuthoredSource(const MeshAsset& mesh, std::span<const std::byte> vertices)
        {
            return {mesh.Layout(), vertices, mesh.VertexCount(), mesh.IndexFormat(),
                    mesh.Indices(), mesh.IndexCount(), mesh.Submeshes(),
                    mesh.MaterialSlotCount(), mesh.UpdateIntent()};
        }

        glm::dvec3 ReadVector(const std::byte* record, const VertexAttribute& attribute)
        {
            std::array<float, 3> values;
            std::memcpy(values.data(), record + attribute.offsetBytes, sizeof(values));
            return {values[0], values[1], values[2]};
        }

        void WriteVector(std::byte* record, const VertexAttribute& attribute, const glm::dvec3& vector)
        {
            const std::array<float, 3> values{float(vector.x), float(vector.y), float(vector.z)};
            std::memcpy(record + attribute.offsetBytes, values.data(), sizeof(values));
        }

        bool UnitVector(glm::dvec3& vector)
        {
            const auto length = glm::length(vector);
            if (!std::isfinite(length) || length == 0) return false;
            vector /= length;
            return true;
        }

        std::expected<MeshAsset, SceneResourceError> ScaledAuthoredSource(const MeshAsset& mesh,
                                                                        const Math::Vec3f& localScale)
        {
            const VertexAttribute *position = nullptr, *normal = nullptr, *tangent = nullptr, *bitangent = nullptr;
            const auto layout = mesh.Layout();
            for (const auto& attribute : layout.attributes)
            {
                if (attribute.semantic == VertexSemantic::JointIndices || attribute.semantic == VertexSemantic::JointWeights)
                    return std::unexpected(SceneResourceError{"geometry Bake layout", GeometryAuthoringCode::UnsupportedLayout});
                const bool basis = attribute.semantic == VertexSemantic::Position || attribute.semantic == VertexSemantic::Normal ||
                    attribute.semantic == VertexSemantic::Tangent || attribute.semantic == VertexSemantic::Bitangent;
                if (basis && (attribute.scalar != VertexScalarFormat::Float32 ||
                    attribute.interpretation != VertexInterpretation::Floating ||
                    (attribute.components != 3 && !(attribute.semantic == VertexSemantic::Tangent && attribute.components == 4))))
                    return std::unexpected(SceneResourceError{"geometry Bake layout", GeometryAuthoringCode::UnsupportedLayout});
                switch (attribute.semantic)
                {
                case VertexSemantic::Position: position = &attribute; break;
                case VertexSemantic::Normal: normal = &attribute; break;
                case VertexSemantic::Tangent: tangent = &attribute; break;
                case VertexSemantic::Bitangent: bitangent = &attribute; break;
                default: break;
                }
            }
            if (!position || ((tangent || bitangent) && !normal))
                return std::unexpected(SceneResourceError{"geometry Bake layout", GeometryAuthoringCode::UnsupportedLayout});
            const auto bytes = mesh.Vertices().size();
            std::unique_ptr<std::byte[]> transformed;
            if (bytes)
            {
                transformed.reset(new (std::nothrow) std::byte[bytes]);
                if (!transformed) return std::unexpected(SceneResourceError{"geometry Bake preparation", GeometryAuthoringCode::Allocation});
                std::memcpy(transformed.get(), mesh.Vertices().data(), bytes);
            }
            const glm::dvec3 scale(localScale);
            for (std::size_t i = 0; i < mesh.VertexCount(); ++i)
            {
                auto* record = transformed.get() + i * layout.strideBytes;
                const auto p = ReadVector(record, *position) * scale;
                for (int axis = 0; axis < 3; ++axis)
                    if (!std::isfinite(p[axis]) || std::abs(p[axis]) > (std::numeric_limits<float>::max)() ||
                        (p[axis] != 0 && float(p[axis]) == 0))
                        return std::unexpected(SceneResourceError{"geometry Bake position", GeometryAuthoringCode::InvalidScale});
                WriteVector(record, *position, p);
                glm::dvec3 n{}, t{};
                if (normal)
                {
                    n = ReadVector(record, *normal) / scale;
                    if (!UnitVector(n)) return std::unexpected(SceneResourceError{"geometry Bake normal", GeometryAuthoringCode::InvalidBasis});
                    WriteVector(record, *normal, n);
                }
                if (tangent)
                {
                    t = ReadVector(record, *tangent) * scale;
                    t -= n * glm::dot(n, t);
                    if (!UnitVector(t)) return std::unexpected(SceneResourceError{"geometry Bake tangent", GeometryAuthoringCode::InvalidBasis});
                    if (tangent->components == 4)
                    {
                        float handedness;
                        std::memcpy(&handedness, record + tangent->offsetBytes + 3 * sizeof(float), sizeof(float));
                        if (!std::isfinite(handedness) || std::abs(handedness) != 1.f)
                            return std::unexpected(SceneResourceError{"geometry Bake handedness", GeometryAuthoringCode::InvalidBasis});
                    }
                    WriteVector(record, *tangent, t); // Optional handedness component stays unchanged.
                }
                if (bitangent)
                {
                    auto b = ReadVector(record, *bitangent) * scale;
                    b -= n * glm::dot(n, b);
                    if (tangent) b -= t * glm::dot(t, b);
                    if (!UnitVector(b)) return std::unexpected(SceneResourceError{"geometry Bake bitangent", GeometryAuthoringCode::InvalidBasis});
                    WriteVector(record, *bitangent, b);
                }
            }
            auto result = MeshAsset::Create(AuthoredSource(mesh, {transformed.get(), bytes}));
            if (!result) return std::unexpected(SceneResourceError{"geometry Bake validation", result.error()});
            return std::move(*result);
        }
    }

    std::expected<MeshAsset, SceneResourceError>
    BakeAuthoredGeometry(const MeshAsset& mesh, const Math::Vec3f& scale)
    {
        if (!Math::IsFinite(scale) || scale.x <= 0 || scale.y <= 0 || scale.z <= 0)
            return std::unexpected(SceneResourceError{"geometry Bake scale", GeometryAuthoringCode::InvalidScale});
        const auto vertices = mesh.Vertices().size(), indices = mesh.Indices().size();
        if (vertices > 8 * 1024 * 1024 || indices > 8 * 1024 * 1024 - vertices ||
            mesh.Submeshes().size() > 64 ||
            sizeof(MeshAsset) + mesh.Submeshes().size() * sizeof(SubmeshRange) > 4096)
            return std::unexpected(SceneResourceError{"geometry Bake workspace", GeometryAuthoringCode::SourceCapacity});
        return ScaledAuthoredSource(mesh, scale);
    }

    std::expected<GeometrySourceView, SceneResourceError>
    SceneRenderResources::EntityGeometry(_Entity entity) const
    {
        if (!entity || !entity.HasAllComponents<Component::MeshRendererComponent>())
            return std::unexpected(SceneResourceError{"geometry Entity", GeometryAuthoringCode::InvalidEntity});
        if (entity.GetSceneContext()->RenderData().IsExtracting())
            return std::unexpected(SceneResourceError{"geometry Entity", SceneAssignmentError::ExtractionActive});
        if (!entity.HasAllComponents<Component::Transform3DComponent>())
            return std::unexpected(SceneResourceError{"geometry Transform", GeometryAuthoringCode::MissingTransform});
        return GeometrySource(std::as_const(entity).GetComponent<Component::MeshRendererComponent>().mesh);
    }

    std::expected<void, SceneResourceError>
    SceneRenderResources::PrepareGeometryAssignment(_Entity entity, std::size_t submeshCount)
    {
        auto* scene = entity.GetSceneContext();
        if (!entity || !scene || !entity.HasAllComponents<Component::IDComponent>())
            return std::unexpected(SceneResourceError{"geometry Entity", GeometryAuthoringCode::InvalidEntity});
        if (scene->RenderData().IsExtracting())
            return std::unexpected(SceneResourceError{"geometry assignment", SceneAssignmentError::ExtractionActive});
        const auto intent = std::as_const(entity).GetComponent<Component::MeshRendererComponent>();
        if (intent.submesh >= submeshCount)
            return std::unexpected(SceneResourceError{"geometry assignment", SceneAssignmentError::InvalidSubmesh});
        if (scene->m_RenderAssignmentRevision == UINT64_MAX)
            return std::unexpected(SceneResourceError{"geometry assignment", SceneAssignmentError::RevisionExhausted});
        {
            auto access = m_Publication.BeginFrame();
            if (auto material = m_Materials.Acquire(access, intent.material); !material)
                return std::unexpected(SceneResourceError{"geometry assignment", SceneAssignmentError::InvalidMaterial});
        }
        if (!scene->PrepareGeometryReceipt(entity))
            return std::unexpected(SceneResourceError{"geometry assignment", SceneAssignmentError::AuthoringAllocation});
        if (auto identity = scene->RenderData().Identify(entity); !identity)
            return std::unexpected(SceneResourceError{"geometry assignment", SceneAssignmentError::IdentityExhausted});
        return {};
    }

    std::expected<GeometryIdentity, SceneResourceError>
    SceneRenderResources::CloneGeometry(Asset::MeshHandle handle)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(SceneResourceError{"geometry Clone", SceneResourceCode::PublicationBusy});
        auto source = GeometrySource(handle);
        if (!source) return std::unexpected(source.error());
        if (auto budget = CheckSourceBudget(*source->source, SourceKind::Independent); !budget)
            return std::unexpected(budget.error());
        const auto temporary = source->source->Vertices().size() + source->source->Indices().size() +
            source->source->Submeshes().size() * sizeof(SubmeshRange) + sizeof(MeshAsset);
        if (temporary > MaximumTemporaryBytes)
            return std::unexpected(SceneResourceError{"geometry Clone workspace", GeometryAuthoringCode::SourceCapacity});
        m_GeometryWork.peakTemporaryBytes = (std::max)(m_GeometryWork.peakTemporaryBytes, temporary);
        auto mesh = MeshAsset::Create(AuthoredSource(*source->source, source->source->Vertices()));
        if (!mesh) return std::unexpected(SceneResourceError{"geometry Clone validation", mesh.error()});
        return PublishAuthored(std::move(*mesh), SourceKind::Independent);
    }

    std::expected<GeometryChange, SceneResourceError>
    SceneRenderResources::MakeGeometryUnique(_Entity entity)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(SceneResourceError{"geometry MakeUnique", SceneResourceCode::PublicationBusy});
        auto source = EntityGeometry(entity);
        if (!source) return std::unexpected(source.error());
        auto ownership = GeometryOwners(source->geometry.handle);
        if (!ownership) return std::unexpected(ownership.error());
        const auto scale = std::as_const(entity).GetComponent<Component::Transform3DComponent>().Scale;
        if (ownership->IsUnique())
            return GeometryChange{GeometryOperation::MakeUnique, false, source->geometry, source->geometry,
                                  entity.GetSceneContext()->GetRenderAssignmentRevision(), scale};
        if (auto prepared = PrepareGeometryAssignment(entity, source->source->Submeshes().size()); !prepared)
            return std::unexpected(prepared.error());
        auto geometry = CloneGeometry(source->geometry.handle);
        if (!geometry) return std::unexpected(geometry.error());
        auto intent = std::as_const(entity).GetComponent<Component::MeshRendererComponent>();
        intent.mesh = geometry->handle;
        auto assigned = AssignRenderable(entity, intent);
        if (!assigned)
        {
            DiscardCandidate(geometry->handle);
            return std::unexpected(assigned.error());
        }
        return GeometryChange{GeometryOperation::MakeUnique, true, source->geometry, *geometry, assigned->revision, scale};
    }

    std::expected<ObjectDimensions, GeometryAuthoringCode>
    MeasureObjectDimensions(const GeometryIdentity& geometry, const Math::Vec3f& scale)
    {
        if (!Math::IsFinite(scale))
            return std::unexpected(GeometryAuthoringCode::InvalidScale);
        ObjectDimensions result{geometry, BoundsStatus::Empty, {}, scale};
        if (geometry.bounds.empty) return result;
        if (!std::isfinite(geometry.bounds.sphereRadius) || geometry.bounds.sphereRadius < 0)
            return std::unexpected(GeometryAuthoringCode::InvalidBounds);
        result.status = BoundsStatus::Valid;
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto lo = geometry.bounds.minimum[axis], hi = geometry.bounds.maximum[axis];
            const auto extent = hi - lo;
            if (!std::isfinite(lo) || !std::isfinite(hi) || !std::isfinite(extent) || extent < 0 ||
                !std::isfinite(geometry.bounds.sphereCenter[axis]))
                return std::unexpected(GeometryAuthoringCode::InvalidBounds);
            result.dimensions[axis] = extent * std::abs(double(scale[axis]));
            if (!std::isfinite(result.dimensions[axis]))
                return std::unexpected(GeometryAuthoringCode::InvalidDimensions);
        }
        return result;
    }

    std::expected<Math::Vec3f, GeometryAuthoringCode>
    ResolveDimensionScale(const ObjectDimensions& previous, const std::array<double, 3>& requested)
    {
        auto valid = MeasureObjectDimensions(previous.geometry, previous.localScale);
        if (!valid) return std::unexpected(valid.error());
        if (valid->status == BoundsStatus::Empty) return std::unexpected(GeometryAuthoringCode::EmptyBounds);
        auto scale = previous.localScale;
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto target = requested[axis];
            const auto extent = previous.geometry.bounds.maximum[axis] - previous.geometry.bounds.minimum[axis];
            if (!std::isfinite(target) || target < 0)
                return std::unexpected(GeometryAuthoringCode::InvalidDimensions);
            if (extent == 0)
            {
                if (target != 0)
                    return std::unexpected(GeometryAuthoringCode::DegenerateExtent);
                continue; // Preserve local scale on a degenerate axis; never divide by epsilon.
            }
            const auto magnitude = target / extent;
            if (target == 0 || !std::isfinite(magnitude) || magnitude > (std::numeric_limits<float>::max)() || float(magnitude) == 0)
                return std::unexpected(GeometryAuthoringCode::InvalidDimensions);
            scale[axis] = std::copysign(float(magnitude), scale[axis] == 0 ? 1.f : scale[axis]);
        }
        return scale;
    }

    std::expected<ObjectDimensions, SceneResourceError>
    SceneRenderResources::Dimensions(_Entity entity) const
    {
        auto source = EntityGeometry(entity);
        if (!source) return std::unexpected(source.error());
        auto result = MeasureObjectDimensions(source->geometry,
            std::as_const(entity).GetComponent<Component::Transform3DComponent>().Scale);
        if (!result) return std::unexpected(SceneResourceError{"object dimensions", result.error()});
        return *result;
    }

    std::expected<ObjectDimensionsChange, SceneResourceError>
    SceneRenderResources::SetDimensions(_Entity entity, const std::array<double, 3>& requested)
    {
        auto previous = Dimensions(entity);
        if (!previous) return std::unexpected(previous.error());
        auto scale = ResolveDimensionScale(*previous, requested);
        if (!scale) return std::unexpected(SceneResourceError{"object dimensions", scale.error()});
        const bool changed = *scale != previous->localScale;
        entity.Transform().SetScale(*scale); // Preserve the existing S22 callback/no-op path.
        auto result = MeasureObjectDimensions(previous->geometry, *scale);
        Asset::AssetDetail::RequireInvariant(bool(result));
        return ObjectDimensionsChange{changed, *result};
    }

    std::expected<GeometryChange, SceneResourceError>
    SceneRenderResources::RegenerateGeometry(_Entity entity, const GeometryTemplates::Request& request)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(SceneResourceError{"geometry regeneration", SceneResourceCode::PublicationBusy});
        auto previous = EntityGeometry(entity);
        if (!previous) return std::unexpected(previous.error());
        auto key = GeometryTemplates::KeyFor(request);
        if (!key) return std::unexpected(SceneResourceError{"geometry parameters", key.error()});
        if (auto prepared = PrepareGeometryAssignment(entity, 1); !prepared) return std::unexpected(prepared.error());
        const auto cacheBefore = m_SharedTemplateCount;
        auto mesh = PublishGeometry(request);
        if (!mesh) return std::unexpected(mesh.error());
        const auto source = GeometrySource(*mesh);
        Asset::AssetDetail::RequireInvariant(bool(source));
        auto intent = std::as_const(entity).GetComponent<Component::MeshRendererComponent>();
        intent.mesh = *mesh;
        auto assigned = AssignRenderable(entity, intent);
        if (!assigned)
        {
            if (m_SharedTemplateCount != cacheBefore)
            {
                m_SharedTemplates[--m_SharedTemplateCount] = {};
                DiscardCandidate(*mesh);
            }
            return std::unexpected(assigned.error());
        }
        return GeometryChange{GeometryOperation::Regenerate, assigned->changed, previous->geometry, source->geometry,
            assigned->revision, std::as_const(entity).GetComponent<Component::Transform3DComponent>().Scale};
    }

    std::expected<GeometryChange, SceneResourceError>
    SceneRenderResources::BakeGeometry(_Entity entity)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(SceneResourceError{"geometry Bake", SceneResourceCode::PublicationBusy});
        auto previous = EntityGeometry(entity);
        if (!previous) return std::unexpected(previous.error());
        auto ownership = GeometryOwners(previous->geometry.handle);
        if (!ownership) return std::unexpected(ownership.error());
        if (!ownership->IsUnique())
            return std::unexpected(SceneResourceError{"geometry Bake", GeometryAuthoringCode::GeometryNotUnique});
        const auto& transform = std::as_const(entity).GetComponent<Component::Transform3DComponent>();
        const auto scale = transform.Scale;
        if (!Math::IsFinite(scale) || scale.x <= 0 || scale.y <= 0 || scale.z <= 0)
            return std::unexpected(SceneResourceError{"geometry Bake scale", GeometryAuthoringCode::InvalidScale});
        if (entity.GetSceneContext()->HasLiveGeometryScaleBinding(entity))
            return std::unexpected(SceneResourceError{"geometry Bake reset", GeometryAuthoringCode::LiveScaleBinding});
        if (scale == Math::Vec3f(1.f))
            return GeometryChange{GeometryOperation::Bake, false, previous->geometry, previous->geometry,
                entity.GetSceneContext()->GetRenderAssignmentRevision(), scale};
        // Arbitrary callbacks can re-enter assignment or modify the target. They have
        // no reviewed transactional reset contract; preserve them with a typed rejection.
        if (transform.OnScaleChanged)
            return std::unexpected(SceneResourceError{"geometry Bake reset", GeometryAuthoringCode::ScaleCallbackUnsupported});
        if (auto budget = CheckSourceBudget(*previous->source, SourceKind::Independent); !budget)
            return std::unexpected(budget.error());
        if (auto prepared = PrepareGeometryAssignment(entity, previous->source->Submeshes().size()); !prepared)
            return std::unexpected(prepared.error());
        const auto temporary = previous->source->Vertices().size() * 2 + previous->source->Indices().size() +
            previous->source->Submeshes().size() * sizeof(SubmeshRange) + sizeof(MeshAsset);
        if (temporary > MaximumTemporaryBytes)
            return std::unexpected(SceneResourceError{"geometry Bake workspace", GeometryAuthoringCode::SourceCapacity});
        m_GeometryWork.peakTemporaryBytes = (std::max)(m_GeometryWork.peakTemporaryBytes, temporary);
        auto mesh = BakeAuthoredGeometry(*previous->source, scale);
        if (!mesh) return std::unexpected(mesh.error());
        auto currentOwnership = GeometryOwners(previous->geometry.handle);
        if (!currentOwnership || !currentOwnership->IsUnique())
            return std::unexpected(currentOwnership ? SceneResourceError{"geometry Bake commit", GeometryAuthoringCode::GeometryNotUnique}
                                                     : currentOwnership.error());
        auto geometry = PublishAuthored(std::move(*mesh), SourceKind::Independent);
        if (!geometry) return std::unexpected(geometry.error());
        auto intent = std::as_const(entity).GetComponent<Component::MeshRendererComponent>();
        intent.mesh = geometry->handle;
        auto assigned = AssignRenderable(entity, intent);
        if (!assigned)
        {
            DiscardCandidate(geometry->handle);
            return std::unexpected(assigned.error());
        }
        entity.Transform().SetScale(Math::Vec3f(1.f));
        return GeometryChange{GeometryOperation::Bake, true, previous->geometry, *geometry,
                              assigned->revision, Math::Vec3f(1.f)};
    }

    std::expected<SceneAssignmentChange, SceneResourceError>
    SceneRenderResources::AssignRenderable(_Entity entity,
                                           const Component::MeshRendererComponent& value)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(
                SceneResourceError{"renderable assignment", SceneResourceCode::PublicationBusy});
        if (auto typed = DescribeMaterial(value.material); typed && typed->mapping == TextureMappingMode::Triplanar)
        {
            auto source = GeometrySource(value.mesh);
            if (!source || !SupportsLocalProjection(value.mesh, source->geometry.revision))
                return std::unexpected(SceneResourceError{"Triplanar requires retained finite nonzero local normals",
                    SceneResourceCode::InvalidMaterial});
        }
        auto access = m_Publication.BeginFrame();
        auto assigned = entity.AssignRenderable(value, ForFrame(access));
        if (!assigned)
            return std::unexpected(SceneResourceError{"renderable assignment", assigned.error()});
        return *assigned;
    }

    bool SceneRenderResources::SupportsLocalProjection(Asset::MeshHandle handle, std::uint64_t revision)
    {
        auto* owner = GeometryPublisher(handle);
        auto* record = owner ? owner->FindSource(handle) : nullptr;
        if (!record || record->revision != revision || !record->source) return false;
        if (record->localProjection) return *record->localProjection;
        const auto& source = *record->source;
        const VertexAttribute* normal = nullptr;
        for (const auto& attribute : source.Layout().attributes)
            if (attribute.semantic == VertexSemantic::Normal) normal = &attribute;
        bool supported = normal && normal->scalar == VertexScalarFormat::Float32 && normal->components == 3;
        if (supported)
            for (std::size_t i = 0; i < source.VertexCount(); ++i)
            {
                std::array<float, 3> n;
                std::memcpy(n.data(), source.Vertices().data() + i * source.Layout().strideBytes + normal->offsetBytes, sizeof(n));
                const double length2 = double(n[0])*n[0] + double(n[1])*n[1] + double(n[2])*n[2];
                if (!std::isfinite(length2) || length2 < 1e-12) { supported = false; break; }
            }
        record->localProjection = supported;
        return supported;
    }

    std::expected<std::unique_ptr<Asset::AsyncMeshLoader>, Asset::AsyncMeshError>
    SceneRenderResources::CreateMeshLoader(std::filesystem::path root,
                                           Asset::AsyncMeshLimits limits)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(
                Asset::AsyncMeshError{Asset::UploadError{Asset::UploadCode::PublicationBusy}});
        return Asset::AsyncMeshLoader::Create(m_Publication, m_Meshes, std::move(root), limits);
    }

    std::expected<bool, SceneResourceError>
    SceneRenderResources::SetMaterialTexture(Asset::MaterialInstanceHandle handle,
                                             std::string_view name, MaterialTextureValue value)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(
                SceneResourceError{"material texture", SceneResourceCode::PublicationBusy});
        // Compatibility callers editing a typed instance retain its semantic checks.
        if (auto typed = DescribeMaterial(handle); typed)
        {
            constexpr std::string_view names[]{"albedoMap", "normalMap", "metallicMap", "roughnessMap", "aoMap"};
            for (std::size_t i = 0; i < std::size(names); ++i)
                if (names[i] == name) return SetMaterialTexture(handle, static_cast<MaterialTextureSemantic>(i), value);
            return std::unexpected(SceneResourceError{"unknown material texture role", SceneResourceCode::InvalidMaterial});
        }
        std::optional<MaterialInstance> candidate;
        {
            auto access = m_Publication.BeginFrame();
            auto previous = m_Materials.Acquire(access, handle);
            if (!previous)
                return std::unexpected(SceneResourceError{"material lookup", previous.error()});
            candidate = **previous;
            const auto revision = candidate->Revision();
            if (auto changed = candidate->SetTexture(name, value); !changed)
                return std::unexpected(SceneResourceError{"material texture", changed.error()});
            auto texture = m_Bindings->textures.Acquire(access, value.texture);
            if (!texture)
                return std::unexpected(
                    SceneResourceError{"material texture identity", texture.error()});
            auto sampler = m_Bindings->samplers.Acquire(access, value.sampler);
            if (!sampler)
                return std::unexpected(
                    SceneResourceError{"material sampler identity", sampler.error()});
            if (candidate->Revision() == revision)
                return false;
        }
        auto publication = m_Publication.BeginPublication();
        auto replaced = m_Materials.Replace(publication, handle, std::move(*candidate));
        if (!replaced)
            return std::unexpected(SceneResourceError{"material replacement", replaced.error()});
        m_Materials.Collect(publication);
        return true;
    }

    RenderStateResources
    SceneRenderResources::ForFrame(const Asset::AssetPublication::FrameAccess& access) const
    {
        return {access, m_Meshes, m_Materials, *m_Bindings, {}};
    }

    std::expected<void, SceneResourceError>
    SceneRenderResources::AttachPhysicsShape(_Entity& entity, std::string_view name)
    {
        return m_Shapes.AttachPhysicsShape(entity, name);
    }

    std::expected<Asset::MeshHandle, SceneResourceError>
    SceneRenderResources::PublishShape(std::string_view name)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(
                SceneResourceError{"shape publication", SceneResourceCode::PublicationBusy});
        for (const auto& [key, handle] : m_SharedShapes)
            if (key == name)
            {
                if (auto source = GeometrySource(handle); !source) return std::unexpected(source.error());
                return handle;
            }
        auto mesh = m_Shapes.ExportMesh(name);
        if (!mesh)
            return std::unexpected(mesh.error());
        // Prepare legacy cache allocations before GPU publication/source commit.
        std::string key(name);
        m_SharedShapes.reserve(m_SharedShapes.size() + 1);
        auto geometry = PublishAuthored(std::move(*mesh), SourceKind::Legacy);
        if (!geometry) return std::unexpected(geometry.error());
        m_SharedShapes.emplace_back(std::move(key), geometry->handle);
        return geometry->handle;
    }

    std::expected<Asset::MeshHandle, SceneResourceError>
    SceneRenderResources::PublishGeometry(const GeometryTemplates::Request& request)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(
                SceneResourceError{"geometry publication", SceneResourceCode::PublicationBusy});
        auto key = GeometryTemplates::KeyFor(request);
        if (!key)
            return std::unexpected(SceneResourceError{"geometry parameters", key.error()});
        for (std::size_t i = 0; i < m_SharedTemplateCount; ++i)
            if (m_SharedTemplates[i].first == *key)
            {
                const auto handle = m_SharedTemplates[i].second;
                if (auto source = GeometrySource(handle); !source)
                    return std::unexpected(source.error());
                return handle;
            }
        if (m_SharedTemplateCount == m_SharedTemplates.size())
            return std::unexpected(
                SceneResourceError{"geometry cache", SceneResourceCode::TemplateCapacity});
        auto mesh = GeometryTemplates::Generate(request);
        if (!mesh)
            return std::unexpected(SceneResourceError{"geometry generation", mesh.error()});
        auto geometry = PublishAuthored(std::move(*mesh), SourceKind::Template);
        if (!geometry) return std::unexpected(geometry.error());
        // Fixed storage: the sole cache mutation follows successful publication
        // and cannot allocate or fail. All preceding failures preserve old entries.
        m_SharedTemplates[m_SharedTemplateCount++] = {*key, geometry->handle};
        return geometry->handle;
    }

    namespace
    {
        constexpr const char* MaterialTextureNames[]{"albedoMap", "normalMap", "metallicMap", "roughnessMap", "aoMap"};
        auto MaterialFailure(const char* operation)
        { return std::unexpected(SceneResourceError{operation, SceneResourceCode::InvalidMaterial}); }
        auto AuthoredParameters(const MaterialAuthoringDesc& d)
        {
            using P = MaterialParameterDecl;
            using T = MaterialParameterType;
            return std::array<P, 14>{{
                {"baseColor", T::Float4, d.baseColor}, {"metallicFactor", T::Float, d.metallic},
                {"roughnessScale", T::Float, d.roughness}, {"normalStrength", T::Float, d.normalStrength},
                {"aoStrength", T::Float, d.aoStrength}, {"emissive", T::Float3, d.emissive},
                {"metalness", T::Float3, d.dielectricReflectance}, {"materialOpacity", T::Float, d.opacity},
                {"materialCutoff", T::Float, d.alphaCutoff}, {"u_tiling", T::Float2, d.uvTiling},
                {"mappingMode", T::Integer, static_cast<std::int32_t>(d.mapping)},
                {"projectionScale", T::Float, d.projectionScale.value},
                {"projectionSharpness", T::Float, d.blendSharpness.value},
                {"normalConvention", T::Integer, static_cast<std::int32_t>(d.normalConvention)}}};
        }
        template<class T> bool ReadParameter(const MaterialInstance& instance, std::string_view name, T& value)
        {
            const auto parameters = instance.Declaration()->Parameters();
            for (std::size_t i = 0; i < parameters.size(); ++i)
                if (parameters[i].declaration.name == name)
                    if (auto* found = std::get_if<T>(&instance.Values()[i])) { value = *found; return true; }
            return false;
        }
    }

    std::expected<void, SceneResourceError>
    SceneRenderResources::ValidateMaterial(const MaterialAuthoringDesc& d) const
    {
        const auto finiteRange = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
        if (d.kind < MaterialKind::Standard || d.kind > MaterialKind::Debug ||
            d.mapping < TextureMappingMode::UV || d.mapping > TextureMappingMode::Triplanar ||
            d.normalConvention < NormalMapConvention::NegativeY || d.normalConvention > NormalMapConvention::PositiveY ||
            d.blend < TransparentBlend::StraightAlpha || d.blend > TransparentBlend::PremultipliedAlpha)
            return MaterialFailure("material enum value");
        if (!finiteRange(d.metallic, 0, 1) || !finiteRange(d.roughness, 0, 1) ||
            !finiteRange(d.normalStrength, 0, 2) || !finiteRange(d.aoStrength, 0, 1) ||
            !finiteRange(d.opacity, 0, 1) || !finiteRange(d.alphaCutoff, 0, 1) ||
            !finiteRange(d.projectionScale.value, .001f, 1024) || !finiteRange(d.blendSharpness.value, 1, 8))
            return MaterialFailure("material scalar range");
        for (float value : d.baseColor) if (!finiteRange(value, 0, 1)) return MaterialFailure("base color range");
        for (float value : d.emissive) if (!finiteRange(value, 0, 64)) return MaterialFailure("emissive range");
        for (float value : d.dielectricReflectance) if (!finiteRange(value, 0, 1)) return MaterialFailure("reflectance range");
        for (float value : d.uvTiling) if (!finiteRange(value, .001f, 1024)) return MaterialFailure("UV tiling range");
        if (d.kind != MaterialKind::Transparent && d.blend != TransparentBlend::StraightAlpha)
            return MaterialFailure("blend policy applies only to Transparent");
        if (d.kind == MaterialKind::Unlit || d.kind == MaterialKind::Debug)
        {
            const MaterialAuthoringDesc defaults;
            if (d.metallic != defaults.metallic || d.roughness != defaults.roughness ||
                d.normalStrength != defaults.normalStrength || d.aoStrength != defaults.aoStrength ||
                d.emissive != defaults.emissive || d.dielectricReflectance != defaults.dielectricReflectance ||
                d.normalConvention != defaults.normalConvention)
                return MaterialFailure("lit parameters on Unlit or Debug");
            for (std::size_t i = d.kind == MaterialKind::Debug ? 0 : 1; i < d.textures.size(); ++i)
                if (d.textures[i]) return MaterialFailure("texture role on Unlit or Debug");
        }
        if (d.kind == MaterialKind::Debug && (d.mapping != TextureMappingMode::UV || d.uvTiling != std::array<float, 2>{1, 1}))
            return MaterialFailure("Debug has no texture mapping");
        auto access = m_Publication.BeginFrame();
        for (std::size_t i = 0; i < d.textures.size(); ++i)
        {
            if (!d.textures[i]) continue;
            const auto value = *d.textures[i];
            auto texture = m_Bindings->textures.Acquire(access, value.texture);
            if (!texture) return std::unexpected(SceneResourceError{"material texture identity", texture.error()});
            auto sampler = m_Bindings->samplers.Acquire(access, value.sampler);
            if (!sampler) return std::unexpected(SceneResourceError{"material sampler identity", sampler.error()});
            const auto& description = (*texture)->Description();
            if (description.kind != Asset::TextureKind::Image2D || description.usage != Asset::TextureUsage::Sampled ||
                description.format == Asset::TextureFormat::Depth32Float)
                return MaterialFailure("surface texture requires a sampled color/data Image2D");
            if (i && description.colorSpace != Asset::TextureColorSpace::Linear)
                return MaterialFailure("normal and scalar textures require linear data");
        }
        return {};
    }

    std::expected<MaterialInstance, SceneResourceError>
    SceneRenderResources::PrepareMaterial(const MaterialAuthoringDesc& desc)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(SceneResourceError{"material preparation", SceneResourceCode::PublicationBusy});
        if (auto valid = ValidateMaterial(desc); !valid) return std::unexpected(valid.error());
        Asset::MaterialTemplateHandle declaration;
        for (std::size_t i = 0; i < m_MaterialWork.templates; ++i)
        {
            const auto& item = m_BuiltinMaterials[i];
            if (item.kind == desc.kind && item.doubleSided == desc.doubleSided && item.blend == desc.blend)
                declaration = item.declaration;
        }
        if (!declaration)
        {
            if (m_MaterialWork.templates == m_BuiltinMaterials.size())
                return std::unexpected(SceneResourceError{"built-in material templates", SceneResourceCode::TemplateCapacity});
            const auto family = desc.kind == MaterialKind::Unlit ? 1u : desc.kind == MaterialKind::Debug ? 2u : 0u;
            const auto kind = family == 1 ? SceneMaterialKind::Unlit : family == 2 ? SceneMaterialKind::Helper : SceneMaterialKind::Lit;
            const auto parameters = AuthoredParameters(MaterialAuthoringDesc{});
            auto& program = m_BuiltinPrograms[family];
            if (!program)
            {
                ++m_MaterialWork.programCreations;
                auto description = DescribeSceneProgram(kind, parameters, true);
                if (!description) return std::unexpected(description.error());
                auto published = CacheProgram(*description);
                if (!published) return std::unexpected(published.error());
                program = *published;
            }
            SceneMaterialDesc pipelineDesc;
            pipelineDesc.kind = kind;
            pipelineDesc.doubleSided = desc.doubleSided;
            pipelineDesc.alpha = desc.kind == MaterialKind::Masked ? AlphaMode::Masked :
                desc.kind == MaterialKind::Transparent ? AlphaMode::Transparent : AlphaMode::Opaque;
            pipelineDesc.transparentBlend = desc.blend;
            auto state = PipelineState::Create(DescribePipeline(pipelineDesc, program));
            if (!state) return std::unexpected(SceneResourceError{"built-in pipeline", state.error()});
            Asset::PipelineHandle pipelineHandle;
            {
                auto publication = m_Publication.BeginPublication();
                auto published = m_Pipelines.Create(publication, std::move(*state));
                if (!published) return std::unexpected(SceneResourceError{"built-in pipeline", published.error()});
                pipelineHandle = *published;
            }
            struct PipelineRollback
            {
                SceneRenderResources& owner; Asset::PipelineHandle handle; bool committed = false;
                ~PipelineRollback() { if (!committed) { auto p = owner.m_Publication.BeginPublication();
                    (void)owner.m_Pipelines.Destroy(p, handle); owner.m_Pipelines.Collect(p); } }
            } rollback{*this, pipelineHandle};
            PipelineView pipeline;
            {
                auto access = m_Publication.BeginFrame();
                auto found = m_Pipelines.Acquire(access, pipelineHandle);
                if (!found) return std::unexpected(SceneResourceError{"built-in pipeline lease", found.error()});
                pipeline = *found;
            }
            std::array<MaterialTextureSlotDecl, 5> slots;
            for (std::size_t i = 0; i < slots.size(); ++i) slots[i] = {MaterialTextureNames[i], false, {}};
            const bool depth = desc.kind == MaterialKind::Standard || desc.kind == MaterialKind::Masked;
            auto materialTemplate = MaterialTemplate::Create({pipeline, parameters, slots, depth, depth});
            if (!materialTemplate) return std::unexpected(SceneResourceError{"built-in template", materialTemplate.error()});
            // Reserve role metadata before publishing a template that refers to it.
            m_Roles.reserve(m_Roles.size() + 1);
            {
                auto publication = m_Publication.BeginPublication();
                auto published = m_Templates.Create(publication, std::move(*materialTemplate));
                if (!published) return std::unexpected(SceneResourceError{"built-in template", published.error()});
                declaration = *published;
            }
            m_Roles.push_back({pipelineHandle, kind, 1.f, 1.f, true});
            m_BuiltinMaterials[m_MaterialWork.templates++] = {desc.kind, desc.doubleSided, desc.blend, declaration};
            rollback.committed = true;
        }
        MaterialTemplateView view;
        {
            auto access = m_Publication.BeginFrame();
            auto found = m_Templates.Acquire(access, declaration);
            if (!found) return std::unexpected(SceneResourceError{"built-in template lease", found.error()});
            view = *found;
        }
        auto instance = MaterialInstance::Create(view);
        if (!instance) return std::unexpected(SceneResourceError{"built-in instance", instance.error()});
        for (const auto& parameter : AuthoredParameters(desc))
            if (auto set = instance->SetParameter(parameter.name, parameter.defaultValue); !set)
                return std::unexpected(SceneResourceError{"material parameter", set.error()});
        for (std::size_t i = 0; i < desc.textures.size(); ++i)
            if (desc.textures[i])
                if (auto set = instance->SetTexture(MaterialTextureNames[i], *desc.textures[i]); !set)
                    return std::unexpected(SceneResourceError{"material binding", set.error()});
        return std::move(*instance);
    }

    std::expected<MaterialHandle, SceneResourceError>
    SceneRenderResources::CreateMaterial(const MaterialAuthoringDesc& desc)
    {
        auto candidate = PrepareMaterial(desc);
        if (!candidate) return std::unexpected(candidate.error());
        auto publication = m_Publication.BeginPublication();
        auto result = m_Materials.Create(publication, std::move(*candidate));
        if (!result) return std::unexpected(SceneResourceError{"material creation", result.error()});
        ++m_MaterialWork.publications;
        return *result;
    }

    std::expected<MaterialAuthoringDesc, SceneResourceError>
    SceneRenderResources::DescribeMaterial(MaterialHandle handle) const
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(SceneResourceError{"material description", SceneResourceCode::PublicationBusy});
        auto access = m_Publication.BeginFrame();
        auto found = m_Materials.Acquire(access, handle);
        if (!found) return std::unexpected(SceneResourceError{"material lookup", found.error()});
        const auto& source = **found;
        const BuiltinMaterial* builtin = nullptr;
        for (std::size_t i = 0; i < m_MaterialWork.templates; ++i)
            if (m_BuiltinMaterials[i].declaration == source.Template()) builtin = &m_BuiltinMaterials[i];
        if (!builtin) return MaterialFailure("typed description requires a built-in material");
        MaterialAuthoringDesc d;
        d.kind = builtin->kind; d.doubleSided = builtin->doubleSided; d.blend = builtin->blend;
        std::int32_t mapping = 0, convention = 0;
        if (!ReadParameter(source, "baseColor", d.baseColor) || !ReadParameter(source, "metallicFactor", d.metallic) ||
            !ReadParameter(source, "roughnessScale", d.roughness) || !ReadParameter(source, "normalStrength", d.normalStrength) ||
            !ReadParameter(source, "aoStrength", d.aoStrength) || !ReadParameter(source, "emissive", d.emissive) ||
            !ReadParameter(source, "metalness", d.dielectricReflectance) || !ReadParameter(source, "materialOpacity", d.opacity) ||
            !ReadParameter(source, "materialCutoff", d.alphaCutoff) || !ReadParameter(source, "u_tiling", d.uvTiling) ||
            !ReadParameter(source, "mappingMode", mapping) || !ReadParameter(source, "normalConvention", convention) ||
            !ReadParameter(source, "projectionScale", d.projectionScale.value) || !ReadParameter(source, "projectionSharpness", d.blendSharpness.value))
            return MaterialFailure("built-in instance schema mismatch");
        d.mapping = static_cast<TextureMappingMode>(mapping);
        d.normalConvention = static_cast<NormalMapConvention>(convention);
        const auto slots = source.Declaration()->Textures();
        for (std::size_t i = 0; i < slots.size(); ++i)
            for (std::size_t j = 0; j < d.textures.size(); ++j)
                if (slots[i].declaration.name == MaterialTextureNames[j]) d.textures[j] = source.Textures()[i];
        return d;
    }

    std::expected<bool, SceneResourceError>
    SceneRenderResources::EditMaterial(MaterialHandle handle, const MaterialAuthoringDesc& desc)
    {
        auto previous = DescribeMaterial(handle);
        if (!previous) return std::unexpected(previous.error());
        if (auto valid = ValidateMaterial(desc); !valid) return std::unexpected(valid.error());
        if (*previous == desc) return false;
        auto candidate = PrepareMaterial(desc);
        if (!candidate) return std::unexpected(candidate.error());
        // Keep the existing authored revision counter for same-template edits.
        {
            auto access = m_Publication.BeginFrame();
            auto old = m_Materials.Acquire(access, handle);
            if (!old) return std::unexpected(SceneResourceError{"material edit lookup", old.error()});
            if ((*old)->Template() == candidate->Template())
            {
                auto revised = **old;
                for (const auto& p : AuthoredParameters(desc))
                    if (auto set = revised.SetParameter(p.name, p.defaultValue); !set)
                        return std::unexpected(SceneResourceError{"material edit parameter", set.error()});
                for (std::size_t i = 0; i < desc.textures.size(); ++i)
                {
                    auto set = desc.textures[i] ? revised.SetTexture(MaterialTextureNames[i], *desc.textures[i]) :
                        revised.ResetTexture(MaterialTextureNames[i]);
                    if (!set) return std::unexpected(SceneResourceError{"material edit binding", set.error()});
                }
                *candidate = std::move(revised);
            }
        }
        auto publication = m_Publication.BeginPublication();
        auto result = m_Materials.Replace(publication, handle, std::move(*candidate));
        if (!result) return std::unexpected(SceneResourceError{"material edit publication", result.error()});
        m_Materials.Collect(publication);
        ++m_MaterialWork.publications;
        return true;
    }

    std::expected<bool, SceneResourceError> SceneRenderResources::SetMaterialTexture(
        MaterialHandle handle, MaterialTextureSemantic semantic, std::optional<MaterialTextureValue> value)
    {
        if (semantic < MaterialTextureSemantic::BaseColor || semantic > MaterialTextureSemantic::AO)
            return MaterialFailure("material texture semantic");
        auto desc = DescribeMaterial(handle);
        if (!desc) return std::unexpected(desc.error());
        desc->textures[static_cast<std::size_t>(semantic)] = value;
        return EditMaterial(handle, *desc);
    }

    std::expected<MaterialHandle, SceneResourceError> SceneRenderResources::CloneMaterial(MaterialHandle handle)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(SceneResourceError{"material Clone", SceneResourceCode::PublicationBusy});
        std::optional<MaterialInstance> candidate;
        {
            auto access = m_Publication.BeginFrame();
            auto source = m_Materials.Acquire(access, handle);
            if (!source) return std::unexpected(SceneResourceError{"material Clone source", source.error()});
            // Validate source bindings without shader creation, texture upload or publication.
            auto resolved = PreparedMaterialBinding::Prepare(*source, access, *m_Bindings);
            if (!resolved) return MaterialFailure("material Clone source bindings");
            candidate = **source;
        }
        auto publication = m_Publication.BeginPublication();
        auto result = m_Materials.Create(publication, std::move(*candidate));
        if (!result) return std::unexpected(SceneResourceError{"material Clone publication", result.error()});
        ++m_MaterialWork.clones; ++m_MaterialWork.publications;
        return *result;
    }

    std::expected<Asset::ShaderProgramHandle, SceneResourceError>
    SceneRenderResources::CacheProgram(const Asset::ShaderDescription& description,
                                      Asset::ShaderVariantKey variant, const MaterialShaderDescription* material)
    {
        using namespace Asset;
        if (!m_Publication.CanPublish())
            return std::unexpected(SceneResourceError{"shader cache", SceneResourceCode::PublicationBusy});
        const auto failure = [](ShaderErrorCode code, std::string source, std::string log) {
            return std::unexpected(SceneResourceError{"shader interface", ShaderError{code, {}, std::move(source), std::move(log)}});
        };
        auto selected = description.Select(variant);
        if (!selected) return std::unexpected(SceneResourceError{"shader variant", selected.error()});
        for (std::size_t i = 0; i < m_ShaderWork.programs; ++i)
            if (m_ProgramCache[i].custom == (material != nullptr) && m_ProgramCache[i].identity == (*selected)->identity)
            {
                ++m_ShaderWork.cacheHits;
                return m_ProgramCache[i].handle;
            }
        if (m_ShaderWork.programs == m_ProgramCache.size())
            return failure(ShaderErrorCode::Capacity, {}, "Scene shader cache has 64 entries; no eviction or implicit recompilation");
        ++m_ShaderWork.cacheMisses;
        CachedProgram entry;
        entry.custom = material != nullptr; entry.identity = (*selected)->identity;
        std::vector<ShaderSource> stages;
        for (const auto& stage : (*selected)->stages) stages.push_back({stage.type, stage.source, stage.label});
        std::vector<std::string> packedSources;
        if (material)
        {
            // This temporary owner proves authored uniform types before the packed
            // adapter removes them. The two cold links are explicit and counted.
            auto authored = CreateShaderProgram(stages);
            if (!authored) return std::unexpected(SceneResourceError{"authored shader", authored.error()});
            auto reflected = authored->Reflect(description.Bindings());
            if (!reflected) return std::unexpected(SceneResourceError{"authored reflection", reflected.error()});
            entry.authored = std::move(*reflected);
            for (const auto& item : entry.authored)
            {
                if (!item.active || item.name.starts_with("gl_")) continue;
                if (item.kind == ShaderResourceKind::UniformBlock || item.kind == ShaderResourceKind::StorageBlock ||
                    item.kind == ShaderResourceKind::BlockMember || !item.semantic ||
                    std::none_of(description.Bindings().begin(), description.Bindings().end(),
                        [&](const auto& binding) { return binding.name == item.name && binding.semantic == item.semantic; }))
                    return failure(ShaderErrorCode::Validation, item.name, "Custom material exposes an undeclared or unsupported interface");
            }
            packedSources.reserve(stages.size());
            for (const auto& stage : stages)
            {
                ++m_ShaderWork.packingPasses;
                auto packed = RenderBackend::PackedStage(std::string(stage.source), false, material->Parameters(), stage.type);
                if (!packed) return std::unexpected(packed.error());
                packedSources.push_back(std::move(*packed));
            }
            for (std::size_t i = 0; i < stages.size(); ++i) stages[i].source = packedSources[i];
        }
        auto shader = CreateShaderProgram(stages);
        if (!shader) return std::unexpected(SceneResourceError{"packed shader", shader.error()});
        auto reflected = shader->Reflect();
        if (!reflected) return std::unexpected(SceneResourceError{"packed reflection", reflected.error()});
        entry.packed = std::move(*reflected);
        for (const auto& item : entry.packed)
        {
            if (item.kind == ShaderResourceKind::UniformBlock &&
                (item.name != "GEngineFrame" || item.blockBytes > sizeof(RenderBackend::PackedFrame)))
                return failure(ShaderErrorCode::BindingType, item.name, "Packed frame block does not match the renderer ABI");
            if (item.kind == ShaderResourceKind::StorageBlock && item.name != "GEngineMaterials" && item.name != "GEngineInstances")
                return failure(ShaderErrorCode::Validation, item.name, "Unsupported packed storage block");
            if (item.kind == ShaderResourceKind::BlockMember)
            {
                if (item.block == "GEngineMaterials" && (item.type != ShaderValueType::UInt4 || item.byteOffset != 0 || item.arrayStride != 16))
                    return failure(ShaderErrorCode::BindingType, item.name, "Packed material word layout differs from shared material storage");
                if (item.block == "GEngineInstances" && item.arrayStride != sizeof(RenderBackend::PackedInstance))
                    return failure(ShaderErrorCode::BindingType, item.name, "Packed instance stride differs from renderer storage");
                const std::pair<std::string_view, std::size_t> matrices[]{
                    {"geView", offsetof(RenderBackend::PackedFrame, view)},
                    {"geProjection", offsetof(RenderBackend::PackedFrame, projection)},
                    {"geSkyView", offsetof(RenderBackend::PackedFrame, skyView)}};
                for (const auto& [name, offset] : matrices)
                    if (item.block == "GEngineFrame" && (item.name == name || item.name.ends_with(std::string(".") + std::string(name))) &&
                        (item.type != ShaderValueType::Matrix4 || item.byteOffset != offset || item.matrixStride != 16))
                        return failure(ShaderErrorCode::BindingType, item.name, "Packed frame matrix layout differs from renderer storage");
            }
            if (material && item.kind == ShaderResourceKind::Uniform)
            {
                const auto texture = std::find_if(material->Textures().begin(), material->Textures().end(),
                    [&](const auto& slot) { return slot.name == item.name; });
                const bool control = item.name == "u_model" || item.name == "geInstanced" || item.name == "geInstanceBase" || item.name == "geMaterialOffset";
                if (!control && texture == material->Textures().end())
                    return failure(ShaderErrorCode::Validation, item.name, "Material uniform was not converted to packed storage; use separate scalar/vector/matrix declarations");
                if (texture != material->Textures().end() && (item.type != ShaderValueType::Sampler2D || item.elements != 1))
                    return failure(ShaderErrorCode::BindingType, item.name, "Packed texture semantic differs from schema");
            }
        }
        // Allocate the cache's CPU data before publishing. Fixed cache storage and
        // noexcept moves cannot strand a newly published program on allocation failure.
        auto publication = m_Publication.BeginPublication();
        auto handle = m_Programs.Create(publication, std::move(*shader));
        if (!handle) return std::unexpected(SceneResourceError{"cached program publication", handle.error()});
        entry.handle = *handle;
        m_ProgramCache[m_ShaderWork.programs++] = std::move(entry);
        return *handle;
    }

    std::expected<std::span<const Asset::ShaderReflection>, SceneResourceError>
    SceneRenderResources::ShaderInterface(const MaterialShaderDescription& description, Asset::ShaderVariantKey variant) const
    {
        auto selected = description.Program().Select(variant);
        if (!selected) return std::unexpected(SceneResourceError{"shader variant", selected.error()});
        for (std::size_t i = 0; i < m_ShaderWork.programs; ++i)
            if (m_ProgramCache[i].custom && m_ProgramCache[i].identity == (*selected)->identity)
                return std::span<const Asset::ShaderReflection>(m_ProgramCache[i].authored);
        return std::unexpected(SceneResourceError{"shader interface not created", SceneResourceCode::InvalidMaterial});
    }

    std::expected<MaterialHandle, SceneResourceError>
    SceneRenderResources::CreateMaterial(const MaterialShaderDescription& shader, Asset::ShaderVariantKey variant)
    {
        SceneMaterialDesc desc;
        desc.kind = SceneMaterialKind::Unlit; desc.parameters = shader.Parameters();
        return PublishMaterial(desc, &shader, variant);
    }

    std::expected<Asset::MaterialInstanceHandle, SceneResourceError>
    SceneRenderResources::PublishMaterial(const SceneMaterialDesc& desc)
    { return PublishMaterial(desc, nullptr, {}); }

    std::expected<MaterialHandle, SceneResourceError>
    SceneRenderResources::PublishMaterial(const SceneMaterialDesc& desc, const MaterialShaderDescription* custom,
                                        Asset::ShaderVariantKey variant)
    {
        if (!m_Publication.CanPublish())
            return std::unexpected(
                SceneResourceError{"material publication", SceneResourceCode::PublicationBusy});
        if (desc.kind < SceneMaterialKind::Lit || desc.kind > (custom ? SceneMaterialKind::Unlit : SceneMaterialKind::Sky) ||
            !std::isfinite(desc.lineWidth) || desc.lineWidth <= 0 || !std::isfinite(desc.opacity) ||
            desc.opacity < 0 || desc.opacity > 1 ||
            (desc.kind != SceneMaterialKind::Lit && desc.alpha != AlphaMode::Opaque))
            return std::unexpected(
                SceneResourceError{"material description", SceneResourceCode::InvalidMaterial});
        std::optional<Asset::ShaderDescription> standard;
        if (!custom)
        {
            auto description = DescribeSceneProgram(desc.kind, desc.parameters);
            if (!description) return std::unexpected(description.error());
            standard = std::move(*description);
        }
        auto program = CacheProgram(custom ? custom->Program() : *standard, variant, custom);
        if (!program) return std::unexpected(program.error());
        // Every failure (including standard container unwinding) retires only this
        // transaction's versions. Dependent local leases end before rollback runs.
        struct Rollback
        {
            SceneRenderResources& owner;
            Asset::PipelineHandle pipeline;
            Asset::MaterialTemplateHandle declaration;
            Asset::MaterialInstanceHandle material;
            bool committed = false;
            ~Rollback()
            {
                if (committed)
                    return;
                auto access = owner.m_Publication.BeginPublication();
                if (material)
                    (void)owner.m_Materials.Destroy(access, material);
                owner.m_Materials.Collect(access);
                if (declaration)
                    (void)owner.m_Templates.Destroy(access, declaration);
                owner.m_Templates.Collect(access);
                if (pipeline)
                    (void)owner.m_Pipelines.Destroy(access, pipeline);
                owner.m_Pipelines.Collect(access);
            }
        } rollback{*this};
        {
            auto access = m_Publication.BeginPublication();
            const auto pipeline = DescribePipeline(desc, *program);
            auto state = PipelineState::Create(pipeline);
            if (!state)
                return std::unexpected(SceneResourceError{"pipeline", state.error()});
            auto handle = m_Pipelines.Create(access, std::move(*state));
            if (!handle)
                return std::unexpected(SceneResourceError{"pipeline publication", handle.error()});
            rollback.pipeline = *handle;
        }
        PipelineView pipeline;
        {
            auto access = m_Publication.BeginFrame();
            auto view = m_Pipelines.Acquire(access, rollback.pipeline);
            if (!view)
                return std::unexpected(SceneResourceError{"pipeline resolution", view.error()});
            pipeline = std::move(*view);
        }
        std::vector<MaterialTextureSlotDecl> textures;
        if (custom) textures.assign(custom->Textures().begin(), custom->Textures().end());
        else for (const auto& texture : desc.textures)
                textures.push_back({std::string(texture.name), true, texture.value});
        const bool shadow =
            desc.kind == SceneMaterialKind::Lit && desc.alpha != AlphaMode::Transparent;
        auto declaration =
            MaterialTemplate::Create({pipeline, desc.parameters, textures, shadow, shadow});
        if (!declaration)
            return std::unexpected(SceneResourceError{"material template", declaration.error()});
        if (auto valid = declaration->ValidateBindings({}); !valid)
            return std::unexpected(SceneResourceError{"material texture schema", valid.error()});
        {
            auto access = m_Publication.BeginPublication();
            auto handle = m_Templates.Create(access, std::move(*declaration));
            if (!handle)
                return std::unexpected(SceneResourceError{"template publication", handle.error()});
            rollback.declaration = *handle;
        }
        MaterialTemplateView view;
        {
            auto access = m_Publication.BeginFrame();
            auto acquired = m_Templates.Acquire(access, rollback.declaration);
            if (!acquired)
                return std::unexpected(SceneResourceError{"template resolution", acquired.error()});
            view = std::move(*acquired);
        }
        auto instance = MaterialInstance::Create(view);
        if (!instance)
            return std::unexpected(SceneResourceError{"material instance", instance.error()});
        // No allocation after instance publication can leave it without role metadata.
        m_Roles.reserve(m_Roles.size() + 1);
        {
            auto access = m_Publication.BeginPublication();
            auto handle = m_Materials.Create(access, std::move(*instance));
            if (!handle)
                return std::unexpected(SceneResourceError{"material publication", handle.error()});
            rollback.material = *handle;
        }
        m_Roles.push_back({rollback.pipeline, desc.kind, desc.lineWidth, desc.opacity});
        rollback.committed = true;
        return rollback.material;
    }
}
