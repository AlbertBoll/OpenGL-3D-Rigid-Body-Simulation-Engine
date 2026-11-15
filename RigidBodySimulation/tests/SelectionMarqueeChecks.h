#pragma once
#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "RbsEditorState.h"
#include <cstdio>
#include <limits>
namespace Rbs {
struct SelectionMarqueeChecks {
static int Run() {
    using namespace ::GEngine;using namespace ::GEngine::Component;using namespace Rbs::Editor;
    namespace B=Rbs::Editor::BulkPhysics;
#define SM(x) do{if(!(x)){std::fprintf(stderr,"SELECTION_BATCH_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
    std::setvbuf(stdout,nullptr,_IONBF,0);Log::Initialize();
    std::puts("SELECTION_BATCH_BEGIN");
    for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
        LaunchConfig launch;launch.m_Backend=backend;auto owned=std::make_unique<RigidBodySimulationApp>(launch);auto& app=*owned;
        app.m_ActiveScene=CreateRefPtr<_Scene>();auto& scene=*app.m_ActiveScene;SM(scene.ConfigurePhysicsBackend(backend));app.m_Backend=backend;app.m_ActiveSelection.scene=RbsScenePreset::SphereLattice;
        std::vector<_Entity> entities;std::vector<EntityRenderId> rows;
        for(int i=0;i<180;++i) {
            auto e=scene.CreateEntity(i%3?"Duplicate":"Sphere "+std::to_string(i));SM(e);e->Transform().SetTranslation({float(i*3),10,0});
            RigidBody3DComponent rb;rb.Type=BodyType::Dynamic;e->AddComponent<RigidBody3DComponent>(rb);e->AddComponent<SphereFixture3DComponent>();
            e->AddComponent<PresetIdentity>(PresetIdentity{(std::uint64_t(SphereLatticeSphereRole)<<32)|std::uint64_t(i+1),"fixture"});
            entities.push_back(*e);rows.push_back(*scene.RenderData().Identify(*e));SM(app.IsSphereLatticeEntity(rows.back()));
        }
        auto unrelated=scene.CreateEntity("wood_sphere999");SM(unrelated);unrelated->AddComponent<RigidBody3DComponent>();unrelated->AddComponent<SphereFixture3DComponent>();
        unrelated->AddComponent<PresetIdentity>(PresetIdentity{(std::uint64_t(21)<<32)|1,"prototype-role"});
        SM(!app.IsSphereLatticeEntity(*scene.RenderData().Identify(*unrelated)));SM(scene.DestroyEntity(*unrelated));
        SM(scene.OnRuntimeStart());scene.SetPaused(true);auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();
        // Real first launch has generation zero, before any Reset/scene switch.
        app.ProcessJoltSettingsCommand();SM(app.m_JoltSettingsEditor.generation==0 && app.m_JoltSettingsEditor.bodyBaseline.size()==180);
        auto& selected=app.m_EditorSelection;
        // An object mouse-down is tentative. Both drag and Esc retain the prior
        // set; only a below-threshold release commits the original stable pick.
        SM(selected.Select(scene.RenderData(),rows[0],false));
        const auto armClick=[&](EntityRenderId id,bool add=false,bool toggle=false) {
            app.m_Marquee.armed=true;app.m_Marquee.clicked=id;
            app.m_Marquee.generation=app.m_SceneGeneration;
            app.m_Marquee.add=add;app.m_Marquee.toggle=toggle;
        };
        armClick(rows[1]);SM(selected.count==1 && selected.ids[0]==rows[0]);
        app.CancelViewportMarquee();SM(selected.count==1 && selected.ids[0]==rows[0] && !app.m_Marquee.armed);
        armClick(rows[1]);app.CommitViewportClick();SM(selected.count==1 && selected.ids[0]==rows[1] && selected.anchor==rows[1]);
        armClick(rows[2],false,true);app.CommitViewportClick();SM(selected.count==2 && selected.anchor==rows[2]);
        armClick(rows[1],false,true);app.CommitViewportClick();SM(selected.count==1 && selected.ids[0]==rows[2]);
        armClick(rows[3],true);app.CommitViewportClick();SM(selected.count==2);
        armClick({},true);app.CommitViewportClick();SM(selected.count==2);
        armClick(rows[0]);++app.m_Marquee.generation;app.CommitViewportClick();SM(selected.count==2);
        auto expired=rows[0];++expired.generation;armClick(expired);app.CommitViewportClick();SM(selected.count==2);
        armClick({});app.CommitViewportClick();SM(selected.count==0);
        SM(scene.GetPhysicsSystem()->GetCompletedSteps()==0);
        SM(selected.Select(scene.RenderData(),rows[0],false));SM(selected.SelectRange(scene.RenderData(),rows,rows[179],false));
        SM(selected.count==180 && selected.anchor==rows[0] && selected.ids[179]==rows[179]);
        SM(selected.SelectRange(scene.RenderData(),rows,rows[63],false) && selected.count==64);
        SM(selected.SelectRange(scene.RenderData(),rows,rows[64],true) && selected.count==65);
        SM(selected.SelectSet(scene.RenderData(),rows,EntitySelection::SetOperation::Replace) && selected.count==180);
        app.SynchronizeEditorSelection();SM(app.m_ColliderOutlines.count==180);
        SM(!app.TransformToolDisabled(RigidBodySimulationApp::TransformTool::Translate));SM(selected.count==180 && app.m_GroupTransform.preview.count==0);
        std::array toggle{rows[0],rows[179],rows[179]};SM(selected.SelectSet(scene.RenderData(),toggle,EntitySelection::SetOperation::Toggle) && selected.count==178);
        SM(selected.SelectSet(scene.RenderData(),toggle,EntitySelection::SetOperation::Add) && selected.count==180);
        std::vector<EntityRenderId> filtered{rows[10],rows[20],rows[170]};
        SM(selected.Select(scene.RenderData(),rows[10],false));SM(selected.SelectRange(scene.RenderData(),filtered,rows[170],false) && selected.count==3);
        SM(selected.SelectRange(scene.RenderData(),filtered,rows[10],true) && selected.count==3 && selected.ids[2]==rows[10]);
        _Scene foreign;auto other=foreign.CreateEntity("Duplicate");auto stale=*foreign.RenderData().Identify(*other);const auto old=selected;
        SM(!selected.SelectSet(scene.RenderData(),std::span(&stale,1),EntitySelection::SetOperation::Replace) && selected.ids==old.ids && selected.count==old.count);
        std::vector<PhysicsBodyPropertyEdit> edits;
        for(auto e:entities) {auto h=*scene.GetBodyBinding(e);auto p=*world.ReadBodyProperties(h);edits.push_back({h,p,p});}
        for(std::size_t count:{1u,64u,65u,180u}) {
            for(auto& e:edits){e.before=*world.ReadBodyProperties(e.body);e.after=e.before;e.after.material.restitution=.375f;e.after.material.friction=.625f;}
            SM(world.UpdateBodyPropertiesBatch(std::span(edits).first(count)));
            for(std::size_t i=0;i<edits.size();++i)SM(*world.ReadBodyProperties(edits[i].body)==(i<count?edits[i].after:edits[i].before));
            for(auto& e:edits){e.before=*world.ReadBodyProperties(e.body);e.after=e.before;e.after.material.restitution=.5f;e.after.material.friction=.5f;}
            SM(world.UpdateBodyPropertiesBatch(edits));
        }
        for(auto& e:edits){e.before=*world.ReadBodyProperties(e.body);e.after=e.before;e.after.material.restitution=.25f;}
        const auto oldState=*world.ReadBody(edits[0].body);edits.back().after.material.restitution=std::numeric_limits<float>::quiet_NaN();
        SM(!world.UpdateBodyPropertiesBatch(edits));for(auto& e:edits)SM(*world.ReadBodyProperties(e.body)==e.before);
        SM(world.ReadBody(edits[0].body)->position==oldState.position && world.ReadBody(edits[0].body)->linearVelocity==oldState.linearVelocity);
        edits.back().after.material.restitution=.25f;auto duplicate=edits;duplicate.back().body=duplicate[0].body;
        SM(!world.UpdateBodyPropertiesBatch(duplicate));for(auto& e:edits)SM(*world.ReadBodyProperties(e.body)==e.before);
        auto staleBatch=edits;staleBatch.back().before.material.friction=.8f;SM(!world.UpdateBodyPropertiesBatch(staleBatch));
        for(auto& e:edits)SM(*world.ReadBodyProperties(e.body)==e.before);
        auto special=*world.ReadBodyProperties(edits.back().body);special.type=BodyType::Static;SM(world.UpdateBodyProperties(edits.back().body,special));
        for(auto& e:edits){e.before=*world.ReadBodyProperties(e.body);e.after=e.before;e.after.linearDamping=.2f;}
        SM(!world.UpdateBodyPropertiesBatch(edits));for(auto& e:edits)SM(*world.ReadBodyProperties(e.body)==e.before);
        special.type=BodyType::Dynamic;SM(world.UpdateBodyProperties(edits.back().body,special));
        for(auto& e:edits){e.before=*world.ReadBodyProperties(e.body);e.after=e.before;}
        if(backend==PhysicsBackend::Jolt) {
            for(auto& e:edits){e.after.linearDamping=.1f;e.after.angularDamping=.2f;e.after.gravityFactor=.75f;e.after.continuousCollision=false;e.after.allowSleeping=false;e.after.velocityStepsOverride=16;e.after.positionStepsOverride=4;}
            SM(world.UpdateBodyPropertiesBatch(edits));for(auto& e:edits)SM(*world.ReadBodyProperties(e.body)==e.after);
        } else {edits.back().after.gravityFactor=.75f;SM(!world.UpdateBodyPropertiesBatch(edits));for(auto& e:edits)SM(*world.ReadBodyProperties(e.body)==e.before);}
        SM(selected.SelectSet(scene.RenderData(),rows,EntitySelection::SetOperation::Replace));app.SynchronizeEditorSelection();app.RefreshBulkBodyProperties();
        SM(app.m_BulkBodyEditor.bodies.size()==180);auto& ui=app.m_BulkBodyEditor;ui.draft.material.restitution=.625f;ui.fields=B::Restitution;ui.queued=true;app.ProcessBulkBodyProperties();
        SM(ui.readbackCount==180 && ui.fields==0);for(auto e:entities)SM(world.ReadBodyProperties(*scene.GetBodyBinding(e))->material.restitution==.625f);
        ui.fields=ui.supported;ui.resetBaseline=true;ui.queued=true;app.ProcessBulkBodyProperties();
        SM(ui.readbackCount==180);for(const auto& [h,baseline]:app.m_JoltSettingsEditor.bodyBaseline)SM(*world.ReadBodyProperties(h)==baseline);
        ui.draft.material.restitution=.625f;ui.fields=B::Restitution;ui.queued=true;app.ProcessBulkBodyProperties();SM(ui.readbackCount==180);
        SM(scene.GetPhysicsSystem()->GetCompletedSteps()==0);
        auto p=*world.ReadBodyProperties(edits[0].body);p.material.friction=.75f;SM(world.UpdateBodyProperties(edits[0].body,p));app.RefreshBulkBodyProperties();SM(ui.mixed&B::Friction);
        ui.draft.material.restitution=.4f;ui.fields=B::Restitution;ui.queued=true;++app.m_SelectionRevision;app.ProcessBulkBodyProperties();
        for(auto e:entities)SM(world.ReadBodyProperties(*scene.GetBodyBinding(e))->material.restitution==.625f);
        SM(scene.DestroyEntity(entities.back()));SM(!selected.SelectSet(scene.RenderData(),rows,EntitySelection::SetOperation::Replace));selected.Prune(scene.RenderData());SM(selected.count==179);
        std::printf("SELECTION_BATCH_PASS backend=%s counts=1,64,65,180 range_union_toggle=1 duplicates_identity=1 filtered_anchor=1 stale_atomic=1 complete_property_readback=180 invalid_last_rollback=1 mixed_static_rejected=1 no_steps=1 transform_limit_separate=1\n",PhysicsBackendName(backend).data());
    }
    return 0;
#undef SM
}
};
}
