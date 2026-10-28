#pragma once
#include <windows.h>
#include <cstdint>
// Shared observer memory is allocated before BaseApp::Run starts its clock.
// The native consumer owns no rendering commands and never writes per event.
namespace Phase24NativeObservation
{
    constexpr unsigned Capacity=32768;
    struct Boundary {std::uint64_t begin{},end{};};
    struct Shared {
        volatile LONG published{},ready{},finished{},sentinelPhase{},sentinelPass{};
        std::uint64_t heap{},sentinelBegin[2]{},sentinelEnd[2]{};
        Boundary frames[Capacity]{};
    };
}
#if defined(GENGINE_PHASE24_HEAP_CONSUMER)
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <memory>
#include <psapi.h>
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"tdh.lib")
namespace Phase24NativeObservation
{
    constexpr GUID HeapGuid{0x222962ab,0x6180,0x4b88,{0xa8,0x25,0x34,0x6b,0x75,0xf2,0xa2,0x4a}};
    struct Count {std::uint64_t allocations{},bytes{},reallocations{},reallocBytes{},frees{};};
    struct Event {std::uint64_t time{},size{};unsigned opcode{};};
    struct Schema {
        bool valid{};unsigned sizeOffset{},sizeWidth{},heapOffset{},heapWidth{},length{};
        wchar_t fields[4096]{};
    };
    struct Consumer {
        Shared* shared{};DWORD pid{};bool offline{},failed{};
        Count rows[Capacity]{},outside{},gaps{},sentinels[2]{};
        unsigned sentinelExplicit[2]{},sentinelBacking[2]{},sentinelUnexpected[2]{};
        Schema schemas[256]{};
        static constexpr unsigned PendingCapacity=1048576;
        std::unique_ptr<Event[]> pending{new Event[PendingCapacity]};
        unsigned pendingCount{},pendingHighWater{};
        std::uint64_t events{},first{},last{},lost{},buffers{},schemaErrors{},foreign{};
    };
    inline Consumer* consumer{};
    inline std::uint64_t Read(const EVENT_RECORD* e,unsigned offset,unsigned width)
    {std::uint64_t result{};if(offset+width<=e->UserDataLength && width<=8)std::memcpy(&result,static_cast<const char*>(e->UserData)+offset,width);return result;}
    inline bool SchemaFor(EVENT_RECORD* e,Schema& s)
    {
        // Classic v2 has no registered TDH metadata on this host. Each offset
        // below was cross-checked with xperf decoding the SAME raw QPC event;
        // revision05/native-offline02 retains both observations. Sentinel
        // allocations/reallocations validate this layout on every live run.
        const auto op=e->EventHeader.EventDescriptor.Opcode;
        if(e->EventHeader.EventDescriptor.Version==2 && (e->EventHeader.Flags&EVENT_HEADER_FLAG_64_BIT_HEADER)) {
            s.heapOffset=0;s.heapWidth=8;s.sizeOffset=op==0x22?24:8;s.sizeWidth=op==0x24?0:8;
            s.length=op==0x21?28:op==0x22?44:20;s.valid=e->UserDataLength==s.length;
            swprintf_s(s.fields,L"xperf-verified-v2-x64;heap=0:8;size=%u:%u;length=%u",s.sizeOffset,s.sizeWidth,s.length);
            return s.valid;
        }
        alignas(TRACE_EVENT_INFO) unsigned char storage[65536];ULONG size=sizeof(storage);
        auto* info=reinterpret_cast<TRACE_EVENT_INFO*>(storage);
        const auto status=TdhGetEventInformation(e,0,nullptr,info,&size);
        if(status!=ERROR_SUCCESS) {
            swprintf_s(s.fields,L"TDH=%lu;version=%u;flags=%u;time=%llu;length=%u;raw=",status,e->EventHeader.EventDescriptor.Version,e->EventHeader.Flags,std::uint64_t(e->EventHeader.TimeStamp.QuadPart),e->UserDataLength);
            for(unsigned i=0;i<e->UserDataLength && i<64;++i){wchar_t hex[4];swprintf_s(hex,L"%02x",static_cast<const unsigned char*>(e->UserData)[i]);wcscat_s(s.fields,hex);}return false;
        }
        unsigned offset=0;bool haveSize=e->EventHeader.EventDescriptor.Opcode==0x24,haveHeap=false;
        for(ULONG i=0;i<info->TopLevelPropertyCount;++i) {
            const auto& property=info->EventPropertyInfoArray[i];
            const auto* name=reinterpret_cast<const wchar_t*>(storage+property.NameOffset);
            PROPERTY_DATA_DESCRIPTOR descriptor{};descriptor.PropertyName=reinterpret_cast<ULONGLONG>(name);descriptor.ArrayIndex=ULONG_MAX;
            ULONG width{};if(TdhGetPropertySize(e,0,nullptr,1,&descriptor,&width)!=ERROR_SUCCESS)return false;
            wchar_t field[256];swprintf_s(field,L"%s:%u:%u;",name,offset,width);wcscat_s(s.fields,field);
            if((!wcscmp(name,L"Size") || !wcscmp(name,L"AllocSize") || !wcscmp(name,L"NewSize")) && width<=8) {
                s.sizeOffset=offset;s.sizeWidth=width;haveSize=true;
            }
            if(!wcscmp(name,L"HeapHandle") && width<=8) {s.heapOffset=offset;s.heapWidth=width;haveHeap=true;}
            offset+=width;
        }
        s.length=offset;s.valid=haveSize && haveHeap && offset==e->UserDataLength;
        return s.valid;
    }
    inline void Add(Count& c,const Event& e)
    {if(e.opcode==0x21){++c.allocations;c.bytes+=e.size;}else if(e.opcode==0x22){++c.reallocations;c.reallocBytes+=e.size;}else if(e.opcode==0x24)++c.frees;}
    inline bool Associate(const Event& e)
    {
        auto& c=*consumer;if(!c.shared){Add(c.outside,e);return true;}
        const unsigned n=InterlockedCompareExchange(&c.shared->published,0,0);
        // Until the first boundary is published, retain events rather than
        // guessing whether they precede the first frame.
        if(!n)return false;
        if(e.time<c.shared->frames[0].begin){Add(c.outside,e);return true;}
        if(e.time>c.shared->frames[n-1].end) {
            if(InterlockedCompareExchange(&c.shared->finished,0,0)){Add(c.outside,e);return true;}
            return false;
        }
        // Attribute native heap activity over continuous begin-to-next-begin
        // intervals, including work-end/next-begin gaps and other PID threads.
        // CPU work timing still uses the original begin/end boundaries.
        unsigned lo=0,hi=n;
        while(lo<hi){const unsigned mid=(lo+hi)/2;if(c.shared->frames[mid].begin<=e.time)lo=mid+1;else hi=mid;}
        const auto frame=lo-1;Add(c.rows[frame],e);
        if(e.time>c.shared->frames[frame].end)Add(c.gaps,e);
        return true;
    }
    inline void Drain()
    {auto& c=*consumer;unsigned keep=0;for(unsigned i=0;i<c.pendingCount;++i)if(!Associate(c.pending[i]))c.pending[keep++]=c.pending[i];c.pendingCount=keep;}
    void WINAPI EventRecord(EVENT_RECORD* e)
    {
        auto& c=*consumer;if(e->EventHeader.ProviderId!=HeapGuid)return;
        if(e->EventHeader.ProcessId!=c.pid){++c.foreign;return;}
        const auto op=e->EventHeader.EventDescriptor.Opcode;if(op!=0x21 && op!=0x22 && op!=0x24)return;
        ++c.events;const auto time=std::uint64_t(e->EventHeader.TimeStamp.QuadPart);if(!c.first || time<c.first)c.first=time;if(time>c.last)c.last=time;
        auto& schema=c.schemas[op];
        if(!schema.valid && (schema.fields[0] || !SchemaFor(e,schema))){c.failed=true;++c.schemaErrors;return;}
        if(schema.length!=e->UserDataLength){c.failed=true;++c.schemaErrors;return;}
        Event event{time,Read(e,schema.sizeOffset,schema.sizeWidth),op};
        if(c.shared && Read(e,schema.heapOffset,schema.heapWidth)==c.shared->heap) {
            for(unsigned i=0;i<2;++i)if(c.shared->sentinelBegin[i] && time>=c.shared->sentinelBegin[i] && (!c.shared->sentinelEnd[i] || time<=c.shared->sentinelEnd[i])) {
                Add(c.sentinels[i],event);
                if(op==0x21){if(event.size==123457)++c.sentinelExplicit[i];else if(event.size==234569)++c.sentinelBacking[i];else ++c.sentinelUnexpected[i];}
                if(op==0x22 && event.size!=234569)++c.sentinelUnexpected[i];
            }
        }
        if(!Associate(event)) {
            if(c.pendingCount==Consumer::PendingCapacity)c.failed=true;
            else {c.pending[c.pendingCount++]=event;if(c.pendingCount>c.pendingHighWater)c.pendingHighWater=c.pendingCount;}
        }
    }
    inline bool SentinelComplete(unsigned i)
    {
        const auto& c=*consumer;const auto& s=c.sentinels[i];
        // All 64 public calls remain mandatory. Relocating HeapReAlloc emits
        // backing alloc/free events; in-place HeapReAlloc does not.
        return c.sentinelExplicit[i]==64 && c.sentinelBacking[i]<=64 && !c.sentinelUnexpected[i] &&
            s.allocations==64+c.sentinelBacking[i] && s.bytes==64ull*123457+c.sentinelBacking[i]*234569ull &&
            s.reallocations==64 && s.reallocBytes==64ull*234569 && s.frees==64+c.sentinelBacking[i];
    }
    ULONG WINAPI Buffer(EVENT_TRACE_LOGFILEW* log)
    {
        auto& c=*consumer;++c.buffers;if(log->EventsLost>c.lost)c.lost=log->EventsLost;Drain();
        if(c.shared)for(unsigned i=0;i<2;++i)if(c.shared->sentinelEnd[i]) {
            if(SentinelComplete(i))
                InterlockedExchange(&c.shared->sentinelPass,LONG(i+1));
        }
        return TRUE;
    }
}
int wmain(int argc,wchar_t** argv)
{
    using namespace Phase24NativeObservation;
    if(argc!=5)return 2;auto state=std::make_unique<Consumer>();consumer=state.get();state->pid=wcstoul(argv[2],nullptr,10);
    state->offline=!wcscmp(argv[1],L"offline");HANDLE mapping{};
    if(!state->offline) {
        wchar_t name[96];swprintf_s(name,L"Local\\GEnginePhase24Frames-%u",state->pid);
        const auto start=GetTickCount64();while(!(mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name)) && GetTickCount64()-start<20000)Sleep(10);
        if(!mapping)return 3;state->shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!state->shared)return 4;
    }
    EVENT_TRACE_LOGFILEW log{};log.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD|PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    if(state->offline)log.LogFileName=argv[3];else{log.LoggerName=argv[3];log.ProcessTraceMode|=PROCESS_TRACE_MODE_REAL_TIME;}
    log.EventRecordCallback=EventRecord;log.BufferCallback=Buffer;
    auto trace=OpenTraceW(&log);if(trace==INVALID_PROCESSTRACE_HANDLE)return 5;
    if(state->shared)InterlockedExchange(&state->shared->ready,1);
    const auto code=ProcessTrace(&trace,1,nullptr,nullptr);CloseTrace(trace);Drain();
    FILE* file{};wchar_t path[1024];swprintf_s(path,L"%s/native-frames.csv",argv[4]);_wfopen_s(&file,path,L"wb");if(!file)return 6;
    fprintf(file,"frame,allocations,bytes,reallocations,realloc_bytes,frees\n");
    const unsigned n=state->shared?InterlockedCompareExchange(&state->shared->published,0,0):0;
    for(unsigned i=0;i<n;++i){const auto& c=state->rows[i];fprintf(file,"%u,%llu,%llu,%llu,%llu,%llu\n",i,c.allocations,c.bytes,c.reallocations,c.reallocBytes,c.frees);}fclose(file);
    swprintf_s(path,L"%s/native-result.txt",argv[4]);_wfopen_s(&file,path,L"wb");if(!file)return 7;
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
    FILETIME created{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user);
    const auto ticks=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    fprintf(file,"process_trace=%lu\nfailed=%d\nevents=%llu\nfirst_qpc=%llu\nlast_qpc=%llu\nlost=%llu\nbuffers=%llu\nschema_errors=%llu\nforeign=%llu\npending=%u\npending_high_water=%u\nconsumer_preallocated_bytes=%zu\nconsumer_private_bytes=%zu\nconsumer_cpu_ns=%llu\nframes=%u\nsentinel_pass=%ld\n",code,state->failed,state->events,state->first,state->last,state->lost,state->buffers,state->schemaErrors,state->foreign,state->pendingCount,state->pendingHighWater,sizeof(Consumer)+sizeof(Event)*Consumer::PendingCapacity,memory.PrivateUsage,(ticks(kernel)+ticks(user))*100,n,state->shared?state->shared->sentinelPass:0);
    for(auto op:{0x21,0x22,0x24})fprintf(file,"schema_%x=%ls\n",op,state->schemas[op].fields);
    fprintf(file,"gap_allocations=%llu\ngap_bytes=%llu\noutside_allocations=%llu\noutside_bytes=%llu\n",state->gaps.allocations,state->gaps.bytes,state->outside.allocations,state->outside.bytes);
    for(unsigned i=0;i<2;++i){const auto& c=state->sentinels[i];fprintf(file,"sentinel_%u=%llu,%llu,%llu,%llu,%llu\nsentinel_calls_%u=%u,%u,%u\n",i,c.allocations,c.bytes,c.reallocations,c.reallocBytes,c.frees,i,state->sentinelExplicit[i],state->sentinelBacking[i],state->sentinelUnexpected[i]);}fclose(file);
    if(state->shared)UnmapViewOfFile(state->shared);if(mapping)CloseHandle(mapping);
    return code || state->failed || state->lost || state->pendingCount?1:0;
}
#else
// Compiled only into the owner-authorized Phase24 qualification products.
#if !defined(GENGINE_PHASE24_OUTLINE_QUALIFICATION) || !defined(GENGINE_PHASE24_ISOLATED_BUILD)
#error SelectionOutlineChecks belongs only to the explicit isolated Phase24 qualification build.
#endif
#include "Renderer/FrameScheduler.h"
#include "Renderer/PassTiming.h"
#include "Core/RenderTarget.h"
#include "Core/Window.h"
#include "../../external/sdl2/include/sdl2/SDL.h"
#include "../../external/glad/include/glad/glad.h"
#include "PhysicsSceneTransformChecks.h"
#include <stb_image/stb_image_write.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>
#include <atomic>
#include <new.h>
#include <malloc.h>
#include <windows.h>
#include <psapi.h>
#include "Managers/WindowManager.h"
#include <imgui/imgui.h>

