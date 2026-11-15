#pragma once
#include "Physics/PhysicsSystem.h"
#include "Scene/_Scene.h"
#include "Scene/_Entity.h"
#include "Component/Component.h"
#include "Core/Log.h"
#include "LaunchConfigChecks.h"
#include <cstdio>
#include <thread>
#include <array>
#include <vector>
#include <limits>
#if defined(GENGINE_MASS_INERTIA_CHECKS)
#include "../../GEngine/src/Physics/InertiaValidation.h"
#include "../../GEngine/src/Physics/WorldBackend.h"
#include "Physics/ShapeSphere.h"
#include "Physics/ShapeCapsule.h"
#include <glm/gtx/quaternion.hpp>
#endif

#if defined(GENGINE_CHAIN_WAKE_CHECKS)
namespace GEngine
{
    // Isolated validation builds only; no ordinary runtime fault-injection seam.
    struct ChainWakeTestAccess
    {
        static void FailReadbackOnce(PhysicsWorld& world) { world.m_ChainWakeFailReadbackOnce=true; }
        static std::uint64_t ReplaceGeneration(PhysicsWorld& world,PhysicsBodyHandle h,std::uint64_t value)
        { auto& generation=world.m_BodyIdentitySlots[h.index].generation;const auto old=generation;generation=value;return old; }
#if defined(GENGINE_MASS_INERTIA_CHECKS)
        static void FailPropertyReadbackOnce(PhysicsWorld& world) {world.m_MassInertiaFailPropertyReadbackOnce=true;}
        static RigidBody3D* Raw(PhysicsWorld& world,PhysicsBodyHandle h) {auto body=world.Resolve(h,"mass fixture");return body?*body:nullptr;}
        static auto Mass(PhysicsWorld& world,PhysicsBodyHandle h) {return world.m_Backend->CaptureMass(h);}
        static auto Synchronization(PhysicsWorld& world,PhysicsBodyHandle h) {return world.m_Backend->SnapshotSynchronizationForChecks(h);}
        static std::size_t SynchronizationBytes(PhysicsWorld& world) {return world.m_Backend->SynchronizationStorageBytesForChecks();}
#endif
    };
}
#endif

