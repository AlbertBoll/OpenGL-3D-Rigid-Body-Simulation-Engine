#pragma once

#include <glad/glad.h>
#include <sdl2/SDL.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <tuple>

// Define consistently for all consumers. Release leaves the original GL calls
// untouched; Debug observes calls made through this header (not ImGui's loader).
#ifndef GENGINE_RENDER_COUNTERS
#ifdef GENGINE_CONFIG_DEBUG
#define GENGINE_RENDER_COUNTERS 1
#else
#define GENGINE_RENDER_COUNTERS 0
#endif
#endif

namespace GEngine::RenderCounters
{
    inline constexpr bool Enabled = GENGINE_RENDER_COUNTERS != 0;
    enum class Resource : unsigned { Buffer, VertexArray, Texture, Sampler, Framebuffer, Renderbuffer, Shader, Program, Count };
    enum class Pass { Shadow, Picking };

    struct Frame
    {
        std::uint64_t draws = 0, indexedDraws = 0, submittedTriangles = 0;
        std::uint64_t programBinds = 0, vaoBinds = 0, textureBinds = 0, samplerBinds = 0, framebufferBinds = 0;
        std::uint64_t bufferUploadCalls = 0, bufferUploadBytes = 0, bufferAllocationCalls = 0;
        std::uint64_t readbackCalls = 0, readbackCpuNanoseconds = 0;
        std::uint64_t shadowPasses = 0, pickingPasses = 0, targetReallocations = 0;
    };
    struct Snapshot
    {
        Frame frame;
        std::uint64_t frameNumber = 0;
        std::array<std::uint64_t, static_cast<unsigned>(Resource::Count)> liveNames{};
        std::uint64_t estimatedBufferBytes = 0;
    };

#if GENGINE_RENDER_COUNTERS
    namespace Detail
    {
        // Diagnostic bookkeeping only: GL names never become engine identities.
        // Thread-local storage requires the existing context-owning thread policy.
        using Key = std::tuple<std::uintptr_t, Resource, GLuint>;
        struct State
        {
            Snapshot current, completed;
            std::map<Key, std::uint64_t> names;
        };
        inline thread_local State state;
        inline Key KeyFor(Resource kind, GLuint name)
        {
            return { reinterpret_cast<std::uintptr_t>(SDL_GL_GetCurrentContext()), kind, name };
        }
        inline void Created(Resource kind, GLsizei count, const GLuint* names)
        {
            for (GLsizei i = 0; names && i < count; ++i)
                if (names[i] && state.names.emplace(KeyFor(kind, names[i]), 0).second)
                    ++state.current.liveNames[static_cast<unsigned>(kind)];
        }
        inline void Deleted(Resource kind, GLsizei count, const GLuint* names)
        {
            for (GLsizei i = 0; names && i < count; ++i)
            {
                const auto entry = state.names.find(KeyFor(kind, names[i]));
                if (entry == state.names.end()) continue;
                if (kind == Resource::Buffer) state.current.estimatedBufferBytes -= entry->second;
                --state.current.liveNames[static_cast<unsigned>(kind)];
                state.names.erase(entry);
            }
        }
        inline void Draw(GLenum mode, GLsizei count, GLsizei instances, bool indexed)
        {
            auto& frame = state.current.frame;
            ++frame.draws;
            frame.indexedDraws += indexed;
            // Submitted triangle-list primitives only. Strips/restart, geometry
            // amplification, clipping and failed GL calls are not inferred.
            if (mode == GL_TRIANGLES && count > 0 && instances > 0)
                frame.submittedTriangles += (static_cast<std::uint64_t>(count) / 3) * instances;
        }
        inline void BufferAllocation(GLenum target, GLsizeiptr size)
        {
            // Query only at store allocation, not at binds/draws. This avoids an
            // incomplete state cache (especially VAO-owned element bindings).
            GLenum binding = 0;
            switch (target)
            {
            case GL_ARRAY_BUFFER: binding = GL_ARRAY_BUFFER_BINDING; break;
            case GL_ELEMENT_ARRAY_BUFFER: binding = GL_ELEMENT_ARRAY_BUFFER_BINDING; break;
            case GL_UNIFORM_BUFFER: binding = GL_UNIFORM_BUFFER_BINDING; break;
            case GL_PIXEL_PACK_BUFFER: binding = GL_PIXEL_PACK_BUFFER_BINDING; break;
            case GL_PIXEL_UNPACK_BUFFER: binding = GL_PIXEL_UNPACK_BUFFER_BINDING; break;
            default: return; // Other targets have no byte estimate in this phase.
            }
            GLint name = 0;
            glGetIntegerv(binding, &name);
            const auto entry = state.names.find(KeyFor(Resource::Buffer, static_cast<GLuint>(name)));
            if (entry == state.names.end() || size < 0) return;
            state.current.estimatedBufferBytes -= entry->second;
            entry->second = static_cast<std::uint64_t>(size);
            state.current.estimatedBufferBytes += entry->second;
        }
    }
#endif

