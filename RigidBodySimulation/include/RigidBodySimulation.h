#pragma once

#include "Core/BaseApp.h"
#include "RbsLaunchConfig.h"
#include "RbsState.h"
#include "Renderer/FrameScheduler.h"
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <Camera/EditorCamera.h>
#include "Scene/_Entity.h"
#include <filesystem>
#include "Audio/AudioSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/Shape.h"
#include "SpatialPartition/KDTree.h"
#include "Renderer/FrameSubmission.h"
#include "Assets/Textures/AsyncTexture.h"
#include "Mesh/AsyncMesh.h"

class RigidBodySimulationApp : public GEngine::BaseApp
{
public:
    explicit RigidBodySimulationApp(const Rbs::LaunchConfig& launch);
    ~RigidBodySimulationApp() override;

    GEngine::ApplicationInitializationResult Initialize(
        const std::initializer_list<GEngine::WindowProperties>& WindowsPropertyList) override;
    GEngine::ApplicationInitializationResult
    Initialize(const GEngine::WindowProperties& prop = GEngine::WindowProperties{}) override;
    void Update(GEngine::Timestep ts) override;
    void Render() override;

private:
    void ImGuiRender() override;
    void UI_Toolbar();
    void OnMouseClicked();
    void FilledKDTreePoints();
    [[nodiscard]] std::expected<void, GEngine::SceneError> UpdateImportedMesh();
    void OnViewportResize(int viewport_x, int viewport_y);

private:
    // Borrowed immutable launch snapshot lives in main and outlives BaseApp.
    const Rbs::LaunchConfig& m_Launch;
    std::unique_ptr<Rbs::Subscriptions> m_Subscriptions;
    Rbs::MaterialGallery m_MaterialGallery;
    Rbs::ShaderReloadGallery m_ShaderReloadGallery;
    Rbs::AsyncTextureGallery m_AsyncTextureGallery;

