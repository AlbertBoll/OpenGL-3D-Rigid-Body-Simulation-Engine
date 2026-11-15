#pragma once
#ifdef GENGINE_JOLT_FINAL_AUDIT
#include "Physics/JoltAuditObservation.h"
#include "Physics/PhysicsProfile.h"
#include "JoltParameterExperiment.h"
#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib,"psapi.lib")
#include <cstdlib>
#include <malloc.h>
namespace Rbs::FinalAudit {
using namespace GEngine;
struct Sample {
    std::array<std::uint64_t,5> ns{};
    std::uint64_t cppCalls{},cppBytes{},joltCalls{},joltBytes{},tempPeak{},tempCalls{},tempBytes{},visits{},frameInterval{};
    unsigned active{},sleeping{},contacts{};double kinetic{},potential{},jointError{},depth{};
};
inline std::array<Sample,600> samples{};
inline unsigned ticks{},contacts{};inline double depth{};
inline bool started{},observed{};inline std::filesystem::path path;
inline std::uint64_t frameStart{},previousStart{};
inline PhysicsDiagnostic::Observer nativeObserver{};
inline void Memory(const char* name) {
    PROCESS_MEMORY_COUNTERS_EX m{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));
    std::ofstream(path/name)<<"working_set_bytes="<<m.WorkingSetSize<<" private_bytes="<<m.PrivateUsage<<" peak_working_set_bytes="<<m.PeakWorkingSetSize<<'\n';
}
inline void Snapshot(_Scene& scene,const char* name) {
    std::ofstream o(path/name);o<<std::setprecision(9);
    JoltParameterExperiment::Snapshot(*scene.GetPhysicsSystem()->GetPhysicsWorld(),o,ticks);
}
inline bool Begin(_Scene& scene) {
    const char* output=std::getenv("GENGINE_FINAL_AUDIT_OUTPUT");if(!output)return false;
    if(!started){
        path=output;if(std::filesystem::exists(path/"samples.csv"))throw std::runtime_error("Audit output already exists");
        std::filesystem::create_directories(path);observed=std::getenv("GENGINE_FINAL_AUDIT_OBSERVED")!=nullptr;
        auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();
        if(observed && world.GetBackend()==PhysicsBackend::Jolt){if(!world.EnableJoltDiagnostics(true))throw std::runtime_error("Diagnostic admission failed");}
        if(observed && world.GetBackend()==PhysicsBackend::Native){
            nativeObserver.boundary=[](void*,PhysicsDiagnostic::Stage stage,const PhysicsWorld&,ManifoldCollector& m,std::span<const ContactIsland>,std::size_t,int) noexcept {
                if(stage!=PhysicsDiagnostic::Stage::End)return;
                contacts=0;depth=0;for(auto& manifold:m.m_Manifolds)for(int i=0;i<manifold.GetNumContacts();++i){++contacts;depth=std::max(depth,double(-manifold.GetContact(i).separationDistance));}
            };PhysicsDiagnostic::observer=&nativeObserver;
        }
        Snapshot(scene,"initial-bodies.csv");Memory("memory-start.txt");
        std::ofstream o(path/"settings.txt");o<<std::setprecision(9)<<"bodies="<<world.GetBodyCount()<<" joints="<<world.GetJointCount()<<" dt="<<scene.GetPhysicsStep().count()<<" native_iterations="<<scene.GetPhysicsSystem()->GetSolverIterations()<<" sleeping="<<scene.GetPhysicsSystem()->IsSleepingEnabled()<<" observed="<<observed<<'\n';
        if(auto p=world.ReadJoltSettings())JoltParameterExperiment::Settings(o,"effective",*p);
        if(auto p=world.ReadJoltNativeDefaults())JoltParameterExperiment::Settings(o,"native_sdk",*p);
        started=true;
    }
    frameStart=JoltAudit::Now();if(ticks) samples[ticks-1].frameInterval=frameStart-previousStart;previousStart=frameStart;
    JoltAudit::Reset();contacts=0;depth=0;return true;
}
inline bool After(_Scene& scene) {
    JoltAudit::countAllocations=false;
    auto& system=*scene.GetPhysicsSystem();auto& world=*system.GetPhysicsWorld();
    if(system.GetCompletedSteps()!=ticks+1 || system.HasJointFailure() || system.GetBackendFailure())throw std::runtime_error("Audit step/identity invariant failed");
    auto& s=samples[ticks];s.ns=JoltAudit::ns;s.cppCalls=JoltAudit::cppCalls;s.cppBytes=JoltAudit::cppBytes;
    s.joltCalls=JoltAudit::joltCalls;s.joltBytes=JoltAudit::joltBytes;s.tempPeak=JoltAudit::tempPeak;s.tempCalls=JoltAudit::tempCalls;s.tempBytes=JoltAudit::tempBytes;s.visits=JoltAudit::bodyVisits;
    s.contacts=contacts;s.depth=depth;
    if(auto diag=world.ReadJoltDiagnostics();diag && diag->enabled){
        if(diag->overflow)throw std::runtime_error("Contact capture overflow");s.contacts=unsigned(diag->callbacks);
        for(const auto& c:diag->contacts)s.depth=std::max(s.depth,double(c.penetration));
    }
    for(std::size_t i=0;i<world.GetBodyCount();++i){auto h=world.BodyAt(i);if(!h)throw std::runtime_error("BodyAt failed");auto b=world.ReadBody(*h);if(!b)throw std::runtime_error("ReadBody failed");
        if(b->type!=Component::BodyType::Dynamic)continue;
        b->sleeping?++s.sleeping:++s.active;
        s.kinetic+=.5*glm::dot(b->linearVelocity,b->linearVelocity)/b->inverseMass;
        if(std::abs(glm::determinant(b->inverseInertiaWorld))>1e-12f)s.kinetic+=.5*glm::dot(b->angularVelocity,glm::inverse(b->inverseInertiaWorld)*b->angularVelocity);
        s.potential-=glm::dot(world.GetGravity(),b->centerOfMass)/b->inverseMass;
    }
    for(std::size_t i=0;i<world.GetJointCount();++i){auto h=world.JointAt(i);if(!h)throw std::runtime_error("JointAt failed");auto j=world.ReadDistanceJoint(*h);if(!j)throw std::runtime_error("ReadJoint failed");s.jointError=std::max(s.jointError,double(std::abs(j->error)));}
    ++ticks;if(ticks<samples.size())return false;
    Snapshot(scene,"final-bodies.csv");Memory("memory-end.txt");
    std::ofstream o(path/"samples.csv");o<<std::setprecision(12)<<"tick,scene_ns,physics_ns,adapter_pre_ns,jolt_update_ns,adapter_post_ns,cpp_calls,cpp_bytes,jolt_calls,jolt_bytes,temp_peak,temp_calls,temp_bytes,adapter_slot_visits,frame_interval_ns,active,sleeping,contacts,kinetic,potential,joint_error,contact_depth\n";
    for(unsigned i=0;i<ticks;++i){const auto& r=samples[i];o<<i+1;for(auto n:r.ns)o<<','<<n;
        o<<','<<r.cppCalls<<','<<r.cppBytes<<','<<r.joltCalls<<','<<r.joltBytes<<','<<r.tempPeak<<','<<r.tempCalls<<','<<r.tempBytes<<','<<r.visits<<','<<r.frameInterval<<','<<r.active<<','<<r.sleeping<<','<<r.contacts<<','<<r.kinetic<<','<<r.potential<<','<<r.jointError<<','<<r.depth<<'\n';}
    PhysicsDiagnostic::observer=nullptr;
    std::ofstream(path/"complete.txt")<<ticks<<" finite ticks; ordinary Scene scheduler; no setting overrides\n";
    return true;
}
inline void* Allocate(std::size_t n,std::size_t alignment=0) {
    void* p=alignment?_aligned_malloc(n?n:1,alignment):std::malloc(n?n:1);if(!p)throw std::bad_alloc();JoltAudit::Alloc(n);return p;
}
}
#ifdef GENGINE_JOLT_FINAL_AUDIT_ALLOC_IMPLEMENTATION
void* operator new(std::size_t n){return Rbs::FinalAudit::Allocate(n);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){return Rbs::FinalAudit::Allocate(n,std::size_t(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{_aligned_free(p);}
#endif
#endif
