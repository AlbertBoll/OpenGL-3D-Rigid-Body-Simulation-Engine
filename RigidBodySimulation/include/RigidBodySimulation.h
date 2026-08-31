#pragma once

#include "Core/BaseApp.h"
#include <Camera/EditorCamera.h>
#include <Scene/_Entity.h>
#include <filesystem>
#include "Audio/AudioSystem.h"
#include "Physics/PhysicsSystem.h"
#include <SpatialPartition/KDTree.h>

namespace GEngine
{
	class _Scene;
	using namespace Camera;
	using namespace Manager;
	using namespace Audio;
}



	
using namespace GEngine;

class Asset::Texture;

class RigidBodySimulationApp : public BaseApp
{
public:
	RigidBodySimulationApp() : BaseApp() {}
	~RigidBodySimulationApp()override;

	void Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)override;
	void Initialize(const WindowProperties& prop = WindowProperties{}) override;
	void Update(Timestep ts) override;
	void Render()override;

private:
	void ImGuiRender() override;
	void UI_Toolbar();
	void OnMouseClicked();
	void FilledKDTreePoints();
	void OnViewportResize(int viewport_x, int viewport_y);


private:
	_Entity m_Sphere;
	_Entity m_HoveredEntity;
	_Entity m_GridEntity;
	_Entity m_AxisEntity;
	_Entity m_SkyBoxEntity;
	_Entity m_PointLightEntity;
	
	_EditorCamera m_EditorCamera_;
	RefPtr<_Scene> m_ActiveScene;
	RefPtr<_Scene> m_EditorScene;
	std::filesystem::path m_EditorScenePath;
	bool m_PrimaryCamera = true;
	ScopedPtr<Audio::AudioSystem> m_AudioSystem;

	enum class SceneState
	{
		Edit = 0, Play = 1, Simulate = 2
	};
	SceneState m_SceneState = SceneState::Edit;
	bool m_ViewportFocused = false, m_ViewportHovered = false;
	//Vec2f m_ViewportSize = { 1280.f, 400 };
	Vec2f m_ViewportBounds[2];
	Vec3f m_LightDirection;// = { 20.f, 50.0f, 20.f };
	Vec3f m_LightPos;
	bool m_IsPause = false;
	bool m_IsShowDebugBoundingBox = false;
	bool m_IsShowKDTree = false;
	std::vector<float> m_ShadowCascadeLevels;
	float m_NearPlane = 0.1f;
	float m_FarPlane = 40.0f;
	ScopedPtr<DebugKDTreeVisualizer> m_DebugKDTreeVisualizer{};
	KDTree m_KDTree{};
	std::vector<Point3D<float>> m_ObjectsPoints;
	std::vector<Vec3f> m_KDTreePoints;
	//RefPtr<DebugAABBBoundingBoxComponent> m_DebugBoundingBoxComp;
	Asset::Texture* m_IconPlay{};
	Asset::Texture* m_IconPause{};
	Asset::Texture* m_IconStep{};
	Asset::Texture* m_IconSimulate{};
	Asset::Texture* m_IconStop{};
};



