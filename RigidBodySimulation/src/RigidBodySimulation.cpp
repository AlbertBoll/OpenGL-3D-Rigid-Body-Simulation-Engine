#include "RigidBodySimulation.h"
#include "RbsEditorWidgets.h"
#include "RbsLoadingScreen.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Core/RuntimeAssets.h"
#include "Scene/_Scene.h"
#include <format>

#include <new>
#include <utility>
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
#include "../tests/SandboxChecks.h"
#endif

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;

RigidBodySimulationApp::RigidBodySimulationApp(const Rbs::LaunchConfig& launch)
    : BaseApp(launch.engine), m_Launch(launch),
      m_ActiveSelection{launch.scene, launch.chainLinks}, m_PendingSelection(m_ActiveSelection), m_Backend(launch.m_Backend), m_PendingBackend(m_Backend)
{
}

// A parked Scene is never updated or submitted. This is application ownership,
// not another scheduler; both scenes borrow the same idle publication domain.
struct RigidBodySimulationApp::SceneStateStorage
{
    explicit SceneStateStorage(AsyncTextureLoader* loader) : m_Textures(loader) {}
    std::unique_ptr<SceneRenderResources> m_Resources;
    std::optional<FrameSubmission> m_Submission;
    RefPtr<_Scene> m_ActiveScene, m_EditorScene;
    std::vector<std::unique_ptr<PhysicalShape>> m_Shapes;
    std::unique_ptr<AsyncMeshLoader> m_MeshLoads;
    AsyncTextureLoader* m_Textures{};
    Rbs::MaterialGallery m_MaterialGallery;
    Rbs::ShaderReloadGallery m_ShaderGallery;
    Rbs::AsyncTextureGallery m_AsyncGallery;
    UploadTicket m_WoodRequest{}, m_BarrelRequest{};
    MaterialHandle m_AsyncMaterial{};
    bool m_WoodSettled{}, m_LoadBarrel{}, m_BarrelSettled{};
    _EditorCamera m_Camera{45.f, 1280.f, 720.f, .1f, 1000.f};
    EntityRenderId m_EditorView{}, m_PreviewCamera{};
    std::array<EntityRenderId, 2> m_Cameras{};
    CameraView m_CameraView = CameraView::Editor, m_PreviewReturn = CameraView::Editor;
    _Entity m_Sphere, m_Grid, m_Axis, m_Sky, m_Point;
    Vec3f m_LightDirection{}, m_LightPosition{};
    std::vector<float> m_Cascades;
    float m_NearPlane = .1f, m_FarPlane = 40.f;
    bool m_Paused = true, m_BoundingBoxes{};
    Rbs::DistanceChainImpulseControl m_Impulse;
    float m_ImpulseMagnitude = Rbs::DistanceChainImpulseControl::DefaultMagnitude;
    int m_ImpulseDirection{};
    Rbs::SceneSelection m_Selection;
    PhysicsBackend m_Backend{PhysicsBackend::Native};

    bool RetireView()
    {
        if (m_ActiveScene && m_EditorView)
        {
            if (!m_ActiveScene->RetireEditorViewIdentity(m_EditorView)) return false;
            m_EditorView = {};
        }
        return true;
    }

    void Retire() noexcept
    {
        // Requests cannot publish to retired materials; imports join before the
        // mesh registry disappears. Root-owned texture storage remains shared.
        if (m_Textures)
        {
            if (m_WoodRequest) (void)m_Textures->Cancel(m_WoodRequest);
            if (m_AsyncGallery.pending) (void)m_Textures->Cancel(*m_AsyncGallery.pending);
        }
        m_WoodRequest = {};
        m_MeshLoads.reset();
        m_MaterialGallery = {};
        m_ShaderGallery = {};
        m_AsyncGallery = {};
        m_Sphere = m_Grid = m_Axis = m_Sky = m_Point = {};
        (void)RetireView();
        // Scene destruction clears Physics callbacks/body ownership and releases
        // presentation leases. Both aliases must disappear before the publisher.
        m_ActiveScene.reset();
        m_EditorScene.reset();
        m_Shapes.clear();
        m_Submission.reset();
        m_Resources.reset();
    }
    ~SceneStateStorage() { Retire(); }
};