    inline Snapshot Current()
    {
#if GENGINE_RENDER_COUNTERS
        return Detail::state.current;
#else
        return {};
#endif
    }
    inline Snapshot LastFrame()
    {
#if GENGINE_RENDER_COUNTERS
        return Detail::state.completed;
#else
        return {};
#endif
    }
    inline void BeginFrame()
    {
#if GENGINE_RENDER_COUNTERS
        Detail::state.current.frame = {};
        ++Detail::state.current.frameNumber;
#endif
    }
    inline void EndFrame()
    {
#if GENGINE_RENDER_COUNTERS
        Detail::state.completed = Detail::state.current;
#endif
    }
    inline void RecordPass(Pass pass)
    {
#if GENGINE_RENDER_COUNTERS
        auto& frame = Detail::state.current.frame;
        if (pass == Pass::Shadow) ++frame.shadowPasses;
        else ++frame.pickingPasses;
#else
        (void)pass;
#endif
    }
    inline void RecordTargetReallocation(bool replacing)
    {
#if GENGINE_RENDER_COUNTERS
        Detail::state.current.frame.targetReallocations += replacing;
#else
        (void)replacing;
#endif
    }
    inline void ForgetContext(SDL_GLContext context)
    {
#if GENGINE_RENDER_COUNTERS
        // Context destruction retires even leaked names; no GL destruction here.
        auto& state = Detail::state;
        for (auto it = state.names.begin(); it != state.names.end();)
        {
            if (std::get<0>(it->first) != reinterpret_cast<std::uintptr_t>(context)) { ++it; continue; }
            const auto kind = std::get<1>(it->first);
            if (kind == Resource::Buffer) state.current.estimatedBufferBytes -= it->second;
            --state.current.liveNames[static_cast<unsigned>(kind)];
            it = state.names.erase(it);
        }
#else
        (void)context;
#endif
    }
    inline void ReportLastFrame()
    {
#if GENGINE_RENDER_COUNTERS
        const auto s = LastFrame();
        const auto& f = s.frame;
        std::fprintf(stderr, "[RenderCounters] frame=%llu draws=%llu indexed=%llu triangles=%llu "
            "binds(program/vao/texture/sampler/fbo)=%llu/%llu/%llu/%llu/%llu "
            "uploads=%llu upload-bytes=%llu allocations=%llu readbacks=%llu readback-cpu-ns=%llu "
            "passes(shadow/picking)=%llu/%llu target-reallocations=%llu "
            "live(buffer/vao/texture/sampler/fbo/rbo/shader/program)=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu estimated-buffer-bytes=%llu\n",
            s.frameNumber, f.draws, f.indexedDraws, f.submittedTriangles,
            f.programBinds, f.vaoBinds, f.textureBinds, f.samplerBinds, f.framebufferBinds,
            f.bufferUploadCalls, f.bufferUploadBytes, f.bufferAllocationCalls, f.readbackCalls, f.readbackCpuNanoseconds,
            f.shadowPasses, f.pickingPasses, f.targetReallocations,
            s.liveNames[0], s.liveNames[1], s.liveNames[2], s.liveNames[3], s.liveNames[4], s.liveNames[5], s.liveNames[6], s.liveNames[7],
            s.estimatedBufferBytes);
#endif
    }

#if GENGINE_RENDER_COUNTERS
    inline void DrawArrays(GLenum mode, GLint first, GLsizei count)
    { Detail::Draw(mode, count, 1, false); glDrawArrays(mode, first, count); }
    inline void DrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices)
    { Detail::Draw(mode, count, 1, true); glDrawElements(mode, count, type, indices); }
    inline void DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
    { Detail::Draw(mode, count, instances, false); glDrawArraysInstanced(mode, first, count, instances); }
    inline void DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances)
    { Detail::Draw(mode, count, instances, true); glDrawElementsInstanced(mode, count, type, indices, instances); }
    inline void UseProgram(GLuint name)
    { ++Detail::state.current.frame.programBinds; glUseProgram(name); }
    inline void BindVertexArray(GLuint name)
    { ++Detail::state.current.frame.vaoBinds; glBindVertexArray(name); }
    inline void BindTexture(GLenum target, GLuint name)
    { ++Detail::state.current.frame.textureBinds; glBindTexture(target, name); }
    inline void BindSampler(GLuint unit, GLuint name)
    { ++Detail::state.current.frame.samplerBinds; glBindSampler(unit, name); }
    inline void BindFramebuffer(GLenum target, GLuint name)
    { ++Detail::state.current.frame.framebufferBinds; glBindFramebuffer(target, name); }
    inline void BufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage)
    {
        auto& frame = Detail::state.current.frame;
        ++frame.bufferAllocationCalls;
        if (data) { ++frame.bufferUploadCalls; if (size > 0) frame.bufferUploadBytes += size; }
        glBufferData(target, size, data, usage);
        Detail::BufferAllocation(target, size);
    }
    inline void BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data)
    {
        ++Detail::state.current.frame.bufferUploadCalls;
        if (data && size > 0) Detail::state.current.frame.bufferUploadBytes += size;
        glBufferSubData(target, offset, size, data);
    }
    inline void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* data)
    {
        const auto start = std::chrono::steady_clock::now();
        glReadPixels(x, y, width, height, format, type, data);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        ++Detail::state.current.frame.readbackCalls;
        Detail::state.current.frame.readbackCpuNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    }

    // Each wrapper forwards exactly once. Accounting follows issued create/delete
    // requests; deferred driver destruction and failed operations are not queried.
