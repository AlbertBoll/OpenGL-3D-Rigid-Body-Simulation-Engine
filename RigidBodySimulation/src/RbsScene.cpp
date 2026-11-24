#include "RbsEditorState.h"
#include "RigidBodySimulation.h"
#include "RbsEditorState.h"
#include "Core/InitializationProgress.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Geometry/Geometry.h"
#include "Scene/_Scene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>

#include "Managers/AssetsManager.h"
#include "Managers/ShapeManager.h"
#include "Physics/PhysicsWorld.h"
#include <glm/gtx/quaternion.hpp>
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
#include "../tests/RestingContactDiagnosis.h"
#endif
#include "../tests/ColliderReplacementReplay.h"
#include "../tests/OrdinaryMeshHighlightFixture.h"
#if defined(GENGINE_PHASE26_QUALIFICATION) || defined(GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS)
#include "../tests/DistanceJointChecks.h"
#endif
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
#include "../tests/SandboxChecks.h"
#endif
#ifdef GENGINE_PHASE28_QUALIFICATION
#include <cstdlib>
namespace Rbs { bool Phase28AngularOwnsActions(); }
#endif

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;

namespace
{
    // One rust tile spans one metre on the reference body. Keep UVs on the mesh
    // so each bone's ordinary material UV controls remain independent multipliers.
    constexpr float RagdollRustTileMeters = 1.f;

    class RagdollBoxGeometry final : public Geometry
    {
    public:
        explicit RagdollBoxGeometry(const Vec3f& referenceSize)
        {
            // Use the same outward tangent frames and winding as the generated unit cube.
            constexpr Vec3f x{1,0,0}, y{0,1,0}, z{0,0,1};
            const std::array<std::pair<Vec3f,Vec3f>,6> faces{{
                {-z,y},{z,y},{x,-z},{x,z},{x,y},{-x,y}}};
            constexpr std::array<Vec2f,4> corners{{{0,0},{1,0},{0,1},{1,1}}};
            std::vector<Vec3f> positions, normals;
            std::vector<Vec2f> uv;
            std::vector<unsigned int> indices;
            for(const auto& [u,v]:faces)
            {
                const Vec3f n=glm::cross(u,v);
                const Vec2f repeats{std::abs(glm::dot(u,referenceSize))/RagdollRustTileMeters,
                                    std::abs(glm::dot(v,referenceSize))/RagdollRustTileMeters};
                const auto first=static_cast<unsigned int>(positions.size());
                for(const auto& corner:corners)
                {
                    positions.push_back(.5f*n+(corner.x-.5f)*u+(corner.y-.5f)*v);
                    normals.push_back(n);
                    uv.push_back(corner*repeats);
                }
                for(unsigned int i:{0u,1u,3u,0u,3u,2u})indices.push_back(first+i);
            }
            AddAttributes(positions,std::vector<Vec3f>(positions.size(),Vec3f(1.f)),uv,normals);
            AddIndices(indices);
            UnBindVAO();
        }
    };

    // Five immutable prototypes use the existing retained-source shape adapter.
    // Only UVs differ from the unit cube; entity scale still owns every dimension.
    class SandboxRoomGeometry final : public Geometry
    {
    public:
        SandboxRoomGeometry(const MeshAsset& cube, const Rbs::SandboxEnvironment::Box& box,
                            std::size_t positionOffset, std::size_t normalOffset)
        {
            const auto count = cube.VertexCount();
            std::vector<Vec3f> positions(count), normals(count), colors(count, Vec3f{1.f});
            std::vector<Vec2f> uv(count);
            std::vector<unsigned int> indices(cube.IndexCount());
            static_assert(sizeof(unsigned int) == sizeof(std::uint32_t));
            std::memcpy(indices.data(), cube.Indices().data(), cube.Indices().size());
            constexpr float floorRepeats = 16.f;
            constexpr float repeat = Rbs::SandboxEnvironment::Width / floorRepeats;
            constexpr float innerWidth = Rbs::SandboxEnvironment::Width -
                                         2.f * Rbs::SandboxEnvironment::WallThickness;
            constexpr float half = innerWidth * .5f;
            const auto phase = [repeat](float distance)
                { return distance - repeat * static_cast<int>(distance / repeat); };
            // Whole-repeat offsets are immaterial to Repeat sampling. Reduce
            // them before division so thin faces retain Float32 UV precision.
            const float westPhase = phase(3.f * innerWidth + half);
            const float eastPhase = phase(innerWidth + half);
            const float northPhase = phase(2.f * innerWidth + half);
            const float originPhase = phase(half);
            const Vec3f center{box.center[0], box.center[1], box.center[2]};
            const Vec3f size{box.size[0], box.size[1], box.size[2]};
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto* vertex = cube.Vertices().data() + i * cube.Layout().strideBytes;
                std::memcpy(&positions[i].x, vertex + positionOffset, 3 * sizeof(float));
                std::memcpy(&normals[i].x, vertex + normalOffset, 3 * sizeof(float));
                const auto p = center + positions[i] * size;
                const auto n = normals[i];
                // Keep each generated face's tangent/bitangent handedness. All
                // vertical faces share world-up V and a common perimeter U phase.
                // The closing corner preserves the native grout grid; its source
                // mottling has a wrap seam because 632 / 10 is not an integer.
                const float v = p.y - Rbs::SandboxEnvironment::FloorTop;
                if (n.x > .5f) uv[i] = {westPhase - p.z, v};
                else if (n.x < -.5f) uv[i] = {eastPhase + p.z, v};
                else if (n.y > .5f) uv[i] = {p.x + originPhase, originPhase - p.z};
                else if (n.y < -.5f) uv[i] = {p.x + originPhase, p.z + originPhase};
                else if (n.z > .5f) uv[i] = {p.x + originPhase, v};
                else uv[i] = {northPhase - p.x, v};
                uv[i] /= repeat;
            }
            AddAttributes(positions, colors, uv, normals);
            AddIndices(indices);
            UnBindVAO();
        }
    };

    Rbs::RbsScenePreset DebugValidationScene(Rbs::RbsScenePreset selected)
    {
#ifdef GENGINE_PHASE23_DEBUG_GALLERY
        return Rbs::RbsScenePreset::GeometryGallery;
#else
        return selected;
#endif
    }

    std::expected<MaterialHandle, SceneResourceError>
    CreateSurfaceMaterial(SceneRenderResources& resources,
                          const std::array<Asset::Texture*, 5>& images, MaterialAuthoringDesc desc)
    {
        for (std::size_t i = 0; i < images.size(); ++i)
        {
            auto sampled = AssetsManager::SampleTexture(images[i]->View());
            if (!sampled)
                return std::visit(
                    [](const auto& cause) -> std::expected<MaterialHandle, SceneResourceError>
                    {
                        return std::unexpected(SceneResourceError{"material sampling", cause});
                    },
                    sampled.error().cause);
            desc.textures[i] =
                MaterialTextureValue{sampled->TextureIdentity(), sampled->SamplerIdentity()};
        }
        return resources.CreateMaterial(desc);
    }

    std::expected<Asset::MaterialInstanceHandle, SceneResourceError>
    PublishTexturedMaterial(SceneRenderResources& resources, SceneMaterialKind kind,
                            std::initializer_list<Asset::Texture*> images,
                            std::span<const MaterialParameterDecl> parameters,
                            bool doubleSided = false, float width = 1.f)
    {
        std::vector<MaterialTextureAssignment> bindings;
        for (auto* texture : images)
        {
            auto sampled = AssetsManager::SampleTexture(texture->View());
            if (!sampled)
                return std::visit(
                    [](const auto& cause)
                        -> std::expected<Asset::MaterialInstanceHandle, SceneResourceError>
                    {
                        return std::unexpected(SceneResourceError{"material sampling", cause});
                    },
                    sampled.error().cause);
            bindings.push_back({texture->GetUniformName(),
                                {sampled->TextureIdentity(), sampled->SamplerIdentity()}});
        }
        return resources.PublishMaterial({kind, parameters, bindings, doubleSided, width});
    }
}

void RigidBodySimulationApp::FocusMaterialCamera(Camera::_EditorCamera& camera,
                                                 const Math::Vec3f& focal, float pitch, float yaw,
                                                 float distance)
{
    camera.m_FocalPoint = focal;
    camera.m_Pitch = pitch;
    camera.m_Yaw = yaw;
    camera.SetDistance(distance);
    camera.m_PositionDelta = {};
    camera.m_YawDelta = camera.m_PitchDelta = 0;
    camera.m_Position = focal - camera.GetForwardDirection() * distance;
    camera.UpdateView();
}

void RigidBodySimulationApp::SetMaterialComparison(int comparison)
{
    const auto authored = [this](const auto& result)
    {
        if (!result)
        {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Scene Transform",
                         std::to_string(static_cast<unsigned>(result.error().code)),
                         "authoritative Transform mutation", "",
                         "RBS authoring mutation rejected before commit"});
            return false;
        }
        if (!result->notification)
            ReportSubscriptionError(result->notification.error());
        return true;
    };
    m_MaterialGallery.comparison = comparison;
    for (std::size_t i = 0; i < m_MaterialGallery.uv.size(); ++i)
    {
        m_MaterialGallery.uv[i].AddOrReplaceComponent<Component::VisibilityComponent>(
            Component::VisibilityComponent{comparison != 2});
        m_MaterialGallery.triplanar[i].AddOrReplaceComponent<Component::VisibilityComponent>(
            Component::VisibilityComponent{comparison != 1});
        if (!authored(m_ActiveScene->SetLocalTranslation(
                m_MaterialGallery.triplanar[i],
                {-10.f + 4.f * static_cast<float>(i), 2.f, comparison == 0 ? -24.f : -20.f})))
            return;
    }
}

ApplicationInitializationResult
RigidBodySimulationApp::SceneFailure(const SceneResourceError& error)
{
    Log::GetCoreLogger()->error("{}", DescribeSceneResourceError(error));
    return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "scene resources",
                                         DescribeSceneResourceError(error)});
}

