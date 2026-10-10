#include "gepch.h"
#include "Core/RenderBaseline.h"
#include "Core/GEngine.h"
#include "Core/LaunchConfig.h"
#ifdef GENGINE_RENDER_BASELINE
#include "Core/GLContextThread.h"
#include "Core/RenderCounters.h"
#include "Core/RenderTarget.h"
#include "Core/Window.h"
#include "Physics/PhysicsProfile.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <format>
#include <new>
#include <span>

namespace GEngine::RenderBaseline
{
    using Clock = std::chrono::steady_clock;
    namespace { Clock::time_point startup; }
    namespace
    {
        const EngineLaunchConfig* ActiveLaunch()
        {
            const auto* root = EngineContext::TryGet();
            return root ? root->LaunchConfiguration() : nullptr;
        }
    }
    bool Requested()
    {
        if (const auto* launch = ActiveLaunch())
            return launch->baselineOutput.has_value();
        return SDL_getenv("GENGINE_BASELINE_OUTPUT") != nullptr;
    }
    void Configure(WindowProperties& properties)
    {
        if (!Requested()) return;
        startup = Clock::now();
        properties.m_Width = 1280;
        properties.m_Height = 720;
        properties.m_IsVsync = false;
    }
    void Configure(WindowProperties& properties, const EngineLaunchConfig& launch)
    {
        if (!launch.baselineOutput)
            return;
        startup = Clock::now();
        properties.m_Width = 1280;
        properties.m_Height = 720;
        properties.m_IsVsync = false;
    }
    std::string DescribeContext(const Error& error)
    {
        auto context = error.context;
        if (error.system) context += std::format("; system-category={}; system-code={}; system-message={}",
            error.system.category().name(), error.system.value(), error.system.message());
        if (error.framebuffer) {
            const auto& f = *error.framebuffer;
            context += std::format("; framebuffer-code={}; width={}; height={}; samples={}; framebuffer-message={}",
                static_cast<unsigned>(f.code), f.width, f.height, f.samples, f.message);
        }
        return context;
    }
    namespace Detail
    {
    struct State;
    thread_local State* active = nullptr;
    struct State
    {
        static constexpr unsigned Warmup = Session::Warmup, Samples = Session::Samples;
        static Error Failure(ErrorCode code, const char* operation, std::string context, std::string message)
        { return {code, operation, std::move(context), std::move(message)}; }
        Error FileError(ErrorCode code, const char* name, const char* message) const
        { return Failure(code, "baseline output", (output / name).string(), message); }
        Result Initialize()
        {
            if (!RenderCounters::Enabled || !IsPhysicsProfilingEnabled())
                return std::unexpected(Failure(ErrorCode::Capability, "baseline startup", {}, "Baseline requires consistently enabled render/Physics counters"));
            const auto* launch = ActiveLaunch();
            output = std::filesystem::path(launch ? launch->baselineOutput->c_str()
                                                  : SDL_getenv("GENGINE_BASELINE_OUTPUT"));
            std::error_code system;
            const bool directory = std::filesystem::is_directory(output, system);
            if (!output.is_absolute() || !directory || system) {
                auto error = Failure(ErrorCode::InvalidOutput, "baseline startup", output.string(),
                    "GENGINE_BASELINE_OUTPUT must name an existing absolute directory");
                error.system = system;
                return std::unexpected(std::move(error));
            }
            context = SDL_GL_GetCurrentContext();
            window = SDL_GL_GetCurrentWindow();
            if (!context || !window)
                return std::unexpected(Failure(ErrorCode::ContextUnavailable, "baseline startup", {}, "Baseline requires a current drawable context"));
            GLContextThread::RequireCurrent("baseline startup");
            owner = std::this_thread::get_id();
            csv.open(output / "frames.csv", std::ios::out | std::ios::trunc);
            if (!csv) return std::unexpected(FileError(ErrorCode::OutputOpen, "frames.csv", "Cannot open baseline output"));
            metadata.open(output / "runtime.txt", std::ios::out | std::ios::trunc);
            if (!metadata) return std::unexpected(FileError(ErrorCode::OutputOpen, "runtime.txt", "Cannot open baseline output"));
            if (SDL_GL_SetSwapInterval(0) != 0 || SDL_GL_GetSwapInterval() != 0)
                return std::unexpected(Failure(ErrorCode::SwapInterval, "baseline startup", SDL_GetError(), "Baseline requires confirmed VSYNC=0"));
            SDL_GL_GetDrawableSize(window, &width, &height);
            if (width != 1280 || height != 720)
                return std::unexpected(DimensionError("baseline startup", width, height));
            metadata << "protocol=phase13-frozen-v1\nwidth=" << width << "\nheight=" << height
                << "\nvsync=" << SDL_GL_GetSwapInterval() << "\nwarmup=" << Warmup << "\nsamples=" << Samples
                << "\nGL_VENDOR=" << glGetString(GL_VENDOR) << "\nGL_RENDERER=" << glGetString(GL_RENDERER)
                << "\nGL_VERSION=" << glGetString(GL_VERSION) << "\nGLSL=" << glGetString(GL_SHADING_LANGUAGE_VERSION)
                << "\nstartup_ns=" << Nanoseconds(startup, Clock::now())
                << "\nGPU_time=unavailable; no engine GPU timer exists at this baseline"
                << "\ncolor=raw default-framebuffer RGBA8; no added color conversion"
                << "\nphysics=frozen zero-delta update; measured step count must be zero\n";
            GLint samples = 0, srgb = 0;
            glGetIntegerv(GL_SAMPLES, &samples);
            srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
            metadata << "default_samples=" << samples << "\nframebuffer_srgb_enabled=" << srgb << '\n';
            csv << "sample,frame_ns,work_ns,input_ns,update_ns,render_ns,physics_ns,physics_steps,draws,indexed_draws,triangles,program_binds,vao_binds,texture_binds,sampler_binds,fbo_binds,buffer_upload_calls,buffer_upload_bytes,buffer_allocations,readbacks,readback_ns,shadow_passes,picking_passes,target_reallocations,buffers,vaos,textures,samplers,fbos,rbos,shaders,programs,estimated_buffer_bytes\n";
            if (!csv) return std::unexpected(FileError(ErrorCode::OutputWrite, "frames.csv", "Cannot write baseline header"));
            if (!metadata) return std::unexpected(FileError(ErrorCode::OutputWrite, "runtime.txt", "Cannot write baseline metadata"));
            return {};
        }
        Result RequireContext(const char* operation) const
        {
            GLContextThread::RequireOwner(owner, operation);
            if (SDL_GL_GetCurrentContext() != context || SDL_GL_GetCurrentWindow() != window)
                return std::unexpected(Failure(ErrorCode::ContextUnavailable, operation, {}, "Baseline owning drawable context is not current"));
            GLContextThread::RequireCurrent(operation);
            return {};
        }
        static Error DimensionError(const char* operation, int w, int h)
        { return Failure(ErrorCode::Dimensions, operation, std::format("width={}; height={}", w, h), "Baseline requires an unchanged 1280x720 drawable"); }
        std::expected<bool, Error> End()
        {
            if (failure) return std::unexpected(*failure);
            if (finished) return true;
            const auto end = Clock::now();
            if (auto current = RequireContext("baseline frame"); !current) return std::unexpected(current.error());
            const auto physics = GetPhysicsProfileSnapshot();
            if (physics.stepCount != 0)
                return std::unexpected(Failure(ErrorCode::PhysicsAdvanced, "baseline frame", std::format("steps={}", physics.stepCount), "Frozen baseline unexpectedly stepped Physics"));
            int currentWidth = 0, currentHeight = 0;
            SDL_GL_GetDrawableSize(window, &currentWidth, &currentHeight);
            if (currentWidth != width || currentHeight != height)
                return std::unexpected(DimensionError("baseline frame", currentWidth, currentHeight));
            if (SDL_GL_GetSwapInterval() != 0)
                return std::unexpected(Failure(ErrorCode::SwapInterval, "baseline frame", {}, "Baseline VSYNC changed during collection"));
            if (frame >= Warmup && frame < Warmup + Samples)
            {
                const auto snapshot = RenderCounters::LastFrame();
                const auto& f = snapshot.frame;
                csv << frame - Warmup << ',' << Nanoseconds(frameStart, end) << ',' << Nanoseconds(workStart, end)
                    << ',' << Nanoseconds(workStart, inputEnd) << ',' << Nanoseconds(inputEnd, updateEnd)
                    << ',' << Nanoseconds(updateEnd, end) << ',' << physics.physicsWorldTimeNs << ',' << physics.stepCount
                    << ',' << f.draws << ',' << f.indexedDraws << ',' << f.submittedTriangles
                    << ',' << f.programBinds << ',' << f.vaoBinds << ',' << f.textureBinds << ',' << f.samplerBinds << ',' << f.framebufferBinds
                    << ',' << f.bufferUploadCalls << ',' << f.bufferUploadBytes << ',' << f.bufferAllocationCalls
                    << ',' << f.readbackCalls << ',' << f.readbackCpuNanoseconds << ',' << f.shadowPasses << ',' << f.pickingPasses
                    << ',' << f.targetReallocations;
                for (auto count : snapshot.liveNames) csv << ',' << count;
                csv << ',' << snapshot.estimatedBufferBytes << '\n';
                if (!csv) return std::unexpected(FileError(ErrorCode::OutputWrite, "frames.csv", "Cannot write baseline sample"));
            }
            // One extra untimed frame captures the backbuffer through SwapBuffer.
            if (++frame > Warmup + Samples)
            {
                if (!captured) return std::unexpected(Failure(ErrorCode::CaptureMissing, "baseline completion", {}, "Baseline diagnostic image was not captured"));
                csv.close();
                if (!csv) return std::unexpected(FileError(ErrorCode::OutputWrite, "frames.csv", "Cannot finish baseline samples"));
                metadata << "completed_samples=" << Samples << '\n';
                metadata.close();
                if (!metadata) return std::unexpected(FileError(ErrorCode::OutputWrite, "runtime.txt", "Cannot finish baseline metadata"));
                finished = true;
                return true;
            }
            return false;
        }
        Result CaptureScene(const RenderTarget* target)
        {
            if (failure) return std::unexpected(*failure);
            if (frame != Warmup + Samples)
                return {};
            const auto* launch = ActiveLaunch();
            const bool requested = launch ? launch->baselineSceneTarget
                                          : SDL_getenv("GENGINE_BASELINE_SCENE_TARGET") != nullptr;
            if (!requested)
                return {};
            if (auto current = RequireContext("baseline scene capture"); !current) return current;
            if (!target || target->GetWidth() != width || target->GetHeight() != height)
                return std::unexpected(Failure(ErrorCode::SceneTarget, "baseline scene capture", {}, "Diagnostic scene target has unexpected dimensions"));
            const auto bytes = static_cast<size_t>(width) * height * 4;
            std::unique_ptr<unsigned char[]> pixels(new (std::nothrow) unsigned char[bytes]);
            if (!pixels) return std::unexpected(Failure(ErrorCode::Allocation, "baseline scene capture", {}, "Cannot allocate diagnostic pixels"));
            if (auto read = target->ReadColor(std::as_writable_bytes(std::span(pixels.get(), bytes))); !read) {
                auto error = Failure(ErrorCode::CaptureRead, "baseline scene capture", {}, "Cannot read diagnostic scene target");
                error.framebuffer = read.error();
                return std::unexpected(std::move(error));
            }
            if (auto saved = SaveImage(std::span(pixels.get(), bytes), width, height, "scene.bmp"); !saved) return saved;
            metadata << "scene_diagnostic=resolved application render target; presentation defect retained\n";
            if (!metadata) return std::unexpected(FileError(ErrorCode::OutputWrite, "runtime.txt", "Cannot write scene metadata"));
            return {};
        }
        Result Capture()
        {
            if (frame != Warmup + Samples || captured) return {};
            if (auto current = RequireContext("baseline capture"); !current) return current;
            // Preserve the known rendering-error inventory before isolated diagnostic queries.
            for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
                metadata << "pre_capture_gl_error=" << error << '\n';
            if (!metadata) return std::unexpected(FileError(ErrorCode::OutputWrite, "runtime.txt", "Cannot write capture metadata"));
            if (auto recorded = RecordTargets(); !recorded) return recorded;
            int w = 0, h = 0;
            SDL_GL_GetDrawableSize(window, &w, &h);
            if (w != width || h != height) return std::unexpected(DimensionError("baseline capture", w, h));
            const auto bytes = static_cast<size_t>(w) * h * 4;
            std::unique_ptr<unsigned char[]> pixels(new (std::nothrow) unsigned char[bytes]);
            if (!pixels) return std::unexpected(Failure(ErrorCode::Allocation, "baseline capture", {}, "Cannot allocate diagnostic pixels"));
            GLint readFramebuffer = 0, readBuffer = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
            glGetIntegerv(GL_READ_BUFFER, &readBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
            {
                PackState pack;
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
            glReadBuffer(readBuffer);
            if (const auto error = glGetError(); error != GL_NO_ERROR)
                return std::unexpected(Failure(ErrorCode::CaptureRead, "baseline capture", std::format("driver-code={}", error), "Baseline capture/resource query generated a GL error"));
            if (auto saved = SaveImage(std::span(pixels.get(), bytes), w, h, "diagnostic.bmp"); !saved) return saved;
            captured = true;
            return {};
        }
        struct PackState
        {
            GLint buffer = 0;
            std::array<GLint, 4> values{};
            static constexpr std::array<GLenum, 4> keys = { GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS };
            PackState()
            {
                glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &buffer);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                for (size_t i = 0; i < keys.size(); ++i)
                {
                    glGetIntegerv(keys[i], &values[i]);
                    glPixelStorei(keys[i], i == 0 ? 1 : 0);
                }
            }
            ~PackState()
            {
                for (size_t i = 0; i < keys.size(); ++i) glPixelStorei(keys[i], values[i]);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
            }
        };
        Result SaveImage(std::span<unsigned char> pixels, int w, int h, const char* name)
        {
            const auto stride = static_cast<size_t>(w) * 4;
            for (int y = 0; y < h / 2; ++y)
                for (size_t x = 0; x < stride; ++x)
                    std::swap(pixels[static_cast<size_t>(y) * stride + x], pixels[static_cast<size_t>(h - y - 1) * stride + x]);
            std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
                SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
            if (!surface) return std::unexpected(Failure(ErrorCode::CaptureSurface, "baseline image", (output / name).string(), SDL_GetError()));
            if (SDL_SaveBMP(surface.get(), (output / name).string().c_str()) != 0)
                return std::unexpected(Failure(ErrorCode::CaptureWrite, "baseline image", (output / name).string(), SDL_GetError()));
            return {};
        }
        Result RecordTargets()
        {
            std::ofstream targets(output / "targets.txt");
            if (!targets) return std::unexpected(FileError(ErrorCode::OutputOpen, "targets.txt", "Cannot open baseline resource inventory"));
            for (const auto& [key, bytes] : RenderCounters::Detail::state.names)
            {
                (void)bytes;
                if (std::get<0>(key) != reinterpret_cast<std::uintptr_t>(SDL_GL_GetCurrentContext())
                    || std::get<1>(key) != RenderCounters::Resource::Framebuffer) continue;
                const GLuint framebuffer = std::get<2>(key);
                if (!glIsFramebuffer(framebuffer)) continue;
                for (GLenum attachment : { GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT })
                {
                    GLint type = 0, name = 0, w = 0, h = 0, depth = 0, format = 0, sampleCount = 0;
                    glGetNamedFramebufferAttachmentParameteriv(framebuffer, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
                    if (type == GL_NONE) continue;
                    glGetNamedFramebufferAttachmentParameteriv(framebuffer, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
                    if (type == GL_TEXTURE)
                    {
                        GLint level = 0;
                        glGetNamedFramebufferAttachmentParameteriv(framebuffer, attachment, GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &level);
                        glGetTextureLevelParameteriv(name, level, GL_TEXTURE_WIDTH, &w);
                        glGetTextureLevelParameteriv(name, level, GL_TEXTURE_HEIGHT, &h);
                        glGetTextureLevelParameteriv(name, level, GL_TEXTURE_DEPTH, &depth);
                        glGetTextureLevelParameteriv(name, level, GL_TEXTURE_INTERNAL_FORMAT, &format);
                        glGetTextureLevelParameteriv(name, level, GL_TEXTURE_SAMPLES, &sampleCount);
                    }
                    else if (type == GL_RENDERBUFFER)
                    {
                        glGetNamedRenderbufferParameteriv(name, GL_RENDERBUFFER_WIDTH, &w);
                        glGetNamedRenderbufferParameteriv(name, GL_RENDERBUFFER_HEIGHT, &h);
                        glGetNamedRenderbufferParameteriv(name, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
                        glGetNamedRenderbufferParameteriv(name, GL_RENDERBUFFER_SAMPLES, &sampleCount);
                        depth = 1;
                    }
                    targets << "framebuffer=" << framebuffer << " attachment=" << attachment << " type=" << type
                        << " width=" << w << " height=" << h << " depth_or_layers=" << depth
                        << " samples=" << sampleCount << " internal_format=" << format << '\n';
                }
            }
            targets.close();
            if (!targets) return std::unexpected(FileError(ErrorCode::OutputWrite, "targets.txt", "Cannot write baseline resource inventory"));
            return {};
        }
        static std::int64_t Nanoseconds(Clock::time_point from, Clock::time_point to)
        { return std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count(); }
        std::filesystem::path output;
        std::ofstream csv, metadata;
        Clock::time_point frameStart, workStart, inputEnd, updateEnd;
        unsigned frame = 0;
        int width = 0, height = 0;
        bool captured = false, finished = false;
        SDL_GLContext context = nullptr;
        SDL_Window* window = nullptr;
        std::thread::id owner;
        std::optional<Error> failure;
    };
    }
    Session::Session(std::unique_ptr<Detail::State> state) : m_State(std::move(state)) {}
    Session::Session(Session&&) noexcept = default;
    Session::~Session()
    {
        if (m_State) GLContextThread::RequireOwner(m_State->owner, "baseline session destruction");
        if (Detail::active == m_State.get()) Detail::active = nullptr;
    }
    std::expected<Session, Error> Session::Create()
    {
        if (!Requested()) return Session{nullptr};
        if (Detail::active) return std::unexpected(Detail::State::Failure(ErrorCode::AlreadyActive,
            "baseline startup", {}, "A baseline session is already active on this thread"));
        std::unique_ptr<Detail::State> state(new (std::nothrow) Detail::State);
        if (!state) return std::unexpected(Detail::State::Failure(ErrorCode::Allocation, "baseline startup", {}, "Cannot allocate baseline session"));
        if (auto initialized = state->Initialize(); !initialized) return std::unexpected(initialized.error());
        Detail::active = state.get();
        return Session{std::move(state)};
    }
    bool Session::Enabled() const { return m_State != nullptr; }
    void Session::Begin() { if (Enabled()) { m_State->frameStart = Clock::now(); ResetPhysicsProfile(); } }
    void Session::Work() { if (Enabled()) m_State->workStart = Clock::now(); }
    void Session::UpdatedInput() { if (Enabled()) m_State->inputEnd = Clock::now(); }
    void Session::Updated() { if (Enabled()) m_State->updateEnd = Clock::now(); }
    std::expected<bool, Error> Session::End() { return Enabled() ? m_State->End() : std::expected<bool, Error>{false}; }
    Result Session::CaptureScene(const RenderTarget* target) { return Enabled() ? m_State->CaptureScene(target) : Result{}; }
    void Capture()
    {
        if (auto* state = Detail::active; state && !state->failure)
            if (auto captured = state->Capture(); !captured) state->failure = std::move(captured.error());
    }
}
#endif
