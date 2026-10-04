#include "Renderer/FrameScheduler.h"
#include "UI/FramebufferImage.h"
#include "RigidBodySimulation.h"
#include "Core/RuntimeAssets.h"
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include "EntryPoint.h"
#include "Renderer/RenderExtraction.h"
#include "Managers/AssetsManager.h"
#include <glm/gtx/quaternion.hpp>
#include <format>
#include <Core/Log.h>
#include <imgui/imgui.h>
#include "Assets/Textures/Texture.h"

#include "Core/Window.h"
#include "../tests/SceneAuthoringChecks.h"
#include "../tests/ResourceOwnershipChecks.h"
#include "../tests/GeometryTemplateChecks.h"
#include "../tests/ParametricGeometryChecks.h"
#include "../tests/GeometryAuthoringChecks.h"
#include "../tests/MaterialAuthoringChecks.h"

#include <Physics/ShapeBox.h>
#include <Physics/PhysicsWorld.h>
#include <Physics/GJK.h>
#include <print>

using namespace GEngine;
using namespace ::GEngine::Asset;

#ifndef activate_boxes_stacking
#define activate_boxes_stacking 0
#endif
#ifndef activate_sphere_lattice
#define activate_sphere_lattice 0
#endif
#ifndef activate_sphere_diamond
#define activate_sphere_diamond 0
#endif
#ifndef activate_sphere_boxes_stacking
#define activate_sphere_boxes_stacking 0
#endif

#ifndef GENGINE_RBS_GEOMETRY_TEMPLATE_GALLERY
#define GENGINE_RBS_GEOMETRY_TEMPLATE_GALLERY 1
#endif

namespace
{
    struct MaterialGallery
    {
        std::array<_Entity, 6> uv, triplanar;
        _Entity parent;
        MaterialHandle uvMaterial, triplanarMaterial;
        int comparison = 0, inspectedGeometry = 0;
        bool animate = false;
        float time = 0;
        bool presentationRecorded = false;
        unsigned validationFrames = 0;
        std::chrono::steady_clock::time_point validationStart{};
        int validationStage = -1, validationSample = -1, recordedSample = -1;
        bool captureReady = false;
    } materialGallery;

    void FocusMaterialCamera(Camera::_EditorCamera& camera, const Math::Vec3f& focal,
                             float pitch, float yaw, float distance)
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

    void SetMaterialComparison(int comparison)
    {
        materialGallery.comparison = comparison;
        for (std::size_t i = 0; i < materialGallery.uv.size(); ++i)
        {
            materialGallery.uv[i].AddOrReplaceComponent<Component::VisibilityComponent>(
                Component::VisibilityComponent{comparison != 2});
            materialGallery.triplanar[i].AddOrReplaceComponent<Component::VisibilityComponent>(
                Component::VisibilityComponent{comparison != 1});
            materialGallery.triplanar[i].Transform().SetTranslation({-10.f + 4.f * static_cast<float>(i), 2.f,
                comparison == 0 ? -24.f : -20.f});
        }
    }

