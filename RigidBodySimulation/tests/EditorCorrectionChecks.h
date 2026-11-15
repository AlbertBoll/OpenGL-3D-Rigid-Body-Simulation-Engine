#pragma once
#include "Physics/PhysicsSystem.h"
#include "Scene/_Scene.h"
#include "Scene/_Entity.h"
#include "RbsScenePresetPayload.h"
#include "Owner04Checks.h"
#include <cstdio>
#include <limits>

namespace Rbs::EditorCorrectionChecks
{
    using namespace ::GEngine;
    using namespace ::GEngine::Component;
#define EC_CHECK(x) do { if(!(x)){std::fprintf(stderr,"EDITOR_CORRECTION_FAIL line=%d %s\n",__LINE__,#x);return false;} } while(false)
    inline bool Backend(PhysicsBackend backend)
    {
        _Scene scene;EC_CHECK(scene.ConfigurePhysicsBackend(backend));
        auto moving=scene.CreateEntity("editable box"),fixed=scene.CreateEntity("joint anchor");EC_CHECK(moving && fixed);
        for(auto entity:{*moving,*fixed})
        {
            entity.AddOrReplaceComponent<Transform3DComponent>(Vec3f(entity==*moving?0.f:20.f,10,0),Vec3f(0),Vec3f(1));
            RigidBody3DComponent rb;rb.Type=entity==*moving?BodyType::Dynamic:BodyType::Static;
            entity.AddOrReplaceComponent<RigidBody3DComponent>(rb);
            BoxFixture3DComponent fixture;fixture.Shape.halfExtents=Vec3f(2);
            entity.AddOrReplaceComponent<BoxFixture3DComponent>(fixture);
        }
        EC_CHECK(scene.OnRuntimeStart());scene.SetPaused(true);
        auto& system=*scene.GetPhysicsSystem();auto& world=*system.GetPhysicsWorld();world.SetGravity({0,0,0});
        auto h=scene.GetBodyBinding(*moving),anchor=scene.GetBodyBinding(*fixed);EC_CHECK(h && anchor);
        auto joint=world.CreateDistanceJoint({*anchor,*h,{},{},20,false});EC_CHECK(joint);
        const auto worldSleep=world.ReadWorldSleepSettings();
        if(backend==PhysicsBackend::Jolt)
        {
            EC_CHECK(worldSleep && *worldSleep==PhysicsWorldSleepSettings{});
            const PhysicsWorldSleepSettings edited{.04f,.8f};
            EC_CHECK(world.UpdateWorldSleepSettings(edited));
            EC_CHECK(*world.ReadWorldSleepSettings()==edited && world.ReadDistanceJoint(*joint) && world.ReadBody(*h));
            for(const auto invalid:{PhysicsWorldSleepSettings{0,.5f},PhysicsWorldSleepSettings{.03f,-1},
                PhysicsWorldSleepSettings{std::numeric_limits<float>::quiet_NaN(),.5f},
                PhysicsWorldSleepSettings{1,std::numeric_limits<float>::infinity()},
                PhysicsWorldSleepSettings{std::numeric_limits<float>::max(),2}})
            {EC_CHECK(!world.UpdateWorldSleepSettings(invalid));EC_CHECK(*world.ReadWorldSleepSettings()==edited);}
            EC_CHECK(world.UpdateWorldSleepSettings(*worldSleep));
            EC_CHECK(*world.ReadWorldSleepSettings()==*worldSleep);
        }
        else {EC_CHECK(!worldSleep && worldSleep.error().code==PhysicsBodyErrorCode::UnsupportedFeature);EC_CHECK(!world.UpdateWorldSleepSettings({}));}
        auto original=system.ReadBodyCollider(*h);EC_CHECK(original && original->type==ShapeType::Box);
        std::unique_ptr<PhysicalShape> preview;
        auto report=scene.AssessBodyColliderReplacement(*moving,SphereShapeDesc{.25f},&preview);
        EC_CHECK(preview && preview->GetShapeType()==ShapeType::Sphere && report.body==*h && report.complete);
        auto invalid=scene.ReplaceBodyColliderIfNoAddedOverlap(*moving,SphereShapeDesc{-1});
        EC_CHECK(!invalid.committed && system.ReadBodyCollider(*h)->identity==original->identity);
        for(const CollisionShapeDesc desc:{CollisionShapeDesc{SphereShapeDesc{.25f}},
            CollisionShapeDesc{CapsuleShapeDesc{.25f,.5f}},CollisionShapeDesc{BoxShapeDesc{{.3f,.5f,.4f},{.1f,0,0}}},
            CollisionShapeDesc{CapsuleShapeDesc{.25f,0}}})
        {
            report=scene.AssessBodyColliderReplacement(*moving,desc,&preview);EC_CHECK(preview);
            std::array<FrameDebugLine,1024> lines;DebugGeometry geometry(lines);
            EC_CHECK(PhysicsSystem::AppendColliderPreview(geometry,*preview,{},Quat{1,0,0,0},{1,.65f,.15f,1}));
            EC_CHECK(!geometry.Lines().empty());
            auto applied=scene.ReplaceBodyColliderIfNoAddedOverlap(*moving,desc);
            EC_CHECK(applied.committed && (scene.GetBodyBinding(*moving) && *scene.GetBodyBinding(*moving)==*h) && world.ReadDistanceJoint(*joint));
            auto actual=system.ReadBodyCollider(*h);EC_CHECK(actual && actual->type==preview->GetShapeType());
            EC_CHECK(world.ReadBody(*h)->localBounds.mins==preview->GetBounds().mins && world.ReadBody(*h)->localBounds.maxs==preview->GetBounds().maxs);
            system.Update(Timestep(1.0/60.0));EC_CHECK(!system.GetBackendFailure() && world.ReadBody(*h));
        }
        // Large collider must retain confirmation and rollback; stale context cannot authorize it.
        report=scene.ReplaceBodyColliderIfNoAddedOverlap(*moving,SphereShapeDesc{30});
        EC_CHECK(!report.committed && PhysicsSystem::CanConfirmColliderRisk(report));
        auto confirmation=system.PrepareColliderConfirmation(report);EC_CHECK(confirmation);
        EC_CHECK(system.SetBodyPose(*h,{0,11,0},{1,0,0,0}));
        auto stale=scene.ReplaceBodyColliderConfirmed(*moving,SphereShapeDesc{30},*confirmation);
        EC_CHECK(!stale.committed && stale.error==ColliderAdmissionError::StaleConfirmation);
        report=scene.ReplaceBodyColliderIfNoAddedOverlap(*moving,SphereShapeDesc{30});
        confirmation=system.PrepareColliderConfirmation(report);EC_CHECK(confirmation);
        auto forced=scene.ReplaceBodyColliderConfirmed(*moving,SphereShapeDesc{30},*confirmation);
        EC_CHECK(forced.committed && std::get<SphereShapeDesc>(system.ReadBodyCollider(*h)->dimensions).radius==30);
        EC_CHECK(system.ReplaceBodyCollider(*h,BoxShapeDesc{{.5f,.5f,.5f},{}}));
        auto properties=world.ReadBodyProperties(*h);EC_CHECK(properties);
        auto p=*properties;p.mass=2;p.material.friction=.3f;p.material.restitution=.25f;
        if(backend==PhysicsBackend::Jolt){p.linearDamping=.1f;p.angularDamping=.2f;p.gravityFactor=.5f;p.continuousCollision=false;p.allowSleeping=false;}
        else {p.sleep.linearSpeedThreshold=.1f;p.sleep.inactivitySeconds=.75;}
        auto inertia=world.ReadBody(*h)->inverseInertiaBody;
        EC_CHECK(world.UpdateBodyProperties(*h,p));EC_CHECK(*world.ReadBodyProperties(*h)==p);
        EC_CHECK(world.ReadBody(*h)->inverseMass==.5f && world.ReadBody(*h)->inverseInertiaBody!=inertia);
        EC_CHECK(world.ReadDistanceJoint(*joint)->accumulatedImpulse==0 && (scene.GetBodyBinding(*moving) && *scene.GetBodyBinding(*moving)==*h));
        for(int invalidCase=0;invalidCase<4;++invalidCase)
        {
            auto bad=p;if(invalidCase==0)bad.mass=-1;if(invalidCase==1)bad.material.restitution=2;
            if(invalidCase==2)bad.gravityFactor=std::numeric_limits<float>::quiet_NaN();if(invalidCase==3)bad.type=BodyType::Static;
            EC_CHECK(!world.UpdateBodyProperties(*h,bad) && *world.ReadBodyProperties(*h)==p);
        }
        auto unsupported=p;
        if(backend==PhysicsBackend::Native)unsupported.linearDamping=.2f;else unsupported.sleep.inactivitySeconds=.8;
        EC_CHECK(!world.UpdateBodyProperties(*h,unsupported) && *world.ReadBodyProperties(*h)==p);
        EC_CHECK(world.DestroyJoint(*joint));
        for(auto type:{BodyType::Static,BodyType::Kinematic,BodyType::Dynamic})
        {p.type=type;EC_CHECK(world.UpdateBodyProperties(*h,p));EC_CHECK(world.ReadBody(*h)->type==type);system.Update(Timestep(1.0/60.0));EC_CHECK(!system.GetBackendFailure());}
        // Selection identity, stale world and clean teardown remain strict.
        PhysicsWorld foreign;EC_CHECK(!foreign.UpdateBodyProperties(*h,p));
        EC_CHECK((scene.GetBodyBinding(*fixed) && *scene.GetBodyBinding(*fixed)==*anchor));scene.OnRuntimeStop();EC_CHECK(!scene.GetBodyBinding(*moving));
        std::printf("EDITOR_CORRECTION_BACKEND_PASS backend=%s collider=4 confirmation=stale+force properties=validated handles=stable\n",PhysicsBackendName(backend).data());
        return true;
    }
    inline int Run()
    {
        Log::Initialize();
        ScenePresetPayload payload{1,{RbsScenePreset::BoxStack,8}};
        if(!DecodeScenePreset(&payload,sizeof(payload)) || DecodeScenePreset(&payload,sizeof(payload)-1))return 1;
        payload.version=2;if(DecodeScenePreset(&payload,sizeof(payload)))return 1;
        payload.version=1;payload.selection.scene=RbsScenePreset(99);if(DecodeScenePreset(&payload,sizeof(payload)))return 1;
        if(!Backend(PhysicsBackend::Native) || !Backend(PhysicsBackend::Jolt) || !Owner04Checks::Run())return 1;
        std::puts("EDITOR_CORRECTION_CHECKS_PASS");return 0;
    }
#undef EC_CHECK
}
