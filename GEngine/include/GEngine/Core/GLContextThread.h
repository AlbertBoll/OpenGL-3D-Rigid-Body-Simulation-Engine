#pragma once

#include <glad/glad.h>
#include <sdl2/SDL.h>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_map>

// Transitional boundary for first-party GL calls, shared by engine and probes.
// EngineContext and SDLWindow keep their construction thread for their lifetime.
// Registered contexts may switch/detach on that thread, never migrate to a worker.
// ImGui's private loader/context switches are guarded at the first-party boundary.
namespace GEngine::GLContextThread
{
    namespace Detail
    {
        struct Owner { std::thread::id thread; };
        struct Registry
        {
            std::mutex mutex;
            std::unordered_map<SDL_GLContext, Owner> contexts;
        };
        inline Registry& Contexts() { static Registry registry; return registry; }
        [[noreturn]] inline void Fail(const char* operation) noexcept
        {
            std::fprintf(stderr, "[GLThread] assertion failed: %s requires the owning context thread\n", operation);
            std::fflush(stderr);
            std::terminate();
        }
        inline void RequireThread(const Registry& registry, const char* operation)
        {
            if (!registry.contexts.empty()
                && registry.contexts.begin()->second.thread != std::this_thread::get_id()) Fail(operation);
        }
    }

    inline void RequireOwner(std::thread::id owner, const char* operation) noexcept
    {
        if (owner != std::this_thread::get_id()) Detail::Fail(operation);
    }

    // Validation-only query: reads SDL's current-context identity; issues no GL.
    // Passing this query is not permission to transfer a context or GPU resource.
    inline bool IsCurrentOwner()
    {
        const auto context = SDL_GL_GetCurrentContext();
        auto& registry = Detail::Contexts();
        const std::lock_guard lock(registry.mutex);
        const auto found = registry.contexts.find(context);
        return found != registry.contexts.end() && found->second.thread == std::this_thread::get_id();
    }

    inline void RequireCurrent(const char* operation) noexcept
    {
        if (!IsCurrentOwner()) Detail::Fail(operation);
    }

    inline void AssertCurrent(const char* operation) noexcept
    {
#ifdef GENGINE_CONFIG_DEBUG
        RequireCurrent(operation);
#else
        (void)operation;
#endif
    }

    inline SDL_GLContext CreateContext(SDL_Window* window)
    {
        auto& registry = Detail::Contexts();
        const std::lock_guard lock(registry.mutex);
        Detail::RequireThread(registry, "SDL_GL_CreateContext");
        const auto context = SDL_GL_CreateContext(window);
        if (context)
        {
            try { registry.contexts.emplace(context, Detail::Owner{std::this_thread::get_id()}); }
            catch (...) { SDL_GL_DeleteContext(context); throw; }
        }
        return context;
    }

    inline int MakeCurrent(SDL_Window* window, SDL_GLContext context)
    {
        auto& registry = Detail::Contexts();
        const std::lock_guard lock(registry.mutex);
        Detail::RequireThread(registry, "SDL_GL_MakeCurrent");
        if (context)
        {
            const auto found = registry.contexts.find(context);
            // ImGui may restore a context on another compatible drawable. SDL
            // validates the drawable; the immutable context owner stays the same.
            if (found == registry.contexts.end())
                Detail::Fail("SDL_GL_MakeCurrent (unregistered context)");
        }
        return SDL_GL_MakeCurrent(window, context);
    }

    inline void DeleteContext(SDL_GLContext context)
    {
        if (!context) return;
        auto& registry = Detail::Contexts();
        const std::lock_guard lock(registry.mutex);
        const auto found = registry.contexts.find(context);
        if (found == registry.contexts.end() || found->second.thread != std::this_thread::get_id())
            Detail::Fail("SDL_GL_DeleteContext");
        SDL_GL_DeleteContext(context);
        registry.contexts.erase(found);
    }
}

// Context transitions remain checked in Release; ordinary GL assertions compile
// out. Use these SDL entry points for standalone contexts as well as SDLWindow.
#define SDL_GL_CreateContext ::GEngine::GLContextThread::CreateContext
#define SDL_GL_MakeCurrent ::GEngine::GLContextThread::MakeCurrent
#define SDL_GL_DeleteContext ::GEngine::GLContextThread::DeleteContext

