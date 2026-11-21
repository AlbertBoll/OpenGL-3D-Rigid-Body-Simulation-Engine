#include "RigidBodySimulation.h"
#include "RbsComponentEditing.h"
#include "RbsNumericInput.h"
#include "../tests/OwnerReviewDriver.h"
#include "../tests/BackendSwitchChecks.h"
#include "../tests/JoltParameterExperiment.h"
#include "../tests/JoltFinalAudit.h"
#include "../tests/BallSocketJointChecks.h"
#ifdef GENGINE_PHASE29_QUALIFICATION
#include "../tests/PassiveRagdollChecks.h"
#if defined(GENGINE_PHASE29_STEP_SAFETY)
#pragma comment(linker,"/include:g_Phase29StepSafetyFixture")
extern "C" const std::uint64_t g_Phase29StepSafetyFixture[] = {1,8,sizeof(GEngine::PhysicsWorld),
    sizeof(GEngine::PhysicsSystem),sizeof(GEngine::_Scene),sizeof(GEngine::PhysicsBodyError),sizeof(GEngine::PhysicsStepFailure)};
extern "C" bool Phase29ReserveSafetyAttempt() noexcept
{
    if (!Rbs::Phase29::safetyArmed || Rbs::Phase29::safetyAttempts>=8) return false;
    ++Rbs::Phase29::safetyAttempts;
    return true;
}
#endif
#include <Jolt/Jolt.h>
#include <Jolt/Physics/IslandBuilder.h>
#include <Jolt/Physics/PhysicsUpdateContext.h>
#include <Jolt/Physics/Constraints/ContactConstraintManager.h>
#pragma comment(linker,"/include:g_Phase29CaptureConfiguration")
extern "C" const std::uint64_t g_Phase29CaptureConfiguration[] = {
    sizeof(void*),sizeof(JPH::uint32),JPH_RVECTOR_ALIGNMENT,sizeof(JPH::PhysicsUpdateContext::Step),
    sizeof(JPH::PhysicsUpdateContext::Step::CCDBody),JPH::ContactConstraintManager::cMaxConstraintSize,
    sizeof(JPH::BodyPair),sizeof(JPH::BodyID),sizeof(GEngine::PhysicsJoltContactSample),
#ifdef JPH_VALIDATE_ISLAND_BUILDER
    1,
#else
    0,
#endif
#ifdef JPH_TRACK_SIMULATION_STATS
    1,
#else
    0,
#endif
    sizeof(GEngine::PhysicsWorld),sizeof(GEngine::PhysicsSystem),sizeof(GEngine::_Scene),
    sizeof(GEngine::PhysicsBodyError),sizeof(GEngine::PhysicsStepFailure)
};
#elif defined(GENGINE_PHASE28_QUALIFICATION)
#include "../tests/AngularJointChecks.h"
#endif
#include "../tests/ImpulseEditorChecks.h"
#ifdef GENGINE_PHASE24_VALIDATION
namespace PreEditorValidation { void ObservePhase24Frame(const ::GEngine::RenderFrame&); }
#endif
#ifdef GENGINE_PHASE24_OUTLINE_QUALIFICATION
namespace GEngine::Phase24Qualification { bool Functional() noexcept; void Observe(const RenderFrame&) noexcept; }
#endif
#ifdef GENGINE_PHASE24_COLLIDER_VALIDATION
namespace PreEditorValidation::ColliderReplacement { void ObserveColliderRuntime(const ::GEngine::RenderFrame&); }
#endif
#include "Core/Log.h"
#include "Core/Window.h"
#include "Scene/_Scene.h"
#include "Physics/PhysicsSystem.h"
#ifdef GENGINE_PHASE25_QUALIFICATION
#include "../tests/PhysicsCostChecks.h"
#endif
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
#include "../tests/ColliderReplacementObservation.h"
#include "../tests/ColliderReplacementCapture.h"
#include "../tests/ColliderReplacementReplay.h"
#endif
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
#include "../tests/ColliderPassiveObservation.h"
#endif
#include <new>
#include <format>
#include <chrono>

#include "Managers/AssetsManager.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <sdl2/SDL.h>
#include <cmath>
#include <print>
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
#include "../tests/SandboxChecks.h"
#endif

namespace Rbs
{
#ifdef GENGINE_MESH_HIGHLIGHT_AUDIT
    void AuditHighlightDescriptor(::GEngine::FrameSubmissionDesc&);
    void AuditHighlightFrame(const ::GEngine::RenderFrame&);
#endif
#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
    unsigned MeshHighlightStage(); void MeshHighlightDescriptor(::GEngine::FrameSubmission&, ::GEngine::FrameSubmissionDesc&);
#endif
#ifdef GENGINE_PHASE29_QUALIFICATION
    void Phase28AngularWidget(RigidBodySimulationApp&,const char*) {}
    void Phase28ComponentWidget(const char*) {}
    bool Phase28AngularOwnsActions() {return false;}
    float Phase28OutlineViewportScale() {return 1.f;}
#elif defined(GENGINE_PHASE28_QUALIFICATION)
    void Phase28AngularWidget(RigidBodySimulationApp& app,const char* key)
    { AngularJointChecks::Widget(app,key); }
    void Phase28ComponentWidget(const char* key)
    { AngularJointChecks::WidgetKey(key); }
    bool Phase28AngularOwnsActions()
    { return AngularJointChecks::OwnsActions(); }
    float Phase28OutlineViewportScale()
    { return AngularJointChecks::OutlineViewportScale(); }
#endif
    void ResetColliderEditor(::GEngine::_Scene&, bool);
    void ProcessColliderEditorCommands(::GEngine::_Scene&, const ::GEngine::_Entity&, bool&);
    bool ColliderEditorTemporarilyPaused(::GEngine::_Scene&, const ::GEngine::_Entity&, bool);
    ::GEngine::DebugGeometryResult AppendColliderEditorPreviews(::GEngine::_Scene&,
        const ColliderOutlineSelection&, ::GEngine::DebugGeometry&);
}

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;
#ifdef GENGINE_LIFECYCLE_VALIDATION
namespace PreEditorLifecycle { bool GpuChecks(); }
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
#include <array>
namespace PreEditorLifecycle
{
    void PickTraceValues(const char*, std::int64_t = -1, const std::array<std::uint64_t, 12>& = {},
                         const std::array<double, 12>& = {}) noexcept;
    void FinishPickTrace(const char*) noexcept;
}
#endif
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
namespace PreEditorInput
{
    void RecordInputPicking(bool);
}
#endif

namespace
{
    std::optional<glm::dvec2> ScenePointerLogical(const Window& window,double x,double y)
    {
        // InputManager accepts only its own SDL window's events, whose positions
        // are client-local. ImGui multi-viewports use desktop logical positions.
        glm::dvec2 point{x,y};
        if(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_ViewportsEnable) {
            auto* native=SDL_GetWindowFromID(window.GetWindowID());
            if(!native)return {};
            int left{},top{};SDL_GetWindowPosition(native,&left,&top);
            point+=glm::dvec2(left,top);
        }
        return point;
    }
}

