#include "RigidBodySimulation.h"
#include "RbsEditorWidgets.h"
#include "RbsEditorState.h"
#include "RbsLoadingScreen.h"
#include <imgui/imgui_impl_sdl.h>
#include "Core/Log.h"
#include "Core/Window.h"
#include "Core/RuntimeAssets.h"
#include "Scene/_Scene.h"
#include "../tests/RestingContactDiagnosis.h"
#ifdef GENGINE_PHASE29_QUALIFICATION
#include "../tests/PassiveRagdollChecks.h"
#endif
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

#ifdef GENGINE_PHASE28_QUALIFICATION
namespace Rbs::RestDiagnosis
{
    bool Phase28DefaultInitProbe(std::ostream& out)
    {
        using Capture = Phase21Capture;
        static_assert(std::tuple_size_v<decltype(Capture::rows)> == 32);
        static_assert(std::tuple_size_v<decltype(Capture::PairStorage::state)> == 17020);
        static_assert(std::tuple_size_v<decltype(Capture::PairStorage::differences)> == 17020);
        static_assert(sizeof(Capture::PairState) == 64);
        static_assert(std::tuple_size_v<decltype(Capture::PenPacket::stages)> == 4);
        static_assert(std::tuple_size_v<decltype(Capture::PenStage::contact)> == 1024);
        static_assert(std::tuple_size_v<decltype(Capture::PenPacket::positions)> == 1024);
        static_assert(std::is_aggregate_v<Capture::PenContact> && std::is_aggregate_v<Capture::PenPosition>);
        const auto zero = [](const auto& values) {
            return std::all_of(values.begin(), values.end(),
                               [](const auto& value) { return value == 0; });
        };
        const auto zeroVec = [](const auto& v) {
            return v.x == 0 && v.y == 0 && v.z == 0;
        };
        const auto identity = [](const auto& q) {
            return q.w == 1 && q.x == 0 && q.y == 0 && q.z == 0;
        };
        const auto rowDefault = [&](const Capture::Row& r) {
            if (std::array<unsigned,8>{r.tick,r.count,r.contacts,r.islands,
                    r.quiet,r.sleeping,r.wakes,r.sleeps} != std::array<unsigned,8>{} ||
                r.hash != 14695981039346656037ull ||
                std::array<std::uint64_t,5>{r.capacity,r.growth,r.callbackNs,
                    r.eventStart,r.eventNs} != std::array<std::uint64_t,5>{} ||
                std::array<double,10>{r.energy,r.kinetic,r.potential,r.penetration,
                    r.linear,r.angular,r.drift,r.normalImpulse,r.frictionImpulse,
                    r.restitution} != std::array<double,10>{} ||
                !zero(r.cpu) || !zero(r.sleepCpu) || !zero(r.operations) ||
                !zero(r.kineticChange) || !zero(r.potentialChange) ||
                !zero(r.slots) || !zero(r.generations) ||
                !zero(r.bodyContacts) || !zero(r.bodyPenetration) ||
                r.before.stepCount != 0 || r.before.bodyCount != 0 ||
                r.before.physicsWorldTimeNs != 0) return false;
            return std::all_of(r.body.begin(), r.body.end(), [&](const auto& b) {
                return zeroVec(b.position) && zeroVec(b.linear) &&
                       zeroVec(b.angular) && identity(b.orientation) &&
                       b.inactive == 0 && !b.sleeping;
            });
        };
        const auto pairDefault = [](const Capture::PairState& v) {
            return v.anchor == 0 && v.peak == 0 &&
                std::array<unsigned,10>{v.seen,v.first,v.safeStart,v.run,
                    v.exposure,v.episodes,v.totalExposure,v.firstUnsafe,
                    v.lastUnsafe,v.lastObserved} == std::array<unsigned,10>{} &&
                v.a == 0 && v.b == 0 && !v.hasAnchor && !v.durationViolation;
        };
        const auto differenceDefault = [](const Capture::PairStorage::Difference& v) {
            return v.maximum == 0 && v.samples == 0 && v.first == 0 && v.last == 0;
        };
        const auto contactDefault = [&](const Capture::PenContact& v) {
            return v.a == 0 && v.b == 0 && v.manifold == 0 && v.slot == 0 &&
                v.featureA == 0 && v.featureB == 0 && v.separation == 0 && v.toi == 0 && v.depth == 0 &&
                zeroVec(v.cachedA) && zeroVec(v.cachedB) && zeroVec(v.localA) && zeroVec(v.localB) &&
                zeroVec(v.normal) && zeroVec(v.currentA) && zeroVec(v.currentB);
        };
        const auto positionDefault = [&](const Capture::PenPosition& v) {
            return v.a == 0 && v.b == 0 && v.slot == 0 && v.depth == 0 &&
                zeroVec(v.beforeA) && zeroVec(v.afterA) && zeroVec(v.beforeB) && zeroVec(v.afterB) &&
                zeroVec(v.normal) && zeroVec(v.anchorA) && zeroVec(v.anchorB) && zeroVec(v.localA) && zeroVec(v.localB);
        };

        const bool sessionEmptyBefore = !session;
        std::unique_ptr<Capture> capture(new(std::nothrow) Capture);
        const bool allocated = bool(capture);
        bool ok = sessionEmptyBefore && allocated;
        std::size_t rowsChecked{}, pairsChecked{}, differencesChecked{};
        std::size_t penStagesChecked{}, penContactsChecked{}, penPositionsChecked{};
        if (capture) {
            ok &= capture->rows.size() == 32 &&
                capture->pairs.state.size() == 17020 &&
                capture->pairs.differences.size() == 17020;
            for (const auto& r : capture->rows) { ++rowsChecked; ok &= rowDefault(r); }
            for (const auto& v : capture->pairs.state) { ++pairsChecked; ok &= pairDefault(v); }
            for (const auto& v : capture->pairs.differences) {
                ++differencesChecked; ok &= differenceDefault(v);
            }
            // Same default-initializing allocation as the diagnostic owner.
            // Value-initialized single elements provide a bounded reference too;
            // no raw padding reads or second diagnostic allocation are needed.
            ok &= contactDefault(Capture::PenContact{}) && positionDefault(Capture::PenPosition{});
            for (const auto& s : capture->pen.stages) {
                ++penStagesChecked;
                ok &= s.stage == 0 && s.tick == 0 && s.count == 0 && s.contacts == 0 &&
                    zero(s.slots) && zero(s.generations);
                for (const auto& b : s.body)
                    ok &= zeroVec(b.position) && zeroVec(b.linear) && zeroVec(b.angular) &&
                        identity(b.orientation) && b.inactive == 0 && !b.sleeping;
                for (const auto& v : s.contact) { ++penContactsChecked; ok &= contactDefault(v); }
            }
            for (const auto& v : capture->pen.positions) { ++penPositionsChecked; ok &= positionDefault(v); }
            ok &= capture->pen.positionCount == 0 && capture->pen.source == 0 && capture->pen.a == 0 &&
                capture->pen.b == 0 && capture->pen.manifold == 0 && capture->pen.slot == 0 &&
                capture->pen.ground == 0 && !capture->pen.correcting && !capture->pen.written;
            ok &=
                capture->pairBytes == 4096 && capture->count == 0 &&
                capture->used == 0 && capture->pairs.workCount == 0 &&
                capture->pairs.pendingCount == 0 && !capture->pairs.closed &&
                zero(capture->pairs.work) && zero(capture->pairs.pending) &&
                zero(capture->pairs.slots) && zero(capture->pairs.generations) &&
                zero(capture->pairs.quietTicks) && zero(capture->pairs.quiet) &&
                zero(capture->pairs.priorQuiet) &&
                zero(capture->pairs.traceBuffer) && zero(capture->pairs.eventBuffer) &&
                std::all_of(capture->cases.begin(), capture->cases.end(),
                    [](const auto& c) { return c.kind == 0 && c.hz == 120 &&
                        c.passes == 1 && c.ticks == 0 && c.sleep && !c.name; });
        }
        capture.reset(); // Test-only allocation is gone before first physics tick.
        const bool released = !capture && !session;
        ok = ok && released;
        out << "{\"schema\":1,\"complete\":" << (ok ? "true" : "false")
            << ",\"allocated\":" << (allocated ? "true" : "false")
            << ",\"released_before_physics\":" << (released ? "true" : "false")
            << ",\"rows_checked\":" << rowsChecked
            << ",\"pairs_checked\":" << pairsChecked
            << ",\"differences_checked\":" << differencesChecked
            << ",\"pen_stages_checked\":" << penStagesChecked
            << ",\"pen_contacts_checked\":" << penContactsChecked
            << ",\"pen_positions_checked\":" << penPositionsChecked
            << ",\"pen_contact_bytes\":" << sizeof(Capture::PenContact)
            << ",\"pen_position_bytes\":" << sizeof(Capture::PenPosition)
            << ",\"pen_packet_bytes\":" << sizeof(Capture::PenPacket)
            << ",\"row_hash_default\":14695981039346656037"
            << ",\"row_bytes\":" << sizeof(Capture::Row)
            << ",\"pair_state_bytes\":" << sizeof(Capture::PairState)
            << ",\"difference_bytes\":" << sizeof(Capture::PairStorage::Difference)
            << ",\"pair_storage_bytes\":" << sizeof(Capture::PairStorage)
            << ",\"capture_bytes\":" << sizeof(Capture) << "}\n";
        return ok && bool(out);
    }
}
#endif

