#pragma once
#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <cstdlib>

namespace Rbs
{
    // Opt-in bounded driver of the exact panel request and existing transaction.
    // No alternate fixture, application lifecycle or solver observer is installed.
    struct BackendSwitchChecks
    {
        using App=RigidBodySimulationApp;
        using Backend=GEngine::PhysicsBackend;
        using Handle=GEngine::PhysicsBodyHandle;
        enum class Stage { Initial, NativeRun, FailurePending, JoltPending, JoltRun, IncompatiblePending, SleepPending, NativePending, NativeReturn, ResetPending, Done };
        inline static Stage s_Stage=Stage::Initial;
        inline static bool s_Corrections=[] { std::size_t count{};
            getenv_s(&count,nullptr,0,"GENGINE_RBS_OWNER_CORRECTIONS");return count>1; }();
        inline static std::optional<GEngine::PhysicsBodyProperties> s_Properties;
        inline static GEngine::ShapeType s_Shape{};
        inline static std::weak_ptr<GEngine::_Scene> s_Previous;
        inline static GEngine::_Scene* s_OldScene{};
        inline static Handle s_OldBody;
        inline static GEngine::PhysicsJointHandle s_OldJoint;
        inline static std::optional<GEngine::PhysicsBodyState> s_OldState;
        inline static GEngine::SceneBodyPose s_Authored;
        inline static SceneSelection s_Selection;
        inline static entt::entity s_Entity{entt::null};
        inline static std::vector<std::pair<entt::entity,GEngine::SceneBodyPose>> s_AuthoredBodies;
        inline static std::size_t s_Bodies{},s_Joints{};
        inline static unsigned s_Panels{},s_TotalTicks{};
        inline static std::uint64_t s_LastSteps{};
        static void PanelDrawn(){++s_Panels;}
        inline static unsigned s_Layouts{};
        static void InspectLayout(App& app)
        {
            if(ImGui::GetFrameCount()<3)return;
            for(const char* name:{"Hierarchy","Properties","World Settings","Content Browser / Diagnostics","Viewport"})
            {
                const auto* window=ImGui::FindWindowByName(name);
                if(!Require(app,window && window->Active && window->Size.x>0 && window->Size.y>0,
                    "Required compact Editor region absent"))return;
            }
            for(const char* name:{"Scene Control","Body Transform","Material authoring","Custom shader reload"})
            {
                const auto* window=ImGui::FindWindowByName(name);
                if(!Require(app,!window || !window->Active,"Legacy feature window escaped compact Editor"))return;
            }
            ++s_Layouts;
        }
        static bool Require(App& app,bool condition,const char* text)
        {
            if(condition)return true;
            app.FailRuntime({GEngine::ApplicationRuntimeErrorCode::SubsystemFailure,"Physics backend UI","check","bounded transaction", "",text});
            return false;
        }
        static bool Remember(App& app)
        {
            auto* world=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            s_Previous=app.m_ActiveScene;s_OldScene=app.m_ActiveScene.get();
            s_Bodies=world->GetBodyCount();s_Joints=world->GetJointCount();s_Selection=app.m_ActiveSelection;
            GEngine::_Entity entity{s_Entity,app.m_ActiveScene.get()};auto pose=app.m_ActiveScene->ReadBodyTransform(entity);
            if(!Require(app,bool(pose),"Authored body unavailable"))return false;
            s_AuthoredBodies.clear();
            for(auto id:app.m_ActiveScene->GetAllEntitiesWith<GEngine::Component::RigidBody3DComponent>())
            {
                const auto authored=app.m_ActiveScene->ReadBodyTransform(GEngine::_Entity{id,app.m_ActiveScene.get()});
                if(!Require(app,bool(authored),"Missing authored body pose"))return false;
                s_AuthoredBodies.emplace_back(id,authored->authored);
            }
            s_Authored=pose->authored;s_OldBody=pose->body;s_OldState=*world->ReadBody(s_OldBody);
            if(s_Corrections){s_Properties=*world->ReadBodyProperties(s_OldBody);s_Shape=s_OldState->shapeType;}
            s_OldJoint=s_Joints?*world->JointAt(0):GEngine::PhysicsJointHandle{};
            s_LastSteps=app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps();return true;
        }
        static bool CheckCommitted(App& app,Backend backend)
        {
            auto* physics=app.m_ActiveScene->GetPhysicsSystem();auto* world=physics->GetPhysicsWorld();
            if(!Require(app,s_Previous.expired() && app.m_Backend==backend && world->GetBackend()==backend &&
                app.m_ActiveSelection==s_Selection && physics->GetCompletedSteps()==0 && app.m_SimulationState==SimulationState::Ready &&
                world->GetBodyCount()==s_Bodies && world->GetJointCount()==s_Joints && !world->ReadBody(s_OldBody) &&
                (!s_OldJoint || !world->ReadDistanceJoint(s_OldJoint)) && !app.m_ChainImpulse.Pending() && app.m_ColliderOutlines.count==0,
                "Backend transaction identity/configuration/tick reset failed"))return false;
            GEngine::_Entity entity{s_Entity,app.m_ActiveScene.get()};auto pose=app.m_ActiveScene->ReadBodyTransform(entity);
            if(!Require(app,pose && pose->authored.translation==s_Authored.translation && pose->authored.rotation==s_Authored.rotation &&
                world->ReadBody(pose->body)->position==s_Authored.translation,"Authored pose was replaced by simulated state"))return false;
            if(s_Corrections && s_Stage!=Stage::ResetPending)
                if(!Require(app,*world->ReadBodyProperties(pose->body)==*s_Properties && world->ReadBody(pose->body)->shapeType==s_Shape,
                    "Applied collider/body settings lost in backend rebuild"))return false;
            for(const auto& [id,authored]:s_AuthoredBodies)
            {
                const auto mapped=app.m_ActiveScene->ReadBodyTransform(GEngine::_Entity{id,app.m_ActiveScene.get()});
                if(!Require(app,mapped && mapped->authored.translation==authored.translation &&
                    mapped->authored.rotation==authored.rotation && world->ReadBody(mapped->body)->position==authored.translation,
                    "Authored body mapping lost a duplicate-name fixture"))return false;
            }
            GEngine::Log::GetCoreLogger()->info("BACKEND_SWITCH_COMMIT backend={} scene={} links={} ticks=0 old_handles=rejected",
                GEngine::PhysicsBackendName(backend),SceneName(s_Selection.scene),s_Selection.chainLinks);
            s_LastSteps=0;return true;
        }
        static bool Boundary(App& app)
        {
            if(s_Stage==Stage::Initial)
            {
                auto* world=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
                for(auto id:app.m_ActiveScene->GetAllEntitiesWith<GEngine::Component::RigidBody3DComponent>())
                {
                    GEngine::_Entity entity{id,app.m_ActiveScene.get()};auto body=app.m_ActiveScene->GetBodyBinding(entity);
                    if(body && world->ReadBody(*body)->type==GEngine::Component::BodyType::Dynamic){s_Entity=id;break;}
                }
                if(!Require(app,s_Entity!=entt::null,"No authored dynamic body"))return false;
                if(s_Corrections)
                {
                    GEngine::_Entity entity{s_Entity,app.m_ActiveScene.get()};auto h=app.m_ActiveScene->GetBodyBinding(entity);
                    app.SelectColliderEntity(entity,false);app.SelectColliderEntity(entity,true);app.SelectColliderEntity(entity,false);
                    auto p=*world->ReadBodyProperties(*h);p.mass=2;p.material.friction=.3f;
                    app.m_BodyPropertyEditor={*h,entity,app.m_SceneGeneration,p,true,true};
                    app.ProcessBodyPropertyCommand();
                    if(!Require(app,*world->ReadBodyProperties(*h)==p,"Queued Rigidbody Apply failed"))return false;
                    const GEngine::CollisionShapeDesc shape=GEngine::CapsuleShapeDesc{.2f,.25f};
                    auto report=app.m_ActiveScene->ReplaceBodyColliderIfNoAddedOverlap(entity,shape);
                    if(!report.committed && GEngine::PhysicsSystem::CanConfirmColliderRisk(report))
                    {auto consent=app.m_ActiveScene->GetPhysicsSystem()->PrepareColliderConfirmation(report);
                        if(consent)report=app.m_ActiveScene->ReplaceBodyColliderConfirmed(entity,shape,*consent);}
                    if(!Require(app,report.committed && world->ReadBody(*h)->shapeType==GEngine::ShapeType::Capsule,
                        "Applied collider edit before switch failed"))return false;
                }
                app.SetSimulationState(SimulationState::Running);s_Stage=Stage::NativeRun;
            }
            else if(s_Stage==Stage::FailurePending)
            {
                auto* physics=app.m_ActiveScene->GetPhysicsSystem();auto* world=physics->GetPhysicsWorld();
                auto state=world->ReadBody(s_OldBody);
                if(!Require(app,app.m_ActiveScene.get()==s_OldScene && !s_Previous.expired() && app.m_Backend==Backend::Native &&
                    app.m_SimulationState==SimulationState::Paused && physics->GetCompletedSteps()==s_LastSteps &&
                    state && state->position==s_OldState->position && state->linearVelocity==s_OldState->linearVelocity &&
                    world->GetBodyCount()==s_Bodies && world->GetJointCount()==s_Joints &&
                    (!s_OldJoint || bool(world->ReadDistanceJoint(s_OldJoint))) && !app.m_ChainImpulse.Pending() && app.m_ColliderOutlines.count==0 &&
                    !app.m_InjectSceneConstructionFailure && !app.m_SceneDiagnostic.empty(),"Injected failure did not preserve the previous world"))return false;
                GEngine::Log::GetCoreLogger()->info("BACKEND_SWITCH_ROLLBACK_PASS ticks={} old_handles=valid queued_impulse=cleared",s_LastSteps);
                if(!Require(app,app.RequestBackend(Backend::Jolt),"Jolt Apply rejected after rollback"))return false;
                s_Stage=Stage::JoltPending;
            }
            else if(s_Stage==Stage::JoltPending)
            {
                if(app.m_SceneRequest)return true; // Request just queued by the prior boundary.
                if(!CheckCommitted(app,Backend::Jolt))return false;
                app.SetSimulationState(SimulationState::Running);s_Stage=Stage::JoltRun;
            }
            else if(s_Stage==Stage::IncompatiblePending)
            {
                auto* world=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
                if(!Require(app,app.m_ActiveScene.get()==s_OldScene && app.m_Backend==Backend::Jolt &&
                    world->ReadBody(s_OldBody) && world->ReadBodyProperties(s_OldBody)->gravityFactor==.5f &&
                    app.m_SimulationState==SimulationState::Paused,"Incompatible settings did not rollback on Jolt"))return false;
                auto p=*world->ReadBodyProperties(s_OldBody);p.gravityFactor=1;
                if(!Require(app,bool(world->UpdateBodyProperties(s_OldBody,p)),"Revert incompatible setting"))return false;
                GEngine::Log::GetCoreLogger()->info("EDITOR_INCOMPATIBLE_SWITCH_ROLLBACK_PASS backend=Jolt body=valid");
                app.m_WorldSleepEditor={app.m_SceneGeneration,{.045f,.8f},true,true};
                app.ProcessWorldSleepCommand();
                if(!Require(app,*world->ReadWorldSleepSettings()==GEngine::PhysicsWorldSleepSettings{.045f,.8f},"Queued world sleep Apply/readback"))return false;
                if(!Remember(app) || !Require(app,app.RequestBackend(Backend::Native),"Compatible retry"))return false;
                s_Stage=Stage::SleepPending;
            }
            else if(s_Stage==Stage::SleepPending)
            {
                auto* world=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
                if(!Require(app,app.m_ActiveScene.get()==s_OldScene && app.m_Backend==Backend::Jolt &&
                    world->ReadBody(s_OldBody) && (!s_OldJoint || world->ReadDistanceJoint(s_OldJoint)) &&
                    *world->ReadWorldSleepSettings()==GEngine::PhysicsWorldSleepSettings{.045f,.8f},"World sleep override switch rollback"))return false;
                app.m_WorldSleepEditor={app.m_SceneGeneration,{},true,true};app.ProcessWorldSleepCommand();
                if(!Require(app,*world->ReadWorldSleepSettings()==GEngine::PhysicsWorldSleepSettings{},"Sleep Defaults readback"))return false;
                GEngine::Log::GetCoreLogger()->info("EDITOR_SLEEP_SWITCH_ROLLBACK_PASS backend=Jolt body=valid joint=valid defaults=restored");
                if(!Remember(app) || !Require(app,app.RequestBackend(Backend::Native),"Sleep defaults compatible retry"))return false;
                s_Stage=Stage::NativePending;
            }
            else if(s_Stage==Stage::NativePending)
            {
                if(!CheckCommitted(app,Backend::Native))return false;
                app.SetSimulationState(SimulationState::Running);s_Stage=Stage::NativeReturn;
            }
            else if(s_Stage==Stage::ResetPending)
            {
                if(!CheckCommitted(app,Backend::Native) || !Require(app,s_Panels>0 && s_Layouts>0 && s_TotalTicks==60,"UI rendering/tick budget mismatch"))return false;
                GEngine::Log::GetCoreLogger()->info("BACKEND_SWITCH_CHECKS_PASS scene={} links={} total_ticks={} panels={} layout_regions=pass rollback=pass reset=pass",
                    SceneName(s_Selection.scene),s_Selection.chainLinks,s_TotalTicks,s_Panels);
                s_Stage=Stage::Done;app.ShutDown();return false;
            }
            return true;
        }
        static bool AfterTick(App& app)
        {
            auto* physics=app.m_ActiveScene->GetPhysicsSystem();const auto steps=physics->GetCompletedSteps();
            if(steps>s_LastSteps)s_TotalTicks+=unsigned(steps-s_LastSteps);
            s_LastSteps=steps;
            if(!Require(app,s_TotalTicks<=120,"Exceeded bounded switch tick budget"))return false;
            if(steps!=20)return true;
            if(s_Stage==Stage::NativeRun)
            {
                if(!Remember(app))return false;
                app.m_ColliderOutlines.Select(s_OldBody,false);
                if(app.m_ActiveSelection.IsDistanceScene())
                    if(!Require(app,app.m_ChainImpulse.Request(*physics->GetPhysicsWorld(),.5f,DistanceChainImpulseControl::Direction::PositiveX),"Impulse queue setup failed"))return false;
                app.m_InjectSceneConstructionFailure=true;
                if(!Require(app,app.RequestBackend(Backend::Jolt) && app.m_ActiveScene.get()==s_OldScene,"Apply mutated the active scene immediately"))return false;
                s_Stage=Stage::FailurePending;
            }
            else if(s_Stage==Stage::JoltRun)
            {
                if(s_Corrections)
                {
                    auto* w=physics->GetPhysicsWorld();auto h=app.m_ActiveScene->GetBodyBinding(GEngine::_Entity{s_Entity,app.m_ActiveScene.get()});
                    auto p=*w->ReadBodyProperties(*h);p.gravityFactor=.5f;
                    if(!Require(app,bool(w->UpdateBodyProperties(*h,p)),"Jolt gravity edit"))return false;
                }
                if(!Remember(app) || !Require(app,app.RequestBackend(Backend::Native),"Native Apply rejected"))return false;
                s_Stage=s_Corrections?Stage::IncompatiblePending:Stage::NativePending;
            }
            else if(s_Stage==Stage::NativeReturn)
            {
                if(!Remember(app) || !Require(app,app.RequestScene(app.m_ActiveSelection),"Existing Reset rejected"))return false;
                s_Stage=Stage::ResetPending;
            }
            return true;
        }
    };
}
