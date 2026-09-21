#include "Renderer/FrameScheduler.h"
#include "UI/FramebufferImage.h"
#include "RigidBodySimulation.h"
#include "Core/RuntimeAssets.h"
#include <cstdint>
#include <cstdlib>
#include "EntryPoint.h"
#include "Renderer/RenderExtraction.h"
#include "Managers/AssetsManager.h"
#include <glm/gtx/quaternion.hpp>
#include <format>
#include <Core/Log.h>
#include <imgui/imgui.h>
#include "Assets/Textures/Texture.h"

#include "Core/Window.h"

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
#define activate_sphere_lattice 1
#endif
#ifndef activate_sphere_diamond
#define activate_sphere_diamond 0
#endif
#ifndef activate_sphere_boxes_stacking
#define activate_sphere_boxes_stacking 0
#endif




RigidBodySimulationApp::~RigidBodySimulationApp()
{
	if (m_AudioSystem)
		m_AudioSystem->Shutdown();
    if (m_FrameResources) {
        if (auto current = GetEngineContext().MakeCurrent(); !current) { ReportPlatformError(current.error()); std::terminate(); }
        m_MeshLoads.reset(); // Cancel/join imports before registry or scene retirement.
        m_ActiveScene.reset(); m_EditorScene.reset();
        m_FrameSubmission.reset(); m_FrameResources.reset();
    }
}

ApplicationInitializationResult RigidBodySimulationApp::Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)
{
    if (auto initialized = BaseApp::Initialize(WindowsPropertyList); !initialized) return initialized;
    m_AudioSystem = CreateScopedPtr<Audio::AudioSystem>();
    m_AudioSystem->Initialize();
    auto failure = [](const SceneResourceError& error) -> ApplicationInitializationResult {
        Log::GetCoreLogger()->error("{}", DescribeSceneResourceError(error));
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "scene resources", DescribeSceneResourceError(error)});
    };
    auto resources = SceneRenderResources::Create(GetEngineContext());
    if (!resources) return failure(resources.error());
    m_FrameResources = std::move(*resources);
    auto meshRoot = RuntimeAssets::TryFile("Models");
    if (!meshRoot) return std::unexpected(meshRoot.error());
    m_MeshRoot = *meshRoot;
    // Opt-in validation exercises the same action as the File menu.
    m_LoadBarrel = std::getenv("GENGINE_ASYNC_MESH_SMOKE") != nullptr;
    auto submission = FrameSubmission::Create();
    if (!submission) {
        Log::GetCoreLogger()->error("{}", DescribeSubmissionError(submission.error()));
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "frame submission", DescribeSubmissionError(submission.error())});
    }
    m_FrameSubmission.emplace(std::move(*submission));
    m_FarPlane = 100;
    m_EditorScene = CreateRefPtr<_Scene>();
    m_ActiveScene = m_EditorScene;
    m_EditorCamera_ = _EditorCamera(45.0f, 1280.f, 720.f, 0.1f, 1000.f);
    float cameraFarClip = m_EditorCamera_.GetFarClip();
    m_ShadowCascadeLevels = {cameraFarClip / 50.f, cameraFarClip / 25.f, cameraFarClip / 10.f, cameraFarClip / 2.f, cameraFarClip};
    m_FrameCameraEntity = m_ActiveScene->CreateEntity("Editor frame camera");
    m_FrameCameraEntity.AddOrReplaceComponent<RenderCameraComponent>();
    auto sphereMeshResult = m_FrameResources->PublishShape("Sphere");
    if (!sphereMeshResult) return failure(sphereMeshResult.error());
    auto smoothSphereGeo = *sphereMeshResult;
    auto diamondMeshResult = m_FrameResources->PublishShape("Diamond");
    if (!diamondMeshResult) return failure(diamondMeshResult.error());
    auto DiamondGeo = *diamondMeshResult;
    auto boxMeshResult = m_FrameResources->PublishShape("Box");
    if (!boxMeshResult) return failure(boxMeshResult.error());
    auto boxMesh = *boxMeshResult;
    auto renderable = [&]( _Entity entity, Asset::MeshHandle mesh, Asset::MaterialInstanceHandle material,
                           std::string_view physicsShape = {}) -> ApplicationInitializationResult {
        entity.AddOrReplaceComponent<MeshRendererComponent>(MeshRendererComponent{mesh, material});
        if (!physicsShape.empty()) {
            auto attached = m_FrameResources->AttachPhysicsShape(entity, physicsShape);
            if (!attached) return failure(attached.error());
        }
        return {};
    };

	auto m_IconPlayResult = AssetsManager::GetTextureOrFallback("Icons/PlayButton");
	if (!m_IconPlayResult) { Log::GetCoreLogger()->error("Texture {}: {}", m_IconPlayResult.error().source, m_IconPlayResult.error().message); m_Running = false; return std::unexpected(m_IconPlayResult.error()); }
	auto* m_IconPlay = *m_IconPlayResult;
	auto m_IconPauseResult = AssetsManager::GetTextureOrFallback("Icons/PauseButton");
	if (!m_IconPauseResult) { Log::GetCoreLogger()->error("Texture {}: {}", m_IconPauseResult.error().source, m_IconPauseResult.error().message); m_Running = false; return std::unexpected(m_IconPauseResult.error()); }
	auto* m_IconPause = *m_IconPauseResult;
	auto m_IconStepResult = AssetsManager::GetTextureOrFallback("Icons/StepButton");
	if (!m_IconStepResult) { Log::GetCoreLogger()->error("Texture {}: {}", m_IconStepResult.error().source, m_IconStepResult.error().message); m_Running = false; return std::unexpected(m_IconStepResult.error()); }
	auto* m_IconStep = *m_IconStepResult;
	auto m_IconSimulateResult = AssetsManager::GetTextureOrFallback("Icons/SimulateButton");
	if (!m_IconSimulateResult) { Log::GetCoreLogger()->error("Texture {}: {}", m_IconSimulateResult.error().source, m_IconSimulateResult.error().message); m_Running = false; return std::unexpected(m_IconSimulateResult.error()); }
	auto* m_IconSimulate = *m_IconSimulateResult;
	auto m_IconStopResult = AssetsManager::GetTextureOrFallback("Icons/StopButton");
	if (!m_IconStopResult) { Log::GetCoreLogger()->error("Texture {}: {}", m_IconStopResult.error().source, m_IconStopResult.error().message); m_Running = false; return std::unexpected(m_IconStopResult.error()); }
	auto* m_IconStop = *m_IconStopResult;