void RigidBodySimulationApp::Update(Timestep ts)
{
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
    Rbs::SandboxChecks::BeforeUpdate(*this, ts);
#endif
    ProcessSceneRequest();
#ifdef GENGINE_PHASE29_QUALIFICATION
#if defined(GENGINE_PHASE29_STEP_SAFETY)
    const bool stepSafety=Rbs::PassiveRagdollChecks::SafetyRequested();
    if (stepSafety) {
        try { Rbs::PassiveRagdollChecks::PrepareSafety(*this); }
        catch (const std::exception& ex) {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","StepSafetySetup","prepare","Debug",ex.what()});
            return;
        }
    }
#endif
    const bool phase29=Rbs::PassiveRagdollChecks::Requested();
    if(phase29 && !Rbs::Phase29::initialized) {
        try {
            Rbs::PassiveRagdollChecks::Start(*this);
            if(Rbs::Phase29::debug){
                if(!Rbs::Phase29::jolt){Rbs::PassiveRagdollChecks::Finish(*this);ShutDown();return;}
                m_IsPause=true;m_ActiveScene->SetPaused(true);
            } else {
                m_EditorMode=EditorMode::Simulation;
                SetSimulationState(Rbs::SimulationState::Running);
                m_IsPause=false;m_ActiveScene->SetPaused(false);
            }
        } catch(const std::exception& e){Rbs::PassiveRagdollChecks::Finish(*this,e.what());ShutDown();return;}
    }
#elif defined(GENGINE_PHASE28_QUALIFICATION)
    Rbs::AngularJointChecks::PreviewBoundary(*this);
    const bool phase28=Rbs::AngularJointChecks::Begin(*this);
    if(phase28){m_SimulationState=Rbs::SimulationState::Running;m_EditorMode=EditorMode::Simulation;
        m_IsPause=false;m_ActiveScene->SetPaused(false);}
#endif
#ifdef GENGINE_PHASE27_QUALIFICATION
    Rbs::BallSocketChecks::PreviewBoundary(*this);
#endif
    SynchronizeEntityPresentation();
#ifdef GENGINE_OWNER_REVIEW_PROBE
    if(!Rbs::OwnerReviewDriver::Begin(*this))return;
#endif
    if(SandboxEnabled()) { Rbs::ProcessColliderEditorCommands(*m_ActiveScene,m_HoveredEntity,m_IsPause); ProcessBodyPropertyCommand(); ProcessBulkBodyProperties(); ProcessWorldSleepCommand(); ProcessJoltSettingsCommand(); ProcessTransformEdit(); ProcessContextProperties(); ProcessImpulseCommand(); }
    if(SandboxEnabled() && m_ActiveSelection.IsAngularScene() && m_ChainImpulse.Pending())
    {
        const auto before=m_ChainImpulse.AppliedCount();
        m_ChainImpulse.Consume(*m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld(),false);
        if(m_ChainImpulse.AppliedCount()!=before)m_Angular.interactive=true;
    }
    if(!m_Launch.joltParameterProfile.empty() && !Rbs::JoltParameterExperiment::Boundary(*this))return;
    if(m_Launch.m_BackendSwitchChecks && !Rbs::BackendSwitchChecks::Boundary(*this))return;
    Rbs::ImpulseEditorChecks::PresetBoundary(*this);
    if(m_Launch.m_BackendSmokeTicks && m_SimulationState==Rbs::SimulationState::Ready)
        SetSimulationState(Rbs::SimulationState::Running);
    if (!m_Running) return;
    if (SandboxEnabled())
        m_IsPause = m_SimulationState != Rbs::SimulationState::Running;
    if (!ValidateUpdate())
        return;

#ifdef GENGINE_PHASE25_QUALIFICATION
    // The isolated request drives the ordinary Scene scheduler once per frame.
    // Preserve the original Scene owner until the fixture and callbacks retire.
    static std::unique_ptr<Rbs::PhysicsCost::Session> phase25;
    if (!phase25)
    {
        phase25 = std::make_unique<Rbs::PhysicsCost::Session>();
        phase25->original = m_ActiveScene;
        phase25->originalPaused = m_IsPause;
        phase25->originalFrameLimit = GetManualFrameRateLimit();
        SetManualFrameRateLimit(0);
        m_ActiveScene->SetPaused(true);
        phase25->Initialize(*GetWindow(), *m_SceneResources);
        m_EditorCamera_.m_FOV = glm::radians(60.f);
        m_EditorCamera_.m_NearClip = .1f;
        m_EditorCamera_.m_FarClip = 1000.f;
        m_EditorCamera_.m_Position = {60,80,180};
        m_EditorCamera_.m_FocalPoint = {60,0,60};
        m_EditorCamera_.m_Distance = glm::length(Vec3f(0,80,120));
        m_EditorCamera_.m_Pitch = std::atan2(80.f,120.f);
        m_EditorCamera_.m_Yaw = 0;
        m_EditorCamera_.m_PositionDelta = {};
        m_EditorCamera_.m_PitchDelta = m_EditorCamera_.m_YawDelta = 0;
        m_EditorCamera_.UpdateView();
    }
    auto& qualification = *phase25;
    if (qualification.rowDone && !qualification.failed)
    {
        qualification.Check(BindCameraScene(qualification.original), "fixture view retirement");
        qualification.RetireRow();
    }
    if (!qualification.scene && !qualification.complete && !qualification.failed)
    {
        if (qualification.Prepare())
            qualification.Check(BindCameraScene(qualification.scene), "fixture view binding");
    }
    if (!qualification.failed && !qualification.complete)
    {
        qualification.actualFrameLimit = GetManualFrameRateLimit();
        qualification.Check(qualification.actualFrameLimit == 0, "qualification manual frame cap");
        m_IsPause = false;
        m_HoveredEntity = {};
        OnViewportResize(static_cast<int>(m_ViewportSize.x), static_cast<int>(m_ViewportSize.y));
        qualification.Tick();
    }
    if (qualification.failed || qualification.complete)
    {
        qualification.Check(BindCameraScene(qualification.original), "original Scene restoration");
        if (qualification.scene) qualification.RetireRow();
        m_IsPause = qualification.originalPaused;
        m_ActiveScene->SetPaused(m_IsPause);
        SetManualFrameRateLimit(qualification.originalFrameLimit);
        qualification.Export();
        phase25.reset();
        ShutDown();
    }
    return;
#endif

#ifdef GENGINE_MESH_HIGHLIGHT_AUDIT
    // Opt-in pixel references need a stationary background across selection cases.
    if (std::getenv("GENGINE_ORDINARY_HIGHLIGHT_OUTPUT")) ts=Timestep(0.0);
#endif
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
    ts=Timestep(Rbs::ColliderReplay::FrameElapsed(ts.GetSecondsPrecise()));
#endif
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
    ts=Timestep(Rbs::ColliderPassive::FrameElapsed(ts.GetSecondsPrecise()));
#endif
    if (m_Launch.AsyncPerformanceEnabled())
        return;
    if (!ReplayMaterialValidation())
        UpdateMaterialAnimation(ts);
    // ImGui reports subpixel extents; retain whole-pixel truncation for framebuffer resize.
    OnViewportResize(static_cast<int>(m_ViewportSize.x), static_cast<int>(m_ViewportSize.y));
    //auto entity = m_ActiveScene->FindEntityByName("sphere");

    //auto& relationship = entity.GetComponent<RelationshipComponent>();
    if(m_CameraPilot)UpdateCameraPilot(ts);
    else if (m_CameraView == CameraView::Editor && !m_Launch.restingContactDiagnostic)
        m_EditorCamera_.OnUpdate(ts);

    const auto authored = [this](const auto& result)
    {
        if (!result)
        {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Scene Transform",
                         std::to_string(static_cast<unsigned>(result.error().code)),
                         "authoritative Transform mutation", "",
                         "RBS authoring mutation rejected before commit"});
            return false;
        }
        if (!result->notification)
            ReportSubscriptionError(result->notification.error());
        return true;
    };
    auto skyRotation = m_SkyBoxEntity.Transform().EulerRotation;
    skyRotation.y += ts * 0.02f;
    if (!authored(m_ActiveScene->SetLocalRotation(m_SkyBoxEntity, skyRotation)))
        return;
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
    if (m_Launch.restingContactDiagnostic && !BeginRestingContactEpisode())
        return;
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
    m_IsPause=Rbs::ColliderReplay::FramePaused(m_IsPause);
#endif
    if (std::exchange(m_ColliderEscapeRequested, false))
        if (!Rbs::CancelColliderConfirmation(*m_ActiveScene, m_IsPause) && !HasPendingObjectProperties()) ClearColliderSelection();
    const bool confirmationPause = Rbs::ColliderEditorTemporarilyPaused(*m_ActiveScene, m_HoveredEntity, m_IsPause);
    m_ActiveScene->SetPaused(m_IsPause || confirmationPause);
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
    if(Rbs::ColliderPassive::UseDetailed())
#endif
    Rbs::ColliderReplacementObservation::BeforeFrame(*m_ActiveScene,ts.GetSecondsPrecise());
    if(!Rbs::ColliderReplay::BeforeUpdate(*m_ActiveScene,*m_SceneResources)) {
        if(Rbs::ColliderReplacementObservation::session) {
            Rbs::ColliderReplacementObservation::session->Fail(Rbs::ColliderReplay::Failure());
            Rbs::ColliderReplacementObservation::session->Finish();
        }
        ShutDown();return;
    }
    if(auto target=Rbs::ColliderCapture::TakePending(*m_ActiveScene);target) {
        const auto changed=Rbs::ApplyCapturedMeshFit(*m_ActiveScene,*m_SceneResources,target,m_CameraSnapshot?&*m_CameraSnapshot:nullptr);
        if(!changed) Log::GetCoreLogger()->error("Captured fitted Capsule replacement: {}",changed.error());
    }
#endif
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
    if(!Rbs::ColliderPassive::BeforeStep(*m_ActiveScene,ts.GetSecondsPrecise())) { ShutDown(); return; }