void RigidBodySimulationApp::SwapSceneState(SceneStateStorage& state) noexcept
{
    using std::swap;
    swap(m_SceneResources, state.m_Resources);
    swap(m_FrameSubmission, state.m_Submission);
    swap(m_ActiveScene, state.m_ActiveScene);
    swap(m_EditorScene, state.m_EditorScene);
    swap(m_PhysicsShapes, state.m_Shapes);
    swap(m_MeshLoads, state.m_MeshLoads);
    swap(m_MaterialGallery, state.m_MaterialGallery);
    swap(m_ShaderReloadGallery, state.m_ShaderGallery);
    swap(m_AsyncTextureGallery, state.m_AsyncGallery);
    swap(m_WoodRequest, state.m_WoodRequest);
    swap(m_BarrelRequest, state.m_BarrelRequest);
    swap(m_AsyncBoxMaterial, state.m_AsyncMaterial);
    swap(m_WoodSettled, state.m_WoodSettled);
    swap(m_LoadBarrel, state.m_LoadBarrel);
    swap(m_BarrelSettled, state.m_BarrelSettled);
    swap(m_EditorCamera_, state.m_Camera);
    swap(m_EditorViewIdentity, state.m_EditorView);
    swap(m_PreviewCamera, state.m_PreviewCamera);
    swap(m_SceneCameras, state.m_Cameras);
    swap(m_CameraView, state.m_CameraView);
    swap(m_PreviewReturnView, state.m_PreviewReturn);
    swap(m_Sphere, state.m_Sphere);
    swap(m_GridEntity, state.m_Grid);
    swap(m_AxisEntity, state.m_Axis);
    swap(m_SkyBoxEntity, state.m_Sky);
    swap(m_PointLightEntity, state.m_Point);
    swap(m_LightDirection, state.m_LightDirection);
    swap(m_LightPos, state.m_LightPosition);
    swap(m_ShadowCascadeLevels, state.m_Cascades);
    swap(m_NearPlane, state.m_NearPlane);
    swap(m_FarPlane, state.m_FarPlane);
    swap(m_IsPause, state.m_Paused);
    swap(m_IsShowDebugBoundingBox, state.m_BoundingBoxes);
    swap(m_ChainImpulse, state.m_Impulse);
    swap(m_ChainImpulseMagnitude, state.m_ImpulseMagnitude);
    swap(m_ChainImpulseDirection, state.m_ImpulseDirection);
    swap(m_ActiveSelection, state.m_Selection);
    swap(m_Backend, state.m_Backend);
}

bool RigidBodySimulationApp::ResetSceneInteraction()
{
    if (!CancelCameraGesture()) return false;
    ClearColliderSelection();
    m_ChainImpulse.Bind(m_ChainImpulse.Target(), m_ChainImpulse.Enabled());
    m_CameraSnapshot.reset();
    m_FramePickPixel.reset();
    m_PickTable = EntityPickTable{};
    m_CameraAvailable = false;
    m_CameraUnavailableDiagnostic.clear();
    m_CameraDiagnostic.clear();
    m_ColliderEscapeRequested = false;
    m_ViewPickRequested = m_OutlineToggleRequested = m_FrameOutlineToggle = false;
    m_PreviewCamera = {};
    m_CameraView = m_PreviewReturnView = CameraView::Editor;
    m_ViewportActive = m_ViewportRequestFocus = m_ViewportHighlightPending = false;
    m_ViewportFocused = m_ViewportHovered = false;
    m_ViewportForcused = false;
    m_ViewportNavWindow = 0;
    m_ViewportAltGestureConsumed = false;
    m_ViewportActivationButtons = 0;
    m_ViewportHighlightStart = -1;
    m_HierarchySelectionHandoff=false;
    m_HierarchyRevealed={};
    m_ColliderDebugStorage.reset();
    m_ColliderDebugCapacity = 0;
    m_DebugKDTreeVisualizer.reset();
    m_KDTree.ClearNode();
    m_ObjectsPoints.clear();
    m_KDTreePoints.clear();
    return true;
}

void RigidBodySimulationApp::SetSimulationState(Rbs::SimulationState state)
{
    if (!SandboxEnabled()) return;
    if(state==Rbs::SimulationState::Running){CancelGroupTransform();m_EditorMode=EditorMode::Simulation;}
    if (state == Rbs::SimulationState::Running && !m_ActiveScene)
        state = Rbs::SimulationState::Failed;
    if (state == Rbs::SimulationState::Running && m_SimulationState != state)
        m_SkipSimulationElapsed = true;
    m_SimulationState = state;
    m_IsPause = state != Rbs::SimulationState::Running;
    if (m_ActiveScene) Rbs::SetColliderUserPause(*m_ActiveScene, m_IsPause);
}

bool RigidBodySimulationApp::RequestScene(Rbs::SceneSelection selection)
{
    if (!SandboxEnabled() || !selection.IsValid() ||
        m_SimulationState == Rbs::SimulationState::Switching || m_SceneRequest)
    {
        m_SceneDiagnostic = "Scene request rejected; current scene retained.";
        return false;
    }
    m_PendingSelection = selection;
    m_SceneRequest = selection;
    return true;
}

namespace
{
    auto BackendAuthoringFailure(std::string_view message)
    {return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"RBS backend authoring",message});}
}