// PBR vectors and scalar data must not use the color texture sRGB transfer.
    TextureDesc dataMapDesc;
    dataMapDesc.colorSpace = TextureColorSpace::Linear;
auto sphere_albedoResult = AssetsManager::GetTextureOrFallback("PBR/rustediron/rustediron2_basecolor", "albedoMap");
	if (!sphere_albedoResult) { Log::GetCoreLogger()->error("Texture {}: {}", sphere_albedoResult.error().source, sphere_albedoResult.error().message); m_Running = false; return std::unexpected(sphere_albedoResult.error()); }
	auto* sphere_albedo = *sphere_albedoResult;
	auto sphere_normalResult = AssetsManager::GetTextureOrFallback("PBR/rustediron/rustediron2_normal", "normalMap", ".png", dataMapDesc);
	if (!sphere_normalResult) { Log::GetCoreLogger()->error("Texture {}: {}", sphere_normalResult.error().source, sphere_normalResult.error().message); m_Running = false; return std::unexpected(sphere_normalResult.error()); }
	auto* sphere_normal = *sphere_normalResult;
	auto sphere_metallicResult = AssetsManager::GetTextureOrFallback("PBR/rustediron/rustediron2_metallic", "metallicMap", ".png", dataMapDesc);
	if (!sphere_metallicResult) { Log::GetCoreLogger()->error("Texture {}: {}", sphere_metallicResult.error().source, sphere_metallicResult.error().message); m_Running = false; return std::unexpected(sphere_metallicResult.error()); }
	auto* sphere_metallic = *sphere_metallicResult;
	auto sphere_roughnessResult = AssetsManager::GetTextureOrFallback("PBR/rustediron/rustediron2_roughness", "roughnessMap", ".png", dataMapDesc);
	if (!sphere_roughnessResult) { Log::GetCoreLogger()->error("Texture {}: {}", sphere_roughnessResult.error().source, sphere_roughnessResult.error().message); m_Running = false; return std::unexpected(sphere_roughnessResult.error()); }
	auto* sphere_roughness = *sphere_roughnessResult;
	auto sphere_aoResult = AssetsManager::GetTextureOrFallback("PBR/subtle_black_granite/subtle-black-granite_ao", "aoMap", ".png", dataMapDesc);
	if (!sphere_aoResult) { Log::GetCoreLogger()->error("Texture {}: {}", sphere_aoResult.error().source, sphere_aoResult.error().message); m_Running = false; return std::unexpected(sphere_aoResult.error()); }
	auto* sphere_ao = *sphere_aoResult;


	auto floor_albedoResult = AssetsManager::GetTextureOrFallback("PBR/base_white_tile/base-white-tile_albedo", "albedoMap");
	if (!floor_albedoResult) { Log::GetCoreLogger()->error("Texture {}: {}", floor_albedoResult.error().source, floor_albedoResult.error().message); m_Running = false; return std::unexpected(floor_albedoResult.error()); }
	auto* floor_albedo = *floor_albedoResult;
	auto floor_normalResult = AssetsManager::GetTextureOrFallback("PBR/base_white_tile/base-white-tile_normal-dx", "normalMap", ".png", dataMapDesc);
	if (!floor_normalResult) { Log::GetCoreLogger()->error("Texture {}: {}", floor_normalResult.error().source, floor_normalResult.error().message); m_Running = false; return std::unexpected(floor_normalResult.error()); }
	auto* floor_normal = *floor_normalResult;
	auto floor_metallicResult = AssetsManager::GetTextureOrFallback("PBR/base_white_tile/base-white-tile_metallic", "metallicMap", ".png", dataMapDesc);
	if (!floor_metallicResult) { Log::GetCoreLogger()->error("Texture {}: {}", floor_metallicResult.error().source, floor_metallicResult.error().message); m_Running = false; return std::unexpected(floor_metallicResult.error()); }
	auto* floor_metallic = *floor_metallicResult;
	auto floor_roughnessResult = AssetsManager::GetTextureOrFallback("PBR/base_white_tile/base-white-tile_roughness", "roughnessMap", ".png", dataMapDesc);
	if (!floor_roughnessResult) { Log::GetCoreLogger()->error("Texture {}: {}", floor_roughnessResult.error().source, floor_roughnessResult.error().message); m_Running = false; return std::unexpected(floor_roughnessResult.error()); }
	auto* floor_roughness = *floor_roughnessResult;
	auto floor_aoResult = AssetsManager::GetTextureOrFallback("PBR/base_white_tile/base-white-tile_ao", "aoMap", ".png", dataMapDesc);
	if (!floor_aoResult) { Log::GetCoreLogger()->error("Texture {}: {}", floor_aoResult.error().source, floor_aoResult.error().message); m_Running = false; return std::unexpected(floor_aoResult.error()); }
	auto* floor_ao = *floor_aoResult;

	
    auto material = [&](SceneMaterialKind kind, std::initializer_list<Asset::Texture*> images,
                        std::span<const MaterialParameterDecl> parameters, bool doubleSided = false, float width = 1.f)
        -> std::expected<Asset::MaterialInstanceHandle, SceneResourceError> {
        std::vector<MaterialTextureAssignment> bindings;
        for (auto* texture : images) {
            auto sampled = AssetsManager::SampleTexture(texture->View());
            if (!sampled) return std::visit([](const auto& cause) -> std::expected<Asset::MaterialInstanceHandle, SceneResourceError> {
                return std::unexpected(SceneResourceError{"material sampling", cause});
            }, sampled.error().cause);
            bindings.push_back({texture->GetUniformName(), {sampled->TextureIdentity(), sampled->SamplerIdentity()}});
        }
        return m_FrameResources->PublishMaterial({kind, parameters, bindings, doubleSided, width});
    };
    const MaterialParameterDecl sphereParameters[]{
        // Dark-metal patch GGX width fit: 0.275, rounded to 0.28. Keep maps linear.
        {"roughnessScale", MaterialParameterType::Float, .9f},
        {"metalness", MaterialParameterType::Float3, std::array<float,3>{.8f,.8f,.8f}},
        {"u_tiling", MaterialParameterType::Float2, std::array<float,2>{1,1}}};
    auto sphereMaterialResult = material(SceneMaterialKind::Lit, {sphere_albedo,sphere_normal,sphere_metallic,sphere_roughness,sphere_ao}, sphereParameters);
    if (!sphereMaterialResult) return failure(sphereMaterialResult.error());
    auto sphereMaterial = *sphereMaterialResult;
    // Polished floor: retain linear roughness data and author gloss explicitly.
    const MaterialParameterDecl floorParameters[]{
        {"roughnessScale", MaterialParameterType::Float, .3f},
        {"metalness", MaterialParameterType::Float3, std::array<float,3>{.08f,.08f,.08f}},
        {"u_tiling", MaterialParameterType::Float2, std::array<float,2>{2,2}}};
    auto floorMaterialResult = material(SceneMaterialKind::Lit, {floor_albedo,floor_normal,floor_metallic,floor_roughness,floor_ao}, floorParameters);
    if (!floorMaterialResult) return failure(floorMaterialResult.error());
    auto floorMaterial = *floorMaterialResult;
    const MaterialParameterDecl wallParameters[]{
		{"roughnessScale", MaterialParameterType::Float, .3f},
        {"metalness", MaterialParameterType::Float3, std::array<float,3>{.08f,.08f,.08f}},
        {"u_tiling", MaterialParameterType::Float2, std::array<float,2>{2,.2f}}};
    auto wallMaterialResult = material(SceneMaterialKind::Lit, {floor_albedo,floor_normal,floor_metallic,floor_roughness,floor_ao}, wallParameters);
    if (!wallMaterialResult) return failure(wallMaterialResult.error());
    auto wallMaterial = *wallMaterialResult;
    // Keep the bootstrap fallback until the asynchronous file request publishes.
    auto woodResult = AssetsManager::GetTextureOrFallback({}, "albedoMap");
    if (!woodResult) return std::unexpected(woodResult.error());
    const MaterialParameterDecl boxParameters[]{
        {"metalness", MaterialParameterType::Float3, std::array<float,3>{.08f,.08f,.08f}},
        {"u_tiling", MaterialParameterType::Float2, std::array<float,2>{1,1}}};
    // The old box path retained the floor's PBR channels between draws. Make the
    // steady authored combination explicit so frame ordering cannot alter it.
    auto boxMaterialResult = material(SceneMaterialKind::Lit, {*woodResult,floor_normal,floor_metallic,floor_roughness,floor_ao}, boxParameters);
    if (!boxMaterialResult) return failure(boxMaterialResult.error());
    auto boxMaterial = *boxMaterialResult;
    m_AsyncBoxMaterial = boxMaterial;
    auto ambientLightEntity = m_ActiveScene->CreateEntity("ambient_light");
    m_PointLightEntity = m_ActiveScene->CreateEntity("point_light");
    m_LightDirection = glm::normalize(Vec3f{20,50,20});
    m_LightPos = {0,15,-10};
    RenderLightComponent direction;
    direction.color = {.7f,.7f,.7f}; direction.castShadows = true;
    ambientLightEntity.AddOrReplaceComponent<RenderLightComponent>(direction);
    auto& lightTransform = ambientLightEntity.GetComponent<Transform3DComponent>();
    lightTransform.QuatRotation = glm::rotation(Vec3f{0,0,-1}, -m_LightDirection);
    RenderLightComponent point;
    point.kind = RenderLightKind::Point; point.color = {.8f,.2f,.1f}; point.range = m_FarPlane; point.castShadows = true;
    m_PointLightEntity.AddOrReplaceComponent<RenderLightComponent>(point);
    m_PointLightEntity.AddOrReplaceComponent<Transform3DComponent>(m_LightPos);
    auto pointMesh = m_FrameResources->PublishShape("PointLightHelper");
    if (!pointMesh) return failure(pointMesh.error());
    auto pointMaterial = material(SceneMaterialKind::PointLight, {}, {});
    if (!pointMaterial) return failure(pointMaterial.error());
    m_PointLightEntity.AddOrReplaceComponent<MeshRendererComponent>(MeshRendererComponent{*pointMesh,*pointMaterial,0,false,false,true});