namespace Rbs::BackendChecks
{
    using namespace ::GEngine;
    using namespace ::GEngine::Component;
    inline std::uint64_t CompletedCheckTicks{};
    inline void Step(PhysicsSystem& system)
    {
        const auto before=system.GetCompletedSteps();system.Update(Timestep(1.0/60.0));
        CompletedCheckTicks+=system.GetCompletedSteps()-before;
    }
    inline bool Require(bool ok,const char* check)
    { if(!ok)std::fprintf(stderr,"BACKEND_CHECK_FAIL %s\n",check);return ok; }
#define BC_REQUIRE(value) do { if(!Require(bool(value),#value))return false; } while(false)
    inline bool Lifecycle(PhysicsBackend backend)
    {
        for(unsigned cycle=0;cycle<2;++cycle)
        {
            PhysicsSystem system;
            auto* world=new PhysicsWorld;
            BC_REQUIRE(world->ConfigureBackend(backend));system.SetPhysicsWorld(world);world->SetGravity({0,0,0});
            PhysicsWorld foreign;
            BC_REQUIRE(foreign.ConfigureBackend(backend)); // Overlapping process-global Jolt lifetime.
            auto sa=CreateCollisionShape(SphereShapeDesc{1}),sb=CreateCollisionShape(SphereShapeDesc{1});
            BC_REQUIRE(sa && sb);
            PhysicsBodyDesc a;a.type=BodyType::Static;
            PhysicsBodyDesc b;b.position={0,-2.25f,0};
            auto ha=world->CreateBody(a,*sa),hb=world->CreateBody(b,*sb);BC_REQUIRE(ha && hb);
            BC_REQUIRE(!*sa && !*sb);
            BC_REQUIRE(!foreign.ReadBody(*hb) && foreign.ReadBody(*hb).error().code==PhysicsBodyErrorCode::WrongWorld);
            bool wrongThread=false;std::thread thread([&]{auto r=world->ReadBody(*hb);wrongThread=!r && r.error().code==PhysicsBodyErrorCode::WrongThread;});thread.join();BC_REQUIRE(wrongThread);
            BC_REQUIRE(!world->ConfigureBackend(backend));
            // Teleport before creating the constraint, with no intervening tick.
            BC_REQUIRE(system.SetBodyPose(*ha,{3,0,0},{1,0,0,0}));
            BC_REQUIRE(system.SetBodyPose(*hb,{3,-2.25f,0},{1,0,0,0}));
            const DistanceJointDesc desc{*ha,*hb,{},{},2.25f,false};
            auto joint=world->CreateDistanceJoint(desc);BC_REQUIRE(joint);
            auto invalid=desc;invalid.bodyB=*ha;BC_REQUIRE(!world->CreateDistanceJoint(invalid));
            invalid=desc;invalid.length=0;BC_REQUIRE(!world->CreateDistanceJoint(invalid));
            invalid.length=std::numeric_limits<float>::quiet_NaN();BC_REQUIRE(!world->CreateDistanceJoint(invalid));
            BC_REQUIRE(world->GetJointCount()==1 && !foreign.ReadDistanceJoint(*joint));
            BC_REQUIRE(world->SetBodyMaterial(*hb,{.5f,.5f,.05f,.05f}));
            auto material=world->ReadBody(*hb);BC_REQUIRE(material && material->material.spinResistanceLength==.05f && material->material.rollingResistanceLength==.05f);
            BC_REQUIRE(world->ApplyLinearImpulse(*hb,{.5f,0,0}));
            float largest=0;
            for(unsigned tick=0;tick<120;++tick)
            {
                Step(system);BC_REQUIRE(!system.GetBackendFailure() && !system.HasJointFailure());
                auto state=world->ReadBody(*hb);auto j=world->ReadDistanceJoint(*joint);BC_REQUIRE(state && j);
                largest=(std::max)(largest,std::abs(j->error));
                // Existing admitted transient 1% intended-length bound, applied
                // only to this short focused check, not full Phase26 qualification.
                BC_REQUIRE(std::abs(j->error)<=.01f*desc.length);
            }
            std::printf("BACKEND_JOINT backend=%s cycle=%u ticks=120 max_error=%.9g\n",PhysicsBackendName(backend).data(),cycle,largest);
            BC_REQUIRE(system.SetBodyPose(*hb,{3,-2.25f,0},{1,0,0,0}));
            BC_REQUIRE(world->SetBodyMass(*hb,2));
            BC_REQUIRE(world->ReadDistanceJoint(*joint)->accumulatedImpulse==0);
            BC_REQUIRE(world->DestroyJoint(*joint) && !world->ReadDistanceJoint(*joint));
            auto next=world->CreateDistanceJoint(desc),duplicate=world->CreateDistanceJoint(desc);BC_REQUIRE(next && duplicate && *next!=*joint);
            BC_REQUIRE(world->DestroyJoint(*next) && world->ReadDistanceJoint(*duplicate));
            BC_REQUIRE(world->DestroyBody(*hb) && world->GetJointCount()==0 && !world->ReadDistanceJoint(*duplicate));
            auto replacementShape=CreateCollisionShape(SphereShapeDesc{1});BC_REQUIRE(replacementShape);
            auto replacement=world->CreateBody(b,*replacementShape);BC_REQUIRE(replacement && *replacement!=*hb && !world->ReadBody(*hb));
            if(backend==PhysicsBackend::Jolt)
            {
                auto capsule=CreateCollisionShape(CapsuleShapeDesc{});BC_REQUIRE(capsule);
                const auto count=world->GetBodyCount();auto supported=world->CreateBody(b,*capsule);
                BC_REQUIRE(supported && !*capsule && world->GetBodyCount()==count+1);
                BC_REQUIRE(world->DestroyBody(*supported));
                BC_REQUIRE(system.SetBodyScale(*replacement,{2,2,2})); // Supported staged Jolt replacement; identity retained.
            }
            BC_REQUIRE(world->Close() && world->Close() && !world->ReadBody(*replacement));
            system.OnExit();
        }
        return true;
    }
    inline bool FreeMotionAndContact(PhysicsBackend backend)
    {
        PhysicsSystem system;auto* world=new PhysicsWorld;BC_REQUIRE(world->ConfigureBackend(backend));system.SetPhysicsWorld(world);
        world->SetGravity({0,0,0});
        auto shape=CreateCollisionShape(SphereShapeDesc{1});BC_REQUIRE(shape);
        PhysicsBodyDesc body;body.mass=2;body.position={0,5,0};
        auto h=world->CreateBody(body,*shape);BC_REQUIRE(h);
        BC_REQUIRE(world->ApplyForceForDuration(*h,{2,0,0},.5f));
        BC_REQUIRE(world->ReadBody(*h)->linearVelocity==Vec3f(.5f,0,0));
        for(unsigned i=0;i<120;++i)Step(system);
        auto state=world->ReadBody(*h);BC_REQUIRE(state && !system.GetBackendFailure());
        BC_REQUIRE(std::abs(state->position.x-1.f)<1e-4f && state->position.y==5.f);
        BC_REQUIRE(system.SetBodyPose(*h,{0,3,0},{1,0,0,0}));BC_REQUIRE(world->SetBodyVelocity(*h,{0,0,0},{0,0,0}));
        world->SetGravity({0,-12,0});
        auto floor=CreateCollisionShape(BoxShapeDesc{{10,.5f,10},{0,0,0}});BC_REQUIRE(floor);
        PhysicsBodyDesc ground;ground.type=BodyType::Static;BC_REQUIRE(world->CreateBody(ground,*floor));
        for(unsigned i=0;i<120;++i){Step(system);BC_REQUIRE(!system.GetBackendFailure() && world->ReadBody(*h));}
        state=world->ReadBody(*h);BC_REQUIRE(state && state->position.y>0 && state->position.y<3);
        // Broad contact-response safety only; no settling/penetration PASS claim.
        std::printf("BACKEND_CONTACT backend=%s y=%.9g sleeping=%d\n",PhysicsBackendName(backend).data(),state->position.y,int(state->sleeping));
        BC_REQUIRE(world->WakeBody(*h) && world->ApplyLinearImpulse(*h,{0,2,0}));
        Step(system);BC_REQUIRE(!world->ReadBody(*h)->sleeping);
        return true;
    }
    inline bool Rebuild()
    {
        _Scene scene;
        auto a=scene.CreateEntity("root"),b=scene.CreateEntity("tip");BC_REQUIRE(a && b);
        for(auto e:{*a,*b})
        {
            e.AddOrReplaceComponent<Transform3DComponent>(Vec3f(0,e==*a?5.f:2.75f,0),Vec3f(0),Vec3f(1));
            RigidBody3DComponent rigid;rigid.Type=e==*a?BodyType::Static:BodyType::Dynamic;e.AddOrReplaceComponent<RigidBody3DComponent>(rigid);
            SphereFixture3DComponent fixture;fixture.Radius=1;fixture.Property.m_InvMass=e==*a?0.f:1.f;e.AddOrReplaceComponent<SphereFixture3DComponent>(fixture);
        }
        BC_REQUIRE(scene.OnRuntimeStart());auto ha=scene.GetBodyBinding(*a),hb=scene.GetBodyBinding(*b);BC_REQUIRE(ha && hb);
        auto joint=scene.GetPhysicsSystem()->GetPhysicsWorld()->CreateDistanceJoint({*ha,*hb,{},{},2.25f,false});BC_REQUIRE(joint);
        BC_REQUIRE(scene.SetLocalTranslation(*b,{1,2.75f,0})); // An owner authoring edit must survive rebuild.
        for(auto backend:{PhysicsBackend::Jolt,PhysicsBackend::Native,PhysicsBackend::Jolt})
        {
            BC_REQUIRE(scene.RebuildPhysicsBackend(backend));auto* w=scene.GetPhysicsSystem()->GetPhysicsWorld();
            BC_REQUIRE(w && w->GetBackend()==backend && w->GetBodyCount()==2 && w->GetJointCount()==1);
            BC_REQUIRE(!w->ReadBody(*ha) && !w->ReadBody(*hb) && !w->ReadDistanceJoint(*joint));
            auto current=scene.GetBodyBinding(*b);BC_REQUIRE(current && w->ReadBody(*current)->position==Vec3f(1,2.75f,0));
            BC_REQUIRE(scene.IsPaused());
        }
        scene.OnRuntimeStop();
        auto source=CreateRefPtr<_Scene>();BC_REQUIRE(source->ConfigurePhysicsBackend(PhysicsBackend::Jolt));
        auto copy=_Scene::Copy(source);BC_REQUIRE(copy && (*copy)->GetPhysicsBackend()==PhysicsBackend::Jolt);
        return true;
    }
#if defined(GENGINE_CHAIN_WAKE_CHECKS)
    inline bool AddSphere(PhysicsWorld& world,const Vec3f& position,PhysicsBodyHandle& handle,
        BodyType type=BodyType::Dynamic,float mass=1.f)
    {
        auto shape=CreateCollisionShape(SphereShapeDesc{1});BC_REQUIRE(shape);
        PhysicsBodyDesc desc;desc.position=position;desc.type=type;desc.mass=mass;
        desc.material={.5f,.5f,.05f,.05f};
        auto result=world.CreateBody(desc,*shape);BC_REQUIRE(result);handle=*result;return true;
    }
    inline bool SamePhysicalState(const PhysicsBodyState& a,const PhysicsBodyState& b)
    {
        return a.handle==b.handle && a.type==b.type && a.position==b.position && a.orientation==b.orientation &&
            a.linearVelocity==b.linearVelocity && a.angularVelocity==b.angularVelocity && a.inverseMass==b.inverseMass &&
            a.inverseInertiaBody==b.inverseInertiaBody && a.inverseInertiaWorld==b.inverseInertiaWorld && a.material==b.material;
    }
    inline bool SameMotion(const PhysicsImpulseMotion& a,const PhysicsImpulseMotion& b)
    {
        return a.linear==b.linear && a.angular==b.angular && a.center==b.center && a.orientation==b.orientation &&
            a.inverseMass==b.inverseMass && a.inverseInertia==b.inverseInertia;
    }
    inline bool Sleeping(PhysicsWorld& world,PhysicsBodyHandle handle,bool expected)
    {
        const auto mirror=world.ReadBody(handle);const auto sdk=world.ReadImpulseMotion(handle);
        BC_REQUIRE(mirror && sdk && mirror->sleeping==expected && sdk->sleeping==expected);return true;
    }
    inline bool SameJoint(const DistanceJointState& a,const DistanceJointState& b)
    {
        return a.handle==b.handle && a.descriptor.bodyA==b.descriptor.bodyA && a.descriptor.bodyB==b.descriptor.bodyB &&
            a.descriptor.localAnchorA==b.descriptor.localAnchorA && a.descriptor.localAnchorB==b.descriptor.localAnchorB &&
            a.descriptor.length==b.descriptor.length && a.descriptor.collideConnected==b.descriptor.collideConnected &&
            a.accumulatedImpulse==b.accumulatedImpulse;
    }
    inline void TraceChain(const char* name,const char* boundary,unsigned tick,PhysicsWorld& world,
        std::span<const PhysicsBodyHandle> bodies)
    {
        for(std::size_t i=1;i<bodies.size();++i) {
            const auto state=world.ReadBody(bodies[i]);if(!state)continue;
            std::printf("CHAIN_WAKE_STATE case=%s boundary=%s tick=%u ordinal=%zu index=%u generation=%llu registry=%llu awake=%d vx=%.9g vy=%.9g vz=%.9g wx=%.9g wy=%.9g wz=%.9g\n",
                name,boundary,tick,i,bodies[i].index,static_cast<unsigned long long>(bodies[i].generation),
                static_cast<unsigned long long>(bodies[i].registry),int(!state->sleeping),state->linearVelocity.x,
                state->linearVelocity.y,state->linearVelocity.z,state->angularVelocity.x,state->angularVelocity.y,state->angularVelocity.z);
        }
    }
    inline bool ChainAt301(unsigned links,bool contact)
    {
        const char* name=contact?"G8":links==4?"C4":links==8?"C8":"C16";
        PhysicsSystem system;auto* world=new PhysicsWorld;BC_REQUIRE(world->ConfigureBackend(PhysicsBackend::Jolt));system.SetPhysicsWorld(world);
        std::vector<PhysicsBodyHandle> bodies(links+1);std::vector<PhysicsJointHandle> joints;
        for(unsigned i=0;i<=links;++i) {
            const Vec3f position=contact?Vec3f(2.25f*i,1.5f,0):Vec3f(0,1.5f+2.25f*(links-i),0);
            BC_REQUIRE(AddSphere(*world,position,bodies[i],i?BodyType::Dynamic:BodyType::Static));
        }
        if(contact) {
            auto shape=CreateCollisionShape(BoxShapeDesc{{50,.5f,50},{0,0,0}});BC_REQUIRE(shape);
            PhysicsBodyDesc floor;floor.type=BodyType::Static;floor.material={.5f,.5f,.05f,.05f};BC_REQUIRE(world->CreateBody(floor,*shape));
        }
        for(unsigned i=1;i<=links;++i) {
            auto joint=world->CreateDistanceJoint({bodies[i-1],bodies[i],{},{},2.25f,false});BC_REQUIRE(joint);joints.push_back(*joint);
        }
        PhysicsBodyHandle sentinel;BC_REQUIRE(AddSphere(*world,{100,100,100},sentinel));
        auto sentinelProperties=world->ReadBodyProperties(sentinel);BC_REQUIRE(sentinelProperties);
        sentinelProperties->gravityFactor=0;BC_REQUIRE(world->UpdateBodyProperties(sentinel,*sentinelProperties));
        for(unsigned tick=0;tick<300;++tick) {Step(system);BC_REQUIRE(!system.GetBackendFailure() && !system.HasJointFailure());}
        BC_REQUIRE(system.GetCompletedSteps()==300);
        for(unsigned i=1;i<=links;++i)BC_REQUIRE(Sleeping(*world,bodies[i],true));
        BC_REQUIRE(Sleeping(*world,sentinel,true));
        std::vector<PhysicsBodyState> before;std::vector<PhysicsImpulseMotion> sdkBefore;std::vector<DistanceJointState> jointBefore;
        for(auto body:bodies) {auto state=world->ReadBody(body);BC_REQUIRE(state);before.push_back(*state);}
        for(unsigned i=1;i<=links;++i) {auto motion=world->ReadImpulseMotion(bodies[i]);BC_REQUIRE(motion);sdkBefore.push_back(*motion);}
        for(auto joint:joints) {auto state=world->ReadDistanceJoint(joint);BC_REQUIRE(state);jointBefore.push_back(*state);}
        const auto unrelated=*world->ReadBody(sentinel);TraceChain(name,"before",300,*world,bodies);
        const Vec3f impulse=contact?Vec3f(0,0,.5f):Vec3f(.5f,0,0);
        BC_REQUIRE(world->ApplyLinearImpulse(bodies.back(),impulse));
        BC_REQUIRE(system.GetCompletedSteps()==300);
        for(unsigned i=1;i<=links;++i) {
            auto state=world->ReadBody(bodies[i]);BC_REQUIRE(state && !state->sleeping);
            if(i<links) {
                BC_REQUIRE(SamePhysicalState(before[i],*state));
                const auto sdk=world->ReadImpulseMotion(bodies[i]);BC_REQUIRE(sdk && !sdk->sleeping && SameMotion(sdkBefore[i-1],*sdk));
            } else {
                BC_REQUIRE(state->linearVelocity==before[i].linearVelocity+impulse*before[i].inverseMass && state->angularVelocity==before[i].angularVelocity);
                auto pending=world->ReadImpulseMotion(bodies[i]);BC_REQUIRE(!pending && pending.error().code==PhysicsBodyErrorCode::Busy);
            }
        }
        for(unsigned i=0;i<joints.size();++i) {auto state=world->ReadDistanceJoint(joints[i]);BC_REQUIRE(state && SameJoint(jointBefore[i],*state));}
        BC_REQUIRE(SamePhysicalState(unrelated,*world->ReadBody(sentinel)) && Sleeping(*world,sentinel,true));
        TraceChain(name,"command",300,*world,bodies);
        Step(system);BC_REQUIRE(!system.GetBackendFailure() && !system.HasJointFailure() && system.GetCompletedSteps()==301);
        for(unsigned i=1;i<=links;++i)BC_REQUIRE(Sleeping(*world,bodies[i],false));
        BC_REQUIRE(Sleeping(*world,sentinel,true));TraceChain(name,"after",301,*world,bodies);
        std::printf("CHAIN_WAKE_301_PASS case=%s dynamic=%u commands=1 ticks=301 awake=%u extra_neighbor_impulses=0 unrelated_awake=0\n",name,links,links);
        return true;
    }
    inline bool ActivationTopology()
    {
        PhysicsSystem system;auto* world=new PhysicsWorld;BC_REQUIRE(world->ConfigureBackend(PhysicsBackend::Jolt));system.SetPhysicsWorld(world);world->SetGravity({0,0,0});
        std::array<PhysicsBodyHandle,8> h;
        const std::array<Vec3f,8> positions={Vec3f(0,0,0),Vec3f(3,0,0),Vec3f(3,3,0),Vec3f(0,3,0),
            Vec3f(-3,0,0),Vec3f(-6,0,0),Vec3f(0,-3,0),Vec3f(0,-6,0)};
        for(unsigned i=0;i<h.size();++i)BC_REQUIRE(AddSphere(*world,positions[i],h[i],i==4?BodyType::Static:i==6?BodyType::Kinematic:BodyType::Dynamic));
        std::vector<PhysicsJointHandle> joints;
        const std::array<std::array<unsigned,2>,8> edges={{{0,1},{1,2},{2,3},{3,0},{0,4},{4,5},{0,6},{6,7}}};
        for(unsigned i=0;i<edges.size();++i) {
            auto joint=world->CreateDistanceJoint({h[edges[i][0]],h[edges[i][1]],{},{},3.f,(i%2)==0});BC_REQUIRE(joint);joints.push_back(*joint);
        }
        auto retired=world->CreateDistanceJoint({h[0],h[1],{},{},3.f,false});BC_REQUIRE(retired && world->DestroyJoint(*retired));
        auto reused=world->CreateDistanceJoint({h[0],h[1],{},{},3.f,true});BC_REQUIRE(reused && *reused!=*retired && !world->ReadDistanceJoint(*retired));joints.push_back(*reused);
        // Retirement must not leave a bridge into the unrelated anchored branch.
        auto obsolete=world->CreateDistanceJoint({h[3],h[5],{},{},glm::length(positions[3]-positions[5]),false});
        BC_REQUIRE(obsolete && world->DestroyJoint(*obsolete));
        for(unsigned tick=0;tick<60;++tick) {Step(system);BC_REQUIRE(!system.GetBackendFailure());}
        for(unsigned i:{0u,1u,2u,3u,5u,7u})BC_REQUIRE(Sleeping(*world,h[i],true));
        std::array<PhysicsBodyState,8> before;for(unsigned i=0;i<h.size();++i)before[i]=*world->ReadBody(h[i]);
        // An invalid live terminal is still an invalid reachable graph, not an ignored boundary.
        const auto original=ChainWakeTestAccess::ReplaceGeneration(*world,h[4],h[4].generation+1);
        const auto rejected=world->ApplyLinearImpulse(h[0],{0,0,.5f});
        ChainWakeTestAccess::ReplaceGeneration(*world,h[4],original);BC_REQUIRE(!rejected);
        for(unsigned i:{0u,1u,2u,3u,5u,7u})BC_REQUIRE(Sleeping(*world,h[i],true));
        PhysicsImpulseCommand command{h[0],{0,0,.5f}};const auto result=world->ApplyImpulseBatch({&command,1});BC_REQUIRE(result);
        for(unsigned i=1;i<h.size();++i)BC_REQUIRE(SamePhysicalState(before[i],*world->ReadBody(h[i])));
        for(unsigned i:{0u,1u,2u,3u})BC_REQUIRE(Sleeping(*world,h[i],false));
        for(unsigned i:{5u,7u})BC_REQUIRE(Sleeping(*world,h[i],true));
        BC_REQUIRE(system.GetCompletedSteps()==60 && world->GetJointCount()==joints.size());
        for(auto joint:joints)BC_REQUIRE(world->ReadDistanceJoint(joint));
        std::puts("CHAIN_WAKE_TOPOLOGY_PASS branch cycle duplicate_joint retired_reused_joint collideConnected static_terminal kinematic_terminal invalid_live_terminal no_extra_impulse");
        return true;
    }
    inline bool CommandSafety()
    {
        PhysicsSystem system;auto* world=new PhysicsWorld;BC_REQUIRE(world->ConfigureBackend(PhysicsBackend::Jolt));system.SetPhysicsWorld(world);world->SetGravity({0,0,0});
        constexpr unsigned Groups=8;std::array<std::array<PhysicsBodyHandle,3>,Groups> group;
        for(unsigned g=0;g<Groups;++g)for(unsigned i=0;i<3;++i) {
            BC_REQUIRE(AddSphere(*world,{float(g)*20,float(i)*3,0},group[g][i],BodyType::Dynamic,g==4?float(i+1):1.f));
            if(i)BC_REQUIRE(world->CreateDistanceJoint({group[g][i-1],group[g][i],{},{},3.f,false}));
        }
        PhysicsBodyHandle cumulative,angular,offCenter,sentinel;
        BC_REQUIRE(AddSphere(*world,{0,50,0},cumulative));BC_REQUIRE(AddSphere(*world,{5,50,0},angular));
        BC_REQUIRE(AddSphere(*world,{10,50,0},offCenter));BC_REQUIRE(AddSphere(*world,{15,50,0},sentinel));
        for(unsigned tick=0;tick<60;++tick) {Step(system);BC_REQUIRE(!system.GetBackendFailure());}
        for(const auto& g:group)for(auto h:g)BC_REQUIRE(Sleeping(*world,h,true));
        BC_REQUIRE(Sleeping(*world,sentinel,true));
        const auto unchangedPeers=[&](unsigned g,const std::array<PhysicsBodyState,3>& before) {
            for(unsigned i=1;i<3;++i) {
                const auto current=world->ReadBody(group[g][i]);const auto sdk=world->ReadImpulseMotion(group[g][i]);
                if(!current||!sdk||!SamePhysicalState(before[i],*current)||sdk->linear!=before[i].linearVelocity||sdk->angular!=before[i].angularVelocity||sdk->sleeping)return false;
            }return true;
        };
        const auto snapshots=[&](unsigned g) {std::array<PhysicsBodyState,3> out;for(unsigned i=0;i<3;++i)out[i]=*world->ReadBody(group[g][i]);return out;};
        auto before=snapshots(0);BC_REQUIRE(world->ApplyLinearImpulse(group[0][0],{0,0,.5f}) && unchangedPeers(0,before));
        BC_REQUIRE(world->ApplyLinearImpulse(group[0][0],{0,0,.5f}) && unchangedPeers(0,before));
        BC_REQUIRE(world->ReadBody(group[0][0])->linearVelocity==Vec3f(0,0,1.f));
        before=snapshots(1);BC_REQUIRE(world->ApplyAngularImpulse(group[1][0],{.1f,0,0}) && unchangedPeers(1,before));
        before=snapshots(2);const auto center=world->ReadBody(group[2][0])->centerOfMass;
        BC_REQUIRE(world->ApplyImpulse(group[2][0],center+Vec3f(0,1,0),{0,0,.5f}) && unchangedPeers(2,before));
        BC_REQUIRE(world->ReadBody(group[2][0])->angularVelocity.x>0 && world->ReadBody(group[2][0])->linearVelocity.z==.5f);
        // A zero selected target is awakened by the connected union after all readbacks.
        std::array<PhysicsImpulseCommand,2> mixed={PhysicsImpulseCommand{group[3][0],{0,0,.5f}},PhysicsImpulseCommand{group[3][2],{}}};
        auto mixedResult=world->ApplyImpulseBatch(mixed);BC_REQUIRE(mixedResult && (*mixedResult)->bodies.size()==2);
        BC_REQUIRE(!(*mixedResult)->bodies[0].after.sleeping && !(*mixedResult)->bodies[1].after.sleeping);
        BC_REQUIRE((*mixedResult)->bodies[1].after.linear==Vec3f(0) && (*mixedResult)->bodies[1].after.angular==Vec3f(0));
        for(auto h:group[3])BC_REQUIRE(Sleeping(*world,h,false));
        std::array<PhysicsImpulseCommand,2> deltas={PhysicsImpulseCommand{group[4][0],{0,0,.25f},{.1f,0,0},{},PhysicsImpulseMode::VelocityChange},
            PhysicsImpulseCommand{group[4][2],{0,0,.25f},{.1f,0,0},{},PhysicsImpulseMode::VelocityChange}};
        auto deltaResult=world->ApplyImpulseBatch(deltas);BC_REQUIRE(deltaResult);
        for(const auto& r:(*deltaResult)->bodies)BC_REQUIRE(glm::length(r.after.linear-Vec3f(0,0,.25f))<1.e-6f && glm::length(r.after.angular-Vec3f(.1f,0,0))<1.e-6f);
        BC_REQUIRE((*deltaResult)->bodies[0].impulse.z==.25f && (*deltaResult)->bodies[1].impulse.z==.75f);
        BC_REQUIRE(world->ReadImpulseMotion(group[4][1])->linear==Vec3f(0) && Sleeping(*world,group[4][1],false));
        // The isolated build injects one failed invariant after physical commits;
        // the existing rollback must restore targets before any peer activation.
        before=snapshots(5);std::array<PhysicsImpulseCommand,2> rollback={PhysicsImpulseCommand{group[5][0],{0,0,.5f}},PhysicsImpulseCommand{group[5][2],{.25f,0,0}}};
        ChainWakeTestAccess::FailReadbackOnce(*world);const auto rejected=world->ApplyImpulseBatch(rollback);
        BC_REQUIRE(!rejected && rejected.error().code==PhysicsBodyErrorCode::NonFiniteResult);
        for(unsigned i=0;i<3;++i)BC_REQUIRE(SamePhysicalState(before[i],*world->ReadBody(group[5][i])) && Sleeping(*world,group[5][i],true));
        // The hook is one-shot, and retrying the same complete batch commits once.
        auto retry=world->ApplyImpulseBatch(rollback);BC_REQUIRE(retry && (*retry)->bodies.size()==rollback.size());
        BC_REQUIRE(world->ReadImpulseMotion(group[5][0])->linear==Vec3f(0,0,.5f) &&
            world->ReadImpulseMotion(group[5][2])->linear==Vec3f(.25f,0,0) &&
            world->ReadImpulseMotion(group[5][1])->linear==Vec3f(0));
        for(auto h:group[5])BC_REQUIRE(Sleeping(*world,h,false));
        // Invalid last member, duplicate, wrong world/thread and stale reachable
        // endpoint all reject before mutating any complete, valid preceding target.
        before=snapshots(6);std::array<PhysicsImpulseCommand,2> invalid={PhysicsImpulseCommand{group[6][0],{0,0,.5f}},PhysicsImpulseCommand{group[6][2],{0,0,.5f}}};
        invalid.back().linear.x=std::numeric_limits<float>::quiet_NaN();BC_REQUIRE(!world->ApplyImpulseBatch(invalid));
        invalid.back()=invalid.front();BC_REQUIRE(!world->ApplyImpulseBatch(invalid));
        PhysicsWorld foreign;PhysicsBodyHandle other;BC_REQUIRE(AddSphere(foreign,{0,0,0},other));
        invalid.back()={other,{0,0,.5f}};BC_REQUIRE(!world->ApplyImpulseBatch(invalid));
        bool wrongThread=false;std::thread worker([&]{const auto r=world->ApplyImpulseBatch({invalid.data(),1});wrongThread=!r&&r.error().code==PhysicsBodyErrorCode::WrongThread;});worker.join();BC_REQUIRE(wrongThread);
        const auto generation=ChainWakeTestAccess::ReplaceGeneration(*world,group[6][1],group[6][1].generation+1);
        const auto staleLegacy=world->ApplyLinearImpulse(group[6][0],{0,0,.5f});
        const auto staleBatch=world->ApplyImpulseBatch({invalid.data(),1});
        ChainWakeTestAccess::ReplaceGeneration(*world,group[6][1],generation);BC_REQUIRE(!staleLegacy && !staleBatch);
        for(unsigned i=0;i<3;++i)BC_REQUIRE(SamePhysicalState(before[i],*world->ReadBody(group[6][i])) && Sleeping(*world,group[6][i],true));
        std::array<PhysicsImpulseCommand,2> zeros={PhysicsImpulseCommand{group[7][0],{}},PhysicsImpulseCommand{group[7][2],{}}};
        BC_REQUIRE(world->ApplyImpulseBatch(zeros));for(auto h:group[7])BC_REQUIRE(Sleeping(*world,h,true));
        BC_REQUIRE(world->ApplyLinearImpulse(group[7][0],{}));BC_REQUIRE(Sleeping(*world,group[7][1],true) && Sleeping(*world,group[7][2],true));
        // Pending legacy writes must accumulate; activation may not Remember()
        // their cache and swallow any portion of the next synchronization.
        BC_REQUIRE(world->ApplyLinearImpulse(cumulative,{.25f,0,0}) && world->ApplyLinearImpulse(cumulative,{.5f,0,0}));
        BC_REQUIRE(world->ReadBody(cumulative)->linearVelocity==Vec3f(.75f,0,0));
        auto pending=world->ReadImpulseMotion(cumulative);BC_REQUIRE(!pending && pending.error().code==PhysicsBodyErrorCode::Busy);
        BC_REQUIRE(world->ApplyAngularImpulse(angular,{0,0,.1f}) && world->ApplyAngularImpulse(angular,{0,0,.2f}));
        const auto angularExpected=world->ReadBody(angular)->angularVelocity;
        const auto isolatedCenter=world->ReadBody(offCenter)->centerOfMass;
        BC_REQUIRE(world->ApplyImpulse(offCenter,isolatedCenter+Vec3f(0,1,0),{0,0,.5f}));
        const auto offExpected=*world->ReadBody(offCenter);
        BC_REQUIRE(system.GetCompletedSteps()==60 && Sleeping(*world,sentinel,true));
        Step(system);BC_REQUIRE(!system.GetBackendFailure());
        BC_REQUIRE(world->ReadImpulseMotion(cumulative)->linear==Vec3f(.75f,0,0));
        BC_REQUIRE(glm::length(world->ReadImpulseMotion(angular)->angular-angularExpected)<1.e-6f);
        BC_REQUIRE(glm::length(world->ReadImpulseMotion(offCenter)->linear-offExpected.linearVelocity)<1.e-6f &&
            glm::length(world->ReadImpulseMotion(offCenter)->angular-offExpected.angularVelocity)<1.e-6f);
        unsigned commands=0;bool commandAccepted=true;
        system.SetBeforeStep([&](PhysicsWorld& w,std::uint64_t tick) {if(tick==61){++commands;commandAccepted=bool(w.ApplyLinearImpulse(cumulative,{.125f,0,0}));}});
        Step(system);Step(system);BC_REQUIRE(commandAccepted && commands==1 && system.GetCompletedSteps()==63 && !system.GetBackendFailure());
        BC_REQUIRE(world->ReadImpulseMotion(cumulative)->linear==Vec3f(.875f,0,0) && Sleeping(*world,sentinel,true));
        BC_REQUIRE(world->DestroyBody(group[6][1]) && !world->ReadBody(group[6][1]));
        PhysicsBodyHandle replacement;BC_REQUIRE(AddSphere(*world,{200,0,0},replacement) && replacement!=group[6][1]);
        PhysicsImpulseCommand stale{group[6][1],{0,0,.5f}};BC_REQUIRE(!world->ApplyImpulseBatch({&stale,1}));
        BC_REQUIRE(world->Close() && world->Close());
        std::puts("CHAIN_WAKE_COMMANDS_PASS linear angular off_center consecutive_legacy pending_velocity zero_union final_sleep_readback velocity_change paused_zero_steps running_once atomic_readback_rollback invalid_last duplicate wrong_world wrong_thread stale_live_endpoint body_retirement shutdown");
        return true;
    }
#if defined(GENGINE_MASS_INERTIA_CHECKS)
    namespace MI = PhysicsInertia;
    inline bool MassClose(const Vec3f& a,const Vec3f& b)
    { return Math::IsFinite(a) && Math::IsFinite(b) && glm::length(glm::dvec3(a)-glm::dvec3(b))<=MI::FloatTolerance*(std::max)(1.0,glm::length(glm::dvec3(b))); }
    inline Mat3 Diagonal(const Vec3f& v)
    { Mat3 m(0);for(unsigned i=0;i<3;++i)m[i][i]=v[i];return m; }
    inline bool MassReadback(PhysicsWorld& world,PhysicsBodyHandle h,bool oldFloat=false)
    {
        auto* raw=ChainWakeTestAccess::Raw(world,h);auto state=world.ReadBody(h);BC_REQUIRE(raw && state && raw->HasValidMassProperties());
        const auto prepared=MI::Prepare(raw->m_Shape->InertiaTensor(),state->inverseMass);BC_REQUIRE(prepared);
        BC_REQUIRE(MI::MatricesAgree(state->inverseInertiaBody,prepared->inverse));
        const auto rotation=glm::toMat3(state->orientation);const auto expected=rotation*prepared->inverse*glm::transpose(rotation);
        BC_REQUIRE(MI::MatricesAgree(state->inverseInertiaWorld,expected));
        if(oldFloat && state->type==BodyType::Dynamic) {
            const Mat3 prior=glm::inverse(raw->m_Shape->InertiaTensor())*state->inverseMass;
            BC_REQUIRE(prepared->preservedFloat && state->inverseInertiaBody==prior);
        }
        if(world.GetBackend()==PhysicsBackend::Jolt) {
            const auto principal=ChainWakeTestAccess::Mass(world,h);BC_REQUIRE(principal);
            BC_REQUIRE(principal->inverseMass==state->inverseMass);
            if(state->type!=BodyType::Dynamic) {
                BC_REQUIRE(state->inverseMass==0 && state->inverseInertiaBody==Mat3(0) &&
                    principal->inversePrincipal==Vec3f(0) && principal->principalRotation==Quat(1,0,0,0));
            } else {
                const auto axes=glm::toMat3(principal->principalRotation);
                BC_REQUIRE(MI::MatricesAgree(axes*Diagonal(principal->inversePrincipal)*glm::transpose(axes),prepared->inverse));
                const auto motion=world.ReadImpulseMotion(h);BC_REQUIRE(motion && motion->inverseMass==state->inverseMass &&
                    MI::MatricesAgree(motion->inverseInertia,expected));
            }
        }
        return true;
    }
    inline bool MassAlgebra()
    {
        const auto makeRotated=[](Vec3f values) {
            const glm::dmat3 r=glm::mat3_cast(glm::angleAxis(.37,glm::normalize(glm::dvec3(1,2,3))));
            const auto d=r*glm::dmat3(Diagonal(values))*glm::transpose(r);Mat3 m(d);
            for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<3;++j)m[j][i]=m[i][j];return m;
        };
        for(const auto base:{Diagonal({1,1,1}),Diagonal({1,1.000001f,2}),Diagonal({1,1e-10f,.5f}),makeRotated({1,2,4})})
            for(float scale:{1e-20f,1.f,1e20f}) {
                const auto value=MI::Prepare(base*scale,1.f);BC_REQUIRE(value && MI::ValidatePair(value->forward,value->inverse));
                const auto principal=MI::PreparePrincipal(*value);BC_REQUIRE(principal);
                const auto r=glm::toMat3(principal->rotation);
                BC_REQUIRE(MI::MatricesAgree(r*Diagonal(principal->inverseDiagonal)*glm::transpose(r),value->inverse));
            }
        // Tiny coupling matters after inversion. Exact powers avoid attributing
        // an unrepresentable Float32 pair to the Jacobi convergence criterion.
        Mat3 coupled=Diagonal({1,std::ldexp(1.f,-66),.5f});coupled[0][1]=coupled[1][0]=std::ldexp(1.f,-53);
        const auto coupledTensor=MI::Prepare(coupled,1.f);BC_REQUIRE(coupledTensor);
        const auto coupledPrincipal=MI::PreparePrincipal(*coupledTensor);BC_REQUIRE(coupledPrincipal);
        const auto coupledAxes=glm::toMat3(coupledPrincipal->rotation);
        const auto coupledActual=coupledAxes*Diagonal(coupledPrincipal->inverseDiagonal)*glm::transpose(coupledAxes);
        BC_REQUIRE(MI::MatricesAgree(coupledActual,coupledTensor->inverse) && MI::InverseResidualWithin(coupledTensor->forward,coupledActual));
        Mat3 almost(1);almost[1][0]=almost[0][1]=1.f-std::ldexp(1.f,-20);
        const auto close=MI::Prepare(almost,1.f);BC_REQUIRE(close && MI::ValidatePair(close->forward,close->inverse));
        const auto closePrincipal=MI::PreparePrincipal(*close);
        if(closePrincipal) {
            const auto r=glm::toMat3(closePrincipal->rotation);
            const auto actual=r*Diagonal(closePrincipal->inverseDiagonal)*glm::transpose(r);
            BC_REQUIRE(MI::InverseResidualWithin(close->forward,actual) && MI::MatricesAgree(actual,close->inverse));
        } else BC_REQUIRE(closePrincipal.error()==MI::Error::PrincipalRepresentation);
        std::array<Mat3,7> invalid={Mat3(0),Diagonal({1,0,1}),Diagonal({1,-1,1}),Mat3(1),Mat3(1),Mat3(1),Mat3(1)};
        invalid[3][0][1]=.1f;invalid[4][0][0]=std::numeric_limits<float>::quiet_NaN();
        invalid[5][1][1]=std::numeric_limits<float>::infinity();invalid[6][0][0]=std::numeric_limits<float>::denorm_min();
        for(const auto& value:invalid)BC_REQUIRE(!MI::Prepare(value,1.f));
        Mat3 indefinite(1);indefinite[0][1]=indefinite[1][0]=2;BC_REQUIRE(!MI::Prepare(indefinite,1.f));
        BC_REQUIRE(!MI::Prepare(Mat3(1e10f),1e-30f));
        std::printf("MASS_INERTIA_ALGEBRA_PASS double_tolerance=%.17g float_tolerance=%.17g jacobi_sweeps=%u prepared_tensor_bytes=%zu principal_bytes=%zu backend_mass_bytes=%zu\n",
            MI::DoubleTolerance,MI::FloatTolerance,unsigned(MI::MaxJacobiSweeps),sizeof(MI::PreparedTensor),sizeof(MI::PreparedPrincipal),sizeof(PreparedBackendMass));
        return true;
    }
    inline bool SphereMassCase(PhysicsBackend backend,float radius,float mass)
    {
        PhysicsSystem system;auto* world=new PhysicsWorld;BC_REQUIRE(world->ConfigureBackend(backend));system.SetPhysicsWorld(world);world->SetGravity({0,0,0});
        std::array<PhysicsBodyHandle,3> bodies;
        for(unsigned i=0;i<bodies.size();++i) {
            auto shape=CreateCollisionShape(SphereShapeDesc{radius});BC_REQUIRE(shape);
            PhysicsBodyDesc desc;desc.mass=mass;desc.position={float(i)*10,0,0};
            desc.orientation=glm::angleAxis(.4f,glm::normalize(Vec3f(1,2,3)));
            auto h=world->CreateBody(desc,*shape);BC_REQUIRE(h && !*shape);bodies[i]=*h;
            BC_REQUIRE(MassReadback(*world,*h,radius>=.5f));
            const auto state=world->ReadBody(*h);BC_REQUIRE(state && state->inverseMass==1.f/mass);
            const double expected=1.0/(.4*double(mass)*radius*radius);
            BC_REQUIRE(MI::MatricesAgree(state->inverseInertiaBody,Mat3(float(expected))));
        }
        // Constant small angular velocity increment also covers the 0.001 SDK fallback boundary.
        const float inertia=.4f*mass*radius*radius;
        const Vec3f angular=Vec3f(.1f,.2f,.3f)*inertia;
        const Vec3f linear(.01f*mass,0,0),lever(0,radius,0);
        auto angularBefore=*world->ReadBody(bodies[0]);auto offBefore=*world->ReadBody(bodies[1]);
        BC_REQUIRE(world->ApplyAngularImpulse(bodies[0],angular));
        // Scale the lever/impulse pair for tiny spheres to remain below the existing 30 rad/s cap.
        const Vec3f offLinear=linear*radius;
        BC_REQUIRE(world->ApplyImpulse(bodies[1],offBefore.centerOfMass+lever,offLinear));
        const Vec3f angularExpected=angularBefore.inverseInertiaWorld*angular;
        const Vec3f offExpected=offBefore.inverseInertiaWorld*glm::cross(lever,offLinear);
        PhysicsImpulseCommand command{bodies[2],offLinear,angular,world->ReadBody(bodies[2])->centerOfMass+lever};
        auto result=world->ApplyImpulseBatch({&command,1});BC_REQUIRE(result && (*result)->bodies.size()==1);
        const auto& applied=(*result)->bodies[0];
        BC_REQUIRE(MassClose(applied.after.linear,offLinear/mass) &&
            MassClose(applied.after.angular,applied.before.inverseInertia*(angular+glm::cross(lever,offLinear))));
        BC_REQUIRE(system.GetCompletedSteps()==0);
        Step(system);BC_REQUIRE(!system.GetBackendFailure() && system.GetCompletedSteps()==1);
        BC_REQUIRE(MassClose(world->ReadBody(bodies[0])->angularVelocity,angularExpected) &&
            MassClose(world->ReadBody(bodies[1])->angularVelocity,offExpected) &&
            MassClose(world->ReadBody(bodies[1])->linearVelocity,offLinear/mass));
        for(auto h:bodies)BC_REQUIRE(MassReadback(*world,h,radius>=.5f));
        std::printf("MASS_INERTIA_SPHERE_PASS backend=%s radius=%.9g mass=%.9g inverse=%.9g ticks=1 legacy_and_batch=1\n",
            PhysicsBackendName(backend).data(),radius,mass,world->ReadBody(bodies[0])->inverseInertiaBody[0][0]);
        return true;
    }
    inline bool MassGeometryAndTransactions(PhysicsBackend backend)
    {
        PhysicsSystem system;auto* world=new PhysicsWorld;BC_REQUIRE(world->ConfigureBackend(backend));system.SetPhysicsWorld(world);world->SetGravity({0,0,0});
        const Quat orientation=glm::angleAxis(.63f,glm::normalize(Vec3f(1,2,-1)));
        const std::array<CollisionShapeDesc,3> geometries={BoxShapeDesc{{1,2,3},{.25f,.5f,-.25f}},CapsuleShapeDesc{1,.5f},SphereShapeDesc{1}};
        std::array<PhysicsBodyHandle,3> bodies;
        for(unsigned i=0;i<geometries.size();++i) {
            auto shape=CreateCollisionShape(geometries[i]);BC_REQUIRE(shape);const auto unit=(*shape)->InertiaTensor();const auto com=(*shape)->GetCenterOfMass();
            PhysicsBodyDesc desc;desc.mass=2;desc.position={float(i)*20,0,0};desc.orientation=orientation;
            auto h=world->CreateBody(desc,*shape);BC_REQUIRE(h);bodies[i]=*h;
            BC_REQUIRE(MassReadback(*world,*h,true));
            const auto state=world->ReadBody(*h);BC_REQUIRE(state && MassClose(state->centerOfMass,desc.position+orientation*com));
            BC_REQUIRE(ChainWakeTestAccess::Raw(*world,*h)->m_Shape->InertiaTensor()==unit);
        }
        auto h=bodies[2];const auto old=*world->ReadBody(h);
        const auto distance=glm::length(world->ReadBody(bodies[0])->centerOfMass-world->ReadBody(h)->centerOfMass);
        auto joint=world->CreateDistanceJoint({bodies[0],h,{},{},distance,false});BC_REQUIRE(joint);
        const auto jointBefore=*world->ReadDistanceJoint(*joint);
        BC_REQUIRE(world->SetBodyMass(h,1));Step(system);BC_REQUIRE(!system.GetBackendFailure() && MassReadback(*world,h,true));
        // Mass invalidation may reset accumulated impulse; identity/descriptor remain authoritative.
        const auto jointAfter=world->ReadDistanceJoint(*joint);BC_REQUIRE(jointAfter && jointAfter->handle==jointBefore.handle &&
            jointAfter->descriptor.bodyA==jointBefore.descriptor.bodyA && jointAfter->descriptor.bodyB==jointBefore.descriptor.bodyB &&
            jointAfter->descriptor.localAnchorA==jointBefore.descriptor.localAnchorA && jointAfter->descriptor.localAnchorB==jointBefore.descriptor.localAnchorB &&
            jointAfter->descriptor.length==jointBefore.descriptor.length);
        BC_REQUIRE(world->DestroyJoint(*joint));
        auto properties=world->ReadBodyProperties(h);BC_REQUIRE(properties);properties->mass=2;
        BC_REQUIRE(world->UpdateBodyProperties(h,*properties) && MassReadback(*world,h,true));
        const auto beforeReplace=*world->ReadBody(h);
        BC_REQUIRE(system.ReplaceBodyCollider(h,SphereShapeDesc{.1f}) && MassReadback(*world,h));
        auto afterReplace=world->ReadBody(h);BC_REQUIRE(afterReplace && afterReplace->handle==h && afterReplace->inverseMass==beforeReplace.inverseMass &&
            afterReplace->linearVelocity==beforeReplace.linearVelocity && afterReplace->angularVelocity==beforeReplace.angularVelocity);
        BC_REQUIRE(system.SetBodyScale(h,{10,10,10}) && MassReadback(*world,h,true));
        BC_REQUIRE(world->ReadBody(h)->inverseMass==beforeReplace.inverseMass && world->ReadBody(h)->handle==old.handle);
        // Existing sampled hull tensor/COM remains the input; scale uses the admitted symmetric affine path.
        ConvexHullShapeDesc hull{{{-1,-1,-1},{2,-1,-1},{-.5f,2,-1},{0,0,2}}};
        auto hullShape=CreateCollisionShape(hull);BC_REQUIRE(hullShape);PhysicsBodyDesc hullBody;hullBody.position={100,0,0};hullBody.orientation=orientation;
        auto hullHandle=world->CreateBody(hullBody,*hullShape);BC_REQUIRE(hullHandle && MassReadback(*world,*hullHandle));
        BC_REQUIRE(system.SetBodyScale(*hullHandle,{1.25f,.75f,1.5f}) && MassReadback(*world,*hullHandle));
        const auto tensor=ChainWakeTestAccess::Raw(*world,*hullHandle)->m_Shape->InertiaTensor();
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)BC_REQUIRE(tensor[i][j]==tensor[j][i]);
        std::puts("MASS_INERTIA_GEOMETRY_PASS rotated_box_capsule offset_box_authored_tensor hull_affine_symmetry mass_replacement_scale joint_identity");
        return true;
    }
    struct InvalidInertiaSphere final:ShapeSphere
    {
        Mat3 tensor;
        explicit InvalidInertiaSphere(Mat3 value):tensor(value){}
        Mat3 InertiaTensor() const override {return tensor;}
    };
    inline bool MassFailuresAndRollback(PhysicsBackend backend)
    {
        PhysicsSystem system;auto* world=new PhysicsWorld;BC_REQUIRE(world->ConfigureBackend(backend));system.SetPhysicsWorld(world);world->SetGravity({0,0,0});
        std::array<PhysicsBodyHandle,2> bodies;
        BC_REQUIRE(AddSphere(*world,{0,0,0},bodies[0]) && AddSphere(*world,{10,0,0},bodies[1]));
        const auto first=*world->ReadBody(bodies[0]),second=*world->ReadBody(bodies[1]);
        std::array<Mat3,6> invalid={Mat3(0),Diagonal({1,0,1}),Diagonal({1,-1,1}),Mat3(1),Mat3(1),Mat3(1)};
        invalid[3][0][1]=.2f;invalid[4][0][0]=std::numeric_limits<float>::quiet_NaN();invalid[5][0][0]=std::numeric_limits<float>::infinity();
        for(const auto& tensor:invalid) {
            std::unique_ptr<PhysicalShape> shape=std::make_unique<InvalidInertiaSphere>(tensor);auto* owner=shape.get();
            PhysicsBodyDesc desc;const auto count=world->GetBodyCount();const auto rejected=world->CreateBody(desc,shape);
            BC_REQUIRE(!rejected && shape.get()==owner && world->GetBodyCount()==count && SamePhysicalState(first,*world->ReadBody(bodies[0])));
        }
        auto box=CreateCollisionShape(BoxShapeDesc{{10,20,30},{}});BC_REQUIRE(box);PhysicsBodyDesc boxDesc;boxDesc.position={1000,0,0};
        auto boxHandle=world->CreateBody(boxDesc,*box);BC_REQUIRE(boxHandle && MassReadback(*world,*boxHandle,true));
        const auto boxBefore=*world->ReadBody(*boxHandle);auto boxProperties=*world->ReadBodyProperties(*boxHandle);boxProperties.mass=1e37f;
        BC_REQUIRE(!world->SetBodyMass(*boxHandle,1e37f) && !world->UpdateBodyProperties(*boxHandle,boxProperties) &&
            SamePhysicalState(boxBefore,*world->ReadBody(*boxHandle)) && MassReadback(*world,*boxHandle,true));
        // Real finite geometry; only its proposed mass-weighted tensor is unrepresentable.
        BC_REQUIRE(world->SetBodyMass(bodies[1],1e30f));Step(system);BC_REQUIRE(!system.GetBackendFailure());
        const auto hugeBefore=*world->ReadBody(bodies[1]);auto* shapeBefore=ChainWakeTestAccess::Raw(*world,bodies[1])->m_Shape;
        const auto revision=shapeBefore->GetRevision();
        BC_REQUIRE(!system.ReplaceBodyCollider(bodies[1],SphereShapeDesc{1e5f}) && SamePhysicalState(hugeBefore,*world->ReadBody(bodies[1])) &&
            ChainWakeTestAccess::Raw(*world,bodies[1])->m_Shape==shapeBefore && shapeBefore->GetRevision()==revision);
        const auto firstNow=*world->ReadBody(bodies[0]);
        std::array<PhysicsSystem::BodyTransformEdit,2> edits={PhysicsSystem::BodyTransformEdit{bodies[0],firstNow.position+Vec3f(1,0,0),firstNow.orientation,Vec3f(2),Vec3f(1)},
            PhysicsSystem::BodyTransformEdit{bodies[1],hugeBefore.position,hugeBefore.orientation,Vec3f(1e5f),Vec3f(1)}};
        BC_REQUIRE(!system.SetBodyTransforms(edits) && SamePhysicalState(firstNow,*world->ReadBody(bodies[0])) && SamePhysicalState(hugeBefore,*world->ReadBody(bodies[1])));
        // Property rollback is distinct from the existing impulse readback injection.
        BC_REQUIRE(world->SetBodyMass(bodies[1],1));Step(system);BC_REQUIRE(!system.GetBackendFailure());
        const auto beforeA=*world->ReadBody(bodies[0]),beforeB=*world->ReadBody(bodies[1]);
        const auto propsA=*world->ReadBodyProperties(bodies[0]),propsB=*world->ReadBodyProperties(bodies[1]);
        PreparedBackendMass massA{},massB{};
        if(backend==PhysicsBackend::Jolt) {massA=*ChainWakeTestAccess::Mass(*world,bodies[0]);massB=*ChainWakeTestAccess::Mass(*world,bodies[1]);}
        auto single=propsA;single.mass=2;ChainWakeTestAccess::FailPropertyReadbackOnce(*world);
        BC_REQUIRE(!world->UpdateBodyProperties(bodies[0],single) && *world->ReadBodyProperties(bodies[0])==propsA && SamePhysicalState(beforeA,*world->ReadBody(bodies[0])));
        std::array<PhysicsBodyPropertyEdit,2> batch={PhysicsBodyPropertyEdit{bodies[0],propsA,propsA},PhysicsBodyPropertyEdit{bodies[1],propsB,propsB}};
        batch[0].after.material.friction=.25f;batch[1].after.material.restitution=.75f;
        ChainWakeTestAccess::FailPropertyReadbackOnce(*world);BC_REQUIRE(!world->UpdateBodyPropertiesBatch(batch));
        BC_REQUIRE(*world->ReadBodyProperties(bodies[0])==propsA && *world->ReadBodyProperties(bodies[1])==propsB &&
            SamePhysicalState(beforeA,*world->ReadBody(bodies[0])) && SamePhysicalState(beforeB,*world->ReadBody(bodies[1])));
        if(backend==PhysicsBackend::Jolt)BC_REQUIRE(*ChainWakeTestAccess::Mass(*world,bodies[0])==massA && *ChainWakeTestAccess::Mass(*world,bodies[1])==massB);
        batch[1].after.material.restitution=std::numeric_limits<float>::quiet_NaN();
        BC_REQUIRE(!world->UpdateBodyPropertiesBatch(batch) && SamePhysicalState(beforeA,*world->ReadBody(bodies[0])) && SamePhysicalState(beforeB,*world->ReadBody(bodies[1])));
        // Typed failure does not poison the one-shot hook or subsequent valid transactions.
        BC_REQUIRE(world->UpdateBodyProperties(bodies[0],single) && MassReadback(*world,bodies[0],true));
        std::printf("MASS_INERTIA_ROLLBACK_PASS backend=%s invalid_creation_ownership=1 invalid_last_preflight=1 property_readback_exact_mass=1 ticks=2 conservative_wake_retained=1\n",PhysicsBackendName(backend).data());
        return true;
    }
    inline bool MassFixedAndCache(PhysicsBackend backend)
    {
        PhysicsSystem system;auto* world=new PhysicsWorld;BC_REQUIRE(world->ConfigureBackend(backend));system.SetPhysicsWorld(world);world->SetGravity({0,0,0});
        auto shape=CreateCollisionShape(SphereShapeDesc{.1f});BC_REQUIRE(shape);PhysicsBodyDesc desc;desc.type=BodyType::Static;desc.mass=2;
        auto handle=world->CreateBody(desc,*shape);BC_REQUIRE(handle && MassReadback(*world,*handle));
        for(auto type:{BodyType::Dynamic,BodyType::Kinematic,BodyType::Dynamic,BodyType::Static,BodyType::Dynamic}) {
            auto props=*world->ReadBodyProperties(*handle);props.type=type;props.mass=2;
            BC_REQUIRE(world->UpdateBodyProperties(*handle,props) && MassReadback(*world,*handle));
        }
        // Warm readback, then unchanged-mass steps must not reprepare/decompose.
        BC_REQUIRE(MassReadback(*world,*handle));const auto prepares=MI::PrepareCalls,principals=MI::PrincipalCalls;
        for(unsigned i=0;i<3;++i){Step(system);BC_REQUIRE(!system.GetBackendFailure());}
        BC_REQUIRE(MI::PrepareCalls==prepares && MI::PrincipalCalls==principals);
        std::printf("MASS_INERTIA_IDLE_PASS backend=%s ticks=3 prepare_delta=0 principal_delta=0\n",PhysicsBackendName(backend).data());
        if(backend==PhysicsBackend::Jolt) {
            const auto bytes=ChainWakeTestAccess::SynchronizationBytes(*world);
            BC_REQUIRE(bytes>0 && bytes<=1024*1024 && bytes%PhysicsWorld::MaxImpulseBodies==0);
            std::printf("MASS_INERTIA_STORAGE sync_bytes=%zu record_bytes=%zu capacity=%zu no_step_resize=source_fixed_array\n",
                bytes,bytes/PhysicsWorld::MaxImpulseBodies,std::size_t(PhysicsWorld::MaxImpulseBodies));
        }
        // A fixed shape may be valid even when the retained mass cannot form a Dynamic tensor.
        auto large=CreateCollisionShape(SphereShapeDesc{1e5f});BC_REQUIRE(large);PhysicsBodyDesc fixed;fixed.type=BodyType::Static;fixed.mass=1e30f;fixed.position={1e6f,0,0};
        auto big=world->CreateBody(fixed,*large);BC_REQUIRE(big);const auto before=*world->ReadBody(*big);auto props=*world->ReadBodyProperties(*big);props.type=BodyType::Dynamic;
        BC_REQUIRE(!world->UpdateBodyProperties(*big,props) && SamePhysicalState(before,*world->ReadBody(*big)) && MassReadback(*world,*big));
        return true;
    }
    inline bool MassSynchronizationAtomicity()
    {
        PhysicsSystem system;auto* world=new PhysicsWorld;BC_REQUIRE(world->ConfigureBackend(PhysicsBackend::Jolt));system.SetPhysicsWorld(world);
        std::array<PhysicsBodyHandle,2> bodies;BC_REQUIRE(AddSphere(*world,{0,0,0},bodies[0]) && AddSphere(*world,{10,0,0},bodies[1]));
        const auto massA=*ChainWakeTestAccess::Mass(*world,bodies[0]),massB=*ChainWakeTestAccess::Mass(*world,bodies[1]);
        const auto beforeA=*ChainWakeTestAccess::Synchronization(*world,bodies[0]),beforeB=*ChainWakeTestAccess::Synchronization(*world,bodies[1]);
        BC_REQUIRE(world->SetBodyMass(bodies[0],2) && world->SetBodyVelocity(bodies[0],{.25f,0,0},{}));
        auto* rawB=ChainWakeTestAccess::Raw(*world,bodies[1]);BC_REQUIRE(rawB);const float original=rawB->m_InvMass;
        // Test-only corruption of a pending mirror, never a published SDK tensor.
        // It models a legacy caller bypassing managed admission and must stop safely.
        rawB->m_InvMass=std::numeric_limits<float>::min()*.01f;
        const auto joint=world->CreateDistanceJoint({bodies[0],bodies[1],{},{},10,false});
        BC_REQUIRE(!joint && world->GetJointCount()==0 && *ChainWakeTestAccess::Mass(*world,bodies[0])==massA &&
            *ChainWakeTestAccess::Mass(*world,bodies[1])==massB && *ChainWakeTestAccess::Synchronization(*world,bodies[0])==beforeA &&
            *ChainWakeTestAccess::Synchronization(*world,bodies[1])==beforeB);
        world->SetGravity({0,-7,0});Step(system);
        const bool stopped=system.GetBackendFailure().has_value() && system.GetCompletedSteps()==0 &&
            *ChainWakeTestAccess::Mass(*world,bodies[0])==massA && *ChainWakeTestAccess::Mass(*world,bodies[1])==massB &&
            *ChainWakeTestAccess::Synchronization(*world,bodies[0])==beforeA && *ChainWakeTestAccess::Synchronization(*world,bodies[1])==beforeB;
        // Restore only the injected mirror value. The failed system remains stopped;
        // this is not a claim to rewind or automatically resume a failed simulation.
        rawB->m_InvMass=original;BC_REQUIRE(stopped);
        BC_REQUIRE(world->ReadBody(bodies[0])->inverseMass==.5f && world->ReadBody(bodies[0])->linearVelocity==Vec3f(.25f,0,0));
        BC_REQUIRE(world->Close());
        std::puts("MASS_INERTIA_SYNC_ATOMIC_PASS sdk_mass_pose_velocity_gravity_sleep_unchanged pending_mirror_preserved ticks=0 public_joint_rejection_adapter_order_source_proof");
        return true;
    }
    inline bool MassInertia()
    {
        const auto initialTicks=CompletedCheckTicks;BC_REQUIRE(MassAlgebra());
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
            BC_REQUIRE(PhysicsBackendAvailable(backend));
            for(float radius:{.1f,.5f,1.f})for(float mass:{1.f,2.f})BC_REQUIRE(SphereMassCase(backend,radius,mass));
            BC_REQUIRE(MassGeometryAndTransactions(backend) && MassFailuresAndRollback(backend) && MassFixedAndCache(backend));
        }
        BC_REQUIRE(SphereMassCase(PhysicsBackend::Jolt,.001f,1.f) && MassSynchronizationAtomicity());
        const auto ticks=CompletedCheckTicks-initialTicks;BC_REQUIRE(ticks<=64 && CompletedCheckTicks<=2353);
        std::printf("MASS_INERTIA_CHECKS_PASS ticks=%llu\n",static_cast<unsigned long long>(ticks));return true;
    }