bool RigidBodySimulationApp::BeginRestingContactEpisode()
{
    auto& diagnostic=*Rbs::RestDiagnosis::session;
    if (diagnostic.episodeDone && !diagnostic.complete && !diagnostic.failed)
    {
        ++diagnostic.episode;
        if (!diagnostic.BeginEpisode(*m_ActiveScene,*m_SceneResources,diagnostic.assets,m_PhysicsShapes))
        {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Phase19","","episode setup","",diagnostic.failure});
            return false;
        }
    }
    m_IsPause=false;
    return true;
}

bool RigidBodySimulationApp::CompleteRestingContactFrame(Timestep ts)
{
    auto& diagnostic=*Rbs::RestDiagnosis::session;
    if (!diagnostic.FrameComplete(ts.GetSecondsPrecise()))
    {
        FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Phase19","","record diagnostic","",diagnostic.failure});
        return false;
    }
    if (diagnostic.complete) ShutDown();
    return true;
}

ApplicationInitializationResult
RigidBodySimulationApp::InitializeRestingContactScene(Rbs::SceneAssets& assets)
{
    if (auto result=CreateEnvironmentHelpers();!result) return result;
    auto& diagnostic=Rbs::RestDiagnosis::session;
    diagnostic=std::make_unique<Rbs::RestDiagnosis::Session>(*m_Launch.restingContactDiagnostic);
    // Exercise the normal default and rejected pre-step selections before
    // choosing the admitted rate. No separate Physics scheduler is created.
    const bool clock=m_ActiveScene->GetPhysicsStep().count()==1.0/60.0 &&
        !m_ActiveScene->SetPhysicsStep(Seconds{0}) &&
        !m_ActiveScene->SetPhysicsStep(Seconds{std::numeric_limits<double>::quiet_NaN()}) &&
        !m_ActiveScene->SetPhysicsStep(Seconds{1.0/90.0}) &&
        m_ActiveScene->GetPhysicsStep().count()==1.0/60.0 &&
        m_ActiveScene->SetPhysicsStep(Seconds{1.0/120.0});
    if (!clock || !diagnostic->Open() ||
        !diagnostic->BeginEpisode(*m_ActiveScene,*m_SceneResources,assets,m_PhysicsShapes))
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization,"Phase19 diagnostic",
            "Clock, frozen fixture or recorder initialization failed"});
    const Vec3f eye(25,25,35), focal(0,9,0), offset=eye-focal;
    FocusMaterialCamera(m_EditorCamera_,focal,std::asin(offset.y/glm::length(offset)),
                        -std::atan2(offset.x,offset.z),glm::length(offset));
    m_IsShowDebugBoundingBox=true;
    const auto logical=GetWindow()->GetLogicalSize();
    const auto pixels=GetWindow()->GetFramebufferPixelSize();
    Log::GetCoreLogger()->info("PHASE19_VIEW logical={}x{} physical={}x{} camera=({},{},{}) step={}",
        logical.Width,logical.Height,pixels.Width,pixels.Height,m_EditorCamera_.GetPosition().x,
        m_EditorCamera_.GetPosition().y,m_EditorCamera_.GetPosition().z,m_ActiveScene->GetPhysicsStep().count());
    return ConnectApplicationEventHandlers();
}

RigidBodySimulationApp::RigidBodySimulationApp(const Rbs::LaunchConfig& launch)
    : BaseApp(launch.engine), m_Launch(launch),
      m_ActiveSelection{launch.scene, launch.chainLinks}, m_PendingSelection(m_ActiveSelection), m_Backend(launch.m_Backend), m_PendingBackend(m_Backend)
{
    if (launch.scene == Rbs::RbsScenePreset::PassiveRagdoll)
    {
        m_Ragdoll.committed = Rbs::ReferenceRagdollPacket();
        m_Ragdoll.draft = m_Ragdoll.committed;
        m_Ragdoll.draftGeneration = m_SceneGeneration;
    }
    if (m_ActiveSelection.IsAngularScene())
    {
        if (auto reference=Rbs::ReferenceAngularPacket(launch.scene,launch.angularVariant))
        {
            m_Angular.committed=*reference;
            m_Angular.draft=*reference;
            m_Angular.draftBaseRevision=m_Angular.revision;
            m_PendingAngularVariant=launch.angularVariant;
        }
    }
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
    PhysicsBackend m_Backend{DefaultPhysicsBackend};
    AngularBindings m_AngularBindings;
    RagdollBindings m_RagdollBindings;

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
    swap(m_AngularBindings, state.m_AngularBindings);
    swap(m_RagdollBindings, state.m_RagdollBindings);
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
    m_ViewportAltGestureConsumed = false;
    m_ViewportActivationButtons = 0;
    m_ViewportHighlightStart = -1;
    m_ViewportOwnsSelection=false;
    m_ViewportAwaitPointer=true;
    m_VisibleSceneIconCount=0;
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
    if(m_CameraPilot && state==Rbs::SimulationState::Running){m_CameraDiagnostic="Commit or Cancel Pilot before running simulation.";return;}
    if(m_Ragdoll.dirty && state==Rbs::SimulationState::Running)
    {m_SceneDiagnostic="Apply or Revert the pending ragdoll changes before starting.";return;}
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
    if (m_Angular.dirty || m_SceneDefaultsPending || HasPendingObjectProperties())
    {
        m_SceneDiagnostic="Apply or Revert pending edits before resetting or changing the scene or backend.";
        return false;
    }
    // Reset queues reconstruction of the committed packet at the next safe
    // Update boundary, including when the current world is still running.
    const bool resetCommittedAngular=selection==m_ActiveSelection &&
        selection.IsAngularScene() && m_Angular.committed.has_value();
    if ((m_ActiveSelection.IsAngularScene() || selection.IsAngularScene()) &&
        m_SimulationState==Rbs::SimulationState::Running && !resetCommittedAngular)
    {
        m_SceneDiagnostic="Pause and enter Edit before loading a different angular case.";
        return false;
    }
    std::optional<AngularCandidate> angular;
    const bool ragdoll=selection.scene==Rbs::RbsScenePreset::PassiveRagdoll;
    if(selection!=m_ActiveSelection && (ragdoll || m_Ragdoll.committed) &&
       m_SimulationState==Rbs::SimulationState::Running)
    {m_SceneDiagnostic="Pause before changing the ragdoll scene.";return false;}
    if (selection.IsAngularScene())
    {
        if (selection==m_ActiveSelection && m_Angular.committed)
            angular=AngularCandidate{*m_Angular.committed,m_SceneGeneration,m_Angular.revision,false};
        else
        {
            const auto variant=m_PendingAngularVariant==Rbs::AngularVariant::Contact &&
                selection.scene!=Rbs::RbsScenePreset::HingeLimited && selection.scene!=Rbs::RbsScenePreset::CvLimited
                ?Rbs::AngularVariant::Air:m_PendingAngularVariant;
            auto reference=Rbs::ReferenceAngularPacket(selection.scene,variant);
            if(!reference){m_SceneDiagnostic=std::string(reference.error().message);return false;}
            angular=AngularCandidate{*reference,m_SceneGeneration,m_Angular.revision,false};
        }
    }
    m_PendingSelection = selection;
    m_AngularRequest=std::move(angular);
    if(ragdoll)
        m_RagdollRequest=RagdollCandidate{selection==m_ActiveSelection && m_Ragdoll.committed ?
            *m_Ragdoll.committed : Rbs::ReferenceRagdollPacket(),m_SceneGeneration,m_Ragdoll.revision,false};
    else m_RagdollRequest.reset();
    m_SceneRequest = selection;
    return true;
}

