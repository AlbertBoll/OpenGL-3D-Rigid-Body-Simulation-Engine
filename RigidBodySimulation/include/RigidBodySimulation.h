#pragma once

#include "Core/BaseApp.h"
#include "RbsLaunchConfig.h"
#include "RbsState.h"
#include "RbsHierarchy.h"
#include "RbsBulkProperties.h"
#include "RbsImpulseEditor.h"
#include "RbsTransformSession.h"
#include "Renderer/FrameScheduler.h"
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <unordered_set>
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

namespace Rbs
{
    struct SelectionMarqueeChecks;
    struct JoltParameterChecks;
    struct JoltParameterExperiment;
    struct ContinuousTransformChecks;
    struct ScalableTransformChecks;
    struct ImpulseEditorChecks;
    struct DistanceChainImpulseChecks;
    struct SandboxChecks;
    struct BackendSwitchChecks;
    struct OwnerReviewDriver;
    struct EditorViewsChecks;
    struct TransformShortcutChecks;
    struct SnapChecks;
    enum class SimulationState { Ready, Running, Paused, Switching, Failed };
    // Presentation command storage only; body/world ownership stays in PhysicsWorld.
    class DistanceChainImpulseControl
    {
    public:
        enum class Direction { PositiveX, NegativeX, PositiveZ, NegativeZ };
        enum class Status { Ready, Disabled, Queued, Applied, Zero, InvalidMagnitude,
                            InvalidDirection, InvalidTarget, Rejected };
        static constexpr float MinMagnitude = 0.f, MaxMagnitude = 5.f, DefaultMagnitude = 2.f;
        void Bind(GEngine::PhysicsBodyHandle target, bool enabled);
        bool Request(const GEngine::PhysicsWorld&, float magnitude, Direction);
        void Consume(GEngine::PhysicsWorld&, bool paused);
        bool Enabled() const { return m_Enabled; }
        bool Pending() const { return m_Pending; }
        auto Target() const { return m_Target; }
        auto LastApplied() const { return m_LastApplied; }
        unsigned AppliedCount() const { return m_AppliedCount; }
        Status GetStatus() const { return m_Status; }
    private:
        GEngine::PhysicsBodyHandle m_Target{};
        GEngine::Math::Vec3f m_QueuedImpulse{0.f}, m_LastApplied{0.f};
        bool m_Enabled{}, m_Pending{};
        unsigned m_AppliedCount{};
        Status m_Status = Status::Disabled;
    };
    void SetColliderUserPause(GEngine::_Scene&, bool);
    bool CancelColliderConfirmation(GEngine::_Scene&, bool, bool clearPreview = true);
    void ResetColliderEditor(GEngine::_Scene&, bool);
    // Outline inspection only. Editing and GPU mesh selection remain singular.
    struct ColliderOutlineSelection
    {
        std::array<GEngine::PhysicsBodyHandle, Rbs::Editor::EntitySelection::Capacity> bodies{};
        std::array<GEngine::EntityRenderId, Rbs::Editor::EntitySelection::Capacity> entities{};
        std::size_t count{};
        std::uint64_t revision{};
        bool capacityReached{};
        void Clear() { capacityReached = false; if (count) { count = 0; ++revision; } }
        bool Contains(GEngine::PhysicsBodyHandle body) const
        {
            for (std::size_t i = 0; i < count; ++i) if (bodies[i] == body) return true;
            return false;
        }
        void Remove(std::size_t index)
        {
            capacityReached = false;
            for (std::size_t i = index + 1; i < count; ++i)
            {
                bodies[i - 1] = bodies[i];
                entities[i - 1] = entities[i];
            }
            --count;
            ++revision;
        }
        void Select(GEngine::PhysicsBodyHandle body, bool toggle, GEngine::EntityRenderId entity = {})
        {
            if (!toggle) Clear();
            if (!body) return;
            for (std::size_t i = 0; i < count; ++i)
                if (bodies[i] == body) { Remove(i); return; }
            if (count < bodies.size())
            {
                bodies[count] = body;
                entities[count++] = entity;
                ++revision;
            }
            else capacityReached = true;
        }
        void Prune(const GEngine::PhysicsSystem& physics)
        {
            for (std::size_t i = 0; i < count;)
                if (!physics.ReadBodyPose(bodies[i])) Remove(i);
                else ++i;
        }

    };
}

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
    friend struct Rbs::DistanceChainImpulseChecks;
    friend struct Rbs::SandboxChecks;
    friend struct Rbs::BackendSwitchChecks;
    friend struct Rbs::OwnerReviewDriver;
    friend struct Rbs::EditorViewsChecks;
    friend struct Rbs::TransformShortcutChecks;
    friend struct Rbs::SnapChecks;
    friend struct Rbs::ContinuousTransformChecks;
    friend struct Rbs::ScalableTransformChecks;
    friend struct Rbs::ImpulseEditorChecks;
    friend struct Rbs::SelectionMarqueeChecks;
    friend struct Rbs::JoltParameterChecks;
    friend struct Rbs::JoltParameterExperiment;
    bool SandboxEnabled() const { return m_Launch.InteractiveSandboxEnabled(); }
    bool RequestScene(Rbs::SceneSelection);
    void ProcessSceneRequest();
    void ProcessBodyPropertyCommand();
    void ProcessWorldSleepCommand();
    void DrawWorldSleepSettings();
    void ProcessJoltSettingsCommand();
    void DrawJoltSettings();
    struct JoltSettingsEditor {
        // Initial scene generation is zero too; an uninitialized baseline must
        // not look like a captured baseline before the first Update boundary.
        std::uint64_t generation{~std::uint64_t{0}};
        GEngine::PhysicsJoltSettings draft{}, baseline{};
        bool dirty{},queued{},diagnostics{};
        std::optional<bool> diagnosticsRequest;
        std::string feedback;
        std::vector<std::pair<GEngine::PhysicsBodyHandle,GEngine::PhysicsBodyProperties>> bodyBaseline;
    } m_JoltSettingsEditor;
    struct WorldSleepEditor
    {
        std::uint64_t generation{};
        GEngine::PhysicsWorldSleepSettings draft;
        bool dirty{}, queued{};
        std::string diagnostic;
    } m_WorldSleepEditor;
    struct BodyPropertyEditor
    {
        GEngine::PhysicsBodyHandle target;
        GEngine::_Entity entity;
        std::uint64_t generation{};
        GEngine::PhysicsBodyProperties draft;
        bool dirty{}, queued{};
    } m_BodyPropertyEditor;
    struct BulkBodyPropertyEditor {
        std::uint64_t generation{},selectionRevision{};
        std::vector<GEngine::PhysicsBodyPropertyEdit> bodies;
        std::vector<GEngine::EntityRenderId> entities;
        GEngine::PhysicsBodyProperties draft;
        unsigned fields{},mixed{},supported{};
        std::size_t nonBodies{},unsupported{},readbackCount{};
        bool queued{},resetBaseline{},readError{};
        std::string diagnostic;
    } m_BulkBodyEditor;
    std::uint64_t m_SelectionRevision{};
    void RefreshBulkBodyProperties();
    void DrawBulkBodyProperties();
    void ProcessBulkBodyProperties();
    Rbs::Editor::ImpulseEditor m_Impulse;
    bool ResolveImpulseTargets(bool saved,std::vector<Rbs::Editor::ImpulseTarget>&);
    bool QueueImpulse(bool saved=false);
    bool SaveImpulseExperiment();
    void ProcessImpulseCommand();
    void DrawImpulseProperties();
    void DrawImpulseOverlay();
    bool RequestBackend(GEngine::PhysicsBackend);
    GEngine::ApplicationInitializationResult BindDistanceCommands(GEngine::PhysicsBodyHandle tip, bool contact);
    void SetSimulationState(Rbs::SimulationState);
    static void PublishPanelInputRegion();
    void PrepareColliderUi();
    void DrawLegacyDiagnosticPanels();
    void PublishEditorInputRouting();
    void PrepareEditorLayout();
    void DrawEditorToolbar();
    void DrawViewportOverlay();
    unsigned EditorPresetKey() const;
    std::expected<GEngine::_Entity,GEngine::SceneError> CreatePresetEntity(unsigned,std::string_view);
    void RegisterPresetEntity(GEngine::_Entity,unsigned);
    void FreezePresetSignatures();
    void SynchronizeEntityPresentation();
    bool EditorEntityVisible(GEngine::EntityRenderId) const;
    void QueueEntityVisibility(GEngine::EntityRenderId,bool);
    void BeginEntityRename(GEngine::EntityRenderId);
    bool DrawEntityRename(GEngine::EntityRenderId,float);
    bool ApplyEntityPresentation(const Rbs::Editor::TransformBatch&,bool undo);
    bool ValidateTransformPlacement(Rbs::Editor::TransformBatch&,const GEngine::PhysicsPlacementSettings&,GEngine::Mat4* pivot=nullptr);
    void ProcessTransformPreview();
    void OpenCameraPanel(GEngine::EntityRenderId);
    void DrawCameraPanels();
    void RenderCameraPanels();
    void DrawEntityPresentationProperties();
    void EditorHistoryShortcuts();
    void DrawViewportGizmo();
    void DrawViewportMarquee();
    void CancelViewportMarquee();
    void ArmViewportMarquee(unsigned x,unsigned y,GEngine::EntityRenderId clicked = {});
    void CommitViewportClick();
    enum class TransformTool { Select, Translate, Rotate, Scale };
    const char* TransformToolDisabled(TransformTool) const;
    bool ActivateTransformTool(TransformTool);
    enum class EditorMode { Edit, Simulation };
    EditorMode m_EditorMode=EditorMode::Edit;
    bool m_LiveEdit=false;
    bool m_GizmoCancelled=false;
    void SetEditorMode(EditorMode);
    bool TransformEditingAllowed() const;
    void FinishTransformGesture();
    bool HandleTransformToolShortcut(const GEngine::Manager::InputEvent&,
        const GEngine::Manager::InputRouting&, std::uint32_t mouseButtons, bool pointerCaptured);
    void QueueTransformEdit(TransformTool, const GEngine::Math::Vec3f&);
    void ProcessTransformEdit();
    bool BeginGroupTransform();
    bool StageGroupTransform(const GEngine::Mat4&,float uniformFactor=1.f);
    void CancelGroupTransform();
    void ProcessGroupTransform();
    void DrawGroupTransformActions();
    void DrawContextProperties();
    void ProcessContextProperties();
    void DrawEnvironmentProperties();
    void DrawComponentOverlays();
    void DrawSceneSelectionControls();
    void DrawHierarchyRegion();
    bool ExecuteSelectionCommand(Rbs::Editor::SelectionCommand,Rbs::Editor::SelectionType type=Rbs::Editor::SelectionType::Sphere,GEngine::EntityRenderId root={});
    bool IsSphereLatticeEntity(GEngine::EntityRenderId) const;
    void DrawInspectorRegion();
    void DrawWorldProperties();
    void DrawEditorProperties();
    void DrawContentBrowser();
    void DrawObjectProperties();
    void DrawPhysicsSettings();
    void DrawMaterialSection();
    void DrawShaderSection();
    void DrawDiagnosticsRegion();
    void DrawPhysicsDebugSection();
    void DrawPresentationSettings();
    void DrawConsoleSection();
    void DrawPerformanceSection();
    bool ResetSceneInteraction();
    struct SceneStateStorage;
    void SwapSceneState(SceneStateStorage&) noexcept;
    void DrawDistanceChainImpulseControls();
    bool RequestDistanceChainImpulse();
    void ImGuiRender() override;
    void UI_Toolbar();
    void OnMouseClicked();
    void ClearColliderSelection();
    void SelectColliderEntity(GEngine::_Entity, bool toggle);
    void SynchronizeEditorSelection();