    std::expected<MaterialHandle, SceneResourceError> CreateSurfaceMaterial(
        SceneRenderResources& resources, const std::array<Asset::Texture*, 5>& images,
        MaterialAuthoringDesc desc)
    {
        for (std::size_t i = 0; i < images.size(); ++i)
        {
            auto sampled = AssetsManager::SampleTexture(images[i]->View());
            if (!sampled)
                return std::visit([](const auto& cause) -> std::expected<MaterialHandle, SceneResourceError>
                    { return std::unexpected(SceneResourceError{"material sampling", cause}); }, sampled.error().cause);
            desc.textures[i] = MaterialTextureValue{sampled->TextureIdentity(), sampled->SamplerIdentity()};
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

RigidBodySimulationApp::~RigidBodySimulationApp()
{
    materialGallery = {};
    if (m_AudioSystem)
        m_AudioSystem->Shutdown();
    if (m_SceneResources)
    {
        if (auto current = GetEngineContext().MakeCurrent(); !current)
        {
            ReportPlatformError(current.error());
            std::terminate();
        }
        m_MeshLoads.reset(); // Cancel/join imports before registry or scene retirement.
        m_ActiveScene.reset();
        m_EditorScene.reset();
        m_FrameSubmission.reset();
        m_SceneResources.reset();
    }
}

ApplicationInitializationResult RigidBodySimulationApp::Initialize(
    const std::initializer_list<WindowProperties>& WindowsPropertyList)
{
    std::size_t cpuMetadataChecks = 0;
    if (std::getenv("GENGINE_PRE_EDITOR_RESOURCE_OWNERSHIP"))
    {
        auto checked = PreEditorValidation::CheckCpuMeshMetadata();
        if (!checked)
            return std::unexpected(PlatformError{
                PlatformErrorCode::Initialization,
                "Phase 05 CPU metadata before platform initialization", checked.error()});
        cpuMetadataChecks = *checked;
    }
    std::size_t cpuGeometryChecks = 0;
    if (std::getenv("GENGINE_PRE_EDITOR_GEOMETRY_TEMPLATES"))
    {
        auto checked = PreEditorValidation::CheckCpuGeometryTemplates();
        if (!checked)
            return std::unexpected(PlatformError{
                PlatformErrorCode::Initialization,
                "Phase 06 CPU geometry before platform initialization", checked.error()});
        cpuGeometryChecks = *checked;
    }
    std::size_t cpuParametricChecks = 0;
    if (std::getenv("GENGINE_PRE_EDITOR_PARAMETRIC_GEOMETRY"))
    {
        auto checked = PreEditorValidation::CheckCpuParametricGeometry();
        if (!checked)
            return std::unexpected(PlatformError{
                PlatformErrorCode::Initialization, "Phase 07 CPU parametric geometry",
                checked.error()});
        cpuParametricChecks = *checked;
    }
    if (auto initialized = BaseApp::Initialize(WindowsPropertyList); !initialized)
        return initialized;
    if (cpuMetadataChecks)
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_05_CPU_PASS checks={} before_platform=true",
                                   cpuMetadataChecks);
    if (cpuGeometryChecks)
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_06_CPU_PASS checks={} before_platform=true",
                                   cpuGeometryChecks);
    if (cpuParametricChecks)
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_07_CPU_PASS checks={} before_platform=true",
                                   cpuParametricChecks);
    m_AudioSystem = CreateScopedPtr<Audio::AudioSystem>();
    if (auto audio = m_AudioSystem->Initialize(); !audio)
        return std::unexpected(audio.error());
    auto failure = [](const SceneResourceError& error) -> ApplicationInitializationResult
    {
        Log::GetCoreLogger()->error("{}", DescribeSceneResourceError(error));
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "scene resources",
                                             DescribeSceneResourceError(error)});
    };
    auto resources = SceneRenderResources::Create(GetEngineContext());
    if (!resources)
        return failure(resources.error());
    m_SceneResources = std::move(*resources);
    auto meshRoot = RuntimeAssets::TryFile("Models");
    if (!meshRoot)
        return std::unexpected(meshRoot.error());
    m_MeshRoot = *meshRoot;
    // Opt-in validation exercises the same action as the File menu.
    m_LoadBarrel = std::getenv("GENGINE_ASYNC_MESH_SMOKE") != nullptr;
    auto submission = FrameSubmission::Create();
    if (!submission)
    {
        Log::GetCoreLogger()->error("{}", DescribeSubmissionError(submission.error()));
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "frame submission",
                                             DescribeSubmissionError(submission.error())});
    }
    m_FrameSubmission.emplace(std::move(*submission));
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
    auto sphereMeshResult = m_SceneResources->PublishShape("Sphere");
    if (!sphereMeshResult)
        return failure(sphereMeshResult.error());
    auto smoothSphereGeo = *sphereMeshResult;
    auto diamondMeshResult = m_SceneResources->PublishShape("Diamond");
    if (!diamondMeshResult)
        return failure(diamondMeshResult.error());
    auto DiamondGeo = *diamondMeshResult;
    auto boxMeshResult = m_SceneResources->PublishGeometry(GeometryTemplates::Cube{});
    if (!boxMeshResult)
        return failure(boxMeshResult.error());
    auto boxMesh = *boxMeshResult;
    auto assignRenderable =
        [&](_Entity entity, const MeshRendererComponent& value) -> ApplicationInitializationResult
    {
        auto assigned = m_SceneResources->AssignRenderable(entity, value);
        if (!assigned)
            return std::unexpected(
                PlatformError{PlatformErrorCode::Initialization, "scene assignment",
                              std::format("Renderable assignment failed: {}",
                                          DescribeSceneResourceError(assigned.error()))});
        return {};
    };
    auto renderable = [&](_Entity entity, Asset::MeshHandle mesh,
                          Asset::MaterialInstanceHandle material,
                          std::string_view physicsShape = {}) -> ApplicationInitializationResult
    {
        if (auto assigned = assignRenderable(entity, {mesh, material}); !assigned)
            return assigned;
        if (!physicsShape.empty())
        {
            auto attached = m_SceneResources->AttachPhysicsShape(entity, physicsShape);
            if (!attached)
                return failure(attached.error());
        }
        return {};
    };

    auto loadTexture =
        [this](const std::string& path, const std::string& uniform = "u_texture",
               const std::string& extension = ".png",
               const TextureDesc& desc = {}) -> std::expected<Asset::Texture*, Asset::TextureError>
    {
        auto texture = AssetsManager::GetTextureOrFallback(path, uniform, extension, desc);
        if (!texture)
        {
            Log::GetCoreLogger()->error("Texture {}: {}", texture.error().source,
                                        texture.error().message);
            m_Running = false;
        }
        return texture;
    };

    auto m_IconPlayResult = loadTexture("Icons/PlayButton");
    if (!m_IconPlayResult)
        return std::unexpected(m_IconPlayResult.error());
    auto* m_IconPlay = *m_IconPlayResult;
    auto m_IconPauseResult = loadTexture("Icons/PauseButton");
    if (!m_IconPauseResult)
        return std::unexpected(m_IconPauseResult.error());
    auto* m_IconPause = *m_IconPauseResult;
    auto m_IconStepResult = loadTexture("Icons/StepButton");
    if (!m_IconStepResult)
        return std::unexpected(m_IconStepResult.error());
    auto* m_IconStep = *m_IconStepResult;
    auto m_IconSimulateResult = loadTexture("Icons/SimulateButton");
    if (!m_IconSimulateResult)
        return std::unexpected(m_IconSimulateResult.error());
    auto* m_IconSimulate = *m_IconSimulateResult;
    auto m_IconStopResult = loadTexture("Icons/StopButton");
    if (!m_IconStopResult)
        return std::unexpected(m_IconStopResult.error());
    auto* m_IconStop = *m_IconStopResult;

    // PBR vectors and scalar data must not use the color texture sRGB transfer.
    TextureDesc dataMapDesc;
    dataMapDesc.colorSpace = TextureColorSpace::Linear;
    auto sphere_albedoResult = loadTexture("PBR/rustediron/rustediron2_basecolor", "albedoMap");
    if (!sphere_albedoResult)
        return std::unexpected(sphere_albedoResult.error());
    auto* sphere_albedo = *sphere_albedoResult;
    auto sphere_normalResult =
        loadTexture("PBR/rustediron/rustediron2_normal", "normalMap", ".png", dataMapDesc);
    if (!sphere_normalResult)
        return std::unexpected(sphere_normalResult.error());
    auto* sphere_normal = *sphere_normalResult;
    auto sphere_metallicResult =
        loadTexture("PBR/rustediron/rustediron2_metallic", "metallicMap", ".png", dataMapDesc);
    if (!sphere_metallicResult)
        return std::unexpected(sphere_metallicResult.error());
    auto* sphere_metallic = *sphere_metallicResult;
    auto sphere_roughnessResult =
        loadTexture("PBR/rustediron/rustediron2_roughness", "roughnessMap", ".png", dataMapDesc);
    if (!sphere_roughnessResult)
        return std::unexpected(sphere_roughnessResult.error());
    auto* sphere_roughness = *sphere_roughnessResult;
    auto sphere_aoResult = loadTexture("PBR/subtle_black_granite/subtle-black-granite_ao", "aoMap",
                                       ".png", dataMapDesc);
    if (!sphere_aoResult)
        return std::unexpected(sphere_aoResult.error());
    auto* sphere_ao = *sphere_aoResult;

    auto floor_albedoResult =
        loadTexture("PBR/base_white_tile/base-white-tile_albedo", "albedoMap");
    if (!floor_albedoResult)
        return std::unexpected(floor_albedoResult.error());
    auto* floor_albedo = *floor_albedoResult;
    auto floor_normalResult = loadTexture("PBR/base_white_tile/base-white-tile_normal-dx",
                                          "normalMap", ".png", dataMapDesc);
    if (!floor_normalResult)
        return std::unexpected(floor_normalResult.error());
    auto* floor_normal = *floor_normalResult;
    auto floor_metallicResult = loadTexture("PBR/base_white_tile/base-white-tile_metallic",
                                            "metallicMap", ".png", dataMapDesc);
    if (!floor_metallicResult)
        return std::unexpected(floor_metallicResult.error());
    auto* floor_metallic = *floor_metallicResult;
    auto floor_roughnessResult = loadTexture("PBR/base_white_tile/base-white-tile_roughness",
                                             "roughnessMap", ".png", dataMapDesc);
    if (!floor_roughnessResult)
        return std::unexpected(floor_roughnessResult.error());
    auto* floor_roughness = *floor_roughnessResult;
    auto floor_aoResult =
        loadTexture("PBR/base_white_tile/base-white-tile_ao", "aoMap", ".png", dataMapDesc);
    if (!floor_aoResult)
        return std::unexpected(floor_aoResult.error());
    auto* floor_ao = *floor_aoResult;

    MaterialAuthoringDesc sphereParameters;
    sphereParameters.roughness = .9f;
    sphereParameters.metallic = 1.f;
    sphereParameters.dielectricReflectance = {.8f, .8f, .8f};
    auto sphereMaterialResult = CreateSurfaceMaterial(
        *m_SceneResources,
        {sphere_albedo, sphere_normal, sphere_metallic, sphere_roughness, sphere_ao},
        sphereParameters);
    if (!sphereMaterialResult)
        return failure(sphereMaterialResult.error());
    auto sphereMaterial = *sphereMaterialResult;
    // Polished floor: retain linear roughness data and author gloss explicitly.
    MaterialAuthoringDesc floorParameters;
    floorParameters.roughness = .3f;
    floorParameters.metallic = 1.f;
    floorParameters.dielectricReflectance = {.08f, .08f, .08f};
    floorParameters.uvTiling = {2, 2};
    auto floorMaterialResult = CreateSurfaceMaterial(
        *m_SceneResources,
        {floor_albedo, floor_normal, floor_metallic, floor_roughness, floor_ao}, floorParameters);
    if (!floorMaterialResult)
        return failure(floorMaterialResult.error());
    auto floorMaterial = *floorMaterialResult;
    auto wallParameters = floorParameters;
    wallParameters.uvTiling = {2, .2f};
    auto wallMaterialResult = CreateSurfaceMaterial(
        *m_SceneResources,
        {floor_albedo, floor_normal, floor_metallic, floor_roughness, floor_ao}, wallParameters);
    if (!wallMaterialResult)
        return failure(wallMaterialResult.error());
    auto wallMaterial = *wallMaterialResult;
    // Keep the bootstrap fallback until the asynchronous file request publishes.
    auto woodResult = AssetsManager::GetTextureOrFallback({}, "albedoMap");
    if (!woodResult)
        return std::unexpected(woodResult.error());
    auto boxParameters = floorParameters;
    boxParameters.roughness = 1.f;
    boxParameters.uvTiling = {1, 1};
    // The old box path retained the floor's PBR channels between draws. Make the
    // steady authored combination explicit so frame ordering cannot alter it.
    auto boxMaterialResult = CreateSurfaceMaterial(
        *m_SceneResources,
        {*woodResult, floor_normal, floor_metallic, floor_roughness, floor_ao}, boxParameters);
    if (!boxMaterialResult)
        return failure(boxMaterialResult.error());
    auto boxMaterial = *boxMaterialResult;
    m_AsyncBoxMaterial = boxMaterial;
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
        return failure(pointMesh.error());
    auto pointMaterial =
        PublishTexturedMaterial(*m_SceneResources, SceneMaterialKind::PointLight, {}, {});
    if (!pointMaterial)
        return failure(pointMaterial.error());
    if (auto assigned = assignRenderable(
            m_PointLightEntity, {*pointMesh, *pointMaterial, 0, false, false, true}); !assigned)
        return assigned;
    RigidBody3DComponent rigidBodyComp;
    rigidBodyComp.Type = BodyType::Dynamic;

    SphereFixture3DComponent sphereFixtureComp;

    sphereFixtureComp.Radius = 1.f;
    sphereFixtureComp.Property.m_Elasticity = 0.5f;
    sphereFixtureComp.Property.m_Friction = 0.5f;
    sphereFixtureComp.Property.m_InvMass = 1.f;

#if activate_sphere_diamond
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
    if (auto authored = renderable(woodSphereEntity, smoothSphereGeo, sphereMaterial, "");
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
    if (auto authored = renderable(DiamondEntity, DiamondGeo, sphereMaterial, "Diamond"); !authored)
        return authored;

#endif

#if activate_sphere_lattice
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
                if (auto authored =
                        renderable(woodSphereEntity, smoothSphereGeo, sphereMaterial, "");
                    !authored)
                    return authored;
            }
        }
    }
#endif

    const auto box = boxMesh;
    BoxFixture3DComponent boxFixtureComp;
    boxFixtureComp.Property.m_InvMass = 1.f;
    boxFixtureComp.Property.m_Friction = 0.5f;
    boxFixtureComp.Property.m_Elasticity = 0.5f;

