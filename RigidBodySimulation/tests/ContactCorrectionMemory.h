#pragma once
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <malloc.h>
#include <new>
#include <new.h>

// Opt-in supporting instrumentation. Only the isolated Sandbox main TU defines
// the replacement allocation functions; ordinary RBS and Qualification do not.
namespace Rbs::ContactCorrectionMemory
{
    struct Trace
    {
        std::uint64_t first{},last{},occurrences{},bytes{};
        unsigned stage{},depth{};
        void* stack[12]{};
    };
    inline std::array<Trace,1024> traces{};
    inline unsigned used{};
    inline bool installed{},opened{},overflow{},failed{};
    inline FILE *ticks{},*updates{};
    inline thread_local bool enabled{},active{};
    inline thread_local unsigned episode{},stage{};
    inline thread_local std::uint64_t ordinal{},localTick{},count{},bytes{},correctionCount{},correctionBytes{},start{},excluded{};
    inline thread_local std::uint64_t attempts{},accepted{},queryFailures{},patchFailures{};
    inline thread_local double querySeconds{};
    inline std::uint64_t totalCount{},totalBytes{},totalAttempts{},totalAccepted{},totalQueryFailures{},totalPatchFailures{};
    inline double totalQuerySeconds{};
    inline std::uint64_t Now() noexcept
    {
        return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    struct Pause
    {
        bool resume=enabled;
        std::uint64_t began=Now();
        Pause() noexcept { enabled=false; }
        ~Pause() noexcept
        {
            if(active)excluded+=Now()-(std::max)(began,start);
            enabled=resume && active;
        }
    };
    inline void Record(std::size_t n) noexcept
    {
        if(!enabled)return;
        enabled=false; // Stack capture and recorder bookkeeping are never counted.
        ++count;bytes+=n;
        if(stage==8){++correctionCount;correctionBytes+=n;}
        Trace next;next.first=next.last=ordinal;next.occurrences=1;next.bytes=n;next.stage=stage;
        next.depth=CaptureStackBackTrace(2,12,next.stack,nullptr);
        if(!next.depth)failed=true;
        unsigned match=used;
        for(unsigned i=0;i<used;++i)
        {
            const auto& t=traces[i];if(t.stage!=next.stage || t.bytes!=next.bytes || t.depth!=next.depth)continue;
            bool same=true;for(unsigned k=0;k<t.depth;++k)same &= t.stack[k]==next.stack[k];
            if(same){match=i;break;}
        }
        if(match<used){++traces[match].occurrences;traces[match].last=ordinal;}
        else if(used==traces.size())overflow=true;
        else traces[used++]=next;
        enabled=active;
    }
    inline void* Allocate(std::size_t n,std::size_t alignment=0)
    {
        for(;;)
        {
            auto* p=alignment?_aligned_malloc((std::max)(n,std::size_t(1)),alignment):std::malloc((std::max)(n,std::size_t(1)));
            if(p){Record(n);return p;}
            if(!_callnewh(n))throw std::bad_alloc();
        }
    }
    inline bool Open(const std::filesystem::path& output)
    {
        if(opened || !installed)return false;
        // Same direct global-new probes as the existing qualification recorder.
        active=enabled=true;count=bytes=0;
        void* a=::operator new(37);void* b=::operator new(65,std::align_val_t(64));
        enabled=active=false;::operator delete(a);::operator delete(b,std::align_val_t(64));
        const bool probe=count==2 && bytes==102 && !failed && !overflow;
        traces={};used=0;count=bytes=ordinal=0;
        if(!probe)return false;
        ticks=std::fopen((output/"contact-allocation-ticks.csv").string().c_str(),"wx");
        updates=std::fopen((output/"contact-update-cost.csv").string().c_str(),"wx");
        if(!ticks || !updates)return false;
        std::fprintf(ticks,"ordinal,episode,tick,physics_stage_window_ns_excluding_callbacks,allocations,bytes,position_stage_allocations,position_stage_bytes,refresh_attempts,refresh_accepted,refresh_query_failure,refresh_patch_invalid,query_patch_seconds\n");
        std::fprintf(updates,"frame,completed_tick,steps_this_update,last_physics_update_ns,last_contact_solve_ns,last_joint_solve_ns\n");
        opened=true;return true;
    }
    inline void Begin(unsigned e,std::uint64_t tick) noexcept
    {
        if(!opened)return;
        if(active){failed=true;return;}
        ++ordinal;episode=e;localTick=tick;stage=0;count=bytes=correctionCount=correctionBytes=0;
        attempts=accepted=queryFailures=patchFailures=0;querySeconds=0;excluded=0;start=Now();active=true;
    }
    inline void Refresh(int reason,double seconds) noexcept
    {
        if(!active || reason<26001 || reason>26003)return;
        ++attempts;accepted+=reason==26001;queryFailures+=reason==26002;patchFailures+=reason==26003;
        querySeconds+=seconds;
    }
    inline void End() noexcept
    {
        if(!opened)return;
        if(!active){failed=true;return;}
        enabled=false;const auto elapsed=Now()-start;active=false;
        std::fprintf(ticks,"%llu,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.17g\n",
            (unsigned long long)ordinal,episode,(unsigned long long)localTick,(unsigned long long)(elapsed>=excluded?elapsed-excluded:0),
            (unsigned long long)count,(unsigned long long)bytes,(unsigned long long)correctionCount,(unsigned long long)correctionBytes,
            (unsigned long long)attempts,(unsigned long long)accepted,(unsigned long long)queryFailures,(unsigned long long)patchFailures,querySeconds);
        totalCount+=count;totalBytes+=bytes;totalAttempts+=attempts;totalAccepted+=accepted;
        totalQueryFailures+=queryFailures;totalPatchFailures+=patchFailures;totalQuerySeconds+=querySeconds;
        failed=failed || std::ferror(ticks)!=0 || overflow;
    }
    inline void Update(unsigned frame,std::uint64_t tick,unsigned steps,std::uint64_t update,std::uint64_t contact,std::uint64_t joint) noexcept
    {
        if(!opened || !steps)return;
        std::fprintf(updates,"%u,%llu,%u,%llu,%llu,%llu\n",frame,(unsigned long long)tick,steps,
            (unsigned long long)update,(unsigned long long)contact,(unsigned long long)joint);
        failed=failed || std::ferror(updates)!=0;
    }
    inline bool Finish(const std::filesystem::path& output)
    {
        if(!opened)return true;
        enabled=false;if(active)failed=true;active=false;
        for(auto** f:{&ticks,&updates})if(*f){const bool ok=std::ferror(*f)==0;failed=std::fclose(*f)!=0 || !ok || failed;*f=nullptr;}
        auto* f=std::fopen((output/"contact-memory.json").string().c_str(),"wx");if(!f)return false;
        LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0)failed=true;
        std::fprintf(f,"{\"diagnostic_only\":true,\"complete\":%s,\"global_new_probe\":true,\"overflow\":%s,\"ticks\":%llu,\"allocations\":%llu,\"bytes\":%llu,\"recorder_bytes\":%zu,\"qpc_frequency_hz\":%llu,\"qpc_resolution_ns\":%.17g,\"refresh_attempts\":%llu,\"refresh_accepted\":%llu,\"refresh_query_failure\":%llu,\"refresh_patch_invalid\":%llu,\"query_patch_seconds\":%.17g,\"image_base\":%llu,\"scope\":\"owner-thread C++ new/new[] during Begin-to-End Physics stage windows; observer callbacks excluded; CRT malloc/driver allocations and before-Begin/after-End work not counted; timings include instrumentation and are not qualified performance\",\"events\":[",
            opened && !failed && !overflow?"true":"false",overflow?"true":"false",(unsigned long long)ordinal,
            (unsigned long long)totalCount,(unsigned long long)totalBytes,sizeof(traces),(unsigned long long)frequency.QuadPart,
            frequency.QuadPart>0?1.e9/double(frequency.QuadPart):0.0,(unsigned long long)totalAttempts,(unsigned long long)totalAccepted,
            (unsigned long long)totalQueryFailures,(unsigned long long)totalPatchFailures,totalQuerySeconds,
            (unsigned long long)reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
        for(unsigned i=0;i<used;++i)
        {
            const auto& t=traces[i];std::fprintf(f,"%s{\"first_ordinal\":%llu,\"last_ordinal\":%llu,\"occurrences\":%llu,\"stage\":%u,\"bytes\":%llu,\"stack\":[",
                i?",":"",(unsigned long long)t.first,(unsigned long long)t.last,(unsigned long long)t.occurrences,t.stage,(unsigned long long)t.bytes);
            for(unsigned k=0;k<t.depth;++k)std::fprintf(f,"%s%llu",k?",":"",(unsigned long long)reinterpret_cast<std::uintptr_t>(t.stack[k]));
            std::fprintf(f,"]}");
        }
        std::fprintf(f,"]}\n");const bool ok=std::ferror(f)==0;return std::fclose(f)==0 && ok && opened && !failed && !overflow;
    }
}
#ifdef GENGINE_CONTACT_CORRECTION_ALLOC_IMPLEMENTATION
namespace Rbs::ContactCorrectionMemory { struct Install { Install(){installed=true;} }; const Install install; }
__declspec(noinline) void* operator new(std::size_t n){return Rbs::ContactCorrectionMemory::Allocate(n);}
__declspec(noinline) void* operator new[](std::size_t n){return Rbs::ContactCorrectionMemory::Allocate(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){return Rbs::ContactCorrectionMemory::Allocate(n,std::size_t(a));}
void* operator new[](std::size_t n,std::align_val_t a){return Rbs::ContactCorrectionMemory::Allocate(n,std::size_t(a));}
void operator delete(void* p,std::align_val_t) noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept{_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept{_aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept{_aligned_free(p);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept{try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept{_aligned_free(p);}
#endif
#endif
