#include "SceneApp.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShapeManager.h"
#include "Managers/ShaderManager.h"
#include "Material/TextureMaterial.h"
#include "Character/Character.h"
#include "Camera/PerspectiveCamera.h"
#include "EntryPoint.h"
#include "Core/Renderer.h"
#include <external/imgui/imgui.h>
#include <GEngine/Core/Log.h>
#include <GEngine/Shapes/SmoothSphere.h>
#include <external/glm/gtc/type_ptr.hpp>
#include <GEngine/Extras/Grid.h>
#include <Camera/PlayerCamera.h>
#include "Extras/CameraRig.h"
#include "Core/RenderTarget.h"
#include <GEngine/Material/BasicMaterial.h>
#include <Scene/BoxEntity.h>
#include <Scene/SkyBoxEntity.h>
#include <imguizmo/ImGuizmo.h>
#include <Math/Matrix.h>
#include <Core/SceneGraph.h>
#include <GEngine/Core/RawModel.h>
#include <GEngine/Material/LightTextureMaterial.h>
#include <GEngine/Material/TerrainLightMaterial.h>
#include <random>
#include <iostream>
#include "Shapes/Terrain.h"
#include <Material/NormalLightTextureMaterial.h>
#include "Light/LightEntity.h"
#include <Animation/Animation.h>
#include <Material/AnimatedMaterial.h>
#include "Animation/AnimatedModel.h"

//#include "Audio/AudioSystem.h"
#include <fmod/fmod_studio.hpp>

SceneApp::~SceneApp()
{
	Manager::AssetsManager::FreeAllResources();
	Manager::ShaderManager::FreeShader();
	Manager::ShapeManager::FreeShape();
	if(m_AudioSystem)
		m_AudioSystem->Shutdown();
	if (m_EditorCamera) delete m_EditorCamera;
	
}

void SceneApp::Update(Timestep ts)
{

	m_AnimationSystem->UpdateAnimation(ts);
	if (m_ViewportSize.x > 0.f && m_ViewportSize.y > 0.f && (m_RenderTarget->GetWidth() != m_ViewportSize.x || m_RenderTarget->GetHeight() != m_ViewportSize.y))
	{
		m_EditorCamera->OnResize((int)m_ViewportSize.x, (int)m_ViewportSize.y);
		m_RenderTarget->OnResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
	}

	
	if (m_ViewportForcused)
	{
		m_CameraRig->Update(ts);
	
	}

	m_SkyBox->RotateY(ts * 0.01f);
	m_AudioSystem->Update(ts);

	auto backgroundMusic = m_MusicEvent.GetPlayState();
	if (backgroundMusic == Audio::PLAYBACK_STOPPING)
		m_MusicEvent.Restart();

	
}