// These replacements are linked only into this isolated qualification EXE.
// DLL-local C++ allocators are observed by the separate PID-specific native
// heap trace, not misrepresented as calls to this EXE's operator new.
namespace Phase24AllocationObservation
{
    inline std::atomic<std::uint64_t> count{},bytes{};
    inline bool minimal=std::getenv("GENGINE_PHASE24_OBSERVER_MINIMAL")!=nullptr;
    inline void Record(std::size_t size) noexcept
    { if(!minimal){count.fetch_add(1,std::memory_order_relaxed);bytes.fetch_add(size,std::memory_order_relaxed);} }
    inline void* Allocate(std::size_t size,std::size_t alignment=0)
    {
        const auto requested=size;size=(std::max)(size,std::size_t{1});
        for(;;) {
            void* result=alignment?_aligned_malloc(size,alignment):std::malloc(size);
            if(result) {Record(requested);return result;}
            if(!_callnewh(size)) throw std::bad_alloc();
        }
    }
}
void* operator new(std::size_t size) {return Phase24AllocationObservation::Allocate(size);}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete[](void* value) noexcept {::operator delete(value);}
void operator delete(void* value,std::size_t) noexcept {::operator delete(value);}
void operator delete[](void* value,std::size_t) noexcept {::operator delete(value);}
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {try{return ::operator new(size);}catch(...){return nullptr;}}
void* operator new[](std::size_t size,const std::nothrow_t&) noexcept {try{return ::operator new[](size);}catch(...){return nullptr;}}
void operator delete(void* value,const std::nothrow_t&) noexcept {::operator delete(value);}
void operator delete[](void* value,const std::nothrow_t&) noexcept {::operator delete[](value);}
void* operator new(std::size_t size,std::align_val_t alignment) {return Phase24AllocationObservation::Allocate(size,std::size_t(alignment));}
void* operator new[](std::size_t size,std::align_val_t alignment) {return ::operator new(size,alignment);}
void operator delete(void* value,std::align_val_t) noexcept {_aligned_free(value);}
void operator delete[](void* value,std::align_val_t alignment) noexcept {::operator delete(value,alignment);}
void operator delete(void* value,std::size_t,std::align_val_t alignment) noexcept {::operator delete(value,alignment);}
void operator delete[](void* value,std::size_t,std::align_val_t alignment) noexcept {::operator delete(value,alignment);}
void* operator new(std::size_t size,std::align_val_t alignment,const std::nothrow_t&) noexcept {try{return ::operator new(size,alignment);}catch(...){return nullptr;}}
void* operator new[](std::size_t size,std::align_val_t alignment,const std::nothrow_t&) noexcept {try{return ::operator new[](size,alignment);}catch(...){return nullptr;}}
void operator delete(void* value,std::align_val_t alignment,const std::nothrow_t&) noexcept {::operator delete(value,alignment);}
void operator delete[](void* value,std::align_val_t alignment,const std::nothrow_t&) noexcept {::operator delete(value,alignment);}

// The backend exports its loader table. Access its object representation with
// memcpy, using offsets verified against the pinned loader source at build prep.
// No backend source, loader behavior or GL command is replaced or skipped.
extern "C" { union GL3WProcs; extern GL3WProcs imgl3wProcs; }

