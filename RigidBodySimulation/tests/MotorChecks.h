#pragma once
#include "PassiveRagdollChecks.h"
#include <cmath>

// The process, Scene, fixed clock, allocator hooks and teardown capture are the
// same ones used by the existing RBS qualification profiles.
namespace Rbs {
struct MotorChecks {
    static bool Requested() { return std::getenv("GENGINE_PHASE30_OUTPUT") != nullptr; }
    static inline unsigned attempts{}, completions{}, frameCount{}, bodyCount{};
    static inline unsigned driverRequests{}; // Release Scene updates, counted independently of PhysicsSystem.
    static inline std::ofstream motor, safety;
    static inline bool finished{};

    static void Safety(GEngine::PhysicsBackend backend) {
        using namespace GEngine;
        using Phase29::Require;
        const unsigned firstAttempt=attempts;
        const auto packet = ReferenceAngularPacket(RbsScenePreset::MotorFree).value();
        auto system = std::make_unique<PhysicsSystem>();
        auto made = PhysicsWorld::Create(backend);
        Require(bool(made),"safety world create");
        system->SetPhysicsWorld(made->release());
        auto& w = *system->GetPhysicsWorld();
        w.SetGravity({0,-10,0});
        auto body = [&](unsigned i) {
            const auto& b=packet.bodies[i];
            auto shape=CreateCollisionShape(BoxShapeDesc{b.halfExtents,{}});
            PhysicsBodyDesc d;
            d.type=i ? Component::BodyType::Dynamic : Component::BodyType::Static;
            d.position=b.center; d.mass=b.authoredMass; d.material={b.friction,b.restitution};
            Require(bool(shape),"safety shape");
            auto h=w.CreateBody(d,*shape); Require(bool(h),"safety body"); return *h;
        };
        auto a=body(0), b=body(1);
        AngularJointDesc d;
        d.bodyA=a; d.bodyB=b; d.localAnchorB=packet.joint.localAnchorB;
        d.angular.localFrameA=packet.joint.localFrameA;
        d.angular.localFrameB=packet.joint.localFrameB;
        const auto passive=w.CreateAngularJoint(d).value();
        Require(!w.SetHingeMotor(passive,{true,2,100}) && !w.ReadJoint(passive)->descriptor.angular.motor,"passive hinge acquired a drive");
        Require(bool(w.DestroyJoint(passive)),"remove passive hinge");
        d.angular.motor=HingeMotorSettings{true,2,100};
        const auto h=w.CreateAngularJoint(d).value();
        auto foreign=PhysicsWorld::Create(backend);
        Require(foreign && !(*foreign)->SetHingeMotor(h,{true,-2,0}),"foreign handle");
        Require(!w.SetHingeMotor(h,{true,3,1}) && !w.SetHingeMotor(h,{true,0,-1}) &&
                !w.SetHingeMotor(h,{true,std::numeric_limits<float>::quiet_NaN(),1}) &&
                w.ReadJoint(h)->descriptor.angular.motor==d.angular.motor,"invalid command atomicity");
        auto step=[&](HingeMotorSettings command) {
            Require(bool(w.SetHingeMotor(h,command)),"valid motor command");
            Require(attempts<120,"Debug attempted-step limit");
            ++attempts;
            auto result=system->TryFixedStep(Timestep(1.0/60.0));
            Require(result && *result==StepDisposition::Completed,"safety fixed step");
            ++completions;
            const auto state=w.ReadJoint(h);
            Require(state && state->descriptor.angular.motor==command && state->angular.motor.valid &&
                    state->angular.motor.peakAccumulated<=state->angular.motor.impulseCap,"effective command or cap");
            return state->angular;
        };
        // Both providers use a 0.5 s dwell by default. Stay inside the shared
        // Debug ceiling even if one backend never accepts the resting island.
        bool asleep=false;
        for(unsigned t=0;t<54 && !asleep;++t) {
            const auto off=step({false,2,100});
            const auto pose=w.ReadEffectiveBody(b).value();
            Require(off.motor.impulse==0 && off.motor.work==0,"disabled motor injected work");
            asleep=pose.sleeping;
        }
        Require(asleep,"stationary unpowered hinge did not become sleep eligible");
        const auto positive=step({true,2,100});
        Require(!w.ReadEffectiveBody(b)->sleeping,"changed drive failed to wake the hinge");
        Require(positive.motor.impulse>0 && positive.motorSpeed>0 && positive.motor.work>0,"positive drive");
        const auto coast=step({false,2,100});
        Require(coast.motor.impulse==0 && coast.motor.work==0 && coast.motorSpeed>0,"disabled coast cache");
        const auto zeroCap=step({true,-2,0});
        Require(zeroCap.motor.impulse==0 && zeroCap.motor.work==0,"zero cap");
        const auto brake=step({true,0,50});
        Require(brake.motor.impulse<0 && brake.motor.work<0 && brake.motorSpeed<coast.motorSpeed,"zero-speed brake");
        const auto reverse=step({true,-2,100});
        Require(reverse.motor.impulse<0 && reverse.motorSpeed<brake.motorSpeed,"reverse");
        Require(w.DestroyBody(b) && !w.ReadJoint(h) && !w.SetHingeMotor(h,{true,1,100}),"retired joint accepted");
        d.bodyB=body(1);
        const auto rebound=w.CreateAngularJoint(d).value();
        Require(rebound!=h && !w.ReadJoint(rebound)->angular.motor.valid,"fresh bounded cache");
        const auto id=w.GetWorldDomain();
        system->SetPhysicsWorld(nullptr);
        auto fresh=PhysicsWorld::Create(backend);
        Require(fresh && (*fresh)->GetWorldDomain()!=id && !(*fresh)->ReadJoint(rebound),"fresh world domain");
        safety<<PhysicsBackendName(backend)<<','<<id<<','<<(*fresh)->GetWorldDomain()<<','<<attempts-firstAttempt<<','<<attempts-firstAttempt<<",PASS\n";
        safety.flush();
    }

