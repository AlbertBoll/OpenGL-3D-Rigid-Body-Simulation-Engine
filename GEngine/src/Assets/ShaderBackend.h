#pragma once

// Backend-private access for existing renderer implementations and driver fixtures.
// Never include this file from a normal consumer header.
#include "Assets/Shaders/Shader.h"
#include <map>
#include <thread>
#include <glad/glad.h>
#include <sdl2/SDL.h>

namespace GEngine::Asset
{
    struct ShaderStorage
    {
        GLuint program{};
        bool linked{};
        SDL_GLContext context{};
        // Captured only for real GPU owners; empty/CPU-only owner retirement has
        // no platform import. The callback still checks the actual current context.
        decltype(&SDL_GL_GetCurrentContext) currentContext{};
        std::thread::id thread;
        std::map<std::string, GLint> uniforms;
        std::vector<GLuint> stages;
        ~ShaderStorage();
        void RequireOwner() const noexcept;
        void RetireStages() noexcept;
    };
    struct ShaderBackendAccess
    {
        static GLuint Program(const Shader& shader) noexcept
        { return shader.m_Storage ? shader.m_Storage->program : 0; }
        static void RequireBindable(const Shader& shader)
        { AssetDetail::RequireInvariant(shader.IsLinked()); shader.m_Storage->RequireOwner(); }
        static const auto& Uniforms(const Shader& shader) { return shader.m_Storage->uniforms; }
        static void BindTexture(Shader& shader, const char* uniform, GLenum target, GLuint name, GLuint unit);
    };
}