#endif
    inline bool IndependentCapacity()
    {
        PhysicsSystem system;auto* world=new PhysicsWorld;BC_REQUIRE(world->ConfigureBackend(PhysicsBackend::Jolt));system.SetPhysicsWorld(world);world->SetGravity({0,0,0});
        std::vector<PhysicsImpulseCommand> commands;commands.reserve(PhysicsWorld::MaxImpulseBodies);
        for(std::size_t i=0;i<PhysicsWorld::MaxImpulseBodies;++i) {
            auto shape=CreateCollisionShape(SphereShapeDesc{.1f});BC_REQUIRE(shape);PhysicsBodyDesc desc;desc.position={float(i)*3,0,0};
            auto handle=world->CreateBody(desc,*shape);BC_REQUIRE(handle);commands.push_back({*handle,{.001f,0,0}});
        }
        BC_REQUIRE(commands.size()==4096 && world->GetJointCount()==0);
        auto result=world->ApplyImpulseBatch(commands);BC_REQUIRE(result && (*result)->bodies.size()==commands.size());
        for(const auto& command:commands) {const auto state=world->ReadImpulseMotion(command.body);BC_REQUIRE(state && state->linear==Vec3f(.001f,0,0));}
        commands.back().linear.z=std::numeric_limits<float>::quiet_NaN();BC_REQUIRE(!world->ApplyImpulseBatch(commands));
        for(const auto& command:commands) {const auto state=world->ReadImpulseMotion(command.body);BC_REQUIRE(state && state->linear==Vec3f(.001f,0,0));}
        BC_REQUIRE(system.GetCompletedSteps()==0);
        std::puts("CHAIN_WAKE_CAPACITY_PASS independent_targets=4096 applied=4096 invalid_last_atomic=1 ticks=0");return true;
    }
    inline bool ChainWake()
    {
        for(unsigned links:{4u,8u,16u})if(!ChainAt301(links,false))return false;
        return ChainAt301(8,true) && ActivationTopology() && CommandSafety() && IndependentCapacity();
    }