#ifdef GENGINE_MESH_HIGHLIGHT_AUDIT
    bool ValidateOrdinaryHighlightUpdate();
    void ObserveOrdinaryHighlightSubmission(const GEngine::ScheduledFrameStats&);
#endif
#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
    bool ValidateColliderSelectionUpdate();
    void ObserveColliderSelectionFrame(const GEngine::RenderFrame&);
    bool ValidateMeshHighlightUpdate();
    void ObserveMeshHighlightSubmission(const GEngine::ScheduledFrameStats&);
#endif
    void FilledKDTreePoints();
    [[nodiscard]] std::expected<void, GEngine::SceneError> UpdateImportedMesh();
    void OnViewportResize(int viewport_x, int viewport_y);

private:
    // Borrowed immutable launch snapshot lives in main and outlives BaseApp.
    const Rbs::LaunchConfig& m_Launch;
    Rbs::SceneSelection m_ActiveSelection, m_PendingSelection;
    std::optional<Rbs::SceneSelection> m_SceneRequest;
    GEngine::PhysicsBackend m_Backend{GEngine::PhysicsBackend::Native};
    GEngine::PhysicsBackend m_PendingBackend{GEngine::PhysicsBackend::Native};
    std::optional<GEngine::PhysicsBackend> m_BackendRequest;
    float m_BackendSmokeMaxJointError{};
    bool m_EditorLayoutPrepared{};
    bool m_ResetEditorLayout{};
    int m_InspectorTabRequest{0}; // World=0, Selection=1; -1 retains the active tab.
    int m_DiagnosticsTabRequest{-1}; // Console=0, Profiler=1, Physics Debug=2.
    struct EntityPresentation {GEngine::EntityRenderId entity;std::uint64_t key{};bool hidden{};std::string signature;};
    bool m_BuildingPreset{};
    std::array<unsigned,64> m_PresetOrdinals{};
    std::vector<EntityPresentation> m_EntityPresentation;
    std::unordered_set<GEngine::EntityRenderId> m_PresentationKnown;
    std::vector<GEngine::EntityRenderId> m_EditorHidden;
    std::uint64_t m_PresentationRegistry{};
    GEngine::EntityRenderId m_RenameTarget;
    std::string m_RenameText,m_RenameError;
    bool m_RenameFocus{};
    struct CameraPanel {
        GEngine::EntityRenderId entity;std::uint64_t key{};
        bool open=true,visible{},ready{};unsigned width{},height{};
        std::optional<GEngine::RenderTarget> color;
        std::optional<GEngine::CascadeShadowFrameBuffer> cascade;
        GEngine::EntityPickTable picks;GEngine::FrameSubmissionStats stats;
        std::string diagnostic;std::uint64_t renderedFrames{};
    };
    std::vector<CameraPanel> m_CameraPanels;
    std::array<char,128> m_HierarchyFilter{};
    Rbs::Editor::HierarchyModel m_Hierarchy;
    std::vector<GEngine::EntityRenderId> m_VisibleHierarchyRows;
    Rbs::Editor::EntitySelection m_EditorSelection;
    GEngine::EntityRenderId m_HierarchyRevealed{};
    bool m_HierarchySelectionHandoff{};
    int m_HierarchyFocusedGroup{int(Rbs::Editor::EntityGroup::PhysicsBodies)};
    bool m_HierarchySphereCohort{};
    struct MarqueeGesture {
        bool armed{},dragging{},queued{},waiting{},add{},toggle{},through{};
        std::uint64_t generation{},serial{};
        GEngine::EntityRenderId clicked;
        GEngine::Math::Vec2f start{},end{},viewportLo{},viewportHi{};
        GEngine::RectangleSelectionRequest request;
        std::vector<GEngine::EntityRenderId> icons;
    } m_Marquee;
    std::uint64_t m_MarqueeSerial{};
    bool m_ViewSelectionAdd{};
    struct SceneIconHit {GEngine::EntityRenderId id;GEngine::Math::Vec2f lo{},hi{};};
    std::array<SceneIconHit,64> m_VisibleSceneIcons{};
    std::size_t m_VisibleSceneIconCount{};
    std::array<char,128> m_PresetFilter{};
    std::array<GEngine::Asset::Texture*,8> m_PresetThumbnails{};
    bool m_PresetThumbnailsLoaded{};
    std::array<GEngine::Asset::Texture*,4> m_ViewportIconAtlases{};
    std::array<GEngine::Asset::Texture*,7> m_ComponentIconAtlases{};
    TransformTool m_TransformTool{TransformTool::Select};
    bool m_ShowGizmo{true}, m_GizmoHovered{}, m_GizmoUsing{};
    bool m_ViewportOverlayInteracting{}, m_ViewportPopupOpen{};
    struct TransformEdit
    {
        TransformTool tool{};
        GEngine::_Entity entity;
        GEngine::PhysicsBodyHandle body;
        GEngine::Math::Vec3f value{};
        std::uint64_t generation{};
    };
    std::optional<TransformEdit> m_TransformEdit;
    Rbs::Editor::TransformSession m_GroupTransform;
    struct VelocityEdit
    {
        GEngine::_Entity entity;
        GEngine::PhysicsBodyHandle body;
        GEngine::Math::Vec3f linear{},angular{};
        std::uint64_t generation{};
    };
    std::optional<VelocityEdit> m_VelocityEdit;
    std::optional<bool> m_GridVisibilityEdit;
    std::string m_PropertyDiagnostic;
    struct ContextEditor
    {
        GEngine::EntityRenderId target;
        std::uint64_t generation{};
        std::optional<GEngine::Component::RenderCameraComponent> camera, cameraBefore;
        std::optional<GEngine::Component::RenderLightComponent> light, lightBefore;
        std::optional<GEngine::MaterialAuthoringDesc> material, materialBefore;
        GEngine::MaterialHandle materialTarget;
        bool cameraDirty{},lightDirty{},materialDirty{};
        int queued{};
        std::string diagnostic;
    } m_ContextEditor;
    std::optional<bool> m_SkyVisibilityEdit;
    std::string m_ConsoleText;
    double m_ConsoleRefreshTime{};
    Rbs::SimulationState m_SimulationState = Rbs::SimulationState::Ready;
    std::string m_SceneDiagnostic;
    std::uint64_t m_SceneGeneration{};
    bool m_SkipSimulationElapsed = true;
    bool m_InjectSceneConstructionFailure{}; // Private bounded validation injection; ordinary UI never sets it.
    std::unique_ptr<Rbs::Subscriptions> m_Subscriptions;
    Rbs::MaterialGallery m_MaterialGallery;
    Rbs::ShaderReloadGallery m_ShaderReloadGallery;
    Rbs::AsyncTextureGallery m_AsyncTextureGallery;

    GEngine::ApplicationInitializationResult InitializeResources();
    // Cold authoring data used by the existing Scene transaction, not a lifecycle owner.
    struct BackendAuthoring
    {
        struct Body
        {
            std::string m_Name;
            entt::entity m_FixtureEntity{entt::null}; // Same-preset construction key, never a Physics handle.
            GEngine::PhysicsBodyHandle m_Old;
            GEngine::SceneBodyPose m_Pose;
            GEngine::Math::Vec3f m_Scale;
            GEngine::Component::RigidBody3DComponent m_Rigid;
            std::variant<GEngine::Component::SphereFixture3DComponent,
                         GEngine::Component::BoxFixture3DComponent,
                         GEngine::Component::CapsuleFixture3DComponent,
                         GEngine::Component::ConvexFixture3DComponent> m_Fixture;
            GEngine::PhysicsMaterial m_Material;
            GEngine::RigidBody3D::SleepSettings m_Sleep;
            GEngine::PhysicsBodyProperties m_Properties;
            GEngine::CollisionShapeDesc m_Collider;
        };
        struct Joint { GEngine::DistanceJointDesc m_Desc; entt::entity m_A{entt::null}, m_B{entt::null}; };
        std::vector<Body> m_Bodies;
        std::vector<Joint> m_Joints;
        GEngine::Math::Vec3f m_Gravity{};
        GEngine::Seconds m_Step{1.0/60.0};
        bool m_Sleeping{true};
        std::optional<GEngine::PhysicsWorldSleepSettings> m_WorldSleep;
        std::optional<GEngine::PhysicsJoltSettings> m_JoltWorld;
        static std::expected<BackendAuthoring, GEngine::SceneError> Capture(GEngine::_Scene&);
        GEngine::ApplicationInitializationResult ApplyBeforeStart(GEngine::_Scene&) const;
        GEngine::ApplicationInitializationResult ApplyAfterStart(GEngine::_Scene&) const;
    };
    GEngine::ApplicationInitializationResult InitializeScene(const BackendAuthoring* = nullptr);
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
#ifdef GENGINE_CAMERA_VALIDATION
    bool ValidateCameraUpdate();
    GEngine::ScheduleResult ValidateCameraFrame(const GEngine::FrameCamera&);
