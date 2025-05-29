#pragma once
#include "Core\BaseApp.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Core/Entity.h"
#include <GEngine/Core/Group.h>
#include "Audio/SoundEvent.h"
#include "Audio/AudioSystem.h"
#include "Animation/AnimationSystem.h"
//#include <GEngine/Camera/PlayerCamera.h>





namespace GEngine
{
	class Grid;
	class CameraRig;
	class Entity;
	class Character;
	class SkyBoxEntity;
	class AnimationSystem;
	class AudioSystem;
	//template<typename T, typename = std::enable_if_t<std::is_base_of_v<Actor, T>>>
	//class Group;

}

namespace GEngine::SceneObjects
{
	class BoxEntity;
	
}


namespace GEngine::Camera
{
	class PlayerCamera;

}

using namespace GEngine;

class SceneApp: public BaseApp
{
	
public:
	SceneApp(): BaseApp(){}
	~SceneApp() override;

	void Update(Timestep ts) override;
	
	void Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)override;
	void Initialize(const WindowProperties& prop = WindowProperties{}) override;

	Vec2f GetMousePosInViewPort();

private:
	void ImGuiRender() override;



	uint8_t BitSetIndex(uint8_t num);

private:
	ScopedPtr<SceneHierarchyPanel> m_Panel{};
	CameraRig* m_CameraRig{};
	
	Grid* m_Grid{};

	Entity* m_Sun{};
	Entity* m_Earth{};
	Entity* m_Moon{};
	Entity* m_MilkyWay{};
	Entity* m_Dragon{};
	Entity* m_Terrain1{};
	Entity* m_Terrain2{};
	SkyBoxEntity* m_SkyBox{};
	std::vector<Entity*> m_Terrains;
	std::vector<Entity*> m_Plants;
	Group<Entity>* m_PineGroup{};
	Group<Entity>* m_TreeGroup;
	Group<Entity>* m_GrassGroup;
	Group<Entity>* m_FernGroup{};
	Group<Entity>* m_TerrainGroup{};
	Group<Entity>* m_FlowerGroup{};
	Group<Entity>* m_LowPolyTreeGroup{};
	Group<Entity>* m_LampGroup{};
	Group<Entity>* m_BarrelGroup{};
	Group<Entity>* m_VampireGroup{};
	Character* m_Person{};
	SceneObjects::BoxEntity* m_Plane{};
	uint8_t m_Aliasing = 8;
	ScopedPtr<Audio::AudioSystem> m_AudioSystem;
	ScopedPtr<AnimationSystem> m_AnimationSystem;
	Audio::SoundEvent m_MusicEvent;
	Audio::SoundEvent m_BackgroundMusicEvent;
	//std::vector<Terrain*> m_TerrainGeos;
	
	
	//bool m_ViewportForcused = false;
	//bool m_ViewportHovered = false;
	
	//Entity* m_Screen{};
	/*Entity* m_Ico{};
	
	Entity* m_Cylinder{};
	Entity* m_Grass{};
	Entity* m_Torus{};
	Entity* m_Box{};
	Entity* m_EnvironmentSphere{};*/
	
	
};