bool RigidBodySimulationApp::RequestAngularVariant(Rbs::AngularVariant variant)
{
    if(!m_ActiveSelection.IsAngularScene() || !m_Angular.committed || m_Angular.dirty ||
        m_SceneDefaultsPending || HasPendingObjectProperties() ||
        m_SceneRequest || m_SimulationState==Rbs::SimulationState::Running ||
        m_SimulationState==Rbs::SimulationState::Switching || m_EditorMode!=EditorMode::Edit)
    {m_SceneDiagnostic="Pause/Edit and resolve any draft before changing angular variant.";return false;}
    if(variant==m_Angular.committed->variant)return false;
    auto packet=Rbs::ReferenceAngularPacket(m_ActiveSelection.scene,variant);
    if(!packet){m_SceneDiagnostic=std::string(packet.error().message);return false;}
    m_AngularRequest=AngularCandidate{*packet,m_SceneGeneration,m_Angular.revision,false};
    m_SceneRequest=m_ActiveSelection;
    m_SceneDiagnostic="The selected variant will load with its default parameters at tick 0.";
    return true;
}

bool RigidBodySimulationApp::RequestAngularApply()
{
    if(!m_ActiveScene || !m_ActiveSelection.IsAngularScene() ||
        !m_Angular.draft || !m_Angular.committed || (!m_Angular.dirty && !m_SceneDefaultsPending) ||
        HasPendingObjectProperties() || m_SceneRequest ||
        m_SimulationState==Rbs::SimulationState::Running ||
        m_SimulationState==Rbs::SimulationState::Switching ||
        m_SimulationState==Rbs::SimulationState::Failed || m_EditorMode!=EditorMode::Edit ||
        m_Angular.draftGeneration!=m_SceneGeneration || m_Angular.draftBaseRevision!=m_Angular.revision)
    {m_Angular.diagnostic="Apply rejected: enter Edit/Ready, retain the current generation and resolve pending requests.";return false;}
    if(auto valid=Rbs::ValidateAngularPacket(*m_Angular.draft,m_Backend);!valid)
    {m_Angular.diagnostic=std::string(valid.error().message);return false;}
    if (m_Angular.draft->joint.motor && !m_SceneDefaultsPending) {
        auto structural = *m_Angular.draft;
        structural.joint.motor = m_Angular.committed->joint.motor;
        if (structural == *m_Angular.committed) {
            m_PendingMotor = MotorCommand{m_AngularBindings.joint,*m_Angular.draft->joint.motor,
                                         m_SceneGeneration,m_Angular.revision};
            m_Angular.diagnostic="Drive queued for the next simulation boundary.";
            return true;
        }
    }
    m_AngularRequest=AngularCandidate{*m_Angular.draft,m_SceneGeneration,m_Angular.revision,true};
    m_SceneRequest=m_ActiveSelection;
    m_Angular.diagnostic="Apply queued; the full candidate will rebuild at Tick 0.";
    return true;
}

void RigidBodySimulationApp::RevertAngularDraft()
{
    if(!m_Angular.committed || m_SceneRequest)return;
    m_SceneDefaultsPending=false;
    m_PendingMotor.reset();
    m_Angular.draft=m_Angular.committed;
    m_Angular.draftBaseRevision=m_Angular.revision;
    m_Angular.draftGeneration=m_SceneGeneration;
    m_Angular.dirty=false;
    Rbs::Editor::Numeric::Reset();
    m_Angular.diagnostic="Pending values reverted; the applied world is unchanged.";
}

void RigidBodySimulationApp::RestoreAngularDefaults()
{
    if(!m_Angular.committed || m_SceneRequest || HasPendingObjectProperties() ||
       m_EditorMode!=EditorMode::Edit || (m_SimulationState!=Rbs::SimulationState::Ready &&
       m_SimulationState!=Rbs::SimulationState::Paused))return;
    auto reference=Rbs::ReferenceAngularPacket(m_ActiveSelection.scene,m_Angular.committed->variant);
    if(!reference){m_Angular.diagnostic=std::string(reference.error().message);return;}
    m_Angular.draft=*reference;
    m_SceneDefaultsPending=true;
    m_Angular.draftGeneration=m_SceneGeneration;
    m_Angular.draftBaseRevision=m_Angular.revision;
    m_Angular.dirty=true;
    Rbs::Editor::Numeric::Reset();
    m_Angular.diagnostic="Default parameters staged. Select Apply to update the simulation.";
}

bool RigidBodySimulationApp::RequestRagdollApply()
{
    if(!m_ActiveScene || !m_Ragdoll.draft || !m_Ragdoll.committed ||
       (!m_Ragdoll.dirty && !m_SceneDefaultsPending) || m_SceneRequest ||
       HasPendingObjectProperties(false) || m_EditorMode!=EditorMode::Edit ||
       (m_SimulationState!=Rbs::SimulationState::Ready && m_SimulationState!=Rbs::SimulationState::Paused) ||
       m_Ragdoll.draftGeneration!=m_SceneGeneration || m_Ragdoll.draftBaseRevision!=m_Ragdoll.revision)
    {m_Ragdoll.diagnostic="Pause in Edit mode and resolve pending changes before Apply.";return false;}
    if(auto valid=Rbs::ValidateRagdollPacket(*m_Ragdoll.draft,m_Backend);!valid)
    {m_Ragdoll.diagnostic=valid.error().message;return false;}
    m_RagdollRequest=RagdollCandidate{*m_Ragdoll.draft,m_SceneGeneration,m_Ragdoll.revision,true};
    m_SceneRequest=m_ActiveSelection;
    return true;
}

void RigidBodySimulationApp::RevertRagdollDraft()
{
    if(!m_Ragdoll.committed || m_SceneRequest)return;
    m_Ragdoll.draft=m_Ragdoll.committed;
    m_Ragdoll.draftBaseRevision=m_Ragdoll.revision;
    m_Ragdoll.draftGeneration=m_SceneGeneration;
    m_Ragdoll.dirty=false;
    m_SceneDefaultsPending=false;
    Rbs::Editor::Numeric::Reset();
    m_Ragdoll.diagnostic="Pending edits reverted.";
}

