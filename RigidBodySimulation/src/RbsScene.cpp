#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Scene/_Scene.h"
#include <format>

#include "Managers/AssetsManager.h"
#include "Physics/PhysicsWorld.h"
#include <glm/gtx/quaternion.hpp>

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;

namespace
{
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
    if (!physicsShape.empty())
    {
        auto attached = m_SceneResources->AttachPhysicsShape(entity, physicsShape);
        if (!attached)
            return SceneFailure(attached.error());
    }
    return {};
}

std::expected<Asset::Texture*, Asset::TextureError>
RigidBodySimulationApp::LoadSceneTexture(const std::string& path, const std::string& uniform,
                                         const std::string& extension, const TextureDesc& desc)
{
    auto texture = AssetsManager::GetTextureOrFallback(path, uniform, extension, desc);
    if (!texture)
    {
        Log::GetCoreLogger()->error("Texture {}: {}", texture.error().source,
                                    texture.error().message);
        m_Running = false;
    }
    return texture;
}

ApplicationInitializationResult RigidBodySimulationApp::InitializeSceneRoot()
{
    m_FarPlane = 100;
    m_EditorScene = CreateRefPtr<_Scene>();
    m_ActiveScene = m_EditorScene;
    m_EditorCamera_ = _EditorCamera(45.0f, 1280.f, 720.f, 0.1f, 1000.f);
    float cameraFarClip = m_EditorCamera_.GetFarClip();
    m_ShadowCascadeLevels = {cameraFarClip / 50.f, cameraFarClip / 25.f, cameraFarClip / 10.f,
                             cameraFarClip / 2.f, cameraFarClip};
    auto createdSceneEntity0 = m_ActiveScene->CreateEntity("Editor frame camera");
    if (!createdSceneEntity0)
        return std::unexpected(createdSceneEntity0.error());
    m_FrameCameraEntity = *createdSceneEntity0;
    m_FrameCameraEntity.AddOrReplaceComponent<RenderCameraComponent>();
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
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::CreateSceneLights(Rbs::SceneAssets& assets)
{
    auto createdSceneEntity1 = m_ActiveScene->CreateEntity("ambient_light");
    if (!createdSceneEntity1)
        return std::unexpected(createdSceneEntity1.error());
    auto ambientLightEntity = *createdSceneEntity1;
    auto createdSceneEntity2 = m_ActiveScene->CreateEntity("point_light");
    if (!createdSceneEntity2)
        return std::unexpected(createdSceneEntity2.error());
    m_PointLightEntity = *createdSceneEntity2;
    m_LightDirection = glm::normalize(Vec3f{20, 50, 20});
    m_LightPos = {0, 15, -10};
    RenderLightComponent direction;
    direction.color = {.7f, .7f, .7f};
    direction.castShadows = true;
    ambientLightEntity.AddOrReplaceComponent<RenderLightComponent>(direction);
    auto& lightTransform = ambientLightEntity.GetComponent<Transform3DComponent>();
    lightTransform.QuatRotation = glm::rotation(Vec3f{0, 0, -1}, -m_LightDirection);
    RenderLightComponent point;
    point.kind = RenderLightKind::Point;
    point.color = {.8f, .2f, .1f};
    point.range = m_FarPlane;
    point.castShadows = true;
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
    RigidBody3DComponent rigidBodyComp;
    rigidBodyComp.Type = BodyType::Dynamic;

    SphereFixture3DComponent sphereFixtureComp;

    sphereFixtureComp.Radius = 1.f;
    sphereFixtureComp.Property.m_Elasticity = 0.5f;
    sphereFixtureComp.Property.m_Friction = 0.5f;
    sphereFixtureComp.Property.m_InvMass = 1.f;

    BoxFixture3DComponent boxFixtureComp;
    boxFixtureComp.Property.m_InvMass = 1.f;
    boxFixtureComp.Property.m_Friction = 0.5f;
    boxFixtureComp.Property.m_Elasticity = 0.5f;

    switch (m_Launch.scene)
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
    auto createdSceneEntity9 = m_ActiveScene->CreateEntity("grid");
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
    auto createdSceneEntity10 = m_ActiveScene->CreateEntity("axis");
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
    auto createdSceneEntity11 = m_ActiveScene->CreateEntity("Environment_SkyBox");
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
RigidBodySimulationApp::CreateGroundAndWalls(const Rbs::SceneAssets& assets)
{
    RigidBody3DComponent rigidBodyComp;
    rigidBodyComp.Type = BodyType::Static;

    auto createdSceneEntity12 = m_ActiveScene->CreateEntity("wood_plane");
    if (!createdSceneEntity12)
        return std::unexpected(createdSceneEntity12.error());
    _Entity planeEntity = *createdSceneEntity12;
    const auto planeGeo = assets.boxMesh;
    BoxFixture3DComponent planeFixtureComp;
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

    auto createdSceneEntity13 = m_ActiveScene->CreateEntity("wall_entity_1");
    if (!createdSceneEntity13)
        return std::unexpected(createdSceneEntity13.error());
    _Entity wallEntity_1 = *createdSceneEntity13;
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

    auto createdSceneEntity14 = m_ActiveScene->CreateEntity("wall_entity_2");
    if (!createdSceneEntity14)
        return std::unexpected(createdSceneEntity14.error());
    _Entity wallEntity_2 = *createdSceneEntity14;
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

    auto createdSceneEntity15 = m_ActiveScene->CreateEntity("wall_entity_3");
    if (!createdSceneEntity15)
        return std::unexpected(createdSceneEntity15.error());
    _Entity wallEntity_3 = *createdSceneEntity15;
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

    auto createdSceneEntity16 = m_ActiveScene->CreateEntity("wall_entity_4");
    if (!createdSceneEntity16)
        return std::unexpected(createdSceneEntity16.error());
    _Entity wallEntity_4 = *createdSceneEntity16;
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
    if (m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery)
    {
        // Render-only template examples; transform scale remains independent of dimensions.
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
            auto created = m_ActiveScene->CreateEntity(fixture.name);
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
                auto created = m_ActiveScene->CreateEntity(
                    std::format("{} {}", curved[family].name, row == 0 ? "Smooth" : "Flat"));
                if (!created)
                    return std::unexpected(created.error());
                created->AddOrReplaceComponent<Transform3DComponent>(
                    Vec3f{-12.f + 4.f * static_cast<float>(family), 3.f, row == 0 ? -10.f : -14.f},
                    Vec3f{}, Vec3f{1});
                if (auto authored = CreateRenderable(*created, *mesh, assets.sphereMaterial);
                    !authored)
                    return authored;
                ++assets.galleryFixtures;
            }
    }
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::ParametricGeometry))
    {
        if (assets.normalPhysicsBodies !=
                m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>().size() ||
            assets.galleryFixtures !=
                ((m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery) ? 15u : 0u))
            return std::unexpected(
                PlatformError{PlatformErrorCode::Initialization, "Phase 07 gallery",
                              "Gallery altered Physics membership or fixture count"});
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_07_GALLERY_PASS enabled={} fixtures={} physics_bodies={}",
            (m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery), assets.galleryFixtures,
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
    auto authoringSource = m_ActiveScene->CreateEntity("Phase08 Source");
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
    authoringDuplicate->Name() = "Phase08 Unique Bake";
    authoringDuplicate->Transform().SetTranslation({0, 2, 6});
    if (auto dimensions = m_SceneResources->SetDimensions(*authoringSource, {4, 6, 8}); !dimensions)
        return SceneFailure(dimensions.error());
    if (auto unique = m_SceneResources->MakeGeometryUnique(*authoringDuplicate); !unique)
        return SceneFailure(unique.error());
    authoringDuplicate->Transform().SetScale({2, 1, .5f});
    if (auto baked = m_SceneResources->BakeGeometry(*authoringDuplicate); !baked)
        return SceneFailure(baked.error());
    auto regenerated = m_ActiveScene->CreateEntity("Phase08 Regenerated");
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
        if (!(m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery) ||
            assets.galleryFixtures != 15u ||
            assets.normalPhysicsBodies !=
                m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>().size())
            return std::unexpected(PlatformError{
                PlatformErrorCode::Initialization, "Phase 08 gallery preservation",
                "Authoring preset requires the existing gallery and Physics membership"});
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_08_GALLERY_PASS enabled={} fixtures={} physics_bodies={}",
            (m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery), assets.galleryFixtures,
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
    auto galleryParent = m_ActiveScene->CreateEntity("Material mapping transform");
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
            auto entity = m_ActiveScene->CreateEntity(
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
        auto entity = m_ActiveScene->CreateEntity(std::format("Typed material kind {}", i));
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
    auto customObject = m_ActiveScene->CreateEntity("Phase10 Custom Shader");
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
    auto cloneObject = m_ActiveScene->CreateEntity("Phase11 Custom Shader Clone");
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

ApplicationInitializationResult RigidBodySimulationApp::StartSceneRuntime()
{
    // Reserve before the Scene hands successful runtime shape ownership to this caller.
    m_PhysicsShapes.reserve(m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>().size());
    if (auto started = m_ActiveScene->OnRuntimeStart(); !started)
        return std::unexpected(started.error());
    for (auto* body : m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies())
        m_PhysicsShapes.emplace_back(body->m_Shape);

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
        auto entity = m_ActiveScene->CreateEntity(std::format("Async texture {}", i));
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

ApplicationInitializationResult RigidBodySimulationApp::InitializeScene()
{
    Rbs::SceneAssets assets;
    if (auto result = InitializeSceneRoot(); !result)
        return result;
    if (auto result = PublishSceneMeshes(assets); !result)
        return result;
    if (auto result = LoadSceneIcons(); !result)
        return result;
    if (auto result = LoadSurfaceTextures(assets); !result)
        return result;
    if (auto result = CreateSceneMaterials(assets); !result)
        return result;
    if (auto result = CreateSceneLights(assets); !result)
        return result;
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
    if (m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery)
    {
        if (auto result = CreateGeometryExamples(assets); !result)
            return result;
        if (auto result = CreateMaterialGallery(assets); !result)
            return result;
        if (auto result = CreateShaderGallery(assets); !result)
            return result;
    }
    if (auto result = StartSceneRuntime(); !result)
        return result;
    if (auto result = ValidateRuntimeScene(assets); !result)
        return result;
    if (auto result = ConnectApplicationEventHandlers(); !result)
        return result;
    if (auto result = ValidateScaleBridge(assets); !result)
        return result;
    if (m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery)
        if (auto result = CreateAsyncExamples(assets); !result)
            return result;
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if (auto result = ValidatePhysicsScene(assets); !result)
        return result;
    if (auto result = ValidateSceneMembership(); !result)
        return result;
    // Exercise real UI/resource frames without stepping the fixture oracle.
    m_IsPause = true;
#endif
    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::CreateSphereDiamond(const Rbs::SceneAssets& assets,
                                            RigidBody3DComponent& rigidBodyComp,
                                            SphereFixture3DComponent& sphereFixtureComp)
{
    sphereFixtureComp.Property.m_LinearVelocity = {-80.f, 0.f, 0.f};
    auto createdSceneEntity3 = m_ActiveScene->CreateEntity("wood_sphere_0");
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

    convexFixtureComp.Property.m_Elasticity = 0.5f;
    convexFixtureComp.Property.m_Friction = 0.5f;
    convexFixtureComp.Property.m_InvMass = 1.f;
    convexFixtureComp.Property.m_AngularVelocity = {5.f, 0.f, 5.f};
    convexFixtureComp.Property.m_LinearVelocity = {100.f, 0.f, 0.f};
    auto createdSceneEntity4 = m_ActiveScene->CreateEntity("Diamond");
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
    static int i = 0;
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
                                               : m_ActiveScene->CreateEntity("wood_sphere");
                if (!createdSceneEntity5)
                    return std::unexpected(createdSceneEntity5.error());
                _Entity woodSphereEntity = *createdSceneEntity5;
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

            auto createdSceneEntity6 = m_ActiveScene->CreateEntity("wood_box");
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

            auto createdSceneEntity7 = m_ActiveScene->CreateEntity("wood_box");
            if (!createdSceneEntity7)
                return std::unexpected(createdSceneEntity7.error());
            _Entity woodBoxEntity = *createdSceneEntity7;

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
    auto createdSceneEntity8 = m_ActiveScene->CreateEntity("wood_sphere_0");
    if (!createdSceneEntity8)
        return std::unexpected(createdSceneEntity8.error());
    _Entity woodSphereEntity = *createdSceneEntity8;

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
