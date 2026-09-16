#pragma once
// Backend implementation/probe only. Normal consumers include Texture.h instead.
#include "Assets/Textures/Texture.h"
#include <glad/glad.h>
#include <sdl2/SDL.h>
#include <functional>

namespace GEngine::Asset::AssetDetail
{
    struct AttachmentState
    {
        std::function<GLuint()> currentName;
        GLenum target;
        SDL_GLContext context;
        GLint unitLimit;
    };
    struct TextureBackend
    {
        static AttachmentView Borrow(std::function<GLuint()> currentName, GLenum target);
        static std::expected<GLuint, TextureError> Name(const TextureView&);
        static GLenum Target(const TextureView&);
        static std::expected<void, TextureError> Bind(const TextureView&, std::uint32_t unit);
    };
}