void SceneApp::Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)
{
	using namespace Camera;
	using namespace Manager;
	using namespace Shape;
	using namespace SceneObjects;

	//Initialize BaseApp 
	BaseApp::Initialize(WindowsPropertyList);

	GENGINE_CORE_INFO("Initialize Audio System...");
	
	m_AudioSystem = CreateScopedPtr<Audio::AudioSystem>();
	m_AudioSystem->Initialize();

	GENGINE_CORE_INFO("Initialize 3D Renderer...");
	Renderer::Initialize();


	auto width = WindowsPropertyList.begin()->m_Width;
	auto height = WindowsPropertyList.begin()->m_Height;

	GENGINE_CORE_INFO("Initialize Perspective Camera...");
	m_EditorCamera = new PerspectiveCamera(
		45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 10000.f);
	m_EditorCamera->SetTag("Editor Camera");
	m_EditorCamera->SetActive(true);
	
	//barrel code
	auto barrelTexNormal = AssetsManager::GetTexture("barrelNormalreflection", "u_normalTexture");
	auto barrelTexDiffuse = AssetsManager::GetTexture("barrelreflection", "u_diffuseTexture");
	auto textures = { barrelTexDiffuse , barrelTexNormal };
	auto barrelMaterial = CreateRefPtr<NormalLightTextureMaterial>(textures, "normal");
	auto barrelGeo = ShapeManager::GetModel("barrel");

	
	auto barrel = new Entity(barrelGeo, barrelMaterial);
	
	barrel->SetTag("barrel");

	barrel->SetPosition({ 40.f, 5.f, -60.f });
	

	
	auto terrainGeo = new Terrain(0, -1, 900);


	
	LightComponents lights;
	lights.Positions.Name = "u_lightPosition";
	Vec3f lightPos1 = {0.f, 1000.f, -7000.f};
	Vec3f lightPos2 = { 185, terrainGeo->GetTerrainHeight(185, -293) + 14, -293 };
	Vec3f lightPos3 = { 370, terrainGeo->GetTerrainHeight(370, -300) + 14, -300 };
	Vec3f lightPos4 = { 293, terrainGeo->GetTerrainHeight(293, -305) + 14, -305 };

	lights.Positions.Data = { lightPos1, lightPos2, lightPos3, lightPos4 };



	lights.Colors.Name = "u_lightColor";

	Vec3f lightCol1 = {0.3f, 0.3f, 0.3f};
	Vec3f lightCol2 = {2.f, 0.f, 0.f};
	Vec3f lightCol3 = {0, 2, 2.f};
	Vec3f lightCol4 = {2, 2, 0};

	Vec3f lightCol5 = { 0, 1, 1 };

	Vec3f lightCol6 = { 1, 0, 1 };

	lights.Colors.Data = { lightCol1, lightCol2, lightCol3, lightCol4};

	lights.Attenuations.Name = "u_lightAttuentation";
	Vec3f lightAtt1 = { 1.f, 0.f, 0.f };
	Vec3f lightAtt2 = { 1.f, 0.01f, 0.002f };
	Vec3f lightAtt3 = { 1.f, 0.01f, 0.002f };
	Vec3f lightAtt4 = { 1.f, 0.01f, 0.002f };
	Vec3f lightAtt5 = { 1.f, 0.01f, 0.002f };
	Vec3f lightAtt6 = { 1.f, 0.01f, 0.002f };

	lights.Attenuations.Data = { lightAtt1, lightAtt2, lightAtt3, lightAtt4 };

	LightEntity* ambient_light = new LightEntity();
	ambient_light->SetTag("Ambient Light");
	ambient_light->SetPos("u_lightPosition")
				 ->SetColor("u_lightColor", lightCol1)
				 ->SetAttenuation("u_lightAttuentation", lightAtt1);
	ambient_light->SetPosition(lightPos1);



	LightEntity* lamp_light_1 = new LightEntity();
	lamp_light_1->SetTag("LampLight_1");
	lamp_light_1->SetPos("u_lightPosition")
				->SetColor("u_lightColor", lightCol2)
				->SetAttenuation("u_lightAttuentation", lightAtt2);

	lamp_light_1->SetPosition(0, 14.f, 0);



	LightEntity* lamp_light_2 = new LightEntity();
	lamp_light_2->SetTag("LampLight_2");
	lamp_light_2->SetPos("u_lightPosition")
				->SetColor("u_lightColor", lightCol3)
				->SetAttenuation("u_lightAttuentation", lightAtt3);

	lamp_light_2->SetPosition(0, 14.f, 0);


	LightEntity* lamp_light_3 = new LightEntity();
	lamp_light_3->SetTag("LampLight_3");
	lamp_light_3->SetPos("u_lightPosition")
				->SetColor("u_lightColor", lightCol4)
				->SetAttenuation("u_lightAttuentation", lightAtt4);


	lamp_light_3->SetPosition(0, 14.f, 0);


	LightEntity* lamp_light_4 = new LightEntity();
	lamp_light_4->SetTag("LampLight_4");
	lamp_light_4->SetPos("u_lightPosition")
		->SetColor("u_lightColor", lightCol5)
		->SetAttenuation("u_lightAttuentation", lightAtt5);


	lamp_light_4->SetPosition(0, 0, 9);

	LightEntity* lamp_light_5 = new LightEntity();
	lamp_light_5->SetTag("LampLight_5");
	lamp_light_5->SetPos("u_lightPosition")
		->SetColor("u_lightColor", lightCol6)
		->SetAttenuation("u_lightAttuentation", lightAtt6);


	lamp_light_5->SetPosition(0, 0, 9);

	barrel->Add(lamp_light_4);


	m_Scene->Push({ ambient_light, lamp_light_1, lamp_light_2, lamp_light_3, lamp_light_4, lamp_light_5 });


	FogComponent fog;
	fog.Density.Name = "u_density";
	fog.Density.Data = 0.002f;
	fog.Gradient.Name = "u_gradient";
	fog.Gradient.Data = 5.f;

	TextureComponent MultiTexture;
	MultiTexture.NumOfRows.Name = "u_numberOfRows";
	MultiTexture.NumOfRows.Data = 2;

	TextureComponent normalTexture;
	normalTexture.NumOfRows.Name = "u_numberOfRows";
	normalTexture.NumOfRows.Data = 1;

	SkyBoxComponent skyBoxComponent;
	skyBoxComponent.FogAmbientColor.Name = "u_FogColor";
	skyBoxComponent.FogAmbientColor.Data = {0.5f, 0.5f, 0.5f};
	skyBoxComponent.LowerLimit.Name = "u_LowerLimit";
	skyBoxComponent.LowerLimit.Data = 0.f;
	
	skyBoxComponent.UpperLimit.Name = "u_UpperLimit";
	skyBoxComponent.UpperLimit.Data = 100.f;

	skyBoxComponent.BlendFactor.Name = "u_BlendFactor";
	skyBoxComponent.BlendFactor.Data = 0.5f;

	auto terrainBackGroundTex = AssetsManager::GetTexture("grassy3", "u_backgroundTexture");
	auto terrainRTex = AssetsManager::GetTexture("dirt", "u_rTexture");
	auto terrainGTex = AssetsManager::GetTexture("pinkFlowers", "u_gTexture");
	auto terrainBTex = AssetsManager::GetTexture("mossPath256", "u_bTexture");
	auto terrainBlendMapTex = AssetsManager::GetTexture("blendMap", "u_blendMap");
	auto terrainMaterial = CreateRefPtr<TerrainLightMaterial>(std::vector{ terrainBackGroundTex, terrainRTex, terrainGTex, terrainBTex, terrainBlendMapTex });
	terrainMaterial->SetLightComponent(lights).SetFogComponent(fog);

	
	AnimatedModel* model = new AnimatedModel("../GEngine/include/GEngine/Assets/AnimatedModels/dancing_vampire.dae");

	auto vampireTexNormal = AssetsManager::GetTexture("Vampire_normal", "u_normalTexture");
	auto vampireTexDiffuse = AssetsManager::GetTexture("Vampire_diffuse", "u_diffuseTexture");
	auto vampiretextures = { vampireTexDiffuse , vampireTexNormal };
	auto vampireMaterial = CreateRefPtr<AnimatedMaterial>(vampiretextures, "animated");
	auto vampireGeo = ShapeManager::GetModels("dancing_vampire")[0];
	
	m_VampireGroup = new Group<Entity>(vampireGeo, vampireMaterial.get());

	Animation* animation = new Animation("../GEngine/include/GEngine/Assets/AnimatedModels/dancing_vampire.dae", model);
	m_AnimationSystem = CreateScopedPtr<AnimationSystem>(animation);

	vampireMaterial->SetAnimationSystem(m_AnimationSystem.get());
	
	vampireMaterial->SetFogComponent(fog).SetTextureComponent(normalTexture);
	auto vampire = new Entity(vampireGeo, vampireMaterial);

	vampire->SetPosition({ 60.f, 0.f, -60.f });
	vampire->Scale(7);
	vampire->SetTag("vampire");
	vampire->Add(lamp_light_5);

	lamp_light_5->Scale(1/7.f, false);
	vampire->CalculateTextureOffset(1, 0);
	m_VampireGroup->Push(vampire);



	m_TerrainGroup = new Group<Entity>(terrainGeo, terrainMaterial.get());
	m_Terrain1 = new Entity(terrainGeo, terrainMaterial);
	m_Terrain1->SetPosition({ 0, 0, -900 });
	m_Terrain1->SetTag("Terrain1");
	m_Terrain2 = new Entity(terrainGeo, terrainMaterial);
	m_Terrain2->SetPosition({ -800, 0, -800 });
	m_Terrain2->SetTag("Terrain2");

	m_TerrainGroup->Push(m_Terrain1);


	m_Terrains.insert(m_Terrains.end(), { m_Terrain1, m_Terrain2 });

	


	/*auto personGeo = ShapeManager::GetModel("person");
	auto personTex = AssetsManager::GetTexture("mirror_reflect");
	auto personMaterial = CreateRefPtr<LightTextureMaterial>(*personTex);

	personMaterial->SetFogComponent(fog).SetLightComponent(lights).SetTextureComponent(normalTexture);


	m_Person = new Character(personGeo, personMaterial);


	m_Person->SetTerrain(terrainGeo);
	
	m_Person->CalculateTextureOffset(1, 0);
	m_Person->SetTag("Person");
	m_Person->Scale(0.4f);*/



	CameraSetting setting;
	setting.m_PerspectiveSetting.m_FieldOfView = 45.f;
	setting.m_PerspectiveSetting.m_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
	setting.m_PerspectiveSetting.m_Near = 0.1f;
	setting.m_PerspectiveSetting.m_Far = 10000.f;


	auto lampGeo = ShapeManager::GetModel("lamp");
	auto lampTex = AssetsManager::GetTexture("lamp");
	auto lampMaterial = CreateRefPtr<LightTextureMaterial>(*lampTex);
	lampMaterial->SetFogComponent(fog).SetLightComponent(lights).SetTextureComponent(normalTexture);
	lampMaterial->GetMaterialProperty().HasFakeLighting = true;
	m_LampGroup = new Group<Entity>(lampGeo, lampMaterial.get());
	auto lamp1 = new Entity(lampGeo, lampMaterial);
	lamp1->SetTag("Lamp_1");
	
	lamp1->SetPosition({ 185, terrainGeo->GetTerrainHeight(185, -293), -293 });
	lamp1->Add(lamp_light_1);

	auto lamp2 = new Entity(lampGeo, lampMaterial);
	lamp2->SetPosition({ 370, terrainGeo->GetTerrainHeight(370, -300), -300 });
	lamp2->SetTag("Lamp_2");
	lamp2->Add(lamp_light_2);


	auto lamp3 = new Entity(lampGeo, lampMaterial);
	lamp3->SetPosition({ 293, terrainGeo->GetTerrainHeight(293, -305), -305 });
	lamp3->SetTag("Lamp_3");
	lamp3->Add(lamp_light_3);

	m_LampGroup->Push({ lamp1, lamp2, lamp3 });

	


	auto treeGeo = ShapeManager::GetModel("tree");
	auto grassGeo = ShapeManager::GetModel("plant");
	auto flowerGeo = ShapeManager::GetModel("plant");
	auto fernGeo = ShapeManager::GetModel("fern");
	auto lowPolyTreeGeo = ShapeManager::GetModel("lowPolyTree");
	auto pineGeo = ShapeManager::GetModel("pine");

	auto treeTex = AssetsManager::GetTexture("tree");
	auto grassTex = AssetsManager::GetTexture("grassTexture");
	auto fernTex = AssetsManager::GetTexture("Multifern");
	auto flowerTex = AssetsManager::GetTexture("flower");
	auto lowPolyTreeTex = AssetsManager::GetTexture("MultilowPolyTree");
	auto pineTex = AssetsManager::GetTexture("pine");

	auto treeMaterial = CreateRefPtr<LightTextureMaterial>(*treeTex);
	auto grassMaterial = CreateRefPtr<LightTextureMaterial>(*grassTex);
	auto fernMaterial = CreateRefPtr<LightTextureMaterial>(*fernTex);
	auto flowerMaterial = CreateRefPtr<LightTextureMaterial>(*flowerTex);
	auto lowPolyTreeMaterial = CreateRefPtr<LightTextureMaterial>(*lowPolyTreeTex);
	auto pineMaterial = CreateRefPtr<LightTextureMaterial>(*pineTex);
	pineMaterial->GetMaterialProperty().ShineDamper = 10.f;
	pineMaterial->GetMaterialProperty().Reflectivity = 0.1f;

	treeMaterial->SetFogComponent(fog).SetLightComponent(lights).SetTextureComponent(normalTexture);
	grassMaterial->SetFogComponent(fog).SetLightComponent(lights).SetTextureComponent(normalTexture);
	fernMaterial->SetFogComponent(fog).SetLightComponent(lights).SetTextureComponent(MultiTexture);
	flowerMaterial->SetFogComponent(fog).SetLightComponent(lights).SetTextureComponent(normalTexture);
	lowPolyTreeMaterial->SetFogComponent(fog).SetLightComponent(lights).SetTextureComponent(MultiTexture);
	pineMaterial->SetFogComponent(fog).SetLightComponent(lights).SetTextureComponent(normalTexture);
	barrelMaterial->SetFogComponent(fog).SetLightComponent(lights).SetTextureComponent(normalTexture);

	m_BarrelGroup = new Group<Entity>(barrelGeo, barrelMaterial.get());
	m_BarrelGroup->Push(barrel);

	m_TreeGroup = new Group<Entity>(treeGeo, treeMaterial.get());
	m_GrassGroup = new Group<Entity>(grassGeo, grassMaterial.get());
	m_FernGroup = new Group<Entity>(fernGeo, fernMaterial.get());
	m_FlowerGroup = new Group<Entity>(flowerGeo, flowerMaterial.get());
	m_LowPolyTreeGroup = new Group<Entity>(lowPolyTreeGeo, lowPolyTreeMaterial.get());
	m_PineGroup = new Group<Entity>(pineGeo, pineMaterial.get());

	grassMaterial->GetMaterialProperty().HasFakeLighting = true;
	grassMaterial->GetMaterialProperty().HasTransparency = true;

	flowerMaterial->GetMaterialProperty().HasFakeLighting = true;
	flowerMaterial->GetMaterialProperty().HasTransparency = true;

	fernMaterial->GetMaterialProperty().HasTransparency = true;

	pineMaterial->GetMaterialProperty().HasTransparency = true;


	static std::default_random_engine e;
	static std::uniform_real_distribution<> float_dis(0, 1);
	static std::uniform_int_distribution<> int_dis(0, 3);


	for (int i = 0; i < 400; i++)
	{

		if (i % 2 == 0)
		{
			Entity * fern = new Entity(fernGeo, fernMaterial);
			fern->CalculateTextureOffset(2, int_dis(e));
			fern->SetPosition((float)float_dis(e) * 800.f + 20.f, 0.f, (float)float_dis(e) * -600.f - 20.f);
			fern->SetPositionY(terrainGeo->GetTerrainHeight(fern->GetPosition().x, fern->GetPosition().z));

			fern->Scale(0.6f);
			fern->SetTag("fern_" + std::to_string(i));
			m_FernGroup->Push(fern);


		}

		if (i % 3 == 0)
		{
			Entity* pine = new Entity(pineGeo, pineMaterial);
			pine->CalculateTextureOffset(1, 0);
			pine->SetPosition((float)float_dis(e) * 800.f + 20.f, 0.f, (float)float_dis(e) * -600.f - 20.f);
			pine->SetPositionY(terrainGeo->GetTerrainHeight(pine->GetPosition().x, pine->GetPosition().z));
			//pine->Scale(3.f);
			pine->SetTag("pine_" + std::to_string(i));
			m_PineGroup->Push(pine);
		}

	}


	m_Scene->Push({ m_TerrainGroup, m_FernGroup, m_PineGroup, m_LampGroup, m_BarrelGroup, m_VampireGroup });

	
	auto DragonGeo = ShapeManager::GetModel("dragon");
	DragonGeo->ApplyTransform(Matrix::MakeRotationY(-Math::PiOver2), 0);
	DragonGeo->ApplyTransform(Matrix::MakeRotationY(-Math::PiOver2), 2, true);
	auto DragonTex = AssetsManager::GetTexture("white");

	auto DragonMaterial = CreateRefPtr<LightTextureMaterial>(*DragonTex);
	DragonMaterial->SetFogComponent(fog).SetLightComponent(lights).SetTextureComponent(normalTexture);
	DragonMaterial->GetMaterialProperty().ShineDamper = 10;
	DragonMaterial->GetMaterialProperty().Reflectivity = 1;
	m_Dragon = new Entity(DragonGeo, DragonMaterial);
	m_Dragon->CalculateTextureOffset(1, 0);

	m_Dragon->SetTag("Dragon");
	m_Dragon->SetPosition({ 30, 0, -60});

	
	m_Scene->Add(m_Dragon);


	m_CameraRig = new CameraRig(true);
	m_CameraRig->SetTag("Camera Rig");
	m_CameraRig->Attach(m_EditorCamera);


	m_CameraRig->SetPosition({ 5.f, 50.f, -100.f });
	

	m_Scene->Add(m_CameraRig);

	m_SkyBox = new SkyBoxEntity(skyBoxComponent);
	m_SkyBox->SetTag("SkyBox");

	m_Scene->Add(m_SkyBox);


	auto MouseScrollWheelEvent = new Events<void(MouseScrollWheelParam)>("MouseScrollWheel");

	MouseScrollWheelEvent->Subscribe([this](const MouseScrollWheelParam& scrollWheelParam)
		{
			if (m_ViewportForcused && m_ViewportHovered)
			{
				auto& MouseState = GetInputManager()->GetMouseState();
				auto& windows = GetWindowManager()->GetWindows();

				if (auto p = windows.find(scrollWheelParam.ID); p != windows.end())
				{
					
					MouseState.SetScrollWheel(Vector2(
						static_cast<float>(scrollWheelParam.X),
						static_cast<float>(scrollWheelParam.Y)));					
					GENGINE_CORE_INFO("{}, {}", scrollWheelParam.X, scrollWheelParam.Y);
					m_EditorCamera->OnScroll(scrollWheelParam.Y);

				}

			}

		});

	GetEventManager()->GetEventDispatcher().RegisterEvent(MouseScrollWheelEvent);

	m_Panel = CreateScopedPtr<SceneHierarchyPanel>(this, m_Scene.get());
	m_MusicEvent = m_AudioSystem->PlayEvent("event:/EnteringValley");
	m_BackgroundMusicEvent = m_AudioSystem->PlayEvent("event:/HyruleFieldMainTheme");
	
}