#if activate_boxes_stacking
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
            if (auto authored = renderable(woodBoxEntity, box, boxMaterial, "Box"); !authored)
                return authored;
        }
    }
#endif

#if activate_sphere_boxes_stacking
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
            if (auto authored = renderable(woodBoxEntity, box, boxMaterial, "Box"); !authored)
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
    if (auto authored = renderable(woodSphereEntity, smoothSphereGeo, sphereMaterial, "");
        !authored)
        return authored;

#endif

    const MaterialParameterDecl helperParameters[]{
        {"u_baseColor", MaterialParameterType::Float4, std::array<float, 4>{1, 1, 1, 1}},
        {"u_useVertexColor", MaterialParameterType::Boolean, true}};
    auto gridMesh = m_SceneResources->PublishShape("GridHelper");
    if (!gridMesh)
        return failure(gridMesh.error());
    auto gridMaterial = PublishTexturedMaterial(*m_SceneResources, SceneMaterialKind::Helper, {},
                                                helperParameters, true);
    if (!gridMaterial)
        return failure(gridMaterial.error());
    auto createdSceneEntity9 = m_ActiveScene->CreateEntity("grid");
    if (!createdSceneEntity9)
        return std::unexpected(createdSceneEntity9.error());
    m_GridEntity = *createdSceneEntity9;
    if (auto assigned = assignRenderable(
            m_GridEntity, {*gridMesh, *gridMaterial, 0, false, false, true}); !assigned)
        return assigned;
    m_GridEntity.AddOrReplaceComponent<VisibilityComponent>(VisibilityComponent{false});
    m_GridEntity.GetComponent<Transform3DComponent>().SetRotation({Math::Pi / 2.f, 0, 0});
    auto axisMesh = m_SceneResources->PublishShape("AxisHelper");
    if (!axisMesh)
        return failure(axisMesh.error());
    auto axisMaterial = PublishTexturedMaterial(*m_SceneResources, SceneMaterialKind::Helper, {},
                                                helperParameters, true, 3.f);
    if (!axisMaterial)
        return failure(axisMaterial.error());
    auto createdSceneEntity10 = m_ActiveScene->CreateEntity("axis");
    if (!createdSceneEntity10)
        return std::unexpected(createdSceneEntity10.error());
    m_AxisEntity = *createdSceneEntity10;
    m_AxisEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{0, 1, 0});
    if (auto assigned = assignRenderable(
            m_AxisEntity, {*axisMesh, *axisMaterial, 0, false, false, true}); !assigned)
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
        return failure(skyMesh.error());
    auto skyMaterial =
        PublishTexturedMaterial(*m_SceneResources, SceneMaterialKind::Sky, {*skyTexture}, {}, true);
    if (!skyMaterial)
        return failure(skyMaterial.error());
    auto createdSceneEntity11 = m_ActiveScene->CreateEntity("Environment_SkyBox");
    if (!createdSceneEntity11)
        return std::unexpected(createdSceneEntity11.error());
    m_SkyBoxEntity = *createdSceneEntity11;
    if (auto assigned = assignRenderable(
            m_SkyBoxEntity, {*skyMesh, *skyMaterial, 0, false, false, false}); !assigned)
        return assigned;
    rigidBodyComp.Type = BodyType::Static;

    auto createdSceneEntity12 = m_ActiveScene->CreateEntity("wood_plane");
    if (!createdSceneEntity12)
        return std::unexpected(createdSceneEntity12.error());
    _Entity planeEntity = *createdSceneEntity12;
    const auto planeGeo = boxMesh;
    BoxFixture3DComponent planeFixtureComp;
    planeFixtureComp.Property.m_InvMass = 0.f;
    planeFixtureComp.Property.m_Friction = 0.5f;
    planeFixtureComp.Property.m_Elasticity = 0.5f;
    if (auto authored = renderable(planeEntity, planeGeo, floorMaterial, "Box"); !authored)
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
    if (auto authored = renderable(wallEntity_1, planeGeo, wallMaterial, "Box"); !authored)
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
    if (auto authored = renderable(wallEntity_2, planeGeo, wallMaterial, "Box"); !authored)
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
    if (auto authored = renderable(wallEntity_3, planeGeo, wallMaterial, "Box"); !authored)
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
    if (auto authored = renderable(wallEntity_4, planeGeo, wallMaterial, "Box"); !authored)
        return authored;
    wallEntity_4.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
    wallEntity_4.AddOrReplaceComponent<Transform3DComponent>(Vec3f{0.f, 4.5f, -49.5f}, Vec3f{},
                                                             Vec3f{100, 10, 1});
    planeFixtureComp.Property.m_Position =
        wallEntity_4.GetComponent<Transform3DComponent>().Translation;
    planeFixtureComp.Property.m_Orientation =
        wallEntity_4.GetComponent<Transform3DComponent>().QuatRotation;
    wallEntity_4.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);

    const auto normalPhysicsBodies =
        m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>().size();
    std::size_t galleryFixtures = 0;
#if GENGINE_RBS_GEOMETRY_TEMPLATE_GALLERY
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
            return failure(mesh.error());
        auto created = m_ActiveScene->CreateEntity(fixture.name);
        if (!created)
            return std::unexpected(created.error());
        created->AddOrReplaceComponent<Transform3DComponent>(fixture.position, Vec3f{}, Vec3f{1});
        if (auto authored = renderable(*created, *mesh, floorMaterial); !authored)
            return authored;
        ++galleryFixtures;
    }

    struct CurvedFixture
    {
        const char* name;
        GeometryTemplates::Request request;
    };
    const CurvedFixture curved[]{
        {"Template Sphere", GeometryTemplates::Sphere{}},
        {"Template Cylinder", GeometryTemplates::Cylinder{}},
        {"Template Cone", GeometryTemplates::Cone{}},
        {"Template Capsule", GeometryTemplates::Capsule{}},
        {"Template Torus", GeometryTemplates::Torus{}},
        {"Template Diamond", GeometryTemplates::Diamond{}}};
    for (std::size_t family = 0; family < std::size(curved); ++family)
        for (int row = 0; row < 2; ++row)
        {
            auto request = curved[family].request;
            std::visit([row](auto& value)
            {
                value.Options.Shading = row == 0 ? GeometryTemplates::ShadingMode::Smooth
                                                : GeometryTemplates::ShadingMode::Flat;
                value.Options.Tangents = GeometryTemplates::TangentMode::Omit;
            }, request);
            auto mesh = m_SceneResources->PublishGeometry(request);
            if (!mesh)
                return failure(mesh.error());
            auto created = m_ActiveScene->CreateEntity(
                std::format("{} {}", curved[family].name, row == 0 ? "Smooth" : "Flat"));
            if (!created)
                return std::unexpected(created.error());
            created->AddOrReplaceComponent<Transform3DComponent>(
                Vec3f{-12.f + 4.f * static_cast<float>(family), 3.f, row == 0 ? -10.f : -14.f},
                Vec3f{}, Vec3f{1});
            if (auto authored = renderable(*created, *mesh, sphereMaterial); !authored)
                return authored;
            ++galleryFixtures;
        }