    static void Capture(RigidBodySimulationApp& app) {
        using namespace GEngine;
        using namespace Phase29;
        auto& w=*app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
        for (unsigned i=0;i<bodyCount;++i) {
            const auto b=w.ReadEffectiveBody(handles[i]);
            Require(bool(b),"body state unavailable");
            bodies<<tick<<','<<i; Handle(bodies,handles[i]); WriteVector(bodies,b->centerOfMass); QuatRow(bodies,b->orientation);
            WriteVector(bodies,b->linearVelocity); WriteVector(bodies,b->angularVelocity); bodies<<','<<b->inverseMass;
            Tensor(bodies,b->inverseInertiaWorld); bodies<<','<<b->sleeping;
            WriteVector(bodies,.5f*(b->localBounds.maxs-b->localBounds.mins)); bodies<<'\n';
        }
        const auto s=w.ReadJoint(app.m_AngularBindings.joint);
        Require(s && s->descriptor.angular.motor,"hinge readback");
        const auto& drive=*s->descriptor.angular.motor;
        const auto& v=s->angular;
        motor<<tick<<','<<drive.enabled<<','<<drive.targetVelocity<<','<<drive.maxTorque<<','<<v.motorSpeed<<','<<v.motorError
             <<','<<v.motor.valid<<','<<v.motor.impulse<<','<<v.motor.impulseCap<<','<<v.motor.peakAccumulated<<','<<v.motor.work
             <<','<<v.motorSaturated<<','<<v.signedAngle<<','<<v.axisError<<','<<v.violationU<<','<<(v.motor.limitSide ? v.motor.limitSide : v.limitU);
        WriteVector(motor,s->residual); motor<<'\n';
    }
    static void Begin(RigidBodySimulationApp& app) {
        using namespace GEngine;
        using namespace Phase29;
        const auto preset=app.m_ActiveSelection.scene;
        Require(IsMotorScene(preset) && app.SandboxEnabled(),"fixture requires ordinary Motors room");
        const auto& bindings=app.m_AngularBindings;
        auto& scene=*app.m_ActiveScene;
        auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();
        jolt=world.GetBackend()==PhysicsBackend::Jolt;
        quantum=Quantum();
        Require(quantum>0 && quantum<=1000,"clock resolution");
        bodyCount=unsigned(bindings.bodyCount+bindings.environmentEntities.size());
        Require(world.GetBodyCount()==bodyCount && world.GetJointCount()==1,"fixture topology");
        for (unsigned i=0;i<bindings.bodyCount;++i) handles[i]=bindings.bodies[i];
        for (unsigned i=0;i<bindings.environmentEntities.size();++i)
            handles[bindings.bodyCount+i]=scene.GetBodyBinding(_Entity{bindings.environmentEntities[i],&scene}).value();
        Open(bodies,"bodies.csv","tick,bone,id,gen,world,x,y,z,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,im,i00,i01,i02,i10,i11,i12,i20,i21,i22,sleep,hx,hy,hz");
        Open(motor,"motor.csv","tick,enabled,target,torque,speed,error,valid,impulse,cap,peak,work,saturated,angle,axis,violation,side,dx,dy,dz");
        Open(contacts,"contacts.csv","tick,a,b,nx,ny,nz,depth,normal_before,normal_after");
        Open(costs,"cost.csv","tick,inclusive_ns,observer_upper_ns,q,cpp_calls,cpp_bytes,jph_calls,jph_bytes,joint_calls,joint_bytes,graph_calls,graph_bytes,broad_calls,broad_bytes");
        Open(allocations,"allocations.csv","tick,allocator,reserve,pointer,caller,requested,aligned,used");
        if (jolt) {
            Open(teardownHeap,"teardown-heap.csv","kind,pointer,caller,requested,event_count");
            auto baseCalls=JoltAudit::cppCalls.load(), baseBytes=JoltAudit::cppBytes.load();
            JoltAudit::countAllocations=true;
            auto on=world.EnableJoltDiagnostics(true);
            JoltAudit::countAllocations=false;
            auto view=world.ReadJoltDiagnostics();
            Require(on && view && view->enabled && !view->callbacks,"contact listener setup");
            listener=true;
            std::ofstream(output/"listener-construction.txt")<<JoltAudit::cppCalls.load()-baseCalls<<' '
                <<JoltAudit::cppBytes.load()-baseBytes<<' '<<sizeof(PhysicsJoltContactSample)<<' '<<MaxContacts<<'\n';
        }
        std::ofstream(output/"address.txt")<<reinterpret_cast<std::uintptr_t>(g_Phase27TargetSizes)<<'\n';
        Open(applied,"load.csv","tick,impulse,pre_speed,inverse_inertia");
        SetObservers();
        Capture(app);
        Require(app.m_SimulationState==SimulationState::Ready && scene.IsPaused(),"fixture expected a fresh Ready scene");
        app.SetSimulationState(SimulationState::Running);
        Require(app.m_SimulationState==SimulationState::Running && !scene.IsPaused(),"Ready-to-Running transition rejected");
    }

