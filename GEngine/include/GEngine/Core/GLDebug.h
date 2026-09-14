#pragma once

#include <glad/glad.h>
#include <sdl2/SDL.h>
#include <cstdio>

// Small context-local diagnostics, also exercised by RenderingValidation.
// Call on the context-owning thread; groups must end on the same current context.
namespace GEngine::GLDebug
{
    inline void ConfigureContext()
    {
#ifdef GENGINE_CONFIG_DEBUG
        if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG) != 0)
            std::fprintf(stderr, "[OpenGL] Debug context request unavailable: %s\n", SDL_GetError());
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
#endif
    }

    inline SDL_GLContext CreateContext(SDL_Window* window)
    {
        auto context = SDL_GL_CreateContext(window);
#ifdef GENGINE_CONFIG_DEBUG
        if (!context)
        {
            std::fprintf(stderr, "[OpenGL] Debug context creation failed: %s; retrying without debug flag\n", SDL_GetError());
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
            context = SDL_GL_CreateContext(window);
        }
#endif
        return context;
    }

#ifdef GENGINE_CONFIG_DEBUG
    inline const char* Source(GLenum value) noexcept
    {
        switch (value)
        {
        case GL_DEBUG_SOURCE_API: return "api";
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "window-system";
        case GL_DEBUG_SOURCE_SHADER_COMPILER: return "shader-compiler";
        case GL_DEBUG_SOURCE_THIRD_PARTY: return "third-party";
        case GL_DEBUG_SOURCE_APPLICATION: return "application";
        case GL_DEBUG_SOURCE_OTHER: return "other";
        default: return "unknown";
        }
    }

    inline const char* Type(GLenum value) noexcept
    {
        switch (value)
        {
        case GL_DEBUG_TYPE_ERROR: return "error";
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "deprecated";
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "undefined-behavior";
        case GL_DEBUG_TYPE_PORTABILITY: return "portability";
        case GL_DEBUG_TYPE_PERFORMANCE: return "performance";
        case GL_DEBUG_TYPE_MARKER: return "marker";
        case GL_DEBUG_TYPE_PUSH_GROUP: return "push-group";
        case GL_DEBUG_TYPE_POP_GROUP: return "pop-group";
        case GL_DEBUG_TYPE_OTHER: return "other";
        default: return "unknown";
        }
    }

    inline const char* Severity(GLenum value) noexcept
    {
        switch (value)
        {
        case GL_DEBUG_SEVERITY_HIGH: return "high";
        case GL_DEBUG_SEVERITY_MEDIUM: return "medium";
        case GL_DEBUG_SEVERITY_LOW: return "low";
        case GL_DEBUG_SEVERITY_NOTIFICATION: return "notification";
        default: return "unknown";
        }
    }

    inline void APIENTRY Message(GLenum source, GLenum type, GLuint id, GLenum severity,
        GLsizei length, const GLchar* message, const void*) noexcept
    {
        // Synchronous delivery keeps this on the GL caller. No GL calls, throwing,
        // user-owned callback state, debugger traps, or per-frame polling here.
        std::fprintf(stderr, "[OpenGL] source=%s(0x%x) type=%s(0x%x) severity=%s(0x%x) id=%u message=%.*s\n",
            Source(source), source, Type(type), type, Severity(severity), severity, id,
            length > 0 ? length : 0, message ? message : "");
        std::fflush(stderr);
    }

    inline bool Available() noexcept
    {
        return (GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug) && glDebugMessageCallback
            && glDebugMessageControl && glPushDebugGroup && glPopDebugGroup;
    }
#endif

    inline bool Initialize()
    {
#ifdef GENGINE_CONFIG_DEBUG
        if (!Available())
        {
            std::fprintf(stderr, "[OpenGL] KHR_debug unavailable; debug diagnostics disabled\n");
            return false;
        }
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(&Message, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        // Keep errors and high/medium/low messages. Drop notification chatter and
        // automatic group-entry/exit messages, while retaining the debugger groups.
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_PUSH_GROUP, GL_DONT_CARE, 0, nullptr, GL_FALSE);
        glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_POP_GROUP, GL_DONT_CARE, 0, nullptr, GL_FALSE);
        GLint flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        std::fprintf(stderr, "[OpenGL] KHR_debug callback active; synchronous=yes debug-context=%s notifications=filtered\n",
            (flags & GL_CONTEXT_FLAG_DEBUG_BIT) ? "yes" : "no (driver diagnostics may be limited)");
        return true;
#else
        return false;
#endif
    }

    class Group
    {
    public:
        explicit Group(const char* label)
        {
#ifdef GENGINE_CONFIG_DEBUG
            m_Active = SDL_GL_GetCurrentContext() && Available() && glIsEnabled(GL_DEBUG_OUTPUT);
            if (m_Active) glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, label);
#else
            (void)label;
#endif
        }
        ~Group()
        {
#ifdef GENGINE_CONFIG_DEBUG
            if (m_Active) glPopDebugGroup();
#endif
        }
        Group(const Group&) = delete;
        Group& operator=(const Group&) = delete;
    private:
#ifdef GENGINE_CONFIG_DEBUG
        bool m_Active = false;
#endif
    };
}