#endif
    if (std::getenv("GENGINE_PRE_EDITOR_PARAMETRIC_GEOMETRY"))
    {
        if (normalPhysicsBodies != m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>().size() ||
            galleryFixtures != (GENGINE_RBS_GEOMETRY_TEMPLATE_GALLERY ? 15u : 0u))
            return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                "Phase 07 gallery", "Gallery altered Physics membership or fixture count"});
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_07_GALLERY_PASS enabled={} fixtures={} physics_bodies={}",
            GENGINE_RBS_GEOMETRY_TEMPLATE_GALLERY, galleryFixtures, normalPhysicsBodies);
    }

    // Phase 08 authoring examples. Ordinary dimensions remain Transform scale;
    // explicit uniqueness, changed Bake and primitive regeneration are distinct.
    GeometryTemplates::Cube authoringCube{2, 3, 4};
    authoringCube.Options.Tangents = GeometryTemplates::TangentMode::Generate;
    auto authoringMesh = m_SceneResources->PublishGeometry(authoringCube);
    if (!authoringMesh) return failure(authoringMesh.error());
    auto authoringSource = m_ActiveScene->CreateEntity("Phase08 Source");
    if (!authoringSource) return std::unexpected(authoringSource.error());
    authoringSource->Transform().SetTranslation({-6, 2, 6});
    if (auto assigned = m_SceneResources->AssignRenderable(*authoringSource, {*authoringMesh, floorMaterial}); !assigned)
        return failure(assigned.error());
    auto authoringDuplicate = authoringSource->Duplicate();
    if (!authoringDuplicate) return std::unexpected(authoringDuplicate.error());
    authoringDuplicate->Name() = "Phase08 Unique Bake";
    authoringDuplicate->Transform().SetTranslation({0, 2, 6});
    if (auto dimensions = m_SceneResources->SetDimensions(*authoringSource, {4, 6, 8}); !dimensions)
        return failure(dimensions.error());
    if (auto unique = m_SceneResources->MakeGeometryUnique(*authoringDuplicate); !unique)
        return failure(unique.error());
    authoringDuplicate->Transform().SetScale({2, 1, .5f});
    if (auto baked = m_SceneResources->BakeGeometry(*authoringDuplicate); !baked)
        return failure(baked.error());
    auto regenerated = m_ActiveScene->CreateEntity("Phase08 Regenerated");
    if (!regenerated) return std::unexpected(regenerated.error());
    regenerated->Transform().SetTranslation({6, 2, 6});
    if (auto assigned = m_SceneResources->AssignRenderable(*regenerated, {*authoringMesh, sphereMaterial}); !assigned)
        return failure(assigned.error());
    GeometryTemplates::Cube regeneratedCube{4, 3, 2};
    regeneratedCube.Options.Tangents = GeometryTemplates::TangentMode::Generate;
    if (auto geometry = m_SceneResources->RegenerateGeometry(*regenerated, regeneratedCube); !geometry)
        return failure(geometry.error());

    if (std::getenv("GENGINE_PRE_EDITOR_GEOMETRY_AUTHORING"))
    {
        if (!GENGINE_RBS_GEOMETRY_TEMPLATE_GALLERY || galleryFixtures != 15u ||
            normalPhysicsBodies != m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>().size())
            return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                "Phase 08 gallery preservation", "Authoring preset requires the existing gallery and Physics membership"});
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_08_GALLERY_PASS enabled={} fixtures={} physics_bodies={}",
            GENGINE_RBS_GEOMETRY_TEMPLATE_GALLERY, galleryFixtures, normalPhysicsBodies);
    }

    // The same regular five-map floor is shared by each A/B geometry pair.
    // Clone changes authored mapping only; no primitive regeneration or UV edits.
    materialGallery = {};
    auto uvGallery = m_SceneResources->CloneMaterial(sphereMaterial);
    if (!uvGallery) return failure(uvGallery.error());
    auto triplanarGallery = m_SceneResources->CloneMaterial(*uvGallery);
    if (!triplanarGallery) return failure(triplanarGallery.error());
    materialGallery.uvMaterial = *uvGallery;
    materialGallery.triplanarMaterial = *triplanarGallery;
    auto projection = m_SceneResources->DescribeMaterial(*triplanarGallery);
    if (!projection) return failure(projection.error());
    projection->mapping = TextureMappingMode::Triplanar;
    if (auto edited = m_SceneResources->EditMaterial(*triplanarGallery, *projection); !edited)
        return failure(edited.error());
    auto galleryParent = m_ActiveScene->CreateEntity("Material mapping transform");
    if (!galleryParent) return std::unexpected(galleryParent.error());
    materialGallery.parent = *galleryParent;
    const GeometryTemplates::Request mappingRequests[]{GeometryTemplates::Plane{2, 2},
        GeometryTemplates::Cube{2, 2, 2}, GeometryTemplates::Sphere{},
        GeometryTemplates::Capsule{}, GeometryTemplates::Torus{}, GeometryTemplates::Diamond{}};
    constexpr const char* mappingNames[]{"Plane", "Cube", "Sphere", "Capsule", "Torus", "Diamond"};
    for (std::size_t i = 0; i < std::size(mappingRequests); ++i)
    {
        auto mesh = m_SceneResources->PublishGeometry(mappingRequests[i]);
        if (!mesh) return failure(mesh.error());
        for (unsigned mode = 0; mode < 2; ++mode)
        {
            auto entity = m_ActiveScene->CreateEntity(std::format("{} {}", mappingNames[i], mode ? "Triplanar" : "UV"));
            if (!entity) return std::unexpected(entity.error());
            entity->Transform().SetTranslation({-10.f + 4.f * static_cast<float>(i), 2.f, mode ? -24.f : -20.f});
            if (auto parented = entity->SetParent(materialGallery.parent); !parented)
                return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "material gallery parent", "Parent assignment failed"});
            if (auto assigned = m_SceneResources->AssignRenderable(*entity,
                {*mesh, mode ? *triplanarGallery : *uvGallery}); !assigned) return failure(assigned.error());
            (mode ? materialGallery.triplanar : materialGallery.uv)[i] = *entity;
        }
    }
    // Opaque, Unlit, Masked, Transparent and Debug use the same authoring API.
    for (unsigned i = 0; i < 5; ++i)
    {
        MaterialAuthoringDesc desc;
        desc.kind = static_cast<MaterialKind>(i);
        desc.baseColor = {.25f + .12f * i, .55f, .85f, 1};
        if (desc.kind == MaterialKind::Transparent) desc.opacity = .45f;
        // Optional controlled alpha fixture is a validation input, never a new
        // permanent asset or a fallback for the regular five-map floor comparison.
        if (desc.kind == MaterialKind::Masked)
            if (const auto* fixture = std::getenv("GENGINE_PRE_EDITOR_MATERIAL_ALPHA_FIXTURE"))
            {
                auto image = AssetsManager::LoadTexture(fixture);
                if (!image) return failure(SceneResourceError{"alpha fixture", image.error()});
                auto texture = AssetsManager::ResolveTexture(*image);
                if (!texture) return failure(SceneResourceError{"alpha fixture lease", texture.error()});
                auto binding = AssetsManager::SampleTexture(*texture);
                if (!binding) return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                    "alpha fixture sampler", binding.error().message});
                desc.textures[0] = MaterialTextureValue{binding->TextureIdentity(), binding->SamplerIdentity()};
                desc.mapping = TextureMappingMode::Triplanar;
            }
        auto material = m_SceneResources->CreateMaterial(desc);
        if (!material) return failure(material.error());
        auto entity = m_ActiveScene->CreateEntity(std::format("Typed material kind {}", i));
        if (!entity) return std::unexpected(entity.error());
        entity->Transform().SetTranslation({-8.f + 4.f * i, 2.f, -28.f});
        if (auto assigned = m_SceneResources->AssignRenderable(*entity, {boxMesh, *material}); !assigned)
            return failure(assigned.error());
    }
    if (std::getenv("GENGINE_PRE_EDITOR_MATERIAL_AUTHORING"))
    {
        auto checked = PreEditorValidation::CheckMaterialAuthoring(*m_SceneResources, *m_ActiveScene,
            boxMesh, *uvGallery, materialGallery.uv[0].GetComponent<MeshRendererComponent>().mesh);
        if (!checked) return checked;
        FocusMaterialCamera(m_EditorCamera_, {0, 2, -22}, .35f, 0, 38.f);
    }

    // Reserve before the Scene hands successful runtime shape ownership to this caller.
    m_PhysicsShapes.reserve(m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>().size());
    if (auto started = m_ActiveScene->OnRuntimeStart(); !started)
        return std::unexpected(started.error());
    for (auto* body : m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies())
        m_PhysicsShapes.emplace_back(body->m_Shape);

    if (std::getenv("GENGINE_PRE_EDITOR_SCENE_AUTHORING"))
    {
        auto checked =
            PreEditorValidation::CheckSceneAuthoring(*m_ActiveScene, *m_SceneResources, boxMesh,
                                                     smoothSphereGeo, boxMaterial, sphereMaterial);
        if (!checked)
            return std::unexpected(checked.error());
    }

    if (std::getenv("GENGINE_PRE_EDITOR_RESOURCE_OWNERSHIP"))
    {
        auto checked = PreEditorValidation::CheckResourceOwnership(
            *m_SceneResources, boxMesh, smoothSphereGeo, boxMaterial, sphereMaterial);
        if (!checked)
            return std::unexpected(checked.error());
    }

    if (std::getenv("GENGINE_PRE_EDITOR_GEOMETRY_TEMPLATES"))
    {
        auto checked = PreEditorValidation::CheckGeometryPublication(
            GetEngineContext(), *m_SceneResources, boxMesh, floorMaterial);
        if (!checked)
            return std::unexpected(checked.error());
    }

    if (std::getenv("GENGINE_PRE_EDITOR_PARAMETRIC_GEOMETRY"))
    {
        auto checked = PreEditorValidation::CheckParametricPublication(
            GetEngineContext(), *m_SceneResources, boxMesh, sphereMaterial, *pointMaterial);
        if (!checked)
            return std::unexpected(checked.error());
    }

    if (std::getenv("GENGINE_PRE_EDITOR_GEOMETRY_AUTHORING"))
    {
        auto checked = PreEditorValidation::CheckGeometryAuthoring(
            GetEngineContext(), *m_ActiveScene, *m_SceneResources, *authoringMesh, floorMaterial);
        if (!checked) return std::unexpected(checked.error());
    }

    auto AppPauseEvent = new Events<void()>("AppPause");
    auto AppResumeEvent = new Events<void()>("AppResume");
    auto debugshowEvent = new Events<void()>("DebugShow");

    auto viewPortEvent = new Events<void()>("ViewportChange");

    auto MouseScrollEvent = new Events<void(MouseScrollWheelParam)>("MouseScrollWheel");

    AppPauseEvent->Subscribe(
        [this]()
        {
            m_IsPause = true;
        });
    AppResumeEvent->Subscribe(
        [this]()
        {
            m_IsPause = false;
        });
    debugshowEvent->Subscribe(
        [this]()
        {
            m_IsShowDebugBoundingBox = !m_IsShowDebugBoundingBox;
        });
    viewPortEvent->Subscribe(
        [this]()
        {
            m_EditorCamera_.OnViewportViewDirectionChange();
        });

    MouseScrollEvent->Subscribe(
        [this](const MouseScrollWheelParam& mousescrollParam)
        {
            m_EditorCamera_.OnMouseScroll(mousescrollParam.Y);
        });

    GetEventManager()->GetEventDispatcher().RegisterEvent(MouseScrollEvent);
    GetEventManager()->GetEventDispatcher().RegisterEvent(AppPauseEvent);
    GetEventManager()->GetEventDispatcher().RegisterEvent(AppResumeEvent);
    GetEventManager()->GetEventDispatcher().RegisterEvent(debugshowEvent);
    GetEventManager()->GetEventDispatcher().RegisterEvent(viewPortEvent);

    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::Initialize(const WindowProperties& prop)
{
    return Initialize(std::initializer_list<WindowProperties>{prop});
}