ApplicationInitializationResult
RigidBodySimulationApp::AssignRenderable(_Entity entity, const MeshRendererComponent& value)
{
    auto assigned = m_SceneResources->AssignRenderable(entity, value);
    if (!assigned)
        return std::unexpected(
            PlatformError{PlatformErrorCode::Initialization, "scene assignment",
                          std::format("Renderable assignment failed: {}",
                                      DescribeSceneResourceError(assigned.error()))});
    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateRenderable(_Entity entity, Asset::MeshHandle mesh,
                                         Asset::MaterialInstanceHandle material,
                                         std::string_view physicsShape)
{
    if (auto assigned = AssignRenderable(entity, {mesh, material}); !assigned)
        return assigned;
    // Kept for source compatibility with the existing scene helpers. Fixtures now
    // author their own semantic dimensions/points; visuals need no CPU Geometry owner.
    (void)physicsShape;
    return {};
}

std::expected<Asset::Texture*, Asset::TextureError>
RigidBodySimulationApp::LoadSceneTexture(const std::string& path, const std::string& uniform,
                                         const std::string& extension, const TextureDesc& desc)
{
    InitializationProgress::Pulse();
    auto texture = AssetsManager::GetTextureOrFallback(path, uniform, extension, desc);
    if (!texture)
    {
        Log::GetCoreLogger()->error("Texture {}: {}", texture.error().source,
                                    texture.error().message);
    }
    return texture;
}

ApplicationInitializationResult RigidBodySimulationApp::InitializeSceneRoot()
{
    m_FarPlane = 100;
    m_EditorScene = CreateRefPtr<_Scene>();
    m_ActiveScene = m_EditorScene;
    if(auto configured=m_ActiveScene->ConfigurePhysicsBackend(m_Backend);!configured)
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization,"Physics backend","Requested backend unavailable"});
    Log::GetCoreLogger()->info("Physics backend: {}. {}",PhysicsBackendName(m_Backend),PhysicsBackendModelNote(m_Backend));
    m_EditorCamera_ = _EditorCamera(45.0f, 1280.f, 720.f, 0.1f, 1000.f);
    float cameraFarClip = m_EditorCamera_.GetFarClip();
    m_ShadowCascadeLevels = {cameraFarClip / 50.f, cameraFarClip / 25.f, cameraFarClip / 10.f,
                             cameraFarClip / 2.f, cameraFarClip};
    if (SandboxEnabled())
        m_ShadowCascadeLevels[3] = 320.f;
    auto view = m_ActiveScene->CreateEditorViewIdentity();
    if (!view)
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "Editor view",
                                             "Transient view identity allocation failed"});
    m_EditorViewIdentity = *view;
    for (std::size_t slot = 0; slot != m_SceneCameras.size(); ++slot)
        if (auto camera = CreateSceneCamera(slot); !camera) return camera;
    auto access = m_SceneResources->Publication().BeginFrame();
    if (auto active = m_ActiveScene->SetActiveSceneCamera(m_SceneCameras[0],
            m_SceneResources->ForFrame(access)); !active)
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "Scene camera",
                                             "Initial active camera validation failed"});
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::CreateSceneCamera(std::size_t slot)
{
    auto created = CreatePresetEntity(1,slot == 0 ? "Scene Camera A" : "Scene Camera B");
    if (!created) return std::unexpected(created.error());
    auto entity = *created;
    auto& camera = entity.AddComponent<RenderCameraComponent>();
    camera.projection = slot == 0 ? CameraProjection::Perspective : CameraProjection::Orthographic;
    camera.verticalFovRadians = glm::radians(45.f);
    camera.orthographicHeight = 20.f;
    camera.nearPlane = .1f;
    camera.farPlane = 1000.f;
    auto pose = m_ActiveScene->SetLocalPose(entity, m_EditorCamera_.GetPosition(),
                                          m_EditorCamera_.GetOrientation());
    auto id = m_ActiveScene->RenderData().Identify(entity);
    if (!pose || !id)
    {
        (void)m_ActiveScene->DestroyEntity(entity);
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "Scene camera",
                                             "Authored camera pose or identity rejected"});
    }
    if (!pose->notification) ReportSubscriptionError(pose->notification.error());
    m_SceneCameras[slot] = *id;
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::PublishSceneMeshes(Rbs::SceneAssets& assets)
{
    auto sphereMeshResult = m_SceneResources->PublishShape("Sphere");
    if (!sphereMeshResult)
        return SceneFailure(sphereMeshResult.error());
    assets.smoothSphereGeo = *sphereMeshResult;
    auto diamondMeshResult = m_SceneResources->PublishShape("Diamond");
    if (!diamondMeshResult)
        return SceneFailure(diamondMeshResult.error());
    assets.DiamondGeo = *diamondMeshResult;
    auto boxMeshResult = m_SceneResources->PublishGeometry(GeometryTemplates::Cube{});
    if (!boxMeshResult)
        return SceneFailure(boxMeshResult.error());
    assets.boxMesh = *boxMeshResult;
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::LoadSceneIcons()
{
    if(SandboxEnabled())
    {
        constexpr int sizes[]{20,25,30,40};
        for(std::size_t i=0;i<m_ViewportIconAtlases.size();++i)
        {
            const auto name="Editor/Icons/viewport-"+std::to_string(sizes[i]);
            if(!RuntimeAssets::TryFile("Images/"+name+".png"))
                return std::unexpected(Asset::TextureError{Asset::TextureErrorCode::InvalidDescription, name, "Missing Viewport icon atlas"});
            auto icon=LoadSceneTexture(name);
            if(!icon)return std::unexpected(icon.error());
            m_ViewportIconAtlases[i]=*icon;

        }
        for(int i=0;i<7;++i) {
            constexpr int billboardSizes[]{44,55,66,88,110,132,176};
            const auto billboardName="Editor/Icons/viewport-"+std::to_string(billboardSizes[i]);
            if(!RuntimeAssets::TryFile("Images/"+billboardName+".png"))
                return std::unexpected(Asset::TextureError{Asset::TextureErrorCode::InvalidDescription,billboardName,"Missing Camera/Light billboard atlas"});
            auto billboard=LoadSceneTexture(billboardName);
            if(!billboard)return std::unexpected(billboard.error());
            m_ComponentIconAtlases[i]=*billboard;
        }
    }
    auto m_IconPlayResult = LoadSceneTexture("Icons/PlayButton");
    if (!m_IconPlayResult)
        return std::unexpected(m_IconPlayResult.error());
    auto* m_IconPlay = *m_IconPlayResult;
    auto m_IconPauseResult = LoadSceneTexture("Icons/PauseButton");
    if (!m_IconPauseResult)
        return std::unexpected(m_IconPauseResult.error());
    auto* m_IconPause = *m_IconPauseResult;
    auto m_IconStepResult = LoadSceneTexture("Icons/StepButton");
    if (!m_IconStepResult)
        return std::unexpected(m_IconStepResult.error());
    auto* m_IconStep = *m_IconStepResult;
    auto m_IconSimulateResult = LoadSceneTexture("Icons/SimulateButton");
    if (!m_IconSimulateResult)
        return std::unexpected(m_IconSimulateResult.error());
    auto* m_IconSimulate = *m_IconSimulateResult;
    auto m_IconStopResult = LoadSceneTexture("Icons/StopButton");
    if (!m_IconStopResult)
        return std::unexpected(m_IconStopResult.error());
    auto* m_IconStop = *m_IconStopResult;

    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::LoadSurfaceTextures(Rbs::SceneAssets& assets)
{
    // PBR vectors and scalar data must not use the color texture sRGB transfer.
    TextureDesc dataMapDesc;
    dataMapDesc.colorSpace = TextureColorSpace::Linear;
    auto sphere_albedoResult =
        LoadSceneTexture("PBR/rustediron/rustediron2_basecolor", "albedoMap");
    if (!sphere_albedoResult)
        return std::unexpected(sphere_albedoResult.error());
    assets.sphere_albedo = *sphere_albedoResult;
    auto sphere_normalResult =
        LoadSceneTexture("PBR/rustediron/rustediron2_normal", "normalMap", ".png", dataMapDesc);
    if (!sphere_normalResult)
        return std::unexpected(sphere_normalResult.error());
    assets.sphere_normal = *sphere_normalResult;
    auto sphere_metallicResult =
        LoadSceneTexture("PBR/rustediron/rustediron2_metallic", "metallicMap", ".png", dataMapDesc);
    if (!sphere_metallicResult)
        return std::unexpected(sphere_metallicResult.error());
    assets.sphere_metallic = *sphere_metallicResult;
    auto sphere_roughnessResult = LoadSceneTexture("PBR/rustediron/rustediron2_roughness",
                                                   "roughnessMap", ".png", dataMapDesc);
    if (!sphere_roughnessResult)
        return std::unexpected(sphere_roughnessResult.error());
    assets.sphere_roughness = *sphere_roughnessResult;
    auto sphere_aoResult = LoadSceneTexture("PBR/subtle_black_granite/subtle-black-granite_ao",
                                            "aoMap", ".png", dataMapDesc);
    if (!sphere_aoResult)
        return std::unexpected(sphere_aoResult.error());
    assets.sphere_ao = *sphere_aoResult;

    auto floor_albedoResult =
        LoadSceneTexture("PBR/base_white_tile/base-white-tile_albedo", "albedoMap");
    if (!floor_albedoResult)
        return std::unexpected(floor_albedoResult.error());
    assets.floor_albedo = *floor_albedoResult;
    auto floor_normalResult = LoadSceneTexture("PBR/base_white_tile/base-white-tile_normal-dx",
                                               "normalMap", ".png", dataMapDesc);
    if (!floor_normalResult)
        return std::unexpected(floor_normalResult.error());
    assets.floor_normal = *floor_normalResult;
    auto floor_metallicResult = LoadSceneTexture("PBR/base_white_tile/base-white-tile_metallic",
                                                 "metallicMap", ".png", dataMapDesc);
    if (!floor_metallicResult)
        return std::unexpected(floor_metallicResult.error());
    assets.floor_metallic = *floor_metallicResult;
    auto floor_roughnessResult = LoadSceneTexture("PBR/base_white_tile/base-white-tile_roughness",
                                                  "roughnessMap", ".png", dataMapDesc);
    if (!floor_roughnessResult)
        return std::unexpected(floor_roughnessResult.error());
    assets.floor_roughness = *floor_roughnessResult;
    auto floor_aoResult =
        LoadSceneTexture("PBR/base_white_tile/base-white-tile_ao", "aoMap", ".png", dataMapDesc);
    if (!floor_aoResult)
        return std::unexpected(floor_aoResult.error());
    assets.floor_ao = *floor_aoResult;

    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateSceneMaterials(Rbs::SceneAssets& assets)
{
    MaterialAuthoringDesc sphereParameters;
    sphereParameters.roughness = .9f;
    sphereParameters.metallic = 1.f;
    sphereParameters.dielectricReflectance = {.8f, .8f, .8f};
    auto sphereMaterialResult =
        CreateSurfaceMaterial(*m_SceneResources,
                              {assets.sphere_albedo, assets.sphere_normal, assets.sphere_metallic,
                               assets.sphere_roughness, assets.sphere_ao},
                              sphereParameters);
    if (!sphereMaterialResult)
        return SceneFailure(sphereMaterialResult.error());
    assets.sphereMaterial = *sphereMaterialResult;
    // Polished floor: retain linear roughness data and author gloss explicitly.
    MaterialAuthoringDesc floorParameters;
    floorParameters.roughness = .3f;
    floorParameters.metallic = 1.f;
    floorParameters.dielectricReflectance = {.08f, .08f, .08f};
    floorParameters.uvTiling = {2, 2};
    auto floorMaterialResult =
        CreateSurfaceMaterial(*m_SceneResources,
                              {assets.floor_albedo, assets.floor_normal, assets.floor_metallic,
                               assets.floor_roughness, assets.floor_ao},
                              floorParameters);
    if (!floorMaterialResult)
        return SceneFailure(floorMaterialResult.error());
    assets.floorMaterial = *floorMaterialResult;
    auto wallParameters = floorParameters;
    wallParameters.uvTiling = {2, .2f};
    auto wallMaterialResult =
        CreateSurfaceMaterial(*m_SceneResources,
                              {assets.floor_albedo, assets.floor_normal, assets.floor_metallic,
                               assets.floor_roughness, assets.floor_ao},
                              wallParameters);
    if (!wallMaterialResult)
        return SceneFailure(wallMaterialResult.error());
    assets.wallMaterial = *wallMaterialResult;
    // Keep the bootstrap fallback until the asynchronous file request publishes.
    auto woodResult = AssetsManager::GetTextureOrFallback({}, "albedoMap");
    if (!woodResult)
        return std::unexpected(woodResult.error());
    auto boxParameters = floorParameters;
    boxParameters.roughness = 1.f;
    boxParameters.uvTiling = {1, 1};
    // The old box path retained the floor's PBR channels between draws. Make the
    // steady authored combination explicit so frame ordering cannot alter it.
    auto boxMaterialResult =
        CreateSurfaceMaterial(*m_SceneResources,
                              {*woodResult, assets.floor_normal, assets.floor_metallic,
                               assets.floor_roughness, assets.floor_ao},
                              boxParameters);
    if (!boxMaterialResult)
        return SceneFailure(boxMaterialResult.error());
    assets.boxMaterial = *boxMaterialResult;
    m_AsyncBoxMaterial = assets.boxMaterial;
    if (SandboxEnabled())
    {
        // The room owns its surface appearance. Keep the fixture materials above
        // independent, including the box's historical floor-channel combination.
        for (const auto material : {assets.floorMaterial, assets.wallMaterial})
        {
            auto surface = m_SceneResources->DescribeMaterial(material);
            if (!surface)
                return SceneFailure(surface.error());
            surface->baseColor = {1.f, 1.f, 1.f, 1.f};
            surface->metallic = 1.f;
            surface->roughness = 1.f;
            surface->normalStrength = 1.f;
            surface->aoStrength = 1.f;
            surface->normalConvention = NormalMapConvention::NegativeY;
            surface->dielectricReflectance = {.04f, .04f, .04f};
            // Preserve all five original tile maps. Factors of one retain their
            // authored variation; per-face mesh UVs provide ten world units per repeat.
            surface->uvTiling = {1.f, 1.f};
            if (auto edited = m_SceneResources->EditMaterial(material, *surface); !edited)
                return SceneFailure(edited.error());
        }
    }
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::CreateSceneLights(Rbs::SceneAssets& assets)
{
    auto createdSceneEntity1 = CreatePresetEntity(2,"ambient_light");
    if (!createdSceneEntity1)
        return std::unexpected(createdSceneEntity1.error());
    auto ambientLightEntity = *createdSceneEntity1;
    auto createdSceneEntity2 = CreatePresetEntity(3,"point_light");
    if (!createdSceneEntity2)
        return std::unexpected(createdSceneEntity2.error());
    m_PointLightEntity = *createdSceneEntity2;
    m_LightDirection = glm::normalize(Vec3f{20, 50, 20});
    m_LightPos = {0, 15, -10};
    RenderLightComponent direction;
    direction.color = {.7f, .7f, .7f};
    if (SandboxEnabled())
    {
        // D65 daylight white in the renderer's linear RGB working space.
        direction.color = {1.f, 1.f, 1.f};
        direction.intensity = 3.0f;
    }
    direction.castShadows = true;
    ambientLightEntity.AddOrReplaceComponent<RenderLightComponent>(direction);
    auto& lightTransform = ambientLightEntity.GetComponent<Transform3DComponent>();
    lightTransform.QuatRotation = glm::rotation(Vec3f{0, 0, -1}, -m_LightDirection);
    RenderLightComponent point;
    point.kind = RenderLightKind::Point;
    point.color = {.8f, .2f, .1f};
    point.range = m_FarPlane;
    point.castShadows = true;
    if (SandboxEnabled())
    {
        point.color = {1.f, 1.f, 1.f};
        point.intensity = 1.f;
        point.range = 200.f;
        point.castShadows = false;
        m_LightPos = {0, 45, 0};
    }
    m_PointLightEntity.AddOrReplaceComponent<RenderLightComponent>(point);
    m_PointLightEntity.AddOrReplaceComponent<Transform3DComponent>(m_LightPos);
    auto pointMesh = m_SceneResources->PublishShape("PointLightHelper");
    if (!pointMesh)
        return SceneFailure(pointMesh.error());
    auto pointMaterial =
        PublishTexturedMaterial(*m_SceneResources, SceneMaterialKind::PointLight, {}, {});
    if (!pointMaterial)
        return SceneFailure(pointMaterial.error());
    if (auto assigned = AssignRenderable(m_PointLightEntity,
                                         {*pointMesh, *pointMaterial, 0, false, false, true});
        !assigned)
        return assigned;
    assets.pointMaterial = *pointMaterial;
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::CreatePhysicsScene(Rbs::SceneAssets& assets)
{
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
    if (Rbs::ColliderReplay::Enabled() && DebugValidationScene(m_ActiveSelection.scene) != Rbs::RbsScenePreset::SphereBoxStack)
        return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Collider replay","Replay manifest requires SphereBoxStack"});
#endif
    RigidBody3DComponent rigidBodyComp;
    rigidBodyComp.Type = BodyType::Dynamic;

    SphereFixture3DComponent sphereFixtureComp;

    sphereFixtureComp.Radius = 1.f;
    sphereFixtureComp.Property.m_Elasticity = 0.5f;
    sphereFixtureComp.Property.m_Friction = 0.5f;
    sphereFixtureComp.Property.m_InvMass = 1.f;

    BoxFixture3DComponent boxFixtureComp;
    boxFixtureComp.Shape.halfExtents = Vec3f{.5f};
    boxFixtureComp.Property.m_InvMass = 1.f;
    boxFixtureComp.Property.m_Friction = 0.5f;
    boxFixtureComp.Property.m_Elasticity = 0.5f;

    switch (DebugValidationScene(m_ActiveSelection.scene))
    {
    case Rbs::RbsScenePreset::GeometryGallery:
        return {};
    case Rbs::RbsScenePreset::SphereDiamond:
        return CreateSphereDiamond(assets, rigidBodyComp, sphereFixtureComp);
    case Rbs::RbsScenePreset::SphereLattice:
        return CreateSphereLattice(assets, rigidBodyComp, sphereFixtureComp);
    case Rbs::RbsScenePreset::BoxStack:
        return CreateBoxStack(assets, rigidBodyComp, sphereFixtureComp, boxFixtureComp);
    case Rbs::RbsScenePreset::SphereBoxStack:
        return CreateSphereBoxStack(assets, rigidBodyComp, sphereFixtureComp, boxFixtureComp);
    }

    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::CreateEnvironmentHelpers()
{
    const MaterialParameterDecl helperParameters[]{
        {"u_baseColor", MaterialParameterType::Float4, std::array<float, 4>{1, 1, 1, 1}},
        {"u_useVertexColor", MaterialParameterType::Boolean, true}};
    auto gridMesh = m_SceneResources->PublishShape("GridHelper");
    if (!gridMesh)
        return SceneFailure(gridMesh.error());
    auto gridMaterial = PublishTexturedMaterial(*m_SceneResources, SceneMaterialKind::Helper, {},
                                                helperParameters, true);
    if (!gridMaterial)
        return SceneFailure(gridMaterial.error());
    auto createdSceneEntity9 = CreatePresetEntity(4,"grid");
    if (!createdSceneEntity9)
        return std::unexpected(createdSceneEntity9.error());
    m_GridEntity = *createdSceneEntity9;
    if (auto assigned =
            AssignRenderable(m_GridEntity, {*gridMesh, *gridMaterial, 0, false, false, true});
        !assigned)
        return assigned;
    m_GridEntity.AddOrReplaceComponent<VisibilityComponent>(VisibilityComponent{false});
    m_GridEntity.GetComponent<Transform3DComponent>().SetRotation({Math::Pi / 2.f, 0, 0});
    auto axisMesh = m_SceneResources->PublishShape("AxisHelper");
    if (!axisMesh)
        return SceneFailure(axisMesh.error());
    auto axisMaterial = PublishTexturedMaterial(*m_SceneResources, SceneMaterialKind::Helper, {},
                                                helperParameters, true, 3.f);
    if (!axisMaterial)
        return SceneFailure(axisMaterial.error());
    auto createdSceneEntity10 = CreatePresetEntity(5,"axis");
    if (!createdSceneEntity10)
        return std::unexpected(createdSceneEntity10.error());
    m_AxisEntity = *createdSceneEntity10;
    m_AxisEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{0, 1, 0});
    if (auto assigned =
            AssignRenderable(m_AxisEntity, {*axisMesh, *axisMaterial, 0, false, false, true});
        !assigned)
        return assigned;
    TextureDesc info;
    info.kind = TextureKind::Cube;
    info.colorSpace = TextureColorSpace::Linear;
    info.mips = TextureMipIntent::None;
    info.orientation = ImageOrientation::TopLeft;
    auto skyTexture =
        AssetsManager::GetTextureOrFallback("SkyBox/Day/", "u_skyBoxDay", ".png", info);
    if (!skyTexture)
        return std::unexpected(skyTexture.error());
    auto skyMesh = m_SceneResources->PublishShape("SkyBox");
    if (!skyMesh)
        return SceneFailure(skyMesh.error());
    auto skyMaterial =
        PublishTexturedMaterial(*m_SceneResources, SceneMaterialKind::Sky, {*skyTexture}, {}, true);
    if (!skyMaterial)
        return SceneFailure(skyMaterial.error());
    auto createdSceneEntity11 = CreatePresetEntity(6,"Environment_SkyBox");
    if (!createdSceneEntity11)
        return std::unexpected(createdSceneEntity11.error());
    m_SkyBoxEntity = *createdSceneEntity11;
    if (auto assigned =
            AssignRenderable(m_SkyBoxEntity, {*skyMesh, *skyMaterial, 0, false, false, false});
        !assigned)
        return assigned;
    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateGroundAndWalls(const Rbs::SceneAssets& assets,
    std::array<entt::entity,5>* sandboxEntities)
{
    if (SandboxEnabled())
    {
        // Both mesh and collider consume the same unit-box transform. End walls
        // stop at the side-wall inside faces, making solid, nonoverlapping corners.
        const auto room = Rbs::SandboxEnvironment::Boxes();
        auto cube = m_SceneResources->GeometrySource(assets.boxMesh);
        if (!cube) return SceneFailure(cube.error());
        const VertexAttribute* position = nullptr;
        const VertexAttribute* normal = nullptr;
        for (const auto& attribute : cube->source->Layout().attributes)
        {
            if (attribute.semantic == VertexSemantic::Position) position = &attribute;
            if (attribute.semantic == VertexSemantic::Normal) normal = &attribute;
        }
        if (cube->source->VertexCount() != 24 || cube->source->IndexCount() != 36 ||
            cube->source->IndexFormat() != MeshIndexFormat::UInt32 || !position || !normal ||
            position->scalar != VertexScalarFormat::Float32 || position->components != 3 ||
            normal->scalar != VertexScalarFormat::Float32 || normal->components != 3)
            return SceneFailure({"sandbox tile cube source", GeometryAuthoringCode::UnsupportedLayout});
        std::size_t roomIndex=0;
        for (const auto& box : room)
        {
            const auto material = box.floor ? assets.floorMaterial : assets.wallMaterial;
            const std::string shapeName = std::string("RbsSandboxTile-v2/") + box.name;
            auto shape = ShapeManager::_GetShape<SandboxRoomGeometry>(shapeName, *cube->source,
                box, position->offsetBytes, normal->offsetBytes);
            if (!shape)
                return std::visit([](const auto& error) -> ApplicationInitializationResult
                    { return std::unexpected(error); }, shape.error());
            auto mesh = m_SceneResources->PublishShape(shapeName);
            if (!mesh) return SceneFailure(mesh.error());
            auto created = CreatePresetEntity(7,box.name);
            if (!created) return std::unexpected(created.error());
            auto entity = *created;
            if(sandboxEntities)(*sandboxEntities)[roomIndex]=static_cast<entt::entity>(entity);
            ++roomIndex;
            const Vec3f center{box.center[0], box.center[1], box.center[2]};
            const Vec3f size{box.size[0], box.size[1], box.size[2]};
            entity.AddOrReplaceComponent<Transform3DComponent>(center, Vec3f{}, size);
            RigidBody3DComponent body;
            body.Type = BodyType::Static;
            entity.AddOrReplaceComponent<RigidBody3DComponent>(body);
            BoxFixture3DComponent fixture;
            fixture.Shape.halfExtents = Vec3f{.5f};
            fixture.Property.m_Position = center;
            fixture.Property.m_InvMass = 0.f;
            fixture.Property.m_Friction = .5f;
            fixture.Property.m_Elasticity = .5f;
            entity.AddOrReplaceComponent<BoxFixture3DComponent>(fixture);
            if (auto authored = CreateRenderable(entity, *mesh, material, "Box"); !authored)
                return authored;
        }
        return {};
    }

    RigidBody3DComponent rigidBodyComp;
    rigidBodyComp.Type = BodyType::Static;

    auto createdSceneEntity12 = Rbs::OrdinaryHighlightFixture::Create(*m_ActiveScene,"wood_plane","wood_plane");
    if (!createdSceneEntity12)
        return std::unexpected(createdSceneEntity12.error());
    _Entity planeEntity = *createdSceneEntity12;
    RegisterPresetEntity(planeEntity,25);
    const auto planeGeo = assets.boxMesh;
    BoxFixture3DComponent planeFixtureComp;
    planeFixtureComp.Shape.halfExtents = Vec3f{.5f};
    planeFixtureComp.Property.m_InvMass = 0.f;
    planeFixtureComp.Property.m_Friction = 0.5f;
    planeFixtureComp.Property.m_Elasticity = 0.5f;
    if (auto authored = CreateRenderable(planeEntity, planeGeo, assets.floorMaterial, "Box");
        !authored)
        return authored;
    planeEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
    planeEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{}, Vec3f{}, Vec3f{100, 1, 100});
    planeFixtureComp.Property.m_Position =
        planeEntity.GetComponent<Transform3DComponent>().Translation;
    planeFixtureComp.Property.m_Orientation =
        planeEntity.GetComponent<Transform3DComponent>().QuatRotation;
    planeEntity.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);

    auto createdSceneEntity13 = Rbs::OrdinaryHighlightFixture::Create(*m_ActiveScene,"wall_entity_1","wall_entity_1");
    if (!createdSceneEntity13)
        return std::unexpected(createdSceneEntity13.error());
    _Entity wallEntity_1 = *createdSceneEntity13;
    RegisterPresetEntity(wallEntity_1,26);
    if (auto authored = CreateRenderable(wallEntity_1, planeGeo, assets.wallMaterial, "Box");
        !authored)
        return authored;
    wallEntity_1.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
    wallEntity_1.AddOrReplaceComponent<Transform3DComponent>(
        Vec3f{-49.5f, 4.5f, 0.f}, Vec3f{0, glm::pi<float>() / 2.0f, 0}, Vec3f{100, 10, 1});
    planeFixtureComp.Property.m_Position =
        wallEntity_1.GetComponent<Transform3DComponent>().Translation;
    planeFixtureComp.Property.m_Orientation =
        wallEntity_1.GetComponent<Transform3DComponent>().QuatRotation;
    wallEntity_1.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);

    auto createdSceneEntity14 = Rbs::OrdinaryHighlightFixture::Create(*m_ActiveScene,"wall_entity_2","wall_entity_2");
    if (!createdSceneEntity14)
        return std::unexpected(createdSceneEntity14.error());
    _Entity wallEntity_2 = *createdSceneEntity14;
    RegisterPresetEntity(wallEntity_2,27);
    if (auto authored = CreateRenderable(wallEntity_2, planeGeo, assets.wallMaterial, "Box");
        !authored)
        return authored;
    wallEntity_2.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
    wallEntity_2.AddOrReplaceComponent<Transform3DComponent>(
        Vec3f{49.5f, 4.5f, 0.f}, Vec3f{0, glm::pi<float>() / 2.0f, 0}, Vec3f{100, 10, 1});
    planeFixtureComp.Property.m_Position =
        wallEntity_2.GetComponent<Transform3DComponent>().Translation;
    planeFixtureComp.Property.m_Orientation =
        wallEntity_2.GetComponent<Transform3DComponent>().QuatRotation;
    wallEntity_2.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);

    auto createdSceneEntity15 = Rbs::OrdinaryHighlightFixture::Create(*m_ActiveScene,"wall_entity_3","wall_entity_3");
    if (!createdSceneEntity15)
        return std::unexpected(createdSceneEntity15.error());
    _Entity wallEntity_3 = *createdSceneEntity15;
    RegisterPresetEntity(wallEntity_3,28);
    if (auto authored = CreateRenderable(wallEntity_3, planeGeo, assets.wallMaterial, "Box");
        !authored)
        return authored;
    wallEntity_3.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
    wallEntity_3.AddOrReplaceComponent<Transform3DComponent>(Vec3f{0.f, 4.5f, 49.5f}, Vec3f{},
                                                             Vec3f{100, 10, 1});
    planeFixtureComp.Property.m_Position =
        wallEntity_3.GetComponent<Transform3DComponent>().Translation;
    planeFixtureComp.Property.m_Orientation =
        wallEntity_3.GetComponent<Transform3DComponent>().QuatRotation;
    wallEntity_3.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);

    auto createdSceneEntity16 = Rbs::OrdinaryHighlightFixture::Create(*m_ActiveScene,"wall_entity_4","wall_entity_4");
    if (!createdSceneEntity16)
        return std::unexpected(createdSceneEntity16.error());
    _Entity wallEntity_4 = *createdSceneEntity16;
    RegisterPresetEntity(wallEntity_4,29);
    if (auto authored = CreateRenderable(wallEntity_4, planeGeo, assets.wallMaterial, "Box");
        !authored)
        return authored;
    wallEntity_4.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
    wallEntity_4.AddOrReplaceComponent<Transform3DComponent>(Vec3f{0.f, 4.5f, -49.5f}, Vec3f{},
                                                             Vec3f{100, 10, 1});
    planeFixtureComp.Property.m_Position =
        wallEntity_4.GetComponent<Transform3DComponent>().Translation;
    planeFixtureComp.Property.m_Orientation =
        wallEntity_4.GetComponent<Transform3DComponent>().QuatRotation;
    wallEntity_4.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);

    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateTemplateGallery(Rbs::SceneAssets& assets)
{
    assets.normalPhysicsBodies = m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>().size();
    assets.galleryFixtures = 0;
    if (DebugValidationScene(m_ActiveSelection.scene) == Rbs::RbsScenePreset::GeometryGallery)
    {
        // Template dimensions remain independent of Transform scale. The two
        // Capsule examples also carry explicit static colliders for inspection.
        struct TemplateFixture
        {
            const char* name;
            GeometryTemplates::Request request;
            Vec3f position;
        };
        const TemplateFixture fixtures[]{
            {"Template Plane", GeometryTemplates::Plane{3, 3}, {-8, 1, 0}},
            {"Template Quad", GeometryTemplates::Quad{2, 2}, {0, 3, -8}},
            {"Template Grid", GeometryTemplates::Grid{3, 3, 4, 4}, {8, 1, 0}}};
        for (const auto& fixture : fixtures)
        {
            auto mesh = m_SceneResources->PublishGeometry(fixture.request);
            if (!mesh)
                return SceneFailure(mesh.error());
            auto created = CreatePresetEntity(8,fixture.name);
            if (!created)
                return std::unexpected(created.error());
            created->AddOrReplaceComponent<Transform3DComponent>(fixture.position, Vec3f{},
                                                                 Vec3f{1});
            if (auto authored = CreateRenderable(*created, *mesh, assets.floorMaterial); !authored)
                return authored;
            ++assets.galleryFixtures;
        }

        struct CurvedFixture
        {
            const char* name;
            GeometryTemplates::Request request;
        };
        const CurvedFixture curved[]{{"Template Sphere", GeometryTemplates::Sphere{}},
                                     {"Template Cylinder", GeometryTemplates::Cylinder{}},
                                     {"Template Cone", GeometryTemplates::Cone{}},
                                     {"Template Capsule", GeometryTemplates::Capsule{}},
                                     {"Template Torus", GeometryTemplates::Torus{}},
                                     {"Template Diamond", GeometryTemplates::Diamond{}}};
        for (std::size_t family = 0; family < std::size(curved); ++family)
            for (int row = 0; row < 2; ++row)
            {
                auto request = curved[family].request;
                std::visit(
                    [row](auto& value)
                    {
                        value.Options.Shading = row == 0 ? GeometryTemplates::ShadingMode::Smooth
                                                         : GeometryTemplates::ShadingMode::Flat;
                        value.Options.Tangents = GeometryTemplates::TangentMode::Omit;
                    },
                    request);
                auto mesh = m_SceneResources->PublishGeometry(request);
                if (!mesh)
                    return SceneFailure(mesh.error());
                auto created = CreatePresetEntity(9,
                    std::format("{} {}", curved[family].name, row == 0 ? "Smooth" : "Flat"));
                if (!created)
                    return std::unexpected(created.error());
                created->AddOrReplaceComponent<Transform3DComponent>(
                    Vec3f{-12.f + 4.f * static_cast<float>(family), 3.f, row == 0 ? -10.f : -14.f},
                    Vec3f{}, Vec3f{1});
                if (auto authored = CreateRenderable(*created, *mesh, assets.sphereMaterial);
                    !authored)
                    return authored;
                if (const auto* capsule = std::get_if<GeometryTemplates::Capsule>(&request))
                {
#ifdef GENGINE_PHASE23_DEBUG_GALLERY
                    // The existing gallery supplies identity and oblique poses
                    // to the opt-in debug regression; normal authoring is intact.
                    if (row==1) created->Transform().SetRotation(Vec3f{.3f,.4f,.2f});
#endif
                    RigidBody3DComponent body;
                    body.Type = BodyType::Static;
                    CapsuleFixture3DComponent fixture;
                    fixture.Property.m_InvMass = 0;
                    fixture.Shape = {capsule->radius, capsule->height*.5f-capsule->radius};
                    created->AddOrReplaceComponent<RigidBody3DComponent>(body);
                    created->AddOrReplaceComponent<CapsuleFixture3DComponent>(fixture);
                }
                ++assets.galleryFixtures;
            }
    }
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::ParametricGeometry))
    {
        const std::size_t capsuleBodies = DebugValidationScene(m_ActiveSelection.scene) == Rbs::RbsScenePreset::GeometryGallery ? 2 : 0;
        if (assets.normalPhysicsBodies + capsuleBodies !=
                m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>().size() ||
            assets.galleryFixtures !=
                ((DebugValidationScene(m_ActiveSelection.scene) == Rbs::RbsScenePreset::GeometryGallery) ? 15u : 0u))
            return std::unexpected(
                PlatformError{PlatformErrorCode::Initialization, "Phase 07 gallery",
                              "Gallery altered expected Physics membership or fixture count"});
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_07_GALLERY_PASS enabled={} fixtures={} physics_bodies={}",
            (DebugValidationScene(m_ActiveSelection.scene) == Rbs::RbsScenePreset::GeometryGallery), assets.galleryFixtures,
            assets.normalPhysicsBodies);
    }

    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateGeometryExamples(Rbs::SceneAssets& assets)
{
    // Phase 08 authoring examples. Ordinary dimensions remain Transform scale;
    // explicit uniqueness, changed Bake and primitive regeneration are distinct.
    GeometryTemplates::Cube authoringCube{2, 3, 4};
    authoringCube.Options.Tangents = GeometryTemplates::TangentMode::Generate;
    auto authoringMesh = m_SceneResources->PublishGeometry(authoringCube);
    if (!authoringMesh)
        return SceneFailure(authoringMesh.error());
    auto authoringSource = CreatePresetEntity(10,"Phase08 Source");
    if (!authoringSource)
        return std::unexpected(authoringSource.error());
    authoringSource->Transform().SetTranslation({-6, 2, 6});
    if (auto assigned = m_SceneResources->AssignRenderable(*authoringSource,
                                                           {*authoringMesh, assets.floorMaterial});
        !assigned)
        return SceneFailure(assigned.error());
    auto authoringDuplicate = authoringSource->Duplicate();
    if (!authoringDuplicate)
        return std::unexpected(authoringDuplicate.error());
    RegisterPresetEntity(*authoringDuplicate,30);
    authoringDuplicate->Name() = "Phase08 Unique Bake";
    authoringDuplicate->Transform().SetTranslation({0, 2, 6});
    if (auto dimensions = m_SceneResources->SetDimensions(*authoringSource, {4, 6, 8}); !dimensions)
        return SceneFailure(dimensions.error());
    if (auto unique = m_SceneResources->MakeGeometryUnique(*authoringDuplicate); !unique)
        return SceneFailure(unique.error());
    authoringDuplicate->Transform().SetScale({2, 1, .5f});
    if (auto baked = m_SceneResources->BakeGeometry(*authoringDuplicate); !baked)
        return SceneFailure(baked.error());
    auto regenerated = CreatePresetEntity(11,"Phase08 Regenerated");
    if (!regenerated)
        return std::unexpected(regenerated.error());
    regenerated->Transform().SetTranslation({6, 2, 6});
    if (auto assigned = m_SceneResources->AssignRenderable(*regenerated,
                                                           {*authoringMesh, assets.sphereMaterial});
        !assigned)
        return SceneFailure(assigned.error());
    GeometryTemplates::Cube regeneratedCube{4, 3, 2};
    regeneratedCube.Options.Tangents = GeometryTemplates::TangentMode::Generate;
    if (auto geometry = m_SceneResources->RegenerateGeometry(*regenerated, regeneratedCube);
        !geometry)
        return SceneFailure(geometry.error());

    if (m_Launch.validation.Includes(Rbs::ValidationCheck::GeometryAuthoring))
    {
        if (!(DebugValidationScene(m_ActiveSelection.scene) == Rbs::RbsScenePreset::GeometryGallery) ||
            assets.galleryFixtures != 15u ||
            assets.normalPhysicsBodies !=
                m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>().size())
            return std::unexpected(PlatformError{
                PlatformErrorCode::Initialization, "Phase 08 gallery preservation",
                "Authoring preset requires the existing gallery and Physics membership"});
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_08_GALLERY_PASS enabled={} fixtures={} physics_bodies={}",
            (DebugValidationScene(m_ActiveSelection.scene) == Rbs::RbsScenePreset::GeometryGallery), assets.galleryFixtures,
            assets.normalPhysicsBodies);
    }

    assets.authoringMesh = *authoringMesh;
    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateMaterialGallery(const Rbs::SceneAssets& assets)
{
    // The same regular five-map floor is shared by each A/B geometry pair.
    // Clone changes authored mapping only; no primitive regeneration or UV edits.
    m_MaterialGallery = {};
    auto uvGallery = m_SceneResources->CloneMaterial(assets.sphereMaterial);
    if (!uvGallery)
        return SceneFailure(uvGallery.error());
    auto triplanarGallery = m_SceneResources->CloneMaterial(*uvGallery);
    if (!triplanarGallery)
        return SceneFailure(triplanarGallery.error());
    m_MaterialGallery.uvMaterial = *uvGallery;
    m_MaterialGallery.triplanarMaterial = *triplanarGallery;
    auto projection = m_SceneResources->DescribeMaterial(*triplanarGallery);
    if (!projection)
        return SceneFailure(projection.error());
    projection->mapping = TextureMappingMode::Triplanar;
    if (auto edited = m_SceneResources->EditMaterial(*triplanarGallery, *projection); !edited)
        return SceneFailure(edited.error());
    auto galleryParent = CreatePresetEntity(12,"Material mapping transform");
    if (!galleryParent)
        return std::unexpected(galleryParent.error());
    m_MaterialGallery.parent = *galleryParent;
    const GeometryTemplates::Request mappingRequests[]{
        GeometryTemplates::Plane{2, 2}, GeometryTemplates::Cube{2, 2, 2},
        GeometryTemplates::Sphere{},    GeometryTemplates::Capsule{},
        GeometryTemplates::Torus{},     GeometryTemplates::Diamond{}};
    constexpr const char* mappingNames[]{"Plane", "Cube", "Sphere", "Capsule", "Torus", "Diamond"};
    for (std::size_t i = 0; i < std::size(mappingRequests); ++i)
    {
        auto mesh = m_SceneResources->PublishGeometry(mappingRequests[i]);
        if (!mesh)
            return SceneFailure(mesh.error());
        for (unsigned mode = 0; mode < 2; ++mode)
        {
            auto entity = CreatePresetEntity(13,
                std::format("{} {}", mappingNames[i], mode ? "Triplanar" : "UV"));
            if (!entity)
                return std::unexpected(entity.error());
            entity->Transform().SetTranslation(
                {-10.f + 4.f * static_cast<float>(i), 2.f, mode ? -24.f : -20.f});
            if (auto parented = entity->SetParent(m_MaterialGallery.parent); !parented)
                return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                                                     "material gallery parent",
                                                     "Parent assignment failed"});
            if (auto assigned = m_SceneResources->AssignRenderable(
                    *entity, {*mesh, mode ? *triplanarGallery : *uvGallery});
                !assigned)
                return SceneFailure(assigned.error());
            (mode ? m_MaterialGallery.triplanar : m_MaterialGallery.uv)[i] = *entity;
        }
    }
    // Opaque, Unlit, Masked, Transparent and Debug use the same authoring API.
    for (unsigned i = 0; i < 5; ++i)
    {
        MaterialAuthoringDesc desc;
        desc.kind = static_cast<MaterialKind>(i);
        desc.baseColor = {.25f + .12f * i, .55f, .85f, 1};
        if (desc.kind == MaterialKind::Transparent)
            desc.opacity = .45f;
        // Optional controlled alpha fixture is a validation input, never a new
        // permanent asset or a fallback for the regular five-map floor comparison.
        if (desc.kind == MaterialKind::Masked)
            if (const auto* fixture = (m_Launch.validation.materialAlphaFixture
                                           ? m_Launch.validation.materialAlphaFixture->c_str()
                                           : nullptr))
            {
                auto image = AssetsManager::LoadTexture(fixture);
                if (!image)
                    return SceneFailure(SceneResourceError{"alpha fixture", image.error()});
                auto texture = AssetsManager::ResolveTexture(*image);
                if (!texture)
                    return SceneFailure(SceneResourceError{"alpha fixture lease", texture.error()});
                auto binding = AssetsManager::SampleTexture(*texture);
                if (!binding)
                    return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                                                         "alpha fixture sampler",
                                                         binding.error().message});
                desc.textures[0] =
                    MaterialTextureValue{binding->TextureIdentity(), binding->SamplerIdentity()};
                desc.mapping = TextureMappingMode::Triplanar;
            }
        auto material = m_SceneResources->CreateMaterial(desc);
        if (!material)
            return SceneFailure(material.error());
        auto entity = CreatePresetEntity(14,std::format("Typed material kind {}", i));
        if (!entity)
            return std::unexpected(entity.error());
        entity->Transform().SetTranslation({-8.f + 4.f * i, 2.f, -28.f});
        if (auto assigned =
                m_SceneResources->AssignRenderable(*entity, {assets.boxMesh, *material});
            !assigned)
            return SceneFailure(assigned.error());
    }
    if (auto checked = ValidateMaterialGallery(assets.boxMesh); !checked)
        return checked;
    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateShaderGallery(const Rbs::SceneAssets& assets)
{
    // Advanced shader authoring is explicit; ordinary Phase 09 materials above
    // keep their existing API, packed storage, mapping and resource identities.
    constexpr const char* customVertex = R"(#version 450 core
layout(location=0) in vec3 aPos;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
void main() { gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0); }
)";
    constexpr const char* customFragment = R"(#version 450 core
