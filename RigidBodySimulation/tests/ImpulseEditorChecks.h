#pragma once
#include "RigidBodySimulation.h"
#include "RbsEditorState.h"
#include <chrono>
#include <cstdio>
#include <sdl2/SDL_stdinc.h>

namespace Rbs {
struct ImpulseEditorChecks {
    static int Run() {
        using namespace ::GEngine;using namespace ::GEngine::Component;using namespace Editor;
#define IC(x) do {if(!(x)){std::fprintf(stderr,"IMPULSE_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
        const auto same=[](const Vec3f& a,const Vec3f& b){return glm::length(a-b)<2.e-4f;};
        std::setvbuf(stdout,nullptr,_IONBF,0);
        ImpulseParameters parameters;IC(!parameters.Validate());
        for(auto direction:{Vec3f(1,0,0),Vec3f(0,1,0),Vec3f(0,0,1),Vec3f(2,3,4)}) {
            PhysicsImpulseMotion m;m.orientation=glm::angleAxis(glm::half_pi<float>(),Vec3f(0,0,1));parameters.direction=direction;parameters.magnitude=2;
            const auto unit=glm::normalize(direction);IC(same(parameters.Command({},m).linear,2.f*unit));
            parameters.local=true;IC(same(parameters.Command({},m).linear,2.f*(m.orientation*unit)));parameters.local=false;
        }
        parameters.direction={};IC(parameters.Validate());parameters.direction={INFINITY,0,0};IC(parameters.Validate());parameters={};parameters.magnitude=-1;IC(parameters.Validate());
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
            for(int kind=0;kind<5;++kind)for(float mass:{1.f,4.f})for(auto mode:{PhysicsImpulseMode::Impulse,PhysicsImpulseMode::VelocityChange}) {
                PhysicsWorld world;IC(world.ConfigureBackend(backend));CollisionShapeDesc shapeDesc=SphereShapeDesc{.75f};
                if(kind==1)shapeDesc=BoxShapeDesc{Vec3f(.7f)};
                if(kind==2)shapeDesc=CapsuleShapeDesc{.5f,1};
                if(kind>=3) {
                    ConvexHullShapeDesc hull;
                    if(kind==3)for(int i=0;i<16;++i){const float a=glm::two_pi<float>()*i/16;hull.points.push_back({std::cos(a),-1,std::sin(a)});hull.points.push_back({std::cos(a),1,std::sin(a)});}
                    else hull.points={{1,0,0},{-1,0,0},{0,2,0},{0,-1,0},{0,0,1},{0,0,-1}};
                    shapeDesc=std::move(hull);
                }
                auto shape=CreateCollisionShape(shapeDesc);IC(shape);PhysicsBodyDesc desc;desc.mass=mass;desc.position={2,4,6};desc.orientation=glm::angleAxis(.6f,glm::normalize(Vec3f(1,2,3)));
                auto handle=world.CreateBody(desc,*shape);IC(handle);const auto initial=*world.ReadBody(*handle);const auto props=*world.ReadBodyProperties(*handle);
                ImpulseParameters p;p.magnitude=.3f;p.mode=mode;p.direction={2,0,0};p.angularEnabled=true;p.angular={0,0,.1f};p.customPoint=true;p.pointOffset={0,.25f,0};
                const auto before=*world.ReadImpulseMotion(*handle);auto c=p.Command(*handle,before);
                auto result=world.ApplyImpulseBatch({&c,1});if(!result)std::fprintf(stderr,"DETAIL %s\n",std::string(result.error().operation).c_str());IC(result);
                const auto& r=(*result)->bodies.front();const auto after=*world.ReadImpulseMotion(*handle);
                const auto wanted=Vec3f(.3f,0,0)*(mode==PhysicsImpulseMode::Impulse?1.f/mass:1.f);
                IC(same(after.linear,wanted));IC(same(after.angular,before.inverseInertia*(glm::cross(p.pointOffset,r.impulse)+r.angularImpulse)));
                IC(world.ReadBody(*handle)->position==initial.position && *world.ReadBodyProperties(*handle)==props);
                IC(world.ReadBody(*handle)->localBounds.maxs==initial.localBounds.maxs);
                // Pure COM action cannot introduce angular velocity.
                c={*handle,{0,.2f,0}};const auto angular=after.angular;IC(world.ApplyImpulseBatch({&c,1}));IC(same(world.ReadImpulseMotion(*handle)->angular,angular));
            }
            for(std::size_t count:{1u,180u,512u}) {
                PhysicsWorld world;IC(world.ConfigureBackend(backend));std::vector<PhysicsImpulseCommand> commands;
                for(std::size_t i=0;i<count;++i) {
                    auto shape=CreateCollisionShape(SphereShapeDesc{.5f});IC(shape);PhysicsBodyDesc desc;desc.position={float(i)*3,10,0};desc.mass=1.f+float(i%4);
                    auto h=world.CreateBody(desc,*shape);IC(h);commands.push_back({*h,{.5f,0,0},{},{},PhysicsImpulseMode::VelocityChange});
                }
                const auto start=std::chrono::steady_clock::now();auto result=world.ApplyImpulseBatch(commands);IC(result && (*result)->bodies.size()==count);
                const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                for(const auto& c:commands)IC(same(world.ReadImpulseMotion(c.body)->linear,{.5f,0,0}));
                const auto untouched=[&](){for(const auto& c:commands)if(!same(world.ReadImpulseMotion(c.body)->linear,{.5f,0,0}))return false;return true;};
                auto invalid=commands;invalid.back().linear.x=INFINITY;IC(!world.ApplyImpulseBatch(invalid)&&untouched());
                invalid=commands;++invalid.back().body.generation;IC(!world.ApplyImpulseBatch(invalid)&&untouched());
                invalid=commands;invalid.back().angular={0,0,100};IC(!world.ApplyImpulseBatch(invalid)&&untouched());
                if(count>1){invalid=commands;invalid.back().body=invalid.front().body;IC(!world.ApplyImpulseBatch(invalid)&&untouched());}
                for(auto type:{BodyType::Static,BodyType::Kinematic}) {
                    auto shape=CreateCollisionShape(SphereShapeDesc{});PhysicsBodyDesc desc;desc.type=type;auto fixed=world.CreateBody(desc,*shape);IC(fixed);
                    invalid=commands;invalid.back().body=*fixed;IC(!world.ApplyImpulseBatch(invalid)&&untouched());
                }
                if(count>1) {
                    auto joint=world.CreateDistanceJoint({commands[0].body,commands[1].body,{},{},3,false});IC(joint);
                    IC(world.ApplyImpulseBatch({commands.data(),1}));IC(world.ReadDistanceJoint(*joint));
                    IC(world.ApplyImpulseBatch(commands));IC(world.ReadDistanceJoint(*joint));
                }
                std::printf("IMPULSE_BATCH_PASS backend=%s size=%zu ms=%.6f complete_readback mixed_types_stale_duplicate_nonfinite_speed_limit atomic_no_partial connected_dynamic\n",PhysicsBackendName(backend).data(),count,elapsed);
            }
            // Exercise actual Editor deferred queue, saved identity resolution and history separation.
            LaunchConfig launch;launch.scene=RbsScenePreset::BoxStack;RigidBodySimulationApp app(launch);app.m_Backend=backend;
            const auto replace=[&](PhysicsBackend b) {
                app.m_ActiveScene=CreateRefPtr<_Scene>();auto& scene=*app.m_ActiveScene;
                if(!scene.ConfigurePhysicsBackend(b))return false;app.m_Backend=b;++app.m_SceneGeneration;
                app.m_EditorSelection.Clear();app.m_GroupTransform.Reset(app.m_SceneGeneration);
                for(int i=0;i<3;++i) {
                    auto e=scene.CreateEntity("Duplicate");if(!e)return false;
                    e->Transform().SetTranslation({float(i*4),20,0});RigidBody3DComponent body;body.Type=BodyType::Dynamic;e->AddComponent<RigidBody3DComponent>(body);e->AddComponent<SphereFixture3DComponent>();
                    e->AddComponent<PresetIdentity>(PresetIdentity{std::uint64_t(i+1),"authored-impulse-test"});
                }
                if(!scene.OnRuntimeStart())return false;scene.SetPaused(true);scene.GetPhysicsSystem()->GetPhysicsWorld()->SetGravity({0,0,0});
                return app.ExecuteSelectionCommand(SelectionCommand::Scene);
            };
            IC(replace(backend));auto first=app.m_EditorSelection.ids.front();
            IC(app.SaveImpulseExperiment());IC(app.QueueImpulse());IC(!app.QueueImpulse());app.ProcessImpulseCommand();
            IC(app.m_Impulse.recordCount==1 && app.m_Impulse.records[0]->results.size()==3);app.ProcessImpulseCommand();IC(app.m_Impulse.recordCount==1);
            IC(app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()==0 && app.m_GroupTransform.size==0);
            IC(app.QueueImpulse());IC(replace(backend));app.ProcessImpulseCommand();IC(app.m_Impulse.recordCount==1);
            IC(app.QueueImpulse(true));app.ProcessImpulseCommand();IC(app.m_Impulse.recordCount==2);
            IC(app.m_Impulse.records[1]->targets.front().entity.registry!=first.registry);
            IC(replace(backend==PhysicsBackend::Native?PhysicsBackend::Jolt:PhysicsBackend::Native));
            IC(app.QueueImpulse(true));app.ProcessImpulseCommand();IC(app.m_Impulse.recordCount==3);
            const auto id=app.m_ActiveScene->RenderData().Resolve(app.m_EditorSelection.ids.front());IC(id);
            _Entity{*id,app.m_ActiveScene.get()}.GetComponent<PresetIdentity>().signature="changed";
            IC(!app.QueueImpulse(true));
            app.SetEditorMode(RigidBodySimulationApp::EditorMode::Simulation);app.SetSimulationState(SimulationState::Running);
            IC(app.QueueImpulse());const auto tick=app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps();app.ProcessImpulseCommand();
            IC(app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()==tick);
            app.m_ActiveScene->GetPhysicsSystem()->Update(Timestep(1./60.));IC(app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()>tick);
            IC(!app.m_ActiveScene->GetPhysicsSystem()->GetBackendFailure());
            for(int i=0;i<10;++i){IC(app.QueueImpulse());app.ProcessImpulseCommand();}
            IC(app.m_Impulse.recordCount==8 && app.m_Impulse.Bytes()<=ImpulseEditor::RecordBytes);
            std::printf("IMPULSE_EDITOR_PASS backend=%s world_local_xyz_custom shapes_mass COM_offcenter angular queue_once reset_repeat signature_rejection backend_replacement running_boundary records_bounded no_transform_history\n",PhysicsBackendName(backend).data());
        }
        return 0;
#undef IC
    }
    // Opt-in actual-preset integration; enumerate real Dynamic bodies, no preset-specific action.
    static void PresetBoundary(RigidBodySimulationApp& app) {
        using namespace ::GEngine;using namespace ::GEngine::Component;
        static const bool enabled=SDL_getenv("GENGINE_IMPULSE_PRESET_CHECK")!=nullptr;static bool done=false;
        if(!enabled || done || !app.m_ActiveScene || !app.m_Launch.m_BackendSmokeTicks)return;done=true;
        auto& scene=*app.m_ActiveScene;auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();
        std::vector<EntityRenderId> ids;
        for(auto e:scene.GetAllEntitiesWith<RigidBody3DComponent>()) {
            const auto handle=scene.GetBodyBinding(_Entity{e,&scene});
            if(handle && world.ReadBodyProperties(*handle)->type==BodyType::Dynamic)ids.push_back(*scene.RenderData().Identify(_Entity{e,&scene}));
        }
        const auto fail=[&](){std::fprintf(stderr,"IMPULSE_PRESET_FAIL selected=%zu records=%zu detail=%s\n",app.m_EditorSelection.count,app.m_Impulse.recordCount,app.m_Impulse.diagnostic.c_str());std::abort();};
        const auto tick=scene.GetPhysicsSystem()->GetCompletedSteps(),joints=world.GetJointCount();
        if(ids.empty()) {
            // A visual gallery can contain only Static fixtures. Do not fabricate
            // Dynamic targets or modify authored motion types to pass this test.
            for(std::size_t i=0;i<world.GetBodyCount();++i) {
                const auto h=world.BodyAt(i);if(!h)fail();const auto before=world.ReadBody(*h);if(!before)fail();
                PhysicsImpulseCommand c{*h,{0,.15f,0}};if(world.ApplyImpulseBatch({&c,1}))fail();
                const auto after=world.ReadBody(*h);if(!after||after->linearVelocity!=before->linearVelocity||after->angularVelocity!=before->angularVelocity)fail();
            }
            std::printf("IMPULSE_PRESET_PASS backend=%s scene=%s dynamic=0 no_eligible_targets static_rejection bodies=%zu no_added_step\n",PhysicsBackendName(app.m_Backend).data(),SceneName(app.m_ActiveSelection.scene),world.GetBodyCount());std::fflush(stdout);return;
        }
        app.m_Impulse.parameters.direction={0,1,0};app.m_Impulse.parameters.magnitude=.15f;
        if(!app.m_EditorSelection.SelectSet(scene.RenderData(),{ids.data(),1},Editor::EntitySelection::SetOperation::Replace))fail();app.SynchronizeEditorSelection();
        if(!app.QueueImpulse())fail();app.ProcessImpulseCommand();
        if(app.m_Impulse.recordCount!=1)fail();
        if(!app.m_EditorSelection.SelectSet(scene.RenderData(),ids,Editor::EntitySelection::SetOperation::Replace))fail();app.SynchronizeEditorSelection();
        if(!app.QueueImpulse())fail();app.ProcessImpulseCommand();app.ProcessImpulseCommand();
        if(app.m_Impulse.recordCount!=2 || app.m_Impulse.records[1]->results.size()!=ids.size() || scene.GetPhysicsSystem()->GetCompletedSteps()!=tick || world.GetJointCount()!=joints)fail();
        std::printf("IMPULSE_PRESET_PASS backend=%s scene=%s dynamic=%zu joints=%zu one_and_batch tick=%llu no_added_step\n",PhysicsBackendName(app.m_Backend).data(),SceneName(app.m_ActiveSelection.scene),ids.size(),joints,static_cast<unsigned long long>(tick));std::fflush(stdout);
    }
};
}
