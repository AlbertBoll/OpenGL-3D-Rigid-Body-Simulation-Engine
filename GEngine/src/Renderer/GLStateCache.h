#pragma once

// Backend-private synchronous submission state. Never a consumer include.
// GL names are transient state values, not resource ownership or engine identity.
#include "Core/RenderCounters.h"
#include <array>
#include <cassert>
#include <concepts>
#include <cstdlib>
#include <optional>
#include <tuple>

namespace GEngine::RenderBackend
{
    class GLStateCache final
    {
        template<std::equality_comparable T> struct Value
        {
            std::optional<T> known;
            bool Change(const T& desired)
            {
                if (known && *known == desired) return false;
                known = desired; return true;
            }
        };
        template<class... T> using Values = Value<std::tuple<T...>>;
        struct Unit { std::array<Value<GLuint>, 4> textures; Value<GLuint> sampler; };
        struct State
        {
            Value<GLuint> program, vao, draw, read, activeTexture, uniformBuffer;
            Value<GLenum> drawBuffer, readBuffer, depthFunc, cullFace, frontFace, polygon;
            Value<GLboolean> depthMask;
            Value<GLuint> stencilMask;
            Value<GLfloat> lineWidth;
            Value<GLdouble> clearDepth;
            Values<GLint, GLint, GLsizei, GLsizei> viewport, scissor;
            Values<GLboolean, GLboolean, GLboolean, GLboolean> colorMask;
            Values<GLenum, GLenum> blendEquation;
            Values<GLenum, GLenum, GLenum, GLenum> blendFunction;
            Values<GLdouble, GLdouble> depthRange;
            Values<GLfloat, GLfloat, GLfloat, GLfloat> clearColor;
            std::array<Value<bool>, 18> capabilities;
            // Higher units compare queried state without allocating cache storage.
            std::array<Unit, 32> units;
        } state;
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        inline static thread_local GLStateCache* active = nullptr;
        static void RequireInvariant(bool condition)
        { if (!condition) { assert(condition); std::abort(); } }
        // The constructor verifies the registered owner; thread-local activation
        // and current-context identity preserve it for this synchronous scope.
        void Require() const
        {
            RequireInvariant(SDL_GL_GetCurrentContext() == context && active == this);
        }
        template<std::equality_comparable T, class F>
            requires std::invocable<F>
        void Set(Value<T>& value, const T& desired, F&& emit)
        {
            Require();
            if (value.Change(desired)) emit();
        }
    public:
        GLStateCache()
        {
            RequireInvariant(!active && context);
            GLContextThread::RequireCurrent("begin submission state cache");
            active = this;
            glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &textureUnits);
        }
        ~GLStateCache() { Require(); active = nullptr; }
        GLStateCache(const GLStateCache&) = delete;
        GLStateCache& operator=(const GLStateCache&) = delete;
        static GLStateCache* Current() noexcept { return active; }
        static GLStateCache& Get() { RequireInvariant(active != nullptr); return *active; }
        GLint textureUnits{};
        // Normal lifetime has no external callbacks. Each Submit starts unknown
        // and discards its cache before restore/UI/readback/uploads/context switch.
        void Invalidate() { Require(); state = {}; }
        void Program(GLuint n) { Set(state.program,n,[&] { glUseProgram(n); }); }
        void VertexArray(GLuint n) { Set(state.vao,n,[&] { glBindVertexArray(n); }); }
        void UniformBuffer(GLuint n) { Set(state.uniformBuffer,n,[&] { glBindBufferBase(GL_UNIFORM_BUFFER,0,n); }); }
        void Framebuffer(GLenum target, GLuint n)
        {
            Require();
            const bool draw = target != GL_READ_FRAMEBUFFER, read = target != GL_DRAW_FRAMEBUFFER;
            const bool changeDraw = draw && state.draw.Change(n), changeRead = read && state.read.Change(n);
            if (changeDraw) state.drawBuffer = {};
            if (changeRead) state.readBuffer = {};
            if (changeDraw || changeRead) glBindFramebuffer(target,n);
        }
        void DrawBuffer(GLenum v) { Set(state.drawBuffer,v,[&] { glDrawBuffer(v); }); }
        void ReadBuffer(GLenum v) { Set(state.readBuffer,v,[&] { glReadBuffer(v); }); }
        void Texture(GLuint unit, GLenum target, GLuint name)
        {
            Set(state.activeTexture,unit,[&] { glActiveTexture(GL_TEXTURE0+unit); });
            const auto slot = target==GL_TEXTURE_2D ? 0u : target==GL_TEXTURE_CUBE_MAP ? 1u
                : target==GL_TEXTURE_2D_ARRAY ? 2u : target==GL_TEXTURE_2D_MULTISAMPLE ? 3u : 4u;
            if (unit < state.units.size() && slot < 4)
                Set(state.units[unit].textures[slot],name,[&] { glBindTexture(target,name); });
            else {
                constexpr GLenum bindings[]{GL_TEXTURE_BINDING_2D,GL_TEXTURE_BINDING_CUBE_MAP,
                    GL_TEXTURE_BINDING_2D_ARRAY,GL_TEXTURE_BINDING_2D_MULTISAMPLE};
                RequireInvariant(slot<std::size(bindings));
                GLint current{};glGetIntegerv(bindings[slot],&current);
                if(static_cast<GLuint>(current)!=name) glBindTexture(target,name);
            }
        }
        void Sampler(GLuint unit, GLuint name)
        {
            Require();
            if (unit < state.units.size()) Set(state.units[unit].sampler,name,[&] { glBindSampler(unit,name); });
            else {
                GLint current{};glGetIntegeri_v(GL_SAMPLER_BINDING,unit,&current);
                if(static_cast<GLuint>(current)!=name) glBindSampler(unit,name);
            }
        }
        void Toggle(GLenum cap, bool enabled)
        {
            constexpr GLenum caps[]{GL_SCISSOR_TEST,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_BLEND,GL_CULL_FACE,
                GL_RASTERIZER_DISCARD,GL_POLYGON_OFFSET_FILL,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE,
                GL_LINE_SMOOTH,GL_SAMPLE_MASK,GL_DEPTH_CLAMP,GL_FRAMEBUFFER_SRGB,GL_DITHER,GL_MULTISAMPLE,
                GL_TEXTURE_CUBE_MAP_SEAMLESS,GL_POLYGON_OFFSET_LINE,GL_POLYGON_OFFSET_POINT};
            for (std::size_t i=0;i<std::size(caps);++i) if (caps[i]==cap) {
                Set(state.capabilities[i],enabled,[&] { if(enabled) glEnable(cap); else glDisable(cap); }); return;
            }
            RequireInvariant(false);
        }
        void Viewport(GLint x, GLint y, GLsizei w, GLsizei h)
        { Set(state.viewport,std::tuple{x,y,w,h},[&] { glViewport(x,y,w,h); }); }
        void Scissor(GLint x, GLint y, GLsizei w, GLsizei h)
        { Set(state.scissor,std::tuple{x,y,w,h},[&] { glScissor(x,y,w,h); }); }
        void DepthMask(GLboolean v) { Set(state.depthMask,v,[&] { glDepthMask(v); }); }
        void DepthFunc(GLenum v) { Set(state.depthFunc,v,[&] { glDepthFunc(v); }); }
        void ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
        { Set(state.colorMask,std::tuple{r,g,b,a},[&] { glColorMask(r,g,b,a); }); }
        void StencilMask(GLuint v) { Set(state.stencilMask,v,[&] { glStencilMask(v); }); }
        void BlendEquation(GLenum rgb, GLenum alpha)
        { Set(state.blendEquation,std::tuple{rgb,alpha},[&] { glBlendEquationSeparate(rgb,alpha); }); }
        void BlendFunction(GLenum sr, GLenum dr, GLenum sa, GLenum da)
        { Set(state.blendFunction,std::tuple{sr,dr,sa,da},[&] { glBlendFuncSeparate(sr,dr,sa,da); }); }
        void CullFace(GLenum v) { Set(state.cullFace,v,[&] { glCullFace(v); }); }
        void FrontFace(GLenum v) { Set(state.frontFace,v,[&] { glFrontFace(v); }); }
        void Polygon(GLenum v) { Set(state.polygon,v,[&] { glPolygonMode(GL_FRONT_AND_BACK,v); }); }
        void LineWidth(GLfloat v) { Set(state.lineWidth,v,[&] { glLineWidth(v); }); }
        void DepthRange(GLdouble nearValue, GLdouble farValue)
        { Set(state.depthRange,std::tuple{nearValue,farValue},[&] { glDepthRange(nearValue,farValue); }); }
        void ClearDepth(GLdouble v) { Set(state.clearDepth,v,[&] { glClearDepth(v); }); }
        void ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
        { Set(state.clearColor,std::tuple{r,g,b,a},[&] { glClearColor(r,g,b,a); }); }
    };
}