RigidBody3DComponent rigidBodyComp;
	rigidBodyComp.Type = BodyType::Dynamic;

	SphereFixture3DComponent sphereFixtureComp;

	sphereFixtureComp.Radius = 1.f;
	sphereFixtureComp.Property.m_Elasticity = 0.5f;
	sphereFixtureComp.Property.m_Friction = 0.5f;
	sphereFixtureComp.Property.m_InvMass = 1.f;

	#if activate_sphere_diamond
	sphereFixtureComp.Property.m_LinearVelocity = { -80.f, 0.f, 0.f };
	_Entity woodSphereEntity = m_ActiveScene->CreateEntity("wood_sphere_0");

	woodSphereEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 30.f, 5.0f, 0.f });

	sphereFixtureComp.Property.m_Position = woodSphereEntity.GetComponent<Transform3DComponent>().Translation;
	sphereFixtureComp.Property.m_Orientation = woodSphereEntity.GetComponent<Transform3DComponent>().QuatRotation;
	woodSphereEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	woodSphereEntity.AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixtureComp);
	if (auto authored = renderable(woodSphereEntity, smoothSphereGeo, sphereMaterial, ""); !authored) return authored;


	ConvexFixture3DComponent convexFixtureComp;

	convexFixtureComp.Property.m_Elasticity = 0.5f;
	convexFixtureComp.Property.m_Friction = 0.5f;
	convexFixtureComp.Property.m_InvMass = 1.f;
	convexFixtureComp.Property.m_AngularVelocity = { 5.f, 0.f, 5.f };
	convexFixtureComp.Property.m_LinearVelocity = { 80.f, 0.f, 0.f };
	_Entity DiamondEntity = m_ActiveScene->CreateEntity("Diamond");
	DiamondEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ -30, 5.f, 0 });

	convexFixtureComp.Property.m_Position = DiamondEntity.GetComponent<Transform3DComponent>().Translation;
	convexFixtureComp.Property.m_Orientation = DiamondEntity.GetComponent<Transform3DComponent>().QuatRotation;
	DiamondEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	DiamondEntity.AddOrReplaceComponent<ConvexFixture3DComponent>(convexFixtureComp);
	if (auto authored = renderable(DiamondEntity, DiamondGeo, sphereMaterial, "Diamond"); !authored) return authored;
	
	#endif

	#if activate_sphere_lattice
	sphereFixtureComp.Property.m_LinearVelocity = { 0.f, 0.f, 0.f };
	static int i = 0;

	for (int z = 1; z < 6; z++)
	{
		for (int x = 0; x < 6; x++)
		{
			for (int y = 0; y < 6; y++)
			{
				float yy = float(z - 1) * sphereFixtureComp.Radius * 2.f;
				float xx = float(x - 1) * sphereFixtureComp.Radius * 2.f;
				float zz = float(y - 1) * sphereFixtureComp.Radius * 2.f;
				_Entity woodSphereEntity = m_ActiveScene->CreateEntity("wood_sphere" + std::to_string(i++));
				woodSphereEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ xx, 10.f + yy, zz });

				sphereFixtureComp.Property.m_Position = woodSphereEntity.GetComponent<Transform3DComponent>().Translation;
				sphereFixtureComp.Property.m_Orientation = woodSphereEntity.GetComponent<Transform3DComponent>().QuatRotation;
				woodSphereEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
				woodSphereEntity.AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixtureComp);
				if (auto authored = renderable(woodSphereEntity, smoothSphereGeo, sphereMaterial, ""); !authored) return authored;


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

			_Entity woodBoxEntity = m_ActiveScene->CreateEntity("wood_box");

			woodBoxEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ x * (offset+0.01f), 1.5f + y * offset, 0.f }, Vec3f{0.f}, Vec3f{2.f});

			boxFixtureComp.Property.m_Position = woodBoxEntity.GetComponent<Transform3DComponent>().Translation;
			boxFixtureComp.Property.m_Orientation = woodBoxEntity.GetComponent<Transform3DComponent>().QuatRotation;
			woodBoxEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
			woodBoxEntity.AddOrReplaceComponent<BoxFixture3DComponent>(boxFixtureComp);
			if (auto authored = renderable(woodBoxEntity, box, boxMaterial, "Box"); !authored) return authored;

		}
	}
	#endif

	#if activate_sphere_boxes_stacking
	float offset = 2.f;

	for (int y = 0; y < 4; y++)
	{
		for (int x = 0; x < 4; x++)
		{

			_Entity woodBoxEntity = m_ActiveScene->CreateEntity("wood_box");

			woodBoxEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ x * (offset + 0.01f), 1.5f + y * offset, 0.f }, Vec3f{ 0.f }, Vec3f{ 2.f });

			boxFixtureComp.Property.m_Position = woodBoxEntity.GetComponent<Transform3DComponent>().Translation;
			boxFixtureComp.Property.m_Orientation = woodBoxEntity.GetComponent<Transform3DComponent>().QuatRotation;
			woodBoxEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
			woodBoxEntity.AddOrReplaceComponent<BoxFixture3DComponent>(boxFixtureComp);
			if (auto authored = renderable(woodBoxEntity, box, boxMaterial, "Box"); !authored) return authored;

		}
	}

	sphereFixtureComp.Property.m_LinearVelocity = { 0.f, 0.f, 40.f };
	_Entity woodSphereEntity = m_ActiveScene->CreateEntity("wood_sphere_0");

	woodSphereEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 3.5f, 5.0f, -20.f });

	sphereFixtureComp.Property.m_Position = woodSphereEntity.GetComponent<Transform3DComponent>().Translation;
	sphereFixtureComp.Property.m_Orientation = woodSphereEntity.GetComponent<Transform3DComponent>().QuatRotation;
	woodSphereEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	woodSphereEntity.AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixtureComp);
	if (auto authored = renderable(woodSphereEntity, smoothSphereGeo, sphereMaterial, ""); !authored) return authored;
	
	#endif

	
    const MaterialParameterDecl helperParameters[]{
        {"u_baseColor", MaterialParameterType::Float4, std::array<float,4>{1,1,1,1}},
        {"u_useVertexColor", MaterialParameterType::Boolean, true}};
    auto gridMesh = m_FrameResources->PublishShape("GridHelper");
    if (!gridMesh) return failure(gridMesh.error());
    auto gridMaterial = material(SceneMaterialKind::Helper, {}, helperParameters, true);
    if (!gridMaterial) return failure(gridMaterial.error());
    m_GridEntity = m_ActiveScene->CreateEntity("grid");
    m_GridEntity.AddOrReplaceComponent<MeshRendererComponent>(MeshRendererComponent{*gridMesh,*gridMaterial,0,false,false,true});
    m_GridEntity.AddOrReplaceComponent<VisibilityComponent>(VisibilityComponent{false});
    m_GridEntity.GetComponent<Transform3DComponent>().SetRotation({Math::Pi/2.f,0,0});
    auto axisMesh = m_FrameResources->PublishShape("AxisHelper");
    if (!axisMesh) return failure(axisMesh.error());
    auto axisMaterial = material(SceneMaterialKind::Helper, {}, helperParameters, true, 3.f);
    if (!axisMaterial) return failure(axisMaterial.error());
    m_AxisEntity = m_ActiveScene->CreateEntity("axis");
    m_AxisEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{0,1,0});
    m_AxisEntity.AddOrReplaceComponent<MeshRendererComponent>(MeshRendererComponent{*axisMesh,*axisMaterial,0,false,false,true});
    TextureDesc info;
    info.kind = TextureKind::Cube; info.colorSpace = TextureColorSpace::Linear;
    info.mips = TextureMipIntent::None; info.orientation = ImageOrientation::TopLeft;
    auto skyTexture = AssetsManager::GetTextureOrFallback("SkyBox/Day/", "u_skyBoxDay", ".png", info);
    if (!skyTexture) return std::unexpected(skyTexture.error());
    auto skyMesh = m_FrameResources->PublishShape("SkyBox");
    if (!skyMesh) return failure(skyMesh.error());
    auto skyMaterial = material(SceneMaterialKind::Sky, {*skyTexture}, {}, true);
    if (!skyMaterial) return failure(skyMaterial.error());
    m_SkyBoxEntity = m_ActiveScene->CreateEntity("Environment_SkyBox");
    m_SkyBoxEntity.AddOrReplaceComponent<MeshRendererComponent>(MeshRendererComponent{*skyMesh,*skyMaterial,0,false,false,false});