void RigidBodySimulationApp::Update(Timestep ts)
{
    if (materialGallery.parent && std::getenv("GENGINE_PRE_EDITOR_MATERIAL_AUTHORING"))
    {
        // Two seconds per static view. Each four-second motion replay advances
        // the same 60 poses, holding poses 15/30/45/60 for matched captures.
        const auto now = std::chrono::steady_clock::now();
        if (materialGallery.validationStart == std::chrono::steady_clock::time_point{})
            materialGallery.validationStart = now;
        const double elapsed = std::chrono::duration<double>(now - materialGallery.validationStart).count();
        const int stage = elapsed < 28 ? int(elapsed / 2) : elapsed < 36 ? 14 + int((elapsed - 28) / 4) : 16;
        const double within = elapsed - (stage < 14 ? stage * 2 : 28 + (stage - 14) * 4);
        if (stage != materialGallery.validationStage)
        {
            materialGallery.validationStage = stage;
            materialGallery.recordedSample = -1;
            SetMaterialComparison(stage == 16 ? 0 : 1 + stage % 2);
            const int primitive = stage >= 2 && stage < 14 ? (stage - 2) / 2 : -1;
            FocusMaterialCamera(m_EditorCamera_, primitive < 0 ? Vec3f{0, 2, -22} :
                Vec3f{-10.f + 4.f * primitive, 2, -20}, .35f, primitive < 0 ? 0 : .65f,
                primitive < 0 ? 38.f : 7.f);
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_VIEW_BEGIN stage={}", stage);
        }
        int motionStep = 0;
        materialGallery.validationSample = 0;
        materialGallery.captureReady = stage < 14 && within >= .3;
        if (stage == 14 || stage == 15)
        {
            const int block = int(within);
            const double fraction = within - block;
            motionStep = block * 15 + (std::min)(15, 1 + int(fraction * 30));
            materialGallery.validationSample = block + 1;
            materialGallery.captureReady = fraction >= .65;
        }
        materialGallery.validationFrames = motionStep;
        const float t = motionStep * .05f;
        materialGallery.parent.Transform().SetTranslation(motionStep ? Vec3f{.5f * std::sin(t), 0, .5f * std::cos(t)} : Vec3f{});
        materialGallery.parent.Transform().SetRotation({0, .04f * std::sin(t), 0});
        for (std::size_t i = 0; i < materialGallery.uv.size(); ++i)
            for (auto entity : {materialGallery.uv[i], materialGallery.triplanar[i]})
            {
                entity.Transform().SetRotation({.3f * std::sin(t), t, .2f * std::cos(t)});
                entity.Transform().SetScale(motionStep == 0 ? Vec3f{1} : motionStep <= 30 ?
                    Vec3f(1.f + .2f * std::sin(t)) : Vec3f{1.f + .2f * std::sin(t), 1.f + .15f * std::cos(t), 1});
            }
    }
    else if (materialGallery.parent && materialGallery.animate)
    {
        const bool validation = std::getenv("GENGINE_PRE_EDITOR_MATERIAL_AUTHORING") != nullptr;
        materialGallery.time += validation ? .05f : float(ts);
        const float t = materialGallery.time;
        materialGallery.parent.Transform().SetTranslation({.5f * std::sin(t), 0, .5f * std::cos(t)});
        materialGallery.parent.Transform().SetRotation({0, .04f * std::sin(t), 0});
        for (std::size_t i = 0; i < materialGallery.uv.size(); ++i)
            for (auto entity : {materialGallery.uv[i], materialGallery.triplanar[i]})
            {
                entity.Transform().SetRotation({.3f * std::sin(t), t, .2f * std::cos(t)});
                entity.Transform().SetScale(validation && materialGallery.validationFrames < 30 ?
                    Vec3f(1.f + .2f * std::sin(t)) : Vec3f{1.f + .2f * std::sin(t), 1.f + .15f * std::cos(t), 1.f});
            }
        if (validation && materialGallery.validationFrames == 59)
        {
            materialGallery.animate = false;
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_MOTION_COMPLETE frames=60 local_mapping=true parent_motion=true positive_nonuniform_scale=true visual_result=pending");
        }
    }
    // ImGui reports subpixel extents; retain whole-pixel truncation for framebuffer resize.
    OnViewportResize(static_cast<int>(m_ViewportSize.x), static_cast<int>(m_ViewportSize.y));
    //auto entity = m_ActiveScene->FindEntityByName("sphere");

    //auto& relationship = entity.GetComponent<RelationshipComponent>();
    m_EditorCamera_.OnUpdate(ts);

    auto& transform = m_SkyBoxEntity.GetComponent<Transform3DComponent>();
    transform.EulerRotation.y += ts * 0.02f;

    transform.SetRotation(transform.EulerRotation);
    //std::cout << "time passes_ " << ts << std::endl;

    //static float angle = 0.f;
    //angle += ts * 0.4f;
    //glm::quat dq = glm::angleAxis(ts * 0.4f, Vec3f{ 0.f, 1.f, 0.f });

    //for (auto& it : m_ActiveScene->GetAllEntitiesWith<Transform3DComponent, RigidBody3DComponent>())
    //{
    //	_Entity ent = { it, m_ActiveScene.get() };
    //	auto& rigidBody = ent.GetComponent<RigidBody3DComponent>();
    //
    //	if (rigidBody.Type != BodyType::Static)
    //	{
    //		auto& transform = ent.GetComponent<Transform3DComponent>();
    //		transform.QuatRotation = dq * transform.QuatRotation;
    //		//transform.SetRotation(q);
    //	}
    //}

    // Scene accumulates elapsed time and owns the bounded fixed physics ticks.
    m_ActiveScene->SetPaused(m_IsPause);
    m_ActiveScene->Update(ts);

    //update entities
    /*auto view = m_ActiveScene->GetAllEntitiesWith<Transform3DComponent>();
	for (auto e : view)
	{

		_Entity entity{ e, m_ActiveScene.get()};
		auto& transform = entity.GetComponent<Transform3DComponent>();
	
		transform.EulerRotation.y += ts * 1;

		transform.SetRotation(transform.EulerRotation);

	}*/

    /*auto entity = m_ActiveScene->FindEntityByName("sphere");


	GENGINE_INFO("{}, {}", entity.GetName(), entity.GetUUID());*/
    //m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

    //m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);

    //switch (m_SceneState)
    //{
    //case SceneState::Edit:
    //{
    //	if (m_ViewportFocused)
    //		m_EditorCamera.OnUpdate(ts);
    //

    //	m_EditorCamera.OnUpdate(ts);

    //	m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);
    //	break;
    //}
    //case SceneState::Simulate:
    //{
    //	m_EditorCamera.OnUpdate(ts);

    //	//m_ActiveScene->OnUpdateSimulation(ts, m_EditorCamera);
    //	break;
    //}
    //case SceneState::Play:
    //{
    //	m_ActiveScene->OnUpdateRuntime(ts);
    //	break;
    //}
    //}
}

