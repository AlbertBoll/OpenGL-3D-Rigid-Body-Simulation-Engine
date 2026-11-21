#pragma once
#ifdef GENGINE_PHASE29_QUALIFICATION
#include "RigidBodySimulation.h"
#include "Physics/JoltAuditObservation.h"
#include "Physics/PhysicsProfile.h"
#include "Scene/_Scene.h"
#include <stb_image/stb_image_write.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#if defined(GENGINE_PHASE29_STEP_SAFETY)
#include "../../GEngine/src/Physics/WorldBackend.h"
#include <thread>
namespace GEngine {
struct Phase29StepTestAccess {
    static RigidBody3D& Mirror(PhysicsWorld& w,PhysicsBodyHandle h) { return **w.Resolve(h,"safety fixture"); }
    static WorldBackend& Provider(PhysicsWorld& w) { return *w.m_Backend; }
    static void RejectNativePrepare(PhysicsWorld& w) { w.m_RejectNativePrepareForTest=true; }
};
}
#endif

extern "C" const std::uint64_t g_Phase27TargetSizes[];
extern "C" std::atomic<std::uint64_t> g_Phase28JoltStageNs[8];
extern "C" std::atomic<std::uint64_t> g_Phase28JoltStageCalls[8];

// A terminal, opt-in driver at the existing Scene::Update boundary. Raw IO and
// independent numerical analysis take place outside PhysicsSystem::Update.
namespace Rbs {
namespace Phase29 {
using namespace GEngine;
using Clock = std::chrono::steady_clock;
using Count = std::uint64_t;
constexpr unsigned LastTick = 1200;
constexpr unsigned MaxContacts = 8192;
struct Action { unsigned tick, bone; Vec3f linear, angular; };
struct Arena { Phase27Audit::ArenaEvent event; };
struct Heap { Phase27Audit::HeapEvent event; };
struct Pair { unsigned a,b; };

inline void Require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
#if defined(GENGINE_PHASE29_STEP_SAFETY)
inline std::filesystem::path safetyOutput;
inline unsigned safetyAttempts{}, safetyCompleted{}, safetyRenders{};
inline bool safetyArmed{}, safetyFinalFault{};
inline std::weak_ptr<_Scene> safetyRetiring;
inline std::uint64_t safetyDomain{};
inline void SafetyRow(const char* role,PhysicsSystem& system,unsigned attempts,unsigned completions) {
    Require(system.GetAttemptedSteps()==attempts && system.GetCompletedSteps()==completions,"case tick accounting");
    std::ofstream f(safetyOutput/"cases.tsv",std::ios::app);
    const auto failure=system.GetStepFailure();
    f<<role<<'\t'<<system.GetAttemptedSteps()<<'\t'<<system.GetCompletedSteps()<<'\t'
        <<system.GetPhysicsWorld()->GetWorldDomain()<<'\t'<<(failure?int(failure->stage):-1)<<'\t'
        <<(failure?int(failure->cause.code):-1)<<'\t'<<(failure?failure->sdkMask:0)<<'\n';
    f.close();Require(bool(f),"case evidence write");safetyCompleted+=completions;
}
#endif
inline std::filesystem::path output;
inline std::ofstream bodies,joints,costs,contacts,allocations,applied,nativeStages;
inline std::string caseName;
inline bool enabled{}, debug{}, jolt{}, listener{}, initialized{};
inline unsigned tick{};
inline Count quantum{},inclusive{},boundary{},beginCalls[3]{},beginBytes[3]{},scopeCalls[3]{},scopeBytes[3]{};
inline std::array<PhysicsBodyHandle,11> handles{};
inline std::array<Arena,128> arena{};
inline std::array<Heap,512> heap{};
inline std::weak_ptr<_Scene> teardownScene;
inline std::ofstream teardownHeap;
inline std::array<Pair,64> nativePairs{};
inline std::atomic<unsigned> arenaCount{},heapCount{};
inline unsigned nativeCount{};
inline bool nativeOverflow{};
inline std::array<Action,29> actions{};
inline unsigned actionCount{};
inline Phase26Observation::Observer updateObserver{};
inline PhysicsDiagnostic::Observer nativeObserver{};
constexpr size_t NumStages=1+static_cast<unsigned>(PhysicsDiagnostic::Stage::End);
struct Stage { Count calls,bytes,narrow,epa; };
inline std::array<Stage,NumStages> stages{};
inline unsigned stageCount{};
inline bool stageMismatch{};

inline void WriteVector(std::ostream& f,const auto& v) { f<<','<<v.x<<','<<v.y<<','<<v.z; }
inline void QuatRow(std::ostream& f,const auto& q) { f<<','<<q.w<<','<<q.x<<','<<q.y<<','<<q.z; }
inline void Tensor(std::ostream& f,const auto& m) {
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)f<<','<<m[j][i];
}
inline void Handle(std::ostream& f,const PhysicsBodyHandle& h) { f<<','<<h.index<<','<<h.generation<<','<<h.registry; }
inline Count Quantum() {
    Count q=(std::numeric_limits<Count>::max)(),last=JoltAudit::Now();
    for(unsigned i=0;i<10000;++i){const auto n=JoltAudit::Now();if(n>last)q=(std::min)(q,n-last);last=n;}
    return q;
}
inline void Open(std::ofstream& stream,const char* name,const char* columns) {
    const auto path=output/name;
    Require(!std::filesystem::exists(path),"capture destination exists");
    stream.open(path);
    stream.exceptions(std::ios::badbit|std::ios::failbit);
    stream<<std::setprecision(17)<<columns<<'\n';
}
inline unsigned Ordinal(PhysicsBodyHandle h) {
    const auto at=std::find(handles.begin(),handles.end(),h);
    Require(at!=handles.end(),"unknown contact body identity");
    return unsigned(at-handles.begin());
}
inline void SetObservers() {
    Require(!Phase26Observation::observer && !Phase27Audit::arena && !Phase27Audit::heap &&
        (!PhysicsDiagnostic::observer||jolt),"observation boundary already owned");
    updateObserver.begin=[](void*)noexcept{
        const auto start=JoltAudit::Now();
        arenaCount=0;heapCount=0;nativeCount=0;nativeOverflow=false;stageCount=0;stageMismatch=false;
        std::fill_n(scopeCalls,3,0);std::fill_n(scopeBytes,3,0);
        for(auto& n:g_Phase28JoltStageNs)n=0;
        for(auto& n:g_Phase28JoltStageCalls)n=0;
        JoltAudit::Reset();JoltAudit::countAllocations=true;boundary=JoltAudit::Now()-start+2*quantum;
    };
    updateObserver.end=[](void*,Count ns)noexcept{
        const auto start=JoltAudit::Now(); JoltAudit::countAllocations=false;inclusive=ns;
        boundary+=JoltAudit::Now()-start+2*quantum;
    };
    updateObserver.allocation=[](void*,unsigned k,bool begin)noexcept{
        if(k>=3)return;
        auto c=JoltAudit::cppCalls.load(),b=JoltAudit::cppBytes.load();
        if(begin){beginCalls[k]=c;beginBytes[k]=b;}else {scopeCalls[k]+=c-beginCalls[k];scopeBytes[k]+=b-beginBytes[k];}
    };
    Phase26Observation::observer=&updateObserver;
    Phase27Audit::arena=[](const Phase27Audit::ArenaEvent& e)noexcept{
        const unsigned i=arenaCount++;if(i<arena.size())arena[i].event=e;
    };
    Phase27Audit::heap=[](const Phase27Audit::HeapEvent& e)noexcept{
        const unsigned i=heapCount++;if(i<heap.size())heap[i].event=e;
    };
    if(!jolt){
        nativeObserver.boundary=[](void*,PhysicsDiagnostic::Stage s,const PhysicsWorld&,ManifoldCollector& manifolds,
                                  std::span<const ContactIsland>,std::size_t,int)noexcept{
            auto i=static_cast<unsigned>(s);
            if(i!=stageCount++||i>=NumStages){stageMismatch=true;return;}
            auto p=GetPhysicsProfileSnapshot();
            stages[i]={JoltAudit::cppCalls.load(),JoltAudit::cppBytes.load(),p.narrowphaseCallCount,p.epaCallCount};
            if(s!=PhysicsDiagnostic::Stage::End)return;
            for(auto& m:manifolds.m_Manifolds){
                if(!m.GetNumContacts())continue;
                const auto a=m.GetBodyAIdentity(),b=m.GetBodyBIdentity();
                auto index=[](const auto& id)noexcept{
                    for(unsigned k=0;k<handles.size();++k)
                        if(handles[k].index+1==id.GetSlot()&&handles[k].generation==id.GetGeneration())return k;
                    return unsigned(handles.size());
                };
                const Pair pair{index(a),index(b)};
                if(nativeCount==nativePairs.size() || pair.a>=handles.size() || pair.b>=handles.size()){nativeOverflow=true;return;}
                nativePairs[nativeCount++]=pair;
            }
        };
        PhysicsDiagnostic::observer=&nativeObserver;
    }
}
inline void RemoveObservers() {
    JoltAudit::countAllocations=false;
    if(Phase26Observation::observer==&updateObserver)Phase26Observation::observer=nullptr;
    Phase27Audit::arena=nullptr;Phase27Audit::heap=nullptr;
    if(PhysicsDiagnostic::observer==&nativeObserver)PhysicsDiagnostic::observer=nullptr;
}
inline bool BeginWorldTeardown(const RefPtr<_Scene>& scene) noexcept {
#if defined(GENGINE_PHASE29_STEP_SAFETY)
    if (safetyFinalFault && scene && safetyRetiring.lock()==scene) return true;
#endif
    if (!scene || teardownScene.lock()!=scene || Phase27Audit::heap || JoltAudit::countAllocations)
        return false;
    heapCount=0;
    Phase27Audit::heap=[](const Phase27Audit::HeapEvent& event) noexcept {
        const auto i=heapCount.fetch_add(1,std::memory_order_relaxed);
        if(i<heap.size())heap[i].event=event;
    };
    JoltAudit::countAllocations=true;
    return true;
}
inline void EndWorldTeardown() noexcept {
#if defined(GENGINE_PHASE29_STEP_SAFETY)
    if (safetyFinalFault) {
        try {
            std::ofstream f(safetyOutput/"shutdown.tsv");
            f<<safetyDomain<<'\t'<<safetyAttempts<<'\t'<<safetyCompleted<<'\t'<<safetyRenders<<'\t'<<safetyRetiring.expired()<<'\n';
        } catch (...) {} // Absence or truncation fails the supervisor.
        return;
    }
#endif
    JoltAudit::countAllocations=false;
    Phase27Audit::heap=nullptr;
    const unsigned count=heapCount.load();
    try {
        for(unsigned i=0;i<(std::min)(count,unsigned(heap.size()));++i) {
            const auto& event=heap[i].event;
            teardownHeap<<(event.reserve?"reserve":"free")<<','<<event.address<<','<<event.caller<<','<<event.requested<<",\n";
        }
        const char* status=count>heap.size()?"overflow":teardownScene.expired()?"complete":"scene_still_owned";
        teardownHeap<<status<<",,,,"<<count<<'\n';
        teardownHeap.close();
    } catch(const std::ios_base::failure&) {
        // A truncated or missing completion record fails the offline gate.
    }
}
inline PhysicsBodyHandle MakeBody(PhysicsWorld& w,const auto& def) {
    auto shape=CreateCollisionShape(BoxShapeDesc{def.halfExtents,def.colliderCenter});
    Require(bool(shape),"box creation");
    PhysicsBodyDesc d;d.position=def.center;d.orientation=def.orientation;d.type=def.properties.type;
    d.mass=def.properties.mass;d.material=def.properties.material;d.sleep=def.properties.sleep;
    auto h=w.CreateBody(d,*shape);Require(bool(h),"reference body creation");return *h;
}
inline void Lifetime(PhysicsBackend backend) {
    const auto p=ReferenceRagdollPacket(); auto invalid=p;invalid.bodies[0].properties.mass=0;
    Require(!ValidateRagdollPacket(invalid,backend),"zero mass accepted");
    invalid=p;invalid.joints[0].localAnchorB.x=std::numeric_limits<float>::quiet_NaN();
    Require(!ValidateRagdollPacket(invalid,backend),"nonfinite anchor accepted");
    auto makeWorld=[&]{auto w=PhysicsWorld::Create(backend);Require(bool(w),"lifetime world creation");return std::move(*w);};
    auto w=makeWorld();auto sentinelDesc=p.bodies[0];sentinelDesc.properties.type=Component::BodyType::Static;
    sentinelDesc.center={8,0,8};const auto sentinel=MakeBody(*w,sentinelDesc);
    std::array<PhysicsBodyHandle,6> b{};
    for(unsigned prefix=0;prefix<=b.size();++prefix){
        for(unsigned i=0;i<prefix;++i)b[i]=MakeBody(*w,p.bodies[i]);
        for(unsigned i=prefix;i>0;--i)Require(bool(w->DestroyBody(b[i-1])),"partial body retirement");
        Require(w->GetBodyCount()==1 && bool(w->ReadBody(sentinel)),"sentinel after body rollback");
    }
    for(unsigned i=0;i<b.size();++i)b[i]=MakeBody(*w,p.bodies[i]);
    for(unsigned j=0;j<5;++j){
        auto failed=PassiveRagdoll::Bind(*w,p,b,static_cast<RagdollJoint>(j));
        Require(!failed && w->GetJointCount()==0 && w->GetBodyCount()==7,"partial joint rollback failed");
    }
    auto foreign=makeWorld();
    for(unsigned i=0;i<b.size();++i){
        auto graph=PassiveRagdoll::Bind(*w,p,b);Require(bool(graph),"lifetime bind");
        for(unsigned k=0;k<6;++k)Require(!graph->ReadBone(*foreign,static_cast<RagdollBone>(k)),"wrong world accepted");
        const auto before=b[i];const auto oldJoints=graph->joints;
        Require(bool(w->DestroyBody(before)),"member retirement");
        Require(!graph->ReadBone(*w,static_cast<RagdollBone>(i)),"deleted member mapped");
        for(unsigned k=0;k<p.joints.size();++k){
            const bool incident=unsigned(p.joints[k].bodyA)==i||unsigned(p.joints[k].bodyB)==i;
            Require(bool(w->ReadJoint(oldJoints[k]))!=incident,"incident joint lifetime");
        }
        b[i]=MakeBody(*w,p.bodies[i]);
        Require(before!=b[i]&&!w->ReadBody(before),"body ABA");
        Require(bool(graph->Retire(*w))&&bool(graph->Retire(*w)),"repeated retirement failed");
        Require(w->GetJointCount()==0&&w->GetBodyCount()==7,"idempotent retirement");
    }
    for(auto h:b)Require(bool(w->DestroyBody(h)),"final member retirement");
    auto last=w->ReadBody(sentinel);Require(last && last->position==sentinelDesc.center,"sentinel changed");
    Require(bool(w->DestroyBody(sentinel))&&bool(w->Close())&&bool(foreign->Close()),"clean lifetime close");
}
}