rigidBodyComp.Type = BodyType::Static;

	_Entity planeEntity = m_ActiveScene->CreateEntity("wood_plane");
	const auto planeGeo = boxMesh;
	BoxFixture3DComponent planeFixtureComp;
	planeFixtureComp.Property.m_InvMass = 0.f;
	planeFixtureComp.Property.m_Friction = 0.5f;
	planeFixtureComp.Property.m_Elasticity = 0.5f;
	if (auto authored = renderable(planeEntity, planeGeo, floorMaterial, "Box"); !authored) return authored;
	planeEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	planeEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{}, Vec3f{}, Vec3f{100, 1, 100});
	planeFixtureComp.Property.m_Position = planeEntity.GetComponent<Transform3DComponent>().Translation;
	planeFixtureComp.Property.m_Orientation = planeEntity.GetComponent<Transform3DComponent>().QuatRotation;
	planeEntity.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);

	_Entity wallEntity_1 = m_ActiveScene->CreateEntity("wall_entity_1");
	if (auto authored = renderable(wallEntity_1, planeGeo, wallMaterial, "Box"); !authored) return authored;
	wallEntity_1.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	wallEntity_1.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ -49.5f, 4.5f, 0.f }, Vec3f{0, glm::pi<float>() / 2.0f, 0}, Vec3f{100, 10, 1});
	planeFixtureComp.Property.m_Position = wallEntity_1.GetComponent<Transform3DComponent>().Translation;
	planeFixtureComp.Property.m_Orientation = wallEntity_1.GetComponent<Transform3DComponent>().QuatRotation;
	wallEntity_1.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);

	_Entity wallEntity_2 = m_ActiveScene->CreateEntity("wall_entity_2");
	if (auto authored = renderable(wallEntity_2, planeGeo, wallMaterial, "Box"); !authored) return authored;
	wallEntity_2.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	wallEntity_2.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 49.5f, 4.5f, 0.f }, Vec3f{ 0, glm::pi<float>() / 2.0f, 0 }, Vec3f{ 100, 10, 1});
	planeFixtureComp.Property.m_Position = wallEntity_2.GetComponent<Transform3DComponent>().Translation;
	planeFixtureComp.Property.m_Orientation = wallEntity_2.GetComponent<Transform3DComponent>().QuatRotation;
	wallEntity_2.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);

	_Entity wallEntity_3 = m_ActiveScene->CreateEntity("wall_entity_3");
	if (auto authored = renderable(wallEntity_3, planeGeo, wallMaterial, "Box"); !authored) return authored;
	wallEntity_3.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	wallEntity_3.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 0.f, 4.5f, 49.5f }, Vec3f{}, Vec3f{ 100, 10, 1 });
	planeFixtureComp.Property.m_Position = wallEntity_3.GetComponent<Transform3DComponent>().Translation;
	planeFixtureComp.Property.m_Orientation = wallEntity_3.GetComponent<Transform3DComponent>().QuatRotation;
	wallEntity_3.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);

	_Entity wallEntity_4 = m_ActiveScene->CreateEntity("wall_entity_4");
	if (auto authored = renderable(wallEntity_4, planeGeo, wallMaterial, "Box"); !authored) return authored;
	wallEntity_4.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	wallEntity_4.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 0.f, 4.5f, -49.5f }, Vec3f{}, Vec3f{ 100, 10, 1 });
	planeFixtureComp.Property.m_Position = wallEntity_4.GetComponent<Transform3DComponent>().Translation;
	planeFixtureComp.Property.m_Orientation = wallEntity_4.GetComponent<Transform3DComponent>().QuatRotation;
	wallEntity_4.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);


    m_ActiveScene->OnRuntimeStart();

	auto AppPauseEvent = new Events<void()>("AppPause");
	auto AppResumeEvent = new Events<void()>("AppResume");
	auto debugshowEvent = new Events<void()>("DebugShow");

	auto viewPortEvent = new Events<void()>("ViewportChange");

	auto MouseScrollEvent = new Events<void(MouseScrollWheelParam)>("MouseScrollWheel");

	AppPauseEvent->Subscribe([this]() { m_IsPause = true; });
	AppResumeEvent->Subscribe([this]() { m_IsPause = false; });
	debugshowEvent->Subscribe([this]() { m_IsShowDebugBoundingBox = !m_IsShowDebugBoundingBox; });
	viewPortEvent->Subscribe([this]() 
		{
			m_EditorCamera_.OnViewportViewDirectionChange();
		});

	MouseScrollEvent->Subscribe([this](const MouseScrollWheelParam& mousescrollParam)
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
	// ImGui reports subpixel extents; retain whole-pixel truncation for framebuffer resize.
	OnViewportResize(static_cast<int>(m_ViewportSize.x), static_cast<int>(m_ViewportSize.y));
	//auto entity = m_ActiveScene->FindEntityByName("sphere");

	//auto& relationship = entity.GetComponent<RelationshipComponent>();
	m_EditorCamera_.OnUpdate(ts);

	auto &transform = m_SkyBoxEntity.GetComponent<Transform3DComponent>();
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

void RigidBodySimulationApp::UpdateImportedMesh()
{
    if (m_BarrelSettled) return;
    auto failed = [&](const AsyncMeshError& error) {
        Log::GetCoreLogger()->warn("Barrel mesh: {}", DescribeAsyncMeshError(error));
        m_BarrelSettled = true;
    };
    if (!m_BarrelRequest) {
        auto request = m_MeshLoads->Request("barrel.obj");
        if (!request) { failed(request.error()); return; }
        m_BarrelRequest = *request;
        return;
    }
    auto status = m_MeshLoads->Status(m_BarrelRequest);
    if (!status) { failed(status.error()); return; }
    if (status->state == AsyncAssetState::Failed || status->state == AsyncAssetState::Cancelled) {
        if (status->error) failed(*status->error);
        m_BarrelSettled = true;
        return;
    }
    if (status->state != AsyncAssetState::Ready) return;
    std::size_t submeshes{};
    {
        auto access = m_FrameResources->Publication().BeginFrame();
        auto mesh = m_FrameResources->Meshes().Acquire(access, status->mesh);
        if (!mesh) { failed(GpuMeshError{GpuMeshErrorCode::Registry, "Imported mesh resolution", 0, mesh.error()}); return; }
        submeshes = (*mesh)->Submeshes().size();
    }
    // This callback runs after upload and before frame extraction. No physics
    // component is authored; imported submeshes share the existing lit material.
    for (std::size_t part = 0; part < submeshes; ++part) {
        auto entity = m_ActiveScene->CreateEntity(std::format("Imported barrel {}", part));
        entity.AddOrReplaceComponent<MeshRendererComponent>(MeshRendererComponent{status->mesh, m_AsyncBoxMaterial, static_cast<std::uint32_t>(part)});
        entity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{-6.f, 0.f, 0.f});
    }
    m_BarrelSettled = true;
    Log::GetCoreLogger()->info("Async barrel mesh published: {} submeshes", submeshes);
}

