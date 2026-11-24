#pragma once
#include "PassiveRagdollChecks.h"

extern "C" std::atomic<std::uint64_t> g_Phase28ObserverWrapperNs, g_Phase28ObserverWrapperCalls;
extern "C" std::atomic<std::uint64_t> g_Phase28JoltProfileHookNs, g_Phase28JoltProfileHookCalls;
namespace Rbs {
struct MoverChecks {
    using Count=Phase29::Count;
    struct StepSample {
        GEngine::PhysicsMoverSample mover;
        std::array<GEngine::PhysicsBodyState,7> bodies;
        std::array<Phase29::Arena,128> arena;
        std::array<Phase29::Heap,512> heap;
        unsigned arenaSize{},heapSize{},nativePairs{};
        Count inclusive{},observerUpper{},cppCalls{},cppBytes{},jphCalls{},jphBytes{};
        Count allocatorHooks{},profileHooks{},hookCalls{},boundaryNs{},captureNs{};
        bool valid{};
    };
    static bool Requested(){return std::getenv("GENGINE_PHASE31_OUTPUT")!=nullptr;}
    static inline unsigned frame{},schedule{1},tick{},retiredAttempts{},retiredCompleted{};
    static inline std::filesystem::path root;
    static inline std::ofstream samples,poses,timing,events;
    static inline RigidBodySimulationApp* active{};
    static inline void (*originalEnd)(void*,Count) noexcept{};
    static inline unsigned slot{};
    static inline std::uintptr_t jobPage{};
    static inline std::array<GEngine::Phase27Audit::HeapEvent,4> pageEvents{};
    static inline std::atomic<unsigned> pageCount{};
    static void RetireReplay(RigidBodySimulationApp& app){
        using namespace GEngine;using Phase29::Require;
        if(!Phase29::jolt){Replace(app,false);return;}
        pageCount=0;
        Phase27Audit::heap=[](const Phase27Audit::HeapEvent& e) noexcept{
            if(jobPage && e.address==jobPage){
                const auto n=pageCount.fetch_add(1);if(n<pageEvents.size())pageEvents[n]=e;
            }
        };
        JoltAudit::countAllocations=true;
        struct ReleaseHook{~ReleaseHook(){JoltAudit::countAllocations=false;Phase27Audit::heap=nullptr;}} guard;
        Replace(app,false); // Keep the existing transaction, including destruction of its retiring scene.
        const auto n=pageCount.load();Require(n<=pageEvents.size(),"tracked job page event capacity");
        std::ofstream out(Phase29::output/"teardown-heap.csv");
        out<<"kind,pointer,caller,requested,event_count\n";
        for(unsigned i=0;i<n;++i){
            const auto& e=pageEvents[i];out<<(e.reserve?"reserve":"free")<<','<<e.address<<','<<e.caller<<','<<e.requested<<",\n";
        }
        out<<"complete,,,,"<<n<<'\n';
        jobPage=0;
    }
    static inline void (*originalBegin)(void*) noexcept{};
    static void Started(void* context) noexcept {
        const auto start=GEngine::JoltAudit::Now();
        g_Phase28ObserverWrapperNs=0;g_Phase28ObserverWrapperCalls=0;
        g_Phase28JoltProfileHookNs=0;g_Phase28JoltProfileHookCalls=0;
        const auto setup=GEngine::JoltAudit::Now()-start;
        originalBegin(context);Phase29::boundary+=setup+2*Phase29::quantum;
    }
    static std::array<StepSample,2>& Buffer(){static std::array<StepSample,2> buffer{};return buffer;}
    static void Counts(const char* event,RigidBodySimulationApp& app){
        auto& system=*app.m_ActiveScene->GetPhysicsSystem();
        std::ofstream out(root/"domains.csv",std::ios::app);
        out<<event<<','<<system.GetPhysicsWorld()->GetWorldDomain()<<','<<system.GetAttemptedSteps()<<','
           <<system.GetCompletedSteps()<<','<<app.m_ActiveScene->GetPhysicsTiming().totalSteps<<'\n';
    }
    static void Replace(RigidBodySimulationApp& app,bool changeBackend){
        using namespace GEngine;
        using Phase29::Require;
        auto& old=*app.m_ActiveScene->GetPhysicsSystem();
        const auto h=app.m_MoverBindings.bodies[0];
        std::weak_ptr retiring=app.m_ActiveScene;
        const auto na=static_cast<unsigned>(old.GetAttemptedSteps()),nc=static_cast<unsigned>(old.GetCompletedSteps());
        app.SetSimulationState(SimulationState::Ready);
        app.m_EditorMode=RigidBodySimulationApp::EditorMode::Edit;
        Counts("retiring",app);
        Require(changeBackend?app.RequestBackend(PhysicsBackend::Native):app.RequestScene(app.m_ActiveSelection),"world replacement request");
        app.ProcessSceneRequest();
        if(retiring.expired()){retiredAttempts+=na;retiredCompleted+=nc;}
        auto& system=*app.m_ActiveScene->GetPhysicsSystem();
        Require(retiring.expired() && !system.GetPhysicsWorld()->ReadMover(h) && !system.GetAttemptedSteps() &&
            !system.GetCompletedSteps() && !app.m_ActiveScene->GetPhysicsTiming().totalSteps,"fresh generation or clock");
        Counts("fresh",app);
    }
    static void Topology(RigidBodySimulationApp& app) {
        using namespace GEngine;
        using Phase29::Require;
        auto& world=*app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
        const auto ids=app.m_MoverBindings.bodies;
        Require(app.SandboxEnabled() && app.m_ActiveSelection.scene==RbsScenePreset::Movers &&
            world.GetBodyCount()==7 && !world.GetJointCount() && ids[0]!=ids[1],"ordinary mover topology");
        Phase29::handles={};Phase29::handles[0]=ids[0];Phase29::handles[1]=ids[1];
        std::size_t n=2;
        for(std::size_t i=0;i<world.GetBodyCount();++i){
            const auto h=world.BodyAt(i).value();if(h==ids[0] || h==ids[1])continue;
            Require(n<7 && world.ReadBody(h)->type==Component::BodyType::Static,"room binding");
            Phase29::handles[n++]=h;
        }
        const auto a=world.ReadEffectiveBody(ids[0]).value(),r=world.ReadEffectiveBody(ids[1]).value();
        const auto reference=ReferenceMoverPacket();
        Require(n==7 && a.type==Component::BodyType::Kinematic && !a.inverseMass &&
            r.type==Component::BodyType::Dynamic && r.inverseMass==1 && world.GetGravity()==reference.gravity &&
            a.position==reference.path.PositionAt(0) && r.position==reference.riderPosition &&
            a.material==reference.platform.properties.material && r.material==reference.rider.properties.material &&
            a.linearVelocity==Vec3f(0) && r.linearVelocity==Vec3f(0) && a.angularVelocity==Vec3f(0) && r.angularVelocity==Vec3f(0) &&
            a.shapeType==ShapeType::Box && r.shapeType==ShapeType::Box &&
            .5f*(a.localBounds.maxs-a.localBounds.mins)==reference.platform.halfExtents &&
            .5f*(r.localBounds.maxs-r.localBounds.mins)==reference.rider.halfExtents,"provider reference readback");
        const auto properties=world.ReadBodyProperties(ids[1]).value();
        Require(!properties.linearDamping && !properties.angularDamping && properties.gravityFactor==1 &&
            properties.allowSleeping && !world.ReadMover(ids[0])->tick,"rider work and sleep preconditions");
        if(world.GetBackend()==PhysicsBackend::Jolt){
            const auto d=world.ReadJoltDiagnostics();
            const auto settings=world.ReadJoltSettings();
            Require(d && !d->enabled && settings && settings->collisionSteps==1,"unexpected Jolt contact listener or substeps");
        }
    }
    static void DebugCases(RigidBodySimulationApp& app){
        using namespace GEngine;
        using Phase29::Require;
        Topology(app);
        auto& scene=*app.m_ActiveScene;auto& system=*scene.GetPhysicsSystem();auto& w=*system.GetPhysicsWorld();
        const auto ids=app.m_MoverBindings.bodies;const auto h=scene.GetPhysicsStep().count();
        auto path=app.m_Mover.committed.path;path.direction={0,1,0};
        auto bad=app.m_Mover.committed;bad.riderPosition=bad.path.center;
        Require(!path.Normalized() && !ValidateMoverPacket(bad) && !w.QueueMoverCommand(ids[1],MoverCommand::Stop) &&
            !w.ApplyLinearImpulse(ids[0],{1,0,0}) && !w.SetBodyVelocity(ids[0],{3,0,0},{}),"typed role and path rejection");
        path=app.m_Mover.committed.path;path.amplitude=0;
        Require(path.Normalized() && path.PositionAt(100)==path.center,"stationary path");
        app.SetSimulationState(SimulationState::Running);
        auto step=[&](unsigned count){
            const auto before=system.GetAttemptedSteps();
            Require(!scene.IsPaused() && retiredAttempts+before+count<=24,"Debug step reservation");
            scene.Update(Timestep(h*count));
            Require(!system.GetStepFailure() && system.GetAttemptedSteps()==before+count &&
                system.GetCompletedSteps()==before+count && scene.GetPhysicsTiming().totalSteps==before+count,"successful Scene clocks");
        };
        step(2);
        const auto first=w.ReadMover(ids[0]).value();
        Require(first.tick==2 && first.phaseTick==2 && first.position.z<0,"forward motion");
        app.SetSimulationState(SimulationState::Paused);
        Require(bool(w.QueueMoverCommand(ids[0],MoverCommand::Stop)) && bool(w.QueueMoverCommand(ids[0],MoverCommand::Stop)),"queue Stop");
        scene.Update(Timestep(2*h));
        const auto paused=w.ReadMover(ids[0]).value();
        Require(scene.IsPaused() && paused.commandPending && paused.phaseTick==2 && system.GetAttemptedSteps()==2,"pause consumed work");
        app.SetSimulationState(SimulationState::Running); step(1);
        const auto stopped=w.ReadMover(ids[0]).value();
        Require(!stopped.enabled && stopped.phaseTick==2 && stopped.velocity==Vec3f(0) && stopped.position==first.position,"stop failed to hold");
        Require(w.QueueMoverCommand(ids[0],MoverCommand::Reverse) && w.QueueMoverCommand(ids[0],MoverCommand::Start),"queue reverse start");
        step(1);
        Require(w.ReadMover(ids[0])->phaseTick==1 && w.ReadMover(ids[0])->velocity.z>0,"reverse did not resume");
        const auto initial=w.ReadBody(ids[1])->linearVelocity;
        Require(w.ApplyLinearImpulse(ids[1],{.25f,0,0}) && w.ReadBody(ids[1])->linearVelocity.x>initial.x,"rider impulse");
        std::ofstream out(root/"safety.csv",std::ios::app);
        out<<PhysicsBackendName(w.GetBackend())<<','<<w.GetWorldDomain()<<','<<system.GetAttemptedSteps()<<','
           <<system.GetCompletedSteps()<<','<<w.ReadMover(ids[0])->phaseTick<<",PASS\n";
    }
    static void IsolatedRejection(GEngine::PhysicsBackend backend){
        using namespace GEngine;using Phase29::Require;
        PhysicsSystem system;
        auto made=PhysicsWorld::Create(backend);Require(bool(made),"isolated world creation");
        system.SetPhysicsWorld(made->release());auto& w=*system.GetPhysicsWorld();
        struct Debit{
            PhysicsSystem& system;
            ~Debit(){
                retiredAttempts+=static_cast<unsigned>(system.GetAttemptedSteps());
                retiredCompleted+=static_cast<unsigned>(system.GetCompletedSteps());
                std::ofstream out(root/"domains.csv",std::ios::app);
                out<<"invalid_duration,"<<system.GetPhysicsWorld()->GetWorldDomain()<<','<<system.GetAttemptedSteps()<<','<<system.GetCompletedSteps()<<",0\n";
            }
        } debit{system};
        const auto packet=ReferenceMoverPacket();auto stationary=packet.path;stationary.amplitude=0;
        auto create=[&](bool platform){
            const auto& body=platform?packet.platform:packet.rider;
            auto shape=CreateCollisionShape(BoxShapeDesc{body.halfExtents,{}}).value();
            PhysicsBodyDesc d;d.type=body.properties.type;d.position=platform?stationary.center:packet.riderPosition;
            return w.CreateBody(d,shape).value();
        };
        const auto p=create(true),r=create(false);constexpr double h=1./60;
        auto wrong=w.RegisterMover(r,p,stationary,h);
        Require(!wrong && wrong.error().code==PhysicsBodyErrorCode::WrongBodyType && w.RegisterMover(p,r,stationary,h),"typed registration");
        Require(retiredAttempts+2+8<=24,"isolated attempt reservation");
        Require(system.TryFixedStep(Timestep(h)) && w.ReadMover(p)->position==stationary.center && w.ReadMover(p)->velocity==Vec3f(0),"stationary path step");
        const auto rejected=system.TryFixedStep(Timestep(h*.5));
        const auto fault=system.GetStepFailure();
        Require(!rejected && fault && fault->cause.code==PhysicsBodyErrorCode::InvalidDuration &&
            fault->stage==PhysicsStepStage::Preparation && system.GetAttemptedSteps()==2 &&
            system.GetCompletedSteps()==1,"typed rejection or partial advancement");
    }
    static void Snapshot(StepSample& s) {
        auto& system=*active->m_ActiveScene->GetPhysicsSystem();auto& w=*system.GetPhysicsWorld();
        s.mover=w.ReadMover(Phase29::handles[0]).value();
        for(unsigned i=0;i<7;++i)s.bodies[i]=w.ReadBody(Phase29::handles[i]).value();
    }
    static void Completed(void* context,Count ns) noexcept {
        using namespace Phase29;
        originalEnd(context,ns);
        if(slot>=2){++slot;return;}
        auto& s=Buffer()[slot++];s.valid=false;
        try{
            const auto start=JoltAudit::Now();
            s.arenaSize=arenaCount.load();s.heapSize=heapCount.load();s.nativePairs=nativeCount;
            if(s.arenaSize>s.arena.size() || s.heapSize>s.heap.size() || nativeOverflow || stageMismatch ||
                (!jolt && stageCount!=NumStages))return;
            std::copy_n(arena.begin(),s.arenaSize,s.arena.begin());std::copy_n(heap.begin(),s.heapSize,s.heap.begin());
            s.inclusive=inclusive;s.cppCalls=JoltAudit::cppCalls;s.cppBytes=JoltAudit::cppBytes;
            s.jphCalls=JoltAudit::joltCalls;s.jphBytes=JoltAudit::joltBytes;
            s.allocatorHooks=g_Phase28ObserverWrapperNs.load();s.profileHooks=g_Phase28JoltProfileHookNs.load();
            s.hookCalls=g_Phase28ObserverWrapperCalls.load()+g_Phase28JoltProfileHookCalls.load();
            s.captureNs=JoltAudit::Now()-start+2*quantum;s.boundaryNs=boundary;
            // Include worker hook time when its sum exceeds the full main-thread bracket.
            s.observerUpper=(std::max)(inclusive,s.allocatorHooks+s.profileHooks+2*quantum*s.hookCalls)+boundary+s.captureNs;
            Snapshot(s); // Common per-tick parity data, outside measured Update and incremental observer.
            s.valid=true;
        }catch(...){s.valid=false;}
    }
    static void Write(const StepSample& s,bool cost) {
        using namespace Phase29;
        const auto& m=s.mover;const auto t=m.tick;
        samples<<t<<','<<m.phaseTick<<','<<m.phaseTime<<','<<m.enabled<<','<<m.reversed<<','<<m.pairInContact<<','<<m.slipValid<<','<<m.riderSlip;
        WriteVector(samples,m.target);WriteVector(samples,m.velocity);WriteVector(samples,m.riderRelativeVelocity);samples<<'\n';
        for(unsigned i=0;i<7;++i){const auto& b=s.bodies[i];
            poses<<t<<','<<i;Handle(poses,b.handle);WriteVector(poses,b.centerOfMass);QuatRow(poses,b.orientation);
            WriteVector(poses,b.linearVelocity);WriteVector(poses,b.angularVelocity);poses<<','<<b.inverseMass<<','<<b.sleeping;
            WriteVector(poses,.5f*(b.localBounds.maxs-b.localBounds.mins));poses<<'\n';
        }
        if(!cost)return;
        timing<<t<<','<<s.inclusive<<','<<s.observerUpper<<','<<quantum<<','<<s.cppCalls<<','<<s.cppBytes<<','<<s.jphCalls<<','<<s.jphBytes<<','<<s.allocatorHooks<<','<<s.profileHooks<<','<<s.hookCalls<<','<<s.boundaryNs<<','<<s.captureNs<<'\n';
        for(unsigned i=0;i<s.arenaSize;++i){const auto& e=s.arena[i].event;
            events<<t<<",arena,"<<e.reserve<<','<<e.address<<','<<e.caller<<','<<e.requested<<','<<e.aligned<<','<<e.used<<'\n';}
        for(unsigned i=0;i<s.heapSize;++i){const auto& e=s.heap[i].event;
            if(t==1 && e.reserve && e.requested==229376)jobPage=e.address;
            events<<t<<",heap,"<<e.reserve<<','<<e.address<<','<<e.caller<<','<<e.requested<<",,\n";}
    }
    static void BeginReplay(RigidBodySimulationApp& app) {
        using namespace GEngine;using Phase29::Open;using Phase29::Require;
        Topology(app);active=&app;Phase29::jolt=app.m_Backend==PhysicsBackend::Jolt;
        Phase29::quantum=Phase29::Quantum();Require(Phase29::quantum && Phase29::quantum<=1000,"clock quantum");
        Phase29::output=root/std::to_string(schedule);
        Require(std::filesystem::create_directory(Phase29::output),"fresh schedule output");
        Open(samples,"mover.csv","tick,phase_tick,phase_time,enabled,reversed,contact,slip_valid,slip,tx,ty,tz,vx,vy,vz,rx,ry,rz");
        Open(poses,"bodies.csv","tick,bone,id,gen,world,x,y,z,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,im,sleep,hx,hy,hz");
        Open(timing,"cost.csv","tick,inclusive_ns,observer_upper_ns,q,cpp_calls,cpp_bytes,jph_calls,jph_bytes,wrapper_ns,profile_hooks_ns,hook_calls,boundary_ns,capture_ns");
        Open(events,"allocations.csv","tick,allocator,reserve,pointer,caller,requested,aligned,used");
        std::ofstream(Phase29::output/"address.txt")<<reinterpret_cast<std::uintptr_t>(g_Phase27TargetSizes)<<'\n';
        auto& s=Buffer()[0];Snapshot(s);Write(s,false);
        Phase29::SetObservers();originalBegin=Phase29::updateObserver.begin;Phase29::updateObserver.begin=Started;
        originalEnd=Phase29::updateObserver.end;Phase29::updateObserver.end=Completed;
        app.SetSimulationState(SimulationState::Running);
        Require(!app.m_ActiveScene->IsPaused(),"start ordinary Running motion");tick=0;
    }
    static void ReplayFrame(RigidBodySimulationApp& app) {
        using namespace GEngine;using Phase29::Require;
        auto& scene=*app.m_ActiveScene;auto& system=*scene.GetPhysicsSystem();auto& w=*system.GetPhysicsWorld();
        const auto platform=app.m_MoverBindings.bodies[0];const auto h=scene.GetPhysicsStep().count();
        if(tick==360 || tick==600){
            const auto before=w.ReadMover(platform).value();
            app.SetSimulationState(SimulationState::Paused);
            Require(bool(w.QueueMoverCommand(platform,tick==360?MoverCommand::Stop:MoverCommand::Reverse)),"interleaved command");
            for(unsigned i=0;i<60;++i){
                scene.Update(Timestep(schedule*h));
                Require(scene.IsPaused() && system.GetAttemptedSteps()==tick && scene.GetPhysicsTiming().totalSteps==tick,"pause advanced work");
            }
            const auto after=w.ReadMover(platform).value();
            Require(after.phaseTick==before.phaseTick && after.position==before.position && after.commandPending,"pause changed motion");
            app.SetSimulationState(SimulationState::Running);
        }
        if(tick==540 || tick==1440)Require(bool(w.QueueMoverCommand(platform,tick==540?MoverCommand::Start:MoverCommand::Stop)),"scheduled command");
        Require(!scene.IsPaused() && !scene.GetPhysicsTiming().pendingSeconds && system.GetAttemptedSteps()==tick &&
            retiredAttempts+tick+schedule<=3240,"attempt admission or Ready state");
        slot=0;scene.Update(Timestep(schedule*h));
        Require(!system.GetStepFailure() && system.GetAttemptedSteps()==tick+schedule && system.GetCompletedSteps()==tick+schedule &&
            scene.GetPhysicsTiming().totalSteps==tick+schedule && slot==schedule,"actual fixed-step counts");
        for(unsigned i=0;i<schedule;++i){
            const auto& s=Buffer()[i];Require(s.valid && s.mover.tick==tick+i+1,"missing intermediate body or cost sample");Write(s,true);
        }
        tick+=schedule;
    }
    static bool Run(RigidBodySimulationApp& app) {
        using namespace GEngine;using Phase29::Require;
        std::string failure;
        try{
            if(!frame++){
                root=std::getenv("GENGINE_PHASE31_OUTPUT");Phase29::output=root;
                Require(app.m_ActiveScene && app.m_ActiveScene->IsPaused(),"expected fresh paused scene");
                return false;
            }
#ifdef GENGINE_CONFIG_DEBUG
            PassiveRagdollChecks::CaptureThumbnail(app);
            Require(app.m_Backend==PhysicsBackend::Jolt,"Jolt-first safety fixture");
            const auto generation=app.m_SceneGeneration;std::weak_ptr old=app.m_ActiveScene;
            Require(app.RequestScene(app.m_ActiveSelection),"rollback request");
            app.m_InjectSceneConstructionFailure=true;app.ProcessSceneRequest();
            Require(old.lock()==app.m_ActiveScene && generation==app.m_SceneGeneration &&
                !app.m_InjectSceneConstructionFailure,"atomic candidate rollback");
            IsolatedRejection(PhysicsBackend::Jolt);DebugCases(app);Replace(app,true);
            Require(app.m_Backend==PhysicsBackend::Native,"Native switch");
            IsolatedRejection(PhysicsBackend::Native);DebugCases(app);Replace(app,false);Topology(app);
#else
            if(schedule==1 && !tick && app.m_Backend==PhysicsBackend::Jolt){
                // The first layout can precede the first visible Scene submission.
                if(frame<9)return false;
                PassiveRagdollChecks::CaptureThumbnail(app);
            }
            if(!tick)BeginReplay(app);
            for(unsigned outer=0;outer<30 && tick<1620;++outer)ReplayFrame(app);
            if(tick<1620)return false;
            Phase29::RemoveObservers();
            for(auto* stream:{&samples,&poses,&timing,&events})stream->close();
            if(schedule==1){RetireReplay(app);schedule=2;tick=0;return false;}
#endif
        }catch(const std::exception& err){failure=err.what();}
        Phase29::RemoveObservers();
        for(auto* stream:{&samples,&poses,&timing,&events})if(stream->is_open())stream->close();
        Counts("exit",app);
        const auto& system=*app.m_ActiveScene->GetPhysicsSystem();
        if(auto fault=system.GetStepFailure())failure+=" "+std::string(fault->cause.operation);
        Phase29::output=root;
        Phase29::Open(Phase29::teardownHeap,"teardown-heap.csv","kind,pointer,caller,requested,event_count");
        Phase29::teardownScene=app.m_ActiveScene;
        std::ofstream(root/"outcome.txt")<<(failure.empty()?"CAPTURE_COMPLETE":failure)<<'\n'
            <<retiredAttempts+system.GetAttemptedSteps()<<' '<<retiredCompleted+system.GetCompletedSteps()<<'\n';
        return true;
    }
};
} // namespace Rbs