// GL entry-point assertions are below. Keep this ahead of RenderCounters so its
// wrappers call the checked driver pointer. No loader pointers are replaced.
#ifdef GENGINE_CONFIG_DEBUG
#undef glActiveTexture
#define glActiveTexture (::GEngine::GLContextThread::AssertCurrent("glActiveTexture"), glad_glActiveTexture)
#undef glAttachShader
#define glAttachShader (::GEngine::GLContextThread::AssertCurrent("glAttachShader"), glad_glAttachShader)
#undef glBindAttribLocation
#define glBindAttribLocation (::GEngine::GLContextThread::AssertCurrent("glBindAttribLocation"), glad_glBindAttribLocation)
#undef glBindBuffer
#define glBindBuffer (::GEngine::GLContextThread::AssertCurrent("glBindBuffer"), glad_glBindBuffer)
#undef glBindBufferBase
#define glBindBufferBase (::GEngine::GLContextThread::AssertCurrent("glBindBufferBase"), glad_glBindBufferBase)
#undef glBindFragDataLocation
#define glBindFragDataLocation (::GEngine::GLContextThread::AssertCurrent("glBindFragDataLocation"), glad_glBindFragDataLocation)
#undef glBindFramebuffer
#define glBindFramebuffer (::GEngine::GLContextThread::AssertCurrent("glBindFramebuffer"), glad_glBindFramebuffer)
#undef glBindRenderbuffer
#define glBindRenderbuffer (::GEngine::GLContextThread::AssertCurrent("glBindRenderbuffer"), glad_glBindRenderbuffer)
#undef glBindSampler
#define glBindSampler (::GEngine::GLContextThread::AssertCurrent("glBindSampler"), glad_glBindSampler)
#undef glBindTexture
#define glBindTexture (::GEngine::GLContextThread::AssertCurrent("glBindTexture"), glad_glBindTexture)
#undef glBindVertexArray
#define glBindVertexArray (::GEngine::GLContextThread::AssertCurrent("glBindVertexArray"), glad_glBindVertexArray)
#undef glBlendFunc
#define glBlendFunc (::GEngine::GLContextThread::AssertCurrent("glBlendFunc"), glad_glBlendFunc)
#undef glBlitFramebuffer
#define glBlitFramebuffer (::GEngine::GLContextThread::AssertCurrent("glBlitFramebuffer"), glad_glBlitFramebuffer)
#undef glBufferData
#define glBufferData (::GEngine::GLContextThread::AssertCurrent("glBufferData"), glad_glBufferData)
#undef glBufferSubData
#define glBufferSubData (::GEngine::GLContextThread::AssertCurrent("glBufferSubData"), glad_glBufferSubData)
#undef glCheckFramebufferStatus
#define glCheckFramebufferStatus (::GEngine::GLContextThread::AssertCurrent("glCheckFramebufferStatus"), glad_glCheckFramebufferStatus)
#undef glClear
#define glClear (::GEngine::GLContextThread::AssertCurrent("glClear"), glad_glClear)
#undef glClearBufferfv
#define glClearBufferfv (::GEngine::GLContextThread::AssertCurrent("glClearBufferfv"), glad_glClearBufferfv)
#undef glClearBufferiv
#define glClearBufferiv (::GEngine::GLContextThread::AssertCurrent("glClearBufferiv"), glad_glClearBufferiv)
#undef glClearColor
#define glClearColor (::GEngine::GLContextThread::AssertCurrent("glClearColor"), glad_glClearColor)
#undef glClearTexImage
#define glClearTexImage (::GEngine::GLContextThread::AssertCurrent("glClearTexImage"), glad_glClearTexImage)
#undef glCompileShader
#define glCompileShader (::GEngine::GLContextThread::AssertCurrent("glCompileShader"), glad_glCompileShader)
#undef glCreateProgram
#define glCreateProgram (::GEngine::GLContextThread::AssertCurrent("glCreateProgram"), glad_glCreateProgram)
#undef glCreateShader
#define glCreateShader (::GEngine::GLContextThread::AssertCurrent("glCreateShader"), glad_glCreateShader)
#undef glCreateTextures
#define glCreateTextures (::GEngine::GLContextThread::AssertCurrent("glCreateTextures"), glad_glCreateTextures)
#undef glCullFace
#define glCullFace (::GEngine::GLContextThread::AssertCurrent("glCullFace"), glad_glCullFace)
#undef glDebugMessageCallback
#define glDebugMessageCallback (::GEngine::GLContextThread::AssertCurrent("glDebugMessageCallback"), glad_glDebugMessageCallback)
#undef glDebugMessageControl
#define glDebugMessageControl (::GEngine::GLContextThread::AssertCurrent("glDebugMessageControl"), glad_glDebugMessageControl)
#undef glDebugMessageInsert
#define glDebugMessageInsert (::GEngine::GLContextThread::AssertCurrent("glDebugMessageInsert"), glad_glDebugMessageInsert)
#undef glDeleteBuffers
#define glDeleteBuffers (::GEngine::GLContextThread::AssertCurrent("glDeleteBuffers"), glad_glDeleteBuffers)
#undef glDeleteFramebuffers
#define glDeleteFramebuffers (::GEngine::GLContextThread::AssertCurrent("glDeleteFramebuffers"), glad_glDeleteFramebuffers)
#undef glDeleteProgram
#define glDeleteProgram (::GEngine::GLContextThread::AssertCurrent("glDeleteProgram"), glad_glDeleteProgram)
#undef glDeleteRenderbuffers
#define glDeleteRenderbuffers (::GEngine::GLContextThread::AssertCurrent("glDeleteRenderbuffers"), glad_glDeleteRenderbuffers)
#undef glDeleteSamplers
#define glDeleteSamplers (::GEngine::GLContextThread::AssertCurrent("glDeleteSamplers"), glad_glDeleteSamplers)
#undef glDeleteShader
#define glDeleteShader (::GEngine::GLContextThread::AssertCurrent("glDeleteShader"), glad_glDeleteShader)
#undef glDeleteTextures
#define glDeleteTextures (::GEngine::GLContextThread::AssertCurrent("glDeleteTextures"), glad_glDeleteTextures)
#undef glDeleteVertexArrays
#define glDeleteVertexArrays (::GEngine::GLContextThread::AssertCurrent("glDeleteVertexArrays"), glad_glDeleteVertexArrays)
#undef glDepthFunc
#define glDepthFunc (::GEngine::GLContextThread::AssertCurrent("glDepthFunc"), glad_glDepthFunc)
#undef glDetachShader
#define glDetachShader (::GEngine::GLContextThread::AssertCurrent("glDetachShader"), glad_glDetachShader)
#undef glDisable
#define glDisable (::GEngine::GLContextThread::AssertCurrent("glDisable"), glad_glDisable)
#undef glDrawArrays
#define glDrawArrays (::GEngine::GLContextThread::AssertCurrent("glDrawArrays"), glad_glDrawArrays)
#undef glDrawArraysInstanced
#define glDrawArraysInstanced (::GEngine::GLContextThread::AssertCurrent("glDrawArraysInstanced"), glad_glDrawArraysInstanced)
#undef glDrawBuffer
#define glDrawBuffer (::GEngine::GLContextThread::AssertCurrent("glDrawBuffer"), glad_glDrawBuffer)
#undef glDrawBuffers
#define glDrawBuffers (::GEngine::GLContextThread::AssertCurrent("glDrawBuffers"), glad_glDrawBuffers)
#undef glDrawElements
#define glDrawElements (::GEngine::GLContextThread::AssertCurrent("glDrawElements"), glad_glDrawElements)
#undef glDrawElementsInstanced
#define glDrawElementsInstanced (::GEngine::GLContextThread::AssertCurrent("glDrawElementsInstanced"), glad_glDrawElementsInstanced)
#undef glEnable
#define glEnable (::GEngine::GLContextThread::AssertCurrent("glEnable"), glad_glEnable)
#undef glEnableVertexAttribArray
#define glEnableVertexAttribArray (::GEngine::GLContextThread::AssertCurrent("glEnableVertexAttribArray"), glad_glEnableVertexAttribArray)
#undef glFramebufferRenderbuffer
#define glFramebufferRenderbuffer (::GEngine::GLContextThread::AssertCurrent("glFramebufferRenderbuffer"), glad_glFramebufferRenderbuffer)
#undef glFramebufferTexture
#define glFramebufferTexture (::GEngine::GLContextThread::AssertCurrent("glFramebufferTexture"), glad_glFramebufferTexture)
#undef glFramebufferTexture2D
#define glFramebufferTexture2D (::GEngine::GLContextThread::AssertCurrent("glFramebufferTexture2D"), glad_glFramebufferTexture2D)
#undef glGenBuffers
#define glGenBuffers (::GEngine::GLContextThread::AssertCurrent("glGenBuffers"), glad_glGenBuffers)
#undef glGenFramebuffers
#define glGenFramebuffers (::GEngine::GLContextThread::AssertCurrent("glGenFramebuffers"), glad_glGenFramebuffers)
#undef glGenRenderbuffers
#define glGenRenderbuffers (::GEngine::GLContextThread::AssertCurrent("glGenRenderbuffers"), glad_glGenRenderbuffers)
#undef glGenSamplers
#define glGenSamplers (::GEngine::GLContextThread::AssertCurrent("glGenSamplers"), glad_glGenSamplers)
#undef glGenTextures
#define glGenTextures (::GEngine::GLContextThread::AssertCurrent("glGenTextures"), glad_glGenTextures)
#undef glGenVertexArrays
#define glGenVertexArrays (::GEngine::GLContextThread::AssertCurrent("glGenVertexArrays"), glad_glGenVertexArrays)
#undef glGenerateMipmap
#define glGenerateMipmap (::GEngine::GLContextThread::AssertCurrent("glGenerateMipmap"), glad_glGenerateMipmap)
#undef glGetAttachedShaders
#define glGetAttachedShaders (::GEngine::GLContextThread::AssertCurrent("glGetAttachedShaders"), glad_glGetAttachedShaders)
#undef glGetBufferParameteri64v
#define glGetBufferParameteri64v (::GEngine::GLContextThread::AssertCurrent("glGetBufferParameteri64v"), glad_glGetBufferParameteri64v)
#undef glGetBufferParameteriv
#define glGetBufferParameteriv (::GEngine::GLContextThread::AssertCurrent("glGetBufferParameteriv"), glad_glGetBufferParameteriv)
#undef glGetBufferSubData
#define glGetBufferSubData (::GEngine::GLContextThread::AssertCurrent("glGetBufferSubData"), glad_glGetBufferSubData)
#undef glGetError
#define glGetError (::GEngine::GLContextThread::AssertCurrent("glGetError"), glad_glGetError)
#undef glGetFloatv
#define glGetFloatv (::GEngine::GLContextThread::AssertCurrent("glGetFloatv"), glad_glGetFloatv)
#undef glGetFramebufferAttachmentParameteriv
#define glGetFramebufferAttachmentParameteriv (::GEngine::GLContextThread::AssertCurrent("glGetFramebufferAttachmentParameteriv"), glad_glGetFramebufferAttachmentParameteriv)
#undef glGetIntegeri_v
#define glGetIntegeri_v (::GEngine::GLContextThread::AssertCurrent("glGetIntegeri_v"), glad_glGetIntegeri_v)
#undef glGetIntegerv
#define glGetIntegerv (::GEngine::GLContextThread::AssertCurrent("glGetIntegerv"), glad_glGetIntegerv)
#undef glGetNamedFramebufferAttachmentParameteriv
#define glGetNamedFramebufferAttachmentParameteriv (::GEngine::GLContextThread::AssertCurrent("glGetNamedFramebufferAttachmentParameteriv"), glad_glGetNamedFramebufferAttachmentParameteriv)
#undef glGetNamedRenderbufferParameteriv
#define glGetNamedRenderbufferParameteriv (::GEngine::GLContextThread::AssertCurrent("glGetNamedRenderbufferParameteriv"), glad_glGetNamedRenderbufferParameteriv)
#undef glGetPointerv
#define glGetPointerv (::GEngine::GLContextThread::AssertCurrent("glGetPointerv"), glad_glGetPointerv)
#undef glGetProgramInfoLog
#define glGetProgramInfoLog (::GEngine::GLContextThread::AssertCurrent("glGetProgramInfoLog"), glad_glGetProgramInfoLog)
#undef glGetProgramInterfaceiv
#define glGetProgramInterfaceiv (::GEngine::GLContextThread::AssertCurrent("glGetProgramInterfaceiv"), glad_glGetProgramInterfaceiv)
#undef glGetProgramResourceName
#define glGetProgramResourceName (::GEngine::GLContextThread::AssertCurrent("glGetProgramResourceName"), glad_glGetProgramResourceName)
#undef glGetProgramResourceiv
#define glGetProgramResourceiv (::GEngine::GLContextThread::AssertCurrent("glGetProgramResourceiv"), glad_glGetProgramResourceiv)
#undef glGetProgramiv
#define glGetProgramiv (::GEngine::GLContextThread::AssertCurrent("glGetProgramiv"), glad_glGetProgramiv)
#undef glGetShaderInfoLog
#define glGetShaderInfoLog (::GEngine::GLContextThread::AssertCurrent("glGetShaderInfoLog"), glad_glGetShaderInfoLog)
#undef glGetShaderiv
#define glGetShaderiv (::GEngine::GLContextThread::AssertCurrent("glGetShaderiv"), glad_glGetShaderiv)
#undef glGetString
#define glGetString (::GEngine::GLContextThread::AssertCurrent("glGetString"), glad_glGetString)
#undef glGetTexImage
#define glGetTexImage (::GEngine::GLContextThread::AssertCurrent("glGetTexImage"), glad_glGetTexImage)
#undef glGetTexLevelParameteriv
#define glGetTexLevelParameteriv (::GEngine::GLContextThread::AssertCurrent("glGetTexLevelParameteriv"), glad_glGetTexLevelParameteriv)
#undef glGetTextureImage
#define glGetTextureImage (::GEngine::GLContextThread::AssertCurrent("glGetTextureImage"), glad_glGetTextureImage)
#undef glGetTextureLevelParameteriv
#define glGetTextureLevelParameteriv (::GEngine::GLContextThread::AssertCurrent("glGetTextureLevelParameteriv"), glad_glGetTextureLevelParameteriv)
#undef glGetUniformLocation
#define glGetUniformLocation (::GEngine::GLContextThread::AssertCurrent("glGetUniformLocation"), glad_glGetUniformLocation)
#undef glGetUniformfv
#define glGetUniformfv (::GEngine::GLContextThread::AssertCurrent("glGetUniformfv"), glad_glGetUniformfv)
#undef glIsBuffer
#define glIsBuffer (::GEngine::GLContextThread::AssertCurrent("glIsBuffer"), glad_glIsBuffer)
#undef glIsEnabled
#define glIsEnabled (::GEngine::GLContextThread::AssertCurrent("glIsEnabled"), glad_glIsEnabled)
#undef glIsFramebuffer
#define glIsFramebuffer (::GEngine::GLContextThread::AssertCurrent("glIsFramebuffer"), glad_glIsFramebuffer)
#undef glIsProgram
#define glIsProgram (::GEngine::GLContextThread::AssertCurrent("glIsProgram"), glad_glIsProgram)
#undef glIsTexture
#define glIsTexture (::GEngine::GLContextThread::AssertCurrent("glIsTexture"), glad_glIsTexture)
#undef glLineWidth
#define glLineWidth (::GEngine::GLContextThread::AssertCurrent("glLineWidth"), glad_glLineWidth)
#undef glLinkProgram
#define glLinkProgram (::GEngine::GLContextThread::AssertCurrent("glLinkProgram"), glad_glLinkProgram)
#undef glPixelStorei
#define glPixelStorei (::GEngine::GLContextThread::AssertCurrent("glPixelStorei"), glad_glPixelStorei)
#undef glPolygonMode
#define glPolygonMode (::GEngine::GLContextThread::AssertCurrent("glPolygonMode"), glad_glPolygonMode)
#undef glPopDebugGroup
#define glPopDebugGroup (::GEngine::GLContextThread::AssertCurrent("glPopDebugGroup"), glad_glPopDebugGroup)
#undef glPushDebugGroup
#define glPushDebugGroup (::GEngine::GLContextThread::AssertCurrent("glPushDebugGroup"), glad_glPushDebugGroup)
#undef glReadBuffer
#define glReadBuffer (::GEngine::GLContextThread::AssertCurrent("glReadBuffer"), glad_glReadBuffer)
#undef glReadPixels
#define glReadPixels (::GEngine::GLContextThread::AssertCurrent("glReadPixels"), glad_glReadPixels)
#undef glRenderbufferStorage
#define glRenderbufferStorage (::GEngine::GLContextThread::AssertCurrent("glRenderbufferStorage"), glad_glRenderbufferStorage)
#undef glRenderbufferStorageMultisample
#define glRenderbufferStorageMultisample (::GEngine::GLContextThread::AssertCurrent("glRenderbufferStorageMultisample"), glad_glRenderbufferStorageMultisample)
#undef glShaderSource
#define glShaderSource (::GEngine::GLContextThread::AssertCurrent("glShaderSource"), glad_glShaderSource)
#undef glTexImage2D
#define glTexImage2D (::GEngine::GLContextThread::AssertCurrent("glTexImage2D"), glad_glTexImage2D)
#undef glTexImage2DMultisample
#define glTexImage2DMultisample (::GEngine::GLContextThread::AssertCurrent("glTexImage2DMultisample"), glad_glTexImage2DMultisample)
#undef glTexImage3D
#define glTexImage3D (::GEngine::GLContextThread::AssertCurrent("glTexImage3D"), glad_glTexImage3D)
#undef glTexParameterf
#define glTexParameterf (::GEngine::GLContextThread::AssertCurrent("glTexParameterf"), glad_glTexParameterf)
#undef glTexParameterfv
#define glTexParameterfv (::GEngine::GLContextThread::AssertCurrent("glTexParameterfv"), glad_glTexParameterfv)
#undef glTexParameteri
#define glTexParameteri (::GEngine::GLContextThread::AssertCurrent("glTexParameteri"), glad_glTexParameteri)
#undef glTexStorage2D
#define glTexStorage2D (::GEngine::GLContextThread::AssertCurrent("glTexStorage2D"), glad_glTexStorage2D)
#undef glTexSubImage2D
#define glTexSubImage2D (::GEngine::GLContextThread::AssertCurrent("glTexSubImage2D"), glad_glTexSubImage2D)
#undef glUniform1i
#define glUniform1i (::GEngine::GLContextThread::AssertCurrent("glUniform1i"), glad_glUniform1i)
#undef glUniform3fv
#define glUniform3fv (::GEngine::GLContextThread::AssertCurrent("glUniform3fv"), glad_glUniform3fv)
#undef glUniformMatrix4fv
#define glUniformMatrix4fv (::GEngine::GLContextThread::AssertCurrent("glUniformMatrix4fv"), glad_glUniformMatrix4fv)
#undef glUseProgram
#define glUseProgram (::GEngine::GLContextThread::AssertCurrent("glUseProgram"), glad_glUseProgram)
#undef glValidateProgram
#define glValidateProgram (::GEngine::GLContextThread::AssertCurrent("glValidateProgram"), glad_glValidateProgram)
#undef glVertexAttribDivisor
#define glVertexAttribDivisor (::GEngine::GLContextThread::AssertCurrent("glVertexAttribDivisor"), glad_glVertexAttribDivisor)
#undef glVertexAttribIPointer
#define glVertexAttribIPointer (::GEngine::GLContextThread::AssertCurrent("glVertexAttribIPointer"), glad_glVertexAttribIPointer)
#undef glVertexAttribPointer
#define glVertexAttribPointer (::GEngine::GLContextThread::AssertCurrent("glVertexAttribPointer"), glad_glVertexAttribPointer)
#undef glViewport
#define glViewport (::GEngine::GLContextThread::AssertCurrent("glViewport"), glad_glViewport)
#endif