std::expected<void, SceneError> RigidBodySimulationApp::UpdateImportedMesh()
{
    if (m_BarrelSettled)
        return {};
    auto failed = [&](const AsyncMeshError& error)
    {
        Log::GetCoreLogger()->warn("Barrel mesh: {}", DescribeAsyncMeshError(error));
        m_BarrelSettled = true;
    };
    if (!m_BarrelRequest)
    {
        auto request = m_MeshLoads->Request("barrel.obj");
        if (!request)
        {
            failed(request.error());
            return {};
        }
        m_BarrelRequest = *request;
        return {};
    }
    auto status = m_MeshLoads->Status(m_BarrelRequest);
    if (!status)
    {
        failed(status.error());
        return {};
    }
    if (status->state == AsyncAssetState::Failed || status->state == AsyncAssetState::Cancelled)
    {
        if (status->error)
            failed(*status->error);
        m_BarrelSettled = true;
        return {};
    }
    if (status->state != AsyncAssetState::Ready)
        return {};
    auto metadata = m_SceneResources->MeshMetadata(status->mesh);
    if (!metadata)
    {
        failed(UploadError{UploadCode::UploadFailed, {}, metadata.error()});
        return {};
    }
    const auto submeshes = metadata->submeshCount;
    // This callback runs after upload and before frame extraction. No physics
    // component is authored; imported submeshes share the existing lit material.
    for (std::size_t part = 0; part < submeshes; ++part)
    {
        auto createdSceneEntity17 =
            m_ActiveScene->CreateEntity(std::format("Imported barrel {}", part));
        if (!createdSceneEntity17)
            return std::unexpected(createdSceneEntity17.error());
        auto entity = *createdSceneEntity17;
        {
            auto assigned = m_SceneResources->AssignRenderable(
                entity, {status->mesh, m_AsyncBoxMaterial, static_cast<std::uint32_t>(part)});
            if (!assigned)
                return std::unexpected(SceneError{SceneErrorCode::InvalidIdentity,
                    "imported mesh assignment", "Imported renderable assignment failed"});
        }
        entity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{-6.f, 0.f, 0.f});
    }
    m_BarrelSettled = true;
    Log::GetCoreLogger()->info("Async barrel mesh published: {} submeshes", submeshes);
    return {};
}

void RigidBodySimulationApp::Render()
{
    RenderContext context{*GetWindow(), *GetWindowManager(), m_RenderTarget.get(),
                          HasVisibleViewport()};
    context.editorUI = {this, [](void* user) -> ScheduleResult
                        {
                            auto& app = *static_cast<RigidBodySimulationApp*>(user);
                            app.ImGuiRender();
                            return {};
                        }};
    auto render = [&]() -> std::expected<ScheduledFrameStats, ScheduleError>
    {
        auto loads = AssetsManager::AsyncTextures();
        if (!loads)
            return std::visit(
                [](const auto& cause) -> std::unexpected<ScheduleError>
                {
                    if constexpr (std::same_as<std::decay_t<decltype(cause)>, UploadError>)
                        return std::unexpected(
                            ScheduleError{FrameStage::UpdateFrameResources, cause});
                    else
                        return std::unexpected(
                            ScheduleError{FrameStage::UpdateFrameResources,
                                          SceneResourceError{"async wood texture", cause}});
                },
                loads.error());
        m_TextureLoads = *loads;
        // This selected one-shot import follows the one-shot texture bootstrap;
        // only the active service needs draining. This is not a second scheduler.
        if (m_LoadBarrel && m_WoodSettled && !m_MeshLoads && !m_BarrelSettled)
        {
            auto meshLoads = m_SceneResources->CreateMeshLoader(m_MeshRoot);
            if (meshLoads)
                m_MeshLoads = std::move(*meshLoads);
            else
            {
                Log::GetCoreLogger()->warn("Barrel mesh: {}",
                                           DescribeAsyncMeshError(meshLoads.error()));
                m_BarrelSettled = true;
            }
        }
        context.uploads = m_MeshLoads ? &m_MeshLoads->Queue() : &m_TextureLoads->Queue();
        context.updateResources = {
            this, [](void* user) -> ScheduleResult
            {
                auto& app = *static_cast<RigidBodySimulationApp*>(user);
                if (app.m_MeshLoads)
                {
                    if (auto updated = app.UpdateImportedMesh(); !updated)
                        return std::unexpected(
                            ScheduleError{FrameStage::UpdateFrameResources, updated.error()});
                }
                if (app.m_WoodSettled)
                    return {};
                auto failure = [](const auto& cause) -> ScheduleResult
                {
                    if constexpr (std::same_as<std::decay_t<decltype(cause)>, UploadError>)
                        return std::unexpected(
                            ScheduleError{FrameStage::UpdateFrameResources, cause});
                    else
                        return std::unexpected(
                            ScheduleError{FrameStage::UpdateFrameResources,
                                          SceneResourceError{"async wood texture", cause}});
                };
                if (!app.m_WoodRequest)
                {
                    auto requested = app.m_TextureLoads->Request("Sphere/wood_diffuse");
                    if (!requested)
                        return std::visit(failure, requested.error());
                    app.m_WoodRequest = *requested;
                    return {};
                }
                auto status = app.m_TextureLoads->Status(app.m_WoodRequest);
                if (!status)
                    return failure(status.error());
                if (status->state == AsyncAssetState::Failed ||
                    status->state == AsyncAssetState::Cancelled)
                {
                    if (status->error)
                        Log::GetCoreLogger()->warn("{}; retaining wood fallback",
                                                   DescribeAsyncTextureError(*status->error));
                    app.m_WoodSettled = true;
                    return {};
                }
                if (status->state != AsyncAssetState::Ready)
                    return {};
                auto view = AssetsManager::ResolveTexture(status->image);
                if (!view)
                    return failure(view.error());
                auto sampled = AssetsManager::SampleTexture(*view);
                if (!sampled)
                    return std::visit(failure, sampled.error().cause);
                auto changed = app.m_SceneResources->SetMaterialTexture(
                    app.m_AsyncBoxMaterial, MaterialTextureSemantic::BaseColor,
                    MaterialTextureValue{sampled->TextureIdentity(), sampled->SamplerIdentity()});
                if (!changed)
                    return std::unexpected(
                        ScheduleError{FrameStage::UpdateFrameResources, changed.error()});
                app.m_WoodSettled = true;
                Log::GetCoreLogger()->info("Async wood texture published: {}x{}",
                                           status->description.width, status->description.height);
                return {};
            }};
        if (!context.visible)
            return FrameScheduler::Render(context);
        m_EditorCamera_.UpdateView();
        auto cameraId = m_ActiveScene->RenderData().Identify(m_FrameCameraEntity);
        if (!cameraId)
            return std::unexpected(ScheduleError{FrameStage::FreezeFrameInputs,
                                                 RenderExtractionError{{}, cameraId.error()}});
        const FrameCamera camera{*cameraId,
                                 m_EditorCamera_.GetViewMatrix(),
                                 m_EditorCamera_.GetProjection(),
                                 m_EditorCamera_.GetPosition(),
                                 0,
                                 0,
                                 static_cast<unsigned>(m_RenderTarget->GetWidth()),
                                 static_cast<unsigned>(m_RenderTarget->GetHeight())};
        FrameSubmissionDesc targets{*m_RenderTarget,
                                    *m_MousePickFrameBuffer,
                                    *m_PointShadowFrameBuffer,
                                    *m_CascadeShadowFrameBuffer,
                                    {}, // FrameScheduler supplies current renderer pipeline roles.
                                    m_ShadowCascadeLevels,
                                    m_EditorCamera_.GetFOV(),
                                    m_EditorCamera_.GetAspectRatio(),
                                    m_EditorCamera_.GetNearClip(),
                                    m_EditorCamera_.GetFarClip(),
                                    m_NearPlane,
                                    m_FarPlane};
        const auto mouse = ImGui::GetMousePos();
        const auto& pickStorage = m_MousePickFrameBuffer->Buffer().Description();
        targets.pickingEnabled =
            GetInputManager()->GetMouseState().isButtonPressed(
                GEngineMouseCode::GENGINE_BUTTON_LEFT) &&
            ViewportPixelAt(mouse.x - m_ViewportBounds[0].x, mouse.y - m_ViewportBounds[0].y,
                            GetEditorViewportLogicalSize(), {pickStorage.Width, pickStorage.Height})
                .has_value();
        FrameSceneInput input{*m_ActiveScene, *m_SceneResources, *m_FrameSubmission,
                              targets,        m_PickTable,       {&camera, 1}};
        // Preserve Phase 45's input/viewport snapshot: read this frame's IDs
        // before BeginUI refreshes ImGui mouse state or authors new panel bounds.
        input.pickingReadback = {this, [](void* user) -> ScheduleResult
                                 {
                                     static_cast<RigidBodySimulationApp*>(user)->OnMouseClicked();
                                     return {};
                                 }};
        auto submitted = FrameScheduler::Render(context, &input);
        if (submitted && std::getenv("GENGINE_PRE_EDITOR_MATERIAL_AUTHORING"))
        {
            if (materialGallery.captureReady && materialGallery.recordedSample != materialGallery.validationSample)
            {
                const auto p = camera.worldPosition;
                const auto& q = camera.projection;
                Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_VIEW stage={} sample={} mode={} position={},{},{} pitch={} yaw={} fov={} near={} far={} viewport={},{} projection={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{} motion_step={}",
                    materialGallery.validationStage, materialGallery.validationSample, materialGallery.comparison,
                    p.x,p.y,p.z,m_EditorCamera_.m_Pitch,m_EditorCamera_.m_Yaw,m_EditorCamera_.GetFOV(),
                    m_EditorCamera_.GetNearClip(),m_EditorCamera_.GetFarClip(),camera.viewportWidth,camera.viewportHeight,
                    q[0][0],q[0][1],q[0][2],q[0][3],q[1][0],q[1][1],q[1][2],q[1][3],
                    q[2][0],q[2][1],q[2][2],q[2][3],q[3][0],q[3][1],q[3][2],q[3][3],materialGallery.validationFrames);
                materialGallery.recordedSample = materialGallery.validationSample;
            }
            if (materialGallery.validationStage == 16 && materialGallery.recordedSample != 0)
            {
                Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_MOTION_COMPLETE poses=60 replays=2 matched_holds=15,30,45,60 visual_result=pending");
                Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_VIEWS_COMPLETE static_views=14 motion_views=8");
                materialGallery.recordedSample = 0;
            }
        }
        return submitted;
    };
    if (auto result = render(); !result)
    {
        if (const auto* sceneError = std::get_if<SceneError>(&result.error().cause))
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Scene",
                         std::to_string(static_cast<unsigned>(sceneError->code)),
                         std::string(sceneError->operation),
                         std::format("entity={}", sceneError->entity),
                         DescribeScheduleError(result.error())});
        else
        {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
                         "Rendering",
                         std::to_string(static_cast<unsigned>(result.error().stage)),
                         "render frame",
                         {},
                         DescribeScheduleError(result.error())});
        }
    }
}

