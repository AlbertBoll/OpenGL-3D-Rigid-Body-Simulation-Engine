#pragma once

#include "Assets/Textures/AsyncTexture.h"
#include "Renderer/FrameScheduler.h"
#include "Renderer/SceneRenderResources.h"
#include "Core/GEngine.h"
#include "Core/Log.h"
#include "Core/RuntimeAssets.h"
#include <chrono>
#include <thread>

namespace PreEditorValidation
{
#ifdef GENGINE_ASYNC_RESOURCE_VALIDATION
    std::expected<void, ::GEngine::PlatformError> RunAsyncPerformance(::GEngine::EngineContext&);
    std::expected<void, ::GEngine::PlatformError> CheckAsyncQueue(::GEngine::EngineContext&);
#else
    inline std::expected<void, ::GEngine::PlatformError> RunAsyncPerformance(::GEngine::EngineContext&) {
        return std::unexpected(::GEngine::PlatformError{::GEngine::PlatformErrorCode::Initialization,
            "Phase 12 validation", "Use the declared validation build for this diagnostic mode"});
    }
    inline std::expected<void, ::GEngine::PlatformError> CheckAsyncQueue(::GEngine::EngineContext& root) {
        return RunAsyncPerformance(root);
    }
#endif

#ifndef GENGINE_ASYNC_RESOURCE_BACKEND
    template<class Request, class Poll, class State>
    std::expected<void, ::GEngine::PlatformError> CheckAsyncAdoption(
        ::GEngine::EngineContext& context, ::GEngine::SceneRenderResources& owner,
        ::GEngine::_Scene& scene, ::GEngine::MaterialHandle material, ::GEngine::MaterialHandle clone,
        ::GEngine::Asset::AsyncTextureLoader& loader, ::GEngine::FrameSubmission& submission,
        ::GEngine::FrameSubmissionDesc targets, ::GEngine::EntityPickTable& picks,
        const ::GEngine::FrameCamera& camera, Request request, Poll poll, State state)
    {
        using namespace ::GEngine; using namespace ::GEngine::Asset;
        using Clock = std::chrono::steady_clock;
        const auto failure = [](const char* label) {
            Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_12_FAIL {}", label);
            return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "Phase 12 async adoption", label});
        };