namespace GEngine::Phase24Qualification
{
    inline std::uint64_t Now() noexcept
    { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    constexpr std::size_t Capacity=32768, Passes=32;
    struct Row
    {
        std::uint64_t start{},work{},pacing{},interval{},preparation{},extraction{};
        std::uint64_t cpu[Passes]{},gpu[Passes]{};
        std::uint32_t pending{},available{},complete{};
        std::size_t draws{},passes{},outlineDraws{},outlinePasses{},scratch{};
        std::uint64_t qpcBegin{},qpcEnd{},newCount{},newBytes{},privateBytes{},workingSet{};
        std::uint64_t engineApiDraws{},uiApiDraws{},elements{},reads{},syncs{};
        std::uint64_t newCountBegin{},newBytesBegin{};
        float selectedX{},selectedY{},selectedZ{};
        float visibilityMargin{},rotationAngle{};
        unsigned occlusionCandidates{},viewportWidth{},viewportHeight{};
        std::size_t frameItems{},selectedItems{},windowCount{},uiViewports{};
        PassViewport region;
        bool successful=false,submitted=false,measured=false;
    };
    struct Probe
    {
        std::unique_ptr<Row[]> rows;
        std::array<std::size_t,Capacity> gpuToCpu{};
        std::size_t count{},current{},measured{},firstMeasured{},lastMeasured{};
        std::uint64_t pacingStart{},start{},measurementEnd{};
        std::filesystem::path output;
        _Entity target,second,occluder;
        EntityRenderId identity;
        MaterialHandle variantMaterial;
        glm::mat4 observed{1.f};
        glm::mat4 observedView{1.f},observedProjection{1.f};
        std::size_t observedDraws{},functionalFrames{},checks{};
        bool functional=true,selected=false,enabled=false,failed=false,done=false,written=false;
        const char* failure="";
        const char* zoomCase=nullptr;
        float zoomDistance{};
        EntityRenderId skyIdentity;
        std::uint64_t qpcFrequency{};
        unsigned width{},height{};
        std::vector<std::byte> pixels;
        void Fail(const char* reason) noexcept { if(!failed) {failed=true;failure=reason;} }
        bool Require(bool value,const char* reason) noexcept { if(!value) Fail(reason);else ++checks;return value; }
    };
    inline Probe probe;
    namespace Native
    {
        inline HANDLE mapping{},heap{};
        inline Phase24NativeObservation::Shared* shared{};
        inline bool Sentinel(unsigned index)
        {
            if(!shared)return false;
            LARGE_INTEGER time{};QueryPerformanceCounter(&time);shared->sentinelBegin[index]=time.QuadPart;
            void* blocks[64]{};
            for(auto& block:blocks) {block=HeapAlloc(heap,0,123457);if(!block)return false;}
            for(auto& block:blocks) {auto* next=HeapReAlloc(heap,0,block,234569);if(!next)return false;block=next;}
            for(auto* block:blocks)HeapFree(heap,0,block);
            QueryPerformanceCounter(&time);shared->sentinelEnd[index]=time.QuadPart;
            InterlockedExchange(&shared->sentinelPhase,LONG(index+1));
            // Startup/post-measurement only. No wait in a measured frame.
            const auto start=GetTickCount64();
            while(InterlockedCompareExchange(&shared->sentinelPass,0,0)<LONG(index+1) && GetTickCount64()-start<10000)Sleep(10);
            return shared->sentinelPass>=LONG(index+1);
        }
        inline bool Arm()
        {
            if(probe.functional || Phase24AllocationObservation::minimal)return true;
            wchar_t name[96];swprintf_s(name,L"Local\\GEnginePhase24Frames-%u",GetCurrentProcessId());
            mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Phase24NativeObservation::Shared),name);
            if(!mapping || GetLastError()==ERROR_ALREADY_EXISTS)return false;
            shared=static_cast<Phase24NativeObservation::Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*shared)));
            if(!shared)return false;std::memset(shared,0,sizeof(*shared));
            heap=HeapCreate(0,0,0);if(!heap)return false;shared->heap=reinterpret_cast<std::uint64_t>(heap);
            const auto start=GetTickCount64();while(!InterlockedCompareExchange(&shared->ready,0,0) && GetTickCount64()-start<20000)Sleep(10);
            if(!shared->ready || !Sentinel(0))return false;
            probe.start=Now();return true;
        }
        inline void Publish(const Row& row)
        {
            if(!shared)return;shared->frames[probe.current]={row.qpcBegin,row.qpcEnd};
            InterlockedExchange(&shared->published,LONG(probe.current+1));
        }
        inline void Finish()
        {
            if(!shared)return;
            InterlockedExchange(&shared->finished,1);
            if(!Sentinel(1))probe.Fail("native heap closing sentinel incomplete");
            HeapDestroy(heap);heap=nullptr;UnmapViewOfFile(shared);shared=nullptr;CloseHandle(mapping);mapping=nullptr;
        }
    }
    namespace Performance
    {
        inline bool installed{};
        inline PFNGLDRAWARRAYSPROC arrays{};
        inline PFNGLDRAWARRAYSINSTANCEDPROC arraysInstanced{};
        inline PFNGLDRAWELEMENTSPROC elements{},uiElements{};
        inline PFNGLDRAWELEMENTSINSTANCEDPROC elementsInstanced{};
        inline PFNGLDRAWELEMENTSBASEVERTEXPROC uiBaseVertex{};
        inline PFNGLREADPIXELSPROC readPixels{};
        inline PFNGLGETTEXTUREIMAGEPROC textureImage{};
        inline PFNGLGETNAMEDBUFFERSUBDATAPROC bufferRead{};
        inline PFNGLFLUSHPROC flush{},uiFlush{};
        inline PFNGLFINISHPROC finish{};
        inline PFNGLCLIENTWAITSYNCPROC clientWait{};
        inline PFNGLWAITSYNCPROC wait{};
        inline Row* Current() noexcept {return probe.enabled && !probe.done && probe.count?&probe.rows[probe.current]:nullptr;}
        inline void Draw(GLsizei count,GLsizei instances,bool ui) noexcept
        {if(auto* r=Current()) {++(ui?r->uiApiDraws:r->engineApiDraws);r->elements+=std::uint64_t(count)*instances;}}
        void APIENTRY Arrays(GLenum mode,GLint first,GLsizei count) {Draw(count,1,false);arrays(mode,first,count);}
        void APIENTRY ArraysInstanced(GLenum mode,GLint first,GLsizei count,GLsizei instances) {Draw(count,instances,false);arraysInstanced(mode,first,count,instances);}
        void APIENTRY Elements(GLenum mode,GLsizei count,GLenum type,const void* indices) {Draw(count,1,false);elements(mode,count,type,indices);}
        void APIENTRY ElementsInstanced(GLenum mode,GLsizei count,GLenum type,const void* indices,GLsizei instances) {Draw(count,instances,false);elementsInstanced(mode,count,type,indices,instances);}
        void APIENTRY UiElements(GLenum mode,GLsizei count,GLenum type,const void* indices) {Draw(count,1,true);uiElements(mode,count,type,indices);}
        void APIENTRY UiBaseVertex(GLenum mode,GLsizei count,GLenum type,const void* indices,GLint base) {Draw(count,1,true);uiBaseVertex(mode,count,type,indices,base);}
        void APIENTRY ReadPixels(GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,void* data) {if(auto* r=Current())++r->reads;readPixels(x,y,w,h,format,type,data);}
        void APIENTRY TextureImage(GLuint texture,GLint level,GLenum format,GLenum type,GLsizei size,void* data) {if(auto* r=Current())++r->reads;textureImage(texture,level,format,type,size,data);}
        void APIENTRY BufferRead(GLuint buffer,GLintptr offset,GLsizeiptr size,void* data) {if(auto* r=Current())++r->reads;bufferRead(buffer,offset,size,data);}
        void APIENTRY Flush() {if(auto* r=Current())++r->syncs;flush();}
        void APIENTRY UiFlush() {if(auto* r=Current())++r->syncs;uiFlush();}
        void APIENTRY Finish() {if(auto* r=Current())++r->syncs;finish();}
        GLenum APIENTRY ClientWait(GLsync sync,GLbitfield flags,GLuint64 timeout) {if(auto* r=Current())++r->syncs;return clientWait(sync,flags,timeout);}
        void APIENTRY Wait(GLsync sync,GLbitfield flags,GLuint64 timeout) {if(auto* r=Current())++r->syncs;wait(sync,flags,timeout);}
        template<class T> void UiHook(std::size_t index,T& original,T replacement)
        {
            static_assert(sizeof(T)==sizeof(void(*)()));
            auto* bytes=reinterpret_cast<unsigned char*>(&imgl3wProcs)+index*sizeof(T);
            std::memcpy(&original,bytes,sizeof(T));std::memcpy(bytes,&replacement,sizeof(T));
        }
        inline void Install()
        {
            if(installed || probe.functional || Phase24AllocationObservation::minimal) return;
            arrays=glad_glDrawArrays;glad_glDrawArrays=Arrays;
            arraysInstanced=glad_glDrawArraysInstanced;glad_glDrawArraysInstanced=ArraysInstanced;
            elements=glad_glDrawElements;glad_glDrawElements=Elements;
            elementsInstanced=glad_glDrawElementsInstanced;glad_glDrawElementsInstanced=ElementsInstanced;
            readPixels=glad_glReadPixels;glad_glReadPixels=ReadPixels;
            textureImage=glad_glGetTextureImage;glad_glGetTextureImage=TextureImage;
            bufferRead=glad_glGetNamedBufferSubData;glad_glGetNamedBufferSubData=BufferRead;
            flush=glad_glFlush;glad_glFlush=Flush;finish=glad_glFinish;glad_glFinish=Finish;
            clientWait=glad_glClientWaitSync;glad_glClientWaitSync=ClientWait;wait=glad_glWaitSync;glad_glWaitSync=Wait;
            UiHook(24,uiElements,UiElements);UiHook(25,uiBaseVertex,UiBaseVertex);UiHook(28,uiFlush,UiFlush);
            LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);probe.qpcFrequency=frequency.QuadPart;
            installed=true;
        }
        inline void End(Row& row) noexcept
        {
            if(probe.functional)return;
            row.newCount=Phase24AllocationObservation::count.load(std::memory_order_relaxed)-row.newCountBegin;
            row.newBytes=Phase24AllocationObservation::bytes.load(std::memory_order_relaxed)-row.newBytesBegin;
            row.windowCount=BaseApp::GetWindowManager()->GetWindows().size();row.uiViewports=ImGui::GetPlatformIO().Viewports.Size;
            if(row.windowCount!=1 || row.uiViewports!=1) probe.Fail("incomplete multi-context GPU timing coverage");
            PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
            if(!K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))) probe.Fail("process memory observation");
            row.privateBytes=memory.PrivateUsage;row.workingSet=memory.WorkingSetSize;
            LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);row.qpcEnd=qpc.QuadPart;
            Native::Publish(row);
            if(row.measured && (row.reads || row.syncs)) probe.Fail("readback or synchronization in timing window");
            if(!Phase24AllocationObservation::minimal && row.submitted && row.engineApiDraws!=row.draws) probe.Fail("incomplete actual draw coverage");
        }
    }
    // Diagnostic-only observation of the existing C08 commands. These hooks do
    // not replace any draw, depth test, resolve or blend. Readbacks and temporary
    // resources are restricted to an explicitly requested functional run.
    namespace Diagnostic
    {
        inline PFNGLBLITNAMEDFRAMEBUFFERPROC blit{};
        inline PFNGLDRAWARRAYSPROC drawArrays{};
        inline PFNGLDRAWELEMENTSPROC drawElements{};
        inline PFNGLSHADERSOURCEPROC shaderSource{};
        inline bool installed{}, captured{}, composited{},sharedVisibility{};
        inline GLuint sceneFbo{};
        inline GLuint rasterFbo{},rasterMask{},rasterDepth{};
        inline GLint origin[2]{}, size[2]{};
        inline GLuint Attachment(GLuint fbo, GLenum attachment)
        { GLint name{};glGetNamedFramebufferAttachmentParameteriv(fbo,attachment,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&name);return name; }
        inline void Binary(const char* name,const void* bytes,std::size_t count)
        {
            FILE* file{};const auto path=(probe.output/name).string();
            if(fopen_s(&file,path.c_str(),"wb") || !file) {probe.Fail("diagnostic output");return;}
            if(std::fwrite(bytes,1,count,file)!=count) probe.Fail("diagnostic write");std::fclose(file);
        }
        inline void Program(GLuint program,const char* name)
        {
            GLuint shaders[8]{};GLsizei count{};glGetAttachedShaders(program,8,&count,shaders);
            std::string source;
            for(int i=0;i<count;++i) { GLint length{};glGetShaderiv(shaders[i],GL_SHADER_SOURCE_LENGTH,&length);
                std::string text(length,'\0');glGetShaderSource(shaders[i],length,nullptr,text.data());source+=text+"\n"; }
            Binary(name,source.data(),source.size());
        }
        inline void Samples(GLuint mask,GLuint depth,GLuint scene,GLint w,GLint h,GLint samples,const char* name="diagnostic-ms-coverage-depth.f32",bool shared=false,bool color=false)
        {
            // Native read-only fetch preserves per-sample coverage/depth instead
            // of using another color resolve to infer multisample contents.
            GLint previousProgram{},previousSsbo{},previousIndexed{},active{},textures[3]{},samplers[3]{};
            GLint64 start{},length{};
            glGetIntegerv(GL_CURRENT_PROGRAM,&previousProgram);glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&previousSsbo);
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,7,&previousIndexed);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,7,&start);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,7,&length);
            glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
            for(int i=0;i<3;++i) {glActiveTexture(GL_TEXTURE29+i);glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE,&textures[i]);glGetIntegeri_v(GL_SAMPLER_BINDING,29+i,&samplers[i]);}
            std::string source=R"(#version 450 core
