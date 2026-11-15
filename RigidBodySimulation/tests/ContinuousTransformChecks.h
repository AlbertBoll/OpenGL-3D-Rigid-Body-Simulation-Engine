#pragma once
#include "RigidBodySimulation.h"
#include "Physics/PhysicsSystem.h"
#include <chrono>
#include <cstdio>

namespace Rbs
{
struct ContinuousTransformChecks
{
    static int Run()
    {
        std::setvbuf(stdout,nullptr,_IONBF,0);
        using namespace ::GEngine;using namespace ::GEngine::Component;using namespace Rbs::Editor;
        using Tool=RigidBodySimulationApp::TransformTool;using Mode=RigidBodySimulationApp::EditorMode;using Action=TransformSession::Action;
#define CT(x) do{if(!(x)){std::fprintf(stderr,"CONTINUOUS_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
            LaunchConfig launch;launch.scene=RbsScenePreset::BoxStack;RigidBodySimulationApp app(launch);
            app.m_ActiveScene=CreateRefPtr<_Scene>();auto& scene=*app.m_ActiveScene;
            CT(scene.ConfigurePhysicsBackend(backend));app.m_Backend=backend;app.m_SceneGeneration=1;app.m_GroupTransform.Reset(1);
            std::array<_Entity,3> entities;
            for(int i=0;i<3;++i){auto e=scene.CreateEntity("Gesture");CT(e);entities[i]=*e;e->Transform().SetTranslation({float(i*30),10,0});
                RigidBody3DComponent body;body.Type=BodyType::Dynamic;e->AddComponent<RigidBody3DComponent>(body);e->AddComponent<BoxFixture3DComponent>();}
            CT(scene.OnRuntimeStart());scene.SetPaused(true);auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();world.SetGravity({0,0,0});
            for(int i=0;i<2;++i)CT(app.m_EditorSelection.Select(scene.RenderData(),*scene.RenderData().Identify(entities[i]),i!=0));
            app.m_HoveredEntity=entities[1];auto& session=app.m_GroupTransform;
            auto candidate=[&](){TransformBatch batch;batch.generation=1;batch.Resize(2);
                for(int i=0;i<2;++i){auto& e=batch.edits[i];e.entity=*scene.RenderData().Identify(entities[i]);e.body=*scene.GetBodyBinding(entities[i]);e.before=*scene.ReadEditorTransform(e.entity);e.after=e.before;}
                return batch;};
            auto propose=[&](const TransformBatch& batch){session.proposal=batch;session.queryPending=true;session.placement={};app.ProcessGroupTransform();};
            auto commit=[&](const TransformBatch& batch){propose(batch);app.FinishTransformGesture();app.ProcessGroupTransform();};
            // 75 distinct completed gestures exceed history capacity without limiting editing.
            for(int step=0;step<75;++step) {
                const auto tool=step%3==0?Tool::Rotate:step%3==1?Tool::Translate:Tool::Scale;CT(app.ActivateTransformTool(tool));
                auto batch=candidate();for(auto& e:std::span(batch.edits.data(),batch.count)) {
                    if(tool==Tool::Rotate)e.after.rotation=glm::normalize(glm::angleAxis(.035f,Vec3f(0,1,0))*e.before.rotation);
                    if(tool==Tool::Translate)e.after.translation.x+=1;
                    if(tool==Tool::Scale)e.after.scale*=1.01f;
                }
                propose(batch);CT(!session.invalid);app.FinishTransformGesture();
                // Switching tools after mouse release must not cancel the released commit.
                CT(app.ActivateTransformTool(Tool::Translate));app.ProcessGroupTransform();
                for(const auto& e:batch.Span())CT(glm::length(scene.ReadEditorTransform(e.entity)->translation-e.after.translation)<1e-5f && scene.ReadEditorTransform(e.entity)->scale==e.after.scale);
                CT(!session.dirty && session.cursor==std::min(std::size_t(step+1),session.HistoryCapacity));
            }
            const auto retained=candidate();CT(retained.edits[0].before.translation.x==25);
            session.pending=Action::Undo;app.ProcessGroupTransform();CT(session.cursor==31);
            session.pending=Action::Redo;app.ProcessGroupTransform();CT(session.cursor==32);
            for(const auto& e:retained.Span())CT(*scene.ReadEditorTransform(e.entity)==e.before);
            auto bad=retained;bad.edits[1].after.translation=bad.edits[0].after.translation;commit(bad);
            CT(session.cursor==32 && !session.dirty);for(const auto& e:retained.Span())CT(*scene.ReadEditorTransform(e.entity)==e.before);
            auto cancel=retained;for(auto& e:std::span(cancel.edits.data(),cancel.count))e.after.translation.y+=2;
            propose(cancel);CT(!session.invalid);session.pending=Action::Cancel;app.ProcessGroupTransform();
            for(const auto& e:retained.Span())CT(*scene.ReadEditorTransform(e.entity)==e.before);
            // Live preview does not teleport bodies; all bodies continue moving.
            app.SetEditorMode(Mode::Simulation);CT(!app.TransformEditingAllowed());app.m_LiveEdit=true;app.SetSimulationState(SimulationState::Running);
            for(int i=0;i<3;++i)CT(world.SetBodyVelocity(*scene.GetBodyBinding(entities[i]),{.25f,0,0},{0,.1f,0}));
            auto live=candidate();for(auto& e:std::span(live.edits.data(),live.count))e.after.translation.z+=4;
            session.live=true;propose(live);CT(!session.invalid);
            const auto bystander=*world.ReadBody(*scene.GetBodyBinding(entities[2]));
            scene.GetPhysicsSystem()->Update(Timestep(1./60.));CT(!scene.GetPhysicsSystem()->GetBackendFailure());
            CT(world.ReadBody(bystander.handle)->position.x>bystander.position.x);
            const auto precommit=*world.ReadBody(live.edits[0].body);CT(precommit.position.z==live.edits[0].before.translation.z);
            app.FinishTransformGesture();app.ProcessGroupTransform();CT(!session.dirty);
            auto actual=*world.ReadBody(live.edits[0].body);CT(actual.position==live.edits[0].after.translation && actual.linearVelocity==precommit.linearVelocity && actual.angularVelocity==precommit.angularVelocity && actual.type==precommit.type);
            CT(app.m_SimulationState==SimulationState::Running);
            auto liveScale=candidate();for(auto& e:std::span(liveScale.edits.data(),liveScale.count)) {
                e.after.scale*=1.1f;e.after.rotation=glm::normalize(glm::angleAxis(.1f,Vec3f(0,1,0))*e.before.rotation);
            }
            const auto oldBounds=world.ReadBody(actual.handle)->localBounds;
            session.live=true;propose(liveScale);CT(!session.invalid);
            CT(world.ReadBody(actual.handle)->localBounds.maxs==oldBounds.maxs);
            scene.GetPhysicsSystem()->Update(Timestep(1./60.));
            const auto scaleVelocity=world.ReadBody(actual.handle)->linearVelocity;
            app.FinishTransformGesture();app.ProcessGroupTransform();CT(!session.dirty);
            actual=*world.ReadBody(actual.handle);
            CT(glm::length(actual.localBounds.maxs-oldBounds.maxs*1.1f)<1e-4f && actual.linearVelocity==scaleVelocity);
            CT(*scene.ReadEditorTransform(liveScale.edits[0].entity)==liveScale.edits[0].after);
            const auto cursor=session.cursor;session.pending=Action::Undo;app.ProcessGroupTransform();CT(session.cursor==cursor && world.ReadBody(actual.handle)->position==actual.position);
            scene.GetPhysicsSystem()->Update(Timestep(1./60.));app.SetEditorMode(Mode::Edit);CT(app.m_SimulationState==SimulationState::Paused);
            const auto evolved=*world.ReadBody(actual.handle);session.pending=Action::Undo;app.ProcessGroupTransform();CT(session.cursor==cursor && world.ReadBody(actual.handle)->position==evolved.position);
            const auto start=std::chrono::steady_clock::now();for(int i=0;i<10000;++i)app.ProcessGroupTransform();
            const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/10000;
            std::printf("CONTINUOUS_PASS backend=%s gestures=75 history=32 atomic_failure esc switch_after_release running_rebase running_scale_rotation velocity_preserved bystander_moves history_no_time_rewind idle_ns=%.1f\n",PhysicsBackendName(backend).data(),ns);
            ++app.m_SceneGeneration;session.pending=Action::Undo;app.ProcessGroupTransform();CT(session.size==0);scene.OnRuntimeStop();
        }
        for(auto type:{BodyType::Static,BodyType::Dynamic,BodyType::Kinematic})for(int kind=0;kind<5;++kind) {
            _Scene scene;CT(scene.ConfigurePhysicsBackend(PhysicsBackend::Jolt));auto e=scene.CreateEntity("Scale matrix");CT(e);
            const float initialScale=kind==0?2.f:1.f;e->Transform().SetScale(Vec3f(initialScale));
            e->Transform().SetTranslation({0,20,0});RigidBody3DComponent body;body.Type=type;e->AddComponent<RigidBody3DComponent>(body);
            if(kind==0)e->AddComponent<BoxFixture3DComponent>();
            if(kind==1)e->AddComponent<SphereFixture3DComponent>();
            if(kind==2)e->AddComponent<CapsuleFixture3DComponent>();
            if(kind>=3) {ConvexFixture3DComponent fixture;
                if(kind==3)for(int i=0;i<24;++i){float a=glm::two_pi<float>()*float(i)/24;fixture.Shape.points.push_back({std::cos(a),-1,std::sin(a)});fixture.Shape.points.push_back({std::cos(a),1,std::sin(a)});}
                else fixture.Shape.points={{1,0,0},{-1,0,0},{0,2,0},{0,-2,0},{0,0,1},{0,0,-1}};
                e->AddComponent<ConvexFixture3DComponent>(fixture);}
            CT(scene.OnRuntimeStart());auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();world.SetGravity({0,0,0});
            const auto id=*scene.RenderData().Identify(*e);const auto handle=*scene.GetBodyBinding(*e);const auto initial=*world.ReadBody(handle);
            for(float scale:{2.f,3.f,1.f}) {
                EditorTransformEdit edit;edit.entity=id;edit.body=handle;edit.before=*scene.ReadEditorTransform(id);edit.after=edit.before;edit.after.scale=Vec3f(scale);
                CT(scene.ApplyEditorTransforms({&edit,1}));const auto actual=*world.ReadBody(handle);
                CT(actual.handle==initial.handle && actual.type==initial.type && actual.inverseMass==initial.inverseMass && actual.material==initial.material);
                CT(glm::length(actual.localBounds.mins-initial.localBounds.mins*(scale/initialScale))<1e-4f && glm::length(actual.localBounds.maxs-initial.localBounds.maxs*(scale/initialScale))<1e-4f);
                if(type==BodyType::Dynamic)for(int i=0;i<3;++i)CT(glm::length(actual.inverseInertiaBody[i]-initial.inverseInertiaBody[i]*(initialScale*initialScale)/(scale*scale))<.005f);
                CT(e->Transform().Scale==Vec3f(scale));scene.GetPhysicsSystem()->Update(Timestep(1./60.));CT(!scene.GetPhysicsSystem()->GetBackendFailure());
                CT(world.ReadBody(handle)->position==initial.position);
            }
            EditorTransformEdit pose;pose.entity=id;pose.body=handle;pose.before=*scene.ReadEditorTransform(id);pose.after=pose.before;pose.after.translation.x+=3;pose.after.rotation=glm::angleAxis(.5f,Vec3f(0,1,0));
            CT(scene.ApplyEditorTransforms({&pose,1}));scene.GetPhysicsSystem()->Update(Timestep(1./60.));CT(glm::length(world.ReadBody(handle)->position-pose.after.translation)<.001f);
            auto nonuniform=pose;nonuniform.before=*scene.ReadEditorTransform(id);nonuniform.after=nonuniform.before;nonuniform.after.scale={2,1,3};
            const auto changed=scene.ApplyEditorTransforms({&nonuniform,1});CT(bool(changed)==(kind!=1&&kind!=2));
            if(!changed)CT(*scene.ReadEditorTransform(id)==nonuniform.before);
            if(changed)for(float factor:{1.1f,.9f}) {
                EditorTransformEdit group;group.entity=id;group.body=handle;group.before=*scene.ReadEditorTransform(id);group.after=group.before;group.after.scale*=factor;
                const auto before=*world.ReadBody(handle);
                PhysicsPlacementPose candidate{handle,group.after.translation,group.after.rotation,group.after.scale,group.before.scale};
                CT(world.QueryEditorPlacement({&candidate,1},{}));CT(scene.ApplyEditorTransforms({&group,1}));
                const auto after=*world.ReadBody(handle);CT(after.handle==handle && after.inverseMass==before.inverseMass && after.material==before.material);
                CT(glm::length(after.localBounds.mins-before.localBounds.mins*factor)<1e-4f && glm::length(after.localBounds.maxs-before.localBounds.maxs*factor)<1e-4f);
            }
            std::printf("JOLT_SCALE_PASS type=%d shape=%d absolute_scale mass inertia pose step identity nonuniform_policy\n",int(type),kind);scene.OnRuntimeStop();
        }
        return 0;
#undef CT
    }
};
}