std::expected<RigidBodySimulationApp::BackendAuthoring,SceneError>
RigidBodySimulationApp::BackendAuthoring::Capture(_Scene& source)
{
    auto* physics=source.GetPhysicsSystem();auto* world=physics->GetPhysicsWorld();
    if(!world)return BackendAuthoringFailure("Current authored world is unavailable");
    BackendAuthoring result;
    result.m_Gravity=world->GetGravity();result.m_Step=source.GetPhysicsStep();
    result.m_Sleeping=physics->IsSleepingEnabled();result.m_Bodies.reserve(world->GetBodyCount());
    if(world->GetBackend()==PhysicsBackend::Jolt)
    {
        auto settings=world->ReadWorldSleepSettings();
        if(!settings)return BackendAuthoringFailure("World sleep settings readback failed");
        result.m_WorldSleep=*settings;
        if(const auto settings=world->ReadJoltSettings())result.m_JoltWorld=*settings;
    }
    for(auto id:source.GetAllEntitiesWith<RigidBody3DComponent,Transform3DComponent,TagComponent>())
    {
        _Entity entity{id,&source};auto pose=source.ReadBodyTransform(entity);
        if(!pose)return BackendAuthoringFailure("Authored body binding is invalid");
        auto state=world->ReadBody(pose->body);
        if(!state)return BackendAuthoringFailure("Current body state is invalid");
        Body body;const auto& tag=entity.GetComponent<TagComponent>();body.m_Name=tag.NameBeforeEditorRename.empty()?tag.Name:tag.NameBeforeEditorRename;body.m_Old=pose->body;body.m_Pose=pose->authored;
        // Preset construction is deterministic; display names need not be unique.
        // Bind the replacement Scene explicitly and validate the name/membership below.
        // Removed/recycled entities fail mapping instead of aliasing a new Physics handle.
        body.m_FixtureEntity=id;
        body.m_Scale=entity.Transform().Scale;body.m_Rigid=entity.GetComponent<RigidBody3DComponent>();
        body.m_Rigid.RuntimeBody={};body.m_Material=state->material;body.m_Sleep=state->sleep;
        std::expected<std::unique_ptr<PhysicalShape>,PhysicsShapeError> shape;
        if(entity.HasAllComponents<SphereFixture3DComponent>())
        {
            const auto fixture=entity.GetComponent<SphereFixture3DComponent>();body.m_Fixture=fixture;
            shape=CreateCollisionShape(SphereShapeDesc{fixture.Radius});
        }
        else if(entity.HasAllComponents<BoxFixture3DComponent>())
        {
            const auto fixture=entity.GetComponent<BoxFixture3DComponent>();body.m_Fixture=fixture;
            shape=CreateCollisionShape(fixture.Shape,body.m_Scale);
        }
        else if(entity.HasAllComponents<CapsuleFixture3DComponent>())
            body.m_Fixture=entity.GetComponent<CapsuleFixture3DComponent>();
        else if(entity.HasAllComponents<ConvexFixture3DComponent>())
            body.m_Fixture=entity.GetComponent<ConvexFixture3DComponent>();
        else return BackendAuthoringFailure("Backend rebuild encountered an unknown authored collider");
        auto properties=world->ReadBodyProperties(pose->body);
        auto collider=physics->CopyBodyShapeDescriptor(pose->body);
        if(!properties || !collider)return BackendAuthoringFailure("Live body authoring readback failed");
        body.m_Properties=*properties;body.m_Rigid.Type=properties->type;
        body.m_Collider=std::move(*collider);
        std::visit([&](auto& fixture){
            fixture.Property.m_Position=body.m_Pose.translation;fixture.Property.m_Orientation=body.m_Pose.rotation;
            if(body.m_Rigid.Type==BodyType::Dynamic)fixture.Property.m_InvMass=state->inverseMass;
            fixture.Property.m_Friction=state->material.friction;
            fixture.Property.m_Elasticity=state->material.restitution;
            // Authored initial velocities stay in the fixture. Simulated momentum is not migrated.
        },body.m_Fixture);
        result.m_Bodies.push_back(std::move(body));
    }
    if(result.m_Bodies.size()!=world->GetBodyCount())
        return BackendAuthoringFailure("Every rebuilt body requires authored Scene membership");
    result.m_Joints.reserve(world->GetJointCount());
    for(std::size_t i=0;i<world->GetJointCount();++i)
    {
        auto handle=world->JointAt(i);if(!handle)return BackendAuthoringFailure("Invalid joint identity");
        auto state=world->ReadDistanceJoint(*handle);if(!state)return BackendAuthoringFailure("Invalid joint state");
        const auto find=[&](PhysicsBodyHandle h){return std::find_if(result.m_Bodies.begin(),result.m_Bodies.end(),[&](const auto& b){return b.m_Old==h;});};
        auto a=find(state->descriptor.bodyA),b=find(state->descriptor.bodyB);
        if(a==result.m_Bodies.end() || b==result.m_Bodies.end())return BackendAuthoringFailure("Joint endpoint has no authored body");
        result.m_Joints.push_back({state->descriptor,a->m_FixtureEntity,b->m_FixtureEntity});
    }
    return result;
}

