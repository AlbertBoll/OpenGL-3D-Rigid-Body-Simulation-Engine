#include "gepch.h"
#include "Renderer/PassTiming.h"
#include "Renderer/FrameSubmission.h"
#include "Core/GLContextThread.h"
#include "Core/RenderCounters.h"
#include <cstdio>
#include <new>

namespace GEngine
{
    std::string_view PassLabel(RenderPass pass) noexcept
    {
        switch(pass) {
        case RenderPass::DirectionalShadow:return "directional-shadow";
        case RenderPass::PointShadow:return "point-shadow";
        case RenderPass::Picking:return "picking";
        case RenderPass::PickingReadback:return "picking-readback";
        case RenderPass::Opaque:return "opaque";
        case RenderPass::Masked:return "masked";
        case RenderPass::Skybox:return "skybox";
        case RenderPass::Transparent:return "transparent";
        case RenderPass::Debug:return "debug";
        case RenderPass::Resolve:return "resolve";
        case RenderPass::EditorUI:return "editor-ui";
        case RenderPass::Present:return "present";
        case RenderPass::LegacyScene:return "legacy-scene";
        }
        return "unknown";
    }
    std::string_view TimingLabel(GpuTiming value) noexcept
    {
        switch(value) {
        case GpuTiming::Pending:return "pending";
        case GpuTiming::Available:return "available";
        case GpuTiming::Unsupported:return "unsupported";
        case GpuTiming::PoolExhausted:return "pool-exhausted";
        case GpuTiming::NotMeasured:return "not-measured";
        case GpuTiming::Abandoned:return "abandoned";
        }
        return "unknown";
    }
    struct PassTiming::Storage
    {
        struct Slot { std::array<GLuint,2> queries{}; PassTimingSample sample; bool pending = false; };
        std::array<Slot,PoolCapacity> slots;
        std::array<PassTimingSample,FrameCapacity> current;
        std::array<PassTimingSample,PoolCapacity> collected;
        std::size_t count{}, collectedCount{};
        std::uint64_t frame{};
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        std::thread::id thread = std::this_thread::get_id();
        bool supported = false, active = false, healthy = true;
        GLint bits{};
        FILE* output{};
        void RequireContext() const noexcept
        {
            GLContextThread::RequireOwner(thread,"pass timing");
            GLContextThread::RequireCurrent("pass timing");
            Asset::AssetDetail::RequireInvariant(context == SDL_GL_GetCurrentContext());
        }
        void Write(const PassTimingSample& s) noexcept
        {
            if(!output) return;
            healthy &= std::fprintf(output,"%llu,%llu,%s,%llu,%llu,%llu,%d,%d,%s,",
                s.frame,s.collectedFrame,PassLabel(s.pass).data(),s.cpuNanoseconds,
                s.submittedItems,s.submittedDraws,s.itemsKnown,s.drawsKnown,TimingLabel(s.gpu).data())>=0;
            if(s.gpuNanoseconds) healthy &= std::fprintf(output,"%llu",*s.gpuNanoseconds)>=0;
            healthy &= std::fprintf(output,",%d\n",s.completed)>=0;
        }
        ~Storage()
        {
            RequireContext();
            for(auto& slot:slots) {
                if(slot.pending) {slot.sample.gpu=GpuTiming::Abandoned; Write(slot.sample);}
                if(slot.queries[0] || slot.queries[1]) glDeleteQueries(2,slot.queries.data());
            }
            if(output && std::fclose(output)!=0) healthy=false;
            if(!healthy) GENGINE_CORE_ERROR("Pass timing output failed; capture is incomplete");
        }
    };
    thread_local PassTiming* PassTiming::active{};
    thread_local PassTiming::Scope* PassTiming::scope{};
    PassTiming::PassTiming() = default;
    PassTiming::~PassTiming() = default;
    PassTiming::PassTiming(PassTiming&& other) noexcept
    {
        Asset::AssetDetail::RequireInvariant(!other.Active() && active!=&other);
        storage=std::move(other.storage);
    }
    PassTiming& PassTiming::operator=(PassTiming&& other) noexcept
    {
        if(this!=&other) {
            Asset::AssetDetail::RequireInvariant(!Active() && !other.Active() && active!=this && active!=&other);
            storage=std::move(other.storage);
        }
        return *this;
    }
    bool PassTiming::Enabled() const noexcept { return bool(storage); }
    bool PassTiming::Active() const noexcept { return storage && storage->active; }
    bool PassTiming::OutputHealthy() const noexcept { return !storage || storage->healthy; }
    void PassTiming::Reset() noexcept
    {
        Asset::AssetDetail::RequireInvariant(!Active() && (!scope || scope->owner!=this) && active!=this);
        storage.reset();
    }
    std::expected<void,TimingError> PassTiming::Initialize(bool enabled,const char* output)
    {
        if(storage) return std::unexpected(TimingError::FrameActive);
        if(!enabled) return {};
        if(!GLContextThread::IsCurrentOwner()) return std::unexpected(TimingError::Context);
        auto next=std::unique_ptr<Storage>(new (std::nothrow) Storage);
        if(!next) return std::unexpected(TimingError::Allocation);
        next->supported=(GLAD_GL_VERSION_3_3 || GLAD_GL_ARB_timer_query) && glad_glQueryCounter
            && glad_glGetQueryObjectui64v && glad_glGetQueryObjectiv && glad_glGetQueryiv
            && glad_glGenQueries && glad_glDeleteQueries;
        if(next->supported) {
            glGetQueryiv(GL_TIMESTAMP,GL_QUERY_COUNTER_BITS,&next->bits);
            next->supported=next->bits>0 && next->bits<=64;
            if(glGetError()!=GL_NO_ERROR) return std::unexpected(TimingError::Driver);
        }
        if(next->supported) for(auto& slot:next->slots) {
            glGenQueries(2,slot.queries.data());
            if(glGetError()!=GL_NO_ERROR || !slot.queries[0] || !slot.queries[1])
                return std::unexpected(TimingError::Driver);
        }
        if(output) {
            if(fopen_s(&next->output,output,"w")!=0) return std::unexpected(TimingError::Output);
            next->healthy=std::fprintf(next->output,"frame,collected_frame,pass,cpu_ns,items,draws,items_known,draws_known,gpu_status,gpu_ns,completed\n")>=0;
            if(!next->healthy) return std::unexpected(TimingError::Output);
        }
        storage=std::move(next);
        return {};
    }
    std::expected<void,TimingError> PassTiming::BeginFrame()
    {
        if(!storage) return {};
        auto& s=*storage;
        if(!GLContextThread::IsCurrentOwner() || s.context!=SDL_GL_GetCurrentContext())
            return std::unexpected(TimingError::Context);
        if(s.active) return std::unexpected(TimingError::FrameActive);
        if(!s.healthy) return std::unexpected(TimingError::Output);
        ++s.frame; s.count=0; s.collectedCount=0;
        for(auto& slot:s.slots) {
            if(!slot.pending || s.frame-slot.sample.frame<CollectionDelay) continue;
            GLint beginReady{},endReady{};
            glGetQueryObjectiv(slot.queries[0],GL_QUERY_RESULT_AVAILABLE,&beginReady);
            glGetQueryObjectiv(slot.queries[1],GL_QUERY_RESULT_AVAILABLE,&endReady);
            if(!beginReady || !endReady) continue;
            // Both availability checks succeeded on an older frame. Never poll
            // in a loop, wait, flush or fetch an unavailable/current result.
            GLuint64 begin{},end{};
            glGetQueryObjectui64v(slot.queries[0],GL_QUERY_RESULT,&begin);
            glGetQueryObjectui64v(slot.queries[1],GL_QUERY_RESULT,&end);
            const auto mask=s.bits==64?~std::uint64_t{}:(std::uint64_t{1}<<s.bits)-1;
            slot.sample.gpuNanoseconds=(end-begin)&mask;
            slot.sample.gpu=GpuTiming::Available; slot.sample.collectedFrame=s.frame;
            s.collected[s.collectedCount++]=slot.sample;
            s.Write(slot.sample); slot.pending=false;
        }
        s.active=true;
        return {};
    }
    void PassTiming::EndFrame() noexcept
    {
        if(!Active()) return;
        GLContextThread::RequireOwner(storage->thread,"timing frame end");
        Asset::AssetDetail::RequireInvariant(!scope || scope->owner!=this);
        for(const auto& sample:Current()) if(sample.gpu!=GpuTiming::Pending) storage->Write(sample);
        if(storage->output) storage->healthy &= std::fflush(storage->output)==0;
        storage->active=false;
    }
    std::span<const PassTimingSample> PassTiming::Current() const noexcept
    { return storage?std::span(storage->current).first(storage->count):std::span<const PassTimingSample>{}; }
    std::span<const PassTimingSample> PassTiming::Collected() const noexcept
    { return storage?std::span(storage->collected).first(storage->collectedCount):std::span<const PassTimingSample>{}; }
    PassTiming::Activation::Activation(PassTiming& value) noexcept : previous(active) { active=&value; }
    PassTiming::Activation::~Activation() { active=previous; }
    PassTiming::Scope::Scope(RenderPass pass)
        : Scope(active?*active:[]()->PassTiming& {static thread_local PassTiming disabled;return disabled;}(),
            pass,TimingCounts::Explicit,pass!=RenderPass::PickingReadback) {}
    PassTiming::Scope::Scope(PassTiming& timing,RenderPass pass,TimingCounts source,bool gpu) : counts(source)
    {
        if(!timing.Active()) return;
        auto& s=*timing.storage;
        s.RequireContext();
        Asset::AssetDetail::RequireInvariant(s.count<s.current.size());
        owner=&timing; sample=s.count++; previous=scope; scope=this;
        auto& record=s.current[sample]; record={}; record.frame=s.frame; record.pass=pass;
        record.itemsKnown=counts==TimingCounts::Explicit;
        record.drawsKnown=record.itemsKnown || (counts==TimingCounts::EngineCalls && RenderCounters::Enabled);
        if(counts==TimingCounts::EngineCalls) initialDraws=RenderCounters::Current().frame.draws;
        record.gpu=!gpu?GpuTiming::NotMeasured:!s.supported?GpuTiming::Unsupported:GpuTiming::PoolExhausted;
        if(gpu && s.supported) for(std::size_t i=0;i<s.slots.size();++i) if(!s.slots[i].pending) {
            slot=i; s.slots[i].pending=true; record.gpu=GpuTiming::Pending;
            glQueryCounter(s.slots[i].queries[0],GL_TIMESTAMP); break;
        }
        start=std::chrono::steady_clock::now();
    }
    PassTiming::Scope::~Scope()
    {
        if(!owner) return;
        const auto end=std::chrono::steady_clock::now();
        auto& s=*owner->storage;
        GLContextThread::RequireOwner(s.thread,"pass timing end");
        // CPU-only UI/error cleanup can finish after a context switch failed.
        // Query commands still require their exact creating context.
        if(slot<PoolCapacity) s.RequireContext();
        Asset::AssetDetail::RequireInvariant(scope==this);
        auto& record=s.current[sample]; record.completed=completed;
        record.cpuNanoseconds=std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
        if(counts==TimingCounts::EngineCalls && record.drawsKnown)
            record.submittedDraws=RenderCounters::Current().frame.draws-initialDraws;
        if(slot<PoolCapacity) {
            glQueryCounter(s.slots[slot].queries[1],GL_TIMESTAMP);
            s.slots[slot].sample=record;
        }
        scope=previous;
    }
    void PassTiming::Submitted(std::uint64_t items,std::uint64_t draws) noexcept
    {
        if(!scope || scope->counts!=TimingCounts::Explicit) return;
        auto& record=scope->owner->storage->current[scope->sample];
        record.submittedItems+=items; record.submittedDraws+=draws;
    }
}