#define AR12_REQUIRE(value, label) if (!static_cast<bool>(value)) return failure(label)
        auto queues = CheckAsyncQueue(context); if (!queues) return queues;
        const auto independent = owner.DescribeMaterial(clone);
        AR12_REQUIRE(independent, "independent authored material");
        std::optional<RenderFrame> oldFrame, newFrame;
        {
            auto access = owner.Publication().BeginFrame(); RenderExtractionStats stats;
            auto frame = ExtractRenderFrame(scene, owner.ForFrame(access), stats, {&camera, 1});
            AR12_REQUIRE(frame, "retained old frame"); oldFrame = std::move(*frame);
        }
        const auto before = owner.DescribeMaterial(material);
        AR12_REQUIRE(before && before->textures[0], "last-valid original texture");
        const auto oldImage = before->textures[0]->texture;
        RenderContext drain{*context.MainWindow(), *context.LegacyEngine().GetWindowManager()};
        drain.visible = false; drain.uploads = &loader.Queue();
        auto settle = [&](const std::string& path, bool cancel) -> std::expected<void, PlatformError> {
            request(path, cancel);
            AR12_REQUIRE(poll(), "real RBS request/cancel caller");
            const auto start = Clock::now();
            while (state() != AsyncAssetState::Ready && state() != AsyncAssetState::Failed && state() != AsyncAssetState::Cancelled) {
                AR12_REQUIRE(FrameScheduler::Render(drain), "scheduler adoption drain");
                AR12_REQUIRE(poll(), "real RBS completion caller");
                AR12_REQUIRE(Clock::now() - start < std::chrono::seconds(30), "adoption request deadline");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return {};
        };
        AR12_REQUIRE(settle("Plane/wood_diffuse", false), "replacement completes");
        AR12_REQUIRE(state() == AsyncAssetState::Ready, "replacement ready");
        auto after = owner.DescribeMaterial(material);
        AR12_REQUIRE(after && after->textures[0] && after->textures[0]->texture != oldImage, "new image assigned");
        auto authoredOnly = *after; authoredOnly.textures = before->textures;
        AR12_REQUIRE(authoredOnly == *before, "independent authored values preserved");
        auto unchanged = owner.DescribeMaterial(clone);
        AR12_REQUIRE(unchanged && *unchanged == *independent, "clone unchanged");
        std::size_t oldDraws = 0;
        for (const auto& draw : oldFrame->Draws()) oldDraws += draw.material == material;
        AR12_REQUIRE(oldDraws >= 2, "actual shared-material scene callers");
        bool retained = false;
        for (const auto& resource : oldFrame->Resources()) if (resource.Material().Instance() == material)
            for (const auto& texture : resource.Material().Textures()) retained |= texture.texture.Identity() == oldImage;
        AR12_REQUIRE(retained, "old frame retains exact old image");
        {
            auto access = owner.Publication().BeginFrame(); RenderExtractionStats stats;
            auto frame = ExtractRenderFrame(scene, owner.ForFrame(access), stats, {&camera, 1});
            AR12_REQUIRE(frame, "new frame extraction"); newFrame = std::move(*frame);
            bool replaced = false;
            for (const auto& resource : newFrame->Resources()) if (resource.Material().Instance() == material)
                for (const auto& texture : resource.Material().Textures()) replaced |= texture.texture.Identity() == after->textures[0]->texture;
            AR12_REQUIRE(replaced, "new frame carries published image");
            targets.pipelines = owner.Pipelines();
            AR12_REQUIRE(submission.Submit(*oldFrame, targets, picks), "retained old frame submission");
            AR12_REQUIRE(submission.Submit(*newFrame, targets, picks), "new frame submission");
        }
        submission.InvalidatePassContents();
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_12_FRAMES_PASS old_new_submitted=true shared_draws={} clone_preserved=true", oldDraws);
        for (const auto* name : {"phase12-missing.png", "phase12-broken.png"}) {
            AR12_REQUIRE(settle((std::filesystem::current_path() / name).string(), false), "expected file/decode error terminates");
            AR12_REQUIRE(state() == AsyncAssetState::Failed, "typed file/decode failure");
            auto current = owner.DescribeMaterial(material);
            AR12_REQUIRE(current && *current == *after, "failure retains last-valid appearance");
        }
        AR12_REQUIRE(settle((std::filesystem::current_path() / "phase12-cancel.png").string(), true), "cancel terminates");
        AR12_REQUIRE(state() == AsyncAssetState::Cancelled, "real texture cancellation");
        auto current = owner.DescribeMaterial(material);
        AR12_REQUIRE(current && *current == *after, "cancel retains last-valid appearance");
        oldFrame.reset(); newFrame.reset();
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_12_API_PASS success=true file_decode_failure=true cancel=true late_result=true handles_values_preserved=true");
        return {};
#undef AR12_REQUIRE
    }