void RigidBodySimulationApp::ImGuiRender()
{
    if (materialGallery.uvMaterial)
    {
        ImGui::Begin("Material authoring");
        ImGui::TextUnformatted("Plane / Cube / Sphere / Capsule / Torus / Diamond");
        ImGui::TextUnformatted("Same floor: UV front row, Triplanar second row");
        if (ImGui::Button("Focus material comparison"))
        {
            FocusMaterialCamera(m_EditorCamera_, {0, 2, -22}, .35f, 0, 38.f);
        }
        ImGui::Checkbox("Animate object transforms", &materialGallery.animate);
        constexpr const char* geometryNames[]{"Plane", "Cube", "Sphere", "Capsule", "Torus", "Diamond"};
        ImGui::Combo("Inspect geometry", &materialGallery.inspectedGeometry, geometryNames, 6);
        if (ImGui::Button("Focus selected geometry"))
        {
            FocusMaterialCamera(m_EditorCamera_, {-10.f + 4.f * materialGallery.inspectedGeometry, 2.f, -20.f}, .35f, .65f, 7.f);
        }
        const char* comparisons[]{"Paired rows", "UV at reference positions", "Triplanar at reference positions"};
        if (ImGui::Combo("Comparison", &materialGallery.comparison, comparisons, 3))
            SetMaterialComparison(materialGallery.comparison);
        if (auto desc = m_SceneResources->DescribeMaterial(materialGallery.triplanarMaterial); desc)
        {
            bool changed = ImGui::SliderFloat("Projection repeats / local unit", &desc->projectionScale.value, .001f, 8.f);
            changed |= ImGui::SliderFloat("Blend sharpness", &desc->blendSharpness.value, 1, 8);
            changed |= ImGui::SliderFloat("Normal strength", &desc->normalStrength, 0, 2);
            if (changed)
                if (auto edited = m_SceneResources->EditMaterial(materialGallery.triplanarMaterial, *desc); !edited)
                    Log::GetCoreLogger()->error("{}", DescribeSceneResourceError(edited.error()));
        }
        ImGui::End();
    }

    static bool p_open = true;
    static bool opt_fullscreen = true;
    static bool opt_padding = false;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
    //ImGui::ShowDemoWindow(&p_open);
    // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
    // because it would be confusing to have two docking targets within each others.
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    if (opt_fullscreen)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }
    /*else
	{
		dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
	}*/

    // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
    // and handle the pass-thru hole, so we ask Begin() to not render a background.
    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
        window_flags |= ImGuiWindowFlags_NoBackground;

    // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
    // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
    // all active windows docked into it will lose their parent and become undocked.
    // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
    // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
    if (!opt_padding)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace Demo", &p_open, window_flags);
    if (!opt_padding)
        ImGui::PopStyleVar();

    if (opt_fullscreen)
        ImGui::PopStyleVar(2);

    // Submit the DockSpace
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Load barrel mesh", nullptr, false, m_WoodSettled && !m_LoadBarrel))
                m_LoadBarrel = true;
            // Disabling fullscreen would allow the window to be moved to the front of other windows,
            // which we can't undo at the moment without finer window depth/z control.
            if (ImGui::MenuItem("Exit"))
            {
                ShutDown();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Shadows"))
        {
            auto request = GetShadowQuality().requested;
            int tier = static_cast<int>(request.quality);
            bool changed =
                ImGui::Combo("Quality", &tier, "Low (1024)\0Medium (2048)\0High (4096)\0Custom\0");
            request.quality = static_cast<ShadowQuality>(tier);
            if (request.quality == ShadowQuality::Custom)
            {
                int resolution = static_cast<int>(request.customResolution);
                if (ImGui::InputInt("Resolution", &resolution, 64, 256,
                                    ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    request.customResolution =
                        resolution > 0 ? static_cast<unsigned>(resolution) : 0;
                    changed = true;
                }
            }
            int budget = static_cast<int>(request.byteBudget / (1024 * 1024));
            if (ImGui::InputInt("Depth budget (MiB)", &budget, 48, 192,
                                ImGuiInputTextFlags_EnterReturnsTrue))
            {
                request.byteBudget = budget > 0 ? std::uint64_t(budget) * 1024 * 1024 : 0;
                changed = true;
            }
            bool fallback = request.fallback == ShadowFallback::LowerTiers;
            if (ImGui::Checkbox("Allow lower quality on allocation failure", &fallback))
            {
                request.fallback = fallback ? ShadowFallback::LowerTiers : ShadowFallback::None;
                changed = true;
            }
            if (changed)
                if (auto queued = RequestShadowQuality(request); !queued)
                    ReportFramebufferError("shadow quality request", queued.error());
            const auto& current = GetShadowQuality();
            ImGui::Text("Active: %s (%u x %u)", ShadowQualityLabel(current.effective).data(),
                        current.resolution, current.resolution);
            ImGui::Text("Estimated / allocated depth: %.1f / %.1f MiB",
                        double(current.memory.estimatedBytes) / (1024 * 1024),
                        double(current.memory.allocatedDepthBytes) / (1024 * 1024));
            ImGui::TextDisabled("Driver overhead is not included.");
            if (ShadowQualityPending())
                ImGui::TextUnformatted("Quality change pending");
            if (current.fallbackReason)
                ImGui::TextWrapped("Lower tier selected: %s", current.fallbackReason->message);
            if (GetShadowQualityError())
                ImGui::TextWrapped("Previous quality retained: %s",
                                   GetShadowQualityError()->message);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
    //ImGui::ShowDemoWindow(&p_open);
    const bool viewportVisible = ImGui::Begin("Viewport");
    auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
    auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
    auto viewportOffset = ImGui::GetWindowPos();
    m_ViewportBounds[0] = {viewportMinRegion.x + viewportOffset.x,
                           viewportMinRegion.y + viewportOffset.y};
    m_ViewportBounds[1] = {viewportMaxRegion.x + viewportOffset.x,
                           viewportMaxRegion.y + viewportOffset.y};
    //GENGINE_CORE_INFO("{0}, {1}", viewportOffset.x, viewportOffset.y);

    m_ViewportForcused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();

    //GENGINE_INFO("Focused: {}", ImGui::IsWindowFocused());
    //GENGINE_INFO("Hovered: {}", ImGui::IsWindowHovered());

    auto viewportPanelSize = ImGui::GetContentRegionAvail();
    const bool hasArea = viewportVisible && viewportPanelSize.x > 0 && viewportPanelSize.y > 0;
    auto scale = hasArea ? UI::CurrentViewportFramebufferScale()
                         : std::expected<FramebufferScale, PlatformError>(FramebufferScale{});
    if (scale)
    {
        if (auto sized = SetEditorViewport(
                hasArea ? EditorViewportLogicalSize{viewportPanelSize.x, viewportPanelSize.y}
                        : EditorViewportLogicalSize{},
                *scale);
            !sized)
            ReportPlatformError(sized.error());
    }
    else
        ReportPlatformError(scale.error());

    static bool phase08PresentationRecorded = false;
    if (!phase08PresentationRecorded && std::getenv("GENGINE_PRE_EDITOR_GEOMETRY_AUTHORING") &&
        HasVisibleViewport() && scale)
    {
        const auto logical = GetEditorViewportLogicalSize();
        const auto pixels = GetEditorViewportPixelSize();
        const auto& target = m_MousePickFrameBuffer->Buffer().Description();
        if (target.Width == pixels.Width && target.Height == pixels.Height)
        {
            const auto position = m_EditorCamera_.GetPosition();
            const auto& shadows = GetShadowQuality();
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_08_PRESENTATION logical={}x{} pixels={}x{} scale={},{} camera={},{},{} fov={} near={} far={} shadow={} resolution={} swap_interval={} manual_cap={}",
                logical.Width, logical.Height, pixels.Width, pixels.Height, scale->X, scale->Y,
                position.x, position.y, position.z, m_EditorCamera_.GetFOV(), m_EditorCamera_.GetNearClip(), m_EditorCamera_.GetFarClip(),
                ShadowQualityLabel(shadows.effective), shadows.resolution, GetWindow()->GetSwapInterval(), GetManualFrameRateLimit());
            phase08PresentationRecorded = true;
        }
    }

    if (!materialGallery.presentationRecorded && std::getenv("GENGINE_PRE_EDITOR_MATERIAL_AUTHORING") &&
        HasVisibleViewport() && scale)
    {
        const auto logical = GetEditorViewportLogicalSize();
        const auto pixels = GetEditorViewportPixelSize();
        const auto position = m_EditorCamera_.GetPosition();
        const auto& shadows = GetShadowQuality();
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_PRESENTATION logical={}x{} pixels={}x{} scale={},{} camera={},{},{} fov={} near={} far={} shadow={} resolution={} swap_interval={} manual_cap={}",
            logical.Width, logical.Height, pixels.Width, pixels.Height, scale->X, scale->Y,
            position.x, position.y, position.z, m_EditorCamera_.GetFOV(), m_EditorCamera_.GetNearClip(), m_EditorCamera_.GetFarClip(),
            ShadowQualityLabel(shadows.effective), shadows.resolution, GetWindow()->GetSwapInterval(), GetManualFrameRateLimit());
        materialGallery.presentationRecorded = true;
    }

    //m_ViewportSize = {1280, 720};
    //m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

    //if (m_RenderTarget && m_RenderTarget->IsMultiSampled())
    //m_RenderTarget->BindAndBlitToScreen(0);

    //ImGui::Image(reinterpret_cast<void*>(m_FinalFrameBuffer->GetColorMap()), { m_ViewportSize.x, m_ViewportSize.y }, { 0,1 }, { 1, 0 });
    if (HasVisibleViewport())
        if (auto image = UI::FramebufferImage(*m_RenderTarget, m_ViewportSize.x, m_ViewportSize.y);
            !image)
            ReportFramebufferError("viewport image", image.error());

    /*m_MousePickFrameBuffer->Bind();
	RenderSystem::OnMouseClicked(m_ActiveScene.get(), *m_MousePickFrameBuffer, m_ViewportBounds[0], m_ViewportBounds[1]);
	m_MousePickFrameBuffer->UnBind();*/

    /*auto windowSize = ImGui::GetWindowSize();
	ImVec2 minBound = ImGui::GetWindowPos();
	minBound.x += viewportOffset.x;
	minBound.y += viewportOffset.y;

	ImVec2 maxBound = { minBound.x + windowSize.x, minBound.y + windowSize.y };

	m_ViewportBounds[0] = { minBound.x, minBound.y };
	m_ViewportBounds[1] = { maxBound.x, maxBound.y };*/

    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::End();

    // Note: Switch this to true to enable dockspace
    //static bool show = true;
    //ImGui::ShowDemoWindow(&show);

    //static bool opt_fullscreen = true;
    //static bool opt_padding = false;
    //static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    //////// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
    //////// because it would be confusing to have two docking targets within each others.
    //ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    //if (opt_fullscreen)
    //{
    //	ImGuiViewport* viewport = ImGui::GetMainViewport();
    //	ImGui::SetNextWindowPos(viewport->Pos);
    //	ImGui::SetNextWindowSize(viewport->Size);
    //	ImGui::SetNextWindowViewport(viewport->ID);
    //	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    //	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    //	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    //	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    //}
    //else
    //{
    //	dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
    //}

    ////// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
    ////// and handle the pass-thru hole, so we ask Begin() to not render a background.
    //if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
    //	window_flags |= ImGuiWindowFlags_NoBackground;

    ////// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
    ////// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
    ////// all active windows docked into it will lose their parent and become undocked.
    ////// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
    ////// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
    //if (!opt_padding)
    //	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    //ImGui::Begin("DockSpace Demo", &show, window_flags);
    //if (!opt_padding)
    //	ImGui::PopStyleVar();

    //if (opt_fullscreen)
    //	ImGui::PopStyleVar(2);

    ////// Submit the DockSpace
    //ImGuiIO& io = ImGui::GetIO();

    //auto& style = ImGui::GetStyle();
    //float winMinSize = style.WindowMinSize.x;
    //style.WindowMinSize.x = 370.f;
    //if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    //{
    //	ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    //	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    //}

    //style.WindowMinSize.x = winMinSize;

    //if (ImGui::BeginMenuBar())
    //{
    //	if (ImGui::BeginMenu("File"))
    //	{
    //		// Disabling fullscreen would allow the window to be moved to the front of other windows,
    //		// which we can't undo at the moment without finer window depth/z control.
    //		//ImGui::MenuItem("Fullscreen", nullptr, &opt_fullscreen);
    //		//ImGui::MenuItem("Padding", nullptr, &opt_padding);
    //		//ImGui::Separator();

    //		if (ImGui::MenuItem("Exit"))
    //		{
    //			ShutDown();
    //		}

    //		ImGui::EndMenu();
    //	}

    //	ImGui::EndMenuBar();
    //}

    //ImGui::End();
}