#define GE_COUNTER_RESOURCE(Plural, Kind) \
    inline void Gen##Plural(GLsizei count, GLuint* names) \
    { glGen##Plural(count, names); Detail::Created(Resource::Kind, count, names); } \
    inline void Delete##Plural(GLsizei count, const GLuint* names) \
    { glDelete##Plural(count, names); Detail::Deleted(Resource::Kind, count, names); }
    GE_COUNTER_RESOURCE(Buffers, Buffer)
    GE_COUNTER_RESOURCE(VertexArrays, VertexArray)
    GE_COUNTER_RESOURCE(Textures, Texture)
    GE_COUNTER_RESOURCE(Samplers, Sampler)
    GE_COUNTER_RESOURCE(Framebuffers, Framebuffer)
    GE_COUNTER_RESOURCE(Renderbuffers, Renderbuffer)
#undef GE_COUNTER_RESOURCE
    inline void CreateTextures(GLenum target, GLsizei count, GLuint* names)
    { glCreateTextures(target, count, names); Detail::Created(Resource::Texture, count, names); }
    inline GLuint CreateShader(GLenum type)
    { const auto name = glCreateShader(type); Detail::Created(Resource::Shader, 1, &name); return name; }
    inline GLuint CreateProgram()
    { const auto name = glCreateProgram(); Detail::Created(Resource::Program, 1, &name); return name; }
    inline void DeleteShader(GLuint name)
    { glDeleteShader(name); Detail::Deleted(Resource::Shader, 1, &name); }
    inline void DeleteProgram(GLuint name)
    { glDeleteProgram(name); Detail::Deleted(Resource::Program, 1, &name); }
#endif
}

// Transitional observation boundary for current gepch consumers. No GL loader
// pointer is changed. New entrypoints must explicitly add coverage here.
#if GENGINE_RENDER_COUNTERS
#undef glDrawArrays
#define glDrawArrays ::GEngine::RenderCounters::DrawArrays
#undef glDrawElements
#define glDrawElements ::GEngine::RenderCounters::DrawElements
#undef glDrawArraysInstanced
#define glDrawArraysInstanced ::GEngine::RenderCounters::DrawArraysInstanced
#undef glDrawElementsInstanced
#define glDrawElementsInstanced ::GEngine::RenderCounters::DrawElementsInstanced
#undef glUseProgram
#define glUseProgram ::GEngine::RenderCounters::UseProgram
#undef glBindVertexArray
#define glBindVertexArray ::GEngine::RenderCounters::BindVertexArray
#undef glBindTexture
#define glBindTexture ::GEngine::RenderCounters::BindTexture
#undef glBindSampler
#define glBindSampler ::GEngine::RenderCounters::BindSampler
#undef glBindFramebuffer
#define glBindFramebuffer ::GEngine::RenderCounters::BindFramebuffer
#undef glBufferData
#define glBufferData ::GEngine::RenderCounters::BufferData
#undef glBufferSubData
#define glBufferSubData ::GEngine::RenderCounters::BufferSubData
#undef glReadPixels
#define glReadPixels ::GEngine::RenderCounters::ReadPixels
#undef glGenBuffers
#define glGenBuffers ::GEngine::RenderCounters::GenBuffers
#undef glDeleteBuffers
#define glDeleteBuffers ::GEngine::RenderCounters::DeleteBuffers
#undef glGenVertexArrays
#define glGenVertexArrays ::GEngine::RenderCounters::GenVertexArrays
#undef glDeleteVertexArrays
#define glDeleteVertexArrays ::GEngine::RenderCounters::DeleteVertexArrays
#undef glGenTextures
#define glGenTextures ::GEngine::RenderCounters::GenTextures
#undef glCreateTextures
#define glCreateTextures ::GEngine::RenderCounters::CreateTextures
#undef glDeleteTextures
#define glDeleteTextures ::GEngine::RenderCounters::DeleteTextures
#undef glGenSamplers
#define glGenSamplers ::GEngine::RenderCounters::GenSamplers
#undef glDeleteSamplers
#define glDeleteSamplers ::GEngine::RenderCounters::DeleteSamplers
#undef glGenFramebuffers
#define glGenFramebuffers ::GEngine::RenderCounters::GenFramebuffers
#undef glDeleteFramebuffers
#define glDeleteFramebuffers ::GEngine::RenderCounters::DeleteFramebuffers
#undef glGenRenderbuffers
#define glGenRenderbuffers ::GEngine::RenderCounters::GenRenderbuffers
#undef glDeleteRenderbuffers
#define glDeleteRenderbuffers ::GEngine::RenderCounters::DeleteRenderbuffers
#undef glCreateShader
#define glCreateShader ::GEngine::RenderCounters::CreateShader
#undef glDeleteShader
#define glDeleteShader ::GEngine::RenderCounters::DeleteShader
#undef glCreateProgram
#define glCreateProgram ::GEngine::RenderCounters::CreateProgram
#undef glDeleteProgram
#define glDeleteProgram ::GEngine::RenderCounters::DeleteProgram
#endif