ApplicationInitializationResult RigidBodySimulationApp::BackendAuthoring::ApplyBeforeStart(_Scene& target) const
{
    std::size_t count=0;
    for(auto id:target.GetAllEntitiesWith<RigidBody3DComponent>()){(void)id;++count;}
    if(count!=m_Bodies.size() || !target.SetPhysicsStep(m_Step))
        return BackendAuthoringFailure("Generated scene differs from the authored body/clock configuration");
    for(const auto& body:m_Bodies)
    {
        _Entity entity{body.m_FixtureEntity,&target};
        if(!entity || !entity.HasAllComponents<RigidBody3DComponent,TagComponent>() || entity.GetName()!=body.m_Name ||
            !target.SetLocalPose(entity,body.m_Pose.translation,body.m_Pose.rotation) || !target.SetLocalScale(entity,body.m_Scale))
            return BackendAuthoringFailure("Authored body could not be mapped into the replacement scene");
        entity.AddOrReplaceComponent<RigidBody3DComponent>(body.m_Rigid);
        entity.RemoveComponents<SphereFixture3DComponent,BoxFixture3DComponent,CapsuleFixture3DComponent,ConvexFixture3DComponent>();
        std::visit([&](const auto& fixture){entity.AddOrReplaceComponent<std::decay_t<decltype(fixture)>>(fixture);},body.m_Fixture);
    }
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::BackendAuthoring::ApplyAfterStart(_Scene& target) const
{
    auto* physics=target.GetPhysicsSystem();auto* world=physics->GetPhysicsWorld();
    if(!world || world->GetBodyCount()!=m_Bodies.size())return BackendAuthoringFailure("Replacement body membership differs");
    world->SetGravity(m_Gravity);physics->SetSleepingEnabled(m_Sleeping);
    if(m_JoltWorld) {
        if(world->GetBackend()==PhysicsBackend::Jolt) {
            if(!world->UpdateJoltSettings(*m_JoltWorld))return BackendAuthoringFailure("Jolt world parameters rejected");
        } else if(*m_JoltWorld!=PhysicsJoltSettings{})
            return BackendAuthoringFailure("Experimental Jolt parameters cannot map to Native. Restore the preserved baseline before switching.");
    }
    if(m_WorldSleep)
    {
        if(world->GetBackend()==PhysicsBackend::Jolt)
        {
            if(!world->UpdateWorldSleepSettings(*m_WorldSleep))return BackendAuthoringFailure("World sleep settings rejected");
        }
        else if(*m_WorldSleep!=PhysicsWorldSleepSettings{})
            return BackendAuthoringFailure("Native cannot represent Jolt world sleep overrides. Apply Jolt sleep Defaults before switching.");
    }
    for(const auto& body:m_Bodies)
    {
        _Entity entity{body.m_FixtureEntity,&target};auto handle=target.GetBodyBinding(entity);
        if(!handle || !physics->ReplaceBodyCollider(*handle,body.m_Collider) ||
            !world->UpdateBodyProperties(*handle,body.m_Properties))
            return BackendAuthoringFailure("Backend cannot represent the applied collider/body settings; revert unsupported settings before switching");
    }
    while(world->GetJointCount())
    {
        auto handle=world->JointAt(0);
        if(!handle || !world->DestroyJoint(*handle))return BackendAuthoringFailure("Candidate joint retirement failed");
    }
    for(const auto& joint:m_Joints)
    {
        auto a=target.GetBodyBinding(_Entity{joint.m_A,&target});
        auto b=target.GetBodyBinding(_Entity{joint.m_B,&target});
        if(!a || !b)return BackendAuthoringFailure("Replacement joint endpoint is unavailable");
        auto desc=joint.m_Desc;desc.bodyA=*a;desc.bodyB=*b;
        if(!world->CreateDistanceJoint(desc))return BackendAuthoringFailure("Replacement distance joint was rejected");
    }
    return {};
}

bool RigidBodySimulationApp::RequestBackend(PhysicsBackend backend)
{
    if(!SandboxEnabled() || !m_ActiveScene || !PhysicsBackendAvailable(backend))
    {m_SceneDiagnostic="Backend unavailable; current scene retained.";return false;}
    if(backend==m_Backend)
    {m_SceneDiagnostic="Selected backend is already active.";return false;}
    if(!RequestScene(m_ActiveSelection))return false;
    m_PendingBackend=backend;
    m_BackendRequest=backend;
    m_SceneDiagnostic="Backend Apply queued for the next Update boundary.";
    return true;
}

void RigidBodySimulationApp::ProcessSceneRequest()
{
    if (!SandboxEnabled() || !m_SceneRequest) return;
    const auto requested = *std::exchange(m_SceneRequest, std::nullopt);
    const auto backendRequest=std::exchange(m_BackendRequest,std::nullopt);
    const auto requestedBackend=backendRequest.value_or(m_Backend);
    const bool keepViewportActive=m_ViewportActive;
    // BaseApp has made the main context current and finished event delivery.
    // No FrameSceneInput, extraction scope, or submitted-frame lease is live here.
    if (!m_SceneResources || !m_SceneResources->Publication().CanPublish())
    {
        m_SceneDiagnostic = "Scene switch unavailable outside the publication boundary.";
        SetSimulationState(m_ActiveScene ? Rbs::SimulationState::Paused : Rbs::SimulationState::Failed);
        return;
    }
    if (!ResetSceneInteraction())
    {
        m_SceneDiagnostic = "Scene switch rejected: input cancellation failed.";
        SetSimulationState(m_ActiveScene ? Rbs::SimulationState::Paused : Rbs::SimulationState::Failed);
        return;
    }
    SetSimulationState(Rbs::SimulationState::Switching);
    SceneStateStorage previous(m_TextureLoads);
    bool parked = false;
    bool built = false;
    m_SceneDiagnostic.clear();
    try
    {
        const auto construct = [&]() -> ApplicationInitializationResult
        {
            std::optional<BackendAuthoring> authoring;
            if(backendRequest)
            {
                auto captured=BackendAuthoring::Capture(*m_ActiveScene);
                if(!captured){m_SceneDiagnostic=std::string(captured.error().message);return std::unexpected(captured.error());}
                authoring=std::move(*captured);
            }
            auto resources = SceneRenderResources::Create(GetEngineContext());
            if (!resources) return SceneFailure(resources.error());
            auto submission = FrameSubmission::Create();
            if (!submission)
                return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                    "scene frame submission", DescribeSubmissionError(submission.error())});
            SwapSceneState(previous);
            parked = true;
            m_ActiveSelection = requested;
            m_Backend=requestedBackend;
            m_SceneResources = std::move(*resources);
            m_FrameSubmission.emplace(std::move(*submission));
            m_LoadBarrel = m_Launch.startupMesh == Rbs::StartupMesh::Barrel;
            auto initialized = InitializeScene(authoring?&*authoring:nullptr);
            if (!initialized) return initialized;
            if(authoring)
                if(auto restored=authoring->ApplyAfterStart(*m_ActiveScene);!restored)return restored;
            if (std::exchange(m_InjectSceneConstructionFailure, false))
            {
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
                Rbs::SandboxChecks::BeforeInjectedFailure(*this);
#endif
                return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                    "sandbox construction injection", "Injected failure before scene commit"});
            }
            return {};
        };
        auto initialized = construct();
        built = bool(initialized);
        if (!initialized)
        {
            ReportApplicationError(initialized.error());
            if(m_SceneDiagnostic.empty())
                std::visit([&](const auto& error){
                    if constexpr(requires {std::string(error.message);})m_SceneDiagnostic=std::string(error.message);
                    else m_SceneDiagnostic="Scene construction failed; see the application log";
                },initialized.error());
            m_SceneDiagnostic += "; previous scene/backend retained and paused.";
        }
    }
    catch (const std::exception& error)
    {
        Log::GetCoreLogger()->error("Scene construction failed: {}", error.what());
        m_SceneDiagnostic = "Scene construction failed; previous scene retained and paused.";
    }
    if (built && !previous.RetireView())
    {
        built = false;
        m_SceneDiagnostic = "Scene switch rejected: previous camera identity could not retire.";
    }
    if (!built)
    {
        if (parked) SwapSceneState(previous); // Failed candidate is now the retiring owner.
        previous.Retire();
        SetSimulationState(m_ActiveScene ? Rbs::SimulationState::Paused : Rbs::SimulationState::Failed);
        m_SkipSimulationElapsed = true;
        return;
    }
    previous.Retire();
    m_IsShowDebugBoundingBox = false;
    m_ShowSelectedColliderOutlines = false;
    ++m_SceneGeneration;
    Rbs::Editor::Numeric::Reset();
    if(backendRequest)
    {
        m_ColliderLayers.contactPoints=m_ColliderLayers.contactNormals=false;
        m_SceneDiagnostic="Backend applied; authored scene rebuilt at Tick 0. Press Start.";
    }
    else m_SceneDiagnostic = "Scene loaded; press Start.";
    m_SkipSimulationElapsed = true;
    SetSimulationState(Rbs::SimulationState::Ready);
    // Reset cancels captures and stale world identities, but a Viewport-owned
    // action must not turn the next object click into an activation-only click.
    if(keepViewportActive){SetViewportActive(true,false);m_ViewportRequestFocus=false;}
}

