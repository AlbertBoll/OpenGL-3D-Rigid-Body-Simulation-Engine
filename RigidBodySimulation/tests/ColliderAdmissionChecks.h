#pragma once
#ifdef GENGINE_COLLIDER_ADMISSION_VALIDATION
#ifndef GENGINE_COLLIDER_ADMISSION_ISOLATED_BUILD
#error Collider admission diagnostics require an isolated build.
#endif
#ifndef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
#error Collider admission qualification requires the preserved canonical replay observer.
#endif
#ifdef GENGINE_PHASE24_REPLACEMENT_MEASUREMENT
#error Only one allocation instrument may define global operators.
#endif
#include "ColliderReplacementReplay.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <malloc.h>
#include <new>
#include <intrin.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#include <bit>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <limits>
#include <sstream>

// Command-only allocation accounting. The fixed thread-local registry allocates
// nothing; it is inactive throughout prefix reconstruction and ordinary startup.
namespace Rbs::AdmissionAllocation
{
    struct Entry { void* pointer{}; std::size_t bytes{}; };
    struct Stats { std::size_t attempts{}, count{}, bytes{}, live{}, peak{}, liveCount{}; bool overflow{}; };
    struct Attempt { std::size_t ordinal{},bytes{},alignment{}; std::uintptr_t caller{},pointer{}; const char* kind{}; const char* result{}; };
    inline thread_local std::array<Attempt,512> attemptEvents{};
    inline thread_local std::array<Entry, 4096> entries{};
    inline thread_local Stats stats{};
    inline thread_local bool enabled{};
    inline thread_local std::size_t failAt{};
    inline void Begin(std::size_t fail = 0)
    {
        if (stats.liveCount) throw std::runtime_error("allocation measurement began with retained allocations");
        stats = {}; failAt = fail; enabled = true;
    }
    inline Stats End() { enabled = false; failAt = 0; return stats; }
    inline void Record(void* p, std::size_t n)
    {
        if (!enabled) return;
        ++stats.count; stats.bytes += n; stats.live += n; ++stats.liveCount;
        stats.peak = (std::max)(stats.peak, stats.live);
        for (auto& e : entries) if (!e.pointer) { e = {p,n}; return; }
        stats.overflow = true;
    }
    inline void Retire(void* p) noexcept
    {
        if (!p || !stats.liveCount) return;
        for (auto& e : entries) if (e.pointer == p)
        { stats.live -= e.bytes; --stats.liveCount; e = {}; return; }
    }
    inline void* Allocate(std::size_t n, std::size_t alignment, void* caller, const char* kind)
    {
        Attempt* event=nullptr;
        if(enabled)
        {
            ++stats.attempts;
            if(stats.attempts<=attemptEvents.size())
            {
                event=&attemptEvents[stats.attempts-1];
                *event={stats.attempts,n,alignment,reinterpret_cast<std::uintptr_t>(caller),0,kind,"attempt"};
            }
            else stats.overflow=true;
            if(stats.attempts==failAt){if(event)event->result="injected_bad_alloc";throw std::bad_alloc();}
        }
        const auto actual=(std::max)(n,std::size_t(1));
        void* p=alignment?_aligned_malloc(actual,alignment):std::malloc(actual);
        if(!p){if(event)event->result="native_allocation_failure";throw std::bad_alloc();}
        if(event){event->pointer=reinterpret_cast<std::uintptr_t>(p);event->result="allocated";}
        Record(p,n);return p;
    }
}
__declspec(noinline) void* operator new(std::size_t n) { return Rbs::AdmissionAllocation::Allocate(n,0,_ReturnAddress(),"new"); }
__declspec(noinline) void* operator new[](std::size_t n) { return Rbs::AdmissionAllocation::Allocate(n,0,_ReturnAddress(),"new[]"); }
void operator delete(void* p) noexcept { Rbs::AdmissionAllocation::Retire(p); std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
__declspec(noinline) void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return Rbs::AdmissionAllocation::Allocate(n,0,_ReturnAddress(),"new_nothrow");}catch(...){return nullptr;}}
__declspec(noinline) void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return Rbs::AdmissionAllocation::Allocate(n,0,_ReturnAddress(),"new_array_nothrow");}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {::operator delete(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {::operator delete(p);}
__declspec(noinline) void* operator new(std::size_t n,std::align_val_t a) {return Rbs::AdmissionAllocation::Allocate(n,std::size_t(a),_ReturnAddress(),"new_aligned");}
__declspec(noinline) void* operator new[](std::size_t n,std::align_val_t a) {return Rbs::AdmissionAllocation::Allocate(n,std::size_t(a),_ReturnAddress(),"new_array_aligned");}
void operator delete(void* p,std::align_val_t) noexcept {Rbs::AdmissionAllocation::Retire(p);_aligned_free(p);}
void operator delete[](void* p,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete(void* p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete[](void* p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
__declspec(noinline) void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return Rbs::AdmissionAllocation::Allocate(n,std::size_t(a),_ReturnAddress(),"new_aligned_nothrow");}catch(...){return nullptr;}}
__declspec(noinline) void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return Rbs::AdmissionAllocation::Allocate(n,std::size_t(a),_ReturnAddress(),"new_array_aligned_nothrow");}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t a,const std::nothrow_t&) noexcept {::operator delete(p,a);}
void operator delete[](void* p,std::align_val_t a,const std::nothrow_t&) noexcept {::operator delete(p,a);}

namespace GEngine
{
    struct ColliderAdmissionCheckAccess
    {
        using Bytes = std::vector<std::uint8_t>;
        template<class T> static void Raw(Bytes& b, const T& v)
        { const auto* p = reinterpret_cast<const std::uint8_t*>(&v); b.insert(b.end(),p,p+sizeof(v)); }
        template<class T> static void Vector(Bytes& b, const std::vector<T>& v)
        { Raw(b,v.size()); for (const auto& x:v) Raw(b,x); }
        static Bytes Image(const PhysicsSystem& s)
        {
            Bytes b; Raw(b,s); // Includes broadphase object/vector headers and every system flag.
            const auto& bodies = PhysicsTestAccess::Bodies(s.m_PhysicsWorld);
            Vector(b,bodies);
            for (const auto* body : bodies) Raw(b,*body);
            const auto bodyIndex = [&](const RigidBody3D* p) { auto i=std::find(bodies.begin(),bodies.end(),p); return unsigned(i-bodies.begin()); };
            const auto shapeIndex = [&](const PhysicalShape* p) { for(unsigned i=0;i<bodies.size();++i)if(bodies[i]->m_Shape==p)return i;return UINT32_MAX; };
            const auto identityIndex = [&](RigidBodyIdentity id) { for(unsigned i=0;i<bodies.size();++i)if(bodies[i]->GetIdentity()==id)return i;return UINT32_MAX; };
            for(unsigned i=0;i<bodies.size();++i)
            {
                const auto wire=PhysicsDiagnostic::ColliderObservationAccess::Body(*bodies[i],i,shapeIndex);
                if(!wire.valid)throw std::runtime_error("body snapshot wire overflow");
                b.insert(b.end(),wire.bytes.begin(),wire.bytes.begin()+wire.used);
            }
            Vector(b,s.m_Contacts); Vector(b,s.m_CollisionPairs);
            Raw(b,s.m_Manifolds.m_Manifolds.size());
            for(const auto& m:s.m_Manifolds.m_Manifolds)
            {
                Raw(b,m);
                const auto wire=PhysicsDiagnostic::ColliderObservationAccess::ManifoldImage(m,bodyIndex,shapeIndex,identityIndex);
                if(!wire.valid)throw std::runtime_error("manifold snapshot wire overflow");
                b.insert(b.end(),wire.bytes.begin(),wire.bytes.begin()+wire.used);
            }
            for(const auto* islands:{&s.m_ContactIslands,&s.m_ActivationIslands})
            { Raw(b,islands->size());for(const auto& i:*islands){Vector(b,i.dynamicBodies);Vector(b,i.boundaryBodies);Vector(b,i.contactPairs);} }
            Vector(b,s.m_ContactIslandScratch.nodes);Vector(b,s.m_ContactIslandScratch.edges);Vector(b,s.m_ContactIslandScratch.counts);
            return b;
        }
        static void Fault(PhysicsSystem& s,unsigned kind,bool on)
        {
            if(kind==0)s.m_AdmissionTestExpired=on;
            if(kind==1)s.m_AdmissionTestBodyLimit=on?0:4096;
            if(kind==2)s.m_AdmissionTestQueryLimit=on?0:8190;
            if(kind==3)s.m_AdmissionTestStorageLimit=on?0:4194304;
            if(kind>=4)s.m_AdmissionTestQueryFailureReason=on?int(kind==4?PhysicsDiagnostic::QueryExit::ContactSearchFailed:kind==5?PhysicsDiagnostic::QueryExit::ClosestDistanceFailed:PhysicsDiagnostic::QueryExit::IterationLimit):0;
        }
    };
}

namespace Rbs
{
    ::GEngine::ColliderAdmissionReport RunMeshColliderAdmission(::GEngine::_Scene&,::GEngine::SceneRenderResources&,
        const ::GEngine::_Entity&,::GEngine::ShapeType,bool);
}

namespace Rbs::ColliderAdmissionChecks
{
    using namespace ::GEngine;
    namespace PD = PhysicsDiagnostic;
    using Outcome = ColliderAdmissionOutcome;
    inline bool checked{},enabled{},done{},failed{};
    inline std::filesystem::path output;
    inline std::ofstream samples,pairs,checks,allocations;
    inline unsigned passed{};
    inline const char* failure="";
    inline std::size_t reportAllocationCount{};
    inline void Require(bool value,const char* name)
    {
        checks<<std::quoted(name)<<','<<(value?"PASS":"FAIL")<<'\n';checks.flush();
        if(!value){failed=true;failure=name;throw std::runtime_error(name);}++passed;
    }
    inline bool Enabled()
    {
        if(checked)return enabled;checked=true;char* raw=nullptr;std::size_t length{};
        _dupenv_s(&raw,&length,"GENGINE_COLLIDER_ADMISSION_OUTPUT");if(!raw)return false;
        output=std::filesystem::path(raw).lexically_normal();std::free(raw);enabled=true;
#ifdef _DEBUG
        // Opt-in diagnostics must retain fatal/assert failures without a hidden
        // CRT modal dialog blocking the owned process runner.
        _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
        if(!output.is_absolute()||output.generic_string().rfind(GENGINE_COLLIDER_VALIDATION_ROOT,0)!=0||!std::filesystem::is_directory(output))
            throw std::runtime_error("unconfined admission fixture output");
        for(auto at=output;!at.empty();at=at.parent_path())
        {auto a=GetFileAttributesW(at.c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("admission fixture reparse path");if(at==at.root_path())break;}
        for(const char* name:{"admission-samples.csv","admission-pairs.csv","admission-checks.csv","admission-allocations.csv","admission-build.json","admission-result.json"})
            if(std::filesystem::exists(output/name))throw std::runtime_error("admission output already exists");
        samples.open(output/"admission-samples.csv");pairs.open(output/"admission-pairs.csv");checks.open(output/"admission-checks.csv");allocations.open(output/"admission-allocations.csv");
        samples<<"variant,sample,warmup,outcome,error,complete,committed,wall_ms,internal_ms,bodies,candidate_pairs,queries,reported_bytes,certificate_margin,allocation_attempts,allocation_count,allocated_bytes,peak_live_bytes,reason\n"<<std::setprecision(17);
        pairs<<"variant,sample,other_index,generation,world,old_outcome,new_outcome,old_termination,new_termination,comparable,old_measure,new_measure,method,reason\n"<<std::setprecision(17);
        checks<<"check,result\n";
        allocations<<"variant,sample,attempt,bytes,alignment,caller_pc,module_base,module_rva,module,operator_kind,result,pointer\n";
        Require(bool(samples)&&bool(pairs)&&bool(checks)&&bool(allocations),"evidence streams open");
        {
            std::ofstream build(output/"admission-build.json");
            build<<"{\"iterator_debug_level\":"<<_ITERATOR_DEBUG_LEVEL<<",\"sizeof_report\":"<<sizeof(ColliderAdmissionReport)<<",\"sizeof_pair\":"<<sizeof(ColliderAdmissionPair)<<",\"sizeof_pointer\":"<<sizeof(void*)<<",\"msc_ver\":"<<_MSC_VER;
#ifdef _M_FP_FAST
            build<<",\"fp_fast_macro_defined\":true,\"fp_fast_macro_value\":"<<_M_FP_FAST;
#else
            build<<",\"fp_fast_macro_defined\":false";
#endif
#if defined(GENGINE_CONFIG_RELEASE) || defined(NDEBUG)
            build<<",\"Release_latency_gate_enabled\":true";
#else
            build<<",\"Release_latency_gate_enabled\":false";
#endif
            build<<"}\n";build.flush();Require(bool(build),"native iterator/report/FP build identity retained");
        }
        Require(ColliderReplay::Enabled()&&!ColliderReplay::request.failed,"preserved replay request loaded");
        return true;
    }
    inline void Report(const char* variant,int sample,const ColliderAdmissionReport& r,double elapsed,const AdmissionAllocation::Stats& a)
    {
        samples<<variant<<','<<sample<<','<<(sample<2)<<','<<int(r.outcome)<<','<<int(r.error)<<','<<r.complete<<','<<r.committed<<','<<elapsed<<','<<r.elapsedMs<<','<<r.assessedBodies<<','<<r.candidatePairs<<','<<r.queryCount<<','<<r.trackedPeakBytes<<','<<r.certificateMargin<<','<<a.attempts<<','<<a.count<<','<<a.bytes<<','<<a.peak<<','<<std::quoted(r.reason?r.reason:"")<<'\n';
        for(const auto& p:r.pairs)pairs<<variant<<','<<sample<<','<<p.other.index<<','<<p.other.generation<<','<<p.other.registry<<','<<int(p.oldOutcome)<<','<<int(p.newOutcome)<<','<<p.oldTermination<<','<<p.newTermination<<','<<p.comparableMeasure<<','<<p.oldMeasure<<','<<p.newMeasure<<','<<std::quoted(p.method?p.method:"")<<','<<std::quoted(p.reason?p.reason:"")<<'\n';
        for(std::size_t i=0;i<(std::min)(a.attempts,AdmissionAllocation::attemptEvents.size());++i)
        {
            const auto& event=AdmissionAllocation::attemptEvents[i];HMODULE module{};
            const bool found=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(event.caller),&module)!=0;
            std::array<wchar_t,MAX_PATH> wide{};std::array<char,MAX_PATH*4> name{};
            if(found&&GetModuleFileNameW(module,wide.data(),DWORD(wide.size())))WideCharToMultiByte(CP_UTF8,0,wide.data(),-1,name.data(),int(name.size()),nullptr,nullptr);
            const auto base=reinterpret_cast<std::uintptr_t>(module);
            allocations<<variant<<','<<sample<<','<<event.ordinal<<','<<event.bytes<<','<<event.alignment<<','<<event.caller<<','<<base<<','<<(found?event.caller-base:0)<<','<<std::quoted(name.data())<<','<<event.kind<<','<<event.result<<','<<event.pointer<<'\n';
        }
        samples.flush();pairs.flush();allocations.flush();
        Require(bool(samples)&&bool(pairs)&&bool(allocations),"sample export complete");
    }
    inline bool Risk(const ColliderAdmissionReport& r,std::uint32_t index,const char* label)
    {for(const auto& p:r.pairs)if(p.other.index==index&&p.reason&&std::strstr(p.reason,label))return true;return false;}
    struct State
    {
        std::vector<std::uint8_t> physics,scene;
        PhysicsProfileSnapshot profile;
        State(PhysicsSystem& system,_Scene* owner):physics(ColliderAdmissionCheckAccess::Image(system)),scene(owner?owner->InspectColliderAdmissionState():std::vector<std::uint8_t>{}),profile(GetPhysicsProfileSnapshot()){}
        bool Same(PhysicsSystem& system,_Scene* owner)const
        {
            const auto now=GetPhysicsProfileSnapshot();
            bool countersPreserved=profile.stepCount==now.stepCount;
            static_assert(sizeof(PhysicsProfileSnapshot)%sizeof(std::uint64_t)==0);
            for(std::size_t i=0;i<sizeof(profile)/sizeof(std::uint64_t);++i)
            {
                std::uint64_t before{},after{};
                std::memcpy(&before,reinterpret_cast<const unsigned char*>(&profile)+i*sizeof(before),sizeof(before));
                std::memcpy(&after,reinterpret_cast<const unsigned char*>(&now)+i*sizeof(after),sizeof(after));
                countersPreserved &= after>=before;
            }
            return physics==ColliderAdmissionCheckAccess::Image(system)&&(!owner||scene==owner->InspectColliderAdmissionState())&&countersPreserved;
        }
    };
    inline void ZeroHeapReports(_Scene& scene)
    {
        static_assert(std::is_nothrow_default_constructible_v<ColliderAdmissionReport>);
        static_assert(std::is_nothrow_move_constructible_v<ColliderAdmissionReport>);
        static_assert(std::is_nothrow_move_assignable_v<ColliderAdmissionReport>);
        static_assert(!std::is_copy_constructible_v<ColliderAdmissionReport>);
        bool values=false;
        AdmissionAllocation::Begin(1);
        {
            ColliderAdmissionReport source;source.error=ColliderAdmissionError::AllocationFailure;source.reason="allocation failed";
            ColliderAdmissionReport moved(std::move(source));ColliderAdmissionReport assigned;assigned=std::move(moved);
            values=assigned.error==ColliderAdmissionError::AllocationFailure&&assigned.outcome==Outcome::Indeterminate&&assigned.pairs.empty();
        }
        const auto alloc=AdmissionAllocation::End();
        Require(values&&alloc.attempts==0&&alloc.count==0&&AdmissionAllocation::stats.liveCount==0,"default error report and both ownership moves allocate zero under failAt1");
        AdmissionAllocation::Begin(1);
        {
            auto invalid=scene.AssessBodyColliderReplacement(_Entity{},BoxShapeDesc{Vec3f(1)});
            values=invalid.error==ColliderAdmissionError::InvalidIdentity&&!invalid.committed;
        }
        const auto invalidAlloc=AdmissionAllocation::End();
        Require(values&&invalidAlloc.attempts==0&&AdmissionAllocation::stats.liveCount==0,"Scene invalid-binding error return allocates zero under failAt1");
    }
    inline void PopulatedReportMove(PhysicsSystem& system,PhysicsBodyHandle handle)
    {
        {
            AdmissionAllocation::Begin();auto source=system.AssessBodyColliderReplacement(handle,ColliderReplay::request.box);
            const auto prepared=AdmissionAllocation::End();
            Require(source.outcome==Outcome::NoAddedOverlap&&!source.pairs.empty()&&prepared.liveCount>0,"populated report owns measured detail storage");
            const auto count=source.pairs.size(),attempts=AdmissionAllocation::stats.attempts;
            AdmissionAllocation::failAt=attempts+1;AdmissionAllocation::enabled=true;
            ColliderAdmissionReport moved(std::move(source));ColliderAdmissionReport assigned;assigned=std::move(moved);
            const auto after=AdmissionAllocation::End();
            Report("populated_report_owner_move",0,assigned,0,after);
            Require(after.attempts==attempts&&assigned.pairs.size()==count&&source.pairs.empty()&&moved.pairs.empty(),"populated report moves transfer exact ownership without allocation under armed OOM");
        }
        Require(AdmissionAllocation::stats.liveCount==0,"moved populated report detail storage retires exactly");
    }
    template<class Call>
    inline void AllocationSweep(const char* name,PhysicsSystem& system,_Scene* scene,Call&& call,
        Outcome expected,bool expectCommit)
    {
        bool terminal=false;std::size_t failedPositions{},terminalCount{};
        for(std::size_t failAt=1;failAt<=512&&!terminal;++failAt)
        {
            const State before(system,scene);bool committed=false;
            checks<<std::quoted(name)<<",ATTEMPT_BEGIN_"<<failAt<<'\n';checks.flush();
            if(!checks)Require(false,"allocation sweep attempt-BEGIN marker flushed");
            {
                AdmissionAllocation::Begin(failAt);const auto start=std::chrono::steady_clock::now();
                auto report=call();
                const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                const auto alloc=AdmissionAllocation::End();Report(name,int(failAt),report,elapsed,alloc);
                Require(!alloc.overflow&&alloc.peak<=4194304,"fault-sweep events and actual peak remain within bounds");
                terminal=alloc.attempts<failAt;committed=report.committed;
                if(terminal)
                {
                    terminalCount=alloc.attempts;
                    Require(report.outcome==expected&&committed==expectCommit,"terminal fault-sweep call has expected verdict/commit");
                    Require(!expectCommit||report.complete,"terminal committed assessment is complete");
                    if(std::strstr(name,"changed_inset"))Require(report.certificateMargin>0,"changed inset commit uses positive native certificate");
                }
                else
                {
                    ++failedPositions;
                    Require(alloc.attempts==failAt&&report.error==ColliderAdmissionError::AllocationFailure&&report.outcome==Outcome::Indeterminate&&!committed,"each actual allocation failure returns typed zero-allocation fail-closed error");
                }
            }
            if(!committed)
                Require(AdmissionAllocation::stats.liveCount==0&&before.Same(system,scene),"fault-sweep preview/rejection retires all temporaries and preserves exact state");
        }
        Require(terminal&&failedPositions==terminalCount,"all actual allocation positions covered contiguously without skipped faults");
        checks<<std::quoted(name)<<",COMPLETE_"<<failedPositions<<"_FAILED_POSITIONS_THEN_TERMINAL_"<<terminalCount<<"_ALLOCATIONS\n";checks.flush();
    }

    inline void Variant(const char* name,PhysicsSystem& system,PhysicsBodyHandle h,const CollisionShapeDesc& descriptor,
                        Outcome expected,_Scene* scene=nullptr,bool fitRisks=false,bool historicalBoundary=false)
    {
        const State before(system,scene);std::vector<double> measured;measured.reserve(32);
        for(int i=0;i<34;++i)
        {
            {
                AdmissionAllocation::Begin();const auto start=std::chrono::steady_clock::now();
                auto result=system.AssessBodyColliderReplacement(h,descriptor);
                const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                const auto alloc=AdmissionAllocation::End();Report(name,i,result,elapsed,alloc);
                reportAllocationCount=(std::max)(reportAllocationCount,alloc.attempts);
                bool expectedOutcome=result.outcome==expected;
                if(historicalBoundary&&result.outcome==Outcome::NoAddedOverlap)
                {
                    expectedOutcome=result.complete&&result.certificateMargin==0&&
                        std::all_of(result.pairs.begin(),result.pairs.end(),[](const auto& pair){return pair.newOutcome==ColliderPairOutcome::SeparatedByBounds;});
                    Require(expectedOutcome,"historical .5 containment remains unproved; any current acceptance uses only complete pair separation");
                }
                Require(expectedOutcome&&!result.committed,"variant expected outcome without commit");
                if(std::strcmp(name,"strict_inset_capsule_04")==0)Require(result.certificateMargin>0,"strict inset native containment certificate has positive margin");
                if(expected!=Outcome::Indeterminate)Require(result.complete,"successful/rejected assessment complete");
                Require(!alloc.overflow&&alloc.peak<=4194304&&result.trackedPeakBytes<=4194304,"assessment tracked memory within 4MiB");
                Require(result.queryCount<=8190&&result.assessedBodies<=4096,"assessment query/body limits");
                if(fitRisks){Require(Risk(result,13,"NEW")&&Risk(result,21,"NEW")&&Risk(result,4,"WORSENED"),"captured fitted risks13/21 NEW and floor4 WORSENED");}
                if(i>=2)measured.push_back(elapsed);
            }
            Require(AdmissionAllocation::stats.liveCount==0,"all preview temporary allocations retired");
            Require(before.Same(system,scene),"preview preserves complete captured state and profile counters");
        }
        std::sort(measured.begin(),measured.end());
#if defined(GENGINE_CONFIG_RELEASE) || defined(NDEBUG)
        Require(measured[30]<=10.0&&measured.back()<=25.0,"Release32sample p95<=10ms max<=25ms");
#endif
    }
    inline PhysicsBodyHandle Add(PhysicsWorld& world,const CollisionShapeDesc& desc,const Vec3f& p,Component::BodyType type=Component::BodyType::Dynamic)
    {
        auto shape=CreateCollisionShape(desc);if(!shape)throw std::runtime_error("fixture shape creation");
        PhysicsBodyDesc body;body.position=p;body.type=type;auto result=world.CreateBody(body,*shape);
        if(!result)throw std::runtime_error("fixture body creation");return *result;
    }
    inline void FreeVariant(PhysicsSystem& captured)
    {
        PhysicsSystem copy;copy.SetPhysicsWorld(new PhysicsWorld());auto& world=*copy.GetPhysicsWorld();
        const auto& bodies=PhysicsTestAccess::Bodies(captured.GetPhysicsWorld());std::vector<PhysicsBodyHandle> handles;
        for(unsigned i=0;i<bodies.size();++i)
        {
            const auto& b=*bodies[i];CollisionShapeDesc desc;
            if(b.m_Shape->GetShapeType()==ShapeType::Sphere)desc=SphereShapeDesc{static_cast<const ShapeSphere*>(b.m_Shape)->GetRadius()};
            else if(b.m_Shape->GetShapeType()==ShapeType::Box){auto bounds=b.m_Shape->GetBounds();desc=BoxShapeDesc{(bounds.maxs-bounds.mins)*.5f,(bounds.maxs+bounds.mins)*.5f};}
            else throw std::runtime_error("unsupported captured clone shape");
            auto h=Add(world,desc,i==17?Vec3f(30,10,30):b.m_Position,b.Type);handles.push_back(h);
            auto* clone=PhysicsTestAccess::Resolve(world,h);clone->m_Orientation=b.m_Orientation;clone->m_InvMass=b.m_InvMass;
            clone->m_CollisionLayer=b.m_CollisionLayer;clone->m_CollisionMask=b.m_CollisionMask;
        }
        Require(handles.size()==22,"free diagnostic copies all22 current geometries and filters");
        Variant("fitted_free_copy",copy,handles[17],ColliderReplay::request.fitted,Outcome::NoAddedOverlap);
        const auto begin=std::chrono::steady_clock::now();auto legacy=copy.ReplaceBodyCollider(handles[17],ColliderReplay::request.box);
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        std::ofstream baseline(output/"baseline-transaction.json");baseline<<std::setprecision(17)<<"{\"scope\":\"legacy identical transaction in22body same-geometry free-location diagnostic copy; no retained contacts cloned\",\"elapsed_ms\":"<<elapsed<<",\"success\":"<<(legacy?"true":"false")<<"}\n";
        Require(bool(legacy)&&bool(baseline),"baseline transaction cost separately retained");
        AllocationSweep("apply_changed_separated",copy,nullptr,[&]{return copy.ReplaceBodyColliderIfNoAddedOverlap(handles[17],ColliderReplay::request.fitted);},Outcome::NoAddedOverlap,true);
        const auto* replacement=PhysicsTestAccess::Resolve(world,handles[17])->m_Shape;
        Require(replacement->GetShapeType()==ShapeType::Capsule&&static_cast<const ShapeCapsule*>(replacement)->GetRadius()==ColliderReplay::request.fitted.radius,"separated changed collider committed actual fitted geometry");

    }
    inline void ChangedInsetCommit(PhysicsSystem& captured)
    {
        PhysicsSystem copy;copy.SetPhysicsWorld(new PhysicsWorld());auto& world=*copy.GetPhysicsWorld();
        const auto& bodies=PhysicsTestAccess::Bodies(captured.GetPhysicsWorld());std::array<PhysicsBodyHandle,22> handles{};
        Require(bodies.size()==handles.size(),"strict inset diagnostic copy has22 bodies");
        for(unsigned i=0;i<bodies.size();++i)
        {
            const auto& b=*bodies[i];CollisionShapeDesc desc;
            if(b.m_Shape->GetShapeType()==ShapeType::Sphere)desc=SphereShapeDesc{static_cast<const ShapeSphere*>(b.m_Shape)->GetRadius()};
            else if(b.m_Shape->GetShapeType()==ShapeType::Box){const auto bounds=b.m_Shape->GetBounds();desc=BoxShapeDesc{(bounds.maxs-bounds.mins)*.5f,(bounds.maxs+bounds.mins)*.5f};}
            else throw std::runtime_error("unsupported inset copy geometry");
            handles[i]=Add(world,desc,b.m_Position,b.Type);auto* clone=PhysicsTestAccess::Resolve(world,handles[i]);
            clone->m_Orientation=b.m_Orientation;clone->m_InvMass=b.m_InvMass;clone->m_CollisionLayer=b.m_CollisionLayer;clone->m_CollisionMask=b.m_CollisionMask;
        }
        AllocationSweep("apply_changed_inset_04",copy,nullptr,[&]{return copy.ReplaceBodyColliderIfNoAddedOverlap(handles[17],CapsuleShapeDesc{.4f,.4f});},Outcome::NoAddedOverlap,true);
        const auto* replacement=PhysicsTestAccess::Resolve(world,handles[17])->m_Shape;
        Require(replacement->GetShapeType()==ShapeType::Capsule&&static_cast<const ShapeCapsule*>(replacement)->GetRadius()==.4f&&static_cast<const ShapeCapsule*>(replacement)->GetHalfLength()==.4f,"strict inset changed commit publishes actual04geometry");
    }

    inline void RejectCommand(const char* name,PhysicsSystem& system,PhysicsBodyHandle h,const CollisionShapeDesc& desc,_Scene* scene=nullptr)
    {
        const State before(system,scene);auto result=system.ReplaceBodyColliderIfNoAddedOverlap(h,desc);
        Report(name,0,result,0,{});Require(!result.committed&&result.outcome!=Outcome::NoAddedOverlap,"rejected command remains uncommitted");
        Require(before.Same(system,scene),"rejected command preserves captured state and profile counters");
    }
    inline void FaultChecks(PhysicsSystem& system,PhysicsBodyHandle h,_Scene& scene)
    {
        RejectCommand("invalid_shape",system,h,SphereShapeDesc{-1},&scene);
        RejectCommand("nonfinite_shape",system,h,SphereShapeDesc{std::numeric_limits<float>::quiet_NaN()},&scene);
        RejectCommand("unsupported_shape",system,h,ConvexHullShapeDesc{},&scene);
        RejectCommand("fitted_reject_command",system,h,ColliderReplay::request.fitted,&scene);
        for(unsigned fault=0;fault<7;++fault)
        {
            ColliderAdmissionCheckAccess::Fault(system,fault,true);
            const CollisionShapeDesc desc=fault==2?CollisionShapeDesc{BoxShapeDesc{Vec3f(1.1f)}}:CollisionShapeDesc{ColliderReplay::request.fitted};
            const char* name=fault==0?"deadline_exhausted":fault==1?"body_limit_exhausted":fault==2?"zero_query_budget_unproved_geometry":fault==3?"storage_limit_exhausted":fault==4?"forced_contact_search_failure":fault==5?"forced_closest_search_failure":"forced_iteration_exhaustion";
            RejectCommand(name,system,h,desc,&scene);
            ColliderAdmissionCheckAccess::Fault(system,fault,false);
        }
    }
    inline void FreshnessChecks()
    {
        {
            PhysicsSystem sphereCase;sphereCase.SetPhysicsWorld(new PhysicsWorld());auto& sphereWorld=*sphereCase.GetPhysicsWorld();
            const auto sphere=Add(sphereWorld,SphereShapeDesc{.1f},Vec3f(0));
            (void)Add(sphereWorld,BoxShapeDesc{Vec3f(.1f)},Vec3f(1.5f,0,0),Component::BodyType::Static);
            const auto result=sphereCase.AssessBodyColliderReplacement(sphere,SphereShapeDesc{1.f});
            Report("sphere_unit_support_direction",0,result,0,{});
            Require(result.outcome!=Outcome::RejectedRisk&&std::none_of(result.pairs.begin(),result.pairs.end(),[](const auto& pair){return pair.reason&&std::strstr(pair.reason,"NEW:");}),"Sphere support normalization cannot invent NEW overlap beyond radius");
        }

        PhysicsSystem system;system.SetPhysicsWorld(new PhysicsWorld());auto& world=*system.GetPhysicsWorld();
        auto target=Add(world,BoxShapeDesc{Vec3f(1)},Vec3f(0));auto other=Add(world,BoxShapeDesc{Vec3f(1)},Vec3f(10,0,0));
        const CapsuleShapeDesc expanded{1.4f,1.f};
        Require(system.AssessBodyColliderReplacement(target,expanded).outcome==Outcome::NoAddedOverlap,"freshness distant baseline clear");
        Require(bool(system.SetBodyPose(other,Vec3f(2,0,0),Quat(1,0,0,0))),"fixture neighbor enters");
        RejectCommand("stale_neighbor_entry",system,target,expanded);
        auto* neighbor=PhysicsTestAccess::Resolve(world,other);neighbor->m_CollisionMask=0;
        Require(system.AssessBodyColliderReplacement(target,expanded).outcome==Outcome::NoAddedOverlap,"filtered overlap explicitly skipped");
        neighbor->m_CollisionMask=~std::uint32_t(0);RejectCommand("stale_filter",system,target,expanded);
        Require(bool(system.SetBodyPose(other,Vec3f(10,0,0),Quat(1,0,0,0))),"fixture neighbor leaves");
        Require(system.AssessBodyColliderReplacement(target,expanded).outcome==Outcome::NoAddedOverlap,"target pose preview clear");
        Require(bool(system.SetBodyPose(target,Vec3f(8,0,0),Quat(1,0,0,0))),"fixture target enters");RejectCommand("stale_target_pose",system,target,expanded);
        auto stale=other;Require(bool(world.DestroyBody(other)),"fixture deletes neighbor");other=Add(world,BoxShapeDesc{Vec3f(1)},Vec3f(10,0,0));
        RejectCommand("stale_deleted_generation",system,stale,expanded);RejectCommand("stale_membership_recreated",system,target,expanded);
        PhysicsWorld foreign;auto wrong=Add(foreign,BoxShapeDesc{Vec3f(1)},Vec3f(0));RejectCommand("wrong_world",system,wrong,expanded);
        Require(bool(system.ReplaceBodyCollider(target,SphereShapeDesc{1})),"legacy replacement remains callable");
        Require(bool(system.ReplaceBodyCollider(target,BoxShapeDesc{Vec3f(1)})),"shape A-B-A fixture");RejectCommand("stale_shape_ABA",system,target,expanded);
        // Exact touch and next representable positions are never accepted as a
        // separation certificate merely because an epsilon was chosen.
        for(float x:{2.f,std::nextafter(2.f,0.f),std::nextafter(2.f,std::numeric_limits<float>::infinity())})
        {
            auto* a=PhysicsTestAccess::Resolve(world,target);a->m_Position=Vec3f(0);
            neighbor=PhysicsTestAccess::Resolve(world,other);neighbor->m_Position=Vec3f(x,0,0);
            RejectCommand("touch_and_adjacent_float_boundary",system,target,expanded);
        }
    }
    inline void MeasurementEnvironment()
    {
        const auto file=output/"measurement-processes.csv";
        Require(!std::filesystem::exists(file),"measurement process snapshot destination unused");
        std::ofstream f(file);f<<"utc_unix_ns,pid,parent_pid,executable,classification\n";
        const auto timestamp=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        Require(snapshot!=INVALID_HANDLE_VALUE,"measurement process snapshot available");
        PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool safe=true,ownFound=false;
        if(!Process32FirstW(snapshot,&entry)){CloseHandle(snapshot);Require(false,"measurement process enumeration started");}
        do
        {
            std::array<char,MAX_PATH*4> name{};
            const int converted=WideCharToMultiByte(CP_UTF8,0,entry.szExeFile,-1,name.data(),int(name.size()),nullptr,nullptr);
            if(!converted){safe=false;name[0]='?';name[1]=0;}
            const bool compiler=_wcsicmp(entry.szExeFile,L"cl.exe")==0||_wcsicmp(entry.szExeFile,L"link.exe")==0;
            const bool rbs=_wcsicmp(entry.szExeFile,L"RigidBodySimulation.exe")==0;
            const bool own=rbs&&entry.th32ProcessID==GetCurrentProcessId();
            const bool ide=_wcsicmp(entry.szExeFile,L"MSBuild.exe")==0||_wcsicmp(entry.szExeFile,L"devenv.exe")==0;
            safe &= !compiler&&(!rbs||own);ownFound |= own;
            f<<timestamp<<','<<entry.th32ProcessID<<','<<entry.th32ParentProcessID<<','<<std::quoted(name.data())<<','
             <<(compiler?"blocking_compiler":own?"owned_RBS":rbs?"blocking_other_RBS":ide?"IDE_or_reusable_worker_record_only":"other")<<'\n';
        }while(Process32NextW(snapshot,&entry));
        const DWORD enumerationEnd=GetLastError();CloseHandle(snapshot);f.flush();
        Require(enumerationEnd==ERROR_NO_MORE_FILES&&bool(f),"measurement process snapshot complete and retained");
        Require(safe&&ownFound,"immediate measurement environment has only owned RBS and no compiler/linker");
    }

    #include "ColliderForceAdmissionChecks.h"

    inline void Execute(_Scene& scene,SceneRenderResources& resources)
    {
        auto& request=ColliderReplay::request;auto& observer=*ColliderReplacementObservation::session;
        Require(scene.GetPhysicsTiming().totalSteps==request.triggerSteps,"canonical trigger Physics step");
        Require(ColliderReplay::Verify(scene,false),"canonical captured physical identity");
        auto target=scene.GetEntityByUUID(UUID{request.selected});auto binding=scene.GetBodyBinding(target);
        Require(bool(binding),"captured target binding");auto& system=*scene.GetPhysicsSystem();
        const auto source=resources.GeometrySource(target.GetComponent<Component::MeshRendererComponent>().mesh);
        Require(bool(source)&&bool(source->source),"captured mesh source available");
        Require(ColliderCapture::Hash(source->source->Vertices())==request.verticesHash&&ColliderCapture::Hash(source->source->Indices())==request.indicesHash,"captured mesh byte identities");
        auto fitted=FitMeshCollider(*source->source,target.GetComponent<Component::Transform3DComponent>().Scale,ShapeType::Capsule);
        Require(fitted&&std::holds_alternative<CapsuleShapeDesc>(*fitted)&&std::get<CapsuleShapeDesc>(*fitted).radius==request.fitted.radius&&std::get<CapsuleShapeDesc>(*fitted).halfLength==request.fitted.halfLength,"current Fit ON descriptor matches capture");
        ColliderReplacementObservation::BeginReplacementCapture(scene,target);
        Require(!observer.failed&&ColliderReplay::SameImage(request.initialCache,observer.output/"observer-cache-initial.bin")&&ColliderReplay::SameImage(request.commandCache,observer.output/"observer-cache-command.bin"),"exact canonical initial and command cache bytes");
        RejectCommand("active_observation_busy",system,*binding,request.box,&scene);
        Require(PD::observer==&observer.observer&&PD::queryObserver==&observer.rawQuerySink&&PD::Coupled::sink==&observer.querySink,"fixture owns all detached observer sinks");
        PD::observer=nullptr;PD::queryObserver=nullptr;PD::Coupled::sink=nullptr;
        MeasurementEnvironment();
        ZeroHeapReports(scene);
        PopulatedReportMove(system,*binding);
        Variant("identical_box",system,*binding,request.box,Outcome::NoAddedOverlap,&scene);
        Variant("strict_inset_capsule_04",system,*binding,CapsuleShapeDesc{.4f,.4f},Outcome::NoAddedOverlap,&scene);
        Variant("historical_boundary_capsule_05",system,*binding,request.manual,Outcome::Indeterminate,&scene,false,true);
        Variant("fitted_original",system,*binding,*fitted,Outcome::RejectedRisk,&scene,true);
        AllocationSweep("preview_identical_manual",system,&scene,[&]{return scene.AssessBodyColliderReplacement(target,request.box);},Outcome::NoAddedOverlap,false);
        AllocationSweep("preview_changed_inset_manual",system,&scene,[&]{return scene.AssessBodyColliderReplacement(target,CapsuleShapeDesc{.4f,.4f});},Outcome::NoAddedOverlap,false);
        AllocationSweep("preview_fitted_UI_route",system,&scene,[&]{return RunMeshColliderAdmission(scene,resources,target,ShapeType::Capsule,false);},Outcome::RejectedRisk,false);
        AllocationSweep("apply_fitted_UI_route_rejected",system,&scene,[&]{return RunMeshColliderAdmission(scene,resources,target,ShapeType::Capsule,true);},Outcome::RejectedRisk,false);
        FreeVariant(system);Require(AdmissionAllocation::stats.liveCount==0,"separated changed diagnostic world retires retained ownership");
        ChangedInsetCommit(system);Require(AdmissionAllocation::stats.liveCount==0,"inset changed diagnostic world retires retained ownership");
        FaultChecks(system,*binding,scene);FreshnessChecks();ForceAdmissionChecks();
        const auto* old=PhysicsTestAccess::Body(target)->m_Shape;
        AllocationSweep("apply_captured_identical_last",system,&scene,[&]{return scene.ReplaceBodyColliderIfNoAddedOverlap(target,request.box);},Outcome::NoAddedOverlap,true);
        const auto finalBinding=scene.GetBodyBinding(target);
        Require(PhysicsTestAccess::Body(target)->m_Shape!=old&&finalBinding&&*finalBinding==*binding,"successful owner retirement with stable binding");
        Require(system.GetSolverIterations()==1&&system.IsSleepingEnabled()&&scene.GetPhysicsStep().count()==1./60.,"default Solver sleep timestep preserved");
        observer.Finish();
    }
    inline void ReconstructPrefix(_Scene& scene,SceneRenderResources& resources)
    {
        auto& r=ColliderReplay::request;
        Require(r.frame==0&&!r.performed,"batch prefix begins at original fresh Scene boundary");
        const auto sequencePath=output/"prefix-sequence.csv",summaryPath=output/"prefix-summary.json";
        Require(!std::filesystem::exists(sequencePath)&&!std::filesystem::exists(summaryPath),"prefix evidence destinations unused");
        std::ofstream sequence(sequencePath);sequence<<"input_index,elapsed_bits,elapsed_seconds,paused,before_steps,after_steps,before_Update_calls,after_Update_calls\n"<<std::setprecision(17);
        std::uint64_t calls{},hash=14695981039346656037ull;
        const auto hashByte=[&](std::uint8_t value){hash^=value;hash*=1099511628211ull;};
        while(r.frame+1<r.triggerFrame)
        {
            const auto input=r.frame;const double elapsed=ColliderReplay::FrameElapsed(0);
            const bool paused=ColliderReplay::FramePaused(scene.IsPaused());scene.SetPaused(paused);
            const auto bits=std::bit_cast<std::uint64_t>(elapsed);
            for(unsigned byte=0;byte<8;++byte)hashByte(std::uint8_t(bits>>(byte*8)));hashByte(std::uint8_t(paused));
            ColliderReplacementObservation::BeforeFrame(scene,elapsed);
            if(!ColliderReplay::BeforeUpdate(scene,resources))Require(false,"original replay prefix admission");
            const auto before=scene.GetPhysicsTiming().totalSteps;
            sequence<<input<<','<<bits<<','<<elapsed<<','<<paused<<','<<before<<',';
            scene.Update(Timestep(elapsed));++calls;
            sequence<<scene.GetPhysicsTiming().totalSteps<<','<<(calls-1)<<','<<calls<<'\n';
            if(ColliderReplacementObservation::AfterFrame())Require(false,"original replay observer prefix completion");
            if((calls&255u)==0)sequence.flush();
        }
        Require(r.frame+1==r.triggerFrame,"batch stops before captured command Update");
        const auto commandInput=r.frame;const double commandElapsed=ColliderReplay::FrameElapsed(0);
        const bool commandPaused=ColliderReplay::FramePaused(scene.IsPaused());scene.SetPaused(commandPaused);
        ColliderReplacementObservation::BeforeFrame(scene,commandElapsed);
        sequence.flush();Require(bool(sequence),"exact captured elapsed/pause sequence retained");
        const char* runnerHash=std::getenv("GENGINE_COLLIDER_ADMISSION_INPUT_SHA256");
        const char* requestPath=std::getenv("GENGINE_COLLIDER_REPLAY_REQUEST");
        std::ofstream summary(summaryPath);
        summary<<"{\"before_Update_calls\":0,\"after_Update_calls\":"<<calls<<",\"command_Update_called\":false,\"command_input_index\":"<<commandInput
               <<",\"command_elapsed_bits\":"<<std::bit_cast<std::uint64_t>(commandElapsed)<<",\"command_paused\":"<<(commandPaused?"true":"false")
               <<",\"trigger_frame\":"<<r.triggerFrame<<",\"actual_frame\":"<<r.frame<<",\"required_steps\":"<<r.triggerSteps<<",\"actual_steps\":"<<scene.GetPhysicsTiming().totalSteps
               <<",\"consumed_sequence_fnv1a64\":"<<hash<<",\"request_path\":"<<std::quoted(requestPath?requestPath:"")<<",\"runner_request_sha256\":"<<std::quoted(runnerHash?runnerHash:"recorded by owned runner")
               <<",\"same_Scene_Update\":true,\"fresh_capture\":false,\"state_injection\":false,\"canonical_cache_checks_still_required\":true}\n";
        summary.flush();Require(bool(summary),"prefix call counts and input identity retained");
        Require(calls+1==r.triggerFrame&&scene.GetPhysicsTiming().totalSteps==r.triggerSteps,"exact captured prefix call and step counts");
    }

    inline bool RunOnce(_Scene& scene,SceneRenderResources& resources)
    {
        try
        {
            if(!Enabled()||done)return done;
            if(ColliderReplay::request.failed)Require(false,"canonical replay remains valid");
            ReconstructPrefix(scene,resources);
            Require(bool(ColliderReplacementObservation::session),"canonical observer available");
            Execute(scene,resources);done=true;
        }
        catch(const std::exception& e)
        {
            AdmissionAllocation::enabled=false;failed=true;done=true;
            if(!*failure)failure="fixture exception; inspect stderr";
            std::fprintf(stderr,"Collider admission fixture stopped: %s\n",e.what());
            if(ColliderReplacementObservation::session)ColliderReplacementObservation::session->Finish();
        }
        if(done&&!output.empty())
        {
            const auto file=output/"admission-result.json";
            if(!std::filesystem::exists(file))
            {
                std::ofstream f(file);f<<"{\"status\":"<<std::quoted(failed?"FAIL":"SCOPED_CHECKS_COMPLETE")<<",\"failure\":"<<std::quoted(failure)<<",\"checks_passed\":"<<passed
                <<",\"full_qualification_pass\":false,\"coverage_limit\":\"Broadphase heap bytes require matching static no-access proof; synthetic query failures test classification only, no real GJK failure proof; original47 failures unchanged; captured postdestroy allocation zero is not observed; clean shutdown and source/UI inspection require runner review\"}\n";
            }
        }
        return done;
    }
}
#endif