layout(location=0) out vec4 FragColor;
uniform vec4 tint;
uniform float gain;
uniform float optionalDetail;
void main() {
#if RBS_VARIANT == 1
    FragColor = vec4(tint.bgr * gain, 1.0);
#else
    FragColor = vec4(tint.rgb * gain, 1.0);
#endif
}
)";
    const ShaderSource customStages[]{{VERTEX, customVertex, "RBS custom vertex"},
                                      {FRAGMENT, customFragment, "RBS custom fragment"}};
    const MaterialShaderParameter customParameters[]{
        {{"tint", MaterialParameterType::Float4, std::array<float, 4>{1.f, .35f, .08f, 1.f}}},
        {{"gain", MaterialParameterType::Float, .8f}},
        {{"optionalDetail", MaterialParameterType::Float, 0.f}, false}};
    const ShaderVariant customVariants[]{{{0}, {{"RBS_VARIANT", 0}}}, {{1}, {{"RBS_VARIANT", 1}}}};
    auto customDescription =
        MaterialShaderDescription::Create({customStages, customParameters, {}, customVariants});
    if (!customDescription)
        return SceneFailure(
            SceneResourceError{"custom shader description", customDescription.error()});
    auto customMaterial = m_SceneResources->CreateMaterial(*customDescription);
    if (!customMaterial)
        return SceneFailure(customMaterial.error());
    auto customObject = CreatePresetEntity(15,"Phase10 Custom Shader");
    if (!customObject)
        return std::unexpected(customObject.error());
    customObject->Transform().SetTranslation({0.f, 5.f, -22.f});
    if (auto assigned =
            m_SceneResources->AssignRenderable(*customObject, {assets.boxMesh, *customMaterial});
        !assigned)
        return SceneFailure(assigned.error());
    if (auto checked =
            ValidateShaderDescription(assets.boxMesh, *customDescription, *customMaterial,
                                      customStages, customParameters, customVariants);
        !checked)
        return checked;

    m_ShaderReloadGallery.material = *customMaterial;
    m_ShaderReloadGallery.original = *customDescription;
    std::string editedFragment(customFragment);
    const auto expression = editedFragment.find("tint.rgb * gain");
    editedFragment.replace(expression, std::string_view("tint.rgb * gain").size(),
                           "tint.gbr * gain");
    const ShaderSource editedStages[]{customStages[0],
                                      {FRAGMENT, editedFragment, "RBS edited custom fragment"}};
    const ShaderSource invalidStages[]{
        customStages[0],
        {FRAGMENT, "#version 450 core\ninvalid shader syntax;", "RBS invalid custom fragment"}};
    auto editedDescription =
        MaterialShaderDescription::Create({editedStages, customParameters, {}, customVariants});
    auto invalidDescription =
        MaterialShaderDescription::Create({invalidStages, customParameters, {}, customVariants});
    std::vector<MaterialShaderParameter> incompatibleParameters(std::begin(customParameters),
                                                                std::end(customParameters));
    incompatibleParameters[1].declaration.name = "incompatibleGain";
    auto incompatibleDescription = MaterialShaderDescription::Create(
        {customStages, incompatibleParameters, {}, customVariants});
    if (!editedDescription || !invalidDescription || !incompatibleDescription)
        return SceneFailure(
            SceneResourceError{"reload fixture descriptions", SceneResourceCode::InvalidMaterial});
    m_ShaderReloadGallery.edited = std::move(*editedDescription);
    m_ShaderReloadGallery.invalid = std::move(*invalidDescription);
    m_ShaderReloadGallery.incompatible = std::move(*incompatibleDescription);
    auto customClone = m_SceneResources->CloneMaterial(*customMaterial);
    if (!customClone)
        return SceneFailure(customClone.error());
    m_ShaderReloadGallery.clone = *customClone;
    if (auto changed = m_SceneResources->SetMaterialParameter(*customClone, "gain", .35f); !changed)
        return SceneFailure(changed.error());
    auto cloneObject = CreatePresetEntity(16,"Phase11 Custom Shader Clone");
    if (!cloneObject)
        return std::unexpected(cloneObject.error());
    cloneObject->Transform().SetTranslation({3.f, 5.f, -22.f});
    if (auto assigned =
            m_SceneResources->AssignRenderable(*cloneObject, {assets.boxMesh, *customClone});
        !assigned)
        return SceneFailure(assigned.error());
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::ShaderReload))
        FocusMaterialCamera(m_EditorCamera_, {0, 3, -22}, .35f, 0, 38.f);

    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::StartSceneRuntime(const BackendAuthoring* authoring)
{
    FreezePresetSignatures();
    if(authoring)
        if(auto restored=authoring->ApplyBeforeStart(*m_ActiveScene);!restored)return restored;
    // Scene's PhysicsWorld owns its runtime bodies and shapes through teardown.
    if (auto started = m_ActiveScene->OnRuntimeStart(); !started)
        return std::unexpected(started.error());

    auto* physics = m_ActiveScene->GetPhysicsSystem();
    const auto* world = physics ? physics->GetPhysicsWorld() : nullptr;
    if (!world || world->GetBackend() != m_Backend)
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
            "Physics backend", "Effective backend does not match the requested backend"});

    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateAsyncExamples(const Rbs::SceneAssets& assets)
{
    // Maintained async editing examples: two entities share the target material;
    // a third has independent authored values and texture assignments.
    auto independent = m_SceneResources->CloneMaterial(m_AsyncBoxMaterial);
    if (!independent)
        return SceneFailure(independent.error());
    m_AsyncTextureGallery.clone = *independent;
    auto authored = m_SceneResources->DescribeMaterial(*independent);
    if (!authored)
        return SceneFailure(authored.error());
    authored->baseColor = {.45f, .8f, .6f, 1.f};
    if (auto edited = m_SceneResources->EditMaterial(*independent, *authored); !edited)
        return SceneFailure(edited.error());
    for (int i = 0; i < 3; ++i)
    {
        auto entity = CreatePresetEntity(17,std::format("Async texture {}", i));
        if (!entity)
            return std::unexpected(entity.error());
        entity->Transform().SetTranslation({-3.f + 3.f * i, 8.f, -22.f});
        if (auto assigned = m_SceneResources->AssignRenderable(
                *entity, {assets.boxMesh, i == 2 ? *independent : m_AsyncBoxMaterial});
            !assigned)
            return SceneFailure(assigned.error());
    }
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::AsyncResources))
        FocusMaterialCamera(m_EditorCamera_, {0, 4, -22}, .35f, 0, 38.f);
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::InitializeScene(
    const BackendAuthoring* authoring,const Rbs::AngularPacket* angularCandidate,const Rbs::RagdollPacket* ragdollCandidate,
    const Rbs::MoverPacket* moverCandidate)
{
    m_BuildingPreset=SandboxEnabled();m_PresetOrdinals={};
    struct EndPreset {bool& flag;~EndPreset(){flag=false;}} endPreset{m_BuildingPreset};
    InitializationProgress::Report(InitializationProgress::Stage::Scene,"Loading scene resources");
    Rbs::SceneAssets assets;
    if (auto result = InitializeSceneRoot(); !result)
        return result;
    if (auto result = PublishSceneMeshes(assets); !result)
        return result;
    if (auto result = LoadSceneIcons(); !result)
        return result;
    InitializationProgress::Report(InitializationProgress::Stage::Scene,"Loading textures and materials");
    if (auto result = LoadSurfaceTextures(assets); !result)
        return result;
    if (auto result = CreateSceneMaterials(assets); !result)
        return result;
    if (auto result = CreateSceneLights(assets); !result)
        return result;
    if(m_ActiveSelection.scene==Rbs::RbsScenePreset::Movers)
        return InitializeMoverScene(moverCandidate ? *moverCandidate : m_Mover.committed,assets,authoring);
    if(m_ActiveSelection.IsRagdollScene())
    {
        const auto* packet=ragdollCandidate?ragdollCandidate:(m_Ragdoll.committed?&*m_Ragdoll.committed:nullptr);
        if(!packet)return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Passive Ragdoll","Missing scene packet"});
#ifdef GENGINE_PHASE29_QUALIFICATION
        if(const char* name=std::getenv("GENGINE_PHASE29_CASE");
           name && std::string_view(name)=="R29_TILT_LIMIT_WAKE_V01")
        {
            if(SandboxEnabled() || authoring || *packet!=Rbs::ReferenceRagdollPacket())
                return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Passive Ragdoll","Invalid tilted fixture environment"});
            return InitializeRagdollScene(Rbs::TiltRagdollPacket(),assets,nullptr);
        }
#endif
        return InitializeRagdollScene(*packet,assets,authoring);
    }
    if(m_ActiveSelection.IsAngularScene())
    {
        const auto* packet=angularCandidate?angularCandidate:
            (m_Angular.committed?&*m_Angular.committed:nullptr);
        if(!packet || packet->scene!=m_ActiveSelection.scene)
            return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Angular scene","Missing or mismatched authored packet"});
        if(auto valid=Rbs::ValidateAngularPacket(*packet,m_Backend,!SandboxEnabled());!valid)
            return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Angular scene",valid.error().message});
#ifdef GENGINE_PHASE28_QUALIFICATION
        if (const char* fixture = std::getenv("GENGINE_PHASE28_CONTACT_FIXTURE"))
        {
            const char* runCase = std::getenv("GENGINE_PHASE28_CASE");
            const auto expectedCase = packet->scene == Rbs::RbsScenePreset::HingeLimited ? "H_CONTACT" :
                                      packet->scene == Rbs::RbsScenePreset::CvLimited ? "CV_CONTACT" : "";
            const std::string_view version(fixture);
            if ((version != "v2" && version != "v3" && version != "v4") ||
                (version == "v4" && packet->scene != Rbs::RbsScenePreset::CvLimited) ||
                SandboxEnabled() || !Rbs::Phase28AngularOwnsActions() ||
                !runCase || std::string_view(runCase) != expectedCase || !*expectedCase ||
                packet->variant != Rbs::AngularVariant::Contact || packet->bodyCount != 3 ||
                packet->bodies[2].key != Rbs::AngularBodyKey::Floor)
                return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Versioned contact","Fixture selector and qualification scene disagree"});
            // Each frozen ramp is a qualification copy of the limited-contact graph.
            // The immutable Book and the ordinary Editor keep their own geometry.
            auto fixturePacket = *packet;
            if (version == "v2") {
                fixturePacket.bodies[2].center = {2.199326470f, 1.474437224f, 0.f};
                fixturePacket.bodies[2].orientation = {0.843391446f, 0.f, 0.f, 0.537299608f};
            } else {
                fixturePacket.bodies[2].center = {2.001681526f, 1.963651052f, 0.f};
                fixturePacket.bodies[2].orientation = {0.793353340f, 0.f, 0.f, 0.608761429f};
                const Quat frameOffset{0.994056338f, 0.f, 0.f, 0.108866875f};
                fixturePacket.joint.localFrameB = glm::normalize(frameOffset * fixturePacket.joint.localFrameB);
            }
            return InitializeAngularScene(fixturePacket, assets);
        }
#endif
        return InitializeAngularScene(*packet,assets,authoring);
    }
    if (m_Launch.restingContactDiagnostic)
        return InitializeRestingContactScene(assets);

    if (m_ActiveSelection.IsBallSocketScene())
    {
        const bool swing=m_ActiveSelection.scene==Rbs::RbsScenePreset::BallSocketSwing;
        const bool pair=m_ActiveSelection.scene==Rbs::RbsScenePreset::BallSocketPair;
        if (auto result=CreateGroundAndWalls(assets);!result)return result;
        std::array<_Entity,2> entities;
        for(unsigned i=0;i<2;++i) {
            const char* name=swing?(i==0?"Ball Socket Anchor":"Ball Socket Swing Body"):
                (i==0?"Ball Socket Body A (1 kg)":"Ball Socket Body B (2 kg)");
            auto created=CreatePresetEntity(19,name); if(!created)return std::unexpected(created.error());
            auto entity=*created; entities[i]=entity;
            const Vec3f position=swing?Vec3f(0,i==0?7.25f:5.f,0):Vec3f(i==0?-1.125f:1.125f,pair?6.f:1.5f,0);
            entity.AddOrReplaceComponent<Transform3DComponent>(position,Vec3f(0),Vec3f(1));
            RigidBody3DComponent body; body.Type=swing && i==0?BodyType::Static:BodyType::Dynamic;
            entity.AddOrReplaceComponent<RigidBody3DComponent>(body);
            SphereFixture3DComponent fixture; fixture.Radius=1;
            fixture.Property.m_InvMass=swing?(i==0?0.f:1.f):(i==0?1.f:.5f);
            fixture.Property.m_Friction=.5f; fixture.Property.m_Elasticity=.5f;
            fixture.Property.m_LinearVelocity=Vec3f(0); fixture.Property.m_AngularVelocity=Vec3f(0);
            entity.AddOrReplaceComponent<SphereFixture3DComponent>(fixture);
            if(auto result=CreateRenderable(entity,assets.smoothSphereGeo,assets.sphereMaterial,"");!result)return result;
        }
        if(auto result=CreateEnvironmentHelpers();!result)return result;
        if(auto result=StartSceneRuntime(authoring);!result)return result;
        auto* physics=m_ActiveScene->GetPhysicsSystem(); auto* world=physics->GetPhysicsWorld();
        world->SetGravity(pair?Vec3f(0):Vec3f(0,-12,0));
        const auto a=m_ActiveScene->GetBodyBinding(entities[0]),b=m_ActiveScene->GetBodyBinding(entities[1]);
        if(!a || !b)return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"BallSocket","Missing body binding"});
        for(std::size_t i=0;i<world->GetBodyCount();++i) {
            auto h=world->BodyAt(i);
            if(!h || !world->SetBodyMaterial(*h,{.5f,.5f,.05f,.05f}) || !world->SetBodySleepSettings(*h,{.05f,.02f,.5}))
                return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"BallSocket","Material/sleep settings rejected"});
        }
        const Vec3f anchorA=swing?Vec3f(0):Vec3f(1.125f,0,0);
        const Vec3f anchorB=swing?Vec3f(0,2.25f,0):Vec3f(-1.125f,0,0);
        const bool connectedContact=m_ActiveSelection.scene==Rbs::RbsScenePreset::BallSocketContactEnabled;
        if(!world->CreateBallSocketJoint({*a,*b,anchorA,anchorB,connectedContact}))
            return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"BallSocket","Point joint rejected"});
        const auto target=swing?*b:*a;
        m_ChainImpulse.Bind(target,true);
        physics->SetBeforeStep([this,physics,target,swing](PhysicsWorld& w,std::uint64_t tick) {
            if(physics->HasJointFailure()) {
                FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","BallSocket","Update","Phase27","Joint numerical failure"});return;
            }
            if(tick==300 && !w.ApplyLinearImpulse(target,swing?Vec3f(.3f,0,.4f):Vec3f(0,0,.5f)))
                FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","BallSocket","Impulse","Phase27","Impulse rejected"});
            m_ChainImpulse.Consume(w,m_ActiveScene->IsPaused());
        });
        FocusMaterialCamera(m_EditorCamera_,swing?Vec3f(0,5.5f,0):Vec3f(0,pair?6.f:1.5f,0),.25f,-.3f,14.f);
        m_IsShowDebugBoundingBox=true;
        Log::GetCoreLogger()->info("RBS point scene={} backend={} bodies={} joints=1 fixedHz=60 kickAfterTick=300",
            Rbs::SceneName(m_ActiveSelection.scene),PhysicsBackendName(m_Backend),world->GetBodyCount());
        return ConnectApplicationEventHandlers();
    }

    if (m_ActiveSelection.IsDistanceScene())
    {
        if(!m_ActiveSelection.IsValid())
            return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"DistanceChain","Invalid startup configuration"});
        const unsigned n=m_ActiveSelection.scene==Rbs::RbsScenePreset::DistancePendulum?1:m_ActiveSelection.chainLinks;
        const bool contact=m_ActiveSelection.scene==Rbs::RbsScenePreset::DistanceChainContact;
        const bool sandbox=SandboxEnabled();
        // Author the room first so the established reverse body insertion still
        // publishes root/links first. G8's sole interactive ground is this floor.
        if (sandbox)
            if (auto result=CreateGroundAndWalls(assets);!result) return result;
        std::array<_Entity,18> entities;
        const unsigned count=n+1+(contact && !sandbox?1:0);
        // EnTT's RigidBody component view traverses reverse insertion order.
        // Insert bodies backwards so the world publishes root,links,ground.
        for(unsigned reverse=count;reverse>0;--reverse) {
            const unsigned i=reverse-1;
            const bool ground=i==n+1;
            auto created=CreatePresetEntity(18,ground?"G8 ground":i==0?"Chain root":"Chain link "+std::to_string(i));
            if(!created)return std::unexpected(created.error());
            auto e=*created; entities[i]=e;
            const Vec3f position=ground?Vec3f(0):contact?Vec3f(2.25f*i,1.5f,0):Vec3f(0,1.5f+2.25f*(n-i),0);
            e.AddOrReplaceComponent<Transform3DComponent>(position,Vec3f(0),ground?Vec3f(100,1,100):Vec3f(1));
            RigidBody3DComponent body; body.Type=(i==0||ground)?BodyType::Static:BodyType::Dynamic;
            e.AddOrReplaceComponent<RigidBody3DComponent>(body);
            if(ground) {
                BoxFixture3DComponent f; f.Shape.halfExtents=Vec3f(.5f);
                f.Property.m_InvMass=0; f.Property.m_Friction=.5f; f.Property.m_Elasticity=.5f;
                e.AddOrReplaceComponent<BoxFixture3DComponent>(f);
            } else {
                SphereFixture3DComponent f; f.Radius=1;
                f.Property.m_InvMass=i==0?0.f:1.f; f.Property.m_Friction=.5f; f.Property.m_Elasticity=.5f;
                f.Property.m_LinearVelocity=Vec3f(0); f.Property.m_AngularVelocity=Vec3f(0);
                e.AddOrReplaceComponent<SphereFixture3DComponent>(f);
            }
            if(auto authored=CreateRenderable(e,ground?assets.boxMesh:assets.smoothSphereGeo,
                    ground?assets.floorMaterial:assets.sphereMaterial,ground?"Box":"");!authored)return authored;
        }
        if(auto result=CreateEnvironmentHelpers();!result)return result;
        if(auto result=StartSceneRuntime(authoring);!result)return result;
        auto* physics=m_ActiveScene->GetPhysicsSystem(); auto* world=physics->GetPhysicsWorld();
        std::array<PhysicsBodyHandle,18> handles;
        for(unsigned i=0;i<count;++i) {
            auto binding=m_ActiveScene->GetBodyBinding(entities[i]);
            auto ordinal=world->BodyAt(i);
            if(!binding || (!sandbox && (!ordinal || *binding!=*ordinal)))
                return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"DistanceChain","Deterministic world membership mismatch"});
            handles[i]=*binding;
            if(!world->SetBodyMaterial(handles[i],{.5f,.5f,.05f,.05f}) ||
                !world->SetBodySleepSettings(handles[i],{.05f,.02f,.5}))
                return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"DistanceChain","Material/sleep settings rejected"});
        }
        if (sandbox)
        {
            // Preserve G8's admitted contact material on its one authoritative
            // floor. Resolve room bodies by entity identity, never body ordinals.
            const auto room = Rbs::SandboxEnvironment::Boxes();
            unsigned configured = 0;
            for (auto id : m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>())
            {
                _Entity entity{id, m_ActiveScene.get()};
                if (std::none_of(room.begin(), room.end(), [&](const auto& box) {
                        return entity.Name() == box.name;
                    })) continue;
                auto binding = m_ActiveScene->GetBodyBinding(entity);
                if (!binding || !world->SetBodyMaterial(*binding,{.5f,.5f,.05f,.05f}) ||
                    !world->SetBodySleepSettings(*binding,{.05f,.02f,.5}))
                    return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"DistanceChain",
                        "Shared room material/sleep settings rejected"});
                ++configured;
            }
            if (configured != room.size())
                return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"DistanceChain",
                    "Shared room body membership mismatch"});
        }
        for(unsigned i=1;i<=n;++i) {
            if(!world->CreateDistanceJoint({handles[i-1],handles[i],{},{},2.25f,false}))
                return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"DistanceChain","Joint creation rejected"});
        }
        if(auto bound=BindDistanceCommands(handles[n],contact);!bound)return bound;
        const Vec3f focal=contact?Vec3f(9,1.5f,0):Vec3f(0,1.5f+1.125f*n,0);
        FocusMaterialCamera(m_EditorCamera_,focal,sandbox?.28f:.12f,contact?-.25f:0.f,
                            contact?30.f:12.f+3.f*n);
        m_IsShowDebugBoundingBox=true;
        Log::GetCoreLogger()->info("RBS scene={} links={} bodies={} joints={} fixedHz=60 passes=1 kickAfterTick=300",
            Rbs::SceneName(m_ActiveSelection.scene),n,world->GetBodyCount(),world->GetJointCount());
        if(auto result=ConnectApplicationEventHandlers();!result)return result;