#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
namespace PreEditorLifecycle { void FinishFocusTrace(const char* reason); }
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
namespace PreEditorLifecycle { void FinishPickTrace(const char* reason) noexcept; }
#endif

RigidBodySimulationApp::~RigidBodySimulationApp()
{
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
    PreEditorLifecycle::FinishFocusTrace("application_teardown_before_trace_decision");
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PreEditorLifecycle::FinishPickTrace("application_teardown_before_first_pick");
#endif
    if (auto* input = GetInputManager())
        if (auto cancelled = input->CancelInput(InputCancelReason::OwnerRelease); !cancelled)
            Log::GetCoreLogger()->error("RBS input cancellation failed: {}",
                                        int(cancelled.error()));
    m_Subscriptions.reset();
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::Subscriptions))
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_13_APP_RELEASED");
    if (m_ActiveScene) Rbs::ResetColliderEditor(*m_ActiveScene, m_IsPause);
    if (m_TextureLoads)
    {
        if (m_WoodRequest) (void)m_TextureLoads->Cancel(m_WoodRequest);
        if (m_AsyncTextureGallery.pending) (void)m_TextureLoads->Cancel(*m_AsyncTextureGallery.pending);
    }
    m_CameraSnapshot.reset();
    m_FramePickPixel.reset();
    m_PickTable = EntityPickTable{};
    m_ChainImpulse = {};
    m_MaterialGallery = {};
    m_ShaderReloadGallery = {};
    m_AsyncTextureGallery = {};
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
        if (m_ActiveScene && m_EditorViewIdentity)
            if (auto retired = m_ActiveScene->RetireEditorViewIdentity(m_EditorViewIdentity); !retired)
                Log::GetCoreLogger()->error("Editor view retirement failed: {}", int(retired.error().code));
