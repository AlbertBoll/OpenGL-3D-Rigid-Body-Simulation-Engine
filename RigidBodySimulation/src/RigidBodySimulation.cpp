#include "RigidBodySimulation.h"
#include "EntryPoint.h"
#include "Managers/ShapeManager.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShaderManager.h"
#include <Core/Log.h>
#include <imgui/imgui.h>
#include "Assets/Textures/Texture.h"
#include "Core/RenderSystem.h"
#include "Windows/SDLWindow.h"
#include <Shapes/Box.h>
#include <Physics/ShapeBox.h>
#include <Physics/PhysicsWorld.h>
#include <Physics/GJK.h>
#include <Shapes/Sphere.h>
#include <Shapes/Cylinder.h>


static std::string base_shader_dir = "../GEngine/include/GEngine/Assets/Shaders/";
static std::string image_base_dir = "../GEngine/include/GEngine/Assets/Images/";
static std::string image_extension = ".png";

	//using namespace Audio;

RigidBodySimulationApp::~RigidBodySimulationApp()
{
	if (m_AudioSystem)
		m_AudioSystem->Shutdown();
}

void RigidBodySimulationApp::Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)
{
	

	//Initialize BaseApp 
	BaseApp::Initialize(WindowsPropertyList);

	GENGINE_CORE_INFO("Initialize Audio System...");
	m_AudioSystem = CreateScopedPtr<Audio::AudioSystem>();
	m_AudioSystem->Initialize();

	GENGINE_CORE_INFO("Initialize Render System...");
	RenderSystem::Initialize();

	m_FarPlane = 100;

	//initialize scene and camera
	m_EditorScene = CreateRefPtr<_Scene>();
	m_ActiveScene = m_EditorScene;
	m_EditorCamera_ = _EditorCamera(45.0f, 1280.f, 720.f, 0.1f, 1000.f);
	float cameraFarClip = m_EditorCamera_.GetFarClip();
	m_ShadowCascadeLevels = {cameraFarClip / 50.f, cameraFarClip / 25.0f, cameraFarClip / 10.0f, cameraFarClip / 2.f,  cameraFarClip};

	auto cascadeShadowMapShader = ShaderManager::GetShaderProgram({ base_shader_dir + "shadow_mapping_depth.vert", base_shader_dir + "shadow_mapping_depth.gs", base_shader_dir + "shadow_mapping_depth.frag" });
	auto cascadedRenderShader = ShaderManager::GetShaderProgram({ base_shader_dir + "pbr_cascade_shadow.vert", base_shader_dir + "pbr_cascade_shadow.frag" });
	auto pointLightRenderShader = ShaderManager::GetShaderProgram({ base_shader_dir + "pbr_cascade_shadow.vert", base_shader_dir + "point_light_sphere_visual.frag" });

	using namespace Shape;
	//auto smoothSphereGeo = ShapeManager::_GetShape<Sphere>("Sphere", 1);
	auto pointLightGeo = ShapeManager::GetShape("PointLight");
	auto smoothSphereGeo = ShapeManager::GetShape("Sphere");
	auto DiamondGeo = ShapeManager::GetShape("Diamond");
	//auto floorSphereGeo = ShapeManager::GetShape("FloorSphere");
	auto lightShadowPreRenderComponent = PreRenderPassComponent{};
	lightShadowPreRenderComponent.Shader = cascadeShadowMapShader;


	auto pointLightRenderComponent = RenderComponent{};
	pointLightRenderComponent.Shader = pointLightRenderShader;
	pointLightRenderComponent.RenderSettings.m_PrimitivesSetting.surfaceSetting = {};
	pointLightRenderComponent.RenderSettings.m_PrimitivesSetting.surfaceSetting.bDoubleSide = false;
	pointLightRenderComponent.RenderSettings.DrawMode = pointLightGeo->IsUsingIndexBuffer() ? DrawMode_::Elements : DrawMode_::Arrays;
	//lightRenderComponent.RenderSettings.DrawStyle = DrawStyle_::TRIANGLE_STRIP;
	pointLightRenderComponent.RenderSettings.DrawStyle = DrawStyle_::TRIANGLES;


	auto lightShadowRenderComponent = RenderComponent{};
	lightShadowRenderComponent.Shader = cascadedRenderShader;
	lightShadowRenderComponent.RenderSettings.m_PrimitivesSetting.surfaceSetting = {};
	lightShadowRenderComponent.RenderSettings.m_PrimitivesSetting.surfaceSetting.bDoubleSide = false;
	lightShadowRenderComponent.RenderSettings.DrawMode = smoothSphereGeo->IsUsingIndexBuffer() ? DrawMode_::Elements : DrawMode_::Arrays;
	//lightRenderComponent.RenderSettings.DrawStyle = DrawStyle_::TRIANGLE_STRIP;
	lightShadowRenderComponent.RenderSettings.DrawStyle = DrawStyle_::TRIANGLES;

	//Load icon
	Texture* m_IconPlay = AssetsManager::GetTexture("Icons/PlayButton");
	Texture* m_IconPause = AssetsManager::GetTexture("Icons/PauseButton");
	Texture* m_IconStep = AssetsManager::GetTexture("Icons/StepButton");
	Texture* m_IconSimulate = AssetsManager::GetTexture("Icons/SimulateButton");
	Texture* m_IconStop = AssetsManager::GetTexture("Icons/StopButton");

	//Load Sphere Texture
	Texture* wood_diffuse = AssetsManager::GetTexture("Sphere/wood_diffuse", "diffuseTexture");
	Texture* cascade_shadow_depth_map = AssetsManager::GetCascadedFrameBufferTexture(*m_CascadeShadowFrameBuffer, "shadowMap");
	Texture* point_shadow_depth_map = AssetsManager::GetPointShadowFrameBufferTexture(*m_PointShadowFrameBuffer, "pointShadowDepthMap");
	Texture* gloss_diffuse = AssetsManager::GetTexture("Sphere/Tiles012_4K-JPG_Color", "diffuseTexture");

	//TexturesComponent sphereTextureComp({ gloss_diffuse, point_shadow_depth_map, cascade_shadow_depth_map });
	//TexturesComponent boxTextureComp({ wood_diffuse, point_shadow_depth_map, cascade_shadow_depth_map });
	////TexturesComponent sphereTextureComp({ wood_diffuse,  wood_metallic});
	//sphereTextureComp.PreBindTextures(cascadedRenderShader);
	//boxTextureComp.PreBindTextures(cascadedRenderShader);


	//Load PBR Texture for sphere
	/*Texture* sphere_albedo = AssetsManager::GetTexture("PBR/subtle_black_granite/subtle-black-granite_albedo", "albedoMap");
	Texture* sphere_normal = AssetsManager::GetTexture("PBR/subtle_black_granite/subtle-black-granite_normal-dx", "normalMap");
	Texture* sphere_metallic = AssetsManager::GetTexture("PBR/subtle_black_granite/subtle-black-granite_metallic", "metallicMap");
	Texture* sphere_roughness = AssetsManager::GetTexture("PBR/subtle_black_granite/subtle-black-granite_roughness", "roughnessMap");
	Texture* sphere_ao = AssetsManager::GetTexture("PBR/subtle_black_granite/subtle-black-granite_ao", "aoMap");*/
	Texture* sphere_albedo = AssetsManager::GetTexture("PBR/rustediron/rustediron2_basecolor", "albedoMap");
	Texture* sphere_normal = AssetsManager::GetTexture("PBR/rustediron/rustediron2_normal", "normalMap");
	Texture* sphere_metallic = AssetsManager::GetTexture("PBR/rustediron/rustediron2_metallic", "metallicMap");
	Texture* sphere_roughness = AssetsManager::GetTexture("PBR/rustediron/rustediron2_roughness", "roughnessMap");
	Texture* sphere_ao = AssetsManager::GetTexture("PBR/subtle_black_granite/subtle-black-granite_ao", "aoMap");

	//Load PBR Texture for sphere
	Texture* floor_albedo = AssetsManager::GetTexture("PBR/base_white_tile/base-white-tile_albedo", "albedoMap");
	Texture* floor_normal = AssetsManager::GetTexture("PBR/base_white_tile/base-white-tile_normal-dx", "normalMap");
	Texture* floor_metallic = AssetsManager::GetTexture("PBR/base_white_tile/base-white-tile_metallic", "metallicMap");
	Texture* floor_roughness = AssetsManager::GetTexture("PBR/base_white_tile/base-white-tile_roughness", "roughnessMap");
	Texture* floor_ao = AssetsManager::GetTexture("PBR/base_white_tile/base-white-tile_ao", "aoMap");

	//TexturesComponent sphereTextureComp({ gloss_diffuse, point_shadow_depth_map, cascade_shadow_depth_map });
	TexturesComponent sphereTextureComp({ sphere_albedo, sphere_normal, sphere_metallic, sphere_roughness, sphere_ao, point_shadow_depth_map, cascade_shadow_depth_map });
	TexturesComponent boxTextureComp({ wood_diffuse, point_shadow_depth_map, cascade_shadow_depth_map });
	//TexturesComponent sphereTextureComp({ wood_diffuse,  wood_metallic});
	sphereTextureComp.PreBindTextures(cascadedRenderShader);
	boxTextureComp.PreBindTextures(cascadedRenderShader);


	//Load ambient light
	auto ambientLightEntity = m_ActiveScene->CreateEntity("ambient_light");
	m_PointLightEntity = m_ActiveScene->CreateEntity("point_light");
	

	DirectionalLightComponent dirLightComp;
	m_LightDirection = glm::normalize(Vec3f{ 20.f, 50.0f, 20.f });
	m_LightPos = Vec3f{ 0.f, 15.f, -10.f };

	PointLightComponent pointLightComp;
	pointLightComp.position = { "lightPos", m_LightPos };
	pointLightComp.ambient = { "pointlightColor", {0.8f, 0.2f, 0.1f} };
	//pointLightComp.ambient = { "pointlightColor", {1.f, 1.f, 1.f} };
	//pointLightComp.ambient = { "pointlightColor", {0.5f, 0.5f, 0.5f} };


	dirLightComp.direction = { "lightDir", m_LightDirection };
	dirLightComp.ambient = { "directionallightColor", {0.7f, 0.7f, 0.7f} };
	/*dirLightComp.diffuse = {"u_dirLight.diffuse", {0.4f, 0.4f, 0.4f}};
	dirLightComp.specular = { "u_dirLight.specular", {0.2f, 0.2f, 0.2f} };*/
	ambientLightEntity.AddOrReplaceComponent<DirectionalLightComponent>(dirLightComp);
	ambientLightEntity.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);

	m_PointLightEntity.AddOrReplaceComponent<PointLightComponent>(pointLightComp);
	m_PointLightEntity.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	m_PointLightEntity.AddOrReplaceComponent<Transform3DComponent>(m_LightPos);
	//pointLightEntity.AddOrReplaceComponent<RenderComponent>(pointLightRenderComponent);
	m_PointLightEntity.AddOrReplaceComponent<MeshComponent>(pointLightGeo);
	m_ActiveScene->PushToRenderList(ambientLightEntity);
	m_ActiveScene->PushToRenderList(m_PointLightEntity);


	MaterialComponent matComp;
	matComp.Metalness = { "metalness", Vec3f(0.8f)};




	RigidBody3DComponent rigidBodyComp;
	rigidBodyComp.Type = BodyType::Dynamic;

	SphereFixture3DComponent sphereFixtureComp;

	sphereFixtureComp.Radius = 1.f;
	sphereFixtureComp.Property.m_Elasticity = 0.5f;
	sphereFixtureComp.Property.m_Friction = 0.5f;
	sphereFixtureComp.Property.m_InvMass = 1.f;
	//sphereFixtureComp.Property.m_LinearVelocity = { -60.f, 0.f, 0.f };
	sphereFixtureComp.Property.m_LinearVelocity = { 0.f, 0.f, 0.f };
	
	_Entity woodSphereEntity = m_ActiveScene->CreateEntity("wood_sphere_0");
	//smoothSphereGeo->AddEntityID(int((entt::entity)woodSphereEntity));
	woodSphereEntity.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	woodSphereEntity.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
	woodSphereEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 30.f, 5.0f, 0.f });
	//sphereFixtureComp.Radius *= woodSphereEntity.GetComponent<Transform3DComponent>().Scale.x;
	sphereFixtureComp.Property.m_Position = woodSphereEntity.GetComponent<Transform3DComponent>().Translation;
	sphereFixtureComp.Property.m_Orientation = woodSphereEntity.GetComponent<Transform3DComponent>().QuatRotation;
	woodSphereEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	woodSphereEntity.AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixtureComp);
	woodSphereEntity.AddOrReplaceComponent<TexturesComponent>(sphereTextureComp);
	woodSphereEntity.AddOrReplaceComponent<MeshComponent>(smoothSphereGeo);
	//woodSphereEntity.AddOrReplaceComponent<DirectionalLightComponent>(dirLightComp);
	woodSphereEntity.AddOrReplaceComponent<MaterialComponent>(matComp);
	//m_ActiveScene->PushToRenderList(woodSphereEntity);


	//ConvexFixture3DComponent convexFixtureComp;

	//
	//convexFixtureComp.Property.m_Elasticity = 0.5f;
	//convexFixtureComp.Property.m_Friction = 0.5f;
	//convexFixtureComp.Property.m_InvMass = 1.f;
	//convexFixtureComp.Property.m_AngularVelocity = { 5.f, 0.f, 5.f };
	//convexFixtureComp.Property.m_LinearVelocity = { 60.f, 0.f, 0.f };
	//_Entity DiamondEntity = m_ActiveScene->CreateEntity("Diamond");
	//DiamondEntity.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	//DiamondEntity.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
	//DiamondEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ -30, 5.f, 0 });
	////sphereFixtureComp.Radius *= woodSphereEntity.GetComponent<Transform3DComponent>().Scale.x;
	//convexFixtureComp.Property.m_Position = DiamondEntity.GetComponent<Transform3DComponent>().Translation;
	//convexFixtureComp.Property.m_Orientation = DiamondEntity.GetComponent<Transform3DComponent>().QuatRotation;
	//DiamondEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	//DiamondEntity.AddOrReplaceComponent<ConvexFixture3DComponent>(convexFixtureComp);
	//DiamondEntity.AddOrReplaceComponent<TexturesComponent>(sphereTextureComp);
	//DiamondEntity.AddOrReplaceComponent<MeshComponent>(DiamondGeo);
	////woodSphereEntity.AddOrReplaceComponent<DirectionalLightComponent>(dirLightComp);
	//DiamondEntity.AddOrReplaceComponent<MaterialComponent>(matComp);
	//m_ActiveScene->PushToRenderList(DiamondEntity);
	//int count = 0;
	//int offset = 2;
	//for (int z = 1; z < 4; z++)
	//{
	//	for (int x = 0; x < 4; x++)
	//	{
	//		for (int y = 0; y < 4; y++)
	//		{
	//			float yy = float(z - 1) * sphereFixtureComp.Radius * 2.f;
	//			float xx = float(x - 1) * sphereFixtureComp.Radius * 2.f;
	//			float zz = float(y - 1) * sphereFixtureComp.Radius * 2.f;
	//			_Entity DiamondEntity = m_ActiveScene->CreateEntity("Diamond_" + std::to_string(count));

	//			//DiamondGeo->AddEntityID(int((entt::entity)DiamondEntity));
	//			//DiamondGeo->AddAttributes(std::vector<Vec1i>(DiamondGeo->GetVerticesCount(), DiamondEntity));
	//			DiamondEntity.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	//			DiamondEntity.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
	//			DiamondEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ xx, 20.f + yy, zz });
	//			//sphereFixtureComp.Radius *= woodSphereEntity.GetComponent<Transform3DComponent>().Scale.x;
	//			convexFixtureComp.Property.m_Position = DiamondEntity.GetComponent<Transform3DComponent>().Translation;
	//			convexFixtureComp.Property.m_Orientation = DiamondEntity.GetComponent<Transform3DComponent>().QuatRotation;
	//			DiamondEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	//			DiamondEntity.AddOrReplaceComponent<ConvexFixture3DComponent>(convexFixtureComp);
	//			DiamondEntity.AddOrReplaceComponent<TexturesComponent>(sphereTextureComp);
	//			DiamondEntity.AddOrReplaceComponent<MeshComponent>(DiamondGeo);
	//			//woodSphereEntity.AddOrReplaceComponent<DirectionalLightComponent>(dirLightComp);
	//			DiamondEntity.AddOrReplaceComponent<MaterialComponent>(matComp);
	//			m_ActiveScene->PushToRenderList(DiamondEntity);
	//			count++;
	//		}
	//	}
	//}
	
	static int i = 0;
	//load dynamic sphere body
	for (int z = 1; z < 5; z++)
	{
		for (int x = 0; x < 4; x++)
		{
			for (int y = 0; y < 4; y++)
			{
				float yy = float(z - 1) * sphereFixtureComp.Radius * 2.f;
				float xx = float(x - 1) * sphereFixtureComp.Radius * 2.f;
				float zz = float(y - 1) * sphereFixtureComp.Radius * 2.f;
				_Entity woodSphereEntity = m_ActiveScene->CreateEntity("wood_sphere" + std::to_string(i++));
				woodSphereEntity.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
				woodSphereEntity.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
				woodSphereEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ xx, 10.f + yy, zz }, Vec3f{ 1.f, 1.f, 1.f });
				//sphereFixtureComp.Radius *= woodSphereEntity.GetComponent<Transform3DComponent>().Scale.x;
				sphereFixtureComp.Property.m_Position = woodSphereEntity.GetComponent<Transform3DComponent>().Translation;
				sphereFixtureComp.Property.m_Orientation = woodSphereEntity.GetComponent<Transform3DComponent>().QuatRotation;
				woodSphereEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
				woodSphereEntity.AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixtureComp);
				woodSphereEntity.AddOrReplaceComponent<TexturesComponent>(sphereTextureComp);
				woodSphereEntity.AddOrReplaceComponent<MeshComponent>(smoothSphereGeo);
				//woodSphereEntity.AddOrReplaceComponent<DirectionalLightComponent>(dirLightComp);
				woodSphereEntity.AddOrReplaceComponent<MaterialComponent>(matComp);
				m_ActiveScene->PushToRenderList(woodSphereEntity);
			}
		}
	}


	//load dynamic box body
	//auto box = ShapeManager::_GetShape<Shape::Box>("wood box", 2.f, 2.f, 2.f);
	auto box = ShapeManager::GetShape("Box");		
	BoxFixture3DComponent boxFixtureComp;
	boxFixtureComp.Property.m_InvMass = 1.f;
	boxFixtureComp.Property.m_Friction = 0.5f;
	boxFixtureComp.Property.m_Elasticity = 0.5f;
	//_Entity woodBoxEntity = m_ActiveScene->CreateEntity("wood_box");
	////box->AddEntityID(int((entt::entity)woodBoxEntity));
	////box->AddAttributes(std::vector<Vec1i>(box->GetVerticesCount(), woodBoxEntity));
	//woodBoxEntity.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	//woodBoxEntity.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
	//woodBoxEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ -30.f , 10.f, 0.f });
	////woodBoxEntity.GetComponent<Transform3DComponent>().SetRotation({ Math::Pi / 4.f, 0.f, 0.f });
	//boxFixtureComp.Property.m_Position = woodBoxEntity.GetComponent<Transform3DComponent>().Translation;
	//boxFixtureComp.Property.m_Orientation = woodBoxEntity.GetComponent<Transform3DComponent>().QuatRotation;
	//woodBoxEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	//woodBoxEntity.AddOrReplaceComponent<BoxFixture3DComponent>(boxFixtureComp);
	//woodBoxEntity.AddOrReplaceComponent<TexturesComponent>(boxTextureComp);
	//woodBoxEntity.AddOrReplaceComponent<MeshComponent>(box);
	
	//float offset = 2.f;

	//for (int y = 0; y < 4; y++)
	//{
	//	for (int x = 0; x < 4; x++)
	//	{
	//		//float x = i % 2 == 0 ? -0.7f : 0.7f;
	//		_Entity woodBoxEntity = m_ActiveScene->CreateEntity("wood_box");

	//		woodBoxEntity.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	//		woodBoxEntity.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
	//		woodBoxEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ x * (offset+0.01f), 1.5f + y * offset, 0.f }, Vec3f{0.f}, Vec3f{2.f});
	//		//woodBoxEntity.GetComponent<Transform3DComponent>().SetRotation({ Math::Pi / 4.f, 0.f, 0.f });
	//		boxFixtureComp.Property.m_Position = woodBoxEntity.GetComponent<Transform3DComponent>().Translation;
	//		boxFixtureComp.Property.m_Orientation = woodBoxEntity.GetComponent<Transform3DComponent>().QuatRotation;
	//		woodBoxEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	//		woodBoxEntity.AddOrReplaceComponent<BoxFixture3DComponent>(boxFixtureComp);
	//		woodBoxEntity.AddOrReplaceComponent<TexturesComponent>(boxTextureComp);
	//		woodBoxEntity.AddOrReplaceComponent<MeshComponent>(box);
	//		m_ActiveScene->PushToRenderList(woodBoxEntity);

	//	}
	//}

	//m_ActiveScene->PushToRenderList(woodBoxEntity);
	//m_ActiveScene->PushToRenderList(DiamondEntity);
	//m_ActiveScene->PushToRenderList(woodSphereEntity);

	//Grid Configuration
	m_GridEntity = m_ActiveScene->CreateEntity("grid");
	auto basicShader = ShaderManager::GetShaderProgram({ base_shader_dir + "basic.vert", base_shader_dir + "basic.frag" });
	auto gridGeo = ShapeManager::GetShape("GridHelper");
	auto basicRenderComp = RenderComponent{};
	basicRenderComp.Shader = basicShader;
	basicRenderComp.RenderSettings.m_PrimitivesSetting.lineSetting.lineType = LineType_::Segments;
	basicRenderComp.RenderSettings.m_PrimitivesSetting.lineSetting.lineWidth = 1.f;
	basicRenderComp.RenderSettings.DrawStyle = DrawStyle_::LINES;
	basicRenderComp.RenderSettings.DrawMode = DrawMode_::Arrays;
	
	m_GridEntity.AddOrReplaceComponent<RenderComponent>(basicRenderComp);
	m_GridEntity.AddOrReplaceComponent<HelperMaterialComponent>();
	m_GridEntity.AddOrReplaceComponent<MeshComponent>(gridGeo);
	auto& transform = m_GridEntity.GetComponent<Transform3DComponent>();
	transform.SetRotation({ Math::Pi / 2.f, 0.f, 0.f });

	//Axis Configuration
	m_AxisEntity = m_ActiveScene->CreateEntity("axis");
	auto axisGeo = ShapeManager::GetShape("AxisHelper");
	basicRenderComp.RenderSettings.m_PrimitivesSetting.lineSetting.lineWidth = 3.f;
	m_AxisEntity.AddOrReplaceComponent<RenderComponent>(basicRenderComp);
	m_AxisEntity.AddOrReplaceComponent<HelperMaterialComponent>();
	m_AxisEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 0.f, 1.f, 0.f });
	m_AxisEntity.AddOrReplaceComponent<MeshComponent>(axisGeo);

	m_ActiveScene->PushToRenderList(m_AxisEntity);
	//m_ActiveScene->PushToRenderList(m_GridEntity, m_AxisEntity);



	//Load SkyBox

	RenderComponent skyBoxRenderComp;
	auto skyBoxShader = ShaderManager::GetShaderProgram({ base_shader_dir + "skybox_environment.vert", base_shader_dir + "skybox_environment.frag" });
	skyBoxRenderComp.Shader = skyBoxShader;
	skyBoxRenderComp.RenderSettings.m_PrimitivesSetting.surfaceSetting.lineWidth = 1.f;
	skyBoxRenderComp.RenderSettings.m_PrimitivesSetting.surfaceSetting.bDoubleSide = true;
	skyBoxRenderComp.RenderSettings.m_PrimitivesSetting.surfaceSetting.bWireFrame = false;
	skyBoxRenderComp.RenderSettings.DrawMode = DrawMode_::Arrays;
	skyBoxRenderComp.RenderSettings.DrawStyle = DrawStyle_::TRIANGLES;

	TextureInfo info;
	info.m_TextureSpec.m_TexTarget = 0x8513;
	info.m_TextureSpec.m_MagFilter = 0x2601;
	info.m_TextureSpec.m_MinFilter = 0x2601;
	info.m_TextureSpec.m_WrapR = 0x812F;
	info.m_TextureSpec.m_WrapS = 0x812F;
	info.m_TextureSpec.m_WrapT = 0x812F;
	info.b_CubeMap = true;
	info.b_HDR = false;
	info.b_GammaCorrection = true;
	Texture* tex1 = AssetsManager::GetTexture("SkyBox/Day/", "u_skyBoxDay", ".png", info);

	auto skyBoxTextureComp = TexturesComponent{ {tex1} };
	skyBoxTextureComp.PreBindTextures(skyBoxShader);

	m_SkyBoxEntity = m_ActiveScene->CreateEntity("Environment_SkyBox");
	m_SkyBoxEntity.AddOrReplaceComponent<TexturesComponent>(std::vector{ tex1 });


	
	m_SkyBoxEntity.AddOrReplaceComponent<RenderComponent>(skyBoxShader);

	auto skyBoxGeo = ShapeManager::GetShape("SkyBox");
	m_SkyBoxEntity.AddOrReplaceComponent<MeshComponent>(skyBoxGeo);


	
	//Load Plane
	_Entity planeEntity = m_ActiveScene->CreateEntity("wood_plane");
	
	//Load Plane Texture
	//AssetsManager::GetTexture("Sphere/wood_diffuse", "diffuseTexture");
	Texture* plane_diffuse = AssetsManager::GetTexture("Wall/wallpaper_albedo", "diffuseTexture");
	//Texture* plane_metallic = AssetsManager::GetTexture("Wall/wallpaper_metallic", "u_material.specular");

	//TexturesComponent planeTextureComp({ plane_diffuse, point_shadow_depth_map, cascade_shadow_depth_map });//, plane_metallic
	TexturesComponent planeTextureComp({ floor_albedo, floor_normal, floor_metallic, floor_roughness, floor_ao, point_shadow_depth_map, cascade_shadow_depth_map });//, plane_metallic
	planeTextureComp.Tiling = { "u_tiling", {2.f, 2.f} };

	//TexturesComponent planeTextureComp_({ plane_diffuse, point_shadow_depth_map, cascade_shadow_depth_map });//, plane_metallic
	TexturesComponent planeTextureComp_({ floor_albedo, floor_normal, floor_metallic, floor_roughness, floor_ao, point_shadow_depth_map, cascade_shadow_depth_map });//, plane_metallic
	planeTextureComp_.Tiling = { "u_tiling", {2.f, 0.2f} };


	planeTextureComp.PreBindTextures(cascadedRenderShader);
	planeTextureComp_.PreBindTextures(cascadedRenderShader);

	rigidBodyComp.Type = BodyType::Static;

	

	auto planeGeo = ShapeManager::GetShape("RectangularPlane");
	//planeGeo->AddEntityID(int((entt::entity)planeEntity));
	//planeGeo->AddAttributes(std::vector<Vec1i>(planeGeo->GetVerticesCount(), planeEntity));
	BoxFixture3DComponent planeFixtureComp;
	planeFixtureComp.Property.m_InvMass = 0.f;
	planeFixtureComp.Property.m_Friction = 0.5f;
	planeFixtureComp.Property.m_Elasticity = 0.5f;
	//planeFixtureComp.Bounds = planeGeo->GetBounds();
	matComp.Metalness = { "metalness", Vec3f(0.08f) };
	planeEntity.AddOrReplaceComponent<MeshComponent>(planeGeo);
	planeEntity.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
	planeEntity.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	planeEntity.AddOrReplaceComponent<TexturesComponent>(planeTextureComp);
	planeEntity.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	planeFixtureComp.Property.m_Position = planeEntity.GetComponent<Transform3DComponent>().Translation;
	planeFixtureComp.Property.m_Orientation = planeEntity.GetComponent<Transform3DComponent>().QuatRotation;
	planeEntity.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);
	//planeEntity.AddOrReplaceComponent<BoxFixture3DComponent>(sphereTextureComp);
	planeEntity.AddOrReplaceComponent<MaterialComponent>(matComp);
	m_ActiveScene->PushToRenderList(planeEntity);
	
	
	auto WallGeo_1 = ShapeManager::_GetShape<Box>("RectangularWall_1", 1, 10, 100);
	_Entity wallEntity_1 = m_ActiveScene->CreateEntity("wall_entity_1");
	//WallGeo_1->AddEntityID(int((entt::entity)wallEntity_1));
	//WallGeo_1->AddAttributes(std::vector<Vec1i>(WallGeo_1->GetVerticesCount(), wallEntity_1));
	wallEntity_1.AddOrReplaceComponent<MeshComponent>(WallGeo_1);
	wallEntity_1.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
	wallEntity_1.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	wallEntity_1.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	wallEntity_1.AddOrReplaceComponent<Transform3DComponent>(Vec3f{-50.f, 4.5f, 0.f});
	//planeFixtureComp.Property.m_Friction = 0.f;
	planeFixtureComp.Property.m_Position = wallEntity_1.GetComponent<Transform3DComponent>().Translation;
	planeFixtureComp.Property.m_Orientation = wallEntity_1.GetComponent<Transform3DComponent>().QuatRotation;
	wallEntity_1.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);
	wallEntity_1.AddOrReplaceComponent<TexturesComponent>(planeTextureComp_);
	wallEntity_1.AddOrReplaceComponent<MaterialComponent>(matComp);
	m_ActiveScene->PushToRenderList(wallEntity_1);

	//auto WallGeo_2 = ShapeManager::_GetShape<Box>("RectangularWall", 1, 10, 100);
	_Entity wallEntity_2 = m_ActiveScene->CreateEntity("wall_entity_2");
	//WallGeo_1->AddAttributes(std::vector<Vec1i>(WallGeo_1->GetVerticesCount(), wallEntity_2));
	//WallGeo_1->AddEntityID(int((entt::entity)wallEntity_2));
	wallEntity_2.AddOrReplaceComponent<MeshComponent>(WallGeo_1);
	wallEntity_2.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
	wallEntity_2.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	wallEntity_2.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	wallEntity_2.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 50.f, 4.5f, 0.f });
	planeFixtureComp.Property.m_Position = wallEntity_2.GetComponent<Transform3DComponent>().Translation;
	planeFixtureComp.Property.m_Orientation = wallEntity_2.GetComponent<Transform3DComponent>().QuatRotation;
	wallEntity_2.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);
	wallEntity_2.AddOrReplaceComponent<TexturesComponent>(planeTextureComp_);
	wallEntity_2.AddOrReplaceComponent<MaterialComponent>(matComp);
	m_ActiveScene->PushToRenderList(wallEntity_2);

	auto WallGeo_2 = ShapeManager::_GetShape<Box>("RectangularWall_2", 100.f, 10.f, 1.f);
	_Entity wallEntity_3 = m_ActiveScene->CreateEntity("wall_entity_3");
	//WallGeo_2->AddEntityID(int((entt::entity)wallEntity_3));
	//WallGeo_2->AddAttributes(std::vector<Vec1i>(WallGeo_2->GetVerticesCount(), wallEntity_3));
	wallEntity_3.AddOrReplaceComponent<MeshComponent>(WallGeo_2);
	wallEntity_3.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
	wallEntity_3.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	wallEntity_3.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	wallEntity_3.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 0.f, 4.5f, 50.f });
	planeFixtureComp.Property.m_Position = wallEntity_3.GetComponent<Transform3DComponent>().Translation;
	planeFixtureComp.Property.m_Orientation = wallEntity_3.GetComponent<Transform3DComponent>().QuatRotation;
	wallEntity_3.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);
	wallEntity_3.AddOrReplaceComponent<TexturesComponent>(planeTextureComp_);
	wallEntity_3.AddOrReplaceComponent<MaterialComponent>(matComp);
	m_ActiveScene->PushToRenderList(wallEntity_3);

	_Entity wallEntity_4 = m_ActiveScene->CreateEntity("wall_entity_3");
	//WallGeo_2->AddEntityID(int((entt::entity)wallEntity_4));
	//WallGeo_2->AddAttributes(std::vector<Vec1i>(WallGeo_2->GetVerticesCount(), wallEntity_4));
	wallEntity_4.AddOrReplaceComponent<MeshComponent>(WallGeo_2);
	wallEntity_4.AddOrReplaceComponent<PreRenderPassComponent>(lightShadowPreRenderComponent);
	wallEntity_4.AddOrReplaceComponent<RenderComponent>(lightShadowRenderComponent);
	wallEntity_4.AddOrReplaceComponent<RigidBody3DComponent>(rigidBodyComp);
	wallEntity_4.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 0.f, 4.5f, -50.f });
	planeFixtureComp.Property.m_Position = wallEntity_4.GetComponent<Transform3DComponent>().Translation;
	planeFixtureComp.Property.m_Orientation = wallEntity_4.GetComponent<Transform3DComponent>().QuatRotation;
	wallEntity_4.AddOrReplaceComponent<BoxFixture3DComponent>(planeFixtureComp);
	wallEntity_4.AddOrReplaceComponent<TexturesComponent>(planeTextureComp_);
	wallEntity_4.AddOrReplaceComponent<MaterialComponent>(matComp);
	m_ActiveScene->PushToRenderList(wallEntity_4);
	
	

	m_ActiveScene->OnRuntimeStart();

	//m_ActiveScene->
	//LightComponent ambient_light;
	//ambient_light.Color = { "u_lightColor[0]", Vec3f{0.3f, 0.3f, 0.3f} };
	//ambient_light.Attenuation = { "u_lightAttuentation[0]", Vec3f{1.f, 0.f, 0.f} };
	//ambientLightEntity.AddComponent<LightComponent>(ambient_light);
	//ambientLightEntity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{ 0.f, 1000.f, -7000.f });


	////Load Directional Light
	//auto directionalLightEntity = m_ActiveScene->CreateEntity("directional_light");
	//LightComponent directional_light;
	//directional_light.Color = { "u_lightColor[1]", Vec3f{0.3f, 0.3f, 0.3f} };
	//ambient_light.Attenuation = { "u_lightAttuentation[0]", Vec3f{1.f, 0.f, 0.f} };
	//ambientLightEntity.AddComponent<LightComponent>(ambient_light);





	//rigister window resize event and mouse click, scroll event
	auto AppPauseEvent = new Events<void()>("AppPause");
	auto AppResumeEvent = new Events<void()>("AppResume");
	auto WindowResizeEvent = new Events<void(WindowResizeParam)>("WindowResize");
	auto MouseScrollEvent = new Events<void(MouseScrollWheelParam)>("MouseScrollWheel");
	//auto MouseClickEvent = new Events<void(MouseButtonParam)>("MouseButtonPress");

	AppPauseEvent->Subscribe([this]() { m_IsPause = true; });
	AppResumeEvent->Subscribe([this]() { m_IsPause = false; });

	/*MouseClickEvent->Subscribe([this](const MouseButtonParam& mouseParam)
		{
			m_MousePickFrameBuffer->Bind();
			
			int x = mouseParam.X;
			int y = m_SDLWindow->GetScreenHeight() - mouseParam.Y;
			int pixel_data = m_MousePickFrameBuffer->ReadPixel(x, y);
			std::cout << "Mouse Clicked at: " << x << ", " << y << std::endl;
			std::cout << "Pixel Data: " << pixel_data << std::endl;
			m_MousePickFrameBuffer->UnBind();
		});*/

	WindowResizeEvent->Subscribe([this](const WindowResizeParam& windowParam)
		{

			auto& windows = GetWindowManager()->GetWindows();
			if (auto p = windows.find(windowParam.ID); p != windows.end())
			{
				if (windowParam.Width == 0 || windowParam.Height == 0)
					m_Minimized = true;

				else
					m_Minimized = false;
				//m_ActiveScene->OnViewportResize(windowParam.Width, windowParam.Height);
			
				if(p->second->GetScreenWidth() == windowParam.Width && p->second->GetScreenHeight() == windowParam.Height)
					return;
				m_EditorCamera_.SetViewportSize(windowParam.Width, windowParam.Height);
				RenderSystem::SetSurfaceSize(windowParam.Width, windowParam.Height);
				m_MousePickFrameBuffer->OnResize(windowParam.Width, windowParam.Height);
				m_SDLWindow->OnResize(windowParam.Width, windowParam.Height);
			}

		

		});

	MouseScrollEvent->Subscribe([this](const MouseScrollWheelParam& mousescrollParam)
		{

			m_EditorCamera_.OnMouseScroll(mousescrollParam.Y);
			
		});


	GetEventManager()->GetEventDispatcher().RegisterEvent(WindowResizeEvent);
	GetEventManager()->GetEventDispatcher().RegisterEvent(MouseScrollEvent);
	GetEventManager()->GetEventDispatcher().RegisterEvent(AppPauseEvent);
	GetEventManager()->GetEventDispatcher().RegisterEvent(AppResumeEvent);
	//GetEventManager()->GetEventDispatcher().RegisterEvent(MouseClickEvent);
}