#ifdef GENGINE_PHASE26_QUALIFICATION
        Rbs::Phase26::Attach(*m_ActiveScene,*physics,*this,m_Launch);
#endif
        return {};
    }

    if (auto result = CreatePhysicsScene(assets); !result)
        return result;
    if (auto result = CreateEnvironmentHelpers(); !result)
        return result;
    if (auto result = CreateGroundAndWalls(assets); !result)
        return result;
    if (auto result = CreateTemplateGallery(assets); !result)
        return result;
    // The preset owns the complete scene. Authoring demos are never shared
    // infrastructure for the Physics fixtures, and UI cannot create them later.
    if (DebugValidationScene(m_ActiveSelection.scene) == Rbs::RbsScenePreset::GeometryGallery)
    {
        if (auto result = CreateGeometryExamples(assets); !result)
            return result;
        if (auto result = CreateMaterialGallery(assets); !result)
            return result;
        if (auto result = CreateShaderGallery(assets); !result)
            return result;
    }
    if (auto result = StartSceneRuntime(authoring); !result)
        return result;
#ifdef GENGINE_PHASE26_QUALIFICATION
    Rbs::Phase26::Attach(*m_ActiveScene,*m_ActiveScene->GetPhysicsSystem(),*this,m_Launch);
#endif
    if (auto result = ValidateRuntimeScene(assets); !result)
        return result;
    if (auto result = ConnectApplicationEventHandlers(); !result)
        return result;
    if (auto result = ValidateScaleBridge(assets); !result)
        return result;
    if (DebugValidationScene(m_ActiveSelection.scene) == Rbs::RbsScenePreset::GeometryGallery)
    {
        if (auto result = CreateAsyncExamples(assets); !result)return result;
        if(SandboxEnabled())FreezePresetSignatures();
    }
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if (auto result = ValidatePhysicsScene(assets); !result)
        return result;
    if (auto result = ValidateSceneMembership(); !result)
        return result;
    // Exercise real UI/resource frames without stepping the fixture oracle.
    m_IsPause = true;