#endif
#ifdef GENGINE_LIFECYCLE_VALIDATION
    bool ValidateLifecycleUpdate();
    GEngine::ScheduleResult ValidateLifecycleFrame(const GEngine::FrameCamera&);
#endif
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
    GEngine::ApplicationInitializationResult StartSceneRuntime(const BackendAuthoring* = nullptr);
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
    void DrawCameraMenu();
    void DrawViewport();
#ifdef GENGINE_RBS_MODULARITY_VALIDATION
    void ValidatePacketAUi();
#endif

    // Scenes release presentation-cache leases before the resource publisher.
    std::unique_ptr<GEngine::SceneRenderResources> m_SceneResources;
    std::optional<GEngine::FrameSubmission> m_FrameSubmission;
    GEngine::EntityPickTable m_PickTable;
    // The current modern camera is authoritative. Picking retains coordinates from
    // this same snapshot until the scheduler's pre-UI readback callback completes.
    std::optional<GEngine::CameraSnapshot> m_CameraSnapshot;
    std::optional<glm::uvec2> m_FramePickPixel;
    GEngine::Asset::AsyncTextureLoader* m_TextureLoads{}; // Borrowed root-owned service.
    GEngine::Asset::UploadTicket m_WoodRequest{};
    GEngine::Asset::MaterialInstanceHandle m_AsyncBoxMaterial{};
    bool m_WoodSettled{};
    std::filesystem::path m_MeshRoot;
    std::unique_ptr<GEngine::Asset::AsyncMeshLoader> m_MeshLoads;
    GEngine::Asset::UploadTicket m_BarrelRequest{};
    bool m_LoadBarrel{}, m_BarrelSettled{};
    enum class CameraView { Editor, ActiveScene, Preview };
    CameraView m_CameraView = CameraView::Editor;
    CameraView m_PreviewReturnView = CameraView::Editor;
    GEngine::EntityRenderId m_EditorViewIdentity{}, m_PreviewCamera{};
    std::array<GEngine::EntityRenderId, 2> m_SceneCameras{}; // UI references, not selection authority.
    std::string m_CameraDiagnostic;
    std::string m_CameraUnavailableDiagnostic;
    bool m_CameraAvailable = false;
    bool CancelCameraGesture();
    bool SetCameraView(CameraView, GEngine::EntityRenderId preview = {});
    bool EndCameraPreview();
    bool BindCameraScene(GEngine::RefPtr<GEngine::_Scene>);
    bool SelectSceneCamera(GEngine::EntityRenderId);
    bool RetireSceneCamera(std::size_t slot, bool replace);
    GEngine::ApplicationInitializationResult CreateSceneCamera(std::size_t slot);
    GEngine::ScheduleResult PrepareCameraFrame(GEngine::RenderContext&,
                                              GEngine::FrameSceneInput&, GEngine::FrameCamera&);