void RigidBodySimulationApp::RestoreRagdollDefaults()
{
    if(!m_Ragdoll.committed || m_SceneRequest || HasPendingObjectProperties(false) ||
       m_EditorMode!=EditorMode::Edit || (m_SimulationState!=Rbs::SimulationState::Ready &&
       m_SimulationState!=Rbs::SimulationState::Paused))return;
    m_Ragdoll.draft=Rbs::ReferenceRagdollPacket();
    m_Ragdoll.draftGeneration=m_SceneGeneration;
    m_Ragdoll.draftBaseRevision=m_Ragdoll.revision;
    m_Ragdoll.dirty=m_SceneDefaultsPending=true;
    Rbs::Editor::Numeric::Reset();
    m_Ragdoll.diagnostic="Scene defaults staged for Apply.";
}

bool RigidBodySimulationApp::HasPendingObjectProperties(bool includeRagdoll) const
{
    return (includeRagdoll && m_Ragdoll.dirty) || m_CameraPilot.has_value() || (m_ContextEditor.generation==m_SceneGeneration && (m_ContextEditor.cameraDirty ||
        m_ContextEditor.lightDirty || m_ContextEditor.materialDirty || m_ContextEditor.meshDirty ||
        m_ContextEditor.visibilityDirty || m_ContextEditor.queued ||
        m_SkyVisibilityEdit.has_value())) ||
        (m_BodyPropertyEditor.generation==m_SceneGeneration && (m_BodyPropertyEditor.dirty || m_BodyPropertyEditor.queued)) ||
        (m_BulkBodyEditor.generation==m_SceneGeneration && m_BulkBodyEditor.selectionRevision==m_SelectionRevision &&
         (m_BulkBodyEditor.fields || m_BulkBodyEditor.queued)) || HasPendingColliderDraft() ||
        m_GridVisibilityEdit.has_value() || m_TransformEdit.has_value() || m_VelocityEdit.has_value() ||
        (m_GroupTransform.generation==m_SceneGeneration &&
         (m_GroupTransform.released || m_GroupTransform.pending!=Rbs::Editor::TransformSession::Action::None));
}

void RigidBodySimulationApp::StageSceneDefaults()
{
    if(m_Ragdoll.committed){RestoreRagdollDefaults();return;}
    if(!SandboxEnabled() || !m_ActiveScene || m_SceneRequest || HasPendingObjectProperties() ||
       m_EditorMode!=EditorMode::Edit || (m_SimulationState!=Rbs::SimulationState::Ready &&
       m_SimulationState!=Rbs::SimulationState::Paused))return;
    if(m_ActiveSelection.IsAngularScene()){RestoreAngularDefaults();return;}
    m_SceneDefaultsPending=true;
    m_SceneDiagnostic="Scene defaults staged. Apply to replace the committed scene.";
}

bool RigidBodySimulationApp::ApplySceneDefaults()
{
    if(m_Ragdoll.committed)return RequestRagdollApply();
    if(!m_SceneDefaultsPending || !m_ActiveScene || m_SceneRequest || HasPendingObjectProperties() ||
       m_EditorMode!=EditorMode::Edit || (m_SimulationState!=Rbs::SimulationState::Ready &&
       m_SimulationState!=Rbs::SimulationState::Paused))return false;
    if(m_ActiveSelection.IsAngularScene())return RequestAngularApply();
    m_PendingSelection=m_ActiveSelection;
    m_SceneRequest=m_ActiveSelection;
    return true;
}

void RigidBodySimulationApp::RevertSceneDefaults()
{
    if(m_Ragdoll.committed){RevertRagdollDraft();return;}
    if(m_SceneRequest)return;
    if(m_ActiveSelection.IsAngularScene()){RevertAngularDraft();return;}
    m_SceneDefaultsPending=false;
    m_SceneDiagnostic="Staged scene defaults reverted.";
}

namespace
{
    auto BackendAuthoringFailure(std::string_view message)
    {return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"RBS backend authoring",message});}
}