struct PassiveRagdollChecks {
#if defined(GENGINE_PHASE29_STEP_SAFETY)
    static bool SafetyRequested() { return std::getenv("GENGINE_PHASE29_STEP_SAFETY_OUTPUT")!=nullptr; }
    static void PrepareSafety(RigidBodySimulationApp& app) {
        using namespace Phase29;
        using Access=Phase29StepTestAccess;
        if (safetyArmed) return;
        safetyOutput=std::getenv("GENGINE_PHASE29_STEP_SAFETY_OUTPUT");
        Require(std::filesystem::is_directory(safetyOutput) && std::filesystem::is_empty(safetyOutput),"fresh safety destination");
        Require(app.m_ActiveScene && app.m_ActiveSelection.IsRagdollScene() && app.m_Backend==PhysicsBackend::Jolt,"safety fixture selection");
        safetyArmed=true;
        const auto packet=ReferenceRagdollPacket();
        const auto dt=Timestep(1.0/60.0);
        const auto open=[&](PhysicsSystem& system,PhysicsBackend backend)->PhysicsWorld& {
            auto made=PhysicsWorld::Create(backend);Require(bool(made),"test world create");
            system.SetPhysicsWorld(made->release());return *system.GetPhysicsWorld();
        };
        {
            auto owner=std::make_unique<PhysicsSystem>();
            auto& system=*owner;
            auto& w=open(system,PhysicsBackend::Jolt);
            auto a=MakeBody(w,packet.bodies[0]), b=MakeBody(w,packet.bodies[1]);
            auto& provider=Access::Provider(w);
            const auto initial=provider.PublicationFingerprintForTest(true);
            Require(w.SetBodyMass(a,2) && w.SetBodyVelocity(a,{.25f,0,0},{}),"pending edit");
            auto& raw=Access::Mirror(w,b);const float saved=raw.m_InvMass;
            raw.m_InvMass=std::numeric_limits<float>::min()*.01f;
            Require(!w.CreateDistanceJoint({a,b,{},{},10,false}) && w.GetJointCount()==0,"joint prepare transaction");
            w.SetGravity({0,-7,0});
            const auto r=system.TryFixedStep(dt);const auto fail=w.GetStepFailure();
            Require(!r && fail && fail->stage==PhysicsStepStage::Preparation &&
                provider.PublicationFingerprintForTest(true)==initial,"preparation changed SDK");
            raw.m_InvMass=saved;
            const auto pending=w.ReadBody(a);
            Require(pending && pending->inverseMass==.5f && pending->linearVelocity==Vec3f(.25f,0,0),"pending authored readback");
            Require(!w.ReadEffectiveBody(a) && !w.SetBodyVelocity(a,{},{}),"stopped world live access");
            system.Update(dt);SafetyRow("prepare",system,1,0);
            Require(w.Close() && w.Close() && w.GetStepFailure()->cause.code==fail->cause.code,"preparation close");
            system.OnExit();
            Require(system.GetStepFailure() && system.GetStepFailure()->stage==fail->stage,"root after system teardown");
        }
        {
            auto owner=std::make_unique<PhysicsSystem>();
            auto& system=*owner;
            const auto detached=system.TryFixedStep(dt);
            Require(detached && *detached==StepDisposition::NoStep,"detached fixed step");
            auto& w=open(system,PhysicsBackend::Jolt);
            auto zero=system.TryFixedStep(Timestep(0.0));
            auto invalid=system.TryFixedStep(Timestep(-1.0));
            auto sticky=system.TryFixedStep(Timestep(std::numeric_limits<double>::quiet_NaN()));
            Require(zero && *zero==StepDisposition::NoStep && !invalid && !sticky &&
                invalid.error().code==PhysicsBodyErrorCode::InvalidDuration && sticky.error().code==invalid.error().code &&
                !system.GetAttemptedSteps() && !system.GetCompletedSteps() && w.Close(),"nonpositive admission");
        }
        PhysicsBodyHandle oldHandle{};
        {
            auto copied=_Scene::Copy(app.m_ActiveScene);Require(bool(copied),"copy committed scene");
            auto& scene=**copied;Require(bool(scene.OnRuntimeStart()),"independent scene start");
            auto& system=*scene.GetPhysicsSystem();auto& w=*system.GetPhysicsWorld();
            Require(w.GetBodyCount()==11 && w.GetBackend()==PhysicsBackend::Jolt,"all live bodies");
            w.SetGravity(app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->GetGravity());
            std::array<PhysicsBodyHandle,6> bones{};
            std::array<_Entity,11> entities{};
            std::array<PhysicsBodyPose,11> expected{};
            for (std::size_t i=0;i<entities.size();++i) {
                const auto id=i<6 ? app.m_RagdollBindings.entities[i] : app.m_RagdollBindings.environmentEntities[i-6];
                const _Entity original{id,app.m_ActiveScene.get()};
                entities[i]=scene.GetEntityByUUID(original.GetUUID());
                const auto binding=scene.GetBodyBinding(entities[i]);Require(bool(binding),"copied body binding");
                if (i<6) {
                    bones[i]=*binding;
                    const auto properties=w.ReadBodyProperties(*binding);
                    Require(properties && (*properties==packet.bodies[i].properties ||
                        w.UpdateBodyProperties(*binding,packet.bodies[i].properties)),"copied effective body properties");
                }
            }
            auto graph=PassiveRagdoll::Bind(w,packet,bones);Require(bool(graph),"copied five-joint graph");
            oldHandle=bones[0];
            const auto before=scene.CommittedPoseFingerprintForTest();
            auto& provider=Access::Provider(w);
            std::uint64_t goodMirror{}, goodHistory{};
            unsigned hooks{};
            scene.SetPaused(true);scene.Update(dt);scene.SetPaused(false);scene.Update(Timestep(0.0));
            Require(system.GetAttemptedSteps()==0,"pause and zero duration");
            system.SetBeforeStep([&](PhysicsWorld&,std::uint64_t done) {
                ++hooks;
                if(done!=1)return;
                goodMirror=provider.PublicationFingerprintForTest();
                goodHistory=scene.CommittedPoseFingerprintForTest();
                for(std::size_t i=0;i<entities.size();++i)expected[i]=system.ReadBodyPose(*scene.GetBodyBinding(entities[i])).value();
                provider.nextTestFault=WorldBackend::TestFault::LastMirror;
            });
            scene.Update(Timestep(2*scene.GetPhysicsStep().count()));
            const auto failure=system.GetStepFailure();
            Require(failure && failure->stage==PhysicsStepStage::Publication && failure->cause.code==PhysicsBodyErrorCode::NonFiniteResult &&
                goodMirror==provider.PublicationFingerprintForTest() && goodHistory==scene.CommittedPoseFingerprintForTest() &&
                goodHistory!=before && hooks==2,"two-tick publication");
            const auto timing=scene.GetPhysicsTiming();
            Require(timing.totalSteps==1 && timing.stepsLastUpdate==1 && timing.pendingSeconds==0 &&
                std::abs(timing.abandonedOnFaultSeconds-scene.GetPhysicsStep().count())<1.e-12 &&
                scene.GetRenderInterpolationAlpha()==1.0,"two-tick clock");
            for(std::size_t i=0;i<entities.size();++i) {
                const auto& transform=entities[i].GetComponent<Component::Transform3DComponent>();
                Require(glm::distance(transform.Translation,expected[i].position)<=1.e-5f &&
                    std::min(glm::length(transform.QuatRotation-expected[i].orientation),glm::length(transform.QuatRotation+expected[i].orientation))<=1.e-5f,
                    "last completed transform");
            }
            system.SetPhysicsWorld(&w);scene.Update(dt);system.Update(dt);
            Require(hooks==2 && scene.GetPhysicsTiming().stepsLastUpdate==0 && !w.ReadBody(oldHandle) &&
                !w.ReadJoint(graph->joints[0]) && !w.ApplyLinearImpulse(oldHandle,{1,0,0}),"poison re-entry");
            SafetyRow("two-tick",system,2,1);scene.OnRuntimeStop();
        }
        {
            auto owner=std::make_unique<PhysicsSystem>();
            auto& system=*owner;
            auto& w=open(system,PhysicsBackend::Jolt);
            const auto body=MakeBody(w,packet.bodies[0]);
            Access::Provider(w).nextTestFault=WorldBackend::TestFault::Capacity;
            system.Update(dt);const auto f=system.GetStepFailure();
            Require(f && f->cause.code==PhysicsBodyErrorCode::Capacity && f->stage==PhysicsStepStage::SdkUpdate && f->sdkMask==1 &&
                !w.ReadBody(body) && !w.ReadJoltDiagnostics(),"SDK capacity and quarantine");
            bool refused=false;std::thread other([&] { const auto r=w.Close();refused=!r && r.error().code==PhysicsBodyErrorCode::WrongThread; });other.join();
            Require(refused && w.Close() && w.Close() && w.GetStepFailure()->sdkMask==1,"owner-only terminal close");
            SafetyRow("capacity",system,1,0);
        }
        {
            auto owner=std::make_unique<PhysicsSystem>();
            auto& system=*owner;
            auto& w=open(system,PhysicsBackend::Native);
            const auto a=MakeBody(w,packet.bodies[0]),b=MakeBody(w,packet.bodies[1]);
            const auto length=glm::distance(w.ReadBody(a)->centerOfMass,w.ReadBody(b)->centerOfMass);
            Require(bool(w.CreateDistanceJoint({a,b,{},{},length,false})),"Native distance joint");
            unsigned hooks{};
            system.SetBeforeStep([&](PhysicsWorld& current,std::uint64_t) {
                ++hooks;const auto busy=current.Close();
                Require(!busy && busy.error().code==PhysicsBodyErrorCode::Busy,"terminal close while active");
                const auto recursive=system.TryFixedStep(dt);
                Require(!recursive && recursive.error().code==PhysicsBodyErrorCode::Busy,"recursive step admission");
            });
            const auto ok=system.TryFixedStep(dt);Require(ok && *ok==StepDisposition::Completed,"Native healthy step");
            Access::RejectNativePrepare(w);system.Update(dt);system.Update(dt);
            const auto f=system.GetStepFailure();Require(f && f->stage==PhysicsStepStage::NativePrepare &&
                f->cause.code==PhysicsBodyErrorCode::JointSolveFailed && system.HasJointFailure() && hooks==2,"Native typed failure");
            SafetyRow("native",system,2,1);Require(w.Close() && w.Close(),"Native close");
        }
        {
            auto owner=std::make_unique<PhysicsSystem>();
            auto& system=*owner;
            auto& w=open(system,PhysicsBackend::Jolt);
            const auto fresh=MakeBody(w,packet.bodies[0]);const auto stale=w.ReadBody(oldHandle);
            Require(!stale && stale.error().code==PhysicsBodyErrorCode::WrongWorld && fresh.registry!=oldHandle.registry &&
                !system.GetStepFailure() && !system.GetAttemptedSteps() && !system.GetCompletedSteps(),"fresh domain");
            Require(bool(system.TryFixedStep(dt)) && bool(w.ReadBody(fresh)),"fresh completed step");
            SafetyRow("fresh",system,1,1);Require(w.Close() && w.Close(),"fresh close");
        }
        auto& active=*app.m_ActiveScene->GetPhysicsSystem();
        Require(safetyAttempts==7 && safetyCompleted==3 && !active.GetAttemptedSteps(),"before fatal eighth attempt");
        safetyDomain=active.GetPhysicsWorld()->GetWorldDomain();
        Access::Provider(*active.GetPhysicsWorld()).nextTestFault=WorldBackend::TestFault::LastMirror;
    }
    static void RecordSafetyFault(RigidBodySimulationApp& app,const GEngine::PhysicsStepFailure& failure) {
        using namespace Phase29;
        auto& p=*app.m_ActiveScene->GetPhysicsSystem();
        if(safetyAttempts!=8 || safetyCompleted!=3 || p.GetAttemptedSteps()!=1 || p.GetCompletedSteps()!=0 ||
            failure.stage!=PhysicsStepStage::Publication || failure.cause.code!=PhysicsBodyErrorCode::NonFiniteResult) return;
        SafetyRow("fatal",p,1,0);
        safetyRetiring=app.m_ActiveScene;safetyFinalFault=true;
        std::ofstream f(safetyOutput/"fatal.tsv");
        f<<safetyDomain<<'\t'<<unsigned(failure.stage)<<'\t'<<unsigned(failure.cause.code)<<'\t'
            <<failure.cause.body.index<<'\t'<<failure.cause.body.generation<<'\t'<<failure.cause.operation<<'\n';
    }
#endif
    static bool Requested() {return std::getenv("GENGINE_PHASE29_OUTPUT")!=nullptr;}
    static void CaptureThumbnail(RigidBodySimulationApp& app) {
        using namespace Phase29;
        Require(app.m_RenderTarget&&!app.m_IsShowDebugBoundingBox&&!app.m_ShowSelectedColliderOutlines&&
            app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()==0,"thumbnail requires ordinary paused viewport");
        const auto& target=*app.m_RenderTarget;
        const auto size=target.Buffer().Description();
        Require(size.Width&&size.Height&&size.Width<=4096&&size.Height<=4096,"viewport extent");
        std::vector<std::byte> pixels(size_t(size.Width)*size.Height*4);
        Require(bool(target.ReadColor(pixels)),"viewport readback failed");
        for(unsigned y=0;y<size.Height/2;++y)
            std::swap_ranges(pixels.begin()+size_t(y)*size.Width*4,pixels.begin()+size_t(y+1)*size.Width*4,
                             pixels.begin()+size_t(size.Height-1-y)*size.Width*4);
        const auto png=output/"ordinary-viewport.png";
        Require(!std::filesystem::exists(png)&&stbi_write_png(png.string().c_str(),int(size.Width),int(size.Height),4,
                pixels.data(),int(size.Width*4)),"ordinary viewport PNG failed");
    }
    static void Capture(RigidBodySimulationApp& app) {
        using namespace Phase29;
        auto& scene=*app.m_ActiveScene;auto& w=*scene.GetPhysicsSystem()->GetPhysicsWorld();
        for(unsigned i=0;i<handles.size();++i){
            auto h=handles[i];auto b=w.ReadBody(h);Require(bool(b),"body capture");
            bodies<<tick<<','<<i;Handle(bodies,h);WriteVector(bodies,b->centerOfMass);QuatRow(bodies,b->orientation);
            WriteVector(bodies,b->linearVelocity);WriteVector(bodies,b->angularVelocity);bodies<<','<<b->inverseMass;
            Tensor(bodies,b->inverseInertiaWorld);bodies<<','<<b->sleeping;
            WriteVector(bodies,.5f*(b->localBounds.maxs-b->localBounds.mins));
            auto id=i<6?app.m_RagdollBindings.entities[i]:app.m_RagdollBindings.environmentEntities[i-6];
            _Entity entity{id,&scene};
            Require(scene.GetBodyBinding(entity)==h,"Scene/body mapping");
            auto render=scene.GetRenderTransform(entity);Require(bool(render),"render pose unavailable");
            const auto& m=render->matrix;WriteVector(bodies,Vec3f(m[3]));
            Math::Mat3 basis{glm::normalize(Vec3f(m[0])),glm::normalize(Vec3f(m[1])),glm::normalize(Vec3f(m[2]))};
            QuatRow(bodies,glm::quat_cast(basis));bodies<<'\n';
        }
        for(unsigned i=0;i<5;++i){
            auto j=w.ReadJoint(app.m_RagdollBindings.mapping.joints[i]);Require(bool(j),"joint capture");
            joints<<tick<<','<<i<<','<<int(j->descriptor.kind)<<','<<Ordinal(j->descriptor.bodyA)<<','<<Ordinal(j->descriptor.bodyB)
                <<','<<j->descriptor.collideConnected;WriteVector(joints,j->worldAnchorA);WriteVector(joints,j->worldAnchorB);WriteVector(joints,j->impulse);
            joints<<','<<j->angular.axisError<<','<<j->angular.cvError<<','<<j->angular.signedAngle
                <<','<<j->angular.angleU<<','<<j->angular.angleV<<','<<j->angular.violationU<<','<<j->angular.violationV<<'\n';
        }
    }
    static void Start(RigidBodySimulationApp& app) {
        using namespace Phase29;
        output=std::getenv("GENGINE_PHASE29_OUTPUT");const char* c=std::getenv("GENGINE_PHASE29_CASE");
        Require(c && std::filesystem::is_directory(output),"missing run configuration");
        caseName=c;debug=caseName=="LIFETIME";jolt=app.m_Backend==PhysicsBackend::Jolt;
        Require(debug||caseName=="R29_DROP_WAKE_V01"||caseName=="R29_TILT_LIMIT_WAKE_V01","unknown case");
        auto& scene=*app.m_ActiveScene;auto& physics=*scene.GetPhysicsSystem();auto& w=*physics.GetPhysicsWorld();
        Require(physics.GetCompletedSteps()==0&&w.GetJointCount()==5&&w.GetBodyCount()==11,"initial scene counts");
        for(unsigned i=0;i<6;++i)handles[i]=app.m_RagdollBindings.mapping.bodies[i];
        for(unsigned i=0;i<5;++i){
            auto h=scene.GetBodyBinding(_Entity{app.m_RagdollBindings.environmentEntities[i],&scene});
            Require(bool(h),"static environment mapping");handles[6+i]=*h;
        }
        quantum=Quantum(); Require(quantum>0&&quantum<=1000,"clock quantum exceeds 1000ns");
        initialized=true;
        if(debug){
            std::weak_ptr oldScene=app.m_ActiveScene;const auto oldHandles=handles;
            const auto generation=app.m_SceneGeneration;
            Lifetime(app.m_Backend);
            Require(app.RequestScene(app.m_ActiveSelection),"Debug failed replacement could not be queued");
            app.m_InjectSceneConstructionFailure=true;
            app.ProcessSceneRequest();
            Require(app.m_ActiveScene==oldScene.lock()&&app.m_RagdollBindings.mapping.bodies[0]==oldHandles[0]&&
                !app.m_InjectSceneConstructionFailure&&app.m_SceneGeneration==generation&&
                physics.GetCompletedSteps()==0&&w.GetJointCount()==5,"committed scene changed by failed replacement");
            for(auto h:oldHandles)Require(bool(w.ReadBody(h)),"committed mapping or room lost during rollback");
            for(unsigned repeat=0;repeat<2;++repeat){
                oldScene=app.m_ActiveScene;
                const auto previous=app.m_RagdollBindings.mapping.bodies;
                Require(app.RequestScene(app.m_ActiveSelection),"Debug Reset could not be queued");
                app.ProcessSceneRequest();
                Require(oldScene.expired(),"retired scene still owned after successful Reset");
                auto& fresh=*app.m_ActiveScene->GetPhysicsSystem();
                Require(app.m_SceneGeneration==generation+repeat+1&&fresh.GetCompletedSteps()==0&&
                    fresh.GetPhysicsWorld()->GetJointCount()==5&&fresh.GetPhysicsWorld()->GetBodyCount()==11,"Reset did not reconstruct");
                for(auto h:previous)Require(!fresh.GetPhysicsWorld()->ReadBody(h),"old generation survived Reset");
            }
            return;
        }
        Require(glm::length(w.GetGravity()-Vec3f(0,-10,0))<1.e-6f&&std::abs(scene.GetPhysicsStep().count()-1./60)<1.e-8,"gravity or timestep drifted");
        if(jolt){auto settings=w.ReadJoltSettings();Require(settings&&settings->velocitySteps==10&&settings->positionSteps==2&&settings->collisionSteps==1&&std::abs(settings->timeBeforeSleep-.5f)<1.e-6f&&std::abs(settings->pointVelocitySleepThreshold-.03f)<1.e-6f,"Jolt settings differ");}
        else Require(physics.GetSolverIterations()==1&&physics.IsSleepingEnabled(),"Native settings differ");
        for(unsigned i=0;i<handles.size();++i){
            auto b=w.ReadEffectiveBody(handles[i]);auto p=w.ReadBodyProperties(handles[i]);
            Require(b&&p&&b->shapeType==ShapeType::Box&&b->type==(i<6?Component::BodyType::Dynamic:Component::BodyType::Static),"wrong actual shape or motion");
            Require(p->material.friction==(i<6?1.f:i==6?.5f:0.f)&&p->material.restitution==(i<6?1.f:.5f)&&
                p->material.spinResistanceLength==0&&p->material.rollingResistanceLength==0,"actual contact material differs");
            if(i<6)Require(p->linearDamping==0&&p->angularDamping==0&&p->gravityFactor==1&&p->allowSleeping&&
                p->continuousCollision&&p->velocityStepsOverride==0&&p->positionStepsOverride==0,"body parameters differ");
        }
        Open(bodies,"bodies.csv","tick,bone,id,gen,world,x,y,z,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,im,i00,i01,i02,i10,i11,i12,i20,i21,i22,sleep,hx,hy,hz,rx,ry,rz,rqw,rqx,rqy,rqz");
        Open(joints,"joints.csv","tick,joint,kind,a,b,collide,ax,ay,az,bx,by,bz,ix,iy,iz,axis,cv,angle,u,v,vu,vv");
        Open(contacts,"contacts.csv","tick,a,b,nx,ny,nz,depth,normal_before,normal_after");
        Open(costs,"cost.csv","tick,inclusive_ns,observer_upper_ns,q,cpp_calls,cpp_bytes,jph_calls,jph_bytes,joint_calls,joint_bytes,graph_calls,graph_bytes,broad_calls,broad_bytes,joint_ns,contact_ns,adapter_pre_ns,sdk_ns,adapter_post_ns,stage0_ns,stage1_ns,stage2_ns,stage3_ns,stage4_ns,stage5_ns,stage6_ns,stage7_ns");
        Open(allocations,"allocations.csv","tick,allocator,reserve,pointer,caller,requested,aligned,used");
        if(jolt)Open(teardownHeap,"teardown-heap.csv","kind,pointer,caller,requested,event_count");
        Open(applied,"actions.csv","tick,bone,jx,jy,jz,lx,ly,lz,x,y,z,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,after_vx,after_vy,after_vz,after_wx,after_wy,after_wz,im,i00,i01,i02,i10,i11,i12,i20,i21,i22");
        Open(nativeStages,"native-stages.csv","tick,stage,calls,bytes,narrowphase,epa");
        std::ifstream f(output.parent_path()/"actions.txt");unsigned t,i;Vec3f l,a;
        while(f>>t>>i>>l.x>>l.y>>l.z>>a.x>>a.y>>a.z){
            Require(i<6&&t>0&&t<=LastTick&&actionCount<actions.size(),"invalid frozen action file");actions[actionCount++]={t,i,l,a};
        }
        Require(f.eof()&&actionCount==(caseName=="R29_DROP_WAKE_V01"?1u:29u),"action schedule incomplete");
        if(jolt){
            auto before=w.ReadJoltDiagnostics();Require(before&&!before->enabled,"unexpected listener state");
            auto beforeBytes=JoltAudit::cppBytes.load(),beforeCalls=JoltAudit::cppCalls.load();JoltAudit::countAllocations=true;
            auto enable=w.EnableJoltDiagnostics(true);JoltAudit::countAllocations=false;
            auto diag=w.ReadJoltDiagnostics();Require(enable&&diag&&diag->enabled&&!diag->callbacks,"listener construction");
            listener=true;std::ofstream size(output/"listener-construction.txt");
            size<<JoltAudit::cppCalls.load()-beforeCalls<<' '<<JoltAudit::cppBytes.load()-beforeBytes<<' '<<sizeof(PhysicsJoltContactSample)<<' '<<MaxContacts<<'\n';
        }
        SetObservers();
        // Query the current fixed pose directly. Interpolation is a presentation option.
        scene.SetRenderInterpolationEnabled(false);Capture(app);
    }
    static void Before(RigidBodySimulationApp& app) {
        using namespace Phase29;
        const auto t=tick+1;auto& w=*app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
        std::array<PhysicsImpulseCommand,2> batch{};unsigned n=0;
        for(unsigned i=0;i<actionCount;++i)if(actions[i].tick==t){
            Require(n<batch.size(),"action endpoint capacity");const auto& a=actions[i];
            batch[n++]={handles[a.bone],a.linear,a.angular};
        }
        if(!n)return;
        auto result=w.ApplyImpulseBatch({batch.data(),n});Require(result&&(*result)->bodies.size()==n,"atomic scheduled impulse rejected");
        for(const auto& r:(*result)->bodies){
            applied<<t<<','<<Ordinal(r.body);WriteVector(applied,r.impulse);WriteVector(applied,r.angularImpulse);WriteVector(applied,r.before.center);QuatRow(applied,r.before.orientation);
            WriteVector(applied,r.before.linear);WriteVector(applied,r.before.angular);WriteVector(applied,r.after.linear);WriteVector(applied,r.after.angular);
            applied<<','<<r.before.inverseMass;Tensor(applied,r.before.inverseInertia);applied<<'\n';
        }
    }
    static void After(RigidBodySimulationApp& app) {
        using namespace Phase29;
        auto& p=*app.m_ActiveScene->GetPhysicsSystem();auto& w=*p.GetPhysicsWorld();
        Require(p.GetCompletedSteps()==tick+1 && p.GetAttemptedSteps()==p.GetCompletedSteps() &&
            !p.GetBackendFailure()&&!p.HasJointFailure(),"failed fixed update");
        ++tick;
        const auto start=JoltAudit::Now();
        Require(arenaCount<=arena.size()&&heapCount<=heap.size()&&!nativeOverflow&&!stageMismatch&&(jolt||stageCount==NumStages),"observation lost events");
        std::span<const PhysicsJoltContactSample> observations;
        if(jolt){auto d=w.ReadJoltDiagnostics();Require(d&&d->enabled&&d->step==tick&&!d->overflow&&d->callbacks==d->contacts.size(),"contact capture overflow or stale step");observations=d->contacts;}
        // Charge the entire inclusive Update as an upper bound on every nested
        // observer (incl. listener, insertion/removal, Finish, profiler, allocator).
        // Copy the fixed provider view before any following Update. Mandatory raw
        // state/parity capture is common to both paths and remains outside U/O.
        static std::array<PhysicsJoltContactSample,MaxContacts> copy{};
        std::copy(observations.begin(),observations.end(),copy.begin());
        const auto observerUpper=inclusive+boundary+(JoltAudit::Now()-start)+2*quantum;
        Capture(app);
        for(unsigned i=0;i<observations.size();++i){auto& e=copy[i];contacts<<tick<<','<<Ordinal(e.bodyA)<<','<<Ordinal(e.bodyB);
            WriteVector(contacts,e.normal);contacts<<','<<e.penetration<<','<<e.normalVelocityBefore<<','<<e.normalVelocityAfter<<'\n';}
        for(unsigned i=0;i<nativeCount;++i)contacts<<tick<<','<<nativePairs[i].a<<','<<nativePairs[i].b<<",,,,,,\n";
        costs<<tick<<','<<inclusive<<','<<observerUpper<<','<<quantum<<','<<JoltAudit::cppCalls<<','<<JoltAudit::cppBytes<<','<<JoltAudit::joltCalls<<','<<JoltAudit::joltBytes;
        for(unsigned i=0;i<3;++i)costs<<','<<scopeCalls[i]<<','<<scopeBytes[i];
        costs<<','<<p.GetJointSolveNanoseconds()<<','<<p.GetContactSolveNanoseconds()<<','<<JoltAudit::ns[2]<<','<<JoltAudit::ns[3]<<','<<JoltAudit::ns[4];
        for(const auto& n:g_Phase28JoltStageNs)costs<<','<<n.load();costs<<'\n';
        for(unsigned i=0;i<arenaCount;++i){const auto& e=arena[i].event;allocations<<tick<<",arena,"<<e.reserve<<','<<e.address<<','<<e.caller<<','<<e.requested<<','<<e.aligned<<','<<e.used<<'\n';}
        for(unsigned i=0;i<heapCount;++i){const auto& e=heap[i].event;allocations<<tick<<",heap,"<<e.reserve<<','<<e.address<<','<<e.caller<<','<<e.requested<<",,\n";}
        if(!jolt)for(unsigned i=0;i<NumStages;++i){const auto& s=stages[i];nativeStages<<tick<<','<<i<<','<<s.calls<<','<<s.bytes<<','<<s.narrow<<','<<s.epa<<'\n';}
    }
    static void Finish(RigidBodySimulationApp& app,const char* error=nullptr) {
        using namespace Phase29;
        RemoveObservers();
        const char* secondary=nullptr;
        const auto cleanupError=[&](const char* message) {
            if (!error) error=message;
            else if (!secondary) secondary=message;
        };
        if(listener){
            auto& w=*app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            if (!w.GetStepError()) {
                auto disabled=w.EnableJoltDiagnostics(false);auto d=w.ReadJoltDiagnostics();
                if (!disabled||!d||d->enabled) cleanupError("listener retirement failed");
            }
            listener=false; // A quarantined provider detaches its listener during terminal retirement.
        }
        for(auto* stream:{&bodies,&joints,&costs,&contacts,&allocations,&applied,&nativeStages}) {
            if (stream->is_open()) {
                try { stream->close(); }
                catch (const std::ios_base::failure&) { cleanupError("fixture stream closure failed"); }
            }
        }
        if(!error && jolt && !debug && tick==LastTick)teardownScene=app.m_ActiveScene;
        if(!output.empty()){
            if (error && app.m_ActiveScene) {
                auto* system=app.m_ActiveScene->GetPhysicsSystem();
                std::ofstream fault(output/"failed-step-v1.txt");
                fault<<"attempted "<<system->GetAttemptedSteps()<<"\ncompleted "<<system->GetCompletedSteps()
                    <<"\nlast_sample "<<tick<<"\ntarget "<<tick+1<<"\nerror "<<error<<'\n';
                if (secondary) fault<<"secondary_cleanup "<<secondary<<'\n';
                if (auto root=system->GetStepFailure())
                    fault<<"stage "<<int(root->stage)<<"\ncode "<<int(root->cause.code)<<"\nsdk "<<root->sdkMask
                        <<"\nworld "<<(system->GetPhysicsWorld()?system->GetPhysicsWorld()->GetWorldDomain():0)<<"\nbody "<<root->cause.body.index
                        <<"\ngeneration "<<root->cause.body.generation<<"\njoint "<<root->jointSlot<<'\n';
            }
            std::ofstream f(output/"result.txt");f<<(error?"FAILED":"CAPTURE_COMPLETE")<<'\n'<<tick<<'\n'<<(error?error:"")<<'\n'
                <<reinterpret_cast<std::uintptr_t>(g_Phase27TargetSizes)<<'\n';
            for(unsigned i=0;i<8;++i)f<<g_Phase27TargetSizes[i]<<' ';f<<'\n';
        }
    }
};
}
#endif
