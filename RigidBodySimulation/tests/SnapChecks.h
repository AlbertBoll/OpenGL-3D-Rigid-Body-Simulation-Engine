#pragma once
#include "RigidBodySimulation.h"
#include "RbsSnapSettings.h"
#include "Physics/PhysicsSystem.h"
#include <cstdio>
#include <thread>

namespace Rbs
{
struct SnapChecks
{
    static int Run()
    {
        using namespace ::GEngine;using namespace ::GEngine::Component;using namespace Rbs::Editor;
#define SNAP_CHECK(x) do{if(!(x)){std::fprintf(stderr,"SNAP_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
        for(float step:{.1f,.25f,.5f,1.f,1.3f})for(float v:{-123.4f,-.05f,0.f,.49f,999.f}) {
            auto value=SnapGrid({v,v,v},step);SNAP_CHECK(value);
            SNAP_CHECK(std::abs(double(value->x)/step-std::round(double(value->x)/step))<.001);
        }
        SNAP_CHECK(!SnapGrid({0,0,0},0)&&!SnapGrid({0,0,0},-1)&&!SnapGrid({NAN,0,0},1));
        const auto create=[](PhysicsWorld& world,const CollisionShapeDesc& desc,Vec3f position,BodyType type=BodyType::Dynamic,
                             Quat rotation=Quat(1,0,0,0),Vec3f scale=Vec3f(1)) {
            auto shape=CreateCollisionShape(desc,scale);if(!shape)return PhysicsBodyResult<PhysicsBodyHandle>{std::unexpected(PhysicsBodyError{PhysicsBodyErrorCode::InvalidShape,{ },"test fixture"})};
            PhysicsBodyDesc body;body.position=position;body.orientation=rotation;body.type=type;return world.CreateBody(body,*shape);
        };
        const auto approximately=[](float a,float b){return std::abs(a-b)<.002f;};
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
            for(int kind=0;kind<4;++kind) {
                PhysicsWorld world;SNAP_CHECK(world.ConfigureBackend(backend));
                SNAP_CHECK(create(world,BoxShapeDesc{{20,.5f,20}},{0,-.5f,0},BodyType::Static));
                CollisionShapeDesc shape=BoxShapeDesc{};float height=.5f;
                if(kind==1){shape=SphereShapeDesc{1};height=1;}
                if(kind==2){shape=CapsuleShapeDesc{.5f,1};height=1.5f;}
                if(kind==3){shape=ConvexHullShapeDesc{{{1,0,0},{-1,0,0},{0,2,0},{0,-2,0},{0,0,1},{0,0,-1}}};height=2;}
                auto body=create(world,shape,{0,6,0});SNAP_CHECK(body);const auto before=*world.ReadBody(*body);
                PhysicsPlacementPose pose{*body,{0,3,0}};
                SNAP_CHECK(world.QueryEditorPlacement({&pose,1},{}));
                PhysicsPlacementSettings options;options.surface=true;options.rayOrigin={0,10,0};options.rayDelta={0,-30,0};
                auto placed=world.QueryEditorPlacement({&pose,1},options);
                if(backend==PhysicsBackend::Native)SNAP_CHECK(!placed && placed.error().code==PhysicsBodyErrorCode::UnsupportedFeature);
                else {
                    if(!placed)std::fprintf(stderr,"surface kind=%d %.*s\n",kind,int(placed.error().operation.size()),placed.error().operation.data());
                    SNAP_CHECK(placed && approximately(placed->poses[0].position.y,height+.0002f));
                    options.offset=.125f;placed=world.QueryEditorPlacement({&pose,1},options);SNAP_CHECK(placed && approximately(placed->poses[0].position.y,height+.1252f));
                    options.offset=-.2f;SNAP_CHECK(!world.QueryEditorPlacement({&pose,1},options));
                    options.offset=0;options.rayOrigin.x=40;SNAP_CHECK(!world.QueryEditorPlacement({&pose,1},options));
                }
                pose.position.y=.1f;SNAP_CHECK(!world.QueryEditorPlacement({&pose,1},{}));
                options={};options.resolve=true;auto recovered=world.QueryEditorPlacement({&pose,1},options);
                if(backend==PhysicsBackend::Jolt)SNAP_CHECK(recovered && approximately(recovered->poses[0].position.y,height+.0002f));
                else SNAP_CHECK(!recovered && recovered.error().code==PhysicsBodyErrorCode::UnsupportedFeature);
                options.maxCorrection=.01f;SNAP_CHECK(!world.QueryEditorPlacement({&pose,1},options));
                const auto after=*world.ReadBody(*body);SNAP_CHECK(before.position==after.position && before.orientation==after.orientation && before.linearVelocity==after.linearVelocity && before.inverseInertiaBody==after.inverseInertiaBody && before.sleeping==after.sleeping);
                pose.position={0,5,0};pose.scale=Vec3f(2);auto scaled=world.QueryEditorPlacement({&pose,1},{});
                SNAP_CHECK(scaled); // Owner-authorized Jolt replacement now admits uniform collider scaling.
                pose.scale.reset();pose.rotation=Quat(1.00002f,0,0,0);
                auto unit=world.QueryEditorPlacement({&pose,1},{});SNAP_CHECK(unit && std::abs(glm::dot(unit->poses[0].rotation,unit->poses[0].rotation)-1.f)<1.e-6f);
                pose.rotation=Quat(2,0,0,0);SNAP_CHECK(!world.QueryEditorPlacement({&pose,1},{}));pose.rotation=Quat(1,0,0,0);
                bool wrongThread=false;std::thread worker([&]{const auto q=world.QueryEditorPlacement({&pose,1},{});wrongThread=!q && q.error().code==PhysicsBodyErrorCode::WrongThread;});worker.join();SNAP_CHECK(wrongThread);
                auto invalid=pose;invalid.position.x=NAN;SNAP_CHECK(!world.QueryEditorPlacement({&invalid,1},{}));
                SNAP_CHECK(world.DestroyBody(*body));SNAP_CHECK(!world.QueryEditorPlacement({&pose,1},{}));
            }
            PhysicsWorld world;SNAP_CHECK(world.ConfigureBackend(backend));
            const auto a=create(world,SphereShapeDesc{.5f},{0,5,0}),b=create(world,SphereShapeDesc{.5f},{3,5,0});SNAP_CHECK(a&&b);
            auto joint=world.CreateDistanceJoint({*a,*b,{},{},3,false});SNAP_CHECK(joint);
            std::array<PhysicsPlacementPose,2> pair{{{*a,{0,7,0}},{*b,{3,7,0}}}};
            SNAP_CHECK(world.QueryEditorPlacement(pair,{}));SNAP_CHECK(!world.QueryEditorPlacement({pair.data(),1},{}));
            pair[1].position.x+=1;SNAP_CHECK(!world.QueryEditorPlacement(pair,{}));pair[1].position.x-=1;
            pair[0].scale=Vec3f(2);SNAP_CHECK(!world.QueryEditorPlacement(pair,{}));pair[0].scale.reset();
            SNAP_CHECK(world.ReadDistanceJoint(*joint));SNAP_CHECK(world.DestroyJoint(*joint));
            pair[1].position=pair[0].position;SNAP_CHECK(!world.QueryEditorPlacement(pair,{}));
            std::printf("SNAP_QUERY_PASS backend=%s geometry=box_sphere_capsule_hull no_live_mutation stale joint_group scale_policy wrong_thread\n",PhysicsBackendName(backend).data());
        }
        for(int terrain=0;terrain<3;++terrain) {
            PhysicsWorld world;SNAP_CHECK(world.ConfigureBackend(PhysicsBackend::Jolt));
            Vec3f normal{0,1,0};Quat q{1,0,0,0};
            if(terrain==1){q=glm::angleAxis(glm::radians(90.f),Vec3f(0,0,1));normal=q*normal;}
            if(terrain==2){q=glm::angleAxis(glm::radians(25.f),Vec3f(0,0,1));normal=q*normal;}
            SNAP_CHECK(create(world,BoxShapeDesc{{20,.5f,20}},-normal*.5f,BodyType::Static,q));
            auto body=create(world,BoxShapeDesc{},normal*7.f,BodyType::Dynamic,Quat(1,0,0,0),{2,1,.5f});SNAP_CHECK(body);
            PhysicsPlacementPose pose{*body,normal*2.f,glm::angleAxis(glm::radians(30.f),Vec3f(0,0,1))};
            PhysicsPlacementSettings options;options.surface=true;options.pivot=pose.position;options.rayOrigin=normal*10.f;options.rayDelta=-normal*30.f;
            auto result=world.QueryEditorPlacement({&pose,1},options);SNAP_CHECK(result && result->poses[0].rotation==pose.rotation);
            auto verify=result->poses[0];SNAP_CHECK(world.QueryEditorPlacement({&verify,1},{}));
            options.alignNormal=true;result=world.QueryEditorPlacement({&pose,1},options);SNAP_CHECK(result);
            SNAP_CHECK(glm::length(result->poses[0].rotation*Vec3f(0,1,0)-normal)<.001f);
            std::printf("SNAP_SURFACE_PASS terrain=%d scaled_rotated_box alignment offset exact_recheck\n",terrain);
        }
        {
            PhysicsWorld world;SNAP_CHECK(world.ConfigureBackend(PhysicsBackend::Jolt));
            SNAP_CHECK(create(world,BoxShapeDesc{{20,.5f,20}},{0,-.5f,0},BodyType::Static));
            SNAP_CHECK(create(world,BoxShapeDesc{{.5f,20,20}},{5,0,0},BodyType::Static));
            auto body=create(world,BoxShapeDesc{}, {0,5,0});SNAP_CHECK(body);
            PhysicsPlacementPose pose{*body,{4.4f,.1f,0}};PhysicsPlacementSettings options;options.resolve=true;
            auto failed=world.QueryEditorPlacement({&pose,1},options);SNAP_CHECK(!failed && failed.error().code==PhysicsBodyErrorCode::Penetration);
            SNAP_CHECK(world.ReadBody(*body)->position==Vec3f(0,5,0));
        }
        {
            PhysicsWorld world;SNAP_CHECK(world.ConfigureBackend(PhysicsBackend::Native));
            SNAP_CHECK(create(world,BoxShapeDesc{{80,.5f,80}},{0,-.5f,0},BodyType::Static));
            auto body=create(world,BoxShapeDesc{}, {0,4,0});SNAP_CHECK(body);
            PhysicsPlacementPose pose{*body,{-7,.5f,0}};
            SNAP_CHECK(world.QueryEditorPlacement({&pose,1},{})); // Large tangential support coordinates cannot reject a valid floor contact.
            pose.position.y-=.001f;SNAP_CHECK(!world.QueryEditorPlacement({&pose,1},{}));
        }
        {
            PhysicsWorld world;SNAP_CHECK(world.ConfigureBackend(PhysicsBackend::Native));
            SNAP_CHECK(create(world,BoxShapeDesc{{1,1,1},{10,0,0}},{0,0,0},BodyType::Static));
            const auto sphere=create(world,SphereShapeDesc{.5f},{11.4f,1.4f,0});SNAP_CHECK(sphere);
            PhysicsPlacementPose pose{*sphere,{11.4f,1.4f,0}};
            SNAP_CHECK(world.QueryEditorPlacement({&pose,1},{})); // Old limited axes fail at this offset Box corner; GJK-derived support plane proves clearance.
        }
        {
            PhysicsWorld world;SNAP_CHECK(world.ConfigureBackend(PhysicsBackend::Native));
            SNAP_CHECK(create(world,BoxShapeDesc{{20,.5f,20}},{0,-.5f,0},BodyType::Static));
            const auto box=create(world,BoxShapeDesc{}, {0,.499f,0});SNAP_CHECK(box);
            PhysicsPlacementPose pose{*box,{0,.499f,0},glm::angleAxis(.3f,Vec3f(0,1,0))};
            SNAP_CHECK(!world.QueryEditorPlacement({&pose,1},{}));
            PhysicsPlacementSettings options;options.preserveExistingContacts=true;
            SNAP_CHECK(world.QueryEditorPlacement({&pose,1},options));
            pose.position.y-=.001f;SNAP_CHECK(!world.QueryEditorPlacement({&pose,1},options));
        }
        {
            PhysicsWorld world;SNAP_CHECK(world.ConfigureBackend(PhysicsBackend::Jolt));
            const auto a=create(world,BoxShapeDesc{}, {0,5,0}),b=create(world,BoxShapeDesc{}, {.999f,5,0});SNAP_CHECK(a&&b);
            std::array<PhysicsPlacementPose,2> poses{{{*a,{0,7,0}},{*b,{.999f,7,0}}}};
            SNAP_CHECK(!world.QueryEditorPlacement(poses,{}));
            PhysicsPlacementSettings options;options.preserveExistingContacts=true;
            SNAP_CHECK(world.QueryEditorPlacement(poses,options)); // Same selected pair, no deeper contact.
            poses[1].position.x-=.01f;SNAP_CHECK(!world.QueryEditorPlacement(poses,options));
            poses[1].position.x=.999f;poses[0].scale=Vec3f(1.1f);SNAP_CHECK(!world.QueryEditorPlacement(poses,options));
            std::puts("SNAP_JOLT_EXISTING_PAIR_PASS unchanged_contact deeper_rejected scale_rejected");
        }
        std::puts("SNAP_QUERY_CHECK_PASS grid bounds surface_floor_wall_slope multiple_contacts bounded_failure unchanged_world");
        return Integration();
#undef SNAP_CHECK
    }
    static int Integration()
    {
        using namespace ::GEngine;using namespace ::GEngine::Component;using namespace Rbs::Editor;
#define SNAP_APP(x) do{if(!(x)){std::fprintf(stderr,"SNAP_APP_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
        using Action=TransformSession::Action;
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
            LaunchConfig launch;launch.scene=RbsScenePreset::BoxStack;RigidBodySimulationApp app(launch);
            app.m_ActiveScene=CreateRefPtr<_Scene>();auto& scene=*app.m_ActiveScene;
            SNAP_APP(scene.ConfigurePhysicsBackend(backend));app.m_Backend=backend;app.m_SceneGeneration=1;
            app.m_GroupTransform.Reset(1);app.m_SimulationState=SimulationState::Paused;
            std::array<_Entity,2> entities;
            for(int i=0;i<2;++i){auto e=scene.CreateEntity("Snap body");SNAP_APP(e);entities[i]=*e;
                e->Transform().SetTranslation({float(i*4),5,0});e->AddComponent<RigidBody3DComponent>();e->AddComponent<BoxFixture3DComponent>();}
            SNAP_APP(scene.OnRuntimeStart());scene.SetPaused(true);auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();
            world.SetGravity({0,0,0});
            TransformBatch batch;batch.generation=1;batch.Resize(2);
            for(int i=0;i<2;++i){auto& e=batch.edits[i];e.entity=*scene.RenderData().Identify(entities[i]);e.body=*scene.GetBodyBinding(entities[i]);
                e.before=*scene.ReadEditorTransform(e.entity);e.after=e.before;e.after.translation.y+=2;}
            auto& session=app.m_GroupTransform;session.proposal=batch;session.queryPending=true;session.proposedPivot=glm::translate(Mat4(1),Vec3f(2,7,0));
            app.ProcessGroupTransform();SNAP_APP(session.dirty&&!session.invalid);
            SNAP_APP(world.ReadBody(batch.edits[0].body)->position==batch.edits[0].before.translation);
            session.pending=Action::Cancel;app.ProcessGroupTransform();SNAP_APP(!session.dirty&&session.size==0);
            session.proposal=batch;session.queryPending=true;app.ProcessGroupTransform();session.pending=Action::Apply;app.ProcessGroupTransform();
            SNAP_APP(session.size==1&&session.cursor==1);
            for(const auto& e:batch.Span())SNAP_APP(*scene.ReadEditorTransform(e.entity)==e.after);
            session.pending=Action::Undo;app.ProcessGroupTransform();SNAP_APP(session.cursor==0);
            for(const auto& e:batch.Span())SNAP_APP(*scene.ReadEditorTransform(e.entity)==e.before);
            session.pending=Action::Redo;app.ProcessGroupTransform();SNAP_APP(session.cursor==1);
            // A new overlap cannot partially commit or enter history.
            auto invalid=batch;for(auto& e:std::span(invalid.edits.data(),invalid.count))e.before=e.after;
            invalid.edits[1].after.translation=invalid.edits[0].after.translation;
            session.proposal=invalid;session.queryPending=true;app.ProcessGroupTransform();SNAP_APP(session.invalid);
            session.pending=Action::Apply;app.ProcessGroupTransform();SNAP_APP(session.size==1);
            for(const auto& e:batch.Span())SNAP_APP(*scene.ReadEditorTransform(e.entity)==e.after);
            app.CancelGroupTransform();
            // Recheck at Apply: obstacle introduced after a valid preview.
            auto next=batch;for(auto& e:std::span(next.edits.data(),next.count)){e.before=e.after;e.after.translation.z+=4;}
            session.proposal=next;session.queryPending=true;app.ProcessGroupTransform();SNAP_APP(!session.invalid);
            auto shape=CreateCollisionShape(BoxShapeDesc{});SNAP_APP(shape);PhysicsBodyDesc obstacle;obstacle.type=BodyType::Static;obstacle.position=next.edits[1].after.translation;
            auto obstruction=world.CreateBody(obstacle,*shape);SNAP_APP(obstruction);
            session.pending=Action::Apply;app.ProcessGroupTransform();SNAP_APP(session.size==1);
            for(const auto& e:batch.Span())SNAP_APP(*scene.ReadEditorTransform(e.entity)==e.after);
            SNAP_APP(world.DestroyBody(*obstruction));
            session.pending=Action::Apply;session.resumeAfterApply=true;app.ProcessGroupTransform();SNAP_APP(session.size==2 && app.m_SimulationState==SimulationState::Running);
            scene.GetPhysicsSystem()->Update(Timestep(1./60.));SNAP_APP(!scene.GetPhysicsSystem()->GetBackendFailure());
            for(const auto& e:next.Span())SNAP_APP(glm::length(world.ReadBody(e.body)->position-e.after.translation)<.001f);
            app.SetSimulationState(SimulationState::Paused);
            ++app.m_SceneGeneration;session.pending=Action::Undo;app.ProcessGroupTransform();SNAP_APP(session.size==0);
            scene.OnRuntimeStop();
            std::printf("SNAP_APP_PASS backend=%s deferred_preview cancel atomic_apply undo_redo recheck_obstacle apply_resume no_impulse stale_generation\n",PhysicsBackendName(backend).data());
        }
        return 0;
#undef SNAP_APP
    }
};
}
