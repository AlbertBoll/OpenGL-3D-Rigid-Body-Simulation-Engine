#include "../GEngine/src/Core/FramebufferBackend.h"
// Run through test_shadow_configuration.py with the repository's Windows toolchain.
#include "gepch.h"
#include "Core/RenderTarget.h"
#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    PFNGLTEXIMAGE2DPROC originalImage2D;
    PFNGLTEXIMAGE3DPROC originalImage3D;
    PFNGLGETERRORPROC originalGetError;
    GLuint failedTexture = 0;
    GLuint failedFramebuffer = 0;
    bool injectedError = false;

    void APIENTRY FailImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)
    {
        GLint texture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &texture);
        failedTexture = static_cast<GLuint>(texture);
        GLint framebuffer = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer); failedFramebuffer = static_cast<GLuint>(framebuffer);
        injectedError = true;
    }

    void APIENTRY FailImage3D(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)
    {
        GLint texture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &texture);
        failedTexture = static_cast<GLuint>(texture);
        GLint framebuffer = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer); failedFramebuffer = static_cast<GLuint>(framebuffer);
        injectedError = true;
    }

    GLenum APIENTRY AllocationError()
    {
        if (injectedError)
        {
            GLint framebuffer = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
            injectedError = false;
            return GL_OUT_OF_MEMORY;
        }
        return originalGetError();
    }

    void CheckTexture(GLenum target, int width, int layers)
    {
        GLint actualWidth = 0, height = 0, depth = 0, bits = 0, format = 0;
        glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &actualWidth);
        glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &height);
        glGetTexLevelParameteriv(target, 0, GL_TEXTURE_DEPTH, &depth);
        glGetTexLevelParameteriv(target, 0, GL_TEXTURE_DEPTH_SIZE, &bits);
        glGetTexLevelParameteriv(target, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        Require(actualWidth == width && height == width && depth == layers, "Texture dimensions differ from request");
        Require(bits > 0, "Missing depth storage");
        std::cout << "Queried texture: " << actualWidth << "x" << height << " layers=" << depth
            << " depth bits=" << bits << " internal format=" << format << std::endl;
    }

    void CheckSuccess(unsigned int resolution)
    {
        GLuint cascadeTexture = 0, pointTexture = 0, cascadeFbo = 0, pointFbo = 0;
        {
            auto cascade = ::GEngine::CascadeShadowFrameBuffer::Create(resolution, resolution, 5).value();
            auto point = ::GEngine::PointShadowFrameBuffer::Create(resolution, resolution).value();
            cascadeTexture = ::GEngine::FramebufferDetail::Backend::Depth(cascade.Buffer());
            pointTexture = ::GEngine::FramebufferDetail::Backend::Depth(point.Buffer());
            cascadeFbo = ::GEngine::FramebufferDetail::Backend::Name(cascade.Buffer());
            pointFbo = ::GEngine::FramebufferDetail::Backend::Name(point.Buffer());
            cascade.Bind();
            Require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Cascade incomplete");
            glClear(GL_DEPTH_BUFFER_BIT);
            glBindTexture(GL_TEXTURE_2D_ARRAY, cascadeTexture);
            CheckTexture(GL_TEXTURE_2D_ARRAY, static_cast<int>(resolution), 6);
            point.Bind();
            Require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Point incomplete");
            glClear(GL_DEPTH_BUFFER_BIT);
            glBindTexture(GL_TEXTURE_CUBE_MAP, pointTexture);
            for (unsigned int face = 0; face < 6; ++face)
                CheckTexture(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, static_cast<int>(resolution), 1);
            point.UnBind();
        }
        Require(!glIsTexture(cascadeTexture) && !glIsTexture(pointTexture)
            && !glIsFramebuffer(cascadeFbo) && !glIsFramebuffer(pointFbo), "Shadow objects leaked");
        Require(glGetError() == GL_NO_ERROR, "GL error after successful allocation/clear/destruction");
    }

    void CheckFailure(bool point, bool injectOom)
    {
        failedTexture = failedFramebuffer = 0;
        if (injectOom)
        {
            glad_glTexImage2D = FailImage2D;
            glad_glTexImage3D = FailImage3D;
            glad_glGetError = AllocationError;
        }
        const unsigned int size = injectOom ? 32 : 8193;
        const auto check = [&](const auto& result)
        {
            Require(!result, "Expected shadow creation failure");
            Require(result.error().code == (injectOom ? ::GEngine::FramebufferErrorCode::Storage : ::GEngine::FramebufferErrorCode::InvalidDescription), "Wrong typed shadow error");
            std::cout << "Expected failure: " << result.error().message << std::endl;
        };
        if (point) check(::GEngine::PointShadowFrameBuffer::Create(size,size));
        else check(::GEngine::CascadeShadowFrameBuffer::Create(size,size,5));
        glad_glTexImage2D = originalImage2D;
        glad_glTexImage3D = originalImage3D;
        if (injectOom) Require(glGetError() == GL_OUT_OF_MEMORY, "Synthetic OOM was consumed by production");
        glad_glGetError = originalGetError;
        if (injectOom)
            Require(failedTexture && failedFramebuffer && !glIsTexture(failedTexture)
                && !glIsFramebuffer(failedFramebuffer), "Partial allocation leaked");
        GLint binding = -1;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &binding);
        Require(binding == 0 && glGetError() == GL_NO_ERROR, "Failure left invalid GL state");
    }
}

int main(int, char**)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 2;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("Shadow framebuffer probe", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    int result = 1;
    try
    {
        Require(context && gladLoadGLLoader(SDL_GL_GetProcAddress), SDL_GetError());
        ::GEngine::Log::Initialize();
        std::cout << "GPU: " << glGetString(GL_RENDERER) << "; GL: " << glGetString(GL_VERSION) << std::endl;
        originalImage2D = glad_glTexImage2D;
        originalImage3D = glad_glTexImage3D;
        originalGetError = glad_glGetError;
        CheckSuccess(2048);
        CheckSuccess(4096);
        CheckFailure(false, false);
        CheckFailure(true, false);
        CheckFailure(false, true);
        CheckFailure(true, true);
        CheckSuccess(128); // Recovery after both injected allocation failures.
        std::cout << "PASS: dimensions, completeness, clear, cleanup, incomplete targets, injected OOM and recovery" << std::endl;
        result = 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << std::endl;
    }
    if (context) SDL_GL_DeleteContext(context);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