#else
    // Native observations live only in the owning implementation's validation build.
    // Neither this branch nor its native types enter the ordinary RBS caller.
}
#include <glad/glad.h>
#include <Windows.h>
#include <psapi.h>
#include <sdl2/SDL.h>
#include <sdl2/SDL_syswm.h>
#include <fstream>
#include <stdexcept>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#pragma comment(lib, "psapi.lib")
namespace PreEditorValidation
{
    namespace AsyncChecks
    {
        using namespace ::GEngine; using namespace ::GEngine::Asset;
        using Clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;
        long long Ns(Clock::duration value) { return std::chrono::duration_cast<std::chrono::nanoseconds>(value).count(); }
        void Need(bool value, const char* label) { if (!value) throw std::runtime_error(label); }
        template<class T, class E> T Take(std::expected<T, E> value, const char* label) {
            Need(bool(value), label); return std::move(*value);
        }
        PlatformError Failed(const char* message) {
            Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_12_FAIL {}", message);
            return {PlatformErrorCode::Initialization, "Phase 12 async validation", message};
        }
        void Environment(bool hidden, EngineContext& root) {
            const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            Need(renderer && version && std::string_view(renderer).find("RTX 3070 Laptop") != std::string_view::npos &&
                std::string_view(version).find("552.44") != std::string_view::npos, "actual NVIDIA renderer/driver admission");
            SYSTEM_POWER_STATUS power{}; Need(GetSystemPowerStatus(&power) && power.ACLineStatus == 1, "AC power admission");
            if (hidden) {
                // Permanent pre-measurement admission: preserve getter order and short-circuiting.
                const char* names[]{"hidden", "width", "height", "swap_interval"};
                const long long expected[]{1, 64, 64, 0};
                long long actual[4]{};
                bool reached[4]{}, passed[4]{};
                const auto observe = [&](unsigned index, auto value) {
                    reached[index] = true;
                    actual[index] = static_cast<long long>(value);
                    passed[index] = actual[index] == expected[index];
                    return passed[index];
                };
                const bool boundary = observe(0, root.MainWindow()->GetState().Hidden) &&
                    observe(1, root.MainWindow()->GetScreenWidth()) &&
                    observe(2, root.MainWindow()->GetScreenHeight()) &&
                    observe(3, root.MainWindow()->GetSwapInterval());
                const auto logger = Log::GetCoreLogger();
                for (unsigned i = 0; i < 4; ++i) {
                    if (!reached[i])
                        logger->info("PRE_EDITOR_PHASE_12_ADMISSION predicate={} status=UNEVALUATED expected={} actual=UNEVALUATED", names[i], expected[i]);
                    else {
                        logger->info("PRE_EDITOR_PHASE_12_ADMISSION predicate={} status={} expected={} actual={}",
                            names[i], passed[i] ? "PASS" : "FAIL", expected[i], actual[i]);
                        if (!passed[i]) logger->info("PRE_EDITOR_PHASE_12_ADMISSION_FIRST_FAILURE predicate={} expected={} actual={}",
                            names[i], expected[i], actual[i]);
                    }
                }
                logger->flush();
                Need(boundary, "64x64 hidden VSync-off boundary");
                // Backend-only identity check binds native client geometry to the main window.
                const auto rootID = root.MainWindow()->GetWindowID();
                auto* window = SDL_GetWindowFromID(rootID);
                Need(window && window == SDL_GL_GetCurrentWindow() && root.MainWindow()->IsCurrent() &&
                    SDL_GL_GetCurrentContext(), "admission main window/current context identity");
                SDL_SysWMinfo wm{}; SDL_VERSION(&wm.version);
                Need(SDL_GetWindowWMInfo(window, &wm) && wm.subsystem == SDL_SYSWM_WINDOWS,
                    "admission native window identity");
                DWORD process = 0;
                const auto ownerThread = GetWindowThreadProcessId(wm.info.win.window, &process);
                Need(process == GetCurrentProcessId() && ownerThread == GetCurrentThreadId(),
                    "admission native window owner");
                RECT client{}; Need(GetClientRect(wm.info.win.window, &client), "admission client rectangle");
                int width = 0, height = 0, drawableWidth = 0, drawableHeight = 0;
                SDL_GetWindowSize(window, &width, &height);
                SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
                const auto flags = SDL_GetWindowFlags(window);
                const auto getDpi = reinterpret_cast<UINT(WINAPI*)(HWND)>(
                    GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
                logger->info("PRE_EDITOR_PHASE_12_WINDOW_OBSERVATION root_id={} sdl_id={} hwnd={} logical={}x{} client={}x{} drawable={}x{} flags={} style={} exstyle={} dpi={} current=1",
                    rootID, SDL_GetWindowID(window), reinterpret_cast<std::uintptr_t>(wm.info.win.window),
                    width, height, client.right-client.left, client.bottom-client.top, drawableWidth, drawableHeight,
                    flags, static_cast<std::uintptr_t>(GetWindowLongPtrW(wm.info.win.window, GWL_STYLE)),
                    static_cast<std::uintptr_t>(GetWindowLongPtrW(wm.info.win.window, GWL_EXSTYLE)),
                    getDpi ? getDpi(wm.info.win.window) : 0);
                logger->flush();
                Need(width == 64 && height == 64 && client.right-client.left == 64 && client.bottom-client.top == 64 &&
                    (flags & SDL_WINDOW_HIDDEN) && (flags & SDL_WINDOW_BORDERLESS) && !IsWindowVisible(wm.info.win.window),
                    "actual diagnostic logical/client64 hidden borderless boundary");
                logger->info("PRE_EDITOR_PHASE_12_WINDOW_BOUNDARY_PASS");
            }
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_12_ENVIRONMENT_PASS renderer={} version={} ac=1 hidden={}", renderer, version, hidden);
        }
        struct ControlledUpload final : UploadRequest {
            std::atomic<unsigned>& applications;
            explicit ControlledUpload(std::atomic<unsigned>& n) : applications(n) {}
            std::size_t Bytes() const noexcept override { return 16; }
            UploadResult Apply(const AssetPublication::Publication&) const noexcept override { ++applications; return {}; }
        };
        struct ControlledJob final : AssetDecodeJob {
            std::atomic<bool>& entered; std::atomic<bool>& proceed; std::atomic<unsigned>& applications;
            ControlledJob(std::atomic<bool>& e, std::atomic<bool>& p, std::atomic<unsigned>& a) : entered(e), proceed(p), applications(a) {}
            std::expected<std::unique_ptr<const UploadRequest>, UploadError> Decode(UploadCancellation cancel, std::size_t) const noexcept override {
                entered = true;
                while (!proceed.load() && !cancel.StopRequested()) std::this_thread::sleep_for(1ms);
                // Intentionally returns a late CPU payload after cancellation; queue must discard it.
                return std::unique_ptr<const UploadRequest>(new(std::nothrow) ControlledUpload(applications));
            }
        };
        template<class Predicate> void Until(Predicate predicate, const char* label) {
            const auto start = Clock::now();
            while (!predicate()) { Need(Clock::now()-start < 5s, label); std::this_thread::sleep_for(1ms); }
        }
        void QueueCases(EngineContext& root) {
            AssetPublication publication; UploadLimits limits;
            limits.requests = 2; limits.queuedRequests = 1; limits.decodedPayloads = 1; limits.observeStages = true;
            auto queue = Take(AsyncUploadQueue::Create(publication, limits), "controlled queue");
            std::atomic<bool> entered{}, proceed{}; std::atomic<unsigned> applications{};
            auto job = [&]() -> std::unique_ptr<const AssetDecodeJob> { return std::make_unique<ControlledJob>(entered, proceed, applications); };
            auto firstJob = job(); const auto cancelled = Take(queue->Submit(firstJob, 16), "controlled cancelled ticket");
            Until([&]{return entered.load();}, "worker entry deadline");
            auto excess = job(); auto full = queue->Submit(excess, 16);
            Need(!full && full.error().code == UploadCode::Capacity && bool(excess), "typed capacity preserves job");
            Need(bool(queue->Cancel(cancelled)), "cancel busy decoder"); proceed = true;
            Until([&]{return queue->Stats().reservedBytes == 0;}, "cancelled late payload retired");
            Need(Take(queue->Status(cancelled), "cancel status").state == AsyncAssetState::Cancelled && applications == 0, "late payload not applied");
            Need(bool(queue->Release(cancelled)) && !queue->Observation(cancelled), "stale observation identity");
            auto successJob = job(); const auto ticket = Take(queue->Submit(successJob, 16), "controlled success ticket");
            RenderContext context{*root.MainWindow(), *root.LegacyEngine().GetWindowManager()}; context.visible = false; context.uploads = queue.get();
            Until([&]{ Take(FrameScheduler::Render(context), "controlled scheduler"); return Take(queue->Status(ticket), "controlled status").state == AsyncAssetState::Ready; }, "success deadline");
            const auto observed = Take(queue->Observation(ticket), "controlled observation");
            Need(observed.enabled && observed.admitted <= observed.decodeStarted && observed.decodeStarted <= observed.decodeFinished &&
                observed.decodeFinished <= observed.enqueued && observed.enqueued <= observed.uploadStarted &&
                observed.uploadStarted <= observed.uploadFinished && observed.uploadFinished != 0 && observed.payloadBytes == 16 && applications == 1,
                "stage order/payload/exactly-once");
            Need(queue->LastDrain().elapsed.count() > 0, "complete drain timing available");
            queue->Shutdown(); Need(queue->Stats().reservedPayloads == 0 && queue->Stats().queuedBytes == 0, "joined CPU retirement");
            Need(!queue->Submit(excess, 16) && bool(excess), "shutdown admission closed");
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_12_QUEUE_PASS cancellation_late=true capacity_typed=true stages_ordered=true joined=true");
        }
        decltype(glad_glTexImage2D) textureUpload{};
        decltype(glad_glBufferData) bufferUpload{};
        std::size_t textures{}, buffers{}, imageBytes{}, bufferBytes{};
        void APIENTRY Image(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* data) {
            ++textures; imageBytes += std::size_t(w)*h*4;
            textureUpload(target, level, internal, w, h, border, format, type, data);
        }
        void APIENTRY Buffer(GLenum target, GLsizeiptr bytes, const void* data, GLenum usage) {
            ++buffers; bufferBytes += std::size_t(bytes); bufferUpload(target, bytes, data, usage);
        }
        struct Hooks {
            Hooks() { textureUpload=glad_glTexImage2D; bufferUpload=glad_glBufferData; glad_glTexImage2D=Image; glad_glBufferData=Buffer; }
            ~Hooks() { glad_glTexImage2D=textureUpload; glad_glBufferData=bufferUpload; }
        };
        struct Sample {
            long long request{}, ready{}, maximum{}, drain{};
            std::size_t frames{}, stalls{}, completed{}, payload{}, peakReserved{}, peakQueued{}, textureCalls{}, bufferCalls{}, image{}, buffer{};
            UploadObservation queue;
            AsyncTextureObservation texture;
            PROCESS_MEMORY_COUNTERS_EX memory{};
        };
        template<class Loader, class Request> Sample Measure(EngineContext& root, Loader& loader, Request request,
            const char* kind, int sample, bool observed, std::ofstream& frames) {
            Sample s; const auto t=textures,b=buffers,ib=imageBytes,bb=bufferBytes;
            RenderContext context{*root.MainWindow(), *root.LegacyEngine().GetWindowManager()}; context.visible=false; context.uploads=&loader.Queue();
            const auto start=Clock::now(); const auto ticket=Take(request(), "exact async request"); s.request=Ns(Clock::now()-start);
            for (;;) {
                const auto begin=Clock::now(); Take(FrameScheduler::Render(context), "measured scheduler"); const auto end=Clock::now();
                const auto elapsed=Ns(end-begin); const auto d=loader.Queue().LastDrain(); const auto q=loader.Queue().Stats();
                const auto status=Take(loader.Status(ticket), "measured ticket status");
                Need(status.state!=AsyncAssetState::Failed && status.state!=AsyncAssetState::Cancelled && !d.failed,"unexpected async failure");
                s.maximum=(std::max)(s.maximum,elapsed); s.drain=(std::max)(s.drain,d.elapsed.count());
                s.completed+=d.completed; s.payload+=d.bytes; s.stalls+=elapsed>16667000;
                s.peakReserved=(std::max)(s.peakReserved,q.reservedBytes); s.peakQueued=(std::max)(s.peakQueued,q.queuedBytes);
                Need(q.reservedBytes<=256u*1024*1024 && q.queuedBytes<=128u*1024*1024,"memory bounds");
                frames<<kind<<','<<sample<<','<<s.frames++<<','<<int(status.state)<<','<<elapsed<<','<<d.elapsed.count()<<','<<d.completed<<','<<d.bytes<<','<<d.timeBudgetReached<<'\n';
                Need(kind[0]!='m' || elapsed<=16667000,"new mesh stall exceeds unchanged limit");
                if(status.state==AsyncAssetState::Ready) {s.ready=Ns(end-start);break;}
                Need(Clock::now()-start<30s,"request deadline");std::this_thread::sleep_for(1ms);
            }
            s.queue=Take(loader.Queue().Observation(ticket),"per-ticket observation");
            if constexpr(std::same_as<Loader,AsyncTextureLoader>) s.texture=Take(loader.Observation(ticket),"texture observation");
            s.textureCalls=textures-t;s.bufferCalls=buffers-b;s.image=imageBytes-ib;s.buffer=bufferBytes-bb;
            s.memory.cb=sizeof(s.memory);Need(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&s.memory),sizeof(s.memory)),"process memory sample");
            Need(s.completed==1 && s.payload==(kind[0]=='t'?16777232u:288392u),"original exact payload and one publication");
            Need(kind[0]=='t' ? s.textureCalls==1 && s.bufferCalls==0 && s.image==16777216u : s.textureCalls==0 && s.bufferCalls==2,"original upload calls/bytes");
            Need(s.queue.enabled==observed && (!observed || (s.queue.admitted && s.queue.decodeFinished<=s.queue.enqueued && s.queue.enqueued<=s.queue.uploadStarted && s.queue.uploadFinished>=s.queue.uploadStarted)),"queue stage completeness");
            if constexpr(std::same_as<Loader,AsyncTextureLoader>) Need(!observed || (s.texture.enabled && s.texture.encodedBytes==7712582u && s.texture.pixelBytes==16777216u && s.texture.peakRequestBytes<=64u*1024*1024 && s.texture.read.count()>0 && s.texture.decode.count()>0 && s.texture.upload.count()>0 && s.texture.publication.count()>0),"texture stage completeness");
            Need(glGetError()==GL_NO_ERROR,"async GL status");return s;
        }
        void Record(std::ofstream& out,const char* kind,int sample,bool observed,const Sample& s) {
            auto metric=[&](long long value,bool available){if(available)out<<value;else out<<"null";out<<',';};
            out<<kind<<','<<sample<<','<<s.request<<','<<s.ready<<','<<s.maximum<<','<<s.frames<<','<<s.stalls<<','<<s.completed<<','<<s.payload<<','<<s.textureCalls<<','<<s.bufferCalls<<','<<s.image<<','<<s.buffer<<','<<s.peakReserved<<','<<s.peakQueued<<',';
            metric(s.queue.decodeStarted-s.queue.admitted,observed);metric(s.queue.decodeFinished-s.queue.decodeStarted,observed);
            metric(s.queue.enqueued-s.queue.decodeFinished,observed);metric(s.queue.uploadStarted-s.queue.enqueued,observed);
            metric(s.queue.uploadFinished-s.queue.uploadStarted,observed);metric(s.drain,observed);
            const bool texture=observed&&kind[0]=='t';
            metric(s.texture.read.count(),texture);metric(s.texture.decode.count(),texture);metric(s.texture.upload.count(),texture);metric(s.texture.publication.count(),texture);
            metric(s.texture.peakRequestBytes,texture);
            out<<s.memory.PrivateUsage<<','<<s.memory.WorkingSetSize<<','<<(kind[0]=='t'?22369620u:s.buffer)<<','<<(s.stalls?"FAIL":"PASS")<<'\n';out.flush();
        }
        void Performance(EngineContext& root) {
            Environment(true,root);
            if (std::getenv("GENGINE_PRE_EDITOR_ASYNC_ADMISSION_ONLY")) {
                // Separate, untimed preflight process; batch warmup and samples stay unchanged.
                RenderContext empty{*root.MainWindow(), *root.LegacyEngine().GetWindowManager()};
                empty.visible = false;
                const auto frame = Take(FrameScheduler::Render(empty), "admission hidden scheduler/presentation");
                unsigned presents = 0, passes = 0;
                for (const auto& event : frame.trace.Events()) if (event.stage == FrameStage::Pass) {
                    ++passes; presents += event.pass == RenderPass::Present;
                }
                Need(presents == 1 && passes == 1 && glGetError() == GL_NO_ERROR,
                    "admission hidden presentation trace/GL status");
                Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_12_PREFLIGHT_PRESENT_PASS count=1");
                Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_12_ADMISSION_ONLY_COMPLETE");
                Log::GetCoreLogger()->flush();
                return; // No hooks, CSVs, warmup or async requests in preflight.
            }
            Hooks hooks;
            const bool observed=std::string_view(std::getenv("GENGINE_PRE_EDITOR_ASYNC_PERFORMANCE"))=="ON";
            std::ofstream out("async.csv"),frames("async-frames.csv"),idle("idle-frames.csv");
            out<<"kind,sample,request_ns,ready_ns,max_scheduler_ns,frames,stalls,completed,payload_bytes,texture_calls,buffer_calls,image_bytes,buffer_bytes,peak_reserved_bytes,peak_queued_bytes,worker_wait_ns,worker_decode_total_ns,enqueue_wait_ns,queue_wait_ns,apply_total_ns,max_drain_ns,texture_read_ns,texture_decode_ns,texture_upload_ns,texture_publication_ns,texture_peak_bytes,process_private_bytes,working_set_bytes,requested_gpu_storage_bytes,result\n";
            frames<<"kind,sample,frame,state,scheduler_ns,drain_ns,completed,payload_bytes,time_budget_reached\n";idle<<"sample,scheduler_ns\n";
            auto& publication=root.SceneServices().value().publication;
            RenderContext empty{*root.MainWindow(),*root.LegacyEngine().GetWindowManager()};empty.visible=false;
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_12_PROTOCOL_ADMITTED texture=Sphere/wood_diffuse.png mesh=barrel.obj window=64x64 swap=true workers=2 request_bytes=67108864 frame_bytes=67108864 observer={}",observed);
            for(int i=-120;i<240;++i){const auto start=Clock::now();Take(FrameScheduler::Render(empty),"idle scheduler");if(i>=0)idle<<i<<','<<Ns(Clock::now()-start)<<'\n';}
            for(const char* kind:{"texture","mesh"})for(int sample=-2;sample<8;++sample){
                if(kind[0]=='t'){
                    TextureRegistry registry(publication);AsyncTextureLimits limits;limits.queue.observeStages=observed;
                    auto loader=Take(AsyncTextureLoader::Create(publication,registry,Take(RuntimeAssets::TryFile("Images"), "image root"),limits),"fresh texture loader");
                    auto s=Measure(root,*loader,[&]{return loader->Request("Sphere/wood_diffuse");},kind,sample,observed,frames);
                    Need(loader->Stats().uploads==1&&loader->Stats().decodes==1,"one texture decode/upload");Record(out,kind,sample,observed,s);
                    loader->Shutdown();Need(loader->Queue().Stats().reservedBytes==0,"texture CPU retirement");
                    {auto access=publication.BeginPublication();Need(bool(registry.Close(access)),"texture registry retirement");}
                }else{
                    MeshRegistry registry(publication);AsyncMeshLimits limits;limits.queue.observeStages=observed;
                    auto loader=Take(AsyncMeshLoader::Create(publication,registry,Take(RuntimeAssets::TryFile("Models"), "model root"),limits),"fresh mesh loader");
                    auto s=Measure(root,*loader,[&]{return loader->Request("barrel.obj");},kind,sample,observed,frames);
                    Need(loader->Stats().uploads==1&&loader->Stats().imports==1,"one mesh import/upload");Record(out,kind,sample,observed,s);
                    loader->Shutdown();Need(loader->Queue().Stats().reservedBytes==0,"mesh CPU retirement");
                    {auto access=publication.BeginPublication();Need(bool(registry.Close(access)),"mesh registry retirement");}
                }
                glFinish(); // Historical untimed isolation after sample owners retire, identical OFF/ON.
            }
            Need(out.good()&&frames.good()&&idle.good()&&glGetError()==GL_NO_ERROR,"measurement output/retirement");
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_12_PERFORMANCE_COMPLETE samples=16 warmups=4 mesh_substages=unavailable_by_accepted_amendment");
        }
    }
    std::expected<void, ::GEngine::PlatformError> RunAsyncPerformance(::GEngine::EngineContext& root) {
        try { AsyncChecks::Performance(root);return {}; }
        catch(const std::exception& error){return std::unexpected(AsyncChecks::Failed(error.what()));}
    }
    std::expected<void, ::GEngine::PlatformError> CheckAsyncQueue(::GEngine::EngineContext& root) {
        try { AsyncChecks::Environment(false,root);AsyncChecks::QueueCases(root);return {}; }
        catch(const std::exception& error){return std::unexpected(AsyncChecks::Failed(error.what()));}
    }
#endif
}