void RigidBodySimulationApp::Initialize(const WindowProperties& prop)
{
	//Initialize BaseApp 
	BaseApp::Initialize(prop);

	Initialize({ prop });
}

void RigidBodySimulationApp::Update(Timestep ts)
{

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




	//m_ActiveScene->Update(ts);
	//update physics
	if (!m_IsPause)
	{
		for (int i = 0; i < 2; i++)
		{
			m_ActiveScene->Update(ts * 1 / 2.f);
		}
	}





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

void RigidBodySimulationApp::Render()
{

	static int i = 0;
	RenderSystem::GetRenderStats().m_ArrayDrawCall = 0;
	RenderSystem::GetRenderStats().m_ElementsDrawCall = 0;
	auto& windows = GetWindowManager()->GetWindows();

	using namespace GEngine;
	RenderParam_ param;
	//m_MousePickFrameBuffer.get()->Bind();
	param.ClearColor = { 0.1f, 0.1f, 0.1f, 1.f };
	param.bEnableDepthTest = true;
	param.bClearColorBit = true;
	param.bClearDepthBit = true;
	param.bClearStencilBit = true;

	//UBO SetUp
	RenderSystem::SetupUBO(*m_UniformBufferObject, m_EditorCamera_, m_LightDirection, m_ShadowCascadeLevels);

	RenderSystem::Set(param);
	//begin render
	//RenderSystem::BeginRender(m_EditorCamera_);

	//Mouse Pick pass
	auto mousePickShader = ShaderManager::GetShaderProgram({ base_shader_dir + "mouse_pick.vert", base_shader_dir + "mouse_pick.frag" });
	RenderSystem::MousePickPass(m_ActiveScene.get(), m_EditorCamera_, mousePickShader, *m_MousePickFrameBuffer.get());

	//point shadow pass
	auto pointLightShadowShader = ShaderManager::GetShaderProgram({ base_shader_dir + "point_shadows_depth.vert", base_shader_dir + "point_shadows_depth.gs", base_shader_dir + "point_shadows_depth.frag" });
	RenderSystem::PointShadowPass(m_ActiveScene.get(), pointLightShadowShader, *m_PointShadowFrameBuffer, m_LightPos, m_NearPlane, m_FarPlane);

	//cascade shadow pass
	auto cascadeShadowMapShader = ShaderManager::GetShaderProgram({ base_shader_dir + "shadow_mapping_depth.vert", base_shader_dir + "shadow_mapping_depth.gs", base_shader_dir + "shadow_mapping_depth.frag" });
	RenderSystem::CascadedShadowPass(m_ActiveScene.get(), cascadeShadowMapShader, *m_CascadeShadowFrameBuffer);

	



	// scene render to frame buffer
	//auto cascadeSceneShader = ShaderManager::GetShaderProgram({ base_shader_dir + "shadow_mapping.vert", base_shader_dir + "shadow_mapping.frag" });
	/*RenderSystem::CascadedShadowScenePass(m_ActiveScene.get(), m_EditorCamera_, cascadeSceneShader,  m_ShadowCascadeLevels, *m_FinalFrameBuffer);
	m_FinalFrameBuffer->BindReadFrameBuffer();
	m_FinalFrameBuffer->BindDefaultDrawFrameBuffer();
	m_FinalFrameBuffer->BlitFrameBuffer();*/
	//m_FinalFrameBuffer->UnBind();

	//start normal render
	RenderSystem::BeginRender(m_EditorCamera_);
	RenderSystem::CascadedShadowSceneRender(m_ActiveScene.get(), m_EditorCamera_, m_ShadowCascadeLevels, m_FarPlane);

	//visualize point lights
	auto visualShader = ShaderManager::GetShaderProgram({ base_shader_dir + "point_light_sphere_visual.vert", base_shader_dir + "point_light_sphere_visual.frag" });
	RenderSystem::PointLightsVisualize(m_ActiveScene.get(), m_EditorCamera_, visualShader);
	
	////RenderSystem::SceneRender(m_ActiveScene.get(), m_EditorCamera_);
	RenderSystem::SkyBoxRender(m_SkyBoxEntity, m_EditorCamera_);

	//if (m_RenderTarget && m_RenderTarget->IsMultiSampled()) m_RenderTarget->BindAndBlitToScreen();

	//if (m_RenderTarget)
	//	m_RenderTarget->UnBind();

	for (auto& [windowID, window] : windows)
	{
		auto window_ = static_cast<SDLWindow*>(window.get());
		window_->GetImGuiWindow()->BeginRender(window_);
		ImGuiRender();
		window_->GetImGuiWindow()->EndRender(window_);
		window_->SwapBuffer();
	}


}


void RigidBodySimulationApp::ImGuiRender()
{
	/**
	// Note: Switch this to true to enable dockspace
	static bool dockspaceOpen = true;
	static bool opt_fullscreen_persistant = true;
	bool opt_fullscreen = opt_fullscreen_persistant;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

	// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
	// because it would be confusing to have two docking targets within each others.
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	if (opt_fullscreen)
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}

	*/

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
		auto mousePos = GetInputManager()->GetMouseState().m_MousePos;
		int x = mousePos.x;
		int y = m_SDLWindow->GetScreenHeight() - mousePos.y;
		//m_MousePickFrameBuffer->Bind();
		int pixel_data = m_MousePickFrameBuffer->ReadPixel(x, y);
		std::cout << "Mouse Clicked at: " << x << ", " << y << std::endl;
		//std::cout << "Pixel Data: " << pixel_data << std::endl;
		//m_MousePickFrameBuffer->UnBind();
	}
}




BaseApp* CreateApp()
{
	return new RigidBodySimulationApp();
}