#ifdef GENGINE_LIFECYCLE_VALIDATION
        if (m_ActiveScene && !m_ActiveScene->RenderData().Resolve(m_EditorViewIdentity) &&
            GetInputManager()->PointerCapture() == InputLayer::None)
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_TEARDOWN_PASS transient_retired=true capture=none");
#endif
        m_EditorViewIdentity = {};
        m_ActiveScene.reset();
        m_EditorScene.reset();
        m_PhysicsShapes.clear();
        m_CameraPanels.clear();
        m_FrameSubmission.reset();
        m_SceneResources.reset();
    }
#ifdef GENGINE_RBS_MODULARITY_VALIDATION
    Log::GetCoreLogger()->info(
        "PRE_EDITOR_PACKET_A_SHUTDOWN_PASS subscriptions_before_state=true resources_before_context=true");
#endif
}

ApplicationInitializationResult
RigidBodySimulationApp::Initialize(const std::initializer_list<WindowProperties>& properties)
{
    Rbs::Editor::LoadingScreen loading;
    InitializationProgress::Scope progress(&loading,&Rbs::Editor::LoadingScreen::Observe,SandboxEnabled());
    try
    {
        if (auto initialized = InitializeValidationPlatform(properties); !initialized)return initialized;
        if (m_Launch.AsyncPerformanceEnabled())return {};
        if (SandboxEnabled() && !loading.fontsInitialized)Rbs::Editor::Initialize();
        InitializationProgress::Report(InitializationProgress::Stage::Renderer,"Preparing shaders and render resources");
        if (auto resources = InitializeResources(); !resources)return resources;
        InitializationProgress::Complete(InitializationProgress::Stage::Renderer);
        auto scene=InitializeScene();
        if(scene && SandboxEnabled())
        {
            InitializationProgress::Complete(InitializationProgress::Stage::Physics);
            InitializationProgress::Complete(InitializationProgress::Stage::Scene);
            InitializationProgress::Report(InitializationProgress::Stage::Editor,"Preparing the initial Editor view");
            m_IsShowDebugBoundingBox=false;m_ShowSelectedColliderOutlines=false;
            SetSimulationState(Rbs::SimulationState::Ready);
            InitializationProgress::Complete(InitializationProgress::Stage::Editor);loading.Finish();
        }
        return scene;
    }
    catch(const InitializationProgress::Cancelled&)
    {
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
            loading.cancelled?"startup cancelled":"loading presentation","Editor initialization stopped before completion"});
    }
}

ApplicationInitializationResult RigidBodySimulationApp::Initialize(const WindowProperties& prop)
{
    return Initialize(std::initializer_list<WindowProperties>{prop});
}

ApplicationInitializationResult RigidBodySimulationApp::InitializeResources()
{
    m_AudioSystem = CreateScopedPtr<Audio::AudioSystem>();
    if (auto audio = m_AudioSystem->Initialize(); !audio)
        return std::unexpected(audio.error());
    auto resources = SceneRenderResources::Create(GetEngineContext());
    if (!resources)
        return SceneFailure(resources.error());
    m_SceneResources = std::move(*resources);
    auto meshRoot = RuntimeAssets::TryFile("Models");
    if (!meshRoot)
        return std::unexpected(meshRoot.error());
    m_MeshRoot = *meshRoot;
    // Opt-in validation exercises the same action as the File menu.
    m_LoadBarrel = m_Launch.startupMesh == Rbs::StartupMesh::Barrel;
    auto submission = FrameSubmission::Create();
    if (!submission)
    {
        Log::GetCoreLogger()->error("{}", DescribeSubmissionError(submission.error()));
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "frame submission",
                                             DescribeSubmissionError(submission.error())});
    }
    m_FrameSubmission.emplace(std::move(*submission));
    return {};
}

void RigidBodySimulationApp::SetViewportActive(bool active, bool cancelInput)
{
    if (m_ViewportActive == active) return;
    m_ViewportActive = active;
    if (active)
    {
        m_ViewportRequestFocus = m_ViewportHighlightPending = true;
#ifdef GENGINE_LIFECYCLE_VALIDATION
        ++m_ViewportActivations;
#endif
        return;
    }
    m_ViewportRequestFocus = m_ViewportHighlightPending = false;
    m_ViewportHighlightStart = -1;
    m_ViewportActivationButtons = 0;
    m_ViewPickRequested = false;
    if (cancelInput)
        if (auto cancelled = GetInputManager()->CancelInput(InputCancelReason::Transfer, InputLayer::View);
            !cancelled)
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Viewport input", "",
                "focus transfer cancellation", "", "View owner cancellation failed"});
}