#endif
    auto simulationTime = ts;
#ifdef GENGINE_PHASE27_QUALIFICATION
    const bool phase27=Rbs::BallSocketChecks::Begin(*this);
    if(phase27){m_EditorMode=EditorMode::Simulation;SetSimulationState(Rbs::SimulationState::Running);m_IsPause=false;}
#endif
#if defined(GENGINE_JOLT_FINAL_AUDIT) && !defined(GENGINE_PHASE29_QUALIFICATION)
    const bool finalAudit=Rbs::FinalAudit::Begin(*m_ActiveScene);
    if(finalAudit && !Rbs::FinalAudit::ticks){m_EditorMode=EditorMode::Simulation;SetSimulationState(Rbs::SimulationState::Running);}
#endif
    if (SandboxEnabled())
    {
        // Paused host time is never admitted to the existing Scene scheduler.
        // The first resumed frame excludes time preceding the Play command;
        // its already accepted fractional backlog remains owned by Scene.
        if (m_IsPause || std::exchange(m_SkipSimulationElapsed, false))
            simulationTime = Timestep(0.0);
    }
    // Explicit smoke mode admits exactly one ordinary fixed tick per frame.
    // Host frame latency cannot exceed the frozen 120-tick safety bound.
    if(m_Launch.m_BackendSmokeTicks && !m_IsPause)
        simulationTime=Timestep(m_ActiveScene->GetPhysicsStep().count());
#ifdef GENGINE_OWNER_REVIEW_PROBE
    if(Rbs::OwnerReviewDriver::ready && Rbs::OwnerReviewDriver::RunningMode())
        simulationTime=Timestep(m_ActiveScene->GetPhysicsStep().count());
#endif
    if(!m_Launch.joltParameterProfile.empty() && !m_IsPause)simulationTime=Timestep(m_ActiveScene->GetPhysicsStep().count());
    const auto experimentStart=m_Launch.joltParameterProfile.empty()?std::chrono::steady_clock::time_point{}:std::chrono::steady_clock::now();
#if defined(GENGINE_JOLT_FINAL_AUDIT) && !defined(GENGINE_PHASE29_QUALIFICATION)
    if(finalAudit){simulationTime=Timestep(m_ActiveScene->GetPhysicsStep().count());JoltAudit::countAllocations=Rbs::FinalAudit::observed;}
#endif
#ifdef GENGINE_PHASE27_QUALIFICATION
    if(phase27){simulationTime=Timestep(m_ActiveScene->GetPhysicsStep().count());Rbs::BallSocketChecks::BeforeTick();}
#endif
#ifdef GENGINE_PHASE29_QUALIFICATION
    if(phase29 && !Rbs::Phase29::debug){
        simulationTime=Timestep(m_ActiveScene->GetPhysicsStep().count());
        try {Rbs::PassiveRagdollChecks::Before(*this);}
        catch(const std::exception& e){Rbs::PassiveRagdollChecks::Finish(*this,e.what());ShutDown();return;}
    }
#elif defined(GENGINE_PHASE28_QUALIFICATION)
    if(phase28){simulationTime=Timestep(m_ActiveScene->GetPhysicsStep().count());Rbs::AngularJointChecks::BeforeTick();}
    if(!Rbs::AngularJointChecks::BeforeEditorSceneUpdate(*this,simulationTime))return;
#endif
#if defined(GENGINE_PHASE29_STEP_SAFETY)
    if (stepSafety) {
        m_ActiveScene->SetPaused(false);
        simulationTime=Timestep(m_ActiveScene->GetPhysicsStep().count());
    }
#endif
    { RBS_REVIEW_SCOPE(4); m_ActiveScene->Update(simulationTime); }
    auto* stepSystem=m_ActiveScene->GetPhysicsSystem();
    if (stepSystem->GetBackendFailure()) {
        const auto failure=*stepSystem->GetStepFailure();
        const auto* world=stepSystem->GetPhysicsWorld();
        const auto backend=world ? PhysicsBackendName(world->GetBackend()) : std::string_view{"detached"};
        auto details=std::format("code={} stage={} sdk={} world={} body={}/{} joint={} attempted={} completed={}",
            unsigned(failure.cause.code),unsigned(failure.stage),failure.sdkMask,world?world->GetWorldDomain():0,
            failure.cause.body.index,failure.cause.body.generation,failure.jointSlot,stepSystem->GetAttemptedSteps(),stepSystem->GetCompletedSteps());
        const std::string operation(failure.cause.operation);
        FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","BackendStep",
            operation,std::string(backend),details});
#ifdef GENGINE_PHASE29_QUALIFICATION
        try {
            if (phase29) Rbs::PassiveRagdollChecks::Finish(*this,operation.c_str());
#if defined(GENGINE_PHASE29_STEP_SAFETY)
            if (stepSafety) Rbs::PassiveRagdollChecks::RecordSafetyFault(*this,failure);
#endif
        } catch (const std::exception& ex) {
            Log::GetCoreLogger()->error("Step failure evidence cleanup: {}",ex.what());
        }
#endif
        return;
    }
#ifdef GENGINE_PHASE29_QUALIFICATION
    if(phase29 && !Rbs::Phase29::debug){
        try {
            Rbs::PassiveRagdollChecks::After(*this);
            if(Rbs::Phase29::tick==Rbs::Phase29::LastTick){Rbs::PassiveRagdollChecks::Finish(*this);ShutDown();return;}
        } catch(const std::exception& e){Rbs::PassiveRagdollChecks::Finish(*this,e.what());ShutDown();return;}
    }
#elif defined(GENGINE_PHASE28_QUALIFICATION)
    Rbs::AngularJointChecks::AfterEditorSceneUpdate(*this);
    if(phase28 && Rbs::AngularJointChecks::AfterTick(*this)){ShutDown();return;}
#endif
#ifdef GENGINE_PHASE27_QUALIFICATION
    if(phase27 && Rbs::BallSocketChecks::AfterTick(*this)){ShutDown();return;}
#endif
#if defined(GENGINE_JOLT_FINAL_AUDIT) && !defined(GENGINE_PHASE29_QUALIFICATION)
    if(finalAudit && Rbs::FinalAudit::After(*m_ActiveScene)){ShutDown();return;}