    static void Step(RigidBodySimulationApp& app) {
        using namespace GEngine;
        using namespace Phase29;
        auto& scene=*app.m_ActiveScene;
        auto& system=*scene.GetPhysicsSystem();
        auto& w=*system.GetPhysicsWorld();
        const auto preset=app.m_ActiveSelection.scene;
        Require(app.m_SimulationState==SimulationState::Running && !scene.IsPaused(),"scene paused before fixed-step request");
        Require(driverRequests<1200 && driverRequests==tick && system.GetAttemptedSteps()==tick &&
                system.GetCompletedSteps()==tick && scene.GetPhysicsTiming().pendingSeconds==0.0,
                "unexpected fixed-step counts or backlog before request");
        const unsigned next=tick+1;
        const float dt=float(scene.GetPhysicsStep().count());
        HingeMotorSettings command{true,2,100};
        if (preset==RbsScenePreset::MotorFree) {
            if(next>=394 && next<=423) command.enabled=false;
            if(next>=424) command.targetVelocity=next<817 ? 0.f : -2.f;
        } else if(preset==RbsScenePreset::MotorContact) {
            if(next>=601) command.targetVelocity=0;
            if(next>=1081) command.enabled=false;
        } else {
            if(next>=361 && next<=720) command.targetVelocity=-2;
            if(next>=1081) command.enabled=false;
        }
        Require(bool(w.SetHingeMotor(app.m_AngularBindings.joint,command)),"scheduled command");
        if(preset==RbsScenePreset::MotorFree && next<=30) {
            const auto b=w.ReadBody(handles[1]).value();
            const auto a=w.ReadBody(handles[0]).value();
            auto joint=w.ReadJoint(app.m_AngularBindings.joint).value();
            const Vec3f axis=glm::normalize(a.orientation*joint.descriptor.angular.localFrameA)*Vec3f(1,0,0);
            const float j=-command.maxTorque*dt;
            applied<<next<<','<<j<<','<<glm::dot(axis,b.angularVelocity-a.angularVelocity)<<','
                   <<glm::dot(axis,b.inverseInertiaWorld*axis)<<'\n';
            Require(bool(w.ApplyAngularImpulse(handles[1],axis*j)),"opposing stall load");
        }
        ++driverRequests;
        scene.Update(Timestep(scene.GetPhysicsStep().count()));
        Require(!system.GetStepFailure() && system.GetCompletedSteps()==next && system.GetAttemptedSteps()==next,"fixed step failed or miscounted");
        tick=next; // Both PhysicsSystem counters matched this completed tick.
        const auto start=JoltAudit::Now();
        Require(arenaCount<=arena.size() && heapCount<=heap.size() && !nativeOverflow &&
            !stageMismatch && (jolt || stageCount==NumStages),"incomplete observer evidence");
        std::span<const PhysicsJoltContactSample> observations;
        if(jolt) {
            auto diag=w.ReadJoltDiagnostics();
            Require(diag && diag->enabled && diag->step==tick && !diag->overflow &&
                    diag->callbacks==diag->contacts.size(),"contact callback accounting");
            observations=diag->contacts;
        }
        static std::array<PhysicsJoltContactSample,MaxContacts> copy{};
        std::copy(observations.begin(),observations.end(),copy.begin());
        const auto overhead=inclusive+boundary+(JoltAudit::Now()-start)+2*quantum;
        Capture(app);
        for(size_t i=0;i<observations.size();++i) {
            const auto& c=copy[i];contacts<<tick<<','<<Ordinal(c.bodyA)<<','<<Ordinal(c.bodyB);
            WriteVector(contacts,c.normal); contacts<<','<<c.penetration<<','<<c.normalVelocityBefore<<','<<c.normalVelocityAfter<<'\n';
        }
        for(unsigned i=0;i<nativeCount;++i) contacts<<tick<<','<<nativePairs[i].a<<','<<nativePairs[i].b<<",,,,,,\n";
        costs<<tick<<','<<inclusive<<','<<overhead<<','<<quantum<<','<<JoltAudit::cppCalls<<','<<JoltAudit::cppBytes<<','
             <<JoltAudit::joltCalls<<','<<JoltAudit::joltBytes;
        for(unsigned i=0;i<3;++i) costs<<','<<scopeCalls[i]<<','<<scopeBytes[i];
        costs<<'\n';
        for(unsigned i=0;i<arenaCount;++i) {
            const auto& e=arena[i].event; allocations<<tick<<",arena,"<<e.reserve<<','<<e.address<<','<<e.caller<<','<<e.requested<<','<<e.aligned<<','<<e.used<<'\n';
        }
        for(unsigned i=0;i<heapCount;++i) {
            const auto& e=heap[i].event;allocations<<tick<<",heap,"<<e.reserve<<','<<e.address<<','<<e.caller<<','<<e.requested<<",,\n";
        }
    }
    static bool Run(RigidBodySimulationApp& app) {
        using namespace GEngine;
        using namespace Phase29;
        if (finished) return true;
        if (++frameCount==1) { output=std::getenv("GENGINE_PHASE30_OUTPUT"); return false; }
        std::string failure;
        try {
#ifdef GENGINE_CONFIG_DEBUG
            PassiveRagdollChecks::CaptureThumbnail(app);
            Open(safety,"safety.csv","backend,domain,fresh_domain,attempted,completed,result");
            Safety(PhysicsBackend::Jolt);
            Safety(PhysicsBackend::Native);
            const auto oldJoint=app.m_AngularBindings.joint;
            const auto generation=app.m_SceneGeneration;
            std::weak_ptr oldScene=app.m_ActiveScene;
            Require(app.RequestScene(app.m_ActiveSelection),"unable to request rollback check");
            app.m_InjectSceneConstructionFailure=true;
            app.ProcessSceneRequest();
            Require(app.m_ActiveScene==oldScene.lock() && generation==app.m_SceneGeneration &&
                    app.m_AngularBindings.joint==oldJoint,"failed construction changed the committed Motor world");
            Require(app.RequestScene(app.m_ActiveSelection),"unable to request Reset");
            app.ProcessSceneRequest();
            Require(oldScene.expired() && app.m_AngularBindings.joint!=oldJoint &&
                    !app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->ReadJoint(oldJoint),"stale Reset mapping");
            Require(app.RequestBackend(PhysicsBackend::Native),"unable to request Native switching");
            app.ProcessSceneRequest();
            Require(app.m_Backend==PhysicsBackend::Native &&
                    app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->ReadJoint(app.m_AngularBindings.joint)->descriptor.angular.motor==
                      app.m_Angular.committed->joint.motor,"backend reconstruction lost motor");
            safety<<"scene_reset_switch,"<<generation<<','<<app.m_SceneGeneration<<",0,0,PASS\n";
#else
            if (!driverRequests) Begin(app);
            Step(app);
            if (tick<1200) return false;
#endif
        } catch (const std::exception& e) {
            failure=e.what();
            auto* physics=app.m_ActiveScene->GetPhysicsSystem();
            if (const auto fault=physics->GetStepFailure()) {
                failure+=std::format(" code={} stage={} sdk={} joint={} cause={}",
                    unsigned(fault->cause.code),unsigned(fault->stage),fault->sdkMask,fault->jointSlot,fault->cause.operation);
                app.FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Physics","BackendStep",
                    std::string(fault->cause.operation),std::string(PhysicsBackendName(app.m_Backend)),failure});
            }
        }
        RemoveObservers();
        if (listener) {
            auto* w=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            if (!w->GetStepError() && !w->EnableJoltDiagnostics(false)) failure+=" listener shutdown";
            listener=false;
            teardownScene=app.m_ActiveScene;
        }
        for (auto* stream:{&bodies,&motor,&costs,&contacts,&allocations,&applied,&safety})
            if (stream->is_open()) stream->close();
#ifdef GENGINE_CONFIG_DEBUG
        std::ofstream(output/"outcome.txt")<<(failure.empty()?"CAPTURE_COMPLETE":failure)<<'\n'<<attempts<<' '<<completions<<'\n';
#else
        const auto& system=*app.m_ActiveScene->GetPhysicsSystem();
        std::ofstream(output/"outcome.txt")<<(failure.empty()?"CAPTURE_COMPLETE":failure)<<'\n'
            <<system.GetAttemptedSteps()<<' '<<system.GetCompletedSteps()<<'\n'<<"driver_requests "<<driverRequests<<'\n';
#endif
        finished=true;
        return true;
    }
};
}