#ifdef GENGINE_CAMERA_VALIDATION
    GEngine::Component::RenderCameraComponent m_CameraValidationIntent;
#endif
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
    bool m_ViewportAltGestureConsumed = false;
    bool m_ViewportActive = false, m_ViewportRequestFocus = false;
    bool m_ViewportHighlightPending = false, m_ViewPickRequested = false;
    std::uint32_t m_ViewportActivationButtons = 0;
    unsigned m_ViewportNavWindow = 0;
    double m_ViewportHighlightStart = -1.0;
    void SetViewportActive(bool active, bool cancelInput = true);
#ifdef GENGINE_LIFECYCLE_VALIDATION
    std::uint64_t m_GesturePickReadbacks = 0, m_ViewportActivations = 0;
    float m_ViewportHighlightOpacity = 0;
    bool ValidateViewportGestures();
#endif
    //Vec2f m_ViewportSize = { 1280.f, 400 };
    GEngine::Math::Vec2f m_ViewportBounds[2];
    GEngine::Math::Vec3f m_LightDirection; // = { 20.f, 50.0f, 20.f };
    GEngine::Math::Vec3f m_LightPos;
    bool m_IsPause = false;
    Rbs::DistanceChainImpulseControl m_ChainImpulse;
    float m_ChainImpulseMagnitude = Rbs::DistanceChainImpulseControl::DefaultMagnitude;
    int m_ChainImpulseDirection{};
    bool m_IsShowDebugBoundingBox = false;
    bool m_ShowSelectedColliderOutlines = true;
    bool m_OutlineToggleRequested{}, m_FrameOutlineToggle{};
    Rbs::ColliderOutlineSelection m_ColliderOutlines;
    GEngine::PhysicsSystem::DebugLayers m_ColliderLayers;
    bool m_ColliderEscapeRequested{};
    std::unique_ptr<GEngine::FrameDebugLine[]> m_ColliderDebugStorage;
    std::size_t m_ColliderDebugCapacity{};
    bool m_IsShowKDTree = false;
    std::vector<float> m_ShadowCascadeLevels;
    std::array<float,5> m_ViewCascadeLevels{};
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