#endif
    if(!m_Launch.joltParameterProfile.empty() && !m_IsPause)Rbs::JoltParameterExperiment::AfterTick(*this,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-experimentStart).count());
    if(m_Launch.m_BackendSmokeTicks)
    {
#ifdef GENGINE_OWNER_REVIEW_PROBE
        Rbs::OwnerReviewDriver::PhysicsSnapshot(*this);
#endif
        auto* system=m_ActiveScene->GetPhysicsSystem();auto* world=system->GetPhysicsWorld();
        if(system->HasJointFailure())
        {FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","Joint","Backend smoke","","Native joint numerical failure"});return;}
        for(std::size_t i=0;i<world->GetBodyCount();++i)
        {
            auto handle=world->BodyAt(i);
            if(!handle || !world->ReadBody(*handle))
            {FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","NonFinite","Backend smoke","","Invalid body state"});return;}
        }
        for(std::size_t i=0;i<world->GetJointCount();++i)
        {
            auto handle=world->JointAt(i);auto joint=handle?world->ReadJoint(*handle):JointResult<JointState>(std::unexpected(handle.error()));
            if(!joint){FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","Joint","Backend smoke","","Invalid joint state"});return;}
            m_BackendSmokeMaxJointError=(std::max)(m_BackendSmokeMaxJointError,std::abs(joint->error));
        }
        if(m_Launch.m_BackendSwitchChecks && !Rbs::BackendSwitchChecks::AfterTick(*this))return;
        if(system->GetCompletedSteps()>=m_Launch.m_BackendSmokeTicks)
        {
            Log::GetCoreLogger()->info("BACKEND_SMOKE_PASS backend={} scene={} ticks={} bodies={} joints={} max_joint_error={}",
                PhysicsBackendName(m_Backend),Rbs::SceneName(m_ActiveSelection.scene),system->GetCompletedSteps(),
                world->GetBodyCount(),world->GetJointCount(),m_BackendSmokeMaxJointError);
            ShutDown();return;
        }
    }
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
    Rbs::SandboxChecks::AfterUpdate(*this);
#endif
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
    if(Rbs::ColliderPassive::AfterStep(ts.GetSecondsPrecise())) { ShutDown(); return; }
#endif
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
    if(Rbs::ColliderPassive::UseDetailed())
#endif
    if (Rbs::ColliderReplacementObservation::AfterFrame()) { ShutDown(); return; }
#endif
    if (m_Launch.restingContactDiagnostic && !CompleteRestingContactFrame(ts))
        return;

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

void RigidBodySimulationApp::UpdateMaterialAnimation(Timestep ts)
{
    const auto authored = [this](const auto& result)
    {
        if (!result)
        {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Scene Transform",
                         std::to_string(static_cast<unsigned>(result.error().code)),
                         "authoritative Transform mutation", "",
                         "RBS authoring mutation rejected before commit"});
            return false;
        }
        if (!result->notification)
            ReportSubscriptionError(result->notification.error());
        return true;
    };
    if (m_MaterialGallery.parent && m_MaterialGallery.animate)
    {
        const bool validation =
            m_Launch.validation.Includes(Rbs::ValidationCheck::MaterialAuthoring);
        m_MaterialGallery.time += validation ? .05f : float(ts);
        const float t = m_MaterialGallery.time;
        if (!authored(m_ActiveScene->SetLocalTranslation(
                m_MaterialGallery.parent, {.5f * std::sin(t), 0, .5f * std::cos(t)})) ||
            !authored(m_ActiveScene->SetLocalRotation(m_MaterialGallery.parent,
                                                      {0, .04f * std::sin(t), 0})))
            return;
        for (std::size_t i = 0; i < m_MaterialGallery.uv.size(); ++i)
            for (auto entity : {m_MaterialGallery.uv[i], m_MaterialGallery.triplanar[i]})
            {
                if (!authored(m_ActiveScene->SetLocalRotation(
                        entity, {.3f * std::sin(t), t, .2f * std::cos(t)})) ||
                    !authored(m_ActiveScene->SetLocalScale(
                        entity,
                        validation && m_MaterialGallery.validationFrames < 30
                            ? Vec3f(1.f + .2f * std::sin(t))
                            : Vec3f{1.f + .2f * std::sin(t), 1.f + .15f * std::cos(t), 1.f})))
                    return;
            }
        if (validation && m_MaterialGallery.validationFrames == 59)
        {
            m_MaterialGallery.animate = false;
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_09_MOTION_COMPLETE frames=60 local_mapping=true parent_motion=true positive_nonuniform_scale=true visual_result=pending");
        }
    }
}

bool RigidBodySimulationApp::CancelCameraGesture()
{
    ClearOutlineHover();
    auto& camera=NavigationCamera();
    const auto position = camera.GetPosition();
    const auto pitch = camera.m_Pitch + camera.m_PitchDelta;
    const auto yaw = camera.m_Yaw + camera.m_YawDelta;
    const bool pendingMovement = camera.m_PositionDelta != Vec3f{} ||
        camera.m_PitchDelta != 0 || camera.m_YawDelta != 0;
    if (auto cancelled = GetInputManager()->CancelInput(InputCancelReason::OwnerRelease); !cancelled)
    {
        m_CameraDiagnostic = std::format("Camera switch rejected: input cancellation {}", int(cancelled.error()));
        return false;
    }
    // Angular damping participates in the controller's current orientation. Consume
    // cancellation, then retain that effective pose with no pending movement.
    camera.OnUpdate(Timestep(0));
    camera.m_Position = position;
    camera.m_Pitch = pitch;
    camera.m_Yaw = yaw;
    if (pendingMovement)
        camera.m_FocalPoint = position +
            camera.GetForwardDirection() * camera.GetDistance();
    m_FramePickPixel.reset();
    return true;
}

bool RigidBodySimulationApp::SetCameraView(CameraView role, EntityRenderId preview)
{
    if(m_CameraPilot || role==CameraView::Pilot){m_CameraDiagnostic="Commit or Cancel Pilot before changing views.";return false;}
    if (role == CameraView::Editor)
    {
        if (auto valid = m_ActiveScene->ValidateEditorViewIdentity(m_EditorViewIdentity); !valid)
        {
            m_CameraDiagnostic = "Editor view identity is unavailable";
            return false;
        }
    }
    else
    {
        auto active = m_ActiveScene->ActiveSceneCamera();
        if (role == CameraView::ActiveScene && (!active || !*active))
        {
            m_CameraDiagnostic = "No valid active Scene Camera";
            return false;
        }
        const auto id = role == CameraView::Preview ? preview : **active;
        auto access = m_SceneResources->Publication().BeginFrame();
        if (auto valid = m_ActiveScene->SnapshotSceneCamera(id, m_SceneResources->ForFrame(access), {}); !valid)
        {
            m_CameraDiagnostic = std::format("Camera binding rejected: {}", int(valid.error().code));
            return false;
        }
    }
    if (!CancelCameraGesture()) return false;
    if (role == CameraView::Preview && m_CameraView != CameraView::Preview)
        m_PreviewReturnView = m_CameraView;
    m_CameraView = role;
    m_PreviewCamera = role == CameraView::Preview ? preview : EntityRenderId{};
    m_CameraDiagnostic.clear();
    return true;
}

bool RigidBodySimulationApp::EndCameraPreview()
{
    if (m_CameraView != CameraView::Preview) return true;
    CameraView restore = CameraView::Editor;
    if (m_PreviewReturnView == CameraView::ActiveScene)
    {
        auto active = m_ActiveScene->ActiveSceneCamera();
        if (active && *active)
        {
            auto access = m_SceneResources->Publication().BeginFrame();
            if (m_ActiveScene->SnapshotSceneCamera(**active, m_SceneResources->ForFrame(access), {}))
                restore = CameraView::ActiveScene;
        }
    }
    // Choose the valid fallback before cancelling. A cancellation failure is
    // reported once and must not be retried as a different role transition.
    return SetCameraView(restore);
}

bool RigidBodySimulationApp::SelectSceneCamera(EntityRenderId id)
{
    if(m_CameraPilot){m_CameraDiagnostic="Commit or Cancel Pilot before changing the active Camera.";return false;}
    auto access = m_SceneResources->Publication().BeginFrame();
    auto resources = m_SceneResources->ForFrame(access);
    if (auto valid = m_ActiveScene->SnapshotSceneCamera(id, resources, {}); !valid)
    {
        m_CameraDiagnostic = std::format("Scene selection rejected: {}", int(valid.error().code));
        return false;
    }
    if (m_CameraView == CameraView::ActiveScene && !CancelCameraGesture()) return false;
    if (auto selected = m_ActiveScene->SetActiveSceneCamera(id, resources); !selected)
    {
        m_CameraDiagnostic = std::format("Scene selection rejected: {}", int(selected.error().code));
        return false;
    }
    m_CameraDiagnostic.clear();
    return true;
}

bool RigidBodySimulationApp::RetireSceneCamera(std::size_t slot, bool replace)
{
    if(m_CameraPilot){m_CameraDiagnostic="Commit or Cancel Pilot before retiring a Camera.";return false;}
    const auto old = m_SceneCameras[slot];
    auto source = m_ActiveScene->RenderData().Resolve(old);
    if (!source) { m_CameraDiagnostic = "Scene Camera has already retired"; return false; }
    const auto active = m_ActiveScene->ActiveSceneCamera();
    const bool bound = (m_CameraView == CameraView::Preview && m_PreviewCamera == old) ||
        (m_CameraView == CameraView::ActiveScene && active && *active && **active == old);
    if (bound && !CancelCameraGesture()) return false;
    std::expected<void, SceneCameraError> result;
    EntityRenderId replacement;
    if (replace)
    {
        auto copied = m_ActiveScene->DuplicateEntity(_Entity{*source, m_ActiveScene.get()});
        if (!copied) { m_CameraDiagnostic = "Camera replacement creation failed"; return false; }
        auto identity = m_ActiveScene->RenderData().Identify(*copied);
        if (identity)
        {
            replacement = *identity;
            auto access = m_SceneResources->Publication().BeginFrame();
            result = m_ActiveScene->ReplaceSceneCamera(old, replacement, m_SceneResources->ForFrame(access));
        }
        if (!identity || !result)
        {
            (void)m_ActiveScene->DestroyEntity(*copied);
            m_CameraDiagnostic = "Camera replacement rejected; original retained";
            return false;
        }
    }
    else result = m_ActiveScene->DeleteSceneCamera(old);
    if (!result)
    {
        m_CameraDiagnostic = std::format("Camera retirement rejected: {}", int(result.error().code));
        return false;
    }
    m_SceneCameras[slot] = replacement;
    // The next frame resolves exact references, including preview and active fallback.
    return true;
}

bool RigidBodySimulationApp::BindCameraScene(RefPtr<_Scene> scene)
{
    // Camera part of the application's scene-owner transition. The caller must
    // finish its other authoring bindings before the next application Update.
    if (!scene) { m_CameraDiagnostic = "Camera binding requires a Scene"; return false; }
    if (scene == m_ActiveScene) return true;
    if(m_CameraPilot && !FinishCameraPilot(false))return false;
    auto view = scene->CreateEditorViewIdentity();
    if (!view) { m_CameraDiagnostic = "New Scene view identity unavailable"; return false; }
    if (!CancelCameraGesture()) { (void)scene->RetireEditorViewIdentity(*view); return false; }
    if (m_ActiveScene && m_EditorViewIdentity)
        if (auto retired = m_ActiveScene->RetireEditorViewIdentity(m_EditorViewIdentity); !retired)
        {
            (void)scene->RetireEditorViewIdentity(*view);
            m_CameraDiagnostic = "Previous Scene view retirement failed";
            return false;
        }
    if (m_ActiveScene) Rbs::ResetColliderEditor(*m_ActiveScene, m_IsPause);
    m_ColliderOutlines.Clear();
    m_EditorSelection.Clear();
    CancelViewportMarquee();
    m_HierarchyRevealed={};
    m_HierarchySelectionHandoff=false;
    m_OutlineToggleRequested = m_FrameOutlineToggle = false;
    m_ActiveScene = std::move(scene);
    m_EditorViewIdentity = *view;
    m_SceneCameras = {};
    m_PreviewCamera = {};
    m_CameraView = m_PreviewReturnView = CameraView::Editor;
    m_HoveredEntity = {};
    m_CameraDiagnostic.clear();
    return true;
}

void RigidBodySimulationApp::ClearOutlineHover()
{
    m_OutlineHoverEntity={};m_OutlineHoverIcon={};
    m_FrameHoverPixel.reset();m_OutlineHoverPixel.reset();
    m_OutlineHoverInputsDirty=true;
}

void RigidBodySimulationApp::PrepareOutlineHover()
{
    m_FrameHoverPixel.reset();
    if(m_CameraPilot){ClearOutlineHover();return;} // Selection is fixed during the Camera transaction.
    const auto& input=*GetInputManager();
    const auto& physical=input.GetPhysicalState();
    const auto& mouse=physical.m_Mouse;
    const auto* ui=ImGui::GetCurrentContext();
    const auto* viewportWindow=ui?ImGui::FindWindowByName("Viewport"):nullptr;
    const bool gesture=mouse.isButtonHeld(GENGINE_BUTTON_RIGHT) || mouse.isButtonHeld(GENGINE_BUTTON_MIDDLE) ||
        (mouse.isButtonHeld(GENGINE_BUTTON_LEFT) && !m_FramePickPixel);
    if(!SandboxEnabled() || !m_CameraSnapshot || !input.Focused() || !m_ViewportHovered ||
       input.Routing().uiMouse || input.PointerCapture()!=InputLayer::None || gesture ||
       (physical.modifiers&(LeftAlt|RightAlt)) || m_ViewportActivationButtons ||
       m_GizmoHovered || m_GizmoUsing || m_ViewportOverlayInteracting || m_ViewportPopupOpen ||
       m_Marquee.armed || m_Marquee.waiting || m_SceneRequest || m_BackendRequest ||
       m_SimulationState==Rbs::SimulationState::Switching || m_SimulationState==Rbs::SimulationState::Failed ||
       !ui || !viewportWindow || viewportWindow->Viewport!=ImGui::GetMainViewport() ||
       ui->DragDropActive || ui->OpenPopupStack.Size || ui->IO.WantTextInput)
    {ClearOutlineHover();return;}
    const auto localPoint=mouse.GetPosition();
    const auto point=ScenePointerLogical(*GetWindow(),localPoint.x,localPoint.y);
    if(!point){ClearOutlineHover();return;}
    const auto pixel=m_CameraSnapshot->ReadbackPixelAt(*point);
    if(!pixel){ClearOutlineHover();return;}
    // Icons use the existing ordinary overlay hit rectangles and own their hover.
    for(std::size_t i=0;i<m_VisibleSceneIconCount;++i) {
        const auto& icon=m_VisibleSceneIcons[i];
        if(point->x>=icon.lo.x && point->x<icon.hi.x && point->y>=icon.lo.y && point->y<icon.hi.y)
        {ClearOutlineHover();return;}
    }
    if(m_OutlineHoverGeneration!=m_SceneGeneration) {
        ClearOutlineHover();m_OutlineHoverGeneration=m_SceneGeneration;
    }
    if(m_OutlineHoverEntity) {
        const auto resolved=m_ActiveScene->RenderData().Resolve(m_OutlineHoverEntity);
        if(!resolved || !EditorEntityVisible(m_OutlineHoverEntity))m_OutlineHoverEntity={};
        else {
            const _Entity entity{*resolved,m_ActiveScene.get()};
            if(!entity.HasAllComponents<MeshRendererComponent>() || !entity.GetComponent<MeshRendererComponent>().pickable ||
               (entity.HasAllComponents<VisibilityComponent>() && !entity.GetComponent<VisibilityComponent>().enabled))
                m_OutlineHoverEntity={};
        }
    }
    const bool moved=!m_OutlineHoverPixel || *m_OutlineHoverPixel!=*pixel;
    if(!moved && !m_OutlineHoverInputsDirty)return;
    if(moved)m_OutlineHoverEntity={}; // Never trail the old object while the pointer moves.
    const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if((m_FramePickPixel && *m_FramePickPixel==*pixel) || m_OutlineHoverLastReadbackTime<0 || now-m_OutlineHoverLastReadbackTime>=1.0/30.0) {
        m_FrameHoverPixel=*pixel;++m_OutlineHoverRequests;
    }
}

void RigidBodySimulationApp::ReadOutlineHover(glm::uvec2 position,std::optional<std::int32_t> sharedPixel)
{
    m_FrameHoverPixel.reset();m_OutlineHoverEntity={};
    if(!m_ActiveScene || m_OutlineHoverGeneration!=m_SceneGeneration)return;
    m_OutlineHoverPixel=position;m_OutlineHoverInputsDirty=false;
    m_OutlineHoverLastReadbackTime=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(!sharedPixel) {
        ++m_OutlineHoverReadbacks;
        const auto read=m_MousePickFrameBuffer->ReadPixel(int(position.x),int(position.y));
        if(!read){ReportFramebufferError("hover picking read",read.error());return;}
        sharedPixel=*read;
    }
    if(*sharedPixel==EntityPickTable::InvalidPixel)return;
    const auto id=m_ActiveScene->RenderData().ResolvePick(m_PickTable,*sharedPixel);
    if(id && EditorEntityVisible(*id))m_OutlineHoverEntity=*id;
}

ScheduleResult RigidBodySimulationApp::PrepareCameraFrame(RenderContext& context,
                                                          FrameSceneInput& input, FrameCamera& camera)
{
    const bool requestedPick = std::exchange(m_ViewPickRequested, false);
    m_FrameOutlineToggle = requestedPick && std::exchange(m_OutlineToggleRequested, false);
    m_CameraSnapshot.reset();
    m_FramePickPixel.reset();
    m_CameraAvailable = false;
    m_CameraUnavailableDiagnostic.clear();
    if (!context.visible) {ClearOutlineHover();return {};}
    EntityRenderId id = m_CameraPilot?m_CameraPilot->command->edits[0].entity:m_EditorViewIdentity;
    if (m_CameraView == CameraView::Preview)
    {
        if (!m_ActiveScene->RenderData().Resolve(m_PreviewCamera))
        {
            if (!EndCameraPreview())
                return std::unexpected(ScheduleError{FrameStage::FreezeFrameInputs,
                    SubmissionError{m_CameraDiagnostic, SubmissionCode::InvalidCamera}});
            m_CameraDiagnostic = "Preview camera retired; previous valid view restored";
        }
        else id = m_PreviewCamera;
    }
    if (m_CameraView == CameraView::ActiveScene)
    {
        auto active = m_ActiveScene->ActiveSceneCamera();
        if (!active || !*active)
        {
            if (!SetCameraView(CameraView::Editor))
                return std::unexpected(ScheduleError{FrameStage::FreezeFrameInputs,
                    SubmissionError{m_CameraDiagnostic, SubmissionCode::InvalidCamera}});
            m_CameraDiagnostic = "Active Scene Camera unavailable; Editor view restored";
        }
        else id = **active;
    }
    const auto logical = GetEditorViewportLogicalSize();
    CameraViewport viewport;
    viewport.logicalOrigin = m_ViewportBounds[0];
    viewport.logicalSize = {logical.Width, logical.Height};
    viewport.pixelSize = {m_RenderTarget->GetWidth(), m_RenderTarget->GetHeight()};
    if (m_CameraView == CameraView::Editor)
    {
        id = m_EditorViewIdentity;
        if (!m_ActiveScene->ValidateEditorViewIdentity(id))
        {
            ClearOutlineHover();
            context.visible = false;
            m_CameraUnavailableDiagnostic = "Editor view unavailable: retired identity";
            return {};
        }
        CameraSnapshotDesc desc;
        desc.position = m_EditorCamera_.GetPosition();
        desc.orientation = m_EditorCamera_.GetOrientation();
        desc.verticalFovRadians = m_EditorCamera_.GetFOV();
        desc.nearPlane = m_EditorCamera_.GetNearClip();
        desc.farPlane = m_EditorCamera_.GetFarClip();
        desc.viewport = viewport;
#ifdef GENGINE_CAMERA_VALIDATION
        desc.projection = m_CameraValidationIntent.projection;
        desc.verticalFovRadians = m_CameraValidationIntent.verticalFovRadians;
        desc.orthographicHeight = m_CameraValidationIntent.orthographicHeight;
#endif
        m_EditorCamera_.UpdateView();
        auto snapshot = CameraSnapshot::Create(desc);
        if (snapshot) m_CameraSnapshot = *snapshot;
        else m_CameraUnavailableDiagnostic = std::format("Editor view unavailable: {}", int(snapshot.error().code));
    }
    else
    {
        // This callback runs after upload/publication and before extraction. Its
        // temporary access ends here; no authoring or Physics update intervenes.
        auto access = m_SceneResources->Publication().BeginFrame();
        auto snapshot = m_ActiveScene->SnapshotSceneCamera(id, m_SceneResources->ForFrame(access), viewport);
        if (snapshot) m_CameraSnapshot = *snapshot;
        else m_CameraUnavailableDiagnostic = std::format("Scene view unavailable: camera error {}", int(snapshot.error().code));
    }
    if (!m_CameraSnapshot) { ClearOutlineHover();context.visible = false; return {}; }
    m_CameraAvailable = true;
    camera = FrameCamera::FromSnapshot(id, *m_CameraSnapshot);
    const auto& desc = m_CameraSnapshot->Description();
    input.targets.cameraFov = static_cast<float>(desc.verticalFovRadians);
    input.targets.cameraAspect = float(camera.viewportWidth) / camera.viewportHeight;
    input.targets.cameraNear = static_cast<float>(desc.nearPlane);
    input.targets.cameraFar = static_cast<float>(desc.farPlane);
    const auto cascades=Rbs::Editor::CameraCascadeSplits(m_ShadowCascadeLevels,
        m_EditorCamera_.GetNearClip(),m_EditorCamera_.GetFarClip(),desc.nearPlane,desc.farPlane);
    if(!cascades) {
        ClearOutlineHover();
        context.visible=false;m_CameraAvailable=false;
        m_CameraUnavailableDiagnostic="Camera view unavailable: clip interval cannot represent five shadow slices";
        return {};
    }
    m_ViewCascadeLevels=*cascades;input.targets.cascadeSplits=m_ViewCascadeLevels;
    const auto& mouse = GetInputManager()->GetViewState().m_Mouse;
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PreEditorLifecycle::PickTraceValues("pick_prepare", -1,
        {m_FramePickPixel.has_value(), m_FramePickPixel ? m_FramePickPixel->x : 0,
         m_FramePickPixel ? m_FramePickPixel->y : 0},
        {desc.viewport.logicalOrigin.x, desc.viewport.logicalOrigin.y,
         desc.viewport.logicalSize.x, desc.viewport.logicalSize.y,
         double(desc.viewport.pixelSize.x), double(desc.viewport.pixelSize.y)});
#endif
    if (requestedPick && m_ViewportActive && !m_ViewportActivationButtons &&
        mouse.isButtonPressed(GEngineMouseCode::GENGINE_BUTTON_LEFT))
    {
        const auto localPoint = mouse.GetPosition();
        const auto point = ScenePointerLogical(*GetWindow(),localPoint.x,localPoint.y);
        if(!point){ClearOutlineHover();return {};}
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        auto pixel = m_CameraSnapshot->ReadbackPixelAt(*point);
        PreEditorLifecycle::PickTraceValues("pick_conversion", bool(pixel),
            {pixel ? pixel->x : 0, pixel ? pixel->y : 0, pixel ? 0 : std::uint64_t(pixel.error().code) + 1});
        if (pixel)
#else
        if (auto pixel = m_CameraSnapshot->ReadbackPixelAt(*point); pixel)
#endif
            m_FramePickPixel = *pixel;
    }
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PreEditorLifecycle::PickTraceValues("pick_ready", m_FramePickPixel.has_value(),
        {m_FramePickPixel ? m_FramePickPixel->x : 0, m_FramePickPixel ? m_FramePickPixel->y : 0});
#endif
    PrepareOutlineHover();
    input.targets.pickingEnabled = m_FramePickPixel.has_value() || m_FrameHoverPixel.has_value();
    if(m_Marquee.queued && m_Marquee.generation==m_SceneGeneration) {
        input.targets.rectangleSelection=m_Marquee.request;input.targets.pickingEnabled=true;m_Marquee.queued=false;
    }
#ifdef GENGINE_CAMERA_VALIDATION
    input.targets.pickingEnabled = true;
#endif
#ifdef GENGINE_LIFECYCLE_VALIDATION
    if (PreEditorLifecycle::GpuChecks()) input.targets.pickingEnabled = true;
#endif
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
    PreEditorInput::RecordInputPicking(input.targets.pickingEnabled);
#endif
    return ValidateBeforeSubmission(input.targets, camera);
}

void RigidBodySimulationApp::Render()
{
#if defined(GENGINE_PHASE29_STEP_SAFETY)
    if (Rbs::Phase29::safetyArmed) ++Rbs::Phase29::safetyRenders;
#endif
    if (m_ViewportAltGestureConsumed)
        ImGui::GetCurrentContext()->NavWindowingToggleLayer = false;
    if (m_Launch.AsyncPerformanceEnabled())
        return;
    if (!m_ActiveScene || !m_SceneResources || !m_FrameSubmission)
    {
        FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Scene", "",
            "render scene", "", "No complete scene is available for submission"});
        return;
    }
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
        if (auto prepared = PrepareFrameResources(context); !prepared)
            return std::unexpected(prepared.error());
        FrameCamera camera;
        FrameSubmissionDesc targets{*m_RenderTarget,
                                    *m_MousePickFrameBuffer,
                                    *m_PointShadowFrameBuffer,
                                    *m_CascadeShadowFrameBuffer,
                                    {}, // FrameScheduler supplies current renderer pipeline roles.
                                    m_ShadowCascadeLevels,
                                    glm::radians(45.f), 1.f, .1f, 1000.f,
                                    m_NearPlane,
                                    m_FarPlane};
        FrameSceneInput input{*m_ActiveScene, *m_SceneResources, *m_FrameSubmission,
                              targets,        m_PickTable,       {&camera, 1}};