#endif
#if defined(GENGINE_G8_RESISTANCE_CHECKS)
    inline int RunResistanceChecks();
#endif
    inline int Run()
    {
#if defined(GENGINE_G8_RESISTANCE_CHECKS)
        return RunResistanceChecks();
#else
        Log::Initialize();
        if(!Require(bool(PreEditorValidation::CheckLaunchConfig()),"existing launch config regression"))return 1;
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt})
        {
            if(!PhysicsBackendAvailable(backend)){std::printf("BACKEND_UNAVAILABLE %s\n",PhysicsBackendName(backend).data());continue;}
            if(!Lifecycle(backend) || !FreeMotionAndContact(backend))return 1;
        }
        if(PhysicsBackendAvailable(PhysicsBackend::Jolt) && !Rebuild())return 1;
#if defined(GENGINE_CHAIN_WAKE_CHECKS)
        if(!Require(PhysicsBackendAvailable(PhysicsBackend::Jolt),"CHAIN-WAKE requires actual Jolt backend") || !ChainWake())return 1;
#if defined(GENGINE_MASS_INERTIA_CHECKS)
        if(!MassInertia())return 1;
#endif
        if(!Require(CompletedCheckTicks<=4096,"accepted aggregate tick budget"))return 1;
        std::printf("CHAIN_WAKE_CHECKS_PASS ticks=%llu\n",static_cast<unsigned long long>(CompletedCheckTicks));
#endif
        std::puts("BACKEND_CHECKS_PASS");return 0;
#endif
    }
#undef BC_REQUIRE
}
#if defined(GENGINE_G8_RESISTANCE_CHECKS)
#include "JoltResistanceChecks.h"
#endif