#endif
    if (SandboxEnabled())
    {
        Vec3f focal{3.f, 6.f, 0.f};
        float distance = 42.f;
        switch (m_ActiveSelection.scene)
        {
        case Rbs::RbsScenePreset::GeometryGallery: focal = {0, 4, -14}; distance = 55.f; break;
        case Rbs::RbsScenePreset::SphereDiamond: focal = {0, 5, 0}; distance = 70.f; break;
        case Rbs::RbsScenePreset::SphereLattice: focal = {3, 14, 3}; distance = 55.f; break;
        default: break;
        }
        FocusMaterialCamera(m_EditorCamera_, focal, .28f, -.2f, distance);
    }
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::InitializeMoverScene(
    const Rbs::MoverPacket& packet,const Rbs::SceneAssets& assets,const BackendAuthoring* authoring)
{
    const auto reject=[](std::string_view why)->ApplicationInitializationResult {
        return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Movers",std::string(why)});
    };
    if(auto valid=Rbs::ValidateMoverPacket(packet);!valid)return reject(valid.error());
    const auto path=packet.path.Normalized();
    if(!path)return reject(path.error().operation);
    const bool rebind = m_MoverBindings.entities[0] != entt::null;
    MaterialHandle platformMaterial{};
    if (!rebind) {
        if (auto room = CreateGroundAndWalls(assets); !room) return room;
        auto material = m_SceneResources->CloneMaterial(assets.floorMaterial);
        if (!material) return SceneFailure(material.error());
        platformMaterial = *material;
    }

    for(std::size_t i=0;i<2;++i) {
        const auto& body=i==0?packet.platform:packet.rider;
        const auto position=i==0?path->PositionAt(0):packet.riderPosition;
        _Entity entity{m_MoverBindings.entities[i],m_ActiveScene.get()};
        if (!rebind) {
            auto created = CreatePresetEntity(33,i==0?"Moving Platform":"Platform Rider");
            if (!created) return std::unexpected(created.error());
            entity = *created;
            m_MoverBindings.entities[i] = static_cast<entt::entity>(entity);
        }
        entity.AddOrReplaceComponent<Transform3DComponent>(position,Vec3f(0),2.f*body.halfExtents);
        entity.Transform().SetRotation(body.orientation);
        RigidBody3DComponent rigid;
        rigid.Type=body.properties.type;
        entity.AddOrReplaceComponent<RigidBody3DComponent>(rigid);
        BoxFixture3DComponent box;
        box.Shape.halfExtents=Vec3f(.5f);
        box.Property.m_Position=position;
        box.Property.m_Orientation=body.orientation;
        box.Property.m_InvMass=i==0?0.f:1.f/body.properties.mass;
        box.Property.m_Friction=body.properties.material.friction;
        box.Property.m_Elasticity=body.properties.material.restitution;
        box.Property.m_LinearVelocity=body.linearVelocity;
        box.Property.m_AngularVelocity=body.angularVelocity;
        entity.AddOrReplaceComponent<BoxFixture3DComponent>(box);
        if (!rebind) {
            // The original Rider handle remains the destination of async Wood publication.
            const auto material = i == 0 ? platformMaterial : assets.boxMaterial;
            if (auto render = CreateRenderable(entity, assets.boxMesh, material, "Box"); !render) return render;
        }
    }
    if (!rebind) {
        if (auto helpers = CreateEnvironmentHelpers(); !helpers) return helpers;
        m_AxisEntity.AddOrReplaceComponent<VisibilityComponent>(VisibilityComponent{false});
    }
    if(auto started=StartSceneRuntime(authoring);!started)return started;
    auto& world=*m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    if(authoring)if(auto r=authoring->ApplyAfterStart(*m_ActiveScene);!r)return r;
    world.SetGravity(packet.gravity);
    for(std::size_t i=0;i<2;++i) {
        auto h=m_ActiveScene->GetBodyBinding(_Entity{m_MoverBindings.entities[i],m_ActiveScene.get()});
        if(!h)return reject("Missing body binding");
        m_MoverBindings.bodies[i]=*h;
        const auto& body=i==0?packet.platform:packet.rider;
        auto applied=world.UpdateBodyProperties(*h,body.properties);
        if(!applied)return reject(applied.error().operation);
        auto effective=world.ReadEffectiveBody(*h);
        if(!effective)return reject(effective.error().operation);
        if(effective->type!=body.properties.type || effective->shapeType!=ShapeType::Box ||
            effective->material!=body.properties.material)return reject("Provider body or material readback mismatch");
    }
    auto binding=world.RegisterMover(m_MoverBindings.bodies[0],m_MoverBindings.bodies[1],
        packet.path,m_ActiveScene->GetPhysicsStep().count());
    if(!binding)return reject(binding.error().operation);
    if (rebind) return {};
    FocusMaterialCamera(m_EditorCamera_,path->center+Vec3f(0,1,0),.22f,1.15f,27.f);
    m_IsShowDebugBoundingBox=false;m_ShowSelectedColliderOutlines=false;
    return ConnectApplicationEventHandlers();
}

