#pragma once

// Opt-in Phase 13 measurement harness. Normal builds contain no collection code.
#ifdef GENGINE_RENDER_BASELINE
#include "Core/RenderCounters.h"
#include "Core/RenderTarget.h"
#include "Core/Window.h"
#include "Physics/PhysicsProfile.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace GEngine::RenderBaseline
{
    using Clock = std::chrono::steady_clock;
    inline Clock::time_point startup;
    inline bool Requested() { return SDL_getenv("GENGINE_BASELINE_OUTPUT") != nullptr; }

    inline void Configure(WindowProperties& properties)
    {
        if (!Requested()) return;
        startup = Clock::now();
        properties.m_Width = 1280;
        properties.m_Height = 720;
        properties.m_IsVsync = false;
    }

    class Session;
    inline thread_local Session* active = nullptr;

    class Session
    {
    public:
        static constexpr unsigned Warmup = 120, Samples = 240;
        Session()
        {
            if (!Requested()) return;
            if (!RenderCounters::Enabled || !IsPhysicsProfilingEnabled())
                throw std::runtime_error("Baseline requires consistently enabled render/Physics counters");
            output = std::filesystem::path(SDL_getenv("GENGINE_BASELINE_OUTPUT"));
            if (!output.is_absolute() || !std::filesystem::is_directory(output))
                throw std::runtime_error("GENGINE_BASELINE_OUTPUT must name an existing absolute directory");
            csv.open(output / "frames.csv", std::ios::out | std::ios::trunc);
            metadata.open(output / "runtime.txt", std::ios::out | std::ios::trunc);
            if (!csv || !metadata) throw std::runtime_error("Cannot open baseline output");
            if (SDL_GL_SetSwapInterval(0) != 0 || SDL_GL_GetSwapInterval() != 0)
                throw std::runtime_error("Baseline requires confirmed VSYNC=0");
            auto* window = SDL_GL_GetCurrentWindow();
            SDL_GL_GetDrawableSize(window, &width, &height);
            if (width != 1280 || height != 720)
                throw std::runtime_error("Baseline requires a 1280x720 drawable");
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
            csv.exceptions(std::ios::badbit | std::ios::failbit);
            metadata.exceptions(std::ios::badbit | std::ios::failbit);
            active = this;
        }
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
        ~Session() { if (active == this) active = nullptr; }
        bool Enabled() const { return active == this; }
        void Begin()
        {
            if (!Enabled()) return;
            frameStart = Clock::now();
            ResetPhysicsProfile();
        }
        void Work() { if (Enabled()) workStart = Clock::now(); }
        void UpdatedInput() { if (Enabled()) inputEnd = Clock::now(); }
        void Updated() { if (Enabled()) updateEnd = Clock::now(); }
        bool End()
        {
            if (!Enabled()) return false;
            const auto end = Clock::now();
            const auto physics = GetPhysicsProfileSnapshot();
            if (physics.stepCount != 0)
                throw std::runtime_error("Frozen baseline unexpectedly stepped Physics");
            int currentWidth = 0, currentHeight = 0;
            SDL_GL_GetDrawableSize(SDL_GL_GetCurrentWindow(), &currentWidth, &currentHeight);
            if (currentWidth != width || currentHeight != height || SDL_GL_GetSwapInterval() != 0)
                throw std::runtime_error("Baseline drawable/VSYNC changed during collection");
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
            }
            // One extra untimed frame captures the backbuffer through SwapBuffer.
            if (++frame > Warmup + Samples)
            {
                if (!captured) throw std::runtime_error("Baseline diagnostic image was not captured");
                csv.close();
                metadata << "completed_samples=" << Samples << '\n';
                metadata.close();
                return true;
            }
            return false;
        }
        void CaptureScene(const RenderTarget* target)
        {
            if (!Enabled() || frame != Warmup + Samples || !SDL_getenv("GENGINE_BASELINE_SCENE_TARGET")) return;
            if (!target || target->GetWidth() != width || target->GetHeight() != height)
                throw std::runtime_error("Requested diagnostic scene target has unexpected dimensions");
            const GLuint texture = target->IsMultiSampled() ? target->GetScreenAttachmentID() : target->GetColorAttachmentID();
            std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
            {
                PackState pack;
                glGetTextureImage(texture, 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(pixels.size()), pixels.data());
            }
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Scene target capture generated a GL error");
            SaveImage(pixels, width, height, "scene.bmp");
            metadata << "scene_diagnostic=resolved application render target; presentation defect retained\n";
        }
        void Capture(SDL_Window* window)
        {
            if (!Enabled() || frame != Warmup + Samples || captured) return;
            // Preserve the known rendering-error inventory before isolated diagnostic queries.
            for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
                metadata << "pre_capture_gl_error=" << error << '\n';
            RecordTargets();
            int w = 0, h = 0;
            SDL_GL_GetDrawableSize(window, &w, &h);
            if (w != width || h != height) throw std::runtime_error("Capture dimensions changed");
            std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
            GLint readFramebuffer = 0, readBuffer = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
            glGetIntegerv(GL_READ_BUFFER, &readBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
            {
                PackState pack;
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
            glReadBuffer(readBuffer);
            SaveImage(pixels, w, h, "diagnostic.bmp");
            if (glGetError() != GL_NO_ERROR)
                throw std::runtime_error("Baseline capture/resource query generated a GL error");
            captured = true;
        }
    private:
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
        void SaveImage(std::vector<unsigned char>& pixels, int w, int h, const char* name)
        {
            const auto stride = static_cast<size_t>(w) * 4;
            for (int y = 0; y < h / 2; ++y)
                for (size_t x = 0; x < stride; ++x)
                    std::swap(pixels[static_cast<size_t>(y) * stride + x], pixels[static_cast<size_t>(h - y - 1) * stride + x]);
            auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
            if (!surface) throw std::runtime_error("Cannot create baseline capture surface");
            const auto result = SDL_SaveBMP(surface, (output / name).string().c_str());
            SDL_FreeSurface(surface);
            if (result != 0) throw std::runtime_error("Cannot save baseline diagnostic image");
        }
        void RecordTargets()
        {
            std::ofstream targets(output / "targets.txt");
            targets.exceptions(std::ios::badbit | std::ios::failbit);
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
        }
        static std::int64_t Nanoseconds(Clock::time_point from, Clock::time_point to)
        { return std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count(); }
        std::filesystem::path output;
        std::ofstream csv, metadata;
        Clock::time_point frameStart, workStart, inputEnd, updateEnd;
        unsigned frame = 0;
        int width = 0, height = 0;
        bool captured = false;
    };
    inline void Capture(SDL_Window* window) { if (active) active->Capture(window); }
}
#endif