layout(local_size_x=8,local_size_y=8) in;
layout(binding=29) uniform sampler2DMS selected;
layout(binding=30) uniform sampler2DMS depth;
layout(binding=31) uniform sampler2DMS scene;
layout(std430,binding=7) buffer Result { vec4 marker; vec4 values[]; };
uniform ivec2 extent,origin;uniform int samples,rowOffset;
void main(){ivec2 local=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(local,extent)))return;
ivec2 p=local+ivec2(0,rowOffset);
if(all(equal(local,ivec2(0)))) marker=vec4(rowOffset,extent,samples);
for(int s=0;s<samples;++s) values[(local.y*extent.x+local.x)*samples+s]=vec4(texelFetch(selected,p,s).rg,texelFetch(depth,p,s).r,texelFetch(scene,p+origin,s).r);})";
            if(shared) {
                source.replace(source.find("sampler2DMS selected"),std::strlen("sampler2DMS selected"),"usampler2DMS selected");
                const auto at=source.find("for(int s=0;s<samples;++s)");
                source.replace(at,source.size()-at,R"(for(int s=0;s<samples;++s) {
float selectedValue=float(texelFetch(selected,p,s).r&1u);float d=texelFetch(depth,p,s).r;
values[(local.y*extent.x+local.x)*samples+s]=vec4(selectedValue,selectedValue*d,d,selectedValue>0?d:1.0);}})");
            }
            if(color) {
                const auto at=source.find("for(int s=0;s<samples;++s)");
                source.replace(at,source.size()-at,"for(int s=0;s<samples;++s) values[(local.y*extent.x+local.x)*samples+s]=texelFetch(selected,p,s);}");
            }
            const auto* sourceText=source.c_str();
            GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&sourceText,nullptr);glCompileShader(shader);
            GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);
            GLint ok{};glGetProgramiv(program,GL_LINK_STATUS,&ok);probe.Require(ok==GL_TRUE,"diagnostic compute link");
            std::vector<float> values(std::size_t(w)*h*samples*4);
            // Full-viewport zoom captures can exceed a driver's SSBO block limit.
            // Read every sample in bounded row tiles; never truncate the image.
            const auto tileRows=(std::min)(64,h);
            const auto rowValues=std::size_t(w)*samples*4;
            GLuint buffer{};glCreateBuffers(1,&buffer);
            glUseProgram(program);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,buffer);
            const GLuint names[]{mask,depth,scene};
            for(int i=0;i<3;++i) {glActiveTexture(GL_TEXTURE29+i);glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,names[i]);glBindSampler(29+i,0);}
            glUniform2i(glGetUniformLocation(program,"origin"),0,0);glUniform1i(glGetUniformLocation(program,"samples"),samples);
            std::string tileState;
            Program(program,"diagnostic-sample-readback-shader.glsl");
            for(int row=0;row<h;row+=tileRows) {
                const auto count=(std::min)(tileRows,h-row);
                // Each tile receives a new data store. Retain a GPU-written
                // coordinate marker to reject stale or misassociated readback.
                glNamedBufferData(buffer,(rowValues*tileRows+4)*sizeof(float),nullptr,GL_STREAM_READ);
                const auto location=glGetUniformLocation(program,"rowOffset");
                glProgramUniform2i(program,glGetUniformLocation(program,"extent"),w,count);glProgramUniform1i(program,location,row);
                GLint actual{},current{};glGetUniformiv(program,location,&actual);glGetIntegerv(GL_CURRENT_PROGRAM,&current);
                probe.Require(actual==row && GLuint(current)==program,"sample tile offset/program association");
                glDispatchCompute((w+7)/8,(count+7)/8,1);glMemoryBarrier(GL_ALL_BARRIER_BITS);
                GLfloat marker[4]{};glGetNamedBufferSubData(buffer,0,sizeof(marker),marker);
                glGetNamedBufferSubData(buffer,sizeof(marker),rowValues*count*sizeof(float),values.data()+rowValues*row);
                probe.Require(marker[0]==row && marker[1]==w && marker[2]==count && marker[3]==samples,"GPU sample tile marker association");
                tileState+=std::format("row={} count={} uniform={} location={} program={} current={} marker={},{},{},{} error={}\n",row,count,actual,location,program,current,marker[0],marker[1],marker[2],marker[3],glGetError());
            }
            Binary((std::string(name)+".tiles.txt").c_str(),tileState.data(),tileState.size());
            Binary(name,values.data(),values.size()*sizeof(float));
            for(int i=0;i<3;++i) {glActiveTexture(GL_TEXTURE29+i);glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,textures[i]);glBindSampler(29+i,samplers[i]);}
            glActiveTexture(active);glUseProgram(previousProgram);
            if(previousIndexed && length>0) glBindBufferRange(GL_SHADER_STORAGE_BUFFER,7,previousIndexed,start,length);
            else glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,previousIndexed);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,previousSsbo);glDeleteBuffers(1,&buffer);glDeleteProgram(program);glDeleteShader(shader);
        }
        inline void APIENTRY Blit(GLuint read,GLuint draw,GLint sx0,GLint sy0,GLint sx1,GLint sy1,GLint dx0,GLint dy0,GLint dx1,GLint dy1,GLbitfield bits,GLenum filter)
        {
            if(bits==GL_DEPTH_BUFFER_BIT && probe.functionalFrames==61 && std::getenv("GENGINE_PHASE24_OUTLINE_CLEAR_DEPTH_TEST")) {
                GLboolean write{};glGetBooleanv(GL_DEPTH_WRITEMASK,&write);const bool scissor=glIsEnabled(GL_SCISSOR_TEST);
                glDepthMask(GL_TRUE);glDisable(GL_SCISSOR_TEST);constexpr GLfloat clear=1;
                glClearNamedFramebufferfv(draw,GL_DEPTH,0,&clear);glDepthMask(write);if(scissor)glEnable(GL_SCISSOR_TEST);
                constexpr char proof[]="destination private depth cleared over full capacity before original depth blit\n";Binary("diagnostic-clear-depth-applied.txt",proof,sizeof(proof)-1);
            }
            if(bits==GL_DEPTH_BUFFER_BIT && probe.functionalFrames==61 && std::getenv("GENGINE_PHASE24_OUTLINE_RAW_COPY_TEST")) {
                GLint type{};glGetNamedFramebufferAttachmentParameteriv(read,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&type);
                glCopyImageSubData(Attachment(read,GL_DEPTH_ATTACHMENT),type==GL_RENDERBUFFER?GL_RENDERBUFFER:GL_TEXTURE_2D_MULTISAMPLE,0,sx0,sy0,0,
                    Attachment(draw,GL_DEPTH_ATTACHMENT),GL_TEXTURE_2D_MULTISAMPLE,0,dx0,dy0,0,sx1-sx0,sy1-sy0,1);
                constexpr char proof[]="original depth framebuffer blit replaced by raw image copy; same region, formats and samples\n";Binary("diagnostic-raw-copy-applied.txt",proof,sizeof(proof)-1);
            } else blit(read,draw,sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1,bits,filter);
            if(captured || probe.functionalFrames!=61) return;
            if(bits&GL_DEPTH_BUFFER_BIT) {
                sceneFbo=read;origin[0]=sx0;origin[1]=sy0;size[0]=sx1-sx0;size[1]=sy1-sy0;
                sharedVisibility=(bits&GL_STENCIL_BUFFER_BIT)!=0;return;
            }
            if(bits!=GL_COLOR_BUFFER_BIT || !sceneFbo) return;
            const auto resolved=Attachment(draw,GL_COLOR_ATTACHMENT0);GLint format{};
            glGetTextureLevelParameteriv(resolved,0,GL_TEXTURE_INTERNAL_FORMAT,&format);if(format!=GL_RG32F) return;
            captured=true;
            const auto mask=Attachment(read,GL_COLOR_ATTACHMENT0),depth=Attachment(read,GL_DEPTH_ATTACHMENT);
            GLint w{},h{},samples{},depthFormat{},program{};
            glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_WIDTH,&w);glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_HEIGHT,&h);
            glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_SAMPLES,&samples);glGetTextureLevelParameteriv(depth,0,GL_TEXTURE_INTERNAL_FORMAT,&depthFormat);
            glGetIntegerv(GL_CURRENT_PROGRAM,&program);Program(program,"diagnostic-mask-shader.glsl");
            std::vector<float> values(std::size_t(w)*h*2);glGetTextureImage(resolved,0,GL_RG,GL_FLOAT,GLsizei(values.size()*sizeof(float)),values.data());
            Binary("diagnostic-resolved-mask.f32",values.data(),values.size()*sizeof(float));
            Samples(mask,depth,rasterDepth,size[0],size[1],samples);
            Samples(rasterMask,rasterDepth,rasterDepth,size[0],size[1],samples,"diagnostic-self-depth.f32");
            glDeleteFramebuffers(1,&rasterFbo);glDeleteTextures(1,&rasterMask);glDeleteTextures(1,&rasterDepth);
            GLint sceneDepthType{},physicalWidth{},physicalHeight{},stencilBits{};
            glGetNamedFramebufferAttachmentParameteriv(sceneFbo,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&sceneDepthType);
            glGetNamedFramebufferAttachmentParameteriv(sceneFbo,GL_STENCIL_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,&stencilBits);
            if(sceneDepthType==GL_RENDERBUFFER) {
                glGetNamedRenderbufferParameteriv(Attachment(sceneFbo,GL_DEPTH_ATTACHMENT),GL_RENDERBUFFER_WIDTH,&physicalWidth);
                glGetNamedRenderbufferParameteriv(Attachment(sceneFbo,GL_DEPTH_ATTACHMENT),GL_RENDERBUFFER_HEIGHT,&physicalHeight);
            }
            auto text=std::format("width={}\nheight={}\nsamples={}\nmask_format={}\ndepth_format={}\nresolved_format={}\nregion_x={}\nregion_y={}\nregion_width={}\nregion_height={}\nphysical_width={}\nphysical_height={}\nstencil_bits={}\ncpu_row={}\nfunctional_before={}\ngl_error={}\n",w,h,samples,GL_RG32F,depthFormat,format,origin[0],origin[1],size[0],size[1],physicalWidth,physicalHeight,stencilBits,probe.current,probe.functionalFrames,glGetError());
            for(unsigned i=0;i<16;++i) text+=std::format("view{}={}\nprojection{}={}\nmodel{}={}\n",i,glm::value_ptr(probe.observedView)[i],i,glm::value_ptr(probe.observedProjection)[i],i,glm::value_ptr(probe.observed)[i]);
            Binary("diagnostic-attachments.txt",text.data(),text.size());
        }
        template<class Draw> inline void Raster(Draw&& draw)
        {
            if(captured || rasterFbo || probe.functionalFrames!=61 || !sceneFbo) return;
            GLint fbo{};glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);if(!fbo || GLuint(fbo)==sceneFbo) return;
            const auto mask=Attachment(fbo,GL_COLOR_ATTACHMENT0);GLint format{};
            glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_INTERNAL_FORMAT,&format);if(format!=GL_RG32F) return;
            GLint w{},h{},samples{},depthFunc{};GLboolean depthWrite{};
            glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_WIDTH,&w);glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_HEIGHT,&h);
            glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_SAMPLES,&samples);glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);
            std::string state=std::format("depth_func={}\ndepth_write={}\n",depthFunc,int(depthWrite));
            for(auto fb:{sceneFbo,GLuint(fbo)}) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fb);
                GLint attachmentType{},attachmentName{};
                glGetNamedFramebufferAttachmentParameteriv(fb,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&attachmentType);
                glGetNamedFramebufferAttachmentParameteriv(fb,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&attachmentName);
                state+=std::format("fbo={} depth_type={} depth_name={}\n",fb,attachmentType,attachmentName);
                for(int s=0;s<samples;++s) {GLfloat xy[2]{};glGetMultisamplefv(GL_SAMPLE_POSITION,s,xy);state+=std::format("sample{}={},{}\n",s,xy[0],xy[1]);}
            }
            Binary("diagnostic-mask-state.txt",state.data(),state.size());
            glCreateFramebuffers(1,&rasterFbo);glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE,1,&rasterMask);glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE,1,&rasterDepth);
            glTextureStorage2DMultisample(rasterMask,samples,GL_RG32F,w,h,GL_TRUE);glTextureStorage2DMultisample(rasterDepth,samples,GL_DEPTH24_STENCIL8,w,h,GL_TRUE);
            glNamedFramebufferTexture(rasterFbo,GL_COLOR_ATTACHMENT0,rasterMask,0);glNamedFramebufferTexture(rasterFbo,GL_DEPTH_STENCIL_ATTACHMENT,rasterDepth,0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,rasterFbo);glDepthFunc(GL_ALWAYS);glDepthMask(GL_TRUE);
            constexpr GLfloat clear[]{0,0,0,0};constexpr GLfloat clearDepth=1;
            glClearNamedFramebufferfv(rasterFbo,GL_COLOR,0,clear);glClearNamedFramebufferfv(rasterFbo,GL_DEPTH,0,&clearDepth);
            draw();
            if(std::getenv("GENGINE_PHASE24_OUTLINE_SELF_DEPTH_TEST")) {
                glClearNamedFramebufferfv(rasterFbo,GL_COLOR,0,clear);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);draw();
                constexpr char proof[]="same mask shader/viewport/FBO: first ALWAYS+depth write, then clear color and LEQUAL+no depth write\n";Binary("diagnostic-self-depth-applied.txt",proof,sizeof(proof)-1);
            }
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo);glDepthFunc(depthFunc);glDepthMask(depthWrite);
        }
        inline void APIENTRY DrawElements(GLenum mode,GLsizei count,GLenum type,const void* indices)
        {
            GLint fbo{},format{};bool bias=false,write=false,sample=false;GLfloat factor{},units{},minimum{};
            if((std::getenv("GENGINE_PHASE24_OUTLINE_BIAS_TEST") || std::getenv("GENGINE_PHASE24_OUTLINE_DEPTH_WRITE_TEST") || std::getenv("GENGINE_PHASE24_OUTLINE_MIN_SAMPLE_TEST")) && sceneFbo && probe.functionalFrames==61) {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
                if(fbo && GLuint(fbo)!=sceneFbo) glGetTextureLevelParameteriv(Attachment(fbo,GL_COLOR_ATTACHMENT0),0,GL_TEXTURE_INTERNAL_FORMAT,&format);
                bias=format==GL_RG32F && std::getenv("GENGINE_PHASE24_OUTLINE_BIAS_TEST");
                write=format==GL_RG32F && std::getenv("GENGINE_PHASE24_OUTLINE_DEPTH_WRITE_TEST");
                sample=format==GL_RG32F && std::getenv("GENGINE_PHASE24_OUTLINE_MIN_SAMPLE_TEST");
                if(bias) {glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&factor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&units);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(0,-1);}
                if(write) {glDepthMask(GL_TRUE);constexpr char proof[]="indexed selected mask draw; private depth write enabled\n";Binary("diagnostic-depth-write-applied.txt",proof,sizeof(proof)-1);}
                if(sample) {glGetFloatv(GL_MIN_SAMPLE_SHADING_VALUE,&minimum);glEnable(GL_SAMPLE_SHADING);glMinSampleShading(1);
                    constexpr char proof[]="indexed selected mask draw; sample shading enabled minimum=1\n";Binary("diagnostic-min-sample-applied.txt",proof,sizeof(proof)-1);}
            }
            drawElements(mode,count,type,indices);
            if(bias) {glDisable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(factor,units);}
            if(write) glDepthMask(GL_FALSE);
            if(sample) {glDisable(GL_SAMPLE_SHADING);glMinSampleShading(minimum);}
            Raster([&]{drawElements(mode,count,type,indices);});
        }
        inline void APIENTRY ShaderSource(GLuint shader,GLsizei count,const GLchar* const* strings,const GLint* lengths)
        {
            std::string source;
            for(int i=0;i<count;++i) source.append(strings[i],lengths && lengths[i]>=0?std::size_t(lengths[i]):std::strlen(strings[i]));
            if(source.find("out vec2 selectedCoverage")!=std::string::npos) {
                Binary("diagnostic-original-mask-fragment.glsl",source.data(),source.size());
                if(std::getenv("GENGINE_PHASE24_OUTLINE_EARLY_TEST")) source.insert(source.find('\n')+1,"layout(early_fragment_tests) in;\n");
                if(std::getenv("GENGINE_PHASE24_OUTLINE_SAMPLE_TEST")) {
                    const auto at=source.find("selectedCoverage=vec2(1.0,gl_FragCoord.z)");
                    if(at!=std::string::npos) source.replace(at,std::strlen("selectedCoverage=vec2(1.0,gl_FragCoord.z)"),"selectedCoverage=vec2(gl_SampleID>=0?1.0:0.0,gl_FragCoord.z)");
                }
                Binary("diagnostic-executed-mask-fragment.glsl",source.data(),source.size());
                const auto* text=source.c_str();shaderSource(shader,1,&text,nullptr);return;
            }
            shaderSource(shader,count,strings,lengths);
        }
        inline void Color(const char* name)
        {
            GLuint texture{},fbo{};glCreateTextures(GL_TEXTURE_2D,1,&texture);glTextureStorage2D(texture,1,GL_RGBA8,size[0],size[1]);
            glCreateFramebuffers(1,&fbo);glNamedFramebufferTexture(fbo,GL_COLOR_ATTACHMENT0,texture,0);
            const bool scissor=glIsEnabled(GL_SCISSOR_TEST);glDisable(GL_SCISSOR_TEST);
            blit(sceneFbo,fbo,origin[0],origin[1],origin[0]+size[0],origin[1]+size[1],0,0,size[0],size[1],GL_COLOR_BUFFER_BIT,GL_NEAREST);
            if(std::getenv("GENGINE_PHASE24_SAMPLE_COLOR")) {
                GLint samples{};glGetNamedRenderbufferParameteriv(Attachment(sceneFbo,GL_DEPTH_ATTACHMENT),GL_RENDERBUFFER_SAMPLES,&samples);
                GLuint sampleTexture{},sampleFbo{};glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE,1,&sampleTexture);
                glTextureStorage2DMultisample(sampleTexture,samples,GL_RGBA8,size[0],size[1],GL_TRUE);
                glCreateFramebuffers(1,&sampleFbo);glNamedFramebufferTexture(sampleFbo,GL_COLOR_ATTACHMENT0,sampleTexture,0);
                probe.Require(glCheckNamedFramebufferStatus(sampleFbo,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"same-frame sample color copy target");
                blit(sceneFbo,sampleFbo,origin[0],origin[1],origin[0]+size[0],origin[1]+size[1],0,0,size[0],size[1],GL_COLOR_BUFFER_BIT,GL_NEAREST);
                Samples(sampleTexture,0,0,size[0],size[1],samples,(std::string(name)+".samples.f32").c_str(),false,true);
                glDeleteFramebuffers(1,&sampleFbo);glDeleteTextures(1,&sampleTexture);
            }
            if(scissor) glEnable(GL_SCISSOR_TEST);
            std::vector<unsigned char> values(std::size_t(size[0])*size[1]*4);glGetTextureImage(texture,0,GL_RGBA,GL_UNSIGNED_BYTE,GLsizei(values.size()),values.data());
            Binary(name,values.data(),values.size());glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
        }
        inline void APIENTRY DrawArrays(GLenum mode,GLint first,GLsizei count)
        {
            GLint program{};if((captured || sharedVisibility) && !composited) glGetIntegerv(GL_CURRENT_PROGRAM,&program);
            const bool sharedResolve=sharedVisibility && !captured && program && glGetUniformLocation(program,"selectedStencil")>=0;
            const bool composite=captured && program && glGetUniformLocation(program,"selectedMask")>=0;
            if(composite) {
                Program(program,"diagnostic-composite-shader.glsl");
                std::string state;
                for(auto key:{GL_BLEND,GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,GL_DEPTH_TEST,GL_DEPTH_FUNC,GL_DEPTH_WRITEMASK,GL_MULTISAMPLE,GL_SAMPLE_SHADING,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_MASK,GL_STENCIL_TEST,GL_STENCIL_FUNC,GL_STENCIL_REF,GL_STENCIL_VALUE_MASK,GL_STENCIL_WRITEMASK}) {
                    GLint value{};glGetIntegerv(key,&value);state+=std::format("{}={}\n",key,value); }
                Binary("diagnostic-composite-state.txt",state.data(),state.size());Color("diagnostic-before-composite.rgba8");
            }
            GLint fbo{},format{};bool bias=false;GLfloat factor{},units{};
            if(!composite && std::getenv("GENGINE_PHASE24_OUTLINE_BIAS_TEST") && sceneFbo && probe.functionalFrames==61) {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
                if(fbo && GLuint(fbo)!=sceneFbo) glGetTextureLevelParameteriv(Attachment(fbo,GL_COLOR_ATTACHMENT0),0,GL_TEXTURE_INTERNAL_FORMAT,&format);
                bias=format==GL_RG32F;
                if(bias) {glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&factor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&units);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(0,-1);
                    constexpr char proof[]="nonindexed mask draw; polygon offset factor=0 units=-1 enabled\n";Binary("diagnostic-bias-applied.txt",proof,sizeof(proof)-1);}
            }
            drawArrays(mode,first,count);
            if(sharedResolve) {
                captured=true;GLint fbo{},active{},stencil{},depth{},w{},h{},samples{},physicalWidth{},physicalHeight{},stencilBits{};
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
                glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE,&stencil);
                glActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE,&depth);glActiveTexture(active);
                glGetTextureLevelParameteriv(depth,0,GL_TEXTURE_WIDTH,&w);glGetTextureLevelParameteriv(depth,0,GL_TEXTURE_HEIGHT,&h);
                glGetTextureLevelParameteriv(depth,0,GL_TEXTURE_SAMPLES,&samples);
                const auto resolved=Attachment(fbo,GL_COLOR_ATTACHMENT0);
                std::vector<float> values(std::size_t(w)*h*2);glGetTextureImage(resolved,0,GL_RG,GL_FLOAT,GLsizei(values.size()*sizeof(float)),values.data());
                Binary("diagnostic-resolved-mask.f32",values.data(),values.size()*sizeof(float));
                Program(program,"diagnostic-shared-visibility-resolve.glsl");
                Samples(stencil,depth,depth,size[0],size[1],samples,"diagnostic-ms-coverage-depth.f32",true);
                glGetNamedRenderbufferParameteriv(Attachment(sceneFbo,GL_DEPTH_ATTACHMENT),GL_RENDERBUFFER_WIDTH,&physicalWidth);
                glGetNamedRenderbufferParameteriv(Attachment(sceneFbo,GL_DEPTH_ATTACHMENT),GL_RENDERBUFFER_HEIGHT,&physicalHeight);
                glGetNamedFramebufferAttachmentParameteriv(sceneFbo,GL_STENCIL_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,&stencilBits);
                auto text=std::format("width={}\nheight={}\nsamples={}\nmask_format=0\ndepth_format={}\nresolved_format={}\nregion_x={}\nregion_y={}\nregion_width={}\nregion_height={}\nphysical_width={}\nphysical_height={}\nstencil_bits={}\nshared_visibility=1\ncpu_row={}\nfunctional_before={}\ngl_error={}\n",w,h,samples,GL_DEPTH24_STENCIL8,GL_RG32F,origin[0],origin[1],size[0],size[1],physicalWidth,physicalHeight,stencilBits,probe.current,probe.functionalFrames,glGetError());
                for(unsigned i=0;i<16;++i) text+=std::format("view{}={}\nprojection{}={}\nmodel{}={}\n",i,glm::value_ptr(probe.observedView)[i],i,glm::value_ptr(probe.observedProjection)[i],i,glm::value_ptr(probe.observed)[i]);
                Binary("diagnostic-attachments.txt",text.data(),text.size());
            }
            if(bias) {glDisable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(factor,units);}
            if(!composite && !sharedVisibility) Raster([&]{drawArrays(mode,first,count);});
            if(composite) {Color("diagnostic-after-composite.rgba8");composited=true;}
        }
        inline void Install()
        {
            if(installed || !probe.functional || !std::getenv("GENGINE_PHASE24_OUTLINE_DIAGNOSTIC")) return;
            blit=glad_glBlitNamedFramebuffer;drawArrays=glad_glDrawArrays;
            drawElements=glad_glDrawElements;
            shaderSource=glad_glShaderSource;
            glad_glBlitNamedFramebuffer=Blit;glad_glDrawArrays=DrawArrays;glad_glDrawElements=DrawElements;glad_glShaderSource=ShaderSource;installed=true;
        }
    }
    bool Functional() noexcept { return probe.enabled && probe.functional; }
    inline PlatformResult Initialize()
    {
        const char* mode=std::getenv("GENGINE_PHASE24_OUTLINE_MODE");
        const char* output=std::getenv("GENGINE_PHASE24_OUTLINE_OUTPUT");
        if(!mode || !output) return std::unexpected(PlatformError{PlatformErrorCode::Initialization,"outline qualifier","Mode/output required"});
        probe.output=output;
        if(!probe.output.is_absolute() || !std::filesystem::is_directory(probe.output))
            return std::unexpected(PlatformError{PlatformErrorCode::Initialization,"outline qualifier","Existing absolute output directory required"});
        probe.functional=std::string_view(mode)=="functional";
        probe.zoomCase=std::getenv("GENGINE_PHASE24_SPHERE_ZOOM");
        if(probe.zoomCase) {
            const std::string_view zoom(probe.zoomCase);
            probe.zoomDistance=zoom=="close"?2.25f:zoom=="distant"?24.f:zoom=="near"?1.02f:
                (zoom=="medium" || zoom=="masked" || zoom=="transparent" || zoom=="partial")?8.f:0.f;
            if(!probe.functional || !probe.zoomDistance)
                return std::unexpected(PlatformError{PlatformErrorCode::Initialization,"Sphere zoom qualifier","Explicit functional close/medium/distant/near case required"});
        }
        probe.selected=std::string_view(mode)=="selected";
        if(!probe.functional && !probe.selected && std::string_view(mode)!="unselected")
            return std::unexpected(PlatformError{PlatformErrorCode::Initialization,"outline qualifier","Unknown mode"});
        probe.rows.reset(new(std::nothrow) Row[Capacity]);
        if(!probe.rows) return std::unexpected(PlatformError{PlatformErrorCode::Initialization,"outline qualifier","Preallocated sample memory unavailable"});
        probe.gpuToCpu.fill(Capacity);probe.enabled=true;probe.start=Now();
        if(!probe.functional) {
            LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);probe.qpcFrequency=frequency.QuadPart;
            std::uint64_t minimum=UINT64_MAX;const auto begin=Now();auto previous=begin;
            for(unsigned i=0;i<100000;++i){const auto next=Now();if(next>previous)minimum=(std::min)(minimum,next-previous);previous=next;}
            const auto clockElapsed=Now()-begin;
            std::atomic<std::uint64_t> counter{};const auto atomicBegin=Now();
            for(unsigned i=0;i<100000;++i){counter.fetch_add(1,std::memory_order_relaxed);counter.fetch_add(1,std::memory_order_relaxed);}
            const auto atomicElapsed=Now()-atomicBegin;
            const auto path=(probe.output/"observer-microbench.txt").string();FILE* file{};
            if(!fopen_s(&file,path.c_str(),"wb") && file) {
                std::fprintf(file,"clock_minimum_positive_ns=%llu\nclock_100000_calls_ns=%llu\ntwo_atomic_100000_iterations_ns=%llu\nqpc_frequency=%llu\nminimal_observer=%d\nshared_frame_bytes=%zu\n",minimum,clockElapsed,atomicElapsed,probe.qpcFrequency,Phase24AllocationObservation::minimal,sizeof(Phase24NativeObservation::Shared));std::fclose(file);
            }else return std::unexpected(PlatformError{PlatformErrorCode::Initialization,"observer","Startup microbenchmark output"});
        }
        return {};
    }
    void Observe(const RenderFrame& frame) noexcept
    {
        if(!probe.enabled) return;
        if(!frame.Cameras().empty()) {probe.observedView=frame.Cameras()[0].view;probe.observedProjection=frame.Cameras()[0].projection;}
        probe.observedDraws=0;
        for(const auto& draw:frame.Draws()) if(draw.entity==probe.identity) {
            probe.observed=draw.worldTransform;++probe.observedDraws;
        }
        if(!probe.functional && probe.count) {
            auto& row=probe.rows[probe.current];row.frameItems=frame.Draws().size();row.selectedItems=probe.observedDraws;
            row.selectedX=probe.observed[3].x;row.selectedY=probe.observed[3].y;row.selectedZ=probe.observed[3].z;
            if(Phase24AllocationObservation::minimal)return;
            // Conservative CPU proof using the exact frozen frame. Every mesh
            // is bounded in world space; the camera-to-selected-bounds AABB
            // contains every potential occluding ray segment. No extra GPU
            // draw, readback or synchronization is introduced for this proof.
            const auto& camera=frame.Cameras()[0];row.viewportWidth=camera.viewportWidth;row.viewportHeight=camera.viewportHeight;
            row.visibilityMargin=1.f;row.rotationAngle=std::atan2(probe.observed[2].x,probe.observed[2].z);
            glm::vec3 beamMin=camera.worldPosition,beamMax=camera.worldPosition;
            const auto matrix=camera.projection*camera.view;
            for(const auto& draw:frame.Draws())if(draw.entity==probe.identity) {
                const auto bounds=frame.Resources()[draw.resources].Mesh()->Bounds();
                if(bounds.empty){probe.Fail("selected fixture bounds unavailable");continue;}
                for(unsigned corner=0;corner<8;++corner) {
                    glm::vec4 p(0,0,0,1);for(unsigned a=0;a<3;++a)p[a]=(corner&(1u<<a))?bounds.maximum[a]:bounds.minimum[a];
                    p=draw.worldTransform*p;beamMin=glm::min(beamMin,glm::vec3(p));beamMax=glm::max(beamMax,glm::vec3(p));
                    const auto clip=matrix*p;
                    for(unsigned a=0;a<3;++a)row.visibilityMargin=(std::min)(row.visibilityMargin,clip.w>0?1.f-std::abs(clip[a]/clip.w):-1.f);
                }
            }
            std::string visibilityEvidence;
            if(probe.current==100)visibilityEvidence=std::format("sky={}/{}/{}\n",probe.skyIdentity.index,probe.skyIdentity.generation,probe.skyIdentity.registry);
            for(const auto& draw:frame.Draws())if(draw.entity!=probe.identity) {
                // The actual environment shader emits far-plane depth (xyww).
                // Its world-space bounds cannot occlude a foreground mesh.
                if(draw.entity==probe.skyIdentity)continue;
                const auto bounds=frame.Resources()[draw.resources].Mesh()->Bounds();
                if(bounds.empty){++row.occlusionCandidates;
                    if(probe.current==100)visibilityEvidence+=std::format("empty={}/{}/{}\n",draw.entity.index,draw.entity.generation,draw.entity.registry);
                    continue;}
                glm::vec3 lower(FLT_MAX),upper(-FLT_MAX);
                for(unsigned corner=0;corner<8;++corner) {
                    glm::vec4 p(0,0,0,1);for(unsigned a=0;a<3;++a)p[a]=(corner&(1u<<a))?bounds.maximum[a]:bounds.minimum[a];
                    const auto world=glm::vec3(draw.worldTransform*p);lower=glm::min(lower,world);upper=glm::max(upper,world);
                }
                if(glm::all(glm::lessThanEqual(lower,beamMax)) && glm::all(glm::greaterThanEqual(upper,beamMin))){++row.occlusionCandidates;
                    if(probe.current==100)visibilityEvidence+=std::format("candidate={}/{}/{} bounds={},{},{}:{},{},{} beam={},{},{}:{},{},{}\n",draw.entity.index,draw.entity.generation,draw.entity.registry,lower.x,lower.y,lower.z,upper.x,upper.y,upper.z,beamMin.x,beamMin.y,beamMin.z,beamMax.x,beamMax.y,beamMax.z);}
            }
            if(probe.current==100)Diagnostic::Binary("visibility-candidates.txt",visibilityEvidence.data(),visibilityEvidence.size());
            if(row.measured && (row.selectedItems!=1 || row.visibilityMargin<=0 || row.occlusionCandidates))
                probe.Fail("selected Sphere continuous visibility proof incomplete");
        }
    }
    inline bool Capture(const RenderTarget& target,const char* name)
    {
        const auto& d=target.Description().Storage;
        probe.width=d.Width;probe.height=d.Height;
        probe.pixels.resize(std::size_t(d.Width)*d.Height*4);
        if(!target.ReadColor(probe.pixels)) {probe.Fail("actual viewport color readback");return false;}
        stbi_flip_vertically_on_write(1);
        const auto path=(probe.output/(std::string(name)+".png")).string();
        if(!stbi_write_png(path.c_str(),d.Width,d.Height,4,probe.pixels.data(),d.Width*4)) {
            probe.Fail("actual viewport PNG write");return false;
        }
        return true;
    }
    inline std::size_t GoldPixels()
    {
        std::size_t gold=0;
        for(std::size_t i=0;i+3<probe.pixels.size();i+=4) {
            const auto r=std::to_integer<unsigned>(probe.pixels[i]),g=std::to_integer<unsigned>(probe.pixels[i+1]),b=std::to_integer<unsigned>(probe.pixels[i+2]);
            gold+=r>200 && g>125 && g<220 && b<85;
        }
        return gold;
    }
    inline void Write()
    {
        if(probe.written) return;probe.written=true;
        FILE* output{};
        const auto file=(probe.output/"frames.csv").string();
        if(fopen_s(&output,file.c_str(),"wb") || !output) {probe.Fail("sample output");return;}
        std::fprintf(output,"frame,measured,success,submitted,work_ns,pacing_ns,interval_ns,preparation_ns,extraction_ns,draws,passes,outline_draws,outline_passes,scratch_bytes,region_x,region_y,region_width,region_height,pending,available,complete");
        for(unsigned pass=0;pass<Passes;++pass) std::fprintf(output,",cpu_%u,gpu_%u",pass,pass);
        std::fprintf(output,",start_ns,qpc_begin,qpc_end,new_count,new_bytes,private_bytes,working_set,engine_api_draws,ui_api_draws,elements,readbacks,synchronizations,frame_items,selected_items,selected_x,selected_y,selected_z,window_count,ui_viewports");
        std::fprintf(output,",visibility_margin,occlusion_candidates,viewport_width,viewport_height,rotation_angle\n");
        for(std::size_t i=0;i<probe.count;++i) {
            const auto& r=probe.rows[i];
            std::fprintf(output,"%zu,%d,%d,%d,%llu,%llu,%llu,%llu,%llu,%zu,%zu,%zu,%zu,%zu,%u,%u,%u,%u,%u,%u,%u",i,r.measured,r.successful,r.submitted,r.work,r.pacing,r.interval,r.preparation,r.extraction,r.draws,r.passes,r.outlineDraws,r.outlinePasses,r.scratch,r.region.x,r.region.y,r.region.width,r.region.height,r.pending,r.available,r.complete);
            for(unsigned pass=0;pass<Passes;++pass) std::fprintf(output,",%llu,%llu",r.cpu[pass],r.gpu[pass]);
            std::fprintf(output,",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%zu,%zu,%.9g,%.9g,%.9g,%zu,%zu",r.start,r.qpcBegin,r.qpcEnd,r.newCount,r.newBytes,r.privateBytes,r.workingSet,r.engineApiDraws,r.uiApiDraws,r.elements,r.reads,r.syncs,r.frameItems,r.selectedItems,r.selectedX,r.selectedY,r.selectedZ,r.windowCount,r.uiViewports);
            std::fprintf(output,",%.9g,%u,%u,%u,%.9g\n",r.visibilityMargin,r.occlusionCandidates,r.viewportWidth,r.viewportHeight,r.rotationAngle);
        }
        if(std::fclose(output)) probe.Fail("sample close");
        const auto summary=(probe.output/"observer-result.txt").string();
        if(!fopen_s(&output,summary.c_str(),"wb") && output) {
            std::fprintf(output,"mode=%s\nframes=%zu\nmeasured=%zu\nchecks=%zu\nfailed=%d\nreason=%s\nsample_capacity=%zu\nsample_bytes=%zu\nqpc_frequency=%llu\n",probe.functional?"functional":probe.selected?"selected":"unselected",probe.count,probe.measured,probe.checks,probe.failed,probe.failure,Capacity,Capacity*sizeof(Row),probe.qpcFrequency);
            std::fclose(output);
        }
    }
}