void RigidBodySimulationApp::Render()
{
    RenderContext context{*GetWindow(),*GetWindowManager(),m_RenderTarget.get(),HasVisibleViewport()};
    context.editorUI={this,[](void* user)->ScheduleResult {
        auto& app=*static_cast<RigidBodySimulationApp*>(user);
        app.ImGuiRender();
        return {};
    }};
    auto render=[&]()->std::expected<ScheduledFrameStats,ScheduleError> {
        auto loads = AssetsManager::AsyncTextures();
        if (!loads) return std::visit([](const auto& cause) -> std::unexpected<ScheduleError> {
            if constexpr (std::same_as<std::decay_t<decltype(cause)>, UploadError>)
                return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources, cause});
            else return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources, SceneResourceError{"async wood texture", cause}});
        }, loads.error());
        m_TextureLoads = *loads;
        // This selected one-shot import follows the one-shot texture bootstrap;
        // only the active service needs draining. This is not a second scheduler.
        if (m_LoadBarrel && m_WoodSettled && !m_MeshLoads && !m_BarrelSettled) {
            auto meshLoads = AsyncMeshLoader::Create(m_FrameResources->Publication(), m_FrameResources->Meshes(), m_MeshRoot);
            if (meshLoads) m_MeshLoads = std::move(*meshLoads);
            else { Log::GetCoreLogger()->warn("Barrel mesh: {}", DescribeAsyncMeshError(meshLoads.error())); m_BarrelSettled = true; }
        }
        context.uploads = m_MeshLoads ? &m_MeshLoads->Queue() : &m_TextureLoads->Queue();
        context.updateResources = {this, [](void* user) -> ScheduleResult {
            auto& app = *static_cast<RigidBodySimulationApp*>(user);
            if (app.m_MeshLoads) app.UpdateImportedMesh();
            if (app.m_WoodSettled) return {};
            auto failure = [](const auto& cause) -> ScheduleResult {
                if constexpr (std::same_as<std::decay_t<decltype(cause)>, UploadError>)
                    return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources, cause});
                else return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources, SceneResourceError{"async wood texture", cause}});
            };
            if (!app.m_WoodRequest) {
                auto requested = app.m_TextureLoads->Request("Sphere/wood_diffuse");
                if (!requested) return std::visit(failure, requested.error());
                app.m_WoodRequest = *requested;
                return {};
            }
            auto status = app.m_TextureLoads->Status(app.m_WoodRequest);
            if (!status) return failure(status.error());
            if (status->state == AsyncAssetState::Failed || status->state == AsyncAssetState::Cancelled) {
                if (status->error) Log::GetCoreLogger()->warn("{}; retaining wood fallback", DescribeAsyncTextureError(*status->error));
                app.m_WoodSettled = true;
                return {};
            }
            if (status->state != AsyncAssetState::Ready) return {};
            auto view = AssetsManager::ResolveTexture(status->image);
            if (!view) return failure(view.error());
            auto sampled = AssetsManager::SampleTexture(*view);
            if (!sampled) return std::visit(failure, sampled.error().cause);
            std::optional<MaterialInstance> material;
            {
                auto access = app.m_FrameResources->Publication().BeginFrame();
                auto previous = app.m_FrameResources->Materials().Acquire(access, app.m_AsyncBoxMaterial);
                if (!previous) return failure(previous.error());
                material = **previous;
            }
            if (auto changed = material->SetTexture("albedoMap", {sampled->TextureIdentity(), sampled->SamplerIdentity()}); !changed)
                return failure(changed.error());
            {
                auto publication = app.m_FrameResources->Publication().BeginPublication();
                if (auto replaced = app.m_FrameResources->Materials().Replace(publication, app.m_AsyncBoxMaterial, std::move(*material)); !replaced)
                    return failure(replaced.error());
            }
            app.m_WoodSettled = true;
            Log::GetCoreLogger()->info("Async wood texture published: {}x{}", status->description.width, status->description.height);
            return {};
        }};
        if(!context.visible) return FrameScheduler::Render(context);
        m_EditorCamera_.UpdateView();
        auto cameraId=m_ActiveScene->RenderData().Identify(m_FrameCameraEntity);
        if(!cameraId) return std::unexpected(ScheduleError{FrameStage::FreezeFrameInputs,
            RenderExtractionError{{},cameraId.error()}});
        const FrameCamera camera{*cameraId,m_EditorCamera_.GetViewMatrix(),m_EditorCamera_.GetProjection(),
            m_EditorCamera_.GetPosition(),0,0,static_cast<unsigned>(m_RenderTarget->GetWidth()),static_cast<unsigned>(m_RenderTarget->GetHeight())};
        FrameSubmissionDesc targets{*m_RenderTarget,*m_MousePickFrameBuffer,*m_PointShadowFrameBuffer,*m_CascadeShadowFrameBuffer,
            m_FrameResources->Pipelines(),m_ShadowCascadeLevels,m_EditorCamera_.GetFOV(),m_EditorCamera_.GetAspectRatio(),
            m_EditorCamera_.GetNearClip(),m_EditorCamera_.GetFarClip(),m_NearPlane,m_FarPlane};
        const auto mouse=ImGui::GetMousePos();
        const auto& pickStorage=m_MousePickFrameBuffer->Buffer().Description();
        targets.pickingEnabled=GetInputManager()->GetMouseState().isButtonPressed(GEngineMouseCode::GENGINE_BUTTON_LEFT)
            && ViewportPixelAt(mouse.x-m_ViewportBounds[0].x,mouse.y-m_ViewportBounds[0].y,
                GetEditorViewportLogicalSize(),{pickStorage.Width,pickStorage.Height}).has_value();
        FrameSceneInput input{*m_ActiveScene,*m_FrameResources,*m_FrameSubmission,targets,m_PickTable,{&camera,1}};
        // Preserve Phase 45's input/viewport snapshot: read this frame's IDs
        // before BeginUI refreshes ImGui mouse state or authors new panel bounds.
        input.pickingReadback={this,[](void* user)->ScheduleResult {
            static_cast<RigidBodySimulationApp*>(user)->OnMouseClicked();
            return {};
        }};
        return FrameScheduler::Render(context,&input);
    };
    if(auto result=render();!result) {
        Log::GetCoreLogger()->error("{}",DescribeScheduleError(result.error()));
        ShutDown();
    }
}

