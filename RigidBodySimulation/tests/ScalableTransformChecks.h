#pragma once
#include "RigidBodySimulation.h"
#include "RbsEditorWidgets.h"
#include "OwnerReviewProbe.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sdl2/SDL_stdinc.h>

namespace Rbs {
struct ScalableTransformChecks {
    template<class F> static void Measure(const char* backend,std::size_t size,const char* action,F&& fn) {
#ifdef GENGINE_OWNER_REVIEW_PROBE
        const auto a=ReviewMetrics::allocations,b=ReviewMetrics::bytes;ReviewMetrics::counting=true;
#endif
        const auto start=std::chrono::steady_clock::now();fn();
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
#ifdef GENGINE_OWNER_REVIEW_PROBE
        ReviewMetrics::counting=false;
        std::printf("GROUP_METRIC backend=%s size=%zu action=%s ms=%.6f allocations=%llu bytes=%llu\n",backend,size,action,ms,ReviewMetrics::allocations-a,ReviewMetrics::bytes-b);
#else
        std::printf("GROUP_METRIC backend=%s size=%zu action=%s ms=%.6f allocations=unmeasured bytes=unmeasured\n",backend,size,action,ms);
#endif
    }
    static int Run() {
        using namespace ::GEngine;using namespace ::GEngine::Component;using namespace Editor;
        using Tool=RigidBodySimulationApp::TransformTool;using Action=TransformSession::Action;
        std::setvbuf(stdout,nullptr,_IONBF,0);
#define SG(x) do{if(!(x)){std::fprintf(stderr,"SCALABLE_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
        const auto closeEnough=[](const auto& a,const auto& b){return glm::length(a-b)<.002f;};
        std::size_t maximum=4096;if(const auto* value=SDL_getenv("GENGINE_SCALABLE_MAX"))maximum=std::size_t(std::atoi(value));
        int samples=0;if(const auto* value=SDL_getenv("GENGINE_SCALABLE_DRAG_SAMPLES"))samples=std::clamp(std::atoi(value),0,120);
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt})for(std::size_t count:{1u,64u,65u,180u,181u,256u,512u,1024u,4096u}) {
            if(count>maximum)continue;
            LaunchConfig launch;RigidBodySimulationApp app(launch);app.m_ActiveScene=CreateRefPtr<_Scene>();auto& scene=*app.m_ActiveScene;
            SG(scene.ConfigurePhysicsBackend(backend));app.m_Backend=backend;app.m_SceneGeneration=1;app.m_GroupTransform.Reset(1);
            std::vector<_Entity> entities;std::vector<EntityRenderId> ids;
            for(std::size_t i=0;i<count;++i) {
                auto e=scene.CreateEntity(i%2?"Duplicate":"Body "+std::to_string(i));SG(e);entities.push_back(*e);
                e->Transform().SetTranslation({float(i%16)*8,100+float((i/16)%16)*8,float(i/256)*8});
                e->Transform().SetRotation(glm::angleAxis(.03f*float(i%7),glm::normalize(Vec3f(1,2,3))));
                RigidBody3DComponent b;b.Type=i%17==0?BodyType::Static:i%17==1?BodyType::Kinematic:BodyType::Dynamic;
                e->AddComponent<RigidBody3DComponent>(b);e->AddComponent<SphereFixture3DComponent>();ids.push_back(*scene.RenderData().Identify(*e));
            }
            SG(scene.OnRuntimeStart());scene.SetPaused(true);auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();world.SetGravity({0,0,0});
            bool ok=false;const auto name=backend==PhysicsBackend::Jolt?"Jolt":"Native";
            Measure(name,count,"selection",[&]{ok=app.ExecuteSelectionCommand(SelectionCommand::Scene);});SG(ok && app.m_EditorSelection.count==count && app.m_ColliderOutlines.count==count);
            SG(app.ExecuteSelectionCommand(SelectionCommand::Type,SelectionType::Sphere) && app.m_EditorSelection.count==count);
            SG(app.ExecuteSelectionCommand(SelectionCommand::Invert) && app.m_EditorSelection.count==0);
            SG(app.ExecuteSelectionCommand(SelectionCommand::Scene) && app.m_EditorSelection.count==count);
            auto& session=app.m_GroupTransform;std::vector<PhysicsBodyState> initial;
            for(auto e:entities)initial.push_back(*world.ReadBody(*scene.GetBodyBinding(e)));
            if(SDL_getenv("GENGINE_SCALABLE_EXTERNAL") && count<4096) {
                auto shape=CreateCollisionShape(BoxShapeDesc{},Vec3f(1));SG(shape);PhysicsBodyDesc body;body.type=BodyType::Static;body.position={10000,1000,1000};SG(world.CreateBody(body,*shape));
            }
            TransformPivot=0;TransformOrientation=0;
            for(int tool=1;tool<=3;++tool) {
                SG(app.ActivateTransformTool(Tool(tool)));
                Measure(name,count,"begin",[&]{ok=app.BeginGroupTransform();});SG(ok && session.preview.count==count);
                const auto baseline=session.preview;const auto pivot=session.origin;
                const auto delta=tool==1?glm::translate(Mat4(1),Vec3f(2,3,4)):
                    glm::translate(Mat4(1),Vec3f(pivot[3]))*(tool==2?glm::mat4_cast(glm::angleAxis(.2f,Vec3f(0,1,0))):glm::scale(Mat4(1),Vec3f(1.1f)))*glm::translate(Mat4(1),-Vec3f(pivot[3]));
                Measure(name,count,"preview",[&]{ok=app.StageGroupTransform(delta*pivot,tool==3?1.1f:1);app.ProcessGroupTransform();});
                if(session.invalid)std::fprintf(stderr,"DIAGNOSTIC %s\n",app.m_PropertyDiagnostic.c_str());SG(ok && !session.invalid);
                for(int sample=0;sample<samples;++sample) {
                    const auto factor=tool==3?1.1f+float(sample%7)*.001f:1.f;
                    const auto varied=tool==3?pivot*glm::scale(Mat4(1),Vec3f(factor)):
                        glm::translate(Mat4(1),Vec3f(0,float(sample%7)*.001f,0))*delta*pivot;
                    Measure(name,count,tool==1?"drag_T":tool==2?"drag_R":"drag_S",[&]{ok=app.StageGroupTransform(varied,factor);app.ProcessGroupTransform();});SG(ok&&!session.invalid);
                }
                if(samples){SG(app.StageGroupTransform(delta*pivot,tool==3?1.1f:1));app.ProcessGroupTransform();SG(!session.invalid);}
                if(samples)Measure(name,count,"placement_query",[&]{ok=app.ValidateTransformPlacement(session.proposal,{});});SG(ok);
                const auto expected=session.preview;
                for(const auto& e:baseline.Span())SG(*scene.ReadEditorTransform(e.entity)==e.before);
                // Scene preview agrees with the complete candidate, not just active root.
                for(std::size_t i=0;i<count;++i)SG(closeEnough(Vec3f(scene.GetRenderTransform(entities[i])->matrix[3]),expected.edits[i].after.translation));
                app.FinishTransformGesture();Measure(name,count,"commit",[&]{app.ProcessGroupTransform();});
                SG(!session.dirty && session.cursor==std::size_t(tool));
                for(std::size_t i=0;i<count;++i) {
                    const auto actual=*scene.ReadEditorTransform(ids[i]);SG(closeEnough(actual.translation,expected.edits[i].after.translation) && actual.scale==expected.edits[i].after.scale);
                    const auto body=*world.ReadBody(initial[i].handle);SG(body.type==initial[i].type && body.inverseMass==initial[i].inverseMass && body.material==initial[i].material);
                    SG(body.linearVelocity==initial[i].linearVelocity && body.angularVelocity==initial[i].angularVelocity);
                    SG(closeEnough(body.localBounds.maxs,initial[i].localBounds.maxs*actual.scale));
                }
                Measure(name,count,"undo",[&]{session.pending=Action::Undo;app.ProcessGroupTransform();});SG(session.cursor==std::size_t(tool-1));
                for(const auto& e:baseline.Span())SG(closeEnough(scene.ReadEditorTransform(e.entity)->translation,e.before.translation) && scene.ReadEditorTransform(e.entity)->scale==e.before.scale);
                Measure(name,count,"redo",[&]{session.pending=Action::Redo;app.ProcessGroupTransform();});SG(session.cursor==std::size_t(tool));
                for(const auto& e:expected.Span())SG(closeEnough(scene.ReadEditorTransform(e.entity)->translation,e.after.translation));
            }
            // Invalid LAST member cannot publish any part of an otherwise valid group.
            SG(app.ActivateTransformTool(Tool::Translate) && app.BeginGroupTransform());const auto retained=session.preview;const auto cursor=session.cursor;
            SG(app.StageGroupTransform(glm::translate(Mat4(1),Vec3f(1,0,0))*session.origin));
            session.proposal.edits.back().after.translation.x=std::numeric_limits<float>::quiet_NaN();
            app.FinishTransformGesture();app.ProcessGroupTransform();SG(session.cursor==cursor && !session.dirty);
            for(const auto& e:retained.Span())SG(*scene.ReadEditorTransform(e.entity)==e.before);
            auto stale=retained;++stale.edits.back().entity.generation;SG(!scene.ApplyEditorTransforms(stale.Span()));
            auto invalidScale=retained;invalidScale.edits.back().after.scale=Vec3f(-1);SG(!scene.ApplyEditorTransforms(invalidScale.Span()));
            auto rejectedShapes=retained;for(auto& e:rejectedShapes.edits)e.after.scale*=1.01f;
            rejectedShapes.edits.back().after.scale=Vec3f(std::numeric_limits<float>::max());
            SG(!scene.ApplyEditorTransforms(rejectedShapes.Span())); // Last collider fails after earlier replacement shapes were staged.
            for(const auto& e:retained.Span())SG(*scene.ReadEditorTransform(e.entity)==e.before);
            SG(app.BeginGroupTransform());SG(app.StageGroupTransform(glm::translate(Mat4(1),Vec3f(1,0,0))*session.origin));app.ProcessGroupTransform();session.pending=Action::Cancel;app.ProcessGroupTransform();
            for(const auto& e:retained.Span())SG(*scene.ReadEditorTransform(e.entity)==e.before);
            // Shared pivot + Individual Origins and World/Local axes use the real proposal path.
            for(int orientation=0;orientation<2;++orientation)for(int mode=0;mode<3;++mode) {
                TransformOrientation=orientation;TransformPivot=mode;SG(app.ActivateTransformTool(Tool::Rotate) && app.BeginGroupTransform());
                const auto old=session.preview;const auto origin=session.origin;
                if(mode==1)SG(closeEnough(Vec3f(origin[3]),old.edits.back().before.translation));
                const auto target=orientation?origin*glm::mat4_cast(glm::angleAxis(.04f,Vec3f(1,0,0))):
                    glm::translate(Mat4(1),Vec3f(origin[3]))*glm::mat4_cast(glm::angleAxis(.04f,Vec3f(1,0,0)))*glm::translate(Mat4(1),-Vec3f(origin[3]))*origin;
                SG(app.StageGroupTransform(target));app.ProcessGroupTransform();SG(!session.invalid);
                if(mode==2)for(const auto& e:session.preview.Span())SG(closeEnough(e.after.translation,e.before.translation));
                session.pending=Action::Cancel;app.ProcessGroupTransform();
                SG(app.ActivateTransformTool(Tool::Scale) && app.BeginGroupTransform());const auto scaleOrigin=session.origin;
                SG(app.StageGroupTransform(scaleOrigin*glm::scale(Mat4(1),Vec3f(1.01f)),1.01f));app.ProcessGroupTransform();SG(!session.invalid);
                if(mode==2)for(const auto& e:session.preview.Span())SG(closeEnough(e.after.translation,e.before.translation));
                session.pending=Action::Cancel;app.ProcessGroupTransform();
            }
            TransformPivot=TransformOrientation=0;
            if(count==4096) {
                for(int i=0;i<12;++i){SG(app.ActivateTransformTool(Tool::Translate) && app.BeginGroupTransform());SG(app.StageGroupTransform(glm::translate(Mat4(1),Vec3f(0,.1f,0))*session.origin));app.FinishTransformGesture();app.ProcessGroupTransform();SG(!session.dirty);}
                SG(session.HistoryBytes()<=EditorTransformBudget::HistoryBytes && session.size<15);
                auto extra=scene.CreateEntity("Extra scene entity");SG(extra);SG(!app.ExecuteSelectionCommand(SelectionCommand::Scene) && app.m_EditorSelection.count==4096);
            }
            SG(scene.GetPhysicsSystem()->GetCompletedSteps()==0);
            std::printf("SCALABLE_PASS backend=%s size=%zu mixed_motion_types T_R_S preview_atomic invalid_last cancel undo_redo pivots orientations no_steps history_bytes=%zu history_commands=%zu\n",name,count,session.HistoryBytes(),session.size);
            if(!samples && (count==180 || count==512)) {
                auto shape=CreateCollisionShape(SphereShapeDesc{.5f},Vec3f(1));SG(shape);
                PhysicsBodyDesc desc;desc.position={10000,100,0};desc.linearVelocity={1,0,0};auto bystander=world.CreateBody(desc,*shape);SG(bystander);
                const auto beforeBystander=*world.ReadBody(*bystander);
                app.SetEditorMode(RigidBodySimulationApp::EditorMode::Simulation);app.m_LiveEdit=true;app.SetSimulationState(SimulationState::Running);
                SG(app.ActivateTransformTool(Tool::Translate) && app.BeginGroupTransform());session.live=true;
                SG(app.StageGroupTransform(glm::translate(Mat4(1),Vec3f(0,2,0))*session.origin));app.ProcessGroupTransform();SG(!session.invalid);const auto target=session.preview;
                scene.GetPhysicsSystem()->Update(Timestep(1./60.));SG(!scene.GetPhysicsSystem()->GetBackendFailure());
                SG(world.ReadBody(*bystander)->position.x>beforeBystander.position.x);
                app.FinishTransformGesture();app.ProcessGroupTransform();SG(!session.dirty);
                for(const auto& e:target.Span())SG(closeEnough(world.ReadBody(e.body)->position,e.after.translation));
                const auto cursor=session.cursor;session.pending=Action::Undo;app.ProcessGroupTransform();SG(session.cursor==cursor);
                app.SetSimulationState(SimulationState::Paused);app.SetEditorMode(RigidBodySimulationApp::EditorMode::Edit);
                session.pending=Action::Undo;app.ProcessGroupTransform();SG(session.cursor==cursor-1);
                std::printf("SCALABLE_LIVE_PASS backend=%s size=%zu static_dynamic_kinematic atomic_commit bystander_steps running_history_rejected paused_undo\n",name,count);
            }
            ++app.m_SceneGeneration;session.pending=Action::Undo;app.ProcessGroupTransform();SG(session.size==0);scene.OnRuntimeStop();
        }
        // Genuine parents are roots; no fabricated group entity or double transform.
        {
            LaunchConfig launch;RigidBodySimulationApp app(launch);app.m_ActiveScene=CreateRefPtr<_Scene>();auto& scene=*app.m_ActiveScene;app.m_SceneGeneration=1;app.m_GroupTransform.Reset(1);
            auto parent=scene.CreateEntity("Parent"),child=scene.CreateEntity("Duplicate"),peer=scene.CreateEntity("Duplicate");SG(parent&&child&&peer);SG(child->SetParent(*parent));
            child->Transform().SetTranslation({2,0,0});peer->Transform().SetTranslation({0,4,0});
            SG(app.ExecuteSelectionCommand(SelectionCommand::Scene) && app.m_EditorSelection.count==3);
            SG(app.ActivateTransformTool(Tool::Translate) && app.BeginGroupTransform() && app.m_GroupTransform.preview.count==2);
            SG(app.StageGroupTransform(glm::translate(Mat4(1),Vec3f(4,0,0))*app.m_GroupTransform.origin));app.FinishTransformGesture();app.ProcessGroupTransform();
            SG(closeEnough(parent->Transform().Translation,Vec3f(4,0,0)) && closeEnough(child->Transform().Translation,Vec3f(2,0,0)));
            SG(app.ExecuteSelectionCommand(SelectionCommand::Children,SelectionType::Sphere,*scene.RenderData().Identify(*parent)) && app.m_EditorSelection.count==1);
            std::snprintf(app.m_HierarchyFilter.data(),app.m_HierarchyFilter.size(),"Duplicate");SG(app.ExecuteSelectionCommand(SelectionCommand::Filtered) && app.m_EditorSelection.count==2);
            SG(app.ExecuteSelectionCommand(SelectionCommand::Invert) && app.m_EditorSelection.count==1);SG(app.ExecuteSelectionCommand(SelectionCommand::Clear) && !app.m_EditorSelection.count);
        }
        std::puts("SCALABLE_SELECTION_PASS filtered full_scene type children invert deselect duplicate_names parent_roots stale_generation budgets");return 0;
#undef SG
    }
};
}