// These definitions have one owner: RbsValidation.cpp. All frame-time collection
// uses the preallocated array. Write is invoked only after the measurement/drain.
namespace GEngine::Phase24Qualification
{
    void WorkBegin() noexcept
    {
        if(!probe.enabled || probe.done) return;
        Diagnostic::Install();
        Performance::Install();
        if(probe.count==Capacity) {probe.Fail("sample capacity exhausted");return;}
        probe.current=probe.count++;auto& row=probe.rows[probe.current];row.start=Now();
        if(!probe.functional) {
            LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);row.qpcBegin=qpc.QuadPart;
            row.newCountBegin=Phase24AllocationObservation::count.load(std::memory_order_relaxed);
            row.newBytesBegin=Phase24AllocationObservation::bytes.load(std::memory_order_relaxed);
        }
        if(probe.current) row.interval=row.start-probe.rows[probe.current-1].start;
        if(!probe.functional && probe.current>=600 && row.start-probe.start>=10000000000ull && probe.measured<10000) {
            row.measured=true;if(!probe.measured) probe.firstMeasured=probe.current;
            ++probe.measured;probe.lastMeasured=probe.current;
        }
    }
    void Pacing(bool begin) noexcept
    {
        if(!probe.enabled || probe.done) return;
        if(begin) probe.pacingStart=Now();else probe.rows[probe.current].pacing+=Now()-probe.pacingStart;
    }
    void WorkEnd(bool success) noexcept
    {
        if(!probe.enabled || probe.done || !probe.count) return;
        auto& row=probe.rows[probe.current];Performance::End(row);row.work=Now()-row.start-row.pacing;row.successful=success;
        if(row.measured && probe.measured==10000 && !probe.measurementEnd) probe.measurementEnd=Now();
        if(!success) probe.Fail("application work failed");
    }
    void CollectTimings(const PassTiming& timings) noexcept
    {
        if(!probe.enabled || probe.done || !probe.count) return;
        if(!timings.Enabled()) {if(!probe.functional) probe.Fail("timings unavailable");return;}
        for(const auto& s:timings.Current()) {
            if(s.frame>=Capacity) {probe.Fail("GPU association capacity");return;}
            probe.gpuToCpu[s.frame]=probe.current;
            const auto pass=static_cast<unsigned>(s.pass);if(pass>=Passes) {probe.Fail("pass capacity");return;}
            auto& row=probe.rows[probe.current];row.cpu[pass]=s.cpuNanoseconds;
            if(s.completed) row.complete|=1u<<pass;
            if(s.gpu==GpuTiming::Pending) row.pending|=1u<<pass;
            else if(s.gpu!=GpuTiming::NotMeasured) probe.Fail("GPU timing unavailable or pool exhausted");
        }
        for(const auto& s:timings.Collected()) {
            if(s.frame>=Capacity || probe.gpuToCpu[s.frame]>=probe.count) {probe.Fail("missing GPU frame association");return;}
            auto& row=probe.rows[probe.gpuToCpu[s.frame]];const auto pass=static_cast<unsigned>(s.pass);
            if(!s.gpuNanoseconds || s.gpu!=GpuTiming::Available) {probe.Fail("missing GPU result");return;}
            row.gpu[pass]=*s.gpuNanoseconds;row.available|=1u<<pass;
        }
    }
    void CollectSubmission(const ScheduledFrameStats& stats) noexcept
    {
        if(!probe.enabled || probe.done || !probe.count) return;
        auto& row=probe.rows[probe.current];row.submitted=true;
        row.preparation=std::uint64_t(stats.extraction.preparationMicroseconds*1000.0);
        row.extraction=std::uint64_t(stats.extraction.extractionMicroseconds*1000.0);
        row.draws=stats.submission.submittedDrawCalls;
        for(const auto& e:stats.trace.Events()) row.passes+=e.stage==FrameStage::Pass;
        row.outlineDraws=stats.submission.selectionDrawCalls;row.outlinePasses=stats.submission.selectionPasses;
        row.scratch=stats.submission.selectionScratchBytes;row.region=stats.submission.selectionRegion;
    }
}
#endif // standalone native heap consumer / RBS observer