    GEngine::ApplicationInitializationResult InitializeResources();
    GEngine::ApplicationInitializationResult InitializeScene();
    GEngine::ApplicationInitializationResult ConnectApplicationEventHandlers();
    GEngine::ApplicationInitializationResult
    InitializeValidationPlatform(const std::initializer_list<GEngine::WindowProperties>&);
    GEngine::ApplicationInitializationResult ValidateRuntimeScene(const Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult ValidateScaleBridge(const Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult ValidateMaterialGallery(GEngine::Asset::MeshHandle);
    GEngine::ApplicationInitializationResult
    ValidateShaderDescription(GEngine::Asset::MeshHandle, const GEngine::MaterialShaderDescription&,
                              GEngine::MaterialHandle,
                              std::span<const GEngine::Asset::ShaderSource>,
                              std::span<const GEngine::MaterialShaderParameter>,
                              std::span<const GEngine::Asset::ShaderVariant>);
    bool ValidateUpdate();
    bool ReplayMaterialValidation();
    void ObserveViewport(const std::expected<GEngine::FramebufferScale, GEngine::PlatformError>&);
    GEngine::ScheduleResult ValidateBeforeSubmission(const GEngine::FrameSubmissionDesc&,
                                                     const GEngine::FrameCamera&);
    GEngine::ScheduleResult ValidateSubmittedFrame(bool, const GEngine::FrameSubmissionDesc&,
                                                   const GEngine::FrameCamera&);

    GEngine::ApplicationInitializationResult InitializeSceneRoot();
    GEngine::ApplicationInitializationResult PublishSceneMeshes(Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult LoadSceneIcons();
    GEngine::ApplicationInitializationResult LoadSurfaceTextures(Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult CreateSceneMaterials(Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult CreateSceneLights(Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult CreatePhysicsScene(Rbs::SceneAssets&);
#ifdef GENGINE_RBS_SCENE_VALIDATION
    GEngine::ApplicationInitializationResult ValidatePhysicsScene(const Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult ValidateSceneMembership();
#endif
    GEngine::ApplicationInitializationResult
    CreateSphereDiamond(const Rbs::SceneAssets&, GEngine::Component::RigidBody3DComponent&,
                        GEngine::Component::SphereFixture3DComponent&);
    GEngine::ApplicationInitializationResult
    CreateSphereLattice(const Rbs::SceneAssets&, GEngine::Component::RigidBody3DComponent&,
                        GEngine::Component::SphereFixture3DComponent&);
    GEngine::ApplicationInitializationResult
    CreateBoxStack(const Rbs::SceneAssets&, GEngine::Component::RigidBody3DComponent&,
                   GEngine::Component::SphereFixture3DComponent&,
                   GEngine::Component::BoxFixture3DComponent&);
    GEngine::ApplicationInitializationResult
    CreateSphereBoxStack(const Rbs::SceneAssets&, GEngine::Component::RigidBody3DComponent&,
                         GEngine::Component::SphereFixture3DComponent&,
                         GEngine::Component::BoxFixture3DComponent&);
    GEngine::ApplicationInitializationResult CreateEnvironmentHelpers();
    GEngine::ApplicationInitializationResult CreateGroundAndWalls(const Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult CreateTemplateGallery(Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult CreateGeometryExamples(Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult CreateMaterialGallery(const Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult CreateShaderGallery(const Rbs::SceneAssets&);
    GEngine::ApplicationInitializationResult StartSceneRuntime();
    GEngine::ApplicationInitializationResult CreateAsyncExamples(const Rbs::SceneAssets&);
    static GEngine::ApplicationInitializationResult
    SceneFailure(const GEngine::SceneResourceError&);
    GEngine::ApplicationInitializationResult
    AssignRenderable(GEngine::_Entity, const GEngine::Component::MeshRendererComponent&);
    GEngine::ApplicationInitializationResult
    CreateRenderable(GEngine::_Entity, GEngine::Asset::MeshHandle,
                     GEngine::Asset::MaterialInstanceHandle, std::string_view physicsShape = {});
    std::expected<GEngine::Asset::Texture*, GEngine::Asset::TextureError>
    LoadSceneTexture(const std::string&, const std::string& uniform = "u_texture",
                     const std::string& extension = ".png",
                     const GEngine::Asset::TextureDesc& = {});
    static void FocusMaterialCamera(GEngine::Camera::_EditorCamera&, const GEngine::Math::Vec3f&,
                                    float, float, float);
    void SetMaterialComparison(int);
    void UpdateMaterialAnimation(GEngine::Timestep);
    GEngine::ScheduleResult ApplyAsyncTextureEdit(GEngine::SceneRenderResources&,
                                                  GEngine::Asset::AsyncTextureLoader&,
                                                  GEngine::MaterialHandle);
    void ApplyShaderReloadRequest(GEngine::SceneRenderResources&);
    GEngine::ScheduleResult PrepareFrameResources(GEngine::RenderContext&);
    GEngine::ScheduleResult UpdateFrameResources();
    void DrawShaderPanel();
    void DrawMaterialPanel();
    void DrawAsyncMenu();
    void DrawMenus();
    void DrawViewport();
#ifdef GENGINE_RBS_MODULARITY_VALIDATION
    void ValidatePacketAUi();
#endif

    // Scenes release presentation-cache leases before the resource publisher.
    std::unique_ptr<GEngine::SceneRenderResources> m_SceneResources;
    std::optional<GEngine::FrameSubmission> m_FrameSubmission;
    GEngine::EntityPickTable m_PickTable;
    GEngine::Asset::AsyncTextureLoader* m_TextureLoads{}; // Borrowed root-owned service.
    GEngine::Asset::UploadTicket m_WoodRequest{};
    GEngine::Asset::MaterialInstanceHandle m_AsyncBoxMaterial{};
    bool m_WoodSettled{};
    std::filesystem::path m_MeshRoot;
    std::unique_ptr<GEngine::Asset::AsyncMeshLoader> m_MeshLoads;
    GEngine::Asset::UploadTicket m_BarrelRequest{};
    bool m_LoadBarrel{}, m_BarrelSettled{};
    GEngine::_Entity m_FrameCameraEntity;
    GEngine::_Entity m_Sphere;
    GEngine::_Entity m_HoveredEntity;
    GEngine::_Entity m_GridEntity;
    GEngine::_Entity m_AxisEntity;
    GEngine::_Entity m_SkyBoxEntity;
    GEngine::_Entity m_PointLightEntity;

    GEngine::Camera::_EditorCamera m_EditorCamera_;
    // Successful runtime shapes outlive their scene/body borrowers.
    std::vector<std::unique_ptr<GEngine::PhysicalShape>> m_PhysicsShapes;
    GEngine::RefPtr<GEngine::_Scene> m_ActiveScene;
    GEngine::RefPtr<GEngine::_Scene> m_EditorScene;
    std::filesystem::path m_EditorScenePath;
    bool m_PrimaryCamera = true;
    GEngine::ScopedPtr<GEngine::Audio::AudioSystem> m_AudioSystem;

    enum class SceneState
    {
        Edit = 0,
        Play = 1,
        Simulate = 2
    };
    SceneState m_SceneState = SceneState::Edit;
    bool m_ViewportFocused = false, m_ViewportHovered = false;
    //Vec2f m_ViewportSize = { 1280.f, 400 };
    GEngine::Math::Vec2f m_ViewportBounds[2];
    GEngine::Math::Vec3f m_LightDirection; // = { 20.f, 50.0f, 20.f };
    GEngine::Math::Vec3f m_LightPos;
    bool m_IsPause = false;
    bool m_IsShowDebugBoundingBox = false;
    bool m_IsShowKDTree = false;
    std::vector<float> m_ShadowCascadeLevels;
    float m_NearPlane = 0.1f;
    float m_FarPlane = 40.0f;
    GEngine::ScopedPtr<GEngine::DebugKDTreeVisualizer> m_DebugKDTreeVisualizer{};
    GEngine::KDTree m_KDTree{};
    std::vector<GEngine::Point3D<float>> m_ObjectsPoints;
    std::vector<GEngine::Math::Vec3f> m_KDTreePoints;
    //RefPtr<DebugAABBBoundingBoxComponent> m_DebugBoundingBoxComp;
    GEngine::Asset::Texture* m_IconPlay{};
    GEngine::Asset::Texture* m_IconPause{};
    GEngine::Asset::Texture* m_IconStep{};
    GEngine::Asset::Texture* m_IconSimulate{};
    GEngine::Asset::Texture* m_IconStop{};
};