#ifdef GENGINE_PHASE24_VALIDATION
        input.observeFrame = [](void*, const RenderFrame& frame) noexcept { PreEditorValidation::ObservePhase24Frame(frame); };
#endif
#ifdef GENGINE_PHASE24_OUTLINE_QUALIFICATION
        input.observeFrame=[](void*,const RenderFrame& frame) noexcept { Phase24Qualification::Observe(frame); };
#endif
#ifdef GENGINE_PHASE24_COLLIDER_VALIDATION
        input.observeFrame=[](void*,const RenderFrame& frame) noexcept { PreEditorValidation::ColliderReplacement::ObserveColliderRuntime(frame); };
#endif
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
        input.observeFrame=[](void*,const RenderFrame& frame) noexcept {
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
            if(!Rbs::ColliderPassive::UseDetailed())return;
#endif
            Rbs::ColliderReplacementObservation::ObserveFrame(frame);
        };
#endif
        // Preserve Phase 45's input/viewport snapshot: read this frame's IDs
        // before BeginUI refreshes ImGui mouse state or authors new panel bounds.
#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
        input.observationContext = this;
        input.observeFrame = [](void* user, const RenderFrame& frame) noexcept
        {
            static_cast<RigidBodySimulationApp*>(user)->ObserveColliderSelectionFrame(frame);
        };