ApplicationInitializationResult RigidBodySimulationApp::ConnectApplicationEventHandlers()
{
    // Application-lifetime callbacks resolve current members when delivered.
    // Staged construction never adds a second set or dispatches an event.
    if (m_Subscriptions) return {};
    auto connections = std::unique_ptr<Rbs::Subscriptions>(new (std::nothrow) Rbs::Subscriptions);
    if (!connections)
        return std::unexpected(PlatformError{PlatformErrorCode::Allocation, "RBS input",
                                             "Could not allocate connection ownership"});
    auto& input = *GetInputManager();
    auto view = input.Subscribe(InputLayer::View,
                                [this](InputDelivery& delivery)
                                {
                                    const auto& event = delivery.event;
                                    const auto& route = GetInputManager()->Routing();
                                    const auto point = event.kind == InputKind::ButtonDown || event.kind == InputKind::Motion
                                        ? Math::Vector2{event.x, event.y} : GetInputManager()->GetPhysicalState().m_Mouse.GetPosition();
                                    const bool inside = route.visible && point.x >= route.left && point.x < route.right &&
                                        point.y >= route.top && point.y < route.bottom;
                                    const auto cameraInput = [this, &event](bool eligible)
                                    {
                                        if (auto result = m_EditorCamera_.OnViewInput(event,
                                                eligible && m_CameraView == CameraView::Editor); !result)
                                        {
                                            ReportPlatformError(result.error());
                                            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
                                                "Editor Camera input", "", "owned View gesture", "", result.error().message});
                                        }
                                    };
                                    if (event.cleanup)
                                    {
                                        const bool activation = m_ViewportActivationButtons != 0;
                                        if (event.kind == InputKind::Cancel)
                                        {
                                            if(event.cancellation==InputCancelReason::FocusLoss || event.cancellation==InputCancelReason::Hidden)
                                                m_HierarchySelectionHandoff=false;
                                            if (const auto* error = GetInputManager()->NativePollFailure())
                                                FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
                                                    "Native input transition", "", error->operation, "", error->message});
                                            m_ViewPickRequested = false;
                                            CancelViewportMarquee();
                                            m_ViewportActivationButtons = 0;
                                            if (event.cancellation == InputCancelReason::FocusLoss ||
                                                event.cancellation == InputCancelReason::Hidden ||
                                                event.cancellation == InputCancelReason::Transfer)
                                                SetViewportActive(false, false);
                                        }
                                        else if (event.kind == InputKind::ButtonUp)
                                            m_ViewportActivationButtons &= ~GENGINE_BUTTON(event.button);
                                        cameraInput(m_ViewportActive && !activation);
                                        return;
                                    }
                                    if (event.kind == InputKind::Motion ||
                                        event.kind == InputKind::ButtonDown ||
                                        event.kind == InputKind::Wheel)
                                    {
                                        const bool fresh = event.kind == InputKind::ButtonDown &&
                                            !GetInputManager()->GetViewState().m_Mouse.isButtonHeld(event.button);
                                        delivery.Handle();
                                        if (fresh && inside && !m_ViewportActive)
                                        {
                                            SetViewportActive(true);
                                            if(!m_HierarchySelectionHandoff || event.button!=GENGINE_BUTTON_LEFT || (event.modifiers & LeftAlt))
                                                m_ViewportActivationButtons |= GENGINE_BUTTON(event.button);
                                        }
                                        else if (fresh && m_ViewportActivationButtons)
                                            m_ViewportActivationButtons |= GENGINE_BUTTON(event.button);
                                        if(fresh)m_HierarchySelectionHandoff=false;
                                        const bool eligible = m_ViewportActive && !m_ViewportActivationButtons &&
                                            (inside || (!fresh && GetInputManager()->PointerCapture() == InputLayer::View));
                                        if (eligible && fresh && event.button == GENGINE_BUTTON_LEFT)
                                        {
                                            m_ViewPickRequested = (event.modifiers & LeftAlt) == 0;
                                            m_ViewSelectionAdd=(event.modifiers & (LeftShift|RightShift))!=0;
                                            m_OutlineToggleRequested = (event.modifiers & (LeftControl | RightControl)) != 0;
                                        }
                                        if ((event.modifiers & LeftAlt) &&
                                            GetInputManager()->GetViewState().m_Mouse.m_CurrentButtons)
                                        {
                                            m_ViewportAltGestureConsumed = true;
                                            m_ViewPickRequested = false;
                                        }
                                        cameraInput(eligible);
                                        if (eligible && event.kind == InputKind::Wheel && m_CameraView == CameraView::Editor)
                                            m_EditorCamera_.OnMouseScroll(event.wheelY);
                                        return;
                                    }
                                    if (event.kind != InputKind::KeyDown)
                                        return;
                                    if(HandleTransformToolShortcut(event,route,
                                        GetInputManager()->GetPhysicalState().m_Mouse.m_CurrentButtons,
                                        GetInputManager()->PointerCapture()!=InputLayer::None))
                                    {delivery.Handle();return;}
                                    std::optional<ViewportMode> direction;
                                    switch (event.key)
                                    {
                                    case GENGINE_KEY_ESCAPE:
                                        if (m_ViewportActive && !event.repeated)
                                        {
                                            delivery.Handle();
                                            if(m_Marquee.armed || m_Marquee.waiting)CancelViewportMarquee();
                                            else if(m_GroupTransform.dirty){m_GroupTransform.released=false;m_GroupTransform.pending=Rbs::Editor::TransformSession::Action::Cancel;}
                                            else m_ColliderEscapeRequested = true;
                                        }
                                        return;
                                    case GENGINE_KEY_Z:
                                    case GENGINE_KEY_Y:
                                        if(m_ViewportActive && !event.repeated && (event.modifiers&(LeftControl|RightControl)))
                                        {
                                            delivery.Handle();
                                            m_GroupTransform.pending=event.key==GENGINE_KEY_Z?Rbs::Editor::TransformSession::Action::Undo:Rbs::Editor::TransformSession::Action::Redo;
                                        }
                                        return;
                                    case GENGINE_KEY_I:
                                        direction = ViewportMode::FRONT;
                                        break;
                                    case GENGINE_KEY_K:
                                        direction = ViewportMode::BACK;
                                        break;
                                    case GENGINE_KEY_J:
                                        direction = ViewportMode::LEFT;
                                        break;
                                    case GENGINE_KEY_L:
                                        direction = ViewportMode::RIGHT;
                                        break;
                                    case GENGINE_KEY_U:
                                        direction = ViewportMode::TOP;
                                        break;
                                    case GENGINE_KEY_O:
                                        direction = ViewportMode::BOTTOM;
                                        break;
                                    case GENGINE_KEY_TAB:
                                        direction = ViewportMode::DEFAULT;
                                        break;
                                    case GENGINE_KEY_W:
                                    case GENGINE_KEY_A:
                                    case GENGINE_KEY_S:
                                    case GENGINE_KEY_D:
                                    case GENGINE_KEY_Q:
                                    case GENGINE_KEY_E:
                                    case GENGINE_KEY_LALT:
                                    case GENGINE_KEY_RALT:
                                    case GENGINE_KEY_LCTRL:
                                    case GENGINE_KEY_RCTRL:
                                    case GENGINE_KEY_LSHIFT:
                                    case GENGINE_KEY_RSHIFT:
                                        delivery.Handle();
                                        if (m_ViewportActive && !m_ViewportActivationButtons)
                                        {
                                            if ((event.modifiers & LeftAlt) &&
                                                GetInputManager()->GetViewState().m_Mouse.m_CurrentButtons)
                                            {
                                                m_ViewportAltGestureConsumed = true;
                                                m_ViewPickRequested = false;
                                            }
                                            cameraInput(inside || GetInputManager()->PointerCapture() == InputLayer::View);
                                        }
                                        return;
                                    default:
                                        return;
                                    }
                                    delivery.Handle();
                                    if (m_ViewportActive && !m_ViewportActivationButtons &&
                                        !event.repeated && m_CameraView == CameraView::Editor)
                                        m_EditorCamera_.Initialize(*direction);
                                });
    auto game = input.Subscribe(InputLayer::Game,
                                [this](InputDelivery& delivery)
                                {
                                    const auto& event = delivery.event;
                                    if(SandboxEnabled() && event.key!=GENGINE_KEY_SPACE)return;
                                    if (event.cleanup || event.kind != InputKind::KeyDown)
                                        return;
                                    if (event.key != GENGINE_KEY_P && event.key != GENGINE_KEY_R &&
                                        event.key != GENGINE_KEY_SPACE)
                                        return;
                                    delivery.Handle();
                                    if (event.repeated)
                                        return;
                                    if (event.key == GENGINE_KEY_P)
                                    {
                                        if (SandboxEnabled()) SetSimulationState(Rbs::SimulationState::Paused);
                                        else m_IsPause = true;
                                    }
                                    else if (event.key == GENGINE_KEY_R)
                                    {
                                        if (SandboxEnabled()) SetSimulationState(Rbs::SimulationState::Running);
                                        else m_IsPause = false;
                                    }
                                    else
                                        m_IsShowDebugBoundingBox = !m_IsShowDebugBoundingBox;
                                    if (!SandboxEnabled() && event.key != GENGINE_KEY_SPACE && m_ActiveScene)
                                        Rbs::SetColliderUserPause(*m_ActiveScene, m_IsPause);
                                });
    auto focus = input.Observe([this](const InputEvent& event)
    {
        // Hover and capture release do not transfer activation. A routed UI press does.
        if (!event.cleanup && event.kind == InputKind::ButtonDown &&
            event.destination != InputLayer::View)
        {
            m_HierarchySelectionHandoff=false;
            SetViewportActive(false);
        }
    });
    if (!view || !game || !focus)
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "RBS input",
                                             "Typed input subscription rejected"});
    connections->tokens[0] = std::move(*view);
    connections->tokens[1] = std::move(*game);
    connections->tokens[2] = std::move(*focus);
    m_Subscriptions = std::move(connections);
    return {};
}