ApplicationInitializationResult RigidBodySimulationApp::InitializeRagdollScene(
    const Rbs::RagdollPacket& packet,const Rbs::SceneAssets& assets,const BackendAuthoring* authoring)
{
    const auto failure=[](std::string_view why)->ApplicationInitializationResult{
        return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Passive Ragdoll",why});};
    if(auto checked=Rbs::ValidateRagdollPacket(packet,m_Backend);!checked)return failure(checked.error().message);
    m_RagdollBindings={};
    if(SandboxEnabled())
    {
        if(auto room=CreateGroundAndWalls(assets,&m_RagdollBindings.environmentEntities);!room)return room;
    }
    else
    {
        // The reference sandbox uses +Y up. Keep its geometry and surfaces
        // independent from the interactive room and its authored settings.
        struct Boundary {const char* name; Vec3f center,halfExtents; float friction;};
        const std::array<Boundary,5> room{{
            {"Floor",{0,-.5f,0},{50,.5f,25},.5f},
            {"Wall +X",{50,2.5f,0},{1,2.5f,25},0},
            {"Wall -X",{-50,2.5f,0},{1,2.5f,25},0},
            {"Wall -Z",{0,2.5f,-25},{50,2.5f,1},0},
            {"Wall +Z",{0,2.5f,25},{50,2.5f,1},0}}};
        for(std::size_t i=0;i<room.size();++i)
        {
            const auto& boundary=room[i];
            auto e=CreatePresetEntity(7,boundary.name);
            if(!e)return std::unexpected(e.error());
            m_RagdollBindings.environmentEntities[i]=static_cast<entt::entity>(*e);
            e->AddOrReplaceComponent<Transform3DComponent>(boundary.center,Vec3f{},2.f*boundary.halfExtents);
            RigidBody3DComponent rigid;
            rigid.Type=BodyType::Static;
            e->AddOrReplaceComponent<RigidBody3DComponent>(rigid);
            BoxFixture3DComponent box;
            box.Property.m_InvMass=0;
            box.Property.m_Position=boundary.center;
            box.Property.m_Friction=boundary.friction;
            box.Property.m_Elasticity=.5f;
            e->AddOrReplaceComponent<BoxFixture3DComponent>(box);
            if(auto visual=CreateRenderable(*e,assets.boxMesh,i==0?assets.floorMaterial:assets.wallMaterial,"Box");!visual)return visual;
        }
    }
    const Vec3f placement{0,packet.dropOffset+(SandboxEnabled()?Rbs::SandboxEnvironment::FloorTop:0.f),0};
    const auto reference=Rbs::ReferenceRagdollPacket();
    for(std::size_t i=0;i<packet.bodies.size();++i)
    {
        const auto& b=packet.bodies[i];
        auto e=CreatePresetEntity(32,RagdollName(b.key));
        if(!e)return std::unexpected(e.error());
        m_RagdollBindings.entities[i]=static_cast<entt::entity>(*e);
        const Vec3f scale=Rbs::RagdollRenderScale(b);
        e->AddOrReplaceComponent<Transform3DComponent>(b.center+placement,Vec3f{},scale);
        e->Transform().SetRotation(b.orientation);
        e->AddOrReplaceComponent<VisibilityComponent>(VisibilityComponent{b.visible});
        RigidBody3DComponent rigid;
        rigid.Type=b.properties.type;
        e->AddOrReplaceComponent<RigidBody3DComponent>(rigid);
        Fixture3DProperty props;
        props.m_Position=b.center+placement;
        props.m_Orientation=b.orientation;
        props.m_InvMass=rigid.Type==BodyType::Dynamic?1.f/b.properties.mass:0.f;
        props.m_Friction=b.properties.material.friction;
        props.m_Elasticity=b.properties.material.restitution;
        props.m_LinearVelocity=b.initialLinearVelocity;
        props.m_AngularVelocity=b.initialAngularVelocity;
        auto mesh=assets.boxMesh;
        if(b.shape==ShapeType::Box)
        {
            BoxFixture3DComponent box;
            box.Property=props;
            box.Shape={Vec3f(.5f),b.colliderCenter/scale};
            e->AddOrReplaceComponent<BoxFixture3DComponent>(box);
            // Only three immutable UV layouts are needed. Ordinary size edits
            // stretch the authored skin with the box without creating cache keys.
            const char* shapeName=b.key==RagdollBone::Head?"RbsRagdollRustHead":
                                  b.key==RagdollBone::Torso?"RbsRagdollRustTorso":"RbsRagdollRustLimb";
            auto shape=ShapeManager::_GetShape<RagdollBoxGeometry>(shapeName,
                Rbs::RagdollRenderScale(reference.bodies[i]));
            if(!shape)return std::visit([](const auto& error)->ApplicationInitializationResult
                {return std::unexpected(error);},shape.error());
            auto authored=m_SceneResources->PublishShape(shapeName);
            if(!authored)return SceneFailure(authored.error());
            mesh=*authored;
        }
        else if(b.shape==ShapeType::Sphere)
        {
            SphereFixture3DComponent sphere;
            sphere.Property=props;sphere.Radius=b.radius;
            e->AddOrReplaceComponent<SphereFixture3DComponent>(sphere);
            GeometryTemplates::Sphere geometry;
            // Integral longitudinal repeats close the seam. In metres, the
            // pole-to-pole meridian is half the equatorial circumference.
            const float wraps=std::max(1.f,std::round(2.f*Math::Pi*b.radius/RagdollRustTileMeters));
            geometry.Options.UVTiling={wraps,.5f*wraps};
            auto authored=m_SceneResources->PublishGeometry(geometry);
            if(!authored)return SceneFailure(authored.error());
            mesh=*authored;
        }
        else
        {
            CapsuleFixture3DComponent capsule;
            capsule.Property=props;capsule.Shape={1.f,b.halfLength/b.radius};
            e->AddOrReplaceComponent<CapsuleFixture3DComponent>(capsule);
            GeometryTemplates::Capsule geometry;
            geometry.radius=1.f;geometry.height=2.f*(1.f+capsule.Shape.halfLength);
            const float circumference=2.f*Math::Pi*b.radius;
            const float wraps=std::max(1.f,std::round(circumference/RagdollRustTileMeters));
            const float meridian=Math::Pi*b.radius+2.f*b.halfLength;
            geometry.Options.UVTiling={wraps,wraps*meridian/circumference};
            auto authored=m_SceneResources->PublishGeometry(geometry);
            if(!authored)return SceneFailure(authored.error());
            mesh=*authored;
        }
        // The Sphere preset's five PBR bindings are shared; editable instances are per bone.
        auto material=m_SceneResources->CloneMaterial(assets.sphereMaterial);
        if(!material)return SceneFailure(material.error());
        if(auto visual=CreateRenderable(*e,mesh,*material,"");!visual)return visual;
    }
    if(auto helpers=CreateEnvironmentHelpers();!helpers)return helpers;
    m_AxisEntity.AddOrReplaceComponent<VisibilityComponent>(VisibilityComponent{false});
    if(auto started=StartSceneRuntime(authoring);!started)return started;
    auto* physics=m_ActiveScene->GetPhysicsSystem();
    auto* world=physics?physics->GetPhysicsWorld():nullptr;
    if(!world || world->GetBodyCount()!=11 || world->GetJointCount()!=0)return failure("Incomplete scene body membership");
    const Vec3f gravity{0,packet.gravityY,0};
    world->SetGravity(gravity);
    std::array<PhysicsBodyHandle,6> handles{};
    for(std::size_t i=0;i<handles.size();++i)
    {
        const auto& b=packet.bodies[i];
        auto h=m_ActiveScene->GetBodyBinding(_Entity{m_RagdollBindings.entities[i],m_ActiveScene.get()});
        if(!h)return failure("Missing Physics binding for a ragdoll member");
        handles[i]=*h;
        const auto initial=world->ReadBodyProperties(*h);
        if(!initial || (*initial!=b.properties && !world->UpdateBodyProperties(*h,b.properties)))
            return failure("Backend rejected authored body properties");
        const auto state=world->ReadEffectiveBody(*h);
        const auto bodyProps=world->ReadBodyProperties(*h);
        const auto extents=RagdollShapeHalfExtents(b);
        if(!state || !bodyProps || std::abs(bodyProps->mass-b.properties.mass)>1.e-5f ||
           glm::length(state->position-b.center-placement)>1.e-5f ||
           glm::length(state->linearVelocity-b.initialLinearVelocity)>1.e-5f ||
           glm::length(state->angularVelocity-b.initialAngularVelocity)>1.e-5f ||
           glm::length(state->localBounds.mins-(b.colliderCenter-extents))>1.e-5f ||
           glm::length(state->localBounds.maxs-(b.colliderCenter+extents))>1.e-5f)
            return failure("Effective body dimensions, mass, velocity or placement differ from the draft");
    }
    auto binding=PassiveRagdoll::Bind(*world,packet,handles);
    if(!binding)return failure(binding.error().message);
    m_RagdollBindings.mapping=*binding;
    if(authoring)
        if(auto restored=authoring->ApplyAfterStart(*m_ActiveScene);!restored)return restored;
    if(world->GetGravity()!=gravity || world->GetJointCount()!=5)return failure("Effective gravity or ragdoll joint count differs");
    physics->SetBeforeStep([this,physics](PhysicsWorld&,std::uint64_t){
        if(physics->HasJointFailure())FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
            "Physics","PassiveRagdoll","Update","Phase29","Joint numerical failure"});
    });
    // The reference limbs span Z. View across X so left and right stay distinct.
    FocusMaterialCamera(m_EditorCamera_,packet.bodies[1].center+placement,.12f,1.32f,13.f);
    m_IsShowDebugBoundingBox=false;m_ShowSelectedColliderOutlines=false;
    return ConnectApplicationEventHandlers();
}