#endif
        input.pickingReadback = {this, [](void* user) -> ScheduleResult
                                 {
                                     static_cast<RigidBodySimulationApp*>(user)->OnMouseClicked();
                                     return {};
                                 }};
#ifdef GENGINE_MESH_HIGHLIGHT_AUDIT
        if (std::getenv("GENGINE_ORDINARY_HIGHLIGHT_OUTPUT"))
            input.observeFrame = [](void*, const RenderFrame& frame) noexcept { Rbs::AuditHighlightFrame(frame); };
#endif
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
        input.observationContext = this;
        input.observeFrame = [](void* user, const RenderFrame& frame) noexcept
        {
            Rbs::SandboxChecks::ObserveFrame(*static_cast<RigidBodySimulationApp*>(user), frame);
        };
#endif
#if defined(GENGINE_PHASE28_QUALIFICATION) && !defined(GENGINE_PHASE29_QUALIFICATION)
        if (Rbs::AngularJointChecks::editorMode && Rbs::AngularJointChecks::ComponentRole(Rbs::AngularJointChecks::editorSession)) {
            input.observationContext = this;
            input.observeFrame = [](void* user, const RenderFrame& frame) noexcept {
                Rbs::AngularJointChecks::ObserveComponentFrame(*static_cast<RigidBodySimulationApp*>(user), frame);
            };
        }