std::expected<RigidBodySimulationApp::BackendAuthoring,SceneError>
RigidBodySimulationApp::BackendAuthoring::Capture(_Scene& source,RigidBodySimulationApp* app)
{
    auto* physics=source.GetPhysicsSystem();auto* world=physics->GetPhysicsWorld();
    if(!world)return BackendAuthoringFailure("Current authored world is unavailable");
    BackendAuthoring result;
    result.m_Ordinary=app!=nullptr;
    result.m_PacketOwnsJoint=app && app->m_ActiveSelection.IsAngularScene();
    const bool ragdoll=app && app->m_ActiveSelection.scene==Rbs::RbsScenePreset::PassiveRagdoll;
    result.m_PacketBodyCount=ragdoll?6u:result.m_PacketOwnsJoint?2u:0u;
    result.m_PacketOwnsJoint=result.m_PacketBodyCount!=0;
    const auto packetEndpoint=[&](entt::entity id){
        if(ragdoll)return std::ranges::find(app->m_RagdollBindings.entities,id)!=app->m_RagdollBindings.entities.end();
        return result.m_PacketOwnsJoint && (id==app->m_AngularBindings.entities[0] || id==app->m_AngularBindings.entities[1]);
    };
    if(app)
    {
        const auto active=source.ActiveSceneCamera();
        if(!active)return BackendAuthoringFailure("Active camera selection could not be read");
        for(auto id:source.GetAllEntitiesWith<Rbs::Editor::PresetIdentity>())
        {
            _Entity entity{id,&source};
            Object object;
            object.source=id;
            const auto& identity=entity.GetComponent<Rbs::Editor::PresetIdentity>();
            object.key=identity.key;
            object.signature=identity.signature;
            if(!object.key || object.signature.empty())
                return BackendAuthoringFailure("Scene object lacks its frozen preset identity");
            object.endpoint=packetEndpoint(id);
            object.sphere=entity.HasAllComponents<SphereFixture3DComponent>();
            if(entity!=app->m_SkyBoxEntity && entity.HasAllComponents<Transform3DComponent>() && !entity.HasAllComponents<RigidBody3DComponent>())
            {
                const auto& t=entity.Transform();
                object.localPose=SceneBodyPose{t.Translation,t.QuatRotation};
                object.scale=t.Scale;
            }
            if(entity.HasAllComponents<VisibilityComponent>())object.visibility=entity.GetComponent<VisibilityComponent>();
            if(entity.HasAllComponents<RenderCameraComponent>())
            {
                object.camera=entity.GetComponent<RenderCameraComponent>();
                object.activeCamera=*active && source.RenderData().Identify(entity).value_or(EntityRenderId{})==**active;
            }
            if(entity.HasAllComponents<RenderLightComponent>())object.light=entity.GetComponent<RenderLightComponent>();
            if(entity.HasAllComponents<MeshRendererComponent>())
            {
                const auto& mesh=entity.GetComponent<MeshRendererComponent>();
                object.meshFlags={{mesh.castShadows,mesh.receiveShadows,mesh.pickable}};
                if(auto desc=app->m_SceneResources->DescribeMaterial(mesh.material))
                {
                    desc->textures={}; // The new registry supplies its own bindings.
                    object.material=std::move(*desc);
                }
            }
            result.m_Objects.push_back(std::move(object));
        }
    }
    result.m_Gravity=world->GetGravity();result.m_Step=source.GetPhysicsStep();
    result.m_Sleeping=physics->IsSleepingEnabled();result.m_Bodies.reserve(world->GetBodyCount());
    if(world->GetBackend()==PhysicsBackend::Jolt)
    {
        auto settings=world->ReadWorldSleepSettings();
        if(!settings)return BackendAuthoringFailure("World sleep settings readback failed");
        result.m_WorldSleep=*settings;
        if(const auto settings=world->ReadJoltSettings())result.m_JoltWorld=*settings;
    }
    std::size_t livePacketBodies=0;
    for(auto id:source.GetAllEntitiesWith<RigidBody3DComponent,Transform3DComponent,TagComponent>())
    {
        if(packetEndpoint(id)){++livePacketBodies;continue;}
        _Entity entity{id,&source};auto pose=source.ReadBodyTransform(entity);
        if(!pose)return BackendAuthoringFailure("Authored body binding is invalid");
        auto state=app?world->ReadEffectiveBody(pose->body):world->ReadBody(pose->body);
        if(!state)return BackendAuthoringFailure("Current body state is invalid");
        Body body;const auto& tag=entity.GetComponent<TagComponent>();body.m_Name=tag.NameBeforeEditorRename.empty()?tag.Name:tag.NameBeforeEditorRename;body.m_Old=pose->body;body.m_Pose=pose->authored;
        // Preset construction is deterministic; display names need not be unique.
        // Bind the replacement Scene explicitly and validate the name/membership below.
        // Removed/recycled entities fail mapping instead of aliasing a new Physics handle.
        body.m_FixtureEntity=id;
        body.m_Shape=state->shapeType;
        body.m_LocalMin=state->localBounds.mins;
        body.m_LocalMax=state->localBounds.maxs;
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
    if(result.m_Bodies.size()+livePacketBodies!=world->GetBodyCount())
        return BackendAuthoringFailure("Every rebuilt body requires authored Scene membership");
    if(result.m_PacketOwnsJoint)return result;
    result.m_Joints.reserve(world->GetJointCount());
    for(std::size_t i=0;i<world->GetJointCount();++i)
    {
        auto handle=world->JointAt(i);if(!handle)return BackendAuthoringFailure("Invalid joint identity");
        auto state=world->ReadJoint(*handle);if(!state)return BackendAuthoringFailure("Invalid joint state");
        const auto find=[&](PhysicsBodyHandle h){return std::find_if(result.m_Bodies.begin(),result.m_Bodies.end(),[&](const auto& b){return b.m_Old==h;});};
        auto a=find(state->descriptor.bodyA),b=find(state->descriptor.bodyB);
        if(a==result.m_Bodies.end() || b==result.m_Bodies.end())return BackendAuthoringFailure("Joint endpoint has no authored body");
        result.m_Joints.push_back({state->descriptor,a->m_FixtureEntity,b->m_FixtureEntity});
    }
    return result;
}

entt::entity RigidBodySimulationApp::BackendAuthoring::Rebound(entt::entity source) const
{
    if(!m_Ordinary)return source; // Standalone diagnostics construct paired registries explicitly.
    const auto found=m_Rebound.find(source);
    return found==m_Rebound.end()?entt::null:found->second;
}

ApplicationInitializationResult RigidBodySimulationApp::BackendAuthoring::ApplyBeforeStart(_Scene& target) const
{
    if(m_Ordinary)
    {
        std::map<std::uint64_t,entt::entity> byKey;
        for(auto id:target.GetAllEntitiesWith<Rbs::Editor::PresetIdentity>())
        {
            const auto key=_Entity{id,&target}.GetComponent<Rbs::Editor::PresetIdentity>().key;
            if(!byKey.emplace(key,id).second)return BackendAuthoringFailure("Duplicate candidate preset identity");
        }
        m_Rebound.clear();
        for(const auto& object:m_Objects)
        {
            auto found=byKey.find(object.key);
            // Some render-only gallery objects are created after Physics startup.
            // Every body must map below; RestoreObjects binds and verifies the complete preset.
            if(found==byKey.end())continue;
            _Entity entity{found->second,&target};
            if(!object.endpoint && entity.GetComponent<Rbs::Editor::PresetIdentity>().signature!=object.signature)
                return BackendAuthoringFailure("Replacement preset object signature differs");
            m_Rebound.emplace(object.source,found->second);
        }
    }
    std::size_t count=0;
    for(auto id:target.GetAllEntitiesWith<RigidBody3DComponent>()){(void)id;++count;}
    if(count!=m_Bodies.size()+m_PacketBodyCount || !target.SetPhysicsStep(m_Step))
        return BackendAuthoringFailure("Generated scene differs from the authored body/clock configuration");
    for(const auto& body:m_Bodies)
    {
        _Entity entity{Rebound(body.m_FixtureEntity),&target};
        if(!entity || !entity.HasAllComponents<RigidBody3DComponent,TagComponent>() || (!m_Ordinary && entity.GetName()!=body.m_Name) ||
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
    if(!world || world->GetBodyCount()!=m_Bodies.size()+m_PacketBodyCount)return BackendAuthoringFailure("Replacement body membership differs");
    if(!m_PacketOwnsJoint)world->SetGravity(m_Gravity);
    physics->SetSleepingEnabled(m_Sleeping);
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
        _Entity entity{Rebound(body.m_FixtureEntity),&target};auto handle=target.GetBodyBinding(entity);
        if(!handle || !physics->ReplaceBodyCollider(*handle,body.m_Collider) ||
            !world->UpdateBodyProperties(*handle,body.m_Properties))
            return BackendAuthoringFailure("Backend cannot represent the applied collider/body settings; revert unsupported settings before switching");
        if(m_Ordinary)
        {
            const auto effective=world->ReadEffectiveBody(*handle);
            const auto properties=world->ReadBodyProperties(*handle);
            auto expectedShape=body.m_Shape;
            if(const auto* capsule=std::get_if<CapsuleShapeDesc>(&body.m_Collider);capsule && capsule->halfLength==0.f)
                expectedShape=world->GetBackend()==PhysicsBackend::Jolt?ShapeType::Sphere:ShapeType::Capsule;
            if(!effective || !properties || effective->shapeType!=expectedShape ||
               glm::length(effective->localBounds.mins-body.m_LocalMin)>1.e-4f ||
               glm::length(effective->localBounds.maxs-body.m_LocalMax)>1.e-4f ||
               glm::length(effective->position-body.m_Pose.translation)>1.e-4f ||
               std::abs(glm::dot(effective->orientation,body.m_Pose.rotation))<1.f-1.e-5f ||
               *properties!=body.m_Properties)
                return BackendAuthoringFailure("Committed body pose, shape or properties differ on provider readback");
        }
    }
    if(m_PacketOwnsJoint)return {};
    while(world->GetJointCount())
    {
        auto handle=world->JointAt(0);
        if(!handle || !world->DestroyJoint(*handle))return BackendAuthoringFailure("Candidate joint retirement failed");
    }
    for(const auto& joint:m_Joints)
    {
        auto a=target.GetBodyBinding(_Entity{Rebound(joint.m_A),&target});
        auto b=target.GetBodyBinding(_Entity{Rebound(joint.m_B),&target});
        if(!a || !b)return BackendAuthoringFailure("Replacement joint endpoint is unavailable");
        auto desc=joint.m_Desc;desc.bodyA=*a;desc.bodyB=*b;
        if(!world->CreateJoint(desc))return BackendAuthoringFailure("Replacement distance joint was rejected");
    }
    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::BackendAuthoring::RestoreObjects(RigidBodySimulationApp& app) const
{
    auto& scene=*app.m_ActiveScene;
    auto& render=scene.RenderData();
    std::map<std::uint64_t,entt::entity> byKey;
    for(auto id:scene.GetAllEntitiesWith<Rbs::Editor::PresetIdentity>())
        if(!byKey.emplace(_Entity{id,&scene}.GetComponent<Rbs::Editor::PresetIdentity>().key,id).second)
            return BackendAuthoringFailure("Duplicate completed preset identity");
    std::optional<EntityRenderId> activeCamera;
    for(const auto& object:m_Objects)
    {
        const auto bound=byKey.find(object.key);
        if(bound==byKey.end())return BackendAuthoringFailure("A committed object is absent from the completed preset");
        _Entity entity{bound->second,&scene};
        if(!object.endpoint && object.signature!=entity.GetComponent<Rbs::Editor::PresetIdentity>().signature)
            return BackendAuthoringFailure("Completed preset object signature differs");
        if(object.localPose && (!scene.SetLocalPose(entity,object.localPose->translation,object.localPose->rotation) ||
           !scene.SetLocalScale(entity,object.scale)))return BackendAuthoringFailure("Local object transform could not be restored");
        const auto id=render.Identify(entity);
        if(!id)return BackendAuthoringFailure("Replacement object has no render identity");
        if(object.visibility)
        {
            auto applied=entity.HasAllComponents<VisibilityComponent>()?render.Replace(*id,*object.visibility):render.Add(*id,*object.visibility);
            if(!applied)return BackendAuthoringFailure("Object visibility could not be restored");
        }
        if(object.camera && !render.Replace(*id,*object.camera))return BackendAuthoringFailure("Camera parameters could not be restored");
        if(object.activeCamera)activeCamera=*id;
        if(object.light)
        {
            if(object.light->kind==RenderLightKind::Spot && object.light->castShadows &&
               (!app.m_FrameSubmission || !app.m_FrameSubmission->PrepareSpotShadowTarget(object.light->spotShadow)))
                return BackendAuthoringFailure("Unable to prepare committed spot shadow");
            if(!render.Replace(*id,*object.light))return BackendAuthoringFailure("Light parameters could not be restored");
        }
        if(object.meshFlags)
        {
            if(!entity.HasAllComponents<MeshRendererComponent>())return BackendAuthoringFailure("Missing replacement mesh component");
            auto mesh=entity.GetComponent<MeshRendererComponent>();
            auto flags=*object.meshFlags;
            mesh.castShadows=flags[0];mesh.receiveShadows=flags[1];mesh.pickable=flags[2];
            if(mesh!=entity.GetComponent<MeshRendererComponent>() && !render.Replace(*id,mesh))
                return BackendAuthoringFailure("Mesh visibility and shadow flags could not be restored");
            // Angular anchor shapes select different surfaces. Ragdoll shapes all
            // carry the same bone's authored rust surface through reconstruction.
            if(object.material && (!object.endpoint || app.m_ActiveSelection.IsRagdollScene() ||
                                   entity.HasAllComponents<SphereFixture3DComponent>()==object.sphere))
            {
                auto current=app.m_SceneResources->DescribeMaterial(mesh.material);
                if(!current)return BackendAuthoringFailure("Could not read replacement material");
                auto restored=*object.material;
                restored.textures=current->textures;
                if(restored!=*current && !app.m_SceneResources->EditMaterial(mesh.material,restored))
                    return BackendAuthoringFailure("Could not restore committed material");
            }
        }
    }
    if(activeCamera)
    {
        if(!app.SelectSceneCamera(*activeCamera))return BackendAuthoringFailure("Active scene camera could not be restored");
    }
    else if(!scene.ClearActiveSceneCamera())return BackendAuthoringFailure("Active camera selection could not be cleared");
    return {};
}

bool RigidBodySimulationApp::RequestBackend(PhysicsBackend backend)
{
    if(!SandboxEnabled() || !m_ActiveScene || !PhysicsBackendAvailable(backend))
    {m_SceneDiagnostic="Backend unavailable; current scene retained.";return false;}
    if(backend==m_Backend)
    {m_SceneDiagnostic="Selected backend is already active.";return false;}
    if((m_ActiveSelection.IsAngularScene() || m_Ragdoll.committed) && m_SimulationState==Rbs::SimulationState::Running)
    {m_SceneDiagnostic="Pause before changing the angular scene backend.";return false;}
    if(!RequestScene(m_ActiveSelection))return false;
    m_PendingBackend=backend;
    m_BackendRequest=backend;
    m_SceneDiagnostic="Backend Apply queued for the next Update boundary.";
    return true;
}

void RigidBodySimulationApp::ApplyPendingMotor()
{
    const auto command = *std::exchange(m_PendingMotor,std::nullopt);
    if (!m_ActiveScene || m_SceneRequest || !m_Angular.committed || !m_Angular.draft ||
        command.generation != m_SceneGeneration || command.revision != m_Angular.revision ||
        command.joint != m_AngularBindings.joint) {
        m_Angular.diagnostic="Drive request expired when the scene or joint changed.";
        return;
    }
    auto* system = m_ActiveScene->GetPhysicsSystem();
    auto* world = system ? system->GetPhysicsWorld() : nullptr;
    if (!world || !world->SetHingeMotor(command.joint,command.settings)) {
        m_Angular.diagnostic="The physics provider rejected the drive command.";
        return;
    }
    const auto state = world->ReadJoint(command.joint);
    if (!state || state->descriptor.angular.motor != command.settings) {
        FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","HingeMotor","Apply",
                     "ReadJoint","Provider drive readback disagrees with the applied command"});
        return;
    }
    m_Angular.committed->joint.motor = state->descriptor.angular.motor;
    m_Angular.draftBaseRevision = ++m_Angular.revision;
    m_Angular.dirty = *m_Angular.draft != *m_Angular.committed;
    m_Angular.diagnostic="Drive applied and verified on the current hinge.";
}

void RigidBodySimulationApp::ProcessSceneRequest()
{
    if (m_PendingMotor) ApplyPendingMotor();
    if (!SandboxEnabled() || !m_SceneRequest) return;
    const auto requested = *std::exchange(m_SceneRequest, std::nullopt);
    const auto angularCandidate=std::exchange(m_AngularRequest,std::nullopt);
    const auto ragdollCandidate=std::exchange(m_RagdollRequest,std::nullopt);
    const auto backendRequest=std::exchange(m_BackendRequest,std::nullopt);
    const auto requestedBackend=backendRequest.value_or(m_Backend);
    const auto oldSelection=m_ActiveSelection;
    if(HasPendingObjectProperties(!(ragdollCandidate && ragdollCandidate->apply)))
    {m_SceneDiagnostic="Apply or Revert pending object edits before reconstructing the current scene.";return;}
    if(requested.scene==Rbs::RbsScenePreset::PassiveRagdoll)
    {
        if(!ragdollCandidate || ragdollCandidate->expectedGeneration!=m_SceneGeneration ||
           ragdollCandidate->expectedRevision!=m_Ragdoll.revision)
        {m_SceneDiagnostic="Ragdoll request is stale or incomplete.";return;}
        if(auto checked=Rbs::ValidateRagdollPacket(ragdollCandidate->packet,requestedBackend);!checked)
        {m_SceneDiagnostic=checked.error().message;m_Ragdoll.diagnostic=m_SceneDiagnostic;return;}
    }
    if(requested.IsAngularScene())
    {
        if(!angularCandidate || angularCandidate->expectedGeneration!=m_SceneGeneration ||
            angularCandidate->expectedRevision!=m_Angular.revision ||
            angularCandidate->packet.scene!=requested.scene)
        {m_SceneDiagnostic="Angular request is stale or incomplete; previous scene retained.";return;}
        if(auto valid=Rbs::ValidateAngularPacket(angularCandidate->packet,requestedBackend,!SandboxEnabled());!valid)
        {m_SceneDiagnostic=std::string(valid.error().message);m_Angular.diagnostic=m_SceneDiagnostic;return;}
    }
    const bool keepViewportActive=m_ViewportActive;
    struct SelectedObject {Rbs::Editor::PresetIdentity identity;bool endpoint;};
    std::vector<SelectedObject> selection;
    for(std::size_t i=0;i<m_EditorSelection.count;++i)
        if(auto e=m_ActiveScene->RenderData().Resolve(m_EditorSelection.ids[i]))
        {
            _Entity entity{*e,m_ActiveScene.get()};
            if(entity.HasAllComponents<Rbs::Editor::PresetIdentity>())
                selection.push_back({entity.GetComponent<Rbs::Editor::PresetIdentity>(),IsAngularEndpoint(entity) || RagdollBoneFor(entity).has_value()});
        }
    std::array<Rbs::Editor::PresetIdentity,2> previousEndpoints{};
    if(oldSelection==requested && requested.IsAngularScene())
        for(std::size_t i=0;i<previousEndpoints.size();++i)
        {
            _Entity entity{m_AngularBindings.entities[i],m_ActiveScene.get()};
            if(entity && entity.HasAllComponents<Rbs::Editor::PresetIdentity>())
                previousEndpoints[i]=entity.GetComponent<Rbs::Editor::PresetIdentity>();
        }
    const auto restoreSelection=[&]() {
        if(selection.empty())return;
        std::map<std::uint64_t,entt::entity> current;
        for(auto e:m_ActiveScene->GetAllEntitiesWith<Rbs::Editor::PresetIdentity>())
            current.emplace(_Entity{e,m_ActiveScene.get()}.GetComponent<Rbs::Editor::PresetIdentity>().key,e);
        std::vector<EntityRenderId> resolved;
        for(const auto& selected:selection)
        {
            auto found=current.find(selected.identity.key);
            if(found==current.end())continue;
            _Entity entity{found->second,m_ActiveScene.get()};
            if(entity.GetComponent<Rbs::Editor::PresetIdentity>().signature!=selected.identity.signature &&
               !(selected.endpoint && (IsAngularEndpoint(entity) || RagdollBoneFor(entity))))continue;
            if(auto id=m_ActiveScene->RenderData().Identify(entity))resolved.push_back(*id);
        }
        if(!resolved.empty() && m_EditorSelection.SelectSet(m_ActiveScene->RenderData(),resolved,Rbs::Editor::EntitySelection::SetOperation::Replace))
            SynchronizeEditorSelection();
    };
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
            if(requested==oldSelection && !m_SceneDefaultsPending)
            {
                auto captured=BackendAuthoring::Capture(*m_ActiveScene,this);
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
            auto initialized = InitializeScene(authoring?&*authoring:nullptr,
                angularCandidate?&angularCandidate->packet:nullptr,ragdollCandidate?&ragdollCandidate->packet:nullptr);
            if (!initialized) return initialized;
            if(authoring)
            {
                if(!angularCandidate && !ragdollCandidate)
                    if(auto restored=authoring->ApplyAfterStart(*m_ActiveScene);!restored)return restored;
                if(auto restored=authoring->RestoreObjects(*this);!restored)return restored;
            }
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
        restoreSelection();
        SetSimulationState(m_ActiveScene ? Rbs::SimulationState::Paused : Rbs::SimulationState::Failed);
        m_SkipSimulationElapsed = true;
        if(angularCandidate)m_Angular.diagnostic=m_SceneDiagnostic;
        if(ragdollCandidate)m_Ragdoll.diagnostic=m_SceneDiagnostic;
        return;
    }
    previous.Retire();
    m_IsShowDebugBoundingBox = false;
    // The rebuilt component owns committed/default grid visibility. Keep the
    // existing toolbar preference synchronized so Update cannot undo that state.
    if(m_GridEntity && m_GridEntity.HasAllComponents<VisibilityComponent>())
        Rbs::Editor::Snap.visible=m_GridEntity.GetComponent<VisibilityComponent>().enabled;
    m_ShowSelectedColliderOutlines = false;
    ++m_SceneGeneration;
    m_SceneDefaultsPending=false;
    if(oldSelection==requested && requested.IsAngularScene())
        for(std::size_t i=0;i<previousEndpoints.size();++i)
        {
            const auto& old=previousEndpoints[i];
            const auto& now=_Entity{m_AngularBindings.entities[i],m_ActiveScene.get()}.GetComponent<Rbs::Editor::PresetIdentity>();
            const auto saved=Rbs::Editor::EntityPreferences.find({EditorPresetKey(),old.key});
            if(old.key==now.key && old.signature!=now.signature && saved!=Rbs::Editor::EntityPreferences.end() &&
               saved->second.signature==old.signature)
            {saved->second.signature=now.signature;ImGui::MarkIniSettingsDirty();}
        }
    if(angularCandidate)
    {
        if(angularCandidate->apply)++m_Angular.revision;
        else if(requested!=oldSelection || !m_Angular.committed ||
                angularCandidate->packet.variant!=m_Angular.committed->variant)
            m_Angular.revision=0;
        m_Angular.committed=angularCandidate->packet;
        m_Angular.draft=angularCandidate->packet;
        m_Angular.dirty=false;
        m_Angular.draftGeneration=m_SceneGeneration;
        m_Angular.draftBaseRevision=m_Angular.revision;
        m_Angular.interactive=false;
        m_Angular.diagnostic="Applied packet verified by body, joint and world readback.";
        m_PendingAngularVariant=angularCandidate->packet.variant;
    }
    else if(oldSelection.IsAngularScene())m_Angular={};
    if(ragdollCandidate)
    {
        if(requested!=oldSelection)m_Ragdoll.revision=0;
        if(ragdollCandidate->apply)++m_Ragdoll.revision;
        m_Ragdoll.committed=ragdollCandidate->packet;
        m_Ragdoll.draft=m_Ragdoll.committed;
        m_Ragdoll.draftGeneration=m_SceneGeneration;
        m_Ragdoll.draftBaseRevision=m_Ragdoll.revision;
        m_Ragdoll.dirty=false;
        m_Ragdoll.contactDiagnostic.clear();
        m_Ragdoll.diagnostic="Committed ragdoll reconstructed at Tick 0.";
    }
    else m_Ragdoll={};
    if(oldSelection==requested)restoreSelection();
    Rbs::Editor::Numeric::Reset();
    if(backendRequest)
    {
        if(SandboxEnabled())
        {
            Rbs::Editor::SavedBackend=m_Backend;
            ImGui::MarkIniSettingsDirty();
        }
        m_ColliderLayers.contactPoints=m_ColliderLayers.contactNormals=false;
        m_SceneDiagnostic="Backend applied; authored scene rebuilt at Tick 0. Press Start.";
    }
    else m_SceneDiagnostic = "Scene loaded; press Start.";
    m_SkipSimulationElapsed = true;
    m_EditorMode=EditorMode::Edit;
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
#ifdef GENGINE_PHASE29_QUALIFICATION
        const bool observingWorldTeardown=Rbs::Phase29::BeginWorldTeardown(m_ActiveScene);
#endif
        m_ActiveScene.reset();
        m_EditorScene.reset();
#ifdef GENGINE_PHASE29_QUALIFICATION
        if(observingWorldTeardown)Rbs::Phase29::EndWorldTeardown();
#endif
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
        if (SandboxEnabled())
        {
            // Loading presentation may have opened imgui.ini before RBS registered its handler.
            if (const char* ini = ImGui::GetIO().IniFilename; ini && *ini)
                ImGui::LoadIniSettingsFromDisk(ini);
            if (!m_Launch.m_BackendExplicit && Rbs::Editor::StartupBackend)
                m_Backend = m_PendingBackend = *Rbs::Editor::StartupBackend;
            if (!PhysicsBackendAvailable(m_Backend))
                return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                    "Physics backend", "Requested backend unavailable"});
            std::string title = std::string("RigidBodySimulation - ") + Rbs::SceneName(m_Launch.scene);
            if (m_Launch.IsDistanceScene())
                title += " (" + std::to_string(m_Launch.scene==Rbs::RbsScenePreset::DistancePendulum?1:m_Launch.chainLinks)
                    + " links, 60 Hz, " + std::string(PhysicsBackendName(m_Backend)) + ")";
            GetWindow()->SetTitle(title + " - " + std::string(PhysicsBackendName(m_Backend)));
        }
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
        m_ViewportAwaitPointer = true;
        m_ViewportRequestFocus = m_ViewportHighlightPending = true;