ApplicationInitializationResult RigidBodySimulationApp::InitializeAngularScene(
    const Rbs::AngularPacket& packet,const Rbs::SceneAssets& assets,const BackendAuthoring* authoring)
{
    using Rbs::AngularBodyKey;
    m_AngularBindings={};
    m_AngularBindings.bodyCount=packet.bodyCount;
    // Ordinary Editor environment revision 1 shares the established room.
    // The isolated numerical fixture still consumes its exact frozen packet.
    if(SandboxEnabled())
        if(auto room=CreateGroundAndWalls(assets,&m_AngularBindings.environmentEntities);!room)return room;
    for(std::size_t i=0;i<packet.bodyCount;++i)
    {
        const auto& body=packet.bodies[i];
        if(SandboxEnabled() && body.key==AngularBodyKey::Floor)
        {
            m_AngularBindings.entities[i]=m_AngularBindings.environmentEntities[0];
            continue; // One physical floor, supplied by the common room.
        }
        const bool dynamic = body.key == AngularBodyKey::Bar || body.key == AngularBodyKey::Load;
        const char* name=packet.joint.motor ? (body.key==AngularBodyKey::Anchor ? "Motor Base" :
            body.key==AngularBodyKey::Bar ? "Motor Bar" : "Contact Sphere") :
            body.key==AngularBodyKey::Anchor?"Angular Anchor":
            body.key==AngularBodyKey::Bar?"Angular Bar":"Angular Contact Floor";
        auto created=CreatePresetEntity(31,name);
        if(!created)return std::unexpected(created.error());
        auto entity=*created;
        m_AngularBindings.entities[i]=static_cast<entt::entity>(entity);
        const Vec3f scale=body.sphere?body.halfExtents:2.f*body.halfExtents;
        entity.AddOrReplaceComponent<Transform3DComponent>(body.center,Vec3f(0.f),scale);
        entity.GetComponent<Transform3DComponent>().SetRotation(body.orientation);
        RigidBody3DComponent rigid;
        rigid.Type=dynamic?BodyType::Dynamic:
            body.kinematic?BodyType::Kinematic:BodyType::Static;
        entity.AddOrReplaceComponent<RigidBody3DComponent>(rigid);
        Fixture3DProperty properties;
        properties.m_Position=body.center;
        properties.m_Orientation=body.orientation;
        properties.m_InvMass=dynamic?1.f/body.authoredMass:0.f;
        properties.m_Friction=body.friction;
        properties.m_Elasticity=body.restitution;
        properties.m_LinearVelocity=body.initialLinearVelocity;
        properties.m_AngularVelocity=body.initialAngularVelocity;
        if(body.sphere) {
            SphereFixture3DComponent fixture;
            fixture.Radius=body.halfExtents.y;
            fixture.Property=properties;
            entity.AddOrReplaceComponent<SphereFixture3DComponent>(fixture);
            if(auto visual=CreateRenderable(entity,assets.smoothSphereGeo,assets.sphereMaterial,"");!visual)return visual;
        } else {
            BoxFixture3DComponent fixture;
            fixture.Shape.halfExtents=Vec3f(.5f);
            fixture.Property=properties;
            entity.AddOrReplaceComponent<BoxFixture3DComponent>(fixture);
            if(auto visual=CreateRenderable(entity,assets.boxMesh,
                body.key==AngularBodyKey::Floor?assets.floorMaterial:assets.boxMaterial,"Box");!visual)return visual;
        }
    }
    if(auto helpers=CreateEnvironmentHelpers();!helpers)return helpers;
    m_AxisEntity.AddOrReplaceComponent<VisibilityComponent>(VisibilityComponent{false});
    if(auto started=StartSceneRuntime(authoring);!started)return started;
    auto* physics=m_ActiveScene->GetPhysicsSystem();
    auto* world=physics?physics->GetPhysicsWorld():nullptr;
    if(!world)return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Angular scene","Physics world unavailable"});
    world->SetGravity(packet.gravity);
    for(std::size_t i=0;i<packet.bodyCount;++i)
    {
        auto handle=m_ActiveScene->GetBodyBinding(_Entity{m_AngularBindings.entities[i],m_ActiveScene.get()});
        if(!handle)return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Angular scene","Body binding unavailable"});
        m_AngularBindings.bodies[i]=*handle;
        const auto& body=packet.bodies[i];
        if(body.linearDamping!=0.f || body.angularDamping!=0.f) {
            auto properties=world->ReadBodyProperties(*handle);
            if(!properties)return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Angular scene","Body properties unavailable"});
            properties->linearDamping=body.linearDamping;
            properties->angularDamping=body.angularDamping;
            if(!world->UpdateBodyProperties(*handle,*properties))
                return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Angular scene","Provider rejected damping"});
        }
    }
    JointDescriptor joint;
    joint.kind=packet.scene==Rbs::RbsScenePreset::CvFree?JointKind::ConstantVelocity:
        packet.scene==Rbs::RbsScenePreset::CvLimited?JointKind::LimitedConstantVelocity:JointKind::Hinge;
    joint.bodyA=m_AngularBindings.bodies[0];joint.bodyB=m_AngularBindings.bodies[1];
    joint.localAnchorA=packet.joint.localAnchorA;joint.localAnchorB=packet.joint.localAnchorB;
    joint.collideConnected=packet.joint.collideConnected;
    joint.angular.localFrameA=packet.joint.localFrameA;
    joint.angular.localFrameB=packet.joint.localFrameB;
    joint.angular.lower=packet.joint.lower;joint.angular.upper=packet.joint.upper;
    joint.angular.halfAngleU=packet.joint.halfAngleU;
    joint.angular.halfAngleV=packet.joint.halfAngleV;
    joint.angular.limitsEnabled=packet.joint.limitsEnabled;
    joint.angular.motor=packet.joint.motor;
    auto created=world->CreateJoint(joint);
    if(!created)return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Angular scene","Typed angular joint creation rejected"});
    m_AngularBindings.joint=*created;
    if(authoring)
        if(auto restored=authoring->ApplyAfterStart(*m_ActiveScene);!restored)return restored;
    if(auto readback=VerifyAngularCandidate(packet,authoring);!readback)return readback;
    m_ChainImpulse.Bind(m_AngularBindings.bodies[1],true);
    const auto target=m_AngularBindings.bodies[1];
    const auto actions=packet.scheduledImpulses;
    physics->SetBeforeStep([this,physics,target,actions,motor=packet.joint.motor.has_value()](PhysicsWorld& w,std::uint64_t tick){
        if(physics->HasJointFailure())
        {FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","AngularJoint","Update","Phase28","Joint numerical failure"});return;}
        bool appOwnsActions=true;
#ifdef GENGINE_PHASE28_QUALIFICATION
        appOwnsActions=!Rbs::Phase28AngularOwnsActions();
#endif
        if(!motor && appOwnsActions && ((tick==60 && !w.ApplyLinearImpulse(target,actions[0])) ||
           (tick==300 && !w.ApplyLinearImpulse(target,actions[1]))))
        {FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","AngularJoint","Impulse","Phase28","Scheduled impulse rejected"});return;}
        m_ChainImpulse.Consume(w,m_ActiveScene->IsPaused());
    });
    const Vec3f pivot=packet.bodies[0].center+packet.bodies[0].orientation*packet.joint.localAnchorA;
    const float yaw=packet.scene==Rbs::RbsScenePreset::HingeBasic?-.42f:
        packet.scene==Rbs::RbsScenePreset::HingeQuaternion?-.26f:
        packet.scene==Rbs::RbsScenePreset::HingeLimited?-.10f:
        packet.scene==Rbs::RbsScenePreset::CvFree?.12f:.30f;
    FocusMaterialCamera(m_EditorCamera_,pivot-packet.bodies[1].orientation*Vec3f{0,packet.bodies[1].halfExtents.y,0},.18f,yaw,11.f);
    m_IsShowDebugBoundingBox=false;m_ShowSelectedColliderOutlines=false;
    if(auto handlers=ConnectApplicationEventHandlers();!handlers)return handlers;
    const char* variant=packet.scene==Rbs::RbsScenePreset::MotorContact ? "contact" :
        packet.scene==Rbs::RbsScenePreset::MotorLimit ? "limit" : packet.scene==Rbs::RbsScenePreset::MotorFree ? "free" :
        packet.variant==Rbs::AngularVariant::Contact ? "contact" : "air";
    Log::GetCoreLogger()->info("RBS angular scene={} variant={} backend={} bodies={} joints={} fixedHz=60 environment={}",
        Rbs::SceneName(packet.scene),variant,
        PhysicsBackendName(m_Backend),world->GetBodyCount(),world->GetJointCount(),
        SandboxEnabled()?"sandbox-v1":"frozen-book-fixture");
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::VerifyAngularCandidate(const Rbs::AngularPacket& packet,const BackendAuthoring* authoring) const
{
    auto* physics=m_ActiveScene?m_ActiveScene->GetPhysicsSystem():nullptr;
    const auto* world=physics?physics->GetPhysicsWorld():nullptr;
    const auto failure=[](std::string_view why)->ApplicationInitializationResult
    {return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Angular readback",why});};
    const auto room=Rbs::SandboxEnvironment::Boxes();
    const bool sandbox=SandboxEnabled();
    const std::size_t expectedBodyCount=sandbox ? room.size()+2+
        (packet.scene==Rbs::RbsScenePreset::MotorContact ? 1 : 0) : packet.bodyCount;
    if(!world || world->GetBackend()!=m_Backend || m_AngularBindings.bodyCount!=packet.bodyCount ||
        world->GetBodyCount()!=expectedBodyCount || world->GetJointCount()!=1 ||
        world->GetGravity()!=packet.gravity || m_ActiveScene->GetPhysicsStep().count()!=packet.fixedStepSeconds)
        return failure("World/body/joint/clock readback mismatch");
    for(std::size_t i=0;i<packet.bodyCount+(sandbox?room.size():0);++i)
    {
        Rbs::AngularBodyPacket expected;
        PhysicsBodyHandle handle;
        if(i<packet.bodyCount)
        {
            expected=packet.bodies[i];
            if(sandbox && expected.key==Rbs::AngularBodyKey::Floor)continue;
            handle=m_AngularBindings.bodies[i];
            if (authoring && expected.key==Rbs::AngularBodyKey::Load) {
                const auto id=m_AngularBindings.entities[i];
                const auto found=std::find_if(authoring->m_Bodies.begin(),authoring->m_Bodies.end(),
                    [&](const auto& b){return authoring->Rebound(b.m_FixtureEntity)==id;});
                if (found==authoring->m_Bodies.end()) return failure("Contact sphere lost its committed body");
                continue; // ApplyAfterStart verified this independently authored body against the provider.
            }
        }
        else
        {
            const auto index=i-packet.bodyCount;
            const auto& box=room[index];
            expected.key=Rbs::AngularBodyKey::Floor;
            expected.center={box.center[0],box.center[1],box.center[2]};
            expected.halfExtents=.5f*Vec3f{box.size[0],box.size[1],box.size[2]};
            expected.restitution=.5f; // Existing common Sandbox material.
            auto binding=m_ActiveScene->GetBodyBinding(
                _Entity{m_AngularBindings.environmentEntities[index],m_ActiveScene.get()});
            if(!binding)return failure("Shared Sandbox body binding unavailable");
            handle=*binding;
            if(index==0 && packet.variant==Rbs::AngularVariant::Contact &&
                (m_AngularBindings.entities[2]!=m_AngularBindings.environmentEntities[0] ||
                 m_AngularBindings.bodies[2]!=handle))
                return failure("Contact must bind the one shared Sandbox Floor");
            if(authoring)
            {
                const auto entity=m_AngularBindings.environmentEntities[index];
                const auto matching=std::find_if(authoring->m_Bodies.begin(),authoring->m_Bodies.end(),
                    [&](const auto& body){return authoring->Rebound(body.m_FixtureEntity)==entity;});
                if(matching==authoring->m_Bodies.end())return failure("Shared Sandbox object lacks its verified committed body");
                // ApplyAfterStart has compared the actual provider geometry, pose and properties
                // against this committed body. Fresh presets use the original room checks below.
                continue;
            }
        }
        const bool dynamic=expected.key==Rbs::AngularBodyKey::Bar || expected.key==Rbs::AngularBodyKey::Load;
        auto state=world->ReadEffectiveBody(handle);
        auto properties=world->ReadBodyProperties(handle);
        const auto finiteMatrix=[](const Mat3& value){
            for(int col=0;col<3;++col)for(int row=0;row<3;++row)
                if(!std::isfinite(value[col][row]))return false;
            return true;
        };
        if(!state || !properties || state->shapeType!=(expected.sphere?ShapeType::Sphere:ShapeType::Box) ||
            state->type!=(dynamic?BodyType::Dynamic:expected.kinematic?BodyType::Kinematic:BodyType::Static) ||
            std::abs(properties->mass-expected.authoredMass)>1.e-5f ||
            glm::length(state->position-expected.center)>1.e-4f ||
            glm::length((state->localBounds.maxs-state->localBounds.mins)*.5f-expected.halfExtents)>1.e-4f ||
            !Math::IsFinite(state->orientation) ||
            std::abs(glm::dot(state->orientation,expected.orientation))<1.f-1.e-5f ||
            glm::length(state->linearVelocity-expected.initialLinearVelocity)>1.e-5f ||
            glm::length(state->angularVelocity-expected.initialAngularVelocity)>1.e-5f ||
            !Math::IsFinite(state->centerOfMass) || !finiteMatrix(state->inverseInertiaBody) ||
            !finiteMatrix(state->inverseInertiaWorld) ||
            std::abs(state->material.friction-expected.friction)>1.e-5f ||
            std::abs(state->material.restitution-expected.restitution)>1.e-5f ||
            std::abs(state->inverseMass-(dynamic?1.f/expected.authoredMass:0.f))>1.e-5f ||
            properties->type!=state->type || properties->linearDamping!=expected.linearDamping ||
            properties->angularDamping!=expected.angularDamping || properties->gravityFactor!=1.f ||
            !properties->continuousCollision || !properties->allowSleeping ||
            properties->sleep!=RigidBody3D::SleepSettings{})
            return failure("Authoritative body pose/velocity/inertia/material/policy readback mismatch");
    }
    auto actual=world->ReadJoint(m_AngularBindings.joint);
    const auto kind=packet.scene==Rbs::RbsScenePreset::CvFree?JointKind::ConstantVelocity:
        packet.scene==Rbs::RbsScenePreset::CvLimited?JointKind::LimitedConstantVelocity:JointKind::Hinge;
    const auto sameFrame=[](const Quat& provider,const Quat& authored){
        // A unit quaternion and its negation encode the same frame. Compare
        // the actual basis, allowing only Float32 reconstruction roundoff.
        constexpr float tolerance=8.f*std::numeric_limits<float>::epsilon();
        if(!Math::IsFinite(provider) || !Math::IsFinite(authored) ||
            std::abs(glm::length(provider)-1.f)>tolerance ||
            std::abs(glm::length(authored)-1.f)>tolerance)return false;
        for(const Vec3f basis:{Vec3f{1.f,0.f,0.f},Vec3f{0.f,1.f,0.f},Vec3f{0.f,0.f,1.f}})
        {
            const Vec3f delta=provider*basis-authored*basis;
            if(!Math::IsFinite(delta) || std::abs(delta.x)>tolerance ||
                std::abs(delta.y)>tolerance || std::abs(delta.z)>tolerance)return false;
        }
        return true;
    };
    if(!actual || actual->descriptor.kind!=kind || actual->descriptor.bodyA!=m_AngularBindings.bodies[0] ||
        actual->descriptor.bodyB!=m_AngularBindings.bodies[1] ||
        !Math::IsFinite(actual->descriptor.localAnchorA) ||
        !Math::IsFinite(actual->descriptor.localAnchorB) ||
        glm::length(actual->descriptor.localAnchorA-packet.joint.localAnchorA)>1.e-5f ||
        glm::length(actual->descriptor.localAnchorB-packet.joint.localAnchorB)>1.e-5f ||
        actual->descriptor.collideConnected!=packet.joint.collideConnected ||
        !sameFrame(actual->descriptor.angular.localFrameA,packet.joint.localFrameA) ||
        !sameFrame(actual->descriptor.angular.localFrameB,packet.joint.localFrameB) ||
        actual->descriptor.angular.lower!=packet.joint.lower ||
        actual->descriptor.angular.upper!=packet.joint.upper ||
        actual->descriptor.angular.halfAngleU!=packet.joint.halfAngleU ||
        actual->descriptor.angular.halfAngleV!=packet.joint.halfAngleV ||
        actual->descriptor.angular.motor!=packet.joint.motor ||
        actual->descriptor.angular.limitsEnabled!=packet.joint.limitsEnabled)
        return failure("Authoritative angular descriptor readback mismatch");
    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateSphereDiamond(const Rbs::SceneAssets& assets,
                                            RigidBody3DComponent& rigidBodyComp,
                                            SphereFixture3DComponent& sphereFixtureComp)
{
    sphereFixtureComp.Property.m_LinearVelocity = {-80.f, 0.f, 0.f};
    auto createdSceneEntity3 = CreatePresetEntity(19,"wood_sphere_0");
    if (!createdSceneEntity3)
        return std::unexpected(createdSceneEntity3.error());
    _Entity woodSphereEntity = *createdSceneEntity3;

    woodSphereEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{30.f, 5.0f, 0.f});

    sphereFixtureComp.Property.m_Position =
        woodSphereEntity.GetComponent<Transform3DComponent>().Translation;
    sphereFixtureComp.Property.m_Orientation =
        woodSphereEntity.GetComponent<Transform3DComponent>().QuatRotation;
    woodSphereEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
    woodSphereEntity.AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixtureComp);
    if (auto authored =
            CreateRenderable(woodSphereEntity, assets.smoothSphereGeo, assets.sphereMaterial, "");
        !authored)
        return authored;

    ConvexFixture3DComponent convexFixtureComp;
    // Same historical CPU point generator, owned directly by the collider fixture.
    // Visual replacement never changes these points.
    convexFixtureComp.Shape.points = Math::GenerateDiamond();

    convexFixtureComp.Property.m_Elasticity = 0.5f;
    convexFixtureComp.Property.m_Friction = 0.5f;
    convexFixtureComp.Property.m_InvMass = 1.f;
    convexFixtureComp.Property.m_AngularVelocity = {5.f, 0.f, 5.f};
    convexFixtureComp.Property.m_LinearVelocity = {100.f, 0.f, 0.f};
    auto createdSceneEntity4 = CreatePresetEntity(20,"Diamond");
    if (!createdSceneEntity4)
        return std::unexpected(createdSceneEntity4.error());
    _Entity DiamondEntity = *createdSceneEntity4;
    DiamondEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{-30, 5.f, 0});

    convexFixtureComp.Property.m_Position =
        DiamondEntity.GetComponent<Transform3DComponent>().Translation;
    convexFixtureComp.Property.m_Orientation =
        DiamondEntity.GetComponent<Transform3DComponent>().QuatRotation;
    DiamondEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
    DiamondEntity.AddOrReplaceComponent<ConvexFixture3DComponent>(convexFixtureComp);
    if (auto authored =
            CreateRenderable(DiamondEntity, assets.DiamondGeo, assets.sphereMaterial, "Diamond");
        !authored)
        return authored;

    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateSphereLattice(const Rbs::SceneAssets& assets,
                                            RigidBody3DComponent& rigidBodyComp,
                                            SphereFixture3DComponent& sphereFixtureComp)
{
    sphereFixtureComp.Property.m_LinearVelocity = {0.f, 0.f, 0.f};
    int i = 0;
    _Entity spherePrototype;

    for (int z = 1; z < 6; z++)
    {
        for (int x = 0; x < 6; x++)
        {
            for (int y = 0; y < 6; y++)
            {
                float yy = float(z - 1) * sphereFixtureComp.Radius * 2.f;
                float xx = float(x - 1) * sphereFixtureComp.Radius * 2.f;
                float zz = float(y - 1) * sphereFixtureComp.Radius * 2.f;
                auto createdSceneEntity5 = spherePrototype
                                               ? spherePrototype.Duplicate()
                                               : CreatePresetEntity(21,"wood_sphere");
                if (!createdSceneEntity5)
                    return std::unexpected(createdSceneEntity5.error());
                _Entity woodSphereEntity = *createdSceneEntity5;
                RegisterPresetEntity(woodSphereEntity,Rbs::Editor::SphereLatticeSphereRole);
                woodSphereEntity.Name() = "wood_sphere" + std::to_string(i++);
                if (!spherePrototype)
                    spherePrototype = woodSphereEntity;
                woodSphereEntity.AddOrReplaceComponent<Transform3DComponent>(
                    Vec3f{xx, 10.f + yy, zz});

                sphereFixtureComp.Property.m_Position =
                    woodSphereEntity.GetComponent<Transform3DComponent>().Translation;
                sphereFixtureComp.Property.m_Orientation =
                    woodSphereEntity.GetComponent<Transform3DComponent>().QuatRotation;
                woodSphereEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
                woodSphereEntity.AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixtureComp);
                if (auto authored = CreateRenderable(woodSphereEntity, assets.smoothSphereGeo,
                                                     assets.sphereMaterial, "");
                    !authored)
                    return authored;
            }
        }
    }
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::CreateBoxStack(
    const Rbs::SceneAssets& assets, RigidBody3DComponent& rigidBodyComp,
    SphereFixture3DComponent& sphereFixtureComp, BoxFixture3DComponent& boxFixtureComp)
{
    float offset = 2.f;

    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {

            auto createdSceneEntity6 = CreatePresetEntity(22,"wood_box");
            if (!createdSceneEntity6)
                return std::unexpected(createdSceneEntity6.error());
            _Entity woodBoxEntity = *createdSceneEntity6;

            woodBoxEntity.AddOrReplaceComponent<Transform3DComponent>(
                Vec3f{x * (offset + 0.01f), 1.5f + y * offset, 0.f}, Vec3f{0.f}, Vec3f{2.f});

            boxFixtureComp.Property.m_Position =
                woodBoxEntity.GetComponent<Transform3DComponent>().Translation;
            boxFixtureComp.Property.m_Orientation =
                woodBoxEntity.GetComponent<Transform3DComponent>().QuatRotation;
            woodBoxEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
            woodBoxEntity.AddOrReplaceComponent<BoxFixture3DComponent>(boxFixtureComp);
            if (auto authored =
                    CreateRenderable(woodBoxEntity, assets.boxMesh, assets.boxMaterial, "Box");
                !authored)
                return authored;
        }
    }
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::CreateSphereBoxStack(
    const Rbs::SceneAssets& assets, RigidBody3DComponent& rigidBodyComp,
    SphereFixture3DComponent& sphereFixtureComp, BoxFixture3DComponent& boxFixtureComp)
{
    float offset = 2.f;

    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {

            auto createdSceneEntity7 = Rbs::OrdinaryHighlightFixture::Create(*m_ActiveScene,"wood_box","box_"+std::to_string(y)+"_"+std::to_string(x));
            if (!createdSceneEntity7)
                return std::unexpected(createdSceneEntity7.error());
            _Entity woodBoxEntity = *createdSceneEntity7;
    RegisterPresetEntity(woodBoxEntity,23);

            woodBoxEntity.AddOrReplaceComponent<Transform3DComponent>(
                Vec3f{x * (offset + 0.01f), 1.5f + y * offset, 0.f}, Vec3f{0.f}, Vec3f{2.f});

            boxFixtureComp.Property.m_Position =
                woodBoxEntity.GetComponent<Transform3DComponent>().Translation;
            boxFixtureComp.Property.m_Orientation =
                woodBoxEntity.GetComponent<Transform3DComponent>().QuatRotation;
            woodBoxEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
            woodBoxEntity.AddOrReplaceComponent<BoxFixture3DComponent>(boxFixtureComp);
            if (auto authored =
                    CreateRenderable(woodBoxEntity, assets.boxMesh, assets.boxMaterial, "Box");
                !authored)
                return authored;
        }
    }

    sphereFixtureComp.Property.m_LinearVelocity = {0.f, 0.f, 40.f};
    auto createdSceneEntity8 = Rbs::OrdinaryHighlightFixture::Create(*m_ActiveScene,"wood_sphere_0","wood_sphere_0");
    if (!createdSceneEntity8)
        return std::unexpected(createdSceneEntity8.error());
    _Entity woodSphereEntity = *createdSceneEntity8;
    RegisterPresetEntity(woodSphereEntity,24);

    woodSphereEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{3.5f, 5.0f, -20.f});

    sphereFixtureComp.Property.m_Position =
        woodSphereEntity.GetComponent<Transform3DComponent>().Translation;
    sphereFixtureComp.Property.m_Orientation =
        woodSphereEntity.GetComponent<Transform3DComponent>().QuatRotation;
    woodSphereEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
    woodSphereEntity.AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixtureComp);
    if (auto authored =
            CreateRenderable(woodSphereEntity, assets.smoothSphereGeo, assets.sphereMaterial, "");
        !authored)
        return authored;

    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::BindDistanceCommands(PhysicsBodyHandle tip, bool contact)
{
    auto* physics=m_ActiveScene->GetPhysicsSystem();
        m_ChainImpulse.Bind(tip,
#ifdef GENGINE_PHASE26_QUALIFICATION
            false
#else
            true
#endif
        );
#ifdef GENGINE_PHASE26_QUALIFICATION
        if (!Rbs::DistanceChainImpulseChecks::QualificationIsolation(*this))
            return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"DistanceChain","Manual impulse isolation failed"});