#endif
        struct Preparation { RigidBodySimulationApp& app; RenderContext& context;
                             FrameSceneInput& input; FrameCamera& camera; } preparation{*this, context, input, camera};
        context.updateResources = {&preparation, [](void* user) -> ScheduleResult
        {
            auto& p = *static_cast<Preparation*>(user);
            if (auto updated = p.app.UpdateFrameResources(); !updated) return updated;
            if(p.app.SandboxEnabled()) {
                p.app.RenderCameraPanels();
                p.input.targets.editorHiddenEntities=p.app.m_EditorHidden;
            }
#ifdef GENGINE_OWNER_REVIEW_PROBE
            Rbs::OwnerReviewDriver::Camera(p.app);
#endif
            if (auto camera=p.app.PrepareCameraFrame(p.context,p.input,p.camera);!camera) return camera;
            if(p.app.m_HoveredEntity) {
                const auto selected=p.app.m_ActiveScene->RenderData().Identify(p.app.m_HoveredEntity);
                if(selected) p.input.targets.selectedEntity=*selected;
                else p.app.m_HoveredEntity={};
            } else p.app.m_HoveredEntity={};
            if(p.app.m_CameraSnapshot) {
                const auto& viewport=p.app.m_CameraSnapshot->Description().viewport;
                p.input.targets.selectionPixelsPerDip=glm::vec2(glm::dvec2(viewport.pixelSize)/viewport.logicalSize);
            }
#ifdef GENGINE_PHASE24_VALIDATION
            p.input.targets.pickingEnabled=true;
#endif
#ifdef GENGINE_PHASE24_COLLIDER_VALIDATION
            p.input.targets.pickingEnabled=true;
#endif
#ifdef GENGINE_PHASE24_OUTLINE_QUALIFICATION
            if(Phase24Qualification::Functional()) p.input.targets.pickingEnabled=true;
#endif
            auto& app = p.app;
            auto& physics = *app.m_ActiveScene->GetPhysicsSystem();
            app.m_ColliderOutlines.Prune(physics);
            app.m_EditorSelection.Prune(app.m_ActiveScene->RenderData());
            static_assert(std::tuple_size_v<decltype(app.m_ColliderOutlines.entities)> == FrameSubmissionDesc::SelectionCapacity);
            p.input.targets.selectedEntities = {app.m_ColliderOutlines.entities.data(), app.m_ColliderOutlines.count};
            if(app.m_EditorSelection.count)
                p.input.targets.selectedEntities = {app.m_EditorSelection.ids.data(),app.m_EditorSelection.count};
            p.input.targets.hoveredEntity=app.m_OutlineHoverEntity;
#if defined(GENGINE_PHASE28_QUALIFICATION) && !defined(GENGINE_PHASE29_QUALIFICATION)
            Rbs::AngularJointChecks::PrepareOutlineDescriptor(app,p.input.targets);
#endif
#ifdef GENGINE_OWNER_REVIEW_PROBE
            Rbs::OwnerReviewDriver::Targets(app,p.input.targets);
#endif
#ifdef GENGINE_MESH_HIGHLIGHT_AUDIT
            if (std::getenv("GENGINE_ORDINARY_HIGHLIGHT_OUTPUT")) Rbs::AuditHighlightDescriptor(p.input.targets);
#endif
#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
            if(std::getenv("GENGINE_MESH_HIGHLIGHT_OUTPUT")) {
                p.input.targets.measureSubmission=true;
                Rbs::MeshHighlightDescriptor(*app.m_FrameSubmission,p.input.targets);
                if(Rbs::MeshHighlightStage()==9) p.input.targets.selectionPixelsPerDip=glm::vec2(2.f);
            }
#endif
            const bool all = app.m_IsShowDebugBoundingBox;
            const bool selected = app.m_ShowSelectedColliderOutlines && app.m_ColliderOutlines.count;
            if (p.context.visible && (all || selected) && app.m_ColliderLayers.Any())
            {
                const std::size_t capacity = (std::max)(std::size_t(32768),
                    physics.GetPhysicsWorld()->GetBodyCount() * 512 + app.m_ColliderOutlines.count * 260 + 4096);
                if (app.m_ColliderDebugCapacity < capacity)
                {
                    std::unique_ptr<FrameDebugLine[]> storage(new(std::nothrow) FrameDebugLine[capacity]);
                    if (!storage) return std::unexpected(ScheduleError{FrameStage::BuildRenderFrame,
                        SubmissionError{"physics debug storage", SubmissionCode::Allocation}});
                    app.m_ColliderDebugStorage = std::move(storage);
                    app.m_ColliderDebugCapacity = capacity;
                }
                DebugGeometry geometry({app.m_ColliderDebugStorage.get(), app.m_ColliderDebugCapacity});
                if (!physics.AppendDebugGeometry(geometry, app.m_ColliderLayers,
                        {app.m_ColliderOutlines.bodies.data(), app.m_ColliderOutlines.count}, all))
                    return std::unexpected(ScheduleError{FrameStage::BuildRenderFrame,
                        SubmissionError{"scoped Physics layers", SubmissionCode::InvalidDraw}});
                // Diagnostic layers stay visible over opaque render meshes.
                for (std::size_t i = 0; i < geometry.Lines().size(); ++i)
                    app.m_ColliderDebugStorage[i].depth = DebugDepth::Overlay;
                if (app.m_ColliderLayers.colliders && app.m_ColliderOutlines.count)
                    if (!Rbs::AppendColliderEditorPreviews(*app.m_ActiveScene, app.m_ColliderOutlines, geometry))
                        return std::unexpected(ScheduleError{FrameStage::BuildRenderFrame,
                            SubmissionError{"selected candidate previews", SubmissionCode::InvalidDraw}});
                p.input.debugLines = geometry.Lines();
            }
            return {};
        }};
        auto submitted = FrameScheduler::Render(context, &input);
#ifdef GENGINE_PHASE29_QUALIFICATION
        if(submitted&&Rbs::Phase29::initialized&&Rbs::Phase29::debug&&Rbs::Phase29::jolt){
            static unsigned pausedFrames{};
            if(++pausedFrames==8){
                try {Rbs::PassiveRagdollChecks::CaptureThumbnail(*this);Rbs::PassiveRagdollChecks::Finish(*this);}
                catch(const std::exception& e){Rbs::PassiveRagdollChecks::Finish(*this,e.what());}
                ShutDown();
            }
        }
#endif
        if(submitted) {
            // Consume the renderer's exact picking-input comparison rather than
            // inventing a second scene/camera/material dirty key in the Editor.
            const auto& picking=submitted->submission.decisions[2];
            m_OutlineHoverInputsDirty=picking.reasons!=PassDirtyReason::None && !picking.requested;
        }
#ifdef GENGINE_PHASE27_QUALIFICATION
        if(submitted)Rbs::BallSocketChecks::PreviewFrame(*this,input.debugLines.size());
#endif
#if defined(GENGINE_PHASE28_QUALIFICATION) && !defined(GENGINE_PHASE29_QUALIFICATION)
        if(submitted) {
            Rbs::AngularJointChecks::ObserveOutlineSubmission(*this,submitted->submission);
            Rbs::AngularJointChecks::PreviewFrame(*this);
        }
#endif
#ifdef GENGINE_OWNER_REVIEW_PROBE
        if(submitted)Rbs::OwnerReviewDriver::End(*this,*submitted);