void RigidBodySimulationApp::UI_Toolbar()
{
    /*ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	auto& colors = ImGui::GetStyle().Colors;
	const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
	const auto& buttonActive = colors[ImGuiCol_ButtonActive];
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActive.x, buttonActive.y, buttonActive.z, 0.5f));*/
}

void RigidBodySimulationApp::OnMouseClicked()
{
    if (GetInputManager()->GetMouseState().isButtonPressed(GEngineMouseCode::GENGINE_BUTTON_LEFT))
    {
        const auto mouse = ImGui::GetMousePos();
        const auto& storage = m_MousePickFrameBuffer->Buffer().Description();
        auto position =
            ViewportPixelAt(mouse.x - m_ViewportBounds[0].x, mouse.y - m_ViewportBounds[0].y,
                            GetEditorViewportLogicalSize(), {storage.Width, storage.Height});
        if (!position || !HasVisibleViewport())
            return;
        const int x = position->X, y = position->Y;
        auto pixel = m_MousePickFrameBuffer->ReadPixel(x, y);
        if (!pixel)
        {
            ReportFramebufferError("picking read", pixel.error());
            return;
        }
        m_HoveredEntity = {};
        if (*pixel != EntityPickTable::InvalidPixel)
        {
            auto id = m_ActiveScene->RenderData().ResolvePick(m_PickTable, *pixel);
            if (!id)
            {
                Log::GetCoreLogger()->warn("Stale/invalid picking result: {}", int(id.error()));
                return;
            }
            auto entity = m_ActiveScene->RenderData().Resolve(*id);
            if (!entity)
            {
                Log::GetCoreLogger()->warn("Picking entity: {}", int(entity.error()));
                return;
            }
            m_HoveredEntity = _Entity{*entity, m_ActiveScene.get()};
        }
        //GENGINE_CORE_INFO("Mouse Position: {}, {}; Entity {} has been clicked",x,y,m_HoveredEntity?m_HoveredEntity.GetName():"None");
        std::println("Mouse Position: {}, {}; Entity {} has been clicked", x, y,
                     m_HoveredEntity ? m_HoveredEntity.GetName() : "None");
        //std::cout << "Pixel Data: " << pixel_data << std::endl;
        //m_MousePickFrameBuffer->UnBind();
    }
}

void RigidBodySimulationApp::FilledKDTreePoints()
{
    //auto& entities = m_ActiveScene->GetAllEntitiesWith<Transform3DComponent, RigidBody3DComponent>();
    /*for (auto& e : m_ActiveScene->GetAllEntitiesWith<Transform3DComponent, RigidBody3DComponent>())
	{
		_Entity entity{ e, m_ActiveScene.get() };
		auto& transform = entity.GetComponent<Transform3DComponent>();
		m_KDTreePoints.push_back(transform.Translation);
	
	}*/
}

void RigidBodySimulationApp::OnViewportResize(int, int)
{
    // Allocation is coalesced by BaseApp once per frame, before application work.
    // Use successful target storage, never native-window or fractional panel size.
    if (!HasVisibleViewport())
        return;
    const auto width = m_RenderTarget->GetWidth(), height = m_RenderTarget->GetHeight();
    if (!width || !height)
        return;
    m_EditorCamera_.SetViewportSize(static_cast<float>(width), static_cast<float>(height));
}

BaseApp* CreateApp()
{
    return new RigidBodySimulationApp();
}