#ifdef GENGINE_LIFECYCLE_VALIDATION
        ++m_ViewportActivations;
#endif
        return;
    }
    m_ViewportRequestFocus = m_ViewportHighlightPending = false;
    m_ViewportHighlightStart = -1;
    m_ViewportActivationButtons = 0;
    m_ViewportOwnsSelection = false;
    m_ViewportAwaitPointer = true;
    m_ViewPickRequested = m_OutlineToggleRequested = m_FrameOutlineToggle = false;
    m_FramePickPixel.reset();
    ClearOutlineHover();
    if (m_Marquee.armed || m_Marquee.waiting) CancelViewportMarquee();
    if (m_GizmoUsing) CancelGroupTransform();
    m_GizmoHovered = m_GizmoUsing = false;
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
                                        if(m_CameraPilot && event.kind==InputKind::Cancel) {
                                            auto& p=*m_CameraPilot;auto& nav=p.navigation;
                                            const auto basis=glm::mat3_cast(p.navigationRotation);
                                            nav.m_Position=p.navigationPosition;
                                            nav.m_Pitch=std::atan2(basis[2][1],basis[1][1]);
                                            nav.m_Yaw=std::atan2(basis[0][2],basis[0][0]);
                                            nav.m_PositionDelta={};nav.m_PitchDelta=nav.m_YawDelta=0;
                                            nav.m_FocalPoint=nav.m_Position+nav.GetForwardDirection()*std::max(1.f,nav.GetDistance());
                                        }
                                        if (auto result = NavigationCamera().OnViewInput(event,
                                                eligible && (m_CameraView == CameraView::Editor || m_CameraPilot.has_value())); !result)
                                        {
                                            ReportPlatformError(result.error());
                                            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
                                                "Editor Camera input", "", "owned View gesture", "", result.error().message});
                                        }
                                        if (event.kind != InputKind::Motion && event.kind != InputKind::Wheel)
                                            ImGui_ImplSDL2_SetViewNavigation(GetInputManager()->PointerCapture() == InputLayer::View);
                                    };
                                    if (event.cleanup)
                                    {
                                        const bool activation = m_ViewportActivationButtons != 0;
                                        if (event.kind == InputKind::Cancel)
                                        {
                                            if (const auto* error = GetInputManager()->NativePollFailure())
                                                FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
                                                    "Native input transition", "", error->operation, "", error->message});
                                            m_ViewPickRequested = false;
                                            CancelViewportMarquee();
                                            m_ViewportActivationButtons = 0;
                                            if (event.cancellation == InputCancelReason::FocusLoss ||
                                                event.cancellation == InputCancelReason::Hidden)
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
                                        const bool wasActive = m_ViewportActive;
                                        const bool captured = GetInputManager()->PointerCapture() == InputLayer::View;
                                        delivery.Handle();
                                        if (fresh && inside && !wasActive)
                                        {
                                            SetViewportActive(true);
                                            m_ViewportActivationButtons |= GENGINE_BUTTON(event.button);
                                            if (event.button == GENGINE_BUTTON_LEFT)
                                                ImGui_ImplSDL2_ConsumeViewActivationClick();
                                        }
                                        else if (fresh && m_ViewportActivationButtons)
                                            m_ViewportActivationButtons |= GENGINE_BUTTON(event.button);
                                        const bool eligible = wasActive && !m_ViewportActivationButtons && (inside || captured);
                                        if (eligible && !captured && event.kind == InputKind::Motion && (event.dx || event.dy))
                                            m_ViewportAwaitPointer = false;
                                        if (eligible && !captured && fresh && event.button == GENGINE_BUTTON_LEFT && !m_CameraPilot &&
                                            !(event.modifiers & (LeftAlt | RightAlt)) &&
                                            !GetInputManager()->GetViewState().m_Mouse.isButtonHeld(GENGINE_BUTTON_RIGHT))
                                        {
                                            m_ViewportAwaitPointer = false;
                                            m_ViewSelectionAdd = m_ViewportOwnsSelection && (event.modifiers & (LeftShift | RightShift));
                                            m_OutlineToggleRequested = m_ViewportOwnsSelection && (event.modifiers & (LeftControl | RightControl));
                                            m_ViewPickRequested = !PickViewportIcon(event.x, event.y, m_OutlineToggleRequested);
                                            if (m_ViewPickRequested) m_ViewClickPoint = glm::vec2{event.x, event.y};
                                        }
                                        if (fresh && event.button == GENGINE_BUTTON_RIGHT)
                                        {
                                            m_ViewPickRequested = false;
                                            ClearOutlineHover();
                                            if (m_Marquee.armed || m_Marquee.waiting) CancelViewportMarquee();
                                        }
                                        if ((event.modifiers & LeftAlt) &&
                                            GetInputManager()->GetViewState().m_Mouse.m_CurrentButtons)
                                        {
                                            m_ViewportAltGestureConsumed = true;
                                            m_ViewPickRequested = false;
                                        }
                                        // A second button cannot extend an already-owned camera gesture.
                                        cameraInput(eligible && !(captured && fresh));
                                        if (eligible && event.kind == InputKind::Wheel && (m_CameraView == CameraView::Editor || m_CameraPilot))
                                            NavigationCamera().OnMouseScroll(event.wheelY);
                                        return;
                                    }
                                    if (event.kind != InputKind::KeyDown)
                                        return;
                                    if (GetInputManager()->PointerCapture() == InputLayer::View)
                                    {
                                        delivery.Handle();
                                        if (event.key == GENGINE_KEY_ESCAPE && !event.repeated)
                                        {
                                            if (!CancelCameraGesture())
                                                FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Viewport input", "",
                                                    "cancel camera navigation", "", m_CameraDiagnostic});
                                        }
                                        else cameraInput(true);
                                        return;
                                    }
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
                                            if(m_CameraPilot)m_CameraPilot->finish=2;
                                            else if(m_Marquee.armed || m_Marquee.waiting)CancelViewportMarquee();
                                            else if(m_GroupTransform.dirty){m_GroupTransform.released=false;m_GroupTransform.pending=Rbs::Editor::TransformSession::Action::Cancel;}
                                            else m_ColliderEscapeRequested = true;
                                        }
                                        return;
                                    case GENGINE_KEY_Z:
                                    case GENGINE_KEY_Y:
                                        if(m_ViewportActive && !m_CameraPilot && !event.repeated && (event.modifiers&(LeftControl|RightControl)))
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
                                        !event.repeated && (m_CameraView == CameraView::Editor || m_CameraPilot))
                                        NavigationCamera().Initialize(*direction);
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
            const auto& route = GetInputManager()->Routing();
            // View-owned toolbar and Gizmo controls use UI delivery inside the image.
            if (!route.visible || event.x < route.left || event.x >= route.right ||
                event.y < route.top || event.y >= route.bottom || !m_ViewportHovered)
                SetViewportActive(false);
            else if (!m_ViewportActive && event.button == GENGINE_BUTTON_LEFT &&
                     !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            {
                // The image toolbar also gets an activation-only first press.
                SetViewportActive(true);
                ImGui_ImplSDL2_ConsumeViewActivationClick();
            }
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
