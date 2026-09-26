#include "Assets/AsyncUploadQueue.h"
#include "Assets/AssetRegistry.h"
#include "Renderer/FrameScheduler.h"
#include <type_traits>
using namespace GEngine;
using namespace GEngine::Asset;
static_assert(!std::is_copy_constructible_v<AsyncUploadQueue>);
static_assert(std::same_as<decltype(std::declval<const AssetDecodeJob&>().Decode(
    std::declval<UploadCancellation>(), 1)), std::expected<std::unique_ptr<const UploadRequest>, UploadError>>);
#ifdef UPLOAD_NEGATIVE_DRAIN
void Unauthorized(AsyncUploadQueue& queue) { queue.DrainUpdateFrameResources(); }
#endif
#ifndef UPLOAD_SCHEMA_ONLY
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLContextThread.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <print>
#include <thread>
#include <vector>
#include <crtdbg.h>
using namespace std::chrono_literals;

namespace
{
    std::atomic<int> checks{};
    template<class T> void Check(const T& condition, const char* label)
    { ++checks; if (!static_cast<bool>(condition)) { std::println(stderr, "[FAIL] {}", label); std::exit(1); } }
    template<class T, class E> T Take(std::expected<T, E> value, const char* label)
    { Check(value, label); return std::move(*value); }
    template<class Predicate> void Wait(Predicate predicate, const char* label)
    {
        const auto end = std::chrono::steady_clock::now() + 10s;
        while (!predicate()) { Check(std::chrono::steady_clock::now() < end, label); std::this_thread::yield(); }
    }
    struct ResourceTag;
    using ResourceHandle = AssetHandle<ResourceTag>;
    struct Resource
    {
        int value; std::atomic<int>* retired;
        Resource(int v, std::atomic<int>* r) : value(v), retired(r) {}
        ~Resource() { GLContextThread::AssertCurrent("async fake resource retirement"); ++*retired; }
    };
    using Registry = AssetRegistry<ResourceHandle, Resource>;
    struct Fence final : AssetRetirementFence
    {
        std::atomic<bool>& complete;
        explicit Fence(std::atomic<bool>& done) : complete(done) {}
        bool IsComplete() const noexcept override { return complete.load(); }
    };
    struct Control
    {
        std::atomic<int> started{}, decoded{}, applied{}, destroyed{};
        std::atomic<bool> gate{true};
        bool cooperate = true, decodeFailure{}, uploadFailure{}, tooLarge{}, slow{}, touchGl{};
        Registry* registry{}; ResourceHandle* handle{}; std::atomic<int>* retired{};
        int value{}; bool destroy{};
        AsyncUploadQueue* queue{}; UploadTicket* ticket{};
        Control* releaseDuringApply{};
        UploadTicket* cancelDuringApply{};
        bool waitForLateQueue{};
        Window* switchContext{};
    };
    struct Request final : UploadRequest
    {
        Control& c; const std::size_t bytes;
        Request(Control& control, std::size_t size) : c(control), bytes(size) { ++c.decoded; }
        ~Request() { ++c.destroyed; }
        std::size_t Bytes() const noexcept override { return bytes; }
        UploadResult Apply(const AssetPublication::Publication& publication) const noexcept override
        {
            GLContextThread::AssertCurrent("async fake upload");
            ++c.applied;
            if (c.queue && c.ticket) {
                Check(Take(c.queue->Status(*c.ticket), "upload status").state == AsyncAssetState::Uploading, "uploading state observable");
                const auto cancelled = c.queue->Cancel(*c.ticket);
                Check(!cancelled && cancelled.error().code == UploadCode::Busy, "late cancellation typed Busy");
            }
            if (c.releaseDuringApply) {
                if (c.cancelDuringApply) Check(c.queue->Cancel(*c.cancelDuringApply), "cancel later entry during upload");
                c.releaseDuringApply->gate = true;
                if (c.waitForLateQueue) Wait([&] { return c.queue->Stats().queuedRequests != 0; }, "late completion queues during upload");
            }
            if (c.slow) std::this_thread::sleep_for(8ms);
            if (c.touchGl) { GLuint texture{}; glGenTextures(1, &texture); Check(texture != 0, "real context upload"); glDeleteTextures(1, &texture); }
            if (c.switchContext) {
                Check(c.switchContext->BeginRender(), "switch context negative setup");
                std::println("[negative] upload returned on another registered context"); std::fflush(stdout);
            }
            if (c.uploadFailure) return std::unexpected(UploadError{UploadCode::UploadFailed, std::make_error_code(std::errc::io_error), RegistryError::Busy, 47});
            if (c.registry) {
                if (c.destroy) {
                    auto destroyed = c.registry->Destroy(publication, *c.handle);
                    if (!destroyed) return std::unexpected(UploadError{UploadCode::UploadFailed, {}, destroyed.error()});
                }
                else if (*c.handle) {
                    auto replaced = c.registry->Replace(publication, *c.handle, c.value, c.retired);
                    if (!replaced) return std::unexpected(UploadError{UploadCode::UploadFailed, {}, replaced.error()});
                }
                else {
                    auto created = c.registry->Create(publication, c.value, c.retired);
                    if (!created) return std::unexpected(UploadError{UploadCode::UploadFailed, {}, created.error()});
                    *c.handle = *created;
                }
            }
            return {};
        }
    };
    struct Job final : AssetDecodeJob
    {
        Control& c;
        explicit Job(Control& control) : c(control) {}
        std::expected<std::unique_ptr<const UploadRequest>, UploadError> Decode(UploadCancellation cancel, std::size_t bytes) const noexcept override
        {
            Check(!GLContextThread::IsCurrentOwner(), "worker owns no GL context"); ++c.started;
            while (!c.gate.load()) {
                if (c.cooperate && cancel.StopRequested()) return std::unexpected(UploadError{UploadCode::Cancelled});
                std::this_thread::yield();
            }
            if (c.decodeFailure) return std::unexpected(UploadError{UploadCode::DecodeFailed, std::make_error_code(std::errc::invalid_argument), {}, 19});
            return std::unique_ptr<const UploadRequest>(new Request(c, bytes + (c.tooLarge ? 1 : 0)));
        }
    };
    auto JobFor(Control& c) -> std::unique_ptr<const AssetDecodeJob> { return std::make_unique<Job>(c); }
    UploadTicket Submit(AsyncUploadQueue& queue, Control& c, std::size_t bytes = 4, std::optional<UploadTicket> old = {})
    {
        auto job = JobFor(c); auto ticket = Take(queue.Submit(job, bytes, old), "submit");
        Check(!job, "submission consumes job once"); return ticket;
    }
    void Queued(AsyncUploadQueue& queue, std::size_t count)
    { Wait([&] { return queue.Stats().queuedRequests == count; }, "wait queue"); }
    void State(AsyncUploadQueue& queue, UploadTicket ticket, AsyncAssetState expected)
    { Wait([&] { return Take(queue.Status(ticket), "ticket status").state == expected; }, "wait state"); }
    void Released(AsyncUploadQueue& queue, UploadTicket ticket)
    {
        Wait([&] { auto r = queue.Release(ticket); Check(r || r.error().code == UploadCode::Busy, "release result"); return bool(r); }, "release joins slot activity");
    }
    UploadLimits Limits()
    { UploadLimits l; l.requests=16; l.workers=2; l.decodedPayloads=8; l.decodedBytes=64; l.queuedRequests=4; l.queuedBytes=32; l.frameBytes=16; l.frameTime=1s; return l; }
    void CpuTests()
    {
        AssetPublication publication;
        auto limits = Limits(); limits.workers = 1; limits.queuedRequests = 1; limits.queuedBytes = 4; limits.decodedPayloads=3; limits.decodedBytes=12;
        auto invalid = limits; invalid.workers=0;
        Check(!AsyncUploadQueue::Create(publication, invalid), "invalid limits");
        Control first, second, third, excess;
        auto queue = Take(AsyncUploadQueue::Create(publication, limits), "CPU independent queue");
        auto oversized=JobFor(excess); auto bad=queue->Submit(oversized, 5);
        Check(!bad && bad.error().code==UploadCode::Oversized && oversized, "oversized preserves ownership");
        auto a=Submit(*queue,first); Queued(*queue,1);
        auto b=Submit(*queue,second); State(*queue,b,AsyncAssetState::CpuReady);
        auto c=Submit(*queue,third); State(*queue,c,AsyncAssetState::Requested);
        auto excessJob=JobFor(excess); auto full=queue->Submit(excessJob,4,a);
        Check(!full && full.error().code==UploadCode::Capacity && excessJob, "capacity and failed supersession preserve request");
        Check(Take(queue->Status(a),"old status").state==AsyncAssetState::CpuReady, "failed supersession keeps old live");
        Check(queue->Stats().reservedBytes==12 && queue->Stats().reservedPayloads==3 && queue->Stats().queuedBytes==4, "queued and outstanding bounds");
        queue->Shutdown();
        for(auto ticket:{a,b,c}) State(*queue,ticket,AsyncAssetState::Cancelled);
        Check(queue->Stats().reservedBytes==0 && queue->Stats().queuedRequests==0 && queue->Stats().reservedPayloads==0, "shutdown wakes capacity waiters without drain");
        Check(first.applied==0 && second.applied==0 && third.started==0, "no worker upload or post-close launch");
        Check(first.destroyed==first.decoded && second.destroyed==second.decoded, "CPU payloads destroyed exactly once");
        auto closed=queue->Submit(excessJob,4); Check(!closed && closed.error().code==UploadCode::Closed, "closed typed admission");
        queue->Shutdown();

        Control loading; loading.gate=false;
        auto running=Take(AsyncUploadQueue::Create(publication,Limits()),"loading shutdown queue");
        auto active=Submit(*running,loading); State(*running,active,AsyncAssetState::Loading);
        running->Shutdown(); State(*running,active,AsyncAssetState::Cancelled);
        Check(loading.started==1 && loading.applied==0 && running->Stats().reservedBytes==0,"cooperative shutdown joins in-flight decode");

        Control late, replacement; late.gate=false; late.cooperate=false;
        auto generations=Take(AsyncUploadQueue::Create(publication,Limits()),"generation queue");
        auto old=Submit(*generations,late); State(*generations,old,AsyncAssetState::Loading);
        auto fresh=Submit(*generations,replacement,4,old); Queued(*generations,1);
        late.gate=true; Released(*generations,old);
        Check(late.decoded==1 && late.destroyed==1 && late.applied==0,"stale successful decode discarded after supersession");
        Control reused; auto next=Submit(*generations,reused);
        Check(next.index==old.index && next.generation!=old.generation,"single generation identity model on reuse");
        Check(!generations->Status(old) && !generations->Cancel(old),"old generation cannot address replacement");
        Check(generations->Cancel(fresh),"queued cancellation");
        generations->Shutdown();

        Control failed, huge; failed.decodeFailure=true; huge.tooLarge=true;
        auto errors=Take(AsyncUploadQueue::Create(publication,Limits()),"typed decode queue");
        auto failure=Submit(*errors,failed); State(*errors,failure,AsyncAssetState::Failed);
        auto status=Take(errors->Status(failure),"failure detail");
        Check(status.error->code==UploadCode::DecodeFailed && status.error->detail==19 && status.error->system==std::errc::invalid_argument,"worker structured error retained");
        auto over=Submit(*errors,huge); State(*errors,over,AsyncAssetState::Failed);
        Check(Take(errors->Status(over),"payload overflow").error->code==UploadCode::PayloadSize && huge.destroyed==1,"decoded contract breach never queued");

        for(bool byteLimit:{false,true}) {
            Control held, refused; held.gate=false;
            auto bounded=Limits(); bounded.decodedPayloads=byteLimit?8:1; bounded.decodedBytes=byteLimit?4:64;
            auto pressure=Take(AsyncUploadQueue::Create(publication,bounded),"independent payload bound");
            Submit(*pressure,held); auto job=JobFor(refused); auto result=pressure->Submit(job,4);
            Check(!result && result.error().code==UploadCode::Capacity && job,"independent decoded count/bytes enforcement");
        }
        {
            Control held, waiting;
            auto bounded=Limits(); bounded.queuedBytes=4;
            auto pressure=Take(AsyncUploadQueue::Create(publication,bounded),"queued byte bound");
            Submit(*pressure,held);Queued(*pressure,1);auto pending=Submit(*pressure,waiting);State(*pressure,pending,AsyncAssetState::CpuReady);
            Check(pressure->Stats().queuedRequests==1 && pressure->Stats().queuedBytes==4 && pressure->Stats().reservedBytes==8,"queue bytes bound independently of request count");
        }
        {
            auto concurrent=Take(AsyncUploadQueue::Create(publication,Limits()),"concurrent producers");
            std::vector<std::thread> producers;
            for(int lane=0;lane<4;++lane) producers.emplace_back([&] {
                for(int iteration=0;iteration<64;++iteration) {
                    Control c; auto job=JobFor(c); std::optional<UploadTicket> ticket;
                    Wait([&] {auto r=concurrent->Submit(job,4);if(r) ticket=*r;else Check(r.error().code==UploadCode::Capacity,"pressure retry typed");return bool(r);},"producer admission");
                    Check(concurrent->Cancel(*ticket),"concurrent cancel");Released(*concurrent,*ticket);
                    Check(c.applied==0 && c.decoded==c.destroyed,"concurrent ownership settles before release");
                }
            });
            for(auto& producer:producers) producer.join();
            Check(concurrent->Stats().reservedBytes==0 && concurrent->Stats().queuedRequests==0,"concurrent accounting returns to zero");
        }
        std::println("[PASS] async-cpu states/cancel/generations/shutdown/capacity/errors");
    }
    void Drain(EngineContext& root, AsyncUploadQueue& queue)
    {
        RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager()};
        context.visible=false; context.uploads=&queue;
        Take(FrameScheduler::Render(context),"scheduler upload boundary");
    }
    void GlTests(EngineContext& root)
    {
        auto& publication=root.SceneServices().value().publication;
        Control first, second; first.touchGl=true;
        auto limits=Limits(); limits.frameBytes=4;
        auto queue=Take(AsyncUploadQueue::Create(publication,limits),"GL queue");
        auto a=Submit(*queue,first); Queued(*queue,1);
        auto b=Submit(*queue,second); Queued(*queue,2);
        first.queue=queue.get(); first.ticket=&a;
        {
            auto frame=publication.BeginFrame();
            RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager()}; context.uploads=queue.get(); context.visible=false;
            auto frozen=FrameScheduler::Render(context);
            Check(!frozen && std::get<UploadError>(frozen.error().cause).code==UploadCode::PublicationBusy && first.applied==0,"no publication during frame input freeze");
        }
        RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager()}; context.uploads=queue.get(); context.visible=false;
        std::thread worker([&] {auto wrong=FrameScheduler::Render(context); Check(!wrong && std::get<ScheduleCode>(wrong.error().cause)==ScheduleCode::Context,"context-thread upload entry rejects worker");}); worker.join();
        Drain(root,*queue); State(*queue,a,AsyncAssetState::Ready); State(*queue,b,AsyncAssetState::CpuReady);
        Check(queue->LastDrain().completed==1 && queue->LastDrain().bytes==4 && queue->LastDrain().byteBudgetReached,"hard per-frame byte budget");
        Drain(root,*queue); State(*queue,b,AsyncAssetState::Ready);

        Control slow, after; slow.slow=true;
        auto timedLimits=Limits(); timedLimits.frameTime=1ns;
        auto timed=Take(AsyncUploadQueue::Create(publication,timedLimits),"time budget queue");
        auto s=Submit(*timed,slow); Queued(*timed,1); auto t=Submit(*timed,after); Queued(*timed,2);
        Drain(root,*timed); State(*timed,s,AsyncAssetState::Ready);
        Check(timed->LastDrain().timeBudgetReached && after.applied==0,"soft time budget stops after indivisible first upload");
        Drain(root,*timed); State(*timed,t,AsyncAssetState::Ready);

        Control early, removed, late; late.gate=false;
        auto batch=Take(AsyncUploadQueue::Create(publication,Limits()),"entry batch queue");
        auto e=Submit(*batch,early); Queued(*batch,1);auto removedTicket=Submit(*batch,removed);Queued(*batch,2);
        auto l=Submit(*batch,late); State(*batch,l,AsyncAssetState::Loading);
        early.queue=batch.get(); early.releaseDuringApply=&late; early.waitForLateQueue=true;early.cancelDuringApply=&removedTicket;
        Drain(root,*batch); State(*batch,e,AsyncAssetState::Ready); Queued(*batch,1);
        Check(late.applied==0 && removed.applied==0,"completion during update waits for next frame even after entry cancellation");
        Drain(root,*batch); State(*batch,l,AsyncAssetState::Ready);

        // Existing frame-version retention and GPU fence policy survive replacement
        // and deferred destroy. This queue introduces no second registry model.
        std::atomic<int> retired{}; std::atomic<bool> fenceDone{};
        Registry registry(publication); ResourceHandle handle;
        Control create, replace, failure, destroy;
        for(auto* c:{&create,&replace,&failure,&destroy}) {c->registry=&registry;c->handle=&handle;c->retired=&retired;}
        create.value=1; replace.value=2; failure.uploadFailure=true; destroy.destroy=true;
        auto resources=Take(AsyncUploadQueue::Create(publication,Limits()),"registry queue");
        auto created=Submit(*resources,create); Queued(*resources,1); Drain(root,*resources);
        Registry::Lease old;
        {auto frame=publication.BeginFrame();old=Take(registry.Acquire(frame,handle),"old version");
            Check(registry.ProtectGpuUse(frame,old,std::make_unique<Fence>(fenceDone)),"retain GPU use");}
        const auto identity=handle;
        auto replaced=Submit(*resources,replace,4,created); Queued(*resources,1);
        State(*resources,created,AsyncAssetState::Ready);
        Check(old->value==1 && old.Revision()==1 && handle==identity,"queued replacement leaves frame immutable");
        Drain(root,*resources);
        Registry::Lease current;
        {auto frame=publication.BeginFrame();current=Take(registry.Acquire(frame,handle),"new version");}
        Check(old->value==1 && current->value==2 && current.Revision()==2 && handle==identity,"replacement keeps identity and exact old version");
        auto failed=Submit(*resources,failure); Queued(*resources,1); Drain(root,*resources); State(*resources,failed,AsyncAssetState::Failed);
        auto diagnostic=Take(resources->Status(failed),"upload failure status");
        Check(diagnostic.error->code==UploadCode::UploadFailed && diagnostic.error->registry==RegistryError::Busy && diagnostic.error->detail==47,"upload typed diagnostic preserved");
        {auto frame=publication.BeginFrame();Check(Take(registry.Acquire(frame,handle),"failure rollback").Revision()==2,"failed upload leaves published version");}
        auto destroyed=Submit(*resources,destroy); Queued(*resources,1);
        {auto frame=publication.BeginFrame();Check(registry.Acquire(frame,handle),"deferred destruction visible until next update");}
        Drain(root,*resources); State(*resources,destroyed,AsyncAssetState::Ready);
        {auto frame=publication.BeginFrame();Check(!registry.Acquire(frame,handle),"destroy invalidates only during update");}
        std::thread drop([lease=std::move(old)]() mutable {lease={};});drop.join();
        current={};
        {auto publish=publication.BeginPublication();Check(registry.Collect(publish)==1 && retired==1,"worker release cannot retire GPU-fenced old version");}
        fenceDone=true;
        {auto publish=publication.BeginPublication();Check(registry.Collect(publish)==1 && retired==2,"completed fence permits owner retirement");Check(registry.Close(publish),"registry closes after queue and frame work");}
        Check(glGetError()==GL_NO_ERROR,"GL driver success");
        std::println("[PASS] async-gl scheduler/freeze/budgets/immutable-batch/typed-upload/retention");
    }
}
int main(int argc,char** argv)
{
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    std::set_terminate([]{std::fflush(nullptr);std::_Exit(86);});
    if(argc==1) CpuTests();
    RuntimeAssets::Initialize("RigidBodySimulation");
    EngineContext root;
    WindowProperties properties;properties.m_Title="Phase 49 async upload validation";
    properties.flag={WindowFlags::INVISIBLE};properties.m_Width=properties.m_Height=64;
    properties.m_MinWidth=properties.m_MinHeight=64;properties.m_IsVsync=false;
    Check(root.Initialize({properties}),"root context");
    if(argc>1 && std::strcmp(argv[1],"--worker-gl")==0) {
        std::thread worker([]{GLContextThread::RequireCurrent("async negative worker upload");});worker.join();
        return 1;
    }
    if(argc>1 && std::strcmp(argv[1],"--switch-context")==0) {
        auto secondary=Take(Window::Create(properties),"secondary negative context");
        Check(root.MainWindow()->BeginRender(),"restore original context");
        Control change;change.switchContext=secondary.get();
        auto queue=Take(AsyncUploadQueue::Create(root.SceneServices().value().publication,Limits()),"context-bound queue");
        Submit(*queue,change);Queued(*queue,1);Drain(root,*queue);
        return 1;
    }
    GlTests(root);
    std::println("[PASS] async-upload checks={}",checks.load());
}
#endif