void SceneApp::Initialize(const WindowProperties& prop)
{
	using namespace Camera;
	using namespace Manager;

	//Initialize BaseApp 
	BaseApp::Initialize(prop);

	Initialize({ prop });
	
	
}

Vec2f SceneApp::GetMousePosInViewPort()
{
	auto [mx, my] = ImGui::GetMousePos();
	mx -= m_ViewportBounds[0].x;
	my -= m_ViewportBounds[0].y;

	auto viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
	int MouseX = (int)mx;
	int MouseY = (int)my;
	return{ MouseX, MouseY };
}

void SceneApp::ImGuiRender()
{
	using namespace Manager;

	/*ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
	if (ImGui::Begin("Earth Pos X"))
	{
		ImGui::DragFloat("Eath Pos X", &Pos.x, 0.1f);

	}
	ImGui::End();

	if (ImGui::Begin("Earth Pos Y"))
		ImGui::DragFloat("Eath Pos Y", &Pos.y, 0.1f);
	


	ImGui::End();

	if (ImGui::Begin("Textures"))
	{
		if (ImGui::Button("Default"))
		{
			auto DefaultTex = AssetsManager::GetTexture();
			auto DefaultTexMaterial = CreateRefPtr<TextureMaterial>(*DefaultTex);
			m_Sphere->SetMaterial(DefaultTexMaterial);
		}

		if (ImGui::Button("Water"))
		{
			auto waterTex = AssetsManager::GetTexture(base_dir + "water.jpg");
			auto waterTexMaterial = CreateRefPtr<TextureMaterial>(*waterTex);
			m_Sphere->SetMaterial(waterTexMaterial);
		}

		if (ImGui::Button("Earth"))
		{
			auto earthTex = AssetsManager::GetTexture(base_dir + "earth.jpg");
			auto earthTexMaterial = CreateRefPtr<TextureMaterial>(*earthTex);
			m_Sphere->SetMaterial(earthTexMaterial);
		}
	}
	ImGui::End();
	
	ImGui::Begin("GameView1");


	auto winsize = ImGui::GetWindowSize();
	if (ImGui::IsWindowHovered())
	{
		ImGui::CaptureMouseFromApp(false);
	}
	ImGui::Image(reinterpret_cast<void*>(m_RenderTarget->GetTargetTextureID()), winsize, { 0,1 }, { 1, 0 });
	ImGui::End();*/

	/*ImGui::Begin("GameView2");
	if (ImGui::IsWindowHovered())
	{
		ImGui::CaptureMouseFromApp(false);
	}
	ImGui::Image(reinterpret_cast<void*>(m_RenderTarget->GetTargetTextureID()), { 480, 320 }, { 0,1 }, { 1, 0 });
	ImGui::End();

	ImGui::Begin("GameView3");
	if (ImGui::IsWindowHovered())
	{
		ImGui::CaptureMouseFromApp(false);
	}
	ImGui::Image(reinterpret_cast<void*>(m_RenderTarget->GetTargetTextureID()), { 480, 320 }, { 0,1 }, { 1, 0 });
	ImGui::End();

	ImGui::Begin("GameView4");
	if (ImGui::IsWindowHovered())
	{
		ImGui::CaptureMouseFromApp(false);
	}
	ImGui::Image(reinterpret_cast<void*>(m_RenderTarget->GetTargetTextureID()), { 480, 320 }, { 0,1 }, { 1, 0 });
	ImGui::End();*/



	//ImGui::End();

	//ImGui::Begin("View2");
	/*if (ImGui::IsWindowHovered())
	{
		ImGui::CaptureMouseFromApp(false);
	}*/
	//ImGui::Image(reinterpret_cast<void*>(m_RenderTarget->GetTargetTextureID()), { 100, 100 }, { 0,1 }, { 1, 0 });
	//ImGui::End();

	//ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

	static bool show = true;
	//ImGui::ShowDemoWindow(&show);

	static bool opt_fullscreen = true;
	static bool opt_padding = false;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

	//// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
	//// because it would be confusing to have two docking targets within each others.
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
	else
	{
		dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
	}

	//// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
	//// and handle the pass-thru hole, so we ask Begin() to not render a background.
	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;

	//// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
	//// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
	//// all active windows docked into it will lose their parent and become undocked.
	//// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
	//// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
	if (!opt_padding)
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", &show, window_flags);
	if (!opt_padding)
		ImGui::PopStyleVar();

	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	//// Submit the DockSpace
	ImGuiIO& io = ImGui::GetIO();

	auto& style = ImGui::GetStyle();
	float winMinSize = style.WindowMinSize.x;
	style.WindowMinSize.x = 370.f;
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}

	style.WindowMinSize.x = winMinSize;

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			// Disabling fullscreen would allow the window to be moved to the front of other windows,
			// which we can't undo at the moment without finer window depth/z control.
			//ImGui::MenuItem("Fullscreen", nullptr, &opt_fullscreen);
			//ImGui::MenuItem("Padding", nullptr, &opt_padding);
			//ImGui::Separator();

			if (ImGui::MenuItem("Exit"))
			{
				ShutDown();
			}

			ImGui::EndMenu();
		}


		ImGui::EndMenuBar();
	}

	m_Panel->OnImGuiRender();

	ImGui::Begin("Stats");

	auto stats = Renderer::GetRenderStats();
	ImGui::Text("Renderer Stats:");

	ImGui::Text("ArrayDraw Calls: %d", stats.m_ArrayDrawCall);
	ImGui::Text("ElementsDraw Calls: %d", stats.m_ElementsDrawCall);

	ImGui::Text("Vertices: %d", stats.m_VerticeCount);

	ImGui::Text("Indices: %d", stats.m_IndicesCount);

	/*if (m_Sun)
	{
		ImGui::Separator();
		ImGui::Text("%s", m_Sun->GetTag().c_str());
		Vec3f pos = m_Sun->GetWorldPosition();
		ImGui::DragFloat3("Sun Pos", glm::value_ptr(pos), 0.1f);
		ImGui::Separator();

		if (m_Earth)
		{

			ImGui::Separator();
			ImGui::Text("%s", m_Earth->GetTag().c_str());
			pos = m_Earth->GetWorldPosition();
			ImGui::DragFloat3("Earth Pos", glm::value_ptr(pos), 0.1f);
			ImGui::Separator();

			if (m_Moon)
			{
				ImGui::Separator();
				ImGui::Text("%s", m_Moon->GetTag().c_str());
				pos = m_Moon->GetWorldPosition();
				ImGui::DragFloat3("Moon Pos", glm::value_ptr(pos), 0.1f);
				ImGui::Separator();
			}
		}
		
	}*/


	ImGui::End();

	ImGui::Begin("Graphic Setting");

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
	ImGui::Separator();
	if (ImGui::TreeNodeEx((void*)(uint64_t)1, flags, "Anti Aliasing"))
	{
		const char* numOfAnti[] = {"1x", "2x", "4x", "8x", "16x"};
		const char* currentAntiLevel = numOfAnti[BitSetIndex(m_Aliasing)];
		if (ImGui::BeginCombo("##combo", currentAntiLevel))
		{
			for (auto i = 0; i < 5; i++)
			{
				bool isSelected = (currentAntiLevel == numOfAnti[i]);

				if (ImGui::Selectable(numOfAnti[i], isSelected))
				{
					m_Aliasing = 1 << i;
					currentAntiLevel = numOfAnti[BitSetIndex(m_Aliasing)];

				}

				if (isSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();


		}

		int AntiLevel = (int)std::pow(2, BitSetIndex(m_Aliasing));
		
		m_RenderTarget->SetSamples(AntiLevel);

		ImGui::TreePop();
	}

	

	ImGui::End();

	ImGui::Separator();
	ImGui::Begin("Skybox");
	auto& skyboxComp = m_SkyBox->GetSkyBoxComponent();
	auto& lowerLimit = skyboxComp.LowerLimit.Data;
	auto& upperLimit = skyboxComp.UpperLimit.Data;
	auto& fogColor = skyboxComp.FogAmbientColor.Data;
	ImGui::ColorEdit3("FogColor", glm::value_ptr(fogColor));
	ImGui::DragFloat("LowerLimit", &lowerLimit, 1.f, 0.f, 100.f);
	ImGui::DragFloat("UpperLimit", &upperLimit, 1.f, 0.f, 100.f);
	ImGui::End();

	ImGui::Separator();
	ImGui::Begin("Terrain");
	auto terrainMaterial = dynamic_cast<TerrainLightMaterial*>(m_Terrain1->GetMaterial());
	auto& fogComp = terrainMaterial->GetFogComponent();
	auto& density = fogComp.Density.Data;
	auto& gradient = fogComp.Gradient.Data;
	auto& reflectity = terrainMaterial->GetMaterialProperty().Reflectivity;
	auto& shine_damper = terrainMaterial->GetMaterialProperty().ShineDamper;

	ImGui::DragFloat("density", &density, 0.001f, 0.f, 1.f);
	ImGui::DragFloat("gradient", &gradient, 0.01f, 0.f, 10.f);
	ImGui::DragFloat("reflectivity", &reflectity, 0.01f, 0.f, 1.f);
	ImGui::DragFloat("shinedamper", &shine_damper, 0.01f, 0.f, 1.f);
	


	ImGui::End();



	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });

	//ImGui::Begin("Settings");
	////const auto IronAlbedoTex = GraphicEngine::ResourceManager::GetTexture(BaseImageFilePath + "RustedIron/albedo.png", "albedoMap");
	//ImGui::Image(reinterpret_cast<void*>(m_RenderTarget->GetTargetTextureID()), { 800,600 }, { 0,1 }, { 1, 0 });
	//ImGui::End();

	//ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
	//if (ImGui::Begin("Earth Pos X"))
	//{
	//	ImGui::DragFloat("Eath Pos X", &Pos.x, 0.1f);

	//}
	//ImGui::End();

	//if (ImGui::Begin("Earth Pos Y"))
	//	ImGui::DragFloat("Eath Pos Y", &Pos.y, 0.1f);



	//ImGui::End();

	//if (ImGui::Begin("Textures"))
	//{
	//	if (ImGui::Button("Default"))
	//	{
	//		auto DefaultTex = AssetsManager::GetTexture();
	//		auto DefaultTexMaterial = CreateRefPtr<TextureMaterial>(*DefaultTex);
	//		m_Sphere->SetMaterial(DefaultTexMaterial);
	//	}

	//	if (ImGui::Button("Water"))
	//	{
	//		auto waterTex = AssetsManager::GetTexture(base_dir + "water.jpg");
	//		auto waterTexMaterial = CreateRefPtr<TextureMaterial>(*waterTex);
	//		m_Sphere->SetMaterial(waterTexMaterial);
	//	}

	//	if (ImGui::Button("Earth"))
	//	{
	//		auto earthTex = AssetsManager::GetTexture(base_dir + "earth.jpg");
	//		auto earthTexMaterial = CreateRefPtr<TextureMaterial>(*earthTex);
	//		m_Sphere->SetMaterial(earthTexMaterial);
	//	}
	//}
	//ImGui::End();
	ImGui::Begin("Viewport");

	auto viewportOffset = ImGui::GetCursorPos();

	//GENGINE_CORE_INFO("{0}, {1}", viewportOffset.x, viewportOffset.y);
	m_ViewportForcused = ImGui::IsWindowFocused();
	m_ViewportHovered = ImGui::IsWindowHovered();
	//GENGINE_INFO("Focused: {}", ImGui::IsWindowFocused());
	//GENGINE_INFO("Hovered: {}", ImGui::IsWindowHovered());
	
	auto viewportPanelSize = ImGui::GetContentRegionAvail();
	m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };



	//GENGINE_CORE_INFO("({}, {})", viewportPanelSize.x, viewportPanelSize.y);
	
	/*if (m_ViewportSize != *((Vec2f*)(&viewportPanelSize)) && viewportPanelSize.x > 0 && viewportPanelSize.y > 0)
	{
		m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
		m_SceneCamera->OnResize((int)m_ViewportSize.x, (int)m_ViewportSize.y);
		m_RenderTarget->OnResize( (uint32_t)viewportPanelSize.x, (uint32_t)viewportPanelSize.y );
	}*/

	//GENGINE_WARN()
	//auto winsize = ImGui::GetWindowSize();
	
	if (m_ViewportHovered)
	{
		ImGui::CaptureMouseFromApp(false);
		auto pos = GetMousePosInViewPort();

		//GENGINE_CORE_INFO("{}, {}", pos.x, pos.y);
	}

	if (m_ViewportForcused)
	{
		ImGui::CaptureKeyboardFromApp(true);
	}



	if(!m_RenderTarget->IsMultiSampled())
		ImGui::Image(reinterpret_cast<void*>(m_RenderTarget->GetColorAttachmentID()), { m_ViewportSize.x, m_ViewportSize.y }, { 0,1 }, { 1, 0 });
	else
		ImGui::Image(reinterpret_cast<void*>(m_RenderTarget->GetScreenAttachmentID()), { m_ViewportSize.x, m_ViewportSize.y }, { 0,1 }, { 1, 0 });
	
	auto windowSize = ImGui::GetWindowSize();
	auto minBound = ImGui::GetWindowPos();

	//auto viewportOffset = ImGui::GetMousePos();
	//auto[x, y] = ImGui::GetMousePos();
	//GENGINE_CORE_INFO("{0}, {1}", x, y);

	minBound.x += viewportOffset.x;
	minBound.y += viewportOffset.y;

	ImVec2 maxBound = { minBound.x + windowSize.x, minBound.y + windowSize.y };

	m_ViewportBounds[0] = { minBound.x, minBound.y };
	m_ViewportBounds[1] = { maxBound.x, maxBound.y };
	
	//Gizmos

	//auto[mx, my] = ImGui::GetMousePos();
	////GENGINE_CORE_INFO("{0}, {1}", mx, my);
	//mx -= m_ViewportBounds[0].x;
	//my -= m_ViewportBounds[0].y;

	//auto viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
	//int MouseX = (int)mx;
	//int MouseY = (int)my;
	//GENGINE_CORE_INFO("{0}, {1}", mx, my);

	float winWidth{};
	float winHeight{};



	Actor* selectActor = m_Panel->GetSelectedEntity();


	if(m_ViewportForcused||m_ViewportHovered)
	{ 
		if (selectActor && m_GizmoType != -1)
		{
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();
			winWidth = ImGui::GetWindowWidth();
			winHeight = ImGui::GetWindowHeight();
			ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, winWidth, winHeight);
			auto view = m_EditorCamera->GetView();
			auto projection = m_EditorCamera->GetProjection();

			auto& tc = selectActor->GetLocalTransformation();
			auto& transform = tc.Transform;
			ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform));
			if (ImGuizmo::IsUsing())
			{
				Vec3f translation, rotation, scale;
				if (Matrix::DecomposeTransform(transform, translation, rotation, scale))
				{
					tc.Translation = translation;
					Vec3f deltaRotation = Math::ToDegrees(rotation) - tc.Rotation;
					tc.Rotation += deltaRotation;

					tc.Scale = scale;
				}


			}

		}
	}
	
	ImGui::End();
	ImGui::PopStyleVar();

	ImGui::End();
}

uint8_t SceneApp::BitSetIndex(uint8_t num)
{
	uint8_t i = 1, index = 0;
	while (!(i & num))
	{
		i = i << 1;
		index++;
	}

	return index;
}


BaseApp* CreateApp()
{
	return new SceneApp();
}