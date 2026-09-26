#include "Assets/Textures/AsyncTexture.h"
#include "Mesh/AsyncMesh.h"
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLContextThread.h"
#include "Renderer/FrameScheduler.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <thread>

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {
    template<class T> void Check(const T& value, const char* label) {
        if (!static_cast<bool>(value)) { std::println(stderr, "[FAIL] {}", label); std::exit(1); }
    }
    template<class T, class E> T Take(std::expected<T, E> result, const char* label) {
        if (!result) {
            if constexpr (std::same_as<E, ScheduleError>) std::println(stderr, "{}", DescribeScheduleError(result.error()));
            if constexpr (std::same_as<E, AsyncTextureError>) std::println(stderr, "{}", DescribeAsyncTextureError(result.error()));
            if constexpr (std::same_as<E, AsyncMeshError>) std::println(stderr, "{}", DescribeAsyncMeshError(result.error()));
        }
        Check(result, label); return std::move(*result);
    }
    long long Ns(Clock::duration d) { return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count(); }
    decltype(glad_glTexImage2D) textureUpload;
    decltype(glad_glBufferData) bufferUpload;
    std::size_t textures{}, buffers{};
    struct UploadTimes { long long names{}, texture{}, mipmap{}, imageQuery{}, stateQuery{}, buffer{}; } uploadTimes;
    decltype(glad_glGenTextures) textureNames;
    decltype(glad_glGenerateMipmap) generateMipmap;
    decltype(glad_glGetTexLevelParameteriv) imageQuery;
    decltype(glad_glGetIntegerv) integerQuery;
    decltype(glad_glGetFloatv) floatQuery;
    void APIENTRY ObserveNames(GLsizei count, GLuint* names) {
        const auto start = Clock::now(); textureNames(count, names); uploadTimes.names += Ns(Clock::now()-start);
    }
    void APIENTRY ObserveMipmap(GLenum target) {
        const auto start = Clock::now(); generateMipmap(target); uploadTimes.mipmap += Ns(Clock::now()-start);
    }
    void APIENTRY ObserveImageQuery(GLenum target, GLint level, GLenum name, GLint* value) {
        const auto start = Clock::now(); imageQuery(target, level, name, value); uploadTimes.imageQuery += Ns(Clock::now()-start);
    }
    void APIENTRY ObserveIntegerQuery(GLenum name, GLint* value) {
        const auto start = Clock::now(); integerQuery(name, value); uploadTimes.stateQuery += Ns(Clock::now()-start);
    }
    void APIENTRY ObserveFloatQuery(GLenum name, GLfloat* value) {
        const auto start = Clock::now(); floatQuery(name, value); uploadTimes.stateQuery += Ns(Clock::now()-start);
    }
    void APIENTRY ObserveTexture(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height,
                         GLint border, GLenum format, GLenum type, const void* data) {
        Check(GLContextThread::IsCurrentOwner(), "texture upload on context owner");
        ++textures; const auto start = Clock::now();
        textureUpload(target, level, internal, width, height, border, format, type, data);
        uploadTimes.texture += Ns(Clock::now()-start);
    }
    void APIENTRY ObserveBuffer(GLenum target, GLsizeiptr bytes, const void* data, GLenum usage) {
        Check(GLContextThread::IsCurrentOwner(), "mesh upload on context owner");
        ++buffers; const auto start = Clock::now(); bufferUpload(target, bytes, data, usage);
        uploadTimes.buffer += Ns(Clock::now()-start);
    }
    struct Sample {
        long long request{}, latency{}, maxFrame{};
        UploadTimes times;
        std::size_t frames{}, completed{}, bytes{}, textureCalls{}, bufferCalls{}, peakReserved{}, peakQueued{}, stalls{};
    };
    template<class Loader, class Request>
    Sample Measure(EngineContext& root, Loader& loader, Request request,
                   const char* kind, int sample, std::ofstream& frameCsv) {
        Sample result;
        const auto textureBefore = textures, bufferBefore = buffers;
        const auto timesBefore = uploadTimes;
        RenderContext context{*root.MainWindow(), *root.LegacyEngine().GetWindowManager()};
        context.visible = false; context.uploads = &loader.Queue();
        const auto start = Clock::now();
        const auto ticket = Take(request(), "request");
        result.request = Ns(Clock::now() - start);
        for (;;) {
            const auto frameStart = Clock::now();
            Take(FrameScheduler::Render(context), "scheduler frame");
            const auto frameEnd = Clock::now();
            const auto elapsed = Ns(frameEnd - frameStart);
            const auto drain = loader.Queue().LastDrain();
            const auto stats = loader.Queue().Stats();
            const auto status = Take(loader.Status(ticket), "status");
            if (status.state == AsyncAssetState::Failed && status.error) {
                if constexpr (std::same_as<Loader, AsyncTextureLoader>) std::println(stderr, "{}", DescribeAsyncTextureError(*status.error));
                else std::println(stderr, "{}", DescribeAsyncMeshError(*status.error));
            }
            Check(status.state != AsyncAssetState::Failed && status.state != AsyncAssetState::Cancelled, "successful publication");
            Check(!drain.failed, "no failed uploads");
            result.maxFrame = (std::max)(result.maxFrame, elapsed);
            result.completed += drain.completed; result.bytes += drain.bytes;
            result.peakReserved = (std::max)(result.peakReserved, stats.reservedBytes);
            result.peakQueued = (std::max)(result.peakQueued, stats.queuedBytes);
            result.stalls += elapsed > 16667000;
            if (sample >= 0) frameCsv << kind << ',' << sample << ',' << result.frames << ',' << int(status.state)
                << ',' << elapsed << ',' << drain.completed << ',' << drain.bytes << ',' << drain.timeBudgetReached << '\n';
            ++result.frames;
            if (status.state == AsyncAssetState::Ready) { result.latency = Ns(frameEnd - start); break; }
            Check(Clock::now() - start < 30s, "async request deadline");
            // Fixed polling cadence, outside measured scheduler CPU. Latency includes this wait.
            std::this_thread::sleep_for(1ms);
        }
        result.textureCalls = textures - textureBefore; result.bufferCalls = buffers - bufferBefore;
        result.times = {uploadTimes.names-timesBefore.names, uploadTimes.texture-timesBefore.texture,
            uploadTimes.mipmap-timesBefore.mipmap, uploadTimes.imageQuery-timesBefore.imageQuery,
            uploadTimes.stateQuery-timesBefore.stateQuery, uploadTimes.buffer-timesBefore.buffer};
        Check(result.completed == 1 && result.bytes != 0, "one actual payload published");
        Check(glGetError() == GL_NO_ERROR, "async measurement GL status");
        return result;
    }
    void Record(std::ofstream& csv, const char* kind, int sample, const Sample& s) {
        csv << kind << ',' << sample << ',' << s.request << ',' << s.latency << ',' << s.frames << ',' << s.maxFrame
            << ',' << s.stalls << ',' << s.completed << ',' << s.bytes << ',' << s.textureCalls << ',' << s.bufferCalls
            << ',' << s.peakReserved << ',' << s.peakQueued
            << ',' << s.times.names << ',' << s.times.texture << ',' << s.times.mipmap
            << ',' << s.times.imageQuery << ',' << s.times.stateQuery << ',' << s.times.buffer << '\n';
    }
    void Run(EngineContext& root) {
        std::ofstream csv("async.csv"), frames("async-frames.csv"), idle("idle-frames.csv");
        csv << "kind,sample,request_ns,ready_ns,frames,max_scheduler_ns,stalls,completed,upload_bytes,texture_calls,buffer_calls,peak_reserved_bytes,peak_queued_bytes,names_ns,texture_upload_ns,mipmap_ns,image_query_ns,state_query_ns,buffer_upload_ns\n";
        frames << "kind,sample,frame,state,scheduler_ns,completed,upload_bytes,time_budget_reached\n";
        idle << "sample,scheduler_ns\n";
        auto& publication = root.SceneServices().value().publication;
        RenderContext empty{*root.MainWindow(), *root.LegacyEngine().GetWindowManager()}; empty.visible = false;
        for (int i = -120; i < 240; ++i) {
            const auto start = Clock::now(); Take(FrameScheduler::Render(empty), "idle scheduler");
            if (i >= 0) idle << i << ',' << Ns(Clock::now()-start) << '\n';
        }
        for (const char* kind : {"texture", "mesh"}) for (int sample = -2; sample < 8; ++sample) {
            if (kind[0] == 't') {
                TextureRegistry registry(publication);
                auto loader = Take(AsyncTextureLoader::Create(publication, registry, RuntimeAssets::File("Images")), "texture loader");
                const auto measured = Measure(root, *loader, [&] { return loader->Request("Sphere/wood_diffuse"); }, kind, sample, frames);
                Check(loader->Stats().uploads == 1 && loader->Stats().decodes == 1, "one texture decode and upload");
                if (sample >= 0) Record(csv, kind, sample, measured);
                loader->Shutdown(); Check(loader->Queue().Stats().reservedBytes == 0, "texture CPU payload retired");
                { auto write = publication.BeginPublication(); Check(registry.Close(write), "texture owners retired"); }
            } else {
                MeshRegistry registry(publication);
                auto loader = Take(AsyncMeshLoader::Create(publication, registry, RuntimeAssets::File("Models")), "mesh loader");
                const auto measured = Measure(root, *loader, [&] { return loader->Request("barrel.obj"); }, kind, sample, frames);
                Check(loader->Stats().uploads == 1 && loader->Stats().imports == 1, "one mesh import and upload");
                if (sample >= 0) Record(csv, kind, sample, measured);
                loader->Shutdown(); Check(loader->Queue().Stats().reservedBytes == 0, "mesh CPU payload retired");
                { auto write = publication.BeginPublication(); Check(registry.Close(write), "mesh owners retired"); }
            }
            glFinish(); // Untimed sample isolation; never part of queue/engine scheduling.
        }
        Check(csv.good() && frames.good() && idle.good(), "measurement output");
        Check(glGetError() == GL_NO_ERROR, "retirement GL status");
    }
}
int main() {
    RuntimeAssets::Initialize("RigidBodySimulation");
    EngineContext root;
    WindowProperties properties; properties.m_Title = "Phase 65 async performance";
    properties.flag = {WindowFlags::INVISIBLE}; properties.m_IsVsync = false;
    properties.m_Width = properties.m_Height = properties.m_MinWidth = properties.m_MinHeight = 64;
    Check(root.Initialize({properties}), "context");
    std::println("GPU: {} / {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)), reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    textureUpload = glad_glTexImage2D; bufferUpload = glad_glBufferData;
    glad_glTexImage2D = ObserveTexture; glad_glBufferData = ObserveBuffer;
    textureNames=glad_glGenTextures; generateMipmap=glad_glGenerateMipmap;
    imageQuery=glad_glGetTexLevelParameteriv; integerQuery=glad_glGetIntegerv; floatQuery=glad_glGetFloatv;
    glad_glGenTextures=ObserveNames; glad_glGenerateMipmap=ObserveMipmap;
    glad_glGetTexLevelParameteriv=ObserveImageQuery; glad_glGetIntegerv=ObserveIntegerQuery; glad_glGetFloatv=ObserveFloatQuery;
    Run(root);
    glad_glTexImage2D = textureUpload; glad_glBufferData = bufferUpload;
    glad_glGenTextures=textureNames; glad_glGenerateMipmap=generateMipmap;
    glad_glGetTexLevelParameteriv=imageQuery; glad_glGetIntegerv=integerQuery; glad_glGetFloatv=floatQuery;
    std::println("[PASS] async-performance two warmups/eight samples per asset; default limits; owner-thread upload; joined retirement");
}
