#pragma once
// One translation unit only (RbsFrame.cpp). No ordinary-build definitions.
#if !defined(GENGINE_PHASE25_QUALIFICATION) || !defined(GENGINE_PHASE25_ISOLATED_BUILD)
#error PhysicsCostChecks belongs only to isolated Phase25 qualification products.
#endif
#include "Renderer/SceneRenderResources.h"
#include "Physics/PhysicsProfile.h"
#include "Physics/ShapeCapsule.h"
#include "../../external/sdl2/include/sdl2/SDL.h"
#include <Windows.h>
#include <atomic>
#include <bit>
#include <fstream>
#include <filesystem>
#include <malloc.h>
#include <new.h>
#include <cstring>
#include <iomanip>

namespace Rbs
{
    std::expected<::GEngine::CollisionShapeDesc, ::GEngine::GeometryAuthoringCode>
        FitMeshCollider(const ::GEngine::MeshAsset&, const ::GEngine::Vec3f&, ::GEngine::ShapeType);
    std::expected<void, std::string> ReplaceMeshCollider(::GEngine::_Scene&, ::GEngine::SceneRenderResources&,
                                                       ::GEngine::_Entity, ::GEngine::ShapeType);
}

namespace Phase25Memory
{
    struct Counts
    {
        std::uint64_t allocations{}, frees{}, requested{}, usable{}, peakRequested{}, peakUsable{};
        std::uint64_t transientRequested{}, transientUsable{}, survivingRequested{}, survivingUsable{};
        std::uint64_t liveRequested{}, liveUsable{};
        std::uint64_t peakTemporaryRequested{}, peakTemporaryUsable{};
    };
    struct Entry
    {
        void* pointer{};
        std::size_t requested{}, usable{};
        std::uint64_t interval{};
        unsigned row{}, owner{};
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
        std::uint64_t birthSequence{}, birthNs{};
        unsigned trace{}, tick{};
#endif
    };
    inline constexpr std::size_t Capacity = 262144;
    inline Entry entries[Capacity]{};
    inline Counts rowCounts[21]{};
    inline Counts current{};
    inline std::atomic_flag lock = ATOMIC_FLAG_INIT;
    inline std::atomic<unsigned> errors{};
    inline std::uint64_t serial{}, liveRequested{}, liveUsable{}, fixtureLive{}, rowLive[21]{};
    inline std::uint64_t processStaticLive{}, processStaticAllocated{};
    struct ProcessStaticRecord
    {std::uintptr_t pointer{},container{},storage{};std::size_t requested{},usable{};unsigned row{};};
    inline ProcessStaticRecord processStaticRecords[6]{};
    inline unsigned processStaticCount{};
    inline thread_local unsigned owner{}, row{};
    inline thread_local std::uint64_t interval{};
    inline thread_local bool observing{};
    inline unsigned ownerThread{};
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
    // Attribution only: absent from all qualifying CPU/allocation products.
    // Fixed storage and raw addresses; symbol resolution runs after process exit.
    inline constexpr unsigned TraceCapacity=4096, TraceDepth=40, WatchCapacity=64;
    struct Trace {void* frames[TraceDepth]{};unsigned depth{};};
    struct Watch {void* pointer{};std::uint64_t birthSequence{};bool freed{};};
    inline Trace traces[TraceCapacity]{};
    inline Watch watches[WatchCapacity]{};
    inline unsigned watchCount{}, currentTick=UINT_MAX;
    inline std::uint64_t eventSequence{};
    inline HANDLE lifetimeFile=INVALID_HANDLE_VALUE;
    // One diagnostic-owned file handle intentionally spans app teardown so late
    // frees remain observable. The OS closes it at process exit; no object,
    // Physics callback or Scene survives solely for this sink.
    inline void Lifetime(unsigned operation,const Entry& e) noexcept
    {
        const std::uint64_t values[]={operation,++eventSequence,e.birthSequence,
            reinterpret_cast<std::uintptr_t>(e.pointer),e.requested,e.usable,
            ::GEngine::Phase25Observation::Nanoseconds(),e.trace};
        DWORD written{};
        if(lifetimeFile==INVALID_HANDLE_VALUE || !WriteFile(lifetimeFile,values,sizeof values,&written,nullptr) || written!=sizeof values)++errors;
    }
    inline unsigned Intern(const Trace& trace) noexcept
    {
        std::uint64_t h=1469598103934665603ull;
        for(unsigned i=0;i<trace.depth;++i){h^=reinterpret_cast<std::uintptr_t>(trace.frames[i]);h*=1099511628211ull;}
        auto at=unsigned(h&(TraceCapacity-1));
        for(unsigned n=0;n<TraceCapacity;++n,at=(at+1)&(TraceCapacity-1)){
            auto& stored=traces[at];
            if(!stored.depth){stored=trace;return at+1;}
            if(stored.depth==trace.depth && !std::memcmp(stored.frames,trace.frames,trace.depth*sizeof(void*)))return at+1;
        }
        ++errors;return 0;
    }
    inline void ExitMarker() noexcept
    {Entry empty{};Lifetime(3,empty);}
#endif
    struct Lock { Lock(){while(lock.test_and_set(std::memory_order_acquire)) {}} ~Lock(){lock.clear(std::memory_order_release);} };
    inline void* Tombstone() { return reinterpret_cast<void*>(std::uintptr_t{1}); }
    inline std::size_t Slot(void* p) { return ((reinterpret_cast<std::uintptr_t>(p)>>4)*11400714819323198485ull) & (Capacity-1); }
    inline void Add(void* p,std::size_t requested,std::size_t usable) noexcept
    {
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
        Trace trace;
        if(owner)trace.depth=CaptureStackBackTrace(1,TraceDepth,trace.frames,nullptr);
#endif
        Lock held;
        auto at=Slot(p), empty=Capacity;
        for(std::size_t n=0;n<Capacity;++n,at=(at+1)&(Capacity-1)) {
            auto& e=entries[at];
            if(e.pointer==p){++errors;return;}
            if(e.pointer==Tombstone() && empty==Capacity)empty=at;
            if(!e.pointer){if(empty==Capacity)empty=at;break;}
        }
        if(empty==Capacity){++errors;return;}
        entries[empty]={p,requested,usable,interval,row,owner};
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
        auto& added=entries[empty];added.birthSequence=++eventSequence;
        if(owner){added.birthNs=::GEngine::Phase25Observation::Nanoseconds();added.trace=Intern(trace);added.tick=currentTick;}
#endif
        if(observing)++errors;
        if(owner && GetCurrentThreadId()!=ownerThread)++errors;
        if(owner==1||owner==2) { fixtureLive+=usable;rowLive[row]+=usable; }
        if(owner==1) {
            liveRequested+=requested;liveUsable+=usable;
            auto add=[&](Counts& c){++c.allocations;c.requested+=requested;c.usable+=usable;
                c.peakRequested=(std::max)(c.peakRequested,liveRequested);c.peakUsable=(std::max)(c.peakUsable,liveUsable);};
            add(rowCounts[row]);if(interval){add(current);
                current.peakTemporaryRequested=(std::max)(current.peakTemporaryRequested,current.requested-current.transientRequested);
                current.peakTemporaryUsable=(std::max)(current.peakTemporaryUsable,current.usable-current.transientUsable);}
        }
    }
    inline void Remove(void* p) noexcept
    {
        if(!p)return;
        Lock held;auto at=Slot(p);
        for(std::size_t n=0;n<Capacity;++n,at=(at+1)&(Capacity-1)) {
            auto& e=entries[at];if(!e.pointer)break;
            if(e.pointer!=p)continue;
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
            for(unsigned i=0;i<watchCount;++i)if(watches[i].pointer==p && watches[i].birthSequence==e.birthSequence){
                if(watches[i].freed)++errors;watches[i].freed=true;Lifetime(2,e);}
#endif
            if(e.owner==1||e.owner==2){fixtureLive-=e.usable;rowLive[e.row]-=e.usable;}
            if(e.owner==3)processStaticLive-=e.usable;
            if(e.owner==1) {
                liveRequested-=e.requested;liveUsable-=e.usable;++rowCounts[e.row].frees;
                if(interval) {
                    ++current.frees;
                    if(e.interval==interval){current.transientRequested+=e.requested;current.transientUsable+=e.usable;}
                }
            }
            e.pointer=Tombstone();return;
        }
        ++errors;
    }
    inline void Begin() noexcept
    {
        if(interval)++errors;
        interval=++serial;current={};current.peakRequested=liveRequested;current.peakUsable=liveUsable;
        owner=1;
    }
    inline Counts End() noexcept
    {
        current.liveRequested=liveRequested;current.liveUsable=liveUsable;
        current.survivingRequested=current.requested-current.transientRequested;
        current.survivingUsable=current.usable-current.transientUsable;
        interval=0;owner=0;return current;
    }
    struct Scope {unsigned previous; explicit Scope(unsigned value):previous(owner){owner=value;} ~Scope(){owner=previous;} };
#ifdef GENGINE_PHASE25_ALLOCATIONS
    inline void* Allocate(std::size_t size,std::size_t alignment=0)
    {
        const auto requested=size;size=(std::max)(size,std::size_t{1});
        for(;;) {
            void* p=alignment?_aligned_malloc(size,alignment):std::malloc(size);
            if(p){Add(p,requested,alignment?_aligned_msize(p,alignment,0):_msize(p));return p;}
            if(!_callnewh(size))throw std::bad_alloc();
        }
    }
#endif
}
#ifdef GENGINE_PHASE25_ALLOCATIONS
void* operator new(std::size_t n){return Phase25Memory::Allocate(n);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{Phase25Memory::Remove(p);std::free(p);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept{::operator delete(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept{::operator delete[](p);}
void* operator new(std::size_t n,std::align_val_t a){return Phase25Memory::Allocate(n,std::size_t(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept{Phase25Memory::Remove(p);_aligned_free(p);}
void operator delete[](void* p,std::align_val_t a) noexcept{::operator delete(p,a);}
void operator delete(void* p,std::size_t,std::align_val_t a) noexcept{::operator delete(p,a);}
void operator delete[](void* p,std::size_t,std::align_val_t a) noexcept{::operator delete(p,a);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept{try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t a,const std::nothrow_t&) noexcept{::operator delete(p,a);}
void operator delete[](void* p,std::align_val_t a,const std::nothrow_t&) noexcept{::operator delete(p,a);}
#endif

namespace Rbs::PhysicsCost
{
    using namespace ::GEngine;
    using namespace ::GEngine::Component;
    using Phase25Memory::Counts;
    inline constexpr unsigned MaxSteps=20000;
    struct Record
    {
        unsigned row{},tick{},measured{},sleeping{},bodies{},islands{},commandKind{};
        std::uint64_t state{},totalNs{},fitNs{},commandNs{},physicalState{},stateCheckNs{};
        double penetration{};
        PhysicsProfileSnapshot profile{};
        PhysicsSystem::SleepTimings sleep{};
        Counts stepMemory{},fitMemory{},commandMemory{};
    };
    static_assert(MaxSteps*sizeof(Record)+sizeof(Phase25Memory::entries)<64ull*1024*1024);
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
    static_assert(MaxSteps*sizeof(Record)+sizeof(Phase25Memory::entries)+sizeof(Phase25Memory::traces)+sizeof(Phase25Memory::watches)<64ull*1024*1024);
#endif
    inline void Hash(std::uint64_t& h,const void* bytes,std::size_t size)
    {for(std::size_t i=0;i<size;++i){h^=static_cast<const unsigned char*>(bytes)[i];h*=1099511628211ull;}}
    template<class T> inline void Hash(std::uint64_t& h,const T& v){Hash(h,&v,sizeof v);}
    inline void HashState(std::uint64_t& h,const PhysicsDiagnostic::BodyState& s)
    {Hash(h,s.position);Hash(h,s.linear);Hash(h,s.angular);Hash(h,s.orientation);Hash(h,s.inactive);Hash(h,s.sleeping);}
    struct Session
    {
        std::unique_ptr<Record[]> records;
        unsigned count{},row{},tick{},total{},warm{},bodyCount{},dynamicCount{},completeRows{},stepCallbacks{};
        bool debug{},failed{},complete{},rowDone{};
        const char* failure="";
        char mode{};
        std::uint64_t q{},clockLoopElapsed{},initialHashes[21]{},orderHashes[21]{};
        std::uint64_t firstSleepingHash{},floorBaseline{},commandAllocationBase{};
        std::filesystem::path output,lattice;
        RefPtr<_Scene> original,scene;
        bool originalPaused{};
        std::uint32_t originalFrameLimit{},actualFrameLimit{};
        std::array<_Entity,257> entities{};
        std::array<PhysicsBodyHandle,257> handles{};
        std::array<PhysicsDiagnostic::BodyState,257> prior{};
        std::array<bool,257> priorValid{};
        std::vector<RestDiagnosis::BodyInput> latticeInputs;
        SceneRenderResources* resources{};
        Window* window{};
        const MeshAsset* fitSource{};
        CollisionShapeDesc frozenDescription{SphereShapeDesc{1}};
        ShapeType replacementType=ShapeType::Sphere;
        Asset::MeshHandle targetMesh{};
        MaterialHandle material{};
        Vec3f replacementScale{1};
        PhysicsBodyHandle replacementHandle{};
        Phase25Observation::Observer observer{};
        Record* current{};
#ifdef GENGINE_PHASE25_W64_DIAGNOSTIC
        struct WakeState { unsigned tick{},ordinal{},stage{},contacts{}; PhysicsDiagnostic::BodyState state{};
            bool eligible{},canSleep{}; double linearThreshold{},angularThreshold{},dwell{},slip{},depth{};
            float friction{},spin{},rolling{}; };
        struct WakeStats { unsigned reasons[11]{},operations[23]{},sleepToAwake{},awakeToSleep{};
            glm::dvec3 solverDv{},solverDw{}; };
        static constexpr unsigned WakeCapacity=360*64*5;
        std::unique_ptr<WakeState[]> wakeStates;
        std::unique_ptr<WakeStats[]> wakeStats;
        std::array<const RigidBody3D*,65> wakeBodies{};
        unsigned wakeCount{};
        PhysicsDiagnostic::Observer wakeObserver{};
        int WakeOrdinal(const RigidBody3D* b)const noexcept
        {for(unsigned i=0;i<65;++i)if(wakeBodies[i]==b)return int(i);return -1;}
        void WakeBoundary(PhysicsDiagnostic::Stage stage,ManifoldCollector& manifolds) noexcept
        {
            using S=PhysicsDiagnostic::Stage;
            if(stage!=S::Begin&&stage!=S::Wake&&stage!=S::Solve&&stage!=S::Sleep&&stage!=S::End)return;
            for(unsigned i=1;i<=64;++i){
                if(wakeCount>=WakeCapacity){Fail("W64 diagnostic state capacity");return;}
                const auto& b=*wakeBodies[i];auto& v=wakeStates[wakeCount++];
                v.tick=tick;v.ordinal=i;v.stage=unsigned(stage);v.state=PhysicsDiagnostic::Capture(b);
                const auto& settings=b.GetSleepSettings();v.linearThreshold=settings.linearSpeedThreshold;
                v.angularThreshold=settings.angularSpeedThreshold;v.dwell=settings.inactivitySeconds;
                const glm::dvec3 linear(v.state.linear),angular(v.state.angular);
                v.eligible=b.GetInverseMass()>0&&b.HasFiniteState()&&glm::dot(linear,linear)<=v.linearThreshold*v.linearThreshold&&glm::dot(angular,angular)<=v.angularThreshold*v.angularThreshold;
                v.canSleep=b.CanSleep();v.friction=b.m_Friction;v.spin=b.GetSpinResistanceLength();v.rolling=b.GetRollingResistanceLength();
                for(auto& m:manifolds.m_Manifolds)for(int n=0;n<m.GetNumContacts();++n){const auto c=m.GetContact(n);
                    if(c.m_BodyA!=&b&&c.m_BodyB!=&b)continue;++v.contacts;
                    const auto a=c.m_BodyA->BodySpaceToWorldSpace(c.ptOnA_LocalSpace),z=c.m_BodyB->BodySpaceToWorldSpace(c.ptOnB_LocalSpace);
                    const auto av=c.m_BodyA->m_LinearVelocity+glm::cross(c.m_BodyA->m_AngularVelocity,a-c.m_BodyA->m_Position);
                    const auto bv=c.m_BodyB->m_LinearVelocity+glm::cross(c.m_BodyB->m_AngularVelocity,z-c.m_BodyB->m_Position);
                    const auto relative=av-bv;const auto tangent=relative-glm::dot(relative,c.normal)*c.normal;
                    v.slip=(std::max)(v.slip,double(glm::length(tangent)));v.depth=(std::max)(v.depth,double(-glm::dot(a-z,c.normal)));
                }
            }
        }
        void WakeEvent(const PhysicsDiagnostic::Event& e) noexcept
        {
            if(tick>=360){Fail("W64 diagnostic event capacity");return;}
            for(unsigned side=0;side<2;++side){if(side&&e.a==e.b)continue;
                const int ordinal=WakeOrdinal(side?e.b:e.a);if(ordinal<=0)continue;
                auto& v=wakeStats[tick*64+ordinal-1];const auto& before=side?e.beforeB:e.beforeA;const auto& after=side?e.afterB:e.afterA;
                const auto op=unsigned(e.operation);if(op<23)++v.operations[op];
                if(e.operation==PhysicsDiagnostic::Operation::SleepDecision&&e.reason>=0&&e.reason<=10)++v.reasons[e.reason];
                if(before.sleeping&&!after.sleeping)++v.sleepToAwake;
                if(!before.sleeping&&after.sleeping)++v.awakeToSleep;
                if(op<=unsigned(PhysicsDiagnostic::Operation::ToiPosition)){
                    v.solverDv+=glm::dvec3(after.linear)-glm::dvec3(before.linear);
                    v.solverDw+=glm::dvec3(after.angular)-glm::dvec3(before.angular);}
            }
        }
        void ExportWake()
        {
            std::ofstream raw(output/"w64-states.csv");raw<<std::setprecision(17);
            raw<<"tick,ordinal,stage,px,py,pz,vx,vy,vz,wx,wy,wz,inactive,sleeping,eligible,can_sleep,contacts,slip,depth,linear_threshold,angular_threshold,dwell,friction,spin,rolling\n";
            for(unsigned i=0;i<wakeCount;++i){const auto& v=wakeStates[i];const auto& s=v.state;
                raw<<v.tick<<','<<v.ordinal<<','<<v.stage<<','<<s.position.x<<','<<s.position.y<<','<<s.position.z<<','<<s.linear.x<<','<<s.linear.y<<','<<s.linear.z<<','<<s.angular.x<<','<<s.angular.y<<','<<s.angular.z<<','<<s.inactive<<','<<s.sleeping<<','<<v.eligible<<','<<v.canSleep<<','<<v.contacts<<','<<v.slip<<','<<v.depth<<','<<v.linearThreshold<<','<<v.angularThreshold<<','<<v.dwell<<','<<v.friction<<','<<v.spin<<','<<v.rolling<<'\n';}
            std::ofstream stats(output/"w64-events.csv");stats<<std::setprecision(17);stats<<"tick,ordinal,sleep_to_awake,awake_to_sleep,dvx,dvy,dvz,dwx,dwy,dwz";
            for(unsigned i=0;i<11;++i)stats<<",reason_"<<i;for(unsigned i=0;i<23;++i)stats<<",operation_"<<i;stats<<'\n';
            for(unsigned t=0;t<tick;++t)for(unsigned i=0;i<64;++i){const auto& s=wakeStats[t*64+i];stats<<t<<','<<i+1<<','<<s.sleepToAwake<<','<<s.awakeToSleep<<','<<s.solverDv.x<<','<<s.solverDv.y<<','<<s.solverDv.z<<','<<s.solverDw.x<<','<<s.solverDw.y<<','<<s.solverDw.z;
                for(auto n:s.reasons)stats<<','<<n;for(auto n:s.operations)stats<<','<<n;stats<<'\n';}
            std::ofstream meta(output/"w64-diagnostic.json");meta<<"{\"diagnostic_only\":true,\"states\":"<<wakeCount<<",\"completed_steps\":"<<tick<<",\"additional_observer_bytes\":"<<WakeCapacity*sizeof(WakeState)+360*64*sizeof(WakeStats)<<",\"event_observation_ns\":"<<wakeObserver.observationNs<<"}\n";
        }
#endif

        static constexpr const char* Names[]={"F64","A16","A64","A256","B64","B64-P2","S64","L120","W64",
            "R0-direct","R0-mesh","R1-direct","R1-mesh","R2-direct","R2-mesh",
            "R3-direct","R3-mesh","R4-direct","R4-mesh","R5-direct","R5-mesh"};
        void Fail(const char* text){if(!failed){failed=true;failure=text;}}
        void Check(bool value,const char* text){if(!value)Fail(text);}
        bool SameDescription(const CollisionShapeDesc& value) const
        {
            if(value.index()!=frozenDescription.index())return false;
            if(const auto* a=std::get_if<SphereShapeDesc>(&value))return a->radius==std::get<SphereShapeDesc>(frozenDescription).radius;
            if(const auto* a=std::get_if<BoxShapeDesc>(&value)){const auto& b=std::get<BoxShapeDesc>(frozenDescription);return a->center==b.center&&a->halfExtents==b.halfExtents;}
            if(const auto* a=std::get_if<CapsuleShapeDesc>(&value)){const auto& b=std::get<CapsuleShapeDesc>(frozenDescription);return a->radius==b.radius&&a->halfLength==b.halfLength;}
            return false;
        }
        void IdentifyProcessStaticOwner()
        {
#if defined(GENGINE_PHASE25_ALLOCATIONS) && _ITERATOR_DEBUG_LEVEL == 2
            // Proven by MD1 stacks, actual _Mycont pointers, linked static-symbol
            // address and six matching late frees. Do not eagerly initialize it.
            const auto& registry=scene->ReadRegistry();
            for(auto&& [id,pool]:registry.storage())
                if(id==entt::type_hash<SphereFixture3DComponent>::value())return;
            const auto& storage=registry.storage<SphereFixture3DComponent>();
            const auto begin=reinterpret_cast<std::uintptr_t>(&storage),end=begin+sizeof(storage);
            MEMORY_BASIC_INFORMATION memory{};
            Check(storage.empty() && VirtualQuery(&storage,&memory,sizeof memory)==sizeof memory &&
                memory.Type==MEM_IMAGE && memory.AllocationBase==GetModuleHandleW(nullptr),"proved EnTT process-static owner");
            if(failed)return;
            static_assert(sizeof(std::_Container_proxy)==16 && offsetof(std::_Container_proxy,_Mycont)==0 && offsetof(std::_Container_base12,_Myproxy)==0);
            unsigned found{};
            for(auto& e:Phase25Memory::entries){
                if(!e.pointer||e.pointer==Phase25Memory::Tombstone()||e.owner!=1||e.interval!=0||e.row!=row||e.requested!=sizeof(std::_Container_proxy))continue;
                std::uintptr_t container{};std::memcpy(&container,e.pointer,sizeof container);
                if(container<begin || container+sizeof(std::_Container_base12)>end)continue;
                void* reciprocal{};std::memcpy(&reciprocal,reinterpret_cast<const void*>(container),sizeof reciprocal);
                Check(reciprocal==e.pointer,"EnTT proxy reciprocal ownership");
                if(failed)return;
                if(Phase25Memory::processStaticCount==6){Fail("unexpected additional process-static proxy");return;}
                Phase25Memory::processStaticRecords[Phase25Memory::processStaticCount++]={reinterpret_cast<std::uintptr_t>(e.pointer),container,begin,e.requested,e.usable,e.row};
                // Counts/bytes in rowCounts remain the complete actual setup cost.
                // Only the independently proved lifetime owner changes category.
                Phase25Memory::fixtureLive-=e.usable;Phase25Memory::rowLive[e.row]-=e.usable;
                Phase25Memory::liveRequested-=e.requested;Phase25Memory::liveUsable-=e.usable;
                Phase25Memory::processStaticLive+=e.usable;Phase25Memory::processStaticAllocated+=e.usable;e.owner=3;++found;
            }
            Check((found==6 && Phase25Memory::processStaticCount==6)||(found==0 && Phase25Memory::processStaticCount==6),"exact proved EnTT proxy membership");
#endif
        }
        double Dt()const{return row==7?1./120:1./60;}
        void Sentinel()
        {
#ifdef GENGINE_PHASE25_ALLOCATIONS
            Phase25Memory::Begin();
            void* a=::operator new(37);void* b=::operator new[](79);
            void* c=::operator new(65,std::align_val_t(64));
            void* d=::operator new(41,std::nothrow);void* e=::operator new[](43,std::nothrow);
            void* f=::operator new(67,std::align_val_t(64),std::nothrow);
            void* g=::operator new[](69,std::align_val_t(64),std::nothrow);
            ::operator delete(a,std::size_t(37));::operator delete[](b,std::size_t(79));
            ::operator delete(c,std::size_t(65),std::align_val_t(64));
            ::operator delete(d,std::nothrow);::operator delete[](e,std::nothrow);
            ::operator delete(f,std::align_val_t(64),std::nothrow);
            const auto held=Phase25Memory::End();
            Check(held.allocations==7 && held.requested==401 && held.frees==6 && held.survivingRequested==69 && held.usable>=401,"allocation variant sentinel");
            Phase25Memory::Begin();::operator delete[](g,std::align_val_t(64),std::nothrow);
            const auto freed=Phase25Memory::End();
            Check(freed.allocations==0 && freed.frees==1 && freed.transientUsable==0,"allocation cross-boundary sentinel");
            Phase25Memory::Begin();{std::vector<std::uint64_t> v;v.reserve(4);v.push_back(11);}
            const auto vector=Phase25Memory::End();
#if _ITERATOR_DEBUG_LEVEL == 2
            constexpr unsigned vectorCount=2;
            constexpr auto vectorBytes=32+sizeof(std::_Container_proxy);
#else
            constexpr unsigned vectorCount=1;
            constexpr unsigned vectorBytes=32;
#endif
            Check(vector.allocations==vectorCount && vector.requested==vectorBytes && vector.frees==vectorCount && vector.transientRequested==vectorBytes,"allocation vector sentinel");
            Check(!Phase25Memory::errors,"allocation coverage sentinel errors");
#endif
        }
        bool Initialize(Window& w,SceneRenderResources& r)
        {
            window=&w;resources=&r;Phase25Memory::ownerThread=GetCurrentThreadId();
            char* request=nullptr;std::size_t length{};
            _dupenv_s(&request,&length,"GENGINE_PHASE25_REQUEST");
            if(!request){Fail("missing absolute runner request");return false;}
            const std::filesystem::path path(request);std::free(request);
            if(!path.is_absolute()){Fail("request is not absolute");return false;}
            std::ifstream in(path);std::string out,lat,kind,requestedMode;
            std::getline(in,out);std::getline(in,lat);std::getline(in,kind);std::getline(in,requestedMode);
            output=out;lattice=lat;debug=kind=="D1"||kind=="MD1"||kind=="MD2"||kind=="WD1";mode=requestedMode.empty()?'?':requestedMode[0];
            if(!in || !output.is_absolute() || !lattice.is_absolute()){Fail("invalid runner request");return false;}
            // Revision03 requalifies the post-Collider baseline from the first
            // row; the earlier W64-only continuation remains historical evidence.
            if(kind=="D1")row=0;
            if(kind=="C-R")row=9;
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
            Check(kind=="MD1"||kind=="MD2","diagnostic product cannot qualify D1 or Release");
            Phase25Memory::lifetimeFile=CreateFileW((output/"allocation-lifetime.bin").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            Check(Phase25Memory::lifetimeFile!=INVALID_HANDLE_VALUE,"allocation lifetime evidence file");
            Check(std::atexit(Phase25Memory::ExitMarker)==0,"allocation diagnostic exit marker");
            Phase25Memory::Trace probe;probe.depth=CaptureStackBackTrace(0,Phase25Memory::TraceDepth,probe.frames,nullptr);
            Check(probe.depth>0,"allocation stack capture admission");
#endif
#ifdef GENGINE_PHASE25_ALLOCATIONS
            Check(mode=='A',"allocation product mode mismatch");
#else
            Check(mode==(IsPhysicsProfilingEnabled()?'P':'M'),"profile product mode mismatch");
#endif
            records=std::make_unique<Record[]>(MaxSteps);
#ifdef GENGINE_PHASE25_W64_DIAGNOSTIC
            Check(kind=="WD1","W64 diagnostic product cannot qualify");row=8;
            wakeStates=std::make_unique<WakeState[]>(WakeCapacity);wakeStats=std::make_unique<WakeStats[]>(360*64);
            wakeObserver.context=this;wakeObserver.detailed=true;wakeObserver.targeted=true;
            wakeObserver.select=[](void* p,const RigidBody3D* a,const RigidBody3D* b)noexcept{auto& s=*static_cast<Session*>(p);return s.WakeOrdinal(a)>0||s.WakeOrdinal(b)>0;};
            wakeObserver.valid=[](void* p,const RigidBody3D* a,const RigidBody3D* b)noexcept{auto& s=*static_cast<Session*>(p);return s.WakeOrdinal(a)>=0&&s.WakeOrdinal(b)>=0;};
            wakeObserver.violation=[](void* p)noexcept{static_cast<Session*>(p)->Fail("W64 observer mutated body");};
            wakeObserver.event=[](void* p,const PhysicsDiagnostic::Event& e)noexcept{static_cast<Session*>(p)->WakeEvent(e);};
            wakeObserver.boundary=[](void* p,PhysicsDiagnostic::Stage stage,const PhysicsWorld&,ManifoldCollector& manifolds,std::span<const ContactIsland>,std::size_t,int)noexcept{static_cast<Session*>(p)->WakeBoundary(stage,manifolds);};
#endif

            auto last=Phase25Observation::Nanoseconds();const auto clockStart=last;std::uint64_t minimum=UINT64_MAX;
            for(unsigned i=0;i<4096;++i){const auto next=Phase25Observation::Nanoseconds();if(next>last)minimum=(std::min)(minimum,next-last);last=next;}
            clockLoopElapsed=last-clockStart;
            const auto nominal=(std::max)(std::uint64_t{1},std::uint64_t(1000000000ull*std::chrono::steady_clock::period::num/std::chrono::steady_clock::period::den));
            q=(std::max)(minimum,nominal);Check(q>0 && q<=1000,"clock resolution admission");
            auto* native=SDL_GetWindowFromID(w.GetWindowID());
            Check(native && w.IsCurrent(),"qualification owned context");
            if(native) {
                Check(SDL_GL_SetSwapInterval(0)==0,"qualification swap interval");
                w.RefreshDimensions();const auto pixel=w.GetFramebufferPixelSize();const auto logical=w.GetLogicalSize();
                if(pixel.Width!=1280 || pixel.Height!=720)SDL_SetWindowSize(native,1280*logical.Width/pixel.Width,720*logical.Height/pixel.Height);
                w.RefreshDimensions();const auto actual=w.GetFramebufferPixelSize();
                Check(actual.Width==1280 && actual.Height==720 && w.GetSwapInterval()==0,"qualification physical dimensions");
            }
            Check(RestDiagnosis::LoadLattice(lattice.string(),latticeInputs),"frozen lattice read");
            auto mat=r.CreateMaterial(MaterialAuthoringDesc{});Check(bool(mat),"fixture material publication");if(mat)material=*mat;
            observer={this,[](void* p) noexcept{
                auto& s=*static_cast<Session*>(p);ResetPhysicsProfile();
                CheckThread(s);Phase25Memory::Begin();
            },[](void* p,std::uint64_t ns) noexcept{
                auto& s=*static_cast<Session*>(p);const auto memory=Phase25Memory::End();
                ++s.stepCallbacks;if(!s.current){s.Fail("unexpected outer step callback");return;}
                s.current->totalNs=ns;s.current->stepMemory=memory;
                s.current->profile=GetPhysicsProfileSnapshot();s.current->sleep=s.scene->GetPhysicsSystem()->GetSleepTimings();
            }};
            Sentinel();return !failed;
        }
        static void CheckThread(Session& s)noexcept
        {if(GetCurrentThreadId()!=Phase25Memory::ownerThread)s.Fail("wrong-thread Physics step");}
        _Entity Make(unsigned ordinal,const RestDiagnosis::BodyInput& b,Asset::MeshHandle mesh,Vec3f scale=Vec3f(1))
        {
            auto made=scene->CreateEntityWithUUID(UUID{0x25000000ull+4096*row+ordinal},Names[row]);
            if(!made){Fail("fixture entity creation");return {};}
            auto e=*made;auto& t=e.GetComponent<Transform3DComponent>();t.Translation=b.position;t.QuatRotation=b.orientation;t.Scale=scale;
            RigidBody3DComponent rb;rb.Type=static_cast<BodyType>(b.type);rb.CollisionLayer=b.layer;rb.CollisionMask=b.mask;
            e.AddOrReplaceComponent<RigidBody3DComponent>(rb);
            Fixture3DProperty property;property.m_Position=b.position;property.m_Orientation=b.orientation;
            property.m_LinearVelocity=b.linear;property.m_AngularVelocity=b.angular;
            property.m_InvMass=b.inverseMass;property.m_Elasticity=b.elasticity;property.m_Friction=b.friction;
            if(b.shape==0){SphereFixture3DComponent f;f.Radius=b.radius;f.Property=property;e.AddOrReplaceComponent<SphereFixture3DComponent>(f);}
            else if(b.shape==1){BoxFixture3DComponent f;f.Property=property;
                Vec3f lo=b.points.front(),hi=lo;for(auto p:b.points){lo=glm::min(lo,p);hi=glm::max(hi,p);}
                f.Shape.halfExtents=(hi-lo)*.5f;f.Shape.center=(hi+lo)*.5f;e.AddOrReplaceComponent<BoxFixture3DComponent>(f);}
            else {ConvexFixture3DComponent f;f.Property=property;f.Shape.points=b.points;e.AddOrReplaceComponent<ConvexFixture3DComponent>(f);}
            Check(bool(resources->AssignRenderable(e,{mesh,material})),"fixture render publication");entities[ordinal]=e;return e;
        }
        bool Prepare()
        {
            if(failed || row>=21){complete=row>=21;return false;}
            Phase25Memory::row=row;tick=0;rowDone=false;priorValid.fill(false);
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
            Phase25Memory::currentTick=UINT_MAX;
#endif
            warm=row==7?60:120;total=row<7?(debug?240:1320):row==7?(debug?180:1800):row==8?(debug?360:2520):(debug?126:450);
            dynamicCount=row==1?16:row==3?256:row==7?180:row>=9?1:64;
            bodyCount=dynamicCount+(row==0?0:row==7?5:1);
            const auto sphere=resources->PublishGeometry(GeometryTemplates::Sphere{1,32,16});
            const auto box=resources->PublishGeometry(GeometryTemplates::Cube{2,2,2});
            const auto floorMesh=resources->PublishGeometry(GeometryTemplates::Cube{160,2,160});
            Check(bool(sphere)&&bool(box)&&bool(floorMesh),"bank geometry publication");if(failed)return false;
            if(row>=9){const unsigned cell=(row-9)/2;const bool large=cell>=3;
                auto mesh=resources->PublishGeometry(GeometryTemplates::Sphere{1,large?128u:32u,large?64u:16u});
                Check(bool(mesh),"replacement geometry publication");if(failed)return false;targetMesh=*mesh;
                auto source=resources->GeometrySource(targetMesh);Check(bool(source)&&source->source,"replacement CPU geometry");if(failed)return false;
                fitSource=source->source;replacementScale=large?Vec3f(1,1.5f,.75f):Vec3f(1);
                replacementType=cell%3==0?ShapeType::Sphere:cell%3==1?ShapeType::Box:ShapeType::Capsule;
                auto fit=Rbs::FitMeshCollider(*fitSource,replacementScale,replacementType);Check(bool(fit),"replacement frozen descriptor");if(failed)return false;frozenDescription=*fit;
            }
            {Phase25Memory::Scope fixture(2);scene=CreateRefPtr<_Scene>();}
            Check(scene->SetPhysicsStep(Seconds{Dt()}),"fixture scheduler rate");
            std::uint64_t hash=1469598103934665603ull;
            // EnTT visits reverse component insertion order: author in reverse to
            // produce ascending fixed semantic ordinals in the Physics world.
            for(unsigned n=bodyCount;n>0;--n){const auto i=n-1;
                RestDiagnosis::BodyInput b;
                if(row==7)b=latticeInputs[i];
                else if(row!=0 && i==0){b=RestDiagnosis::Box({60,-1,60},{160,2,160},false);b.elasticity=0;}
                else {const auto j=row==0?i:i-1;b.type=1;b.inverseMass=1;b.elasticity=0;
                    b.position={float(8*(j%16)),row==0?4.f:1.01f,float(8*(j/16))};
                    if(row==0)b.linear={.25f,0,0};
                    if(row==4 || row==5){b=RestDiagnosis::Box(b.position,{2,2,2},true);b.elasticity=0;}
                }
                if(row>=9 && i==1){auto shape=CreateCollisionShape(frozenDescription);Check(bool(shape),"replacement descriptor construction");
                    if(failed)return false;b.position.y=.01f-(*shape)->Support({0,-1,0},{},{1,0,0,0},0).y;}
                Hash(hash,i);Hash(hash,b.shape);Hash(hash,b.type);Hash(hash,b.inverseMass);Hash(hash,b.elasticity);Hash(hash,b.friction);
                Hash(hash,b.radius);Hash(hash,b.layer);Hash(hash,b.mask);Hash(hash,b.position);Hash(hash,b.linear);Hash(hash,b.angular);Hash(hash,b.orientation);
                for(auto point:b.points)Hash(hash,point);
                const auto mesh=row>=9 && i==1?targetMesh:row!=0 && row!=7 && i==0?*floorMesh:b.shape==0?*sphere:*box;
                Phase25Memory::Scope fixture(2);Make(i,b,mesh,row>=9 && i==1?replacementScale:Vec3f(1));
            }
            if(failed)return false;
            {Phase25Memory::Scope physics(1);Check(bool(scene->OnRuntimeStart()),"fixture Physics creation");}
            if(failed)return false;
#ifndef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
            IdentifyProcessStaticOwner();
            if(failed)return false;
#endif
            auto* system=scene->GetPhysicsSystem();auto* world=system->GetPhysicsWorld();
            world->SetGravity(row==0?Vec3f(0):Vec3f(0,-12,0));system->SetSleepingEnabled(!(row>=1&&row<=5));
            Check(system->SetSolverIterations(row==5?2:1),"fixture pass selection");
            const auto& bodies=PhysicsTestAccess::Bodies(world);Check(bodies.size()==bodyCount,"fixture body count");
            std::uint64_t order=1469598103934665603ull;
            for(unsigned i=0;i<bodyCount&&!failed;++i){auto binding=scene->GetBodyBinding(entities[i]);
                Check(bool(binding),"fixture body identity");if(!binding)break;handles[i]=*binding;
                Check(PhysicsTestAccess::Resolve(*world,*binding)==bodies[i],"fixture body insertion order");
                const auto id=std::uint64_t(entities[i].GetUUID());Hash(order,id);
                if(row==7){bodies[i]->m_Position=latticeInputs[i].position;bodies[i]->m_Orientation=latticeInputs[i].orientation;
                    if(latticeInputs[i].shape!=0)bodies[i]->m_Shape->Build(latticeInputs[i].points);}
                const float resistance=row==7?.05f:0.f;
                const float rolling=row==8?.05f:resistance;
                Check(bool(world->SetBodyMaterial(*binding,{.5f,row==7?.5f:0.f,resistance,rolling})),"fixture material");
                Check(bodies[i]->GetSpinResistanceLength()==resistance && bodies[i]->GetRollingResistanceLength()==rolling,"actual fixture resistance");
                Check(bodies[i]->GetSleepSettings()==RigidBody3D::SleepSettings{},"fixture sleep defaults");
                Hash(hash,bodies[i]->GetSleepSettings().linearSpeedThreshold);Hash(hash,bodies[i]->GetSleepSettings().angularSpeedThreshold);
                Hash(hash,bodies[i]->GetSleepSettings().inactivitySeconds);Hash(hash,resistance);
                if(row==8)Hash(hash,rolling);
            }
            if(row>=9&&!failed){Phase25Memory::Scope physics(1);Check(bool(scene->ReplaceBodyCollider(entities[1],frozenDescription)),"initial exact replacement shape");replacementHandle=handles[1];
                Hash(hash,replacementScale);Hash(hash,fitSource->Vertices().data(),fitSource->Vertices().size());
                Hash(hash,fitSource->Indices().data(),fitSource->Indices().size_bytes());}
            Hash(hash,Dt());const auto passes=system->GetSolverIterations();Hash(hash,passes);const auto sleep=system->IsSleepingEnabled();Hash(hash,sleep);
            initialHashes[row]=hash;orderHashes[row]=order;scene->SetPaused(false);
#ifdef GENGINE_PHASE25_W64_DIAGNOSTIC
            for(unsigned i=0;i<65;++i)wakeBodies[i]=bodies[i];
#endif

            return !failed;
        }
        unsigned Sleeping()const
        {unsigned result=0;for(auto* b:PhysicsTestAccess::Bodies(scene->GetPhysicsSystem()->GetPhysicsWorld()))if(b->m_InvMass>0&&b->IsSleeping())++result;return result;}
        void Tick()
        {
            if(failed||!scene||count>=MaxSteps){Fail("recorder capacity or fixture lifecycle");return;}
            auto& record=records[count++];current=&record;record.row=row;record.tick=tick;record.measured=tick>=warm;
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
            Phase25Memory::currentTick=tick;
#endif
            if(row>=9)record.measured=tick>=(debug?120:150);
            auto* system=scene->GetPhysicsSystem();auto* world=system->GetPhysicsWorld();
            const auto pixel=window->GetFramebufferPixelSize();
            Check(pixel.Width==1280&&pixel.Height==720&&window->GetSwapInterval()==0,"window/pacing drift");
            if(row==6&&tick>=warm)Check(Sleeping()==64,"S64 not naturally asleep at measured boundary");
            const bool wake=row==8&&tick>=120&&(tick-120)%120==0;
            const PhysicalShape* previousShape=nullptr;
            if(wake){Check(Sleeping()==64,"W64 not naturally asleep before wake command");
                if(!failed){record.commandKind=1;Phase25Memory::Begin();const auto start=Phase25Observation::Nanoseconds();
                    bool success=true;for(unsigned i=1;i<=64;++i)success=bool(world->SetBodyVelocity(handles[i],{.1f,0,0},{}))&&success;
                    record.commandNs=Phase25Observation::Nanoseconds()-start;record.commandMemory=Phase25Memory::End();Check(success,"wake typed command");}}
            if(row>=9&&tick>=120&&!failed){
                previousShape=PhysicsTestAccess::Resolve(*world,handles[1])->m_Shape;
                if(record.measured){Phase25Memory::Begin();const auto start=Phase25Observation::Nanoseconds();
                    auto fit=Rbs::FitMeshCollider(*fitSource,replacementScale,replacementType);
                    record.fitNs=Phase25Observation::Nanoseconds()-start;record.fitMemory=Phase25Memory::End();Check(bool(fit),"pure fit rejected");
                    if(fit)Check(SameDescription(*fit),"pure fit differs from frozen descriptor");}
                record.commandKind=(row-9)%2?3:2;Phase25Memory::Begin();const auto start=Phase25Observation::Nanoseconds();
                bool success=record.commandKind==3?bool(Rbs::ReplaceMeshCollider(*scene,*resources,entities[1],replacementType)):bool(scene->ReplaceBodyCollider(entities[1],frozenDescription));
                record.commandNs=Phase25Observation::Nanoseconds()-start;record.commandMemory=Phase25Memory::End();Check(success,"replacement typed command");
                Check(scene->GetBodyBinding(entities[1])==replacementHandle,"replacement identity changed");
#ifdef GENGINE_PHASE25_ALLOCATIONS
                Check(record.commandMemory.allocations>0,"replacement shape allocation not observed");
                Check(record.fitMemory.allocations==0,"pure fitting allocated");
#endif
            }
            if(failed){current=nullptr;return;}
            const auto before=scene->GetPhysicsTiming().totalSteps;stepCallbacks=0;Phase25Observation::observer=&observer;
#ifdef GENGINE_PHASE25_W64_DIAGNOSTIC
            PhysicsDiagnostic::observer=&wakeObserver;
#endif
            {Phase25Memory::Scope fixture(2);scene->Update(Timestep(Dt()));}
#ifdef GENGINE_PHASE25_W64_DIAGNOSTIC
            PhysicsDiagnostic::observer=nullptr;
#endif

            Phase25Observation::observer=nullptr;
            const auto& timing=scene->GetPhysicsTiming();Check(stepCallbacks==1&&timing.totalSteps==before+1&&timing.stepsLastUpdate==1&&timing.totalDiscardedSeconds==0&&std::abs(timing.pendingSeconds)<1e-12,"one Scene scheduler step");
            const auto stateCheckStart=Phase25Observation::Nanoseconds();
            const auto& bodies=PhysicsTestAccess::Bodies(world);record.bodies=unsigned(bodies.size());record.sleeping=Sleeping();record.islands=unsigned(system->GetContactIslands().size());
            std::uint64_t hash=1469598103934665603ull;
            for(unsigned i=0;i<bodies.size();++i){const auto& b=*bodies[i];const auto state=PhysicsDiagnostic::Capture(b);Hash(hash,i);HashState(hash,state);
                Check(Math::IsFinite(state.position)&&Math::IsFinite(state.linear)&&Math::IsFinite(state.angular)&&Math::IsFinite(state.orientation),"nonfinite Physics state");
                if(row!=7&&row!=0&&b.m_InvMass>0){const auto low=b.m_Shape->Support({0,-1,0},state.position,state.orientation,0).y;
                    record.penetration=(std::max)(record.penetration,double(-low));
                    if(tick>=warm)Check(low>=-.035f,"ground support penetration exceeds .035");
                    Check(state.position.y>=-1,"body crossed floor");}
                if(row==6&&tick>=warm&&b.m_InvMass>0){Check(state.sleeping,"S64 sleeping state changed");
                    if(priorValid[i]&&b.m_InvMass>0)Check(state.position==prior[i].position&&state.orientation==prior[i].orientation,"S64 sleeping pose changed");}
                prior[i]=state;priorValid[i]=true;
            }
            record.physicalState=hash;
            if(row>=9){
                const auto& body=*bodies[1];const auto& shape=*body.m_Shape;
                Check(!previousShape||previousShape!=body.m_Shape,"replacement did not install a new owner");
                Check(shape.GetShapeType()==replacementType&&body.GetInverseMass()==1.f,"replacement shape or mass changed");
                Vec3f lo{},hi{};
                if(const auto* d=std::get_if<SphereShapeDesc>(&frozenDescription)){hi=Vec3f(d->radius);lo=-hi;}
                else if(const auto* d=std::get_if<BoxShapeDesc>(&frozenDescription)){lo=d->center-d->halfExtents;hi=d->center+d->halfExtents;}
                else if(const auto* d=std::get_if<CapsuleShapeDesc>(&frozenDescription)){hi={d->radius,d->halfLength+d->radius,d->radius};lo=-hi;}
                const auto bounds=shape.GetBounds();const auto inertia=shape.InertiaTensor();
                Check(bounds.mins==lo&&bounds.maxs==hi,"replacement geometry differs from frozen descriptor");
                for(unsigned axis=0;axis<3;++axis)Check(Math::IsFinite(inertia[axis]),"replacement inertia nonfinite");
                Check(std::isfinite(glm::determinant(inertia))&&glm::determinant(inertia)>0,"replacement inertia unusable");
                const auto& render=entities[1].GetComponent<MeshRendererComponent>();
                Check(render.mesh==targetMesh&&render.material==material&&entities[1].GetComponent<Transform3DComponent>().Scale==replacementScale,"replacement changed mesh material or scale");
                Hash(record.physicalState,bounds.mins);Hash(record.physicalState,bounds.maxs);
                Hash(record.physicalState,inertia);Hash(record.physicalState,body.m_InvMass);
            }
            for(const auto& island:system->GetContactIslands()){
                for(const auto& id:island.dynamicBodies){Hash(hash,id.GetSlot());Hash(hash,id.GetGeneration());}
                for(const auto& id:island.boundaryBodies){Hash(hash,id.GetSlot());Hash(hash,id.GetGeneration());}
                for(const auto& pair:island.contactPairs){Hash(hash,pair.bodyA.GetSlot());Hash(hash,pair.bodyA.GetGeneration());Hash(hash,pair.bodyB.GetSlot());Hash(hash,pair.bodyB.GetGeneration());}}
            record.state=hash;
            if(row==8 && tick+1==total)Check(record.sleeping==64,"W64 final recovery boundary not naturally asleep");
            if(row==0&&IsPhysicsProfilingEnabled())Check(!record.profile.generatedContactCount&&!record.profile.manifoldContactCount,"F64 contacts");
            if(row>=1&&row<=5)Check(record.sleeping==0,"active bank slept");
            Check(!Phase25Memory::errors,"allocation ledger coverage loss");
            Check(Phase25Memory::liveUsable<=64ull*1024*1024,"live Physics memory exceeds 64 MiB");
            record.stateCheckNs=Phase25Observation::Nanoseconds()-stateCheckStart;
            current=nullptr;++tick;rowDone=tick==total;
        }
        void RetireRow()
        {
            Phase25Observation::observer=nullptr;current=nullptr;
            if(scene){scene->OnRuntimeStop();entities={};handles={};scene.reset();
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
                if(Phase25Memory::rowLive[row]){
                    std::ofstream live(output/("survivors-row-"+std::to_string(row)+".csv"));
                    live<<"pointer,requested,usable,interval,row,owner,birth_sequence,birth_ns,tick,trace,first_bytes_hex\n";
                    for(const auto& entry:Phase25Memory::entries){
                        if(!entry.pointer||entry.pointer==Phase25Memory::Tombstone()||entry.row!=row||!entry.owner)continue;
                        const auto e=entry;
                        live<<reinterpret_cast<std::uintptr_t>(e.pointer)<<','<<e.requested<<','<<e.usable<<','<<e.interval<<','<<e.row<<','<<e.owner<<','<<e.birthSequence<<','<<e.birthNs<<','<<e.tick<<','<<e.trace<<',';
                        constexpr char digits[]="0123456789abcdef";
                        for(std::size_t i=0;i<(std::min)(e.requested,std::size_t{128});++i){const auto value=static_cast<const unsigned char*>(e.pointer)[i];live<<digits[value>>4]<<digits[value&15];}live<<'\n';
                        if(Phase25Memory::watchCount==Phase25Memory::WatchCapacity){Fail("allocation diagnostic watch capacity");break;}
                        Phase25Memory::watches[Phase25Memory::watchCount++]={e.pointer,e.birthSequence,false};Phase25Memory::Lifetime(1,e);
                    }
                }
#endif
                Check(Phase25Memory::rowLive[row]==0,"fixture allocations survive Scene teardown");}
            if(!failed){++completeRows;++row;complete=row==21;
#ifdef GENGINE_PHASE25_W64_DIAGNOSTIC
                complete=true;
#endif
            }
        }
        void Export()
        {
            Sentinel();
#ifdef GENGINE_PHASE25_W64_DIAGNOSTIC
            ExportWake();
#endif
#ifdef GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC
            std::ofstream stacks(output/"allocation-stacks.csv");stacks<<"trace,depth,address\n";
            for(unsigned i=0;i<Phase25Memory::TraceCapacity;++i)for(unsigned j=0;j<Phase25Memory::traces[i].depth;++j)
                stacks<<i+1<<','<<j<<','<<reinterpret_cast<std::uintptr_t>(Phase25Memory::traces[i].frames[j])<<'\n';
            std::ofstream module(output/"diagnostic-module.json");
            module<<"{\"exe_base\":"<<reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))<<",\"trace_capacity\":"<<Phase25Memory::TraceCapacity<<",\"trace_depth\":"<<Phase25Memory::TraceDepth<<",\"watched\":"<<Phase25Memory::watchCount<<",\"additional_observer_bytes\":"<<sizeof(Phase25Memory::traces)+sizeof(Phase25Memory::watches)<<"}\n";
#endif
            std::ofstream raw(output/"steps.csv",std::ios::out|std::ios::trunc);raw<<std::setprecision(17);
            raw<<"row,tick,measured,sleeping,bodies,islands,command_kind,state,total_ns,fit_ns,command_ns,penetration,physical_state,state_check_ns";
            for(unsigned i=0;i<sizeof(PhysicsProfileSnapshot)/8;++i)raw<<",profile_"<<i;
            for(unsigned i=0;i<sizeof(PhysicsSystem::SleepTimings)/8;++i)raw<<",sleep_"<<i;
            for(const char* label:{"step","fit","command"})for(unsigned i=0;i<sizeof(Counts)/8;++i)raw<<','<<label<<"_memory_"<<i;
            raw<<'\n';
            for(unsigned j=0;j<count;++j){const auto& r=records[j];raw<<r.row<<','<<r.tick<<','<<r.measured<<','<<r.sleeping<<','<<r.bodies<<','<<r.islands<<','<<r.commandKind<<','<<r.state<<','<<r.totalNs<<','<<r.fitNs<<','<<r.commandNs<<','<<r.penetration<<','<<r.physicalState<<','<<r.stateCheckNs;
                auto values=[&](const auto& v){std::array<std::uint64_t,sizeof(v)/8> a{};std::memcpy(a.data(),&v,sizeof(v));for(auto x:a)raw<<','<<x;};
                values(r.profile);values(r.sleep);values(r.stepMemory);values(r.fitMemory);values(r.commandMemory);raw<<'\n';}
            raw.close();
            std::ofstream identity(output/"rows.csv");identity<<"row,name,input_hash,order_hash,live_after_teardown\n";
            for(unsigned i=0;i<21;++i)identity<<i<<','<<Names[i]<<','<<initialHashes[i]<<','<<orderHashes[i]<<','<<Phase25Memory::rowLive[i]<<'\n';
            std::ofstream ownership(output/"process-static-owners.csv");ownership<<"pointer,container,static_storage,requested,usable,birth_row\n";
            for(unsigned i=0;i<Phase25Memory::processStaticCount;++i){const auto& e=Phase25Memory::processStaticRecords[i];ownership<<e.pointer<<','<<e.container<<','<<e.storage<<','<<e.requested<<','<<e.usable<<','<<e.row<<'\n';}
            std::ofstream setup(output/"row-allocation-totals.csv");setup<<"row,allocations,frees,requested,usable,peak_requested,peak_usable,process_static_allocated,process_static_live\n";
            for(unsigned i=0;i<21;++i){const auto& c=Phase25Memory::rowCounts[i];std::uint64_t staticBytes{};
                for(unsigned j=0;j<Phase25Memory::processStaticCount;++j)if(Phase25Memory::processStaticRecords[j].row==i)staticBytes+=Phase25Memory::processStaticRecords[j].usable;
                setup<<i<<','<<c.allocations<<','<<c.frees<<','<<c.requested<<','<<c.usable<<','<<c.peakRequested<<','<<c.peakUsable<<','<<staticBytes<<','<<staticBytes<<'\n';}
            const auto logical=window->GetLogicalSize();const auto pixels=window->GetFramebufferPixelSize();
            std::ofstream common(output/"common-observer.json");
            common<<"{\"clock_loop_reads\":4096,\"clock_loop_elapsed_ns\":"<<clockLoopElapsed<<",\"physical_width\":"<<pixels.Width<<",\"physical_height\":"<<pixels.Height<<",\"logical_width\":"<<logical.Width<<",\"logical_height\":"<<logical.Height<<",\"swap_interval\":"<<window->GetSwapInterval()<<",\"manual_frame_limit\":"<<actualFrameLimit<<"}\n";
            std::ofstream result(output/"fixture.json");result<<"{\"schema\":1,\"status\":\""<<(failed?"FAIL":complete?"COMPLETE":"INCOMPLETE")<<"\",\"failure\":\""<<failure<<"\",\"mode\":\""<<mode<<"\",\"q_ns\":"<<q<<",\"rows_completed\":"<<completeRows<<",\"records\":"<<count<<",\"allocation_errors\":"<<Phase25Memory::errors.load()<<",\"physics_live_usable\":"<<Phase25Memory::liveUsable<<",\"fixture_live_usable\":"<<Phase25Memory::fixtureLive<<",\"observer_bytes\":"<<MaxSteps*sizeof(Record)+sizeof(Phase25Memory::entries)<<"}\n";
        }
    };
}