#endif
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
        if (submitted) Rbs::SandboxChecks::OnFrame(*this, *submitted);
#endif
#ifdef GENGINE_MESH_HIGHLIGHT_AUDIT
        if (submitted && std::getenv("GENGINE_ORDINARY_HIGHLIGHT_OUTPUT")) ObserveOrdinaryHighlightSubmission(*submitted);
#endif
#ifdef GENGINE_COLLIDER_EDITOR_SMOKE
        if (submitted) ObserveMeshHighlightSubmission(*submitted);
#endif
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
        if(Rbs::ColliderPassive::UseDetailed())
#endif
        Rbs::ColliderReplacementObservation::FrameRecord(2,bool(submitted),context.visible);
#endif
#ifdef GENGINE_PHASE24_OUTLINE_QUALIFICATION
        if(submitted) Phase24Qualification::CollectSubmission(*submitted);
#endif
        if(submitted && submitted->submission.rectangleSelectionDraws)
            Log::GetCoreLogger()->info("MARQUEE_SUBMIT draws={} query_objects_created={} physics_tick={}",submitted->submission.rectangleSelectionDraws,
                submitted->submission.rectangleQueryAllocations,m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps());
        if (context.visible && m_CameraSnapshot)
            if (auto checked = ValidateSubmittedFrame(bool(submitted), input.targets, camera); !checked)
                return std::unexpected(checked.error());
        return submitted;
    };
    if (auto result = render(); !result)
    {
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        PreEditorLifecycle::FinishPickTrace("submission_failure_before_first_pick");
#endif
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

void RigidBodySimulationApp::ClearColliderSelection()
{
    ClearOutlineHover();
    m_ColliderOutlines.Clear();
    m_EditorSelection.Clear();
    CancelViewportMarquee();
    m_HoveredEntity = {};
    m_ViewPickRequested = m_OutlineToggleRequested = m_FrameOutlineToggle = false;
    m_FramePickPixel.reset();
    if (m_ActiveScene) Rbs::ResetColliderEditor(*m_ActiveScene, m_IsPause);
}

void RigidBodySimulationApp::SelectColliderEntity(_Entity entity, bool toggle)
{
    if(SandboxEnabled() && HasPendingObjectProperties() && (toggle || entity!=m_HoveredEntity))
    {m_PropertyDiagnostic="Apply or Revert pending object edits before changing selection.";return;}
    // Never dereference an entity borrowed from a previous/different scene.
    if(entity.GetSceneContext() && entity.GetSceneContext()!=m_ActiveScene.get())return;
    if(entity.GetSceneContext() && !entity)return;
    auto& render=m_ActiveScene->RenderData();
    if(entity)
    {
        const auto id=render.Identify(entity);
        if(!id || !m_EditorSelection.Select(render,*id,toggle))
        {
            m_ColliderOutlines.capacityReached=m_EditorSelection.capacityReached;
            return;
        }
    }
    else if(!toggle)m_EditorSelection.Clear();
    else m_EditorSelection.Prune(render);
    SynchronizeEditorSelection();
}

void RigidBodySimulationApp::SynchronizeEditorSelection()
{
    Rbs::Editor::Numeric::Reset();
    CancelViewportMarquee();
    ++m_SelectionRevision;
    CancelGroupTransform();
    auto& render=m_ActiveScene->RenderData();
    m_ColliderOutlines.Clear();
    m_HoveredEntity={};
    for(std::size_t i=0;i<m_EditorSelection.count;++i)
        if(const auto resolved=render.Resolve(m_EditorSelection.ids[i]))
        {
            const _Entity selected{*resolved,m_ActiveScene.get()};
            if(const auto body=m_ActiveScene->GetBodyBinding(selected))
            {   // EntitySelection already guarantees unique stable IDs; no quadratic toggle scan.
                const auto index=m_ColliderOutlines.count++;
                m_ColliderOutlines.bodies[index]=*body;m_ColliderOutlines.entities[index]=m_EditorSelection.ids[i];++m_ColliderOutlines.revision;
            }
            m_HoveredEntity=selected;
        }
    // Updating the selected-object data must not transfer keyboard/navigation focus.
    // Explicit panel menu commands may still request Properties focus.
    m_PropertyDiagnostic.clear();
    // Selection changes cancel consent, while per-body proposal drafts survive.
    (void)Rbs::CancelColliderConfirmation(*m_ActiveScene, m_IsPause, false);
}

void RigidBodySimulationApp::OnMouseClicked()
{
    RBS_REVIEW_SCOPE(3);
    if(!m_FramePickPixel) {
        if(m_FrameHoverPixel && m_CameraSnapshot)ReadOutlineHover(*m_FrameHoverPixel);
        return;
    }
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PreEditorLifecycle::PickTraceValues("pick_readback_enter", -1,
        {m_FramePickPixel.has_value(), m_CameraSnapshot.has_value(),
         m_FramePickPixel ? m_FramePickPixel->x : 0, m_FramePickPixel ? m_FramePickPixel->y : 0});
#endif
    if (m_FramePickPixel && m_CameraSnapshot)
    {
        const int x = static_cast<int>(m_FramePickPixel->x);
        const int y = static_cast<int>(m_FramePickPixel->y);
        m_FramePickPixel.reset();
#ifdef GENGINE_LIFECYCLE_VALIDATION
        ++m_GesturePickReadbacks;
#endif
        auto pixel = m_MousePickFrameBuffer->ReadPixel(x, y);
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        PreEditorLifecycle::PickTraceValues("pick_readback_result", bool(pixel),
            {pixel ? 0 : std::uint64_t(pixel.error().code) + 1}, {pixel ? double(*pixel) : 0});
#endif
        if (!pixel)
        {
            ClearOutlineHover();
            ReportFramebufferError("picking read", pixel.error());
            return;
        }
        if(m_FrameHoverPixel && *m_FrameHoverPixel==glm::uvec2(unsigned(x),unsigned(y)))
            ReadOutlineHover(*m_FrameHoverPixel,*pixel);
        _Entity picked{};
        if (*pixel != EntityPickTable::InvalidPixel)
        {
            auto id = m_ActiveScene->RenderData().ResolvePick(m_PickTable, *pixel);
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
            PreEditorLifecycle::PickTraceValues("pick_resolve", bool(id),
                {id ? id->index : 0, id ? id->generation : 0, id ? id->registry : 0,
                 id ? 0 : std::uint64_t(id.error()) + 1});
#endif
            if (!id)
            {
                Log::GetCoreLogger()->warn("Stale/invalid picking result: {}", int(id.error()));
                return;
            }
            auto entity = m_ActiveScene->RenderData().Resolve(*id);
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
            PreEditorLifecycle::PickTraceValues("pick_entity", bool(entity),
                {entity ? std::uint64_t(*entity) : 0, entity ? 0 : std::uint64_t(entity.error()) + 1});
#endif
            if (!entity)
            {
                Log::GetCoreLogger()->warn("Picking entity: {}", int(entity.error()));
                return;
            }
            picked = _Entity{*entity, m_ActiveScene.get()};
        }
        if(SandboxEnabled())ArmViewportMarquee(unsigned(x),unsigned(y),
            picked ? m_ActiveScene->RenderData().Identify(picked).value_or(EntityRenderId{}) : EntityRenderId{});
        else SelectColliderEntity(picked, m_FrameOutlineToggle);
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        PreEditorLifecycle::PickTraceValues("pick_hovered", bool(m_HoveredEntity),
            {static_cast<std::uint32_t>(m_HoveredEntity)});
#endif
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
        Rbs::ColliderCapture::RecordSelection(m_HoveredEntity, m_CameraSnapshot ? &*m_CameraSnapshot : nullptr, x, y,
            Vec2f{GetInputManager()->GetViewState().m_Mouse.GetPosition().x, GetInputManager()->GetViewState().m_Mouse.GetPosition().y});
#endif
        if(SandboxEnabled())
            Log::GetCoreLogger()->info("Viewport pick: {}, {}; candidate {} (selection pending click/drag release)",
                x,y,picked ? picked.GetName() : "None");
        else Log::GetCoreLogger()->info("Mouse Position: {}, {}; Entity {} has been clicked", x, y,
                                       m_HoveredEntity ? m_HoveredEntity.GetName() : "None");
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
    if(m_CameraPilot && (m_CameraPilot->navigation.m_ViewportWidth!=width || m_CameraPilot->navigation.m_ViewportHeight!=height))
        m_CameraPilot->navigation.SetViewportSize(static_cast<float>(width),static_cast<float>(height));
}