#endif
        physics->SetBeforeStep([this,physics,tip,contact](PhysicsWorld& w,std::uint64_t tick) {
            if(physics->HasJointFailure()) {
                FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","DistanceJoint","Update","Phase26","Joint numerical failure"}); return;
            }
#ifdef GENGINE_PHASE26_QUALIFICATION
            if(tick==300)Rbs::Phase26::CaptureWork(w,tip,contact?Vec3f(0,0,.5f):Vec3f(.5f,0,0));
#endif
            if(tick==300 && !w.ApplyLinearImpulse(tip,contact?Vec3f(0,0,.5f):Vec3f(.5f,0,0)))
                FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","DistanceJoint","ApplyImpulse","Phase26","Commanded impulse rejected"});
#ifndef GENGINE_PHASE26_QUALIFICATION
#ifdef GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS
            Rbs::DistanceChainImpulseChecks::BeforeBoundary(*this);
#endif
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
            Rbs::SandboxChecks::BeforeBoundary(*this);
#endif
            m_ChainImpulse.Consume(w, m_ActiveScene->IsPaused());
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
            Rbs::SandboxChecks::AfterBoundary(*this);
#endif
#ifdef GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS
            Rbs::DistanceChainImpulseChecks::AfterBoundary(*this);
#endif
#endif
        });
    return {};
}