void RigidBodySimulationApp::ImGuiRender()
{

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
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
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
			if(ImGui::MenuItem("Exit"))
			{
				ShutDown();
			}
				
			ImGui::EndMenu();
		}
	

        if(ImGui::BeginMenu("Shadows")) {
            auto request=GetShadowQuality().requested;
            int tier=static_cast<int>(request.quality);
            bool changed=ImGui::Combo("Quality",&tier,"Low (1024)\0Medium (2048)\0High (4096)\0Custom\0");
            request.quality=static_cast<ShadowQuality>(tier);
            if(request.quality==ShadowQuality::Custom) {
                int resolution=static_cast<int>(request.customResolution);
                if(ImGui::InputInt("Resolution",&resolution,64,256,ImGuiInputTextFlags_EnterReturnsTrue)) {
                    request.customResolution=resolution>0?static_cast<unsigned>(resolution):0;changed=true;
                }
            }
            int budget=static_cast<int>(request.byteBudget/(1024*1024));
            if(ImGui::InputInt("Depth budget (MiB)",&budget,48,192,ImGuiInputTextFlags_EnterReturnsTrue)) {
                request.byteBudget=budget>0?std::uint64_t(budget)*1024*1024:0;changed=true;
            }
            bool fallback=request.fallback==ShadowFallback::LowerTiers;
            if(ImGui::Checkbox("Allow lower quality on allocation failure",&fallback)) {
                request.fallback=fallback?ShadowFallback::LowerTiers:ShadowFallback::None;changed=true;
            }
            if(changed) if(auto queued=RequestShadowQuality(request);!queued)
                ReportFramebufferError("shadow quality request",queued.error());
            const auto& current=GetShadowQuality();
            ImGui::Text("Active: %s (%u x %u)",ShadowQualityLabel(current.effective).data(),current.resolution,current.resolution);
            ImGui::Text("Estimated / allocated depth: %.1f / %.1f MiB",
                double(current.memory.estimatedBytes)/(1024*1024),double(current.memory.allocatedDepthBytes)/(1024*1024));
            ImGui::TextDisabled("Driver overhead is not included.");
            if(ShadowQualityPending()) ImGui::TextUnformatted("Quality change pending");
            if(current.fallbackReason) ImGui::TextWrapped("Lower tier selected: %s",current.fallbackReason->message);
            if(GetShadowQualityError()) ImGui::TextWrapped("Previous quality retained: %s",GetShadowQualityError()->message);
            ImGui::EndMenu();
        }
		ImGui::EndMenuBar();
	}


	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
	//ImGui::ShowDemoWindow(&p_open);
	const bool viewportVisible = ImGui::Begin("Viewport");
	auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
	auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
	auto viewportOffset = ImGui::GetWindowPos();
	m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
	m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };
	//GENGINE_CORE_INFO("{0}, {1}", viewportOffset.x, viewportOffset.y);

	m_ViewportForcused = ImGui::IsWindowFocused();
	m_ViewportHovered = ImGui::IsWindowHovered();

	//GENGINE_INFO("Focused: {}", ImGui::IsWindowFocused());
	//GENGINE_INFO("Hovered: {}", ImGui::IsWindowHovered());

	auto viewportPanelSize = ImGui::GetContentRegionAvail();
	const bool hasArea = viewportVisible && viewportPanelSize.x > 0 && viewportPanelSize.y > 0;
    auto scale = hasArea ? UI::CurrentViewportFramebufferScale() : std::expected<FramebufferScale, PlatformError>(FramebufferScale{});
    if (scale)
    {
        if (auto sized = SetEditorViewport(hasArea ? EditorViewportLogicalSize{viewportPanelSize.x, viewportPanelSize.y} : EditorViewportLogicalSize{}, *scale); !sized)
            ReportPlatformError(sized.error());
    }
    else ReportPlatformError(scale.error());

	//m_ViewportSize = {1280, 720};
	//m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

	//if (m_RenderTarget && m_RenderTarget->IsMultiSampled())
		//m_RenderTarget->BindAndBlitToScreen(0);

	//ImGui::Image(reinterpret_cast<void*>(m_FinalFrameBuffer->GetColorMap()), { m_ViewportSize.x, m_ViewportSize.y }, { 0,1 }, { 1, 0 });
	if (HasVisibleViewport())
        if (auto image = UI::FramebufferImage(*m_RenderTarget, m_ViewportSize.x, m_ViewportSize.y); !image)
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
        auto position = ViewportPixelAt(mouse.x - m_ViewportBounds[0].x, mouse.y - m_ViewportBounds[0].y,
            GetEditorViewportLogicalSize(), {storage.Width, storage.Height});
        if (!position || !HasVisibleViewport()) return;
        const int x = position->X, y = position->Y;
		auto pixel = m_MousePickFrameBuffer->ReadPixel(x, y);
		if (!pixel) { ReportFramebufferError("picking read", pixel.error()); return; }
		m_HoveredEntity = {};
        if (*pixel != EntityPickTable::InvalidPixel) {
            auto id = m_ActiveScene->RenderData().ResolvePick(m_PickTable,*pixel);
            if (!id) { Log::GetCoreLogger()->warn("Stale/invalid picking result: {}",int(id.error())); return; }
            auto entity = m_ActiveScene->RenderData().Resolve(*id);
            if (!entity) { Log::GetCoreLogger()->warn("Picking entity: {}",int(entity.error())); return; }
            m_HoveredEntity = _Entity{*entity,m_ActiveScene.get()};
        }
		//GENGINE_CORE_INFO("Mouse Position: {}, {}; Entity {} has been clicked",x,y,m_HoveredEntity?m_HoveredEntity.GetName():"None");
		std::println("Mouse Position: {}, {}; Entity {} has been clicked", x, y, m_HoveredEntity ? m_HoveredEntity.GetName() : "None");
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
    if (!HasVisibleViewport()) return;
    const auto width = m_RenderTarget->GetWidth(), height = m_RenderTarget->GetHeight();
    if (!width || !height) return;
    m_EditorCamera_.SetViewportSize(static_cast<float>(width), static_cast<float>(height));

}


BaseApp* CreateApp()
{
	return new RigidBodySimulationApp();
}
