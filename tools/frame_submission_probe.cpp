#include "Renderer/FrameSubmission.h"
#include "Renderer/RenderExtraction.h"
#include "Renderer/FrameScheduler.h"
#include "Renderer/PassTiming.h"
#include <type_traits>
#include "../GEngine/src/Renderer/DrawOrdering.h"
using namespace GEngine;
using namespace GEngine::Asset;
using namespace GEngine::Component;
static_assert(!std::is_copy_constructible_v<FrameSubmission>);
static_assert(!std::is_copy_constructible_v<PassTiming>);
static_assert(std::is_nothrow_move_constructible_v<PassTiming>);
static_assert(!std::is_copy_constructible_v<RenderFrame>);
static_assert(std::same_as<decltype(std::declval<const RenderFrame&>().Draws()),std::span<const DrawItem>>);

#ifndef SUBMISSION_SCHEMA_ONLY
#include "Core/BaseApp.h"
#include "Core/Window.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLContextThread.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShapeManager.h"
#include "Managers/ShaderManager.h"
#include "Scene/_Entity.h"
#include "Physics/PhysicsBody.h"
#include "Physics/Shape.h"
#include "../GEngine/src/Assets/ShaderBackend.h"
#include "../GEngine/src/Renderer/GLStateCache.h"
#include "../GEngine/src/Renderer/SubmissionUploads.h"
#include "../GEngine/src/Core/FramebufferBackend.h"
#include <chrono>
#include <numeric>
#include <glm/gtx/quaternion.hpp>
#include <print>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <fstream>
#include <imgui/imgui.h>
#include <set>

// Link the actual application request into this production-library probe.
extern WindowProperties winProp;
namespace WindowPlacementProbe
{
    struct Request { int x{}, y{}, width{}, height{}; Uint32 flags{}; } last;
    unsigned creations{}, repositions{};
    void* Library()
    {
        struct Module {
            void* value=SDL_LoadObject("SDL2.dll");
            ~Module() { if(value) SDL_UnloadObject(value); }
        };
        static Module module;return module.value;
    }
}
// Observe the actual SDL boundary, then forward unchanged to the real DLL.
// No desktop-origin, monitor-size or DPI-specific coordinate expectation.
extern "C" SDL_Window* SDLCALL SDL_CreateWindow(const char* title,int x,int y,int w,int h,Uint32 flags)
{
    using namespace WindowPlacementProbe;
    static const auto create=reinterpret_cast<decltype(&SDL_CreateWindow)>(SDL_LoadFunction(Library(),"SDL_CreateWindow"));
    if(!create) return nullptr;
    last={x,y,w,h,flags};++creations;
    return create(title,x,y,w,h,flags);
}
extern "C" void SDLCALL SDL_SetWindowPosition(SDL_Window* window,int x,int y)
{
    using namespace WindowPlacementProbe;
    static const auto position=reinterpret_cast<decltype(&SDL_SetWindowPosition)>(SDL_LoadFunction(Library(),"SDL_SetWindowPosition"));
    if(!position) std::exit(1);
    ++repositions;position(window,x,y);
}

namespace
{
    int checks{};
    template<class T> void Check(const T& value,const char* message)
    {
        ++checks;
        if (!static_cast<bool>(value)) {std::println(stderr,"[FAIL] {}",message);std::exit(1);}
    }
    template<class T,class E> T Take(std::expected<T,E>&& value,const char* message)
    {
        if (!value) {
            if constexpr (std::same_as<E,SceneResourceError>) std::println(stderr,"{}",DescribeSceneResourceError(value.error()));
            if constexpr (std::same_as<E,SubmissionError>) std::println(stderr,"{}",DescribeSubmissionError(value.error()));
            if constexpr (std::same_as<E,ScheduleError>) std::println(stderr,"{}",DescribeScheduleError(value.error()));
        }
        Check(value,message);return std::move(*value);
    }
    namespace UploadCalls
    {
        std::uint64_t uniforms{}, uniformBytes{}, buffers{}, bufferBytes{};
        GLuint lastFrameBuffer{}, lastMaterialBuffer{};
        decltype(glad_glUniform1i) realUniform1i{};
        void APIENTRY Uniform1i(GLint location, GLint value) { ++uniforms; uniformBytes+=4; realUniform1i(location,value); }
        decltype(glad_glUniform1ui) realUniform1ui{};
        void APIENTRY Uniform1ui(GLint location, GLuint value) { ++uniforms; uniformBytes+=4; realUniform1ui(location,value); }
        decltype(glad_glUniform1f) realUniform1f{};
        void APIENTRY Uniform1f(GLint location, GLfloat value) { ++uniforms; uniformBytes+=4; realUniform1f(location,value); }
        decltype(glad_glUniform2fv) realUniform2fv{};
        void APIENTRY Uniform2fv(GLint location, GLsizei count, const GLfloat* value) { ++uniforms; uniformBytes+=8*count; realUniform2fv(location,count,value); }
        decltype(glad_glUniform3fv) realUniform3fv{};
        void APIENTRY Uniform3fv(GLint location, GLsizei count, const GLfloat* value) { ++uniforms; uniformBytes+=12*count; realUniform3fv(location,count,value); }
        decltype(glad_glUniform4fv) realUniform4fv{};
        void APIENTRY Uniform4fv(GLint location, GLsizei count, const GLfloat* value) { ++uniforms; uniformBytes+=16*count; realUniform4fv(location,count,value); }
        decltype(glad_glUniformMatrix4fv) realUniformMatrix4fv{};
        void APIENTRY UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { ++uniforms; uniformBytes+=64*count; realUniformMatrix4fv(location,count,transpose,value); }
        decltype(glad_glNamedBufferData) realNamedBufferData{};
        void APIENTRY NamedBufferData(GLuint buffer, GLsizeiptr size, const void* data, GLenum usage) { ++buffers; bufferBytes+=size; if(usage==GL_STREAM_DRAW && size==sizeof(RenderBackend::PackedFrame)) lastFrameBuffer=buffer;else if(usage==GL_DYNAMIC_DRAW) lastMaterialBuffer=buffer; realNamedBufferData(buffer,size,data,usage); }
        decltype(glad_glNamedBufferSubData) realNamedBufferSubData{};
        void APIENTRY NamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data) { ++buffers; bufferBytes+=size; realNamedBufferSubData(buffer,offset,size,data); }
        struct Observe {
            Observe() { uniforms=uniformBytes=buffers=bufferBytes=0;
                realUniform1i=glad_glUniform1i;glad_glUniform1i=Uniform1i;
                realUniform1ui=glad_glUniform1ui;glad_glUniform1ui=Uniform1ui;
                realUniform1f=glad_glUniform1f;glad_glUniform1f=Uniform1f;
                realUniform2fv=glad_glUniform2fv;glad_glUniform2fv=Uniform2fv;
                realUniform3fv=glad_glUniform3fv;glad_glUniform3fv=Uniform3fv;
                realUniform4fv=glad_glUniform4fv;glad_glUniform4fv=Uniform4fv;
                realUniformMatrix4fv=glad_glUniformMatrix4fv;glad_glUniformMatrix4fv=UniformMatrix4fv;
                realNamedBufferData=glad_glNamedBufferData;glad_glNamedBufferData=NamedBufferData;
                realNamedBufferSubData=glad_glNamedBufferSubData;glad_glNamedBufferSubData=NamedBufferSubData;
            }
            ~Observe() {
                glad_glUniform1i=realUniform1i;
                glad_glUniform1ui=realUniform1ui;
                glad_glUniform1f=realUniform1f;
                glad_glUniform2fv=realUniform2fv;
                glad_glUniform3fv=realUniform3fv;
                glad_glUniform4fv=realUniform4fv;
                glad_glUniformMatrix4fv=realUniformMatrix4fv;
                glad_glNamedBufferData=realNamedBufferData;
                glad_glNamedBufferSubData=realNamedBufferSubData;
            }
        };
    }
    namespace DriverCalls
    {
        std::array<std::uint64_t,28> counts{};
        decltype(glad_glUseProgram) realUseProgram{};
        void APIENTRY UseProgram(GLuint a) { ++counts[0]; realUseProgram(a); }
        decltype(glad_glBindVertexArray) realBindVertexArray{};
        void APIENTRY BindVertexArray(GLuint a) { ++counts[1]; realBindVertexArray(a); }
        decltype(glad_glBindFramebuffer) realBindFramebuffer{};
        void APIENTRY BindFramebuffer(GLenum a, GLuint b) { ++counts[2]; realBindFramebuffer(a,b); }
        decltype(glad_glActiveTexture) realActiveTexture{};
        void APIENTRY ActiveTexture(GLenum a) { ++counts[3]; realActiveTexture(a); }
        decltype(glad_glBindTexture) realBindTexture{};
        void APIENTRY BindTexture(GLenum a, GLuint b) { ++counts[4]; realBindTexture(a,b); }
        decltype(glad_glBindSampler) realBindSampler{};
        void APIENTRY BindSampler(GLuint a, GLuint b) { ++counts[5]; realBindSampler(a,b); }
        decltype(glad_glEnable) realEnable{};
        void APIENTRY Enable(GLenum a) { ++counts[6]; realEnable(a); }
        decltype(glad_glDisable) realDisable{};
        void APIENTRY Disable(GLenum a) { ++counts[7]; realDisable(a); }
        decltype(glad_glViewport) realViewport{};
        void APIENTRY Viewport(GLint a, GLint b, GLsizei c, GLsizei d) { ++counts[8]; realViewport(a,b,c,d); }
        decltype(glad_glScissor) realScissor{};
        void APIENTRY Scissor(GLint a, GLint b, GLsizei c, GLsizei d) { ++counts[9]; realScissor(a,b,c,d); }
        decltype(glad_glDepthMask) realDepthMask{};
        void APIENTRY DepthMask(GLboolean a) { ++counts[10]; realDepthMask(a); }
        decltype(glad_glDepthFunc) realDepthFunc{};
        void APIENTRY DepthFunc(GLenum a) { ++counts[11]; realDepthFunc(a); }
        decltype(glad_glColorMask) realColorMask{};
        void APIENTRY ColorMask(GLboolean a, GLboolean b, GLboolean c, GLboolean d) { ++counts[12]; realColorMask(a,b,c,d); }
        decltype(glad_glStencilMask) realStencilMask{};
        void APIENTRY StencilMask(GLuint a) { ++counts[13]; realStencilMask(a); }
        decltype(glad_glBlendEquationSeparate) realBlendEquationSeparate{};
        void APIENTRY BlendEquationSeparate(GLenum a, GLenum b) { ++counts[14]; realBlendEquationSeparate(a,b); }
        decltype(glad_glBlendFuncSeparate) realBlendFuncSeparate{};
        void APIENTRY BlendFuncSeparate(GLenum a, GLenum b, GLenum c, GLenum d) { ++counts[15]; realBlendFuncSeparate(a,b,c,d); }
        decltype(glad_glCullFace) realCullFace{};
        void APIENTRY CullFace(GLenum a) { ++counts[16]; realCullFace(a); }
        decltype(glad_glFrontFace) realFrontFace{};
        void APIENTRY FrontFace(GLenum a) { ++counts[17]; realFrontFace(a); }
        decltype(glad_glPolygonMode) realPolygonMode{};
        void APIENTRY PolygonMode(GLenum a, GLenum b) { ++counts[18]; realPolygonMode(a,b); }
        decltype(glad_glLineWidth) realLineWidth{};
        void APIENTRY LineWidth(GLfloat a) { ++counts[19]; realLineWidth(a); }
        decltype(glad_glDepthRange) realDepthRange{};
        void APIENTRY DepthRange(GLdouble a, GLdouble b) { ++counts[20]; realDepthRange(a,b); }
        decltype(glad_glClearDepth) realClearDepth{};
        void APIENTRY ClearDepth(GLdouble a) { ++counts[21]; realClearDepth(a); }
        decltype(glad_glClearColor) realClearColor{};
        void APIENTRY ClearColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { ++counts[22]; realClearColor(a,b,c,d); }
        decltype(glad_glDrawBuffer) realDrawBuffer{};
        void APIENTRY DrawBuffer(GLenum a) { ++counts[23]; realDrawBuffer(a); }
        decltype(glad_glReadBuffer) realReadBuffer{};
        void APIENTRY ReadBuffer(GLenum a) { ++counts[24]; realReadBuffer(a); }
        decltype(glad_glBindBufferBase) realBindBufferBase{};
        void APIENTRY BindBufferBase(GLenum a, GLuint b, GLuint c) { ++counts[25]; realBindBufferBase(a,b,c); }
        decltype(glad_glDrawArrays) realDrawArrays{};
        void APIENTRY DrawArrays(GLenum a, GLint b, GLsizei c) { ++counts[26]; realDrawArrays(a,b,c); }
        decltype(glad_glDrawElements) realDrawElements{};
        void APIENTRY DrawElements(GLenum a, GLsizei b, GLenum c, const void* d) { ++counts[27]; realDrawElements(a,b,c,d); }
        struct Observe
        {
            Observe() { counts={};
                realUseProgram=glad_glUseProgram; glad_glUseProgram=UseProgram;
                realBindVertexArray=glad_glBindVertexArray; glad_glBindVertexArray=BindVertexArray;
                realBindFramebuffer=glad_glBindFramebuffer; glad_glBindFramebuffer=BindFramebuffer;
                realActiveTexture=glad_glActiveTexture; glad_glActiveTexture=ActiveTexture;
                realBindTexture=glad_glBindTexture; glad_glBindTexture=BindTexture;
                realBindSampler=glad_glBindSampler; glad_glBindSampler=BindSampler;
                realEnable=glad_glEnable; glad_glEnable=Enable;
                realDisable=glad_glDisable; glad_glDisable=Disable;
                realViewport=glad_glViewport; glad_glViewport=Viewport;
                realScissor=glad_glScissor; glad_glScissor=Scissor;
                realDepthMask=glad_glDepthMask; glad_glDepthMask=DepthMask;
                realDepthFunc=glad_glDepthFunc; glad_glDepthFunc=DepthFunc;
                realColorMask=glad_glColorMask; glad_glColorMask=ColorMask;
                realStencilMask=glad_glStencilMask; glad_glStencilMask=StencilMask;
                realBlendEquationSeparate=glad_glBlendEquationSeparate; glad_glBlendEquationSeparate=BlendEquationSeparate;
                realBlendFuncSeparate=glad_glBlendFuncSeparate; glad_glBlendFuncSeparate=BlendFuncSeparate;
                realCullFace=glad_glCullFace; glad_glCullFace=CullFace;
                realFrontFace=glad_glFrontFace; glad_glFrontFace=FrontFace;
                realPolygonMode=glad_glPolygonMode; glad_glPolygonMode=PolygonMode;
                realLineWidth=glad_glLineWidth; glad_glLineWidth=LineWidth;
                realDepthRange=glad_glDepthRange; glad_glDepthRange=DepthRange;
                realClearDepth=glad_glClearDepth; glad_glClearDepth=ClearDepth;
                realClearColor=glad_glClearColor; glad_glClearColor=ClearColor;
                realDrawBuffer=glad_glDrawBuffer; glad_glDrawBuffer=DrawBuffer;
                realReadBuffer=glad_glReadBuffer; glad_glReadBuffer=ReadBuffer;
                realBindBufferBase=glad_glBindBufferBase; glad_glBindBufferBase=BindBufferBase;
                realDrawArrays=glad_glDrawArrays; glad_glDrawArrays=DrawArrays;
                realDrawElements=glad_glDrawElements; glad_glDrawElements=DrawElements;
            }
            ~Observe() {
                glad_glUseProgram=realUseProgram;
                glad_glBindVertexArray=realBindVertexArray;
                glad_glBindFramebuffer=realBindFramebuffer;
                glad_glActiveTexture=realActiveTexture;
                glad_glBindTexture=realBindTexture;
                glad_glBindSampler=realBindSampler;
                glad_glEnable=realEnable;
                glad_glDisable=realDisable;
                glad_glViewport=realViewport;
                glad_glScissor=realScissor;
                glad_glDepthMask=realDepthMask;
                glad_glDepthFunc=realDepthFunc;
                glad_glColorMask=realColorMask;
                glad_glStencilMask=realStencilMask;
                glad_glBlendEquationSeparate=realBlendEquationSeparate;
                glad_glBlendFuncSeparate=realBlendFuncSeparate;
                glad_glCullFace=realCullFace;
                glad_glFrontFace=realFrontFace;
                glad_glPolygonMode=realPolygonMode;
                glad_glLineWidth=realLineWidth;
                glad_glDepthRange=realDepthRange;
                glad_glClearDepth=realClearDepth;
                glad_glClearColor=realClearColor;
                glad_glDrawBuffer=realDrawBuffer;
                glad_glReadBuffer=realReadBuffer;
                glad_glBindBufferBase=realBindBufferBase;
                glad_glDrawArrays=realDrawArrays;
                glad_glDrawElements=realDrawElements;
            }
        };
    }
    void StateCacheTests()
    {
        using Cache=RenderBackend::GLStateCache;
        GLuint vao{},textures[2]{},sampler{},fb[2]{};
        glGenVertexArrays(1,&vao);glGenTextures(2,textures);glGenSamplers(1,&sampler);glGenFramebuffers(2,fb);
        {
            DriverCalls::Observe observe;
            Cache cache;
            auto request=[&] {
                cache.Program(0);cache.VertexArray(vao);cache.Framebuffer(GL_FRAMEBUFFER,fb[0]);
                cache.DrawBuffer(GL_NONE);cache.ReadBuffer(GL_NONE);
                cache.Texture(0,GL_TEXTURE_2D,textures[0]);cache.Sampler(0,sampler);
                cache.Toggle(GL_DEPTH_TEST,true);cache.Toggle(GL_CULL_FACE,true);cache.Toggle(GL_BLEND,true);
                cache.Viewport(0,0,64,64);cache.Scissor(0,0,32,32);cache.Toggle(GL_SCISSOR_TEST,false);
                cache.DepthFunc(GL_LESS);cache.DepthMask(GL_TRUE);cache.ColorMask(GL_TRUE,GL_FALSE,GL_TRUE,GL_FALSE);
                cache.StencilMask(0x12);cache.BlendEquation(GL_FUNC_ADD,GL_FUNC_ADD);
                cache.BlendFunction(GL_ONE,GL_ZERO,GL_ONE,GL_ZERO);cache.CullFace(GL_BACK);cache.FrontFace(GL_CCW);
                cache.Polygon(GL_FILL);cache.LineWidth(1);cache.DepthRange(0,1);cache.ClearDepth(1);cache.ClearColor(0,0,0,1);
                cache.UniformBuffer(0);cache.Toggle(GL_FRAMEBUFFER_SRGB,false);
            };
            request();const auto first=DriverCalls::counts;
            request();Check(DriverCalls::counts==first,"every redundant cached request emits zero driver calls");
            cache.DepthFunc(GL_GEQUAL);cache.DepthMask(GL_FALSE);cache.ColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);
            cache.StencilMask(0x34);cache.CullFace(GL_FRONT);cache.FrontFace(GL_CW);cache.Polygon(GL_LINE);
            cache.LineWidth(3);cache.DepthRange(.25,.75);cache.BlendEquation(GL_FUNC_SUBTRACT,GL_FUNC_REVERSE_SUBTRACT);
            cache.BlendFunction(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
            cache.Viewport(2,3,44,45);cache.Scissor(4,5,22,23);cache.Toggle(GL_SCISSOR_TEST,true);
            cache.Toggle(GL_FRAMEBUFFER_SRGB,true);
            GLint integer{};glGetIntegerv(GL_DEPTH_FUNC,&integer);Check(integer==GL_GEQUAL,"depth transition reaches GL");
            GLboolean mask[4]{};glGetBooleanv(GL_COLOR_WRITEMASK,mask);Check(!mask[0]&&mask[1]&&!mask[2]&&mask[3],"color write mask transition");
            glGetBooleanv(GL_DEPTH_WRITEMASK,mask);Check(!mask[0],"depth write mask transition");
            glGetIntegerv(GL_STENCIL_WRITEMASK,&integer);Check(integer==0x34,"stencil write transition");
            glGetIntegerv(GL_CULL_FACE_MODE,&integer);Check(integer==GL_FRONT,"cull transition");
            glGetIntegerv(GL_FRONT_FACE,&integer);Check(integer==GL_CW,"front face transition");
            // OpenGL 4.6 core table 23.10: one polygon mode for front and back.
            GLint polygon{};glGetIntegerv(GL_POLYGON_MODE,&polygon);Check(polygon==GL_LINE,"polygon transition");
            GLfloat width{};glGetFloatv(GL_LINE_WIDTH,&width);Check(width==3,"line width transition");
            GLdouble range[2]{};glGetDoublev(GL_DEPTH_RANGE,range);Check(range[0]==.25&&range[1]==.75,"depth range transition");
            GLint rect[4]{};glGetIntegerv(GL_VIEWPORT,rect);Check(rect[0]==2&&rect[1]==3&&rect[2]==44&&rect[3]==45,"viewport transition");
            glGetIntegerv(GL_SCISSOR_BOX,rect);Check(rect[0]==4&&rect[1]==5&&rect[2]==22&&rect[3]==23,"scissor transition");
            Check(glIsEnabled(GL_SCISSOR_TEST)&&glIsEnabled(GL_FRAMEBUFFER_SRGB),"scissor and color-space transition");
            glGetIntegerv(GL_BLEND_EQUATION_RGB,&integer);Check(integer==GL_FUNC_SUBTRACT,"blend equation transition");
            glGetIntegerv(GL_BLEND_DST_RGB,&integer);Check(integer==GL_ONE_MINUS_SRC_ALPHA,"blend factor transition");
            cache.Texture(0,GL_TEXTURE_CUBE_MAP,textures[1]);cache.Texture(0,GL_TEXTURE_2D,textures[0]);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&integer);Check(integer==int(textures[0]),"per-target texture slots");
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&integer);Check(integer==int(textures[1]),"same-unit cube binding retained");
            cache.Sampler(0,0);glGetIntegeri_v(GL_SAMPLER_BINDING,0,&integer);Check(integer==0,"sampler transition to defaults");
            cache.Framebuffer(GL_READ_FRAMEBUFFER,fb[1]);cache.ReadBuffer(GL_NONE);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&integer);Check(integer==int(fb[1]),"read target independent");
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&integer);Check(integer==int(fb[0]),"draw target retained");
            cache.Framebuffer(GL_FRAMEBUFFER,fb[1]);cache.DrawBuffer(GL_NONE);
            cache.Framebuffer(GL_FRAMEBUFFER,fb[0]);cache.DrawBuffer(GL_NONE);cache.ReadBuffer(GL_NONE);
            if(cache.textureUnits>32) {cache.Texture(32,GL_TEXTURE_2D,textures[0]);cache.Sampler(32,sampler);
                glGetIntegeri_v(GL_SAMPLER_BINDING,32,&integer);Check(integer==int(sampler),"valid high unit selected without truncation");
                const auto before=DriverCalls::counts;cache.Texture(32,GL_TEXTURE_2D,textures[0]);cache.Sampler(32,sampler);
                Check(DriverCalls::counts==before,"high unit queries suppress redundant mutations too");}
            glDepthFunc(GL_NEVER);glViewport(1,1,1,1);glEnable(GL_FRAMEBUFFER_SRGB);
            cache.Invalidate();request();
            glGetIntegerv(GL_DEPTH_FUNC,&integer);Check(integer==GL_LESS&&!glIsEnabled(GL_FRAMEBUFFER_SRGB),"external boundary invalidation re-establishes desired state");
            Check(glGetError()==GL_NO_ERROR,"cache query fixture has no driver errors");
        }
        Check(!Cache::Current(),"cache ends at boundary");
        glBindFramebuffer(GL_FRAMEBUFFER,0);glBindVertexArray(0);glBindSampler(0,0);glBindSampler(32,0);
        glDeleteFramebuffers(2,fb);glDeleteSamplers(1,&sampler);glDeleteTextures(2,textures);glDeleteVertexArrays(1,&vao);
        glActiveTexture(GL_TEXTURE0);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_BLEND);
        std::println("[PASS] state-cache redundant/transition/external/queried-GL");
    }
    auto realAvailable=glad_glGetQueryObjectiv;
    auto realResult=glad_glGetQueryObjectui64v;
    auto realStamp=glad_glQueryCounter;
    auto realDelete=glad_glDeleteQueries;
    auto realGenerate=glad_glGenQueries;
    unsigned generateCalls{};
    unsigned availabilityCalls{},resultCalls{},stampCalls{},deletedQueries{};
    bool ready=false;
    std::set<GLuint> issued;
    void APIENTRY Available(GLuint,GLenum key,GLint* value)
    { Check(key==GL_QUERY_RESULT_AVAILABLE,"availability-only collection");++availabilityCalls;*value=ready; }
    void APIENTRY Result(GLuint query,GLenum key,GLuint64* value)
    { Check(ready && key==GL_QUERY_RESULT,"no unavailable result fetch");++resultCalls;*value=100+query; }
    void APIENTRY Stamp(GLuint query,GLenum key)
    { ++stampCalls;issued.insert(query);realStamp(query,key); }
    void APIENTRY Delete(GLsizei count,const GLuint* names)
    { deletedQueries+=count;realDelete(count,names); }
    void APIENTRY FailedGenerate(GLsizei count,GLuint* names)
    { if(++generateCalls==2) std::fill_n(names,count,0u);else realGenerate(count,names); }
    void TimingTests(EngineContext& root)
    {
        realAvailable=glad_glGetQueryObjectiv;realResult=glad_glGetQueryObjectui64v;
        realStamp=glad_glQueryCounter;realDelete=glad_glDeleteQueries;
        realGenerate=glad_glGenQueries;
        glad_glGetQueryObjectiv=Available;glad_glGetQueryObjectui64v=Result;
        glad_glQueryCounter=Stamp;glad_glDeleteQueries=Delete;
        {
            PassTiming timing;
            Check(timing.Initialize(true),"timing initialize");
            for(unsigned frame=0;frame<9;++frame) {
                Check(timing.BeginFrame(),"timing frame");
                for(unsigned pass=0;pass<8;++pass) {
                    PassTiming::Scope measure(timing,static_cast<RenderPass>(pass));
                    PassTiming::Submitted(3,2);measure.Complete();
                }
                Check(timing.Current().size()==8,"fixed pass labels");
                for(unsigned pass=0;pass<8;++pass) {
                    const auto& s=timing.Current()[pass];
                    Check(s.pass==static_cast<RenderPass>(pass) && PassLabel(s.pass)!="unknown","semantic pass label");
                    Check(s.submittedItems==3 && s.submittedDraws==2 && s.completed,"exact counts and scope completion");
                    Check(s.gpu==(frame==8?GpuTiming::PoolExhausted:GpuTiming::Pending) && !s.gpuNanoseconds,"missing GPU is explicit");
                }
                timing.EndFrame();
                if(frame<2) Check(availabilityCalls==0 && resultCalls==0,"no same-frame or next-frame fetch");
            }
            Check(availabilityCalls>0 && resultCalls==0 && issued.size()==128,"unavailable queries retained without reuse or fetch");
            ready=true;
            Check(timing.BeginFrame(),"collect delayed samples");
            Check(timing.Collected().size()==64 && resultCalls==128,"all available delayed queries collected");
            for(const auto& s:timing.Collected())
                Check(s.gpu==GpuTiming::Available && s.gpuNanoseconds && s.collectedFrame-s.frame>=2 && s.submittedDraws==2,"delayed association preserves frame/pass/counts");
            { PassTiming::Scope measure(timing,RenderPass::Opaque); measure.Complete(); }
            Check(issued.size()==128,"collected query storage is reused");
            timing.EndFrame();
            bool rejected=false;
            std::thread worker([&] {auto begun=timing.BeginFrame();rejected=!begun && begun.error()==TimingError::Context;});
            worker.join();Check(rejected,"worker timing rejected before GL");
            PassTiming moved=std::move(timing);Check(!timing.Enabled() && moved.Enabled(),"move transfers query owner");
            Check(moved.BeginFrame(),"moved owner frame");
            {PassTiming::Scope incomplete(moved,RenderPass::Debug);}
            Check(!moved.Current()[0].completed,"failed scope reported incomplete");
            moved.EndFrame();moved.Reset();moved.Reset();
            Check(deletedQueries==128,"all pending queries deleted exactly once");
        }
        glad_glGetQueryObjectiv=realAvailable;glad_glGetQueryObjectui64v=realResult;
        {
            glad_glGenQueries=FailedGenerate;
            PassTiming failed;auto result=failed.Initialize(true);
            Check(!result && result.error()==TimingError::Driver && deletedQueries==130,"partial query allocation cleans up transactionally");
            glad_glGenQueries=realGenerate;
        }
        glad_glQueryCounter=nullptr;
        {
            PassTiming disabled;Check(disabled.Initialize(false) && disabled.BeginFrame(),"disabled timing requires no query API");
            {PassTiming::Scope measure(disabled,RenderPass::Opaque);measure.Complete();}
            disabled.EndFrame();Check(disabled.Current().empty(),"disabled timing emits no samples");
            PassTiming unsupported;Check(unsupported.Initialize(true),"unsupported is reportable");
            Check(unsupported.BeginFrame(),"unsupported CPU frame");
            {PassTiming::Scope measure(unsupported,RenderPass::Opaque);measure.Complete();}
            Check(unsupported.Current()[0].gpu==GpuTiming::Unsupported && !unsupported.Current()[0].gpuNanoseconds,"unsupported retains CPU sample");
            unsupported.EndFrame();
        }
        glad_glQueryCounter=realStamp;glad_glDeleteQueries=realDelete;
        {
            PassTiming actual;Check(actual.Initialize(true),"real driver timing");Check(actual.BeginFrame(),"real first frame");
            {PassTiming::Scope measure(actual,RenderPass::Opaque);glClear(GL_COLOR_BUFFER_BIT);measure.Complete();}
            actual.EndFrame();glFinish(); // Fixture only: production never waits.
            Check(actual.BeginFrame(),"real next frame");Check(actual.Collected().empty(),"real delay enforced even when ready");actual.EndFrame();
            Check(actual.BeginFrame(),"real delayed frame");Check(actual.Collected().size()==1 && actual.Collected()[0].gpuNanoseconds.has_value(),"real driver GPU evidence");actual.EndFrame();
            WindowProperties properties;properties.m_Width=properties.m_Height=64;properties.m_MinWidth=properties.m_MinHeight=64;
            properties.flag={WindowFlags::INVISIBLE};properties.m_IsVsync=false;
            Check(actual.BeginFrame(),"CPU context-switch frame");
            ScopedPtr<Window> secondary;
            {
                PassTiming::Scope cpu(actual,RenderPass::EditorUI,TimingCounts::Unavailable,false);
                secondary=Take(Window::Create(properties),"secondary timing context");
                // Simulate UI returning a context-activation error. Scope cleanup
                // must retain CPU evidence without issuing GL on another context.
            }
            Check(!actual.Current()[0].completed && actual.Current()[0].gpu==GpuTiming::NotMeasured,"CPU-only error scope survives context switch");
            actual.EndFrame();
            auto foreign=actual.BeginFrame();Check(!foreign && foreign.error()==TimingError::Context,"foreign context cannot collect queries");
            secondary.reset();Check(root.MainWindow()->BeginRender(),"restore original timing context");
        }
        Check(glGetError()==GL_NO_ERROR,"timing leaves driver error-free");
        std::println("[PASS] pass-timing delayed/unavailable/reuse/no-block/labels/move/context/retirement");
    }
    FrameCamera Camera(_Scene& scene)
    {
        auto entity=scene.CreateEntity("camera");
        entity.AddComponent<RenderCameraComponent>();
        auto id=Take(scene.RenderData().Identify(entity),"camera identity");
        return {id,glm::lookAt(glm::vec3(0,2,8),glm::vec3(0,0,0),glm::vec3(0,1,0)),
            glm::perspective(glm::radians(45.f),1.f,.1f,20.f),{0,2,8},0,0,64,64};
    }
    std::vector<std::byte> Pixels(RenderTarget& target)
    {
        std::vector<std::byte> pixels(64*64*4);
        Check(target.ReadColor(pixels),"color readback");return pixels;
    }
    auto Extract(_Scene& scene,SceneRenderResources& resources,const AssetPublication::FrameAccess& access,const FrameCamera& camera)
    {
        RenderExtractionStats stats;
        return ExtractRenderFrame(scene,resources.ForFrame(access),stats,{&camera,1});
    }
    void WindowPlacementTests(EngineContext& root,const WindowProperties& properties)
    {
        using namespace WindowPlacementProbe;
        Check(winProp.m_WinPos==WindowPos::Center,"actual application requests engine Center semantics");
        const bool predecessor=SDL_getenv("GENGINE_PREDECESSOR_PLACEMENT")!=nullptr;
        auto centered=[&] {return SDL_WINDOWPOS_ISCENTERED(last.x) && SDL_WINDOWPOS_ISCENTERED(last.y);};
        Check(creations==1 && centered()!=predecessor,"engine Center reaches the SDL creation boundary");
        Check(last.width==int(properties.m_Width) && last.height==int(properties.m_Height),"placement preserves requested logical extent");
        Check(last.flags==(SDL_WINDOW_OPENGL|SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_HIDDEN),"placement preserves GL/high-DPI/visibility flags");
        Check(repositions==0,"no post-creation reposition or initial-position jump");
        int width{},height{};auto* native=SDL_GL_GetCurrentWindow();
        SDL_GetWindowSize(native,&width,&height);
        Check(root.MainWindow()->GetScreenWidth()==unsigned(width) && root.MainWindow()->GetScreenHeight()==unsigned(height),"engine logical size matches native window");
        SDL_GL_GetDrawableSize(native,&width,&height);
        const auto pixels=root.MainWindow()->GetFramebufferPixelSize();
        Check(pixels.Width==unsigned(width) && pixels.Height==unsigned(height),"framebuffer dimensions retain native DPI mapping");
        auto full=properties;full.flag={WindowFlags::INVISIBLE,WindowFlags::FULLSCREEN};
        auto fullscreen=Take(Window::Create(full),"fullscreen placement preserves creation/context");
        Check(centered()!=predecessor && (last.flags&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN_DESKTOP
            && (last.flags&SDL_WINDOW_ALLOW_HIGHDPI) && (last.flags&SDL_WINDOW_OPENGL),"fullscreen and DPI/context flags survive Center mapping");
        Check((SDL_GetWindowFlags(SDL_GL_GetCurrentWindow())&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN_DESKTOP,"native fullscreen desktop state");
        Check(repositions==0,"fullscreen also needs no corrective reposition");
        Check(root.MainWindow()->BeginRender(),"restore main context before secondary retirement");fullscreen.reset();
        Check(root.MainWindow()->IsCurrent(),"placement probe preserves context/shutdown ordering");
        std::println("{}",predecessor?"[PASS] predecessor numeric-placement observation":"[PASS] window-placement application/semantic-create/size/DPI/GL/fullscreen/no-reposition");
    }

    void OrderingTests()
    {
        std::array<DrawItem,10> draws{};
        for(auto& d:draws) {d.pipeline={1,1,1};d.material={1,1,2};d.mesh={1,1,3};d.submesh={0,3,0};}
        draws[0].pipeline.index=2; // pipeline dominates lower material/mesh.
        draws[1].material.index=2;
        draws[2].mesh.index=2;
        draws[3].submesh.firstElement=3;
        draws[4].submesh.elementCount=6;
        draws[5].submesh.materialSlot=1;
        draws[6].sortKey=999;draws[6].resources=999; // advisory/table position ignored.
        draws[8].mesh.generation=2;
        draws[9].mesh.registry=4;
        const std::array<std::size_t,10> expected{6,7,5,4,3,9,8,2,1,0};
        std::array<std::size_t,10> indices{};
        for(unsigned repeat=0;repeat<10;++repeat) {
            std::iota(indices.begin(),indices.end(),0);
            std::rotate(indices.begin(),indices.begin()+repeat,indices.end());
            RenderDetail::SortLocality(indices,draws);
            Check(indices==expected,"pipeline/material/full mesh identity/submesh and stable ties");
        }
        FrameCamera camera;camera.view=glm::mat4(1);
        draws[0].worldTransform[3]={100,0,-2,1}; // radial distance must not win.
        draws[1].worldTransform[3]={0,0,-8,1};
        draws[2].worldTransform[3]={0,0,-8,1};
        std::array<std::size_t,3> alpha{2,0,1};
        RenderDetail::SortTransparent(alpha,draws,camera);
        Check(alpha==std::array<std::size_t,3>{1,2,0},"transparent camera depth dominates locality and radial distance; stable depth ties");
        camera.view[2][2]=-1;
        RenderDetail::SortTransparent(alpha,draws,camera);
        Check(alpha==std::array<std::size_t,3>{0,1,2},"camera reversal updates transparency order");
        camera.view[2][2]=(std::numeric_limits<float>::max)();
        draws[0].worldTransform[3].z=-(std::numeric_limits<float>::max)();
        draws[1].worldTransform[3].z=-2;
        RenderDetail::SortTransparent(alpha,draws,camera);
        Check(alpha[0]==0,"finite extreme products retain a strict depth order");
        RenderDetail::SortLocality({},draws);RenderDetail::SortTransparent({},draws,camera);
        std::array<std::size_t,1> one{4};RenderDetail::SortLocality(one,draws);
        Check(one[0]==4,"empty and singleton partitions");
        std::println("[PASS] draw-order deterministic/full-identities/submesh/stable-ties/camera-depth/extremes");
    }

    void SortingFixture(EngineContext& root,SceneRenderResources& resources,FrameSubmissionDesc desc,
        MeshHandle box,MeshHandle sphere,std::span<const MaterialParameterDecl> parameters,
        std::span<const MaterialTextureAssignment> textures)
    {
        // Two alpha partitions x two programs x two instances per program.
        // Instance variants share the pipeline but bind a different albedo.
        std::array<MaterialInstanceHandle,8> materials;
        for(unsigned group=0;group<4;++group) {
            auto base=Take(resources.PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,
                group<2?AlphaMode::Opaque:AlphaMode::Masked,.5f,1}),"sort material");
            materials[group*2]=base;
            std::optional<MaterialInstance> copy;
            {auto access=resources.Publication().BeginFrame();copy.emplace(*Take(resources.Materials().Acquire(access,base),"sort instance source"));}
            Check(copy->SetTexture("albedoMap",textures.back().value),"sort distinct texture");
            {auto access=resources.Publication().BeginPublication();materials[group*2+1]=Take(resources.Materials().Create(access,std::move(*copy)),"sort instance variant");}
        }
        desc.pipelines=resources.Pipelines();desc.pickingEnabled=false;
        auto submitter=Take(FrameSubmission::Create(),"sort submitter");EntityPickTable picks;
        auto renderGrid=[&](bool grouped,const char* output) {
            _Scene scene;auto camera=Camera(scene);
            camera.view=glm::lookAt(glm::vec3(0,0,10),glm::vec3(0),glm::vec3(0,1,0));
            camera.projection=glm::ortho(-4.f,4.f,-4.f,4.f,.1f,20.f);camera.worldPosition={0,0,10};
            std::array<unsigned,64> order;std::iota(order.begin(),order.end(),0);
            if(grouped) std::sort(order.begin(),order.end(),[](auto a,auto b) {
                return std::tuple(a%8,(a/8+a)%2,a)<std::tuple(b%8,(b/8+b)%2,b);
            });
            // Empty cached shadow targets keep all declared texture bindings
            // complete; synchronous Debug warnings must not pollute CPU timing.
            auto sun=scene.CreateEntityWithUUID(UUID(1),"sort sun");
            RenderLightComponent light;light.castShadows=true;sun.AddComponent<RenderLightComponent>(light);
            auto bulb=scene.CreateEntityWithUUID(UUID(2),"sort bulb");
            light.kind=RenderLightKind::Point;light.range=100;bulb.AddComponent<RenderLightComponent>(light);
            bulb.GetComponent<Transform3DComponent>().Translation={0,3,5};
            unsigned sourceOrdinal=100;
            for(auto i:order) {
                auto entity=scene.CreateEntityWithUUID(UUID(sourceOrdinal++),"sort "+std::to_string(i));
                entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{(i/8+i)%2?box:sphere,materials[i%8],0,false,false,false});
                auto& pose=entity.GetComponent<Transform3DComponent>();
                // 32 overlapping pairs at distinct depths exercise depth equivalence.
                const unsigned cell=i%32;
                pose.Translation={float(cell%8)*.95f-3.325f,float(cell/8)*1.8f-2.7f,i<32?.5f:-.5f};
                pose.Scale=glm::vec3(i<32?.25f:.4f);
            }
            auto access=resources.Publication().BeginFrame();
            auto frame=Take(Extract(scene,resources,access,camera),"sort frozen extraction");
            const std::vector<DrawItem> before(frame.Draws().begin(),frame.Draws().end());
            std::ofstream csv;
            if(output) {csv.open(output);csv<<"iteration,cpu_ns,UseProgram,BindVertexArray,BindTexture,BindSampler,DrawArrays,DrawElements,UniformCalls,UniformBytes,BufferCalls,BufferBytes\n";}
            const unsigned iterations=output?360:1;
            for(unsigned i=0;i<iterations;++i) {
                std::chrono::nanoseconds elapsed;
                {
                    DriverCalls::Observe observe; UploadCalls::Observe uploads;
                    const auto start=std::chrono::steady_clock::now();
                    auto result=Take(submitter.Submit(frame,desc,picks),"sort submission");
                    elapsed=std::chrono::steady_clock::now()-start;
                    Check(result.opaqueDraws==32 && result.maskedDraws==32 && result.colorDraws==64
                        && result.shadowDraws==0 && result.pickDraws==0,"sort identical 64-draw work");
                }
                if(output && i>=120) csv<<i<<','<<elapsed.count()<<','<<DriverCalls::counts[0]<<','<<DriverCalls::counts[1]<<','
                    <<DriverCalls::counts[4]<<','<<DriverCalls::counts[5]<<','<<DriverCalls::counts[26]<<','<<DriverCalls::counts[27]<<','
                    <<UploadCalls::uniforms<<','<<UploadCalls::uniformBytes<<','<<UploadCalls::buffers<<','<<UploadCalls::bufferBytes<<'\n';
                if(output) root.MainWindow()->SwapBuffer();
            }
            for(std::size_t i=0;i<before.size();++i) {
                const auto& d=frame.Draws()[i];const auto& b=before[i];
                Check(d.entity==b.entity && d.mesh==b.mesh && d.material==b.material && d.pipeline==b.pipeline
                    && d.resources==b.resources && d.worldTransform==b.worldTransform,"published frame stays immutable");
            }
            const auto pixels=Pixels(desc.color);
            if(output) {
                std::ofstream image(std::string(output)+".rgba",std::ios::binary);
                image.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
                Check(csv.good() && image.good(),"sort measurement output");
                std::println("[PASS] draw-sort-measure 120 warmup / 240 samples / 64 draws; 64x64 linear RGBA8; frozen camera; no Physics");
                std::println("sort GPU={} driver={}",reinterpret_cast<const char*>(glGetString(GL_RENDERER)),reinterpret_cast<const char*>(glGetString(GL_VERSION)));
            }
            return pixels;
        };
        const auto original=renderGrid(false,SDL_getenv("GENGINE_DRAW_SORT_MEASURE"));
        Check(original==renderGrid(true,nullptr),"opaque and masked pixels equivalent across source permutations");
        Check(std::any_of(original.begin(),original.end(),[](auto b){return b!=std::byte{26} && b!=std::byte{255};}),"sort fixture has visible fragments");

        // Deliberately publish near before far, opposing locality order.
        const auto nearMaterial=Take(resources.PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,AlphaMode::Transparent,.5f,.5f}),"near alpha");
        auto other=std::vector<MaterialTextureAssignment>(textures.begin(),textures.end());other[0].value=textures.back().value;
        const auto farMaterial=Take(resources.PublishMaterial({SceneMaterialKind::Lit,parameters,other,false,1,AlphaMode::Transparent,.5f,.5f}),"far alpha");
        desc.pipelines=resources.Pipelines();
        auto overlap=[&](unsigned mask,bool reverse,float nearZ) {
            _Scene scene;auto camera=Camera(scene);
            camera.view=glm::lookAt(glm::vec3(0,0,8),glm::vec3(0),glm::vec3(0,1,0));
            camera.projection=glm::ortho(-2.f,2.f,-2.f,2.f,.1f,20.f);camera.worldPosition={0,0,8};
            for(unsigned j=0;j<2;++j) {
                const unsigned i=reverse?1-j:j;if(!(mask&(1u<<i))) continue;
                auto entity=scene.CreateEntityWithUUID(UUID(100+j),"alpha "+std::to_string(i));
                entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{box,i?farMaterial:nearMaterial,0,false,false,false});
                entity.GetComponent<Transform3DComponent>().Translation.z=i?-.75f:nearZ;
            }
            auto access=resources.Publication().BeginFrame();auto frame=Take(Extract(scene,resources,access,camera),"overlap extraction");
            Take(submitter.Submit(frame,desc,picks),"overlap submit");return Pixels(desc.color);
        };
        const auto background=overlap(0,false,.75f),nearOnly=overlap(1,false,.75f),farOnly=overlap(2,false,.75f);
        const auto both=overlap(3,false,.75f);
        Check(both==overlap(3,true,.75f),"transparent overlap invariant under source reversal at distinct depths");
        unsigned overlapPixels{};
        for(unsigned i=0;i<both.size();i+=4) {
            bool nearHit=false,farHit=false;
            for(unsigned c=0;c<3;++c) {nearHit|=nearOnly[i+c]!=background[i+c];farHit|=farOnly[i+c]!=background[i+c];}
            if(!nearHit || !farHit) continue;
            ++overlapPixels;
            for(unsigned c=0;c<3;++c) {
                const float expected=float(nearOnly[i+c])+.5f*(float(farOnly[i+c])-float(background[i+c]));
                Check(std::abs(float(both[i+c])-expected)<=3,"transparent far then near source-over reference");
            }
        }
        Check(overlapPixels>100,"transparent reference has substantial overlap");
        Check(overlap(3,false,-.75f)!=overlap(3,true,-.75f),"equal-depth transparent ties retain source order");
        const MaterialParameterDecl red[]{{"u_baseColor",MaterialParameterType::Float4,std::array<float,4>{1,0,0,1}},
            {"u_useVertexColor",MaterialParameterType::Boolean,false}};
        const MaterialParameterDecl green[]{{"u_baseColor",MaterialParameterType::Float4,std::array<float,4>{0,1,0,1}},
            {"u_useVertexColor",MaterialParameterType::Boolean,false}};
        const auto redHelper=Take(resources.PublishMaterial({SceneMaterialKind::Helper,red,{},true}),"red debug material");
        const auto greenHelper=Take(resources.PublishMaterial({SceneMaterialKind::Helper,green,{},true}),"green debug material");
        desc.pipelines=resources.Pipelines();
        auto debugImage=[&](bool reverse) {
            _Scene scene;const auto camera=Camera(scene);
            for(unsigned j=0;j<2;++j) {
                const auto i=reverse?1-j:j;auto entity=scene.CreateEntityWithUUID(UUID(100+j),"debug "+std::to_string(i));
                entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{box,i?greenHelper:redHelper,0,false,false,false});
            }
            auto access=resources.Publication().BeginFrame();auto frame=Take(Extract(scene,resources,access,camera),"debug extraction");
            auto result=Take(submitter.Submit(frame,desc,picks),"debug ordering submit");
            Check(result.helperDraws==2 && result.colorDraws==0,"debug stays in its own pass");return Pixels(desc.color);
        };
        Check(debugImage(false)!=debugImage(true),"debug equal-depth source ordering is not regrouped by material");
        std::println("[PASS] draw-sort debug-source-order");
        std::println("[PASS] draw-sort opaque/masked-equivalence/immutable/transparent-overlap/stable-ties");
    }


    namespace UploadFault
    {
        decltype(glad_glNamedBufferData) write{};
        decltype(glad_glGetError) error{};
        bool pending{}, denied{};
        void APIENTRY Write(GLuint buffer,GLsizeiptr bytes,const void* data,GLenum usage)
        { if(pending) {pending=false;denied=true;} else write(buffer,bytes,data,usage); }
        GLenum APIENTRY Error() { if(denied) {denied=false;return GL_OUT_OF_MEMORY;} return error(); }
        struct Scope {
            Scope() {write=glad_glNamedBufferData;error=glad_glGetError;pending=true;glad_glNamedBufferData=Write;glad_glGetError=Error;}
            ~Scope() {glad_glNamedBufferData=write;glad_glGetError=error;}
        };
    }
    void UploadFixture(SceneRenderResources& resources,FrameSubmissionDesc desc,MeshHandle box,MeshHandle sphere)
    {
        using namespace RenderBackend;
        // Every parameter representation crosses the actual std430 shader ABI.
        const MaterialParameterDecl parameters[]{
            {"pBool",MaterialParameterType::Boolean,true},
            {"pInt",MaterialParameterType::Integer,std::int32_t(-7)},
            {"pUint",MaterialParameterType::UnsignedInteger,std::uint32_t(4000000000u)},
            {"pFloat",MaterialParameterType::Float,.25f},
            {"pVec2",MaterialParameterType::Float2,std::array<float,2>{.5f,.75f}},
            {"pVec3",MaterialParameterType::Float3,std::array<float,3>{1,2,3}},
            {"pVec4",MaterialParameterType::Float4,std::array<float,4>{4,5,6,7}},
            {"pMatrix",MaterialParameterType::Matrix4,std::array<float,16>{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}},
            {"u_baseColor",MaterialParameterType::Float4,std::array<float,4>{1,0,0,1}},
            {"u_useVertexColor",MaterialParameterType::Boolean,false}};
        std::array<MaterialInstanceHandle,16> materials;
        materials[0]=Take(resources.PublishMaterial({SceneMaterialKind::Helper,parameters,{},true}),"upload source material");
        for(unsigned i=1;i<materials.size();++i) {
            std::optional<MaterialInstance> copy;
            {auto access=resources.Publication().BeginFrame();copy.emplace(*Take(resources.Materials().Acquire(access,materials[0]),"upload clone"));}
            Check(copy->SetParameter("u_baseColor",std::array<float,4>{float(i)/16,1,0,1}),"upload distinct material");
            {auto publish=resources.Publication().BeginPublication();materials[i]=Take(resources.Materials().Create(publish,std::move(*copy)),"upload material publication");}
        }
        desc.pipelines=resources.Pipelines();desc.pickingEnabled=false;
        _Scene scene;auto camera=Camera(scene);
        std::vector<_Entity> bodies;
        for(unsigned i=0;i<32;++i) {
            auto body=scene.CreateEntity("upload body");
            body.AddComponent<VisibilityComponent>();
            body.AddComponent<MeshRendererComponent>(MeshRendererComponent{i<16?box:sphere,materials[i%16],0,false,false,false});
            bodies.push_back(body);
        }
        auto extract=[&]() { auto access=resources.Publication().BeginFrame();return Take(Extract(scene,resources,access,camera),"upload frame extraction"); };
        auto frame=extract();
        auto batch=Take(MaterialBatch::Pack(frame,desc.pipelines,1u<<20),"material batch packing");
        Check(frame.Resources().size()==32 && batch.wordCount==16*(8+52),"distinct instances deduplicated across mesh-resource pairs");
        const auto exactBytes=batch.Bytes().size();
        Check(MaterialBatch::Pack(frame,desc.pipelines,exactBytes),"exact byte boundary accepted");
        auto shortBatch=MaterialBatch::Pack(frame,desc.pipelines,exactBytes-1);
        Check(!shortBatch && std::get<SubmissionCode>(shortBatch.error().cause)==SubmissionCode::InvalidDraw,"one-byte-short rejected before GL");
        for(std::size_t i=0;i<frame.Resources().size();++i) {
            const auto& material=frame.Resources()[i].Material();const auto at=std::size_t(batch.offsets[i])*4;
            Check(at%4==0 && at+8+material.PackedWords().size()<=batch.wordCount,"aligned complete material span");
            Check(std::equal(material.PackedWords().begin(),material.PackedWords().end(),batch.words.get()+at+8),"all packed words preserve representation");
            Check(batch.words[at+6]==0 && batch.words[at+7]==0,"coverage padding initialized");
        }
        // A real compute shader interprets every scalar/vector/matrix field, not
        // a second CPU implementation of the packing rules.
        const std::string compute=R"(#version 450 core
layout(local_size_x=1) in;
layout(std430,binding=1) buffer Result { uint passed; };
uniform bool pBool; uniform int pInt; uniform uint pUint; uniform float pFloat;
uniform vec2 pVec2; uniform vec3 pVec3; uniform vec4 pVec4; uniform mat4 pMatrix;
void main() { passed=(pBool && pInt==-7 && pUint==4000000000u && pFloat==.25
 && pVec2==vec2(.5,.75) && pVec3==vec3(1,2,3) && pVec4==vec4(4,5,6,7)
 && pMatrix[0]==vec4(1,2,3,4) && pMatrix[3]==vec4(13,14,15,16))?1u:0u; }
)";
        auto code=Take(PackedStage(compute,false,parameters),"packed compute adapter");
        const ShaderSource source{ShaderStage::Compute,code,"packed ABI proof"};
        auto shader=Take(Shader::Create({{&source,1}}),"packed compute program");
        GLuint resultBuffer{};glCreateBuffers(1,&resultBuffer);const GLuint zero=0;
        glNamedBufferData(resultBuffer,sizeof(zero),&zero,GL_DYNAMIC_READ);
        {
            UploadBindings restore;UploadBuffer payload;
            Check(payload.Update(batch.Bytes(),GL_DYNAMIC_DRAW),"compute material upload");
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,payload.Name());glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,resultBuffer);
            shader.Bind();glUniform1ui(glGetUniformLocation(ShaderBackendAccess::Program(shader),"geMaterialOffset"),batch.offsets[0]);
            glDispatchCompute(1,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            GLuint passed{};glGetNamedBufferSubData(resultBuffer,0,sizeof(passed),&passed);Check(passed==1,"all packed parameter types interpreted correctly on GPU");
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,0);glUseProgram(0);
        }
        glDeleteBuffers(1,&resultBuffer);
        EntityPickTable picks;GLuint frameName{},materialName{};
        {
            auto submitter=Take(FrameSubmission::Create(),"upload submitter");
            {
                UploadCalls::Observe uploads;Take(submitter.Submit(frame,desc,picks),"many-material upload");
                Check(UploadCalls::buffers==3 && UploadCalls::bufferBytes==sizeof(PackedFrame)+exactBytes+sizeof(PackedInstance),"frame/material uploads plus serial instance sentinel");
                frameName=UploadCalls::lastFrameBuffer;materialName=UploadCalls::lastMaterialBuffer;
            }
            GLint usage{},bytes{};glGetNamedBufferParameteriv(frameName,GL_BUFFER_USAGE,&usage);
            Check(usage==GL_STREAM_DRAW,"frame storage uses streaming draw policy");
            glGetNamedBufferParameteriv(materialName,GL_BUFFER_USAGE,&usage);glGetNamedBufferParameteriv(materialName,GL_BUFFER_SIZE,&bytes);
            Check(usage==GL_DYNAMIC_DRAW && bytes==exactBytes,"material storage usage and exact bytes");
            const auto program=ShaderBackendAccess::Program(*frame.Resources()[0].Material().Program());
            const char* names[]{"geView","geProjection","geSkyView","geCascades[0]","gePointMatrices[0]","geViewPosition","geLights","geSplits[0]"};
            const GLint expected[]{0,64,128,192,1216,1600,1696,1712};
            GLuint indices[8];GLint offsets[8],strides[8];
            glGetUniformIndices(program,8,names,indices);glGetActiveUniformsiv(program,8,indices,GL_UNIFORM_OFFSET,offsets);
            glGetActiveUniformsiv(program,8,indices,GL_UNIFORM_ARRAY_STRIDE,strides);
            for(unsigned i=0;i<8;++i) Check(offsets[i]==expected[i],"driver uniform offset matches ABI");
            Check(strides[3]==64 && strides[4]==64 && strides[7]==16,"driver matrix and scalar-array strides");
            {
                UploadCalls::Observe uploads;Take(submitter.Submit(frame,desc,picks),"unchanged upload frame");
                Check(UploadCalls::buffers==0 && UploadCalls::bufferBytes==0,"unchanged frame/materials issue zero buffer uploads");
            }
            // Preserve incoming nonzero ranges and generic bindings, including failures.
            GLuint foreign{};glCreateBuffers(1,&foreign);GLint alignment{};
            glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,&alignment);
            GLint ssboAlignment{};glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&ssboAlignment);
            const auto offset=std::lcm(alignment,ssboAlignment);glNamedBufferData(foreign,offset+4096,nullptr,GL_DYNAMIC_DRAW);
            glBindBufferRange(GL_UNIFORM_BUFFER,0,foreign,offset,512);
            glBindBufferRange(GL_UNIFORM_BUFFER,1,foreign,offset,2048);glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,foreign,offset,1024);
            const auto restored=[&] {
                GLint bound{};GLint64 start{},size{};
                glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,0,&bound);glGetInteger64i_v(GL_UNIFORM_BUFFER_START,0,&start);glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE,0,&size);
                Check(bound==foreign && start==offset && size==512,"legacy UBO slot remains untouched by packed submission");
                glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,1,&bound);glGetInteger64i_v(GL_UNIFORM_BUFFER_START,1,&start);glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE,1,&size);
                Check(bound==foreign && start==offset && size==2048,"incoming UBO range restored");
                glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&bound);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,0,&start);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,0,&size);
                Check(bound==foreign && start==offset && size==1024,"incoming SSBO range restored");
                glGetIntegerv(GL_UNIFORM_BUFFER_BINDING,&bound);Check(bound==foreign,"generic UBO restored");
                glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&bound);Check(bound==foreign,"generic SSBO restored");
            };
            Take(submitter.Submit(frame,desc,picks),"range restoration submission");restored();
            camera.worldPosition.x+=1;auto changedCamera=extract();
            {UploadFault::Scope fault;auto failed=submitter.Submit(changedCamera,desc,picks);
                Check(!failed && std::get<SubmissionCode>(failed.error().cause)==SubmissionCode::Driver,"upload failure is typed");}
            restored();
            {UploadCalls::Observe uploads;Take(submitter.Submit(changedCamera,desc,picks),"failed upload retry");
                Check(UploadCalls::buffers==1 && UploadCalls::bufferBytes==sizeof(PackedFrame),"retry replaces failed frame storage; material batch stays clean");}
            restored();glBindBufferBase(GL_UNIFORM_BUFFER,0,0);glBindBufferBase(GL_UNIFORM_BUFFER,1,0);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);glDeleteBuffers(1,&foreign);
            // Reduce to one visible object, then queue differently colored frames
            // without readback, waits, swaps or glFinish between submissions.
            for(std::size_t i=1;i<bodies.size();++i) bodies[i].GetComponent<VisibilityComponent>().enabled=false;
            camera.worldPosition.x-=1;
            std::array<std::optional<RenderTarget>,6> targets;
            std::optional<RenderFrame> retained;
            auto replace=[&](MaterialInstanceHandle handle,std::array<float,4> color) {
                std::optional<MaterialInstance> copy;
                {auto access=resources.Publication().BeginFrame();copy.emplace(*Take(resources.Materials().Acquire(access,handle),"dirty material source"));}
                Check(copy->SetParameter("u_baseColor",color),"dirty material edit");
                {auto publish=resources.Publication().BeginPublication();Check(resources.Materials().Replace(publish,handle,std::move(*copy)),"dirty material publish");}
            };
            for(unsigned i=0;i<targets.size();++i) {
                replace(materials[0],i%2?std::array<float,4>{0,1,0,1}:std::array<float,4>{1,0,0,1});
                auto edited=extract();if(i==0) retained.emplace(extract());
                targets[i].emplace(Take(RenderTarget::Create(desc.color.Description()),"queued upload target"));
                auto queued=FrameSubmissionDesc{*targets[i],desc.picking,desc.pointShadow,desc.cascadeShadow,desc.pipelines,desc.cascadeSplits,
                    desc.cameraFov,desc.cameraAspect,desc.cameraNear,desc.cameraFar,desc.pointNear,desc.pointFar,false};
                UploadCalls::Observe uploads;Take(submitter.Submit(edited,queued,picks),"queued dirty material frame");
                Check(UploadCalls::buffers==(i==0?2:1),"frequent material edits remain one whole batch");
            }
            for(unsigned i=0;i<targets.size();++i) {
                const auto image=Pixels(*targets[i]);const auto pixel=(32*64+32)*4;
                Check(image[pixel+(i%2)]==std::byte{255} && image[pixel+(1-i%2)]==std::byte{0},"queued frame retains its own material bytes");
            }
            Take(submitter.Submit(*retained,desc,picks),"retained old material frame resubmission");
            const auto old=Pixels(desc.color);Check(old[(32*64+32)*4]==std::byte{255} && old[(32*64+32)*4+1]==std::byte{0},"retained publication version restores old packed material");
            // Dense edits across many instances still form one transfer.
            for(auto& body:bodies) body.GetComponent<VisibilityComponent>().enabled=true;
            for(unsigned i=0;i<materials.size();++i) replace(materials[i],{0,0,float(i+1)/16,1});
            auto dense=extract();
            {UploadCalls::Observe uploads;Take(submitter.Submit(dense,desc,picks),"dense material edits");
                Check(UploadCalls::buffers==1 && UploadCalls::bufferBytes==exactBytes,"many dirty materials upload as one bounded batch");}
            {UploadCalls::Observe uploads;Take(submitter.Submit(dense,desc,picks),"dense unchanged material frame");Check(UploadCalls::buffers==0,"dense unchanged batch stays clean");}
            replace(materials[0],{.25f,.5f,.75f,1});auto dirty=extract();
            {UploadFault::Scope fault;auto failed=submitter.Submit(dirty,desc,picks);
                Check(!failed && std::get<SubmissionCode>(failed.error().cause)==SubmissionCode::Driver,"material upload failure stays typed");}
            {UploadCalls::Observe uploads;Take(submitter.Submit(dirty,desc,picks),"material upload retry");
                Check(UploadCalls::buffers==1 && UploadCalls::bufferBytes==exactBytes,"failed material storage retried as one complete batch");}
            auto sun=scene.CreateEntity("upload sun");RenderLightComponent light;light.castShadows=false;
            sun.AddComponent<RenderLightComponent>(light);auto oneLight=extract();
            {UploadCalls::Observe uploads;Take(submitter.Submit(oneLight,desc,picks),"one packed light");
                Check(UploadCalls::buffers==1 && UploadCalls::bufferBytes==sizeof(PackedFrame),"one light changes only frame block");}
            auto bulb=scene.CreateEntity("upload bulb");light.kind=RenderLightKind::Point;light.range=20;
            bulb.AddComponent<RenderLightComponent>(light);auto twoLights=extract();
            {UploadCalls::Observe uploads;Take(submitter.Submit(twoLights,desc,picks),"two packed lights");
                Check(UploadCalls::buffers==1 && UploadCalls::bufferBytes==sizeof(PackedFrame),"multiple supported lights share one upload");}
            auto extra=scene.CreateEntity("upload extra directional");light.kind=RenderLightKind::Directional;
            extra.AddComponent<RenderLightComponent>(light);auto excess=extract();
            {UploadCalls::Observe uploads;auto failed=submitter.Submit(excess,desc,picks);
                Check(!failed && std::get<SubmissionCode>(failed.error().cause)==SubmissionCode::UnsupportedLights && UploadCalls::buffers==0,
                    "excess lights reject before upload; lighting capacity unchanged");}
        }
        Check(!glIsBuffer(frameName) && !glIsBuffer(materialName),"upload buffers retire with submitter on context");
        Check(glGetError()==GL_NO_ERROR,"upload fixture leaves no driver error");
        std::println("[PASS] gpu-uploads one/many/unchanged/edits/layout/byte-boundary/queued-frames/retained/failure-retry/ranges/retirement");
    }

    void PickingParity(const RenderFrame& frame,_Scene& scene,const FrameCamera& camera,
        const MousePickFrameBuffer& actual,std::span<const std::pair<MeshHandle,Geometry*>> geometry,Shader& shader)
    {
        std::vector<int> expectedPixels;
        for (int y=0;y<64;++y) for(int x=0;x<64;++x)
            expectedPixels.push_back(Take(actual.ReadPixel(x,y),"submitted pick pixel"));
        auto reference=Take(MousePickFrameBuffer::Create(64,64),"legacy picking reference target");
        reference.Bind();glViewport(0,0,64,64);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
        Check(reference.ClearAttachment(0,-1),"clear legacy picking reference");
        shader.Bind();shader.SetUniform("u_view",camera.view);shader.SetUniform("u_projection",camera.projection);
        EntityPickTable table;
        for (const auto& draw:frame.Draws()) if(draw.pickable)
        {
            auto source=std::find_if(geometry.begin(),geometry.end(),[&](const auto& entry){return entry.first==draw.mesh;});
            Check(source!=geometry.end(),"legacy CPU/GPU fixture mapping");
            const auto entity=Take(scene.RenderData().Resolve(draw.entity),"legacy transform entity");
            const auto pose=scene.GetRenderTransform(_Entity{entity,&scene});
            Check(pose.matrix==draw.worldTransform,"legacy presentation transform parity");
            shader.SetUniform("u_model",pose.matrix);
            shader.SetUniform("u_EntityID",Take(table.Encode(draw.entity),"reference generation pixel"));
            auto* mesh=source->second;mesh->BindVAO();
            if(mesh->IsUsingIndexBuffer()) glDrawElements(GL_TRIANGLES,mesh->GetIndicesCount(),GL_UNSIGNED_INT,nullptr);
            else glDrawArrays(GL_TRIANGLES,0,mesh->GetVerticesCount());
        }
        std::size_t i=0;
        for (int y=0;y<64;++y) for(int x=0;x<64;++x)
        {
            const auto pixel=Take(reference.ReadPixel(x,y),"reference pick read");
            if (pixel!=expectedPixels[i]) std::println(stderr,"picking difference ({},{}) reference={} submitted={}",x,y,pixel,expectedPixels[i]);
            Check(pixel==expectedPixels[i++],"pixel-exact legacy picking rasterization");
        }
        reference.UnBind();
    }
    namespace PickReads
    {
        decltype(glad_glGetTextureSubImage) original{};
        unsigned calls{};
        void APIENTRY Read(GLuint texture,GLint level,GLint x,GLint y,GLint z,
            GLsizei width,GLsizei height,GLsizei depth,GLenum format,GLenum type,GLsizei bytes,void* data)
        { ++calls;original(texture,level,x,y,z,width,height,depth,format,type,bytes,data); }
        struct Observe
        {
            Observe() { original=glad_glGetTextureSubImage;glad_glGetTextureSubImage=Read;calls=0; }
            ~Observe() { glad_glGetTextureSubImage=original; }
        };
    }
    void PickingArchitecture(SceneRenderResources& resources,FrameSubmissionDesc base,
        MeshHandle mesh,MaterialInstanceHandle material)
    {
        _Scene scene;auto camera=Camera(scene);
        camera.view=glm::lookAt(glm::vec3(0,0,8),glm::vec3(0),glm::vec3(0,1,0));camera.worldPosition={0,0,8};
        camera.projection=glm::ortho(-4.f,4.f,-2.f,2.f,.1f,20.f);
        camera.viewportWidth=192;camera.viewportHeight=96;
        RenderTargetDesc td;td.Storage.Width=192;td.Storage.Height=96;
        td.Storage.Colors[0]=FramebufferFormat::RGBA8;td.Storage.ColorCount=1;td.Storage.Depth=FramebufferFormat::Depth24;
        auto color=Take(RenderTarget::Create(td),"non-square color target");
        auto picking=Take(MousePickFrameBuffer::Create(192,96),"non-square picking target");
        auto submitter=Take(FrameSubmission::Create(),"picking architecture submitter");
        FrameSubmissionDesc desc{color,picking,base.pointShadow,base.cascadeShadow,resources.Pipelines(),base.cascadeSplits,
            base.cameraFov,2.f,base.cameraNear,base.cameraFar,base.pointNear,base.pointFar};
        std::array<_Entity,4> bodies;
        std::array<EntityRenderId,4> ids;
        for(unsigned i=0;i<4;++i) {
            bodies[i]=scene.CreateEntity("pick quadrant "+std::to_string(i));
            bodies[i].AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,material});
            bodies[i].Transform().Translation={i%2?2.f:-2.f,i/2?1.f:-1.f,0};
            bodies[i].Transform().Scale={.4f,.4f,.4f};
            ids[i]=Take(scene.RenderData().Identify(bodies[i]),"quadrant identity");
        }
        EntityPickTable table;
        PassTiming timing;Check(timing.Initialize(true),"picking CPU timing owner");
        PickReads::Observe reads;
        auto position=[&](glm::vec3 world,EditorViewportLogicalSize logical=EditorViewportLogicalSize{96,48}) {
            const auto clip=camera.projection*camera.view*glm::vec4(world,1);
            const auto ndc=glm::vec3(clip)/clip.w;
            // Model an offset screen panel; scale using actual allocated extent.
            const float x=123+(ndc.x*.5f+.5f)*logical.Width;
            const float y=57+(1-(ndc.y*.5f+.5f))*logical.Height;
            const auto& actual=picking.Buffer().Description();
            return ViewportPixelAt(x-123,y-57,logical,{actual.Width,actual.Height});
        };
        struct Result { FrameSubmissionStats stats;std::int32_t pixel=-1;std::uint64_t ns{}; };
        auto run=[&](bool request,glm::vec3 world=glm::vec3(-2,-1,0)) {
            desc.pickingEnabled=request;
            auto access=resources.Publication().BeginFrame();auto frame=Take(Extract(scene,resources,access,camera),"picking frame");
            Check(timing.BeginFrame(),"picking timing begin");
            PassTiming::Activation active(timing);
            const auto before=PickReads::calls;
            Result result{Take(submitter.Submit(frame,desc,table),"picking submission")};
            if(request) {
                auto pixel=position(world);Check(pixel.has_value(),"in-panel projected position");
                result.pixel=Take(picking.ReadPixel(pixel->X,pixel->Y),"timed synchronous pixel");
            }
            unsigned samples=0;
            for(const auto& sample:timing.Current()) if(sample.pass==RenderPass::PickingReadback) {
                ++samples;result.ns=sample.cpuNanoseconds;
                Check(sample.completed && sample.gpu==GpuTiming::NotMeasured && !sample.gpuNanoseconds
                    && sample.submittedItems==1 && sample.submittedDraws==0,"readback is one CPU transfer, no GPU query/draw");
            }
            Check(samples==unsigned(request) && PickReads::calls-before==unsigned(request),"exact requested driver reads and timing samples");
            timing.EndFrame();return result;
        };
        auto idle=run(false);
        Check(!idle.stats.decisions[2].executed && idle.stats.pickDraws==0 && !table.Decode(0),"initial idle does not render or expose a lookup");
        for(unsigned i=0;i<4;++i) {
            auto picked=run(true,bodies[i].Transform().Translation);
            Check(Take(scene.RenderData().ResolvePick(table,picked.pixel),"quadrant generation")==ids[i],"non-square high-DPI instanced entity selection");
            Check(picked.stats.decisions[2].executed==(i==0),"unchanged click reuses paired image/table");
            if(i==0) Check(picked.stats.pickDraws==4 && picked.stats.instancedDrawCalls>=2,"picking and color really use instanced submission");
        }
        Check(run(true,{0,0,0}).pixel==EntityPickTable::InvalidPixel,"background sentinel");
        auto oldTable=table;const auto oldPixel=run(true).pixel;
        scene.DestroyEntity(bodies[0]);bodies[0]=scene.CreateEntity("reused slot");
        bodies[0].AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,material});
        bodies[0].Transform().Translation={-2,-1,0};bodies[0].Transform().Scale={.4f,.4f,.4f};
        const auto reused=Take(scene.RenderData().Identify(bodies[0]),"replacement generation");
        Check(reused.index==ids[0].index && reused.generation!=ids[0].generation,"actual slot reuse with new generation");
        Check(!scene.RenderData().ResolvePick(oldTable,oldPixel),"old image mapping rejects destroyed/reused entity");
        auto deferred=run(false);
        Check(!deferred.stats.decisions[2].executed && HasDirtyReason(deferred.stats.decisions[2].reasons,PassDirtyReason::SceneMembership),"scene change stays stale until requested");
        auto changed=run(true);
        Check(changed.stats.decisions[2].executed && Take(scene.RenderData().ResolvePick(table,changed.pixel),"replacement selection")==reused,"click after scene change uses new mapping");
        EntityPickTable limited(1);const EntityRenderId wide{7,UINT64_MAX,UINT64_MAX};
        Check(limited.Encode(wide)==0 && limited.Decode(0)==wide && limited.Encode(wide)==0,"full 160-bit identity survives signed pixel indirection");
        auto overflow=limited.Encode(reused);
        Check(!overflow && overflow.error()==RenderEcsError::PickCapacity && !limited.Decode(-1) && !limited.Decode(INT32_MAX),"capacity/invalid pixels reject without wrap");
        const auto top=ViewportPixelAt(0,0,{96,48},{192,96});
        const auto bottom=ViewportPixelAt(95.99f,47.99f,{96,48},{192,96});
        Check(top && top->X==0 && top->Y==95 && bottom && bottom->X==191 && bottom->Y==0,"high-DPI corners invert Y exactly once");
        const float nan=std::numeric_limits<float>::quiet_NaN();
        Check(!ViewportPixelAt(-1,0,{96,48},{192,96}) && !ViewportPixelAt(96,0,{96,48},{192,96})
            && !ViewportPixelAt(0,48,{96,48},{192,96}) && !ViewportPixelAt(0,-1,{96,48},{192,96})
            && !ViewportPixelAt(nan,0,{96,48},{192,96}) && !ViewportPixelAt(0,0,{0,48},{192,96})
            && !ViewportPixelAt(0,0,{96,48},{0,0}),"out-of-bounds/nonfinite/hidden inputs reject");
        Check(timing.BeginFrame(),"invalid read timing begin");
        { PassTiming::Activation active(timing);const auto before=PickReads::calls;
          auto invalid=picking.ReadPixel(192,0);
          Check(!invalid && invalid.error().code==FramebufferErrorCode::InvalidCoordinates
              && PickReads::calls==before && timing.Current().empty(),"invalid read issues no driver call or timing sample"); }
        timing.EndFrame();
        Check(!picking.OnResize(9000,96),"failed resize is transactional");
        Check(position({-2,-1,0},{48,48})->X==48,"logical change uses retained actual target width");
        Check(color.OnResize(288,144) && picking.OnResize(288,144),"resize non-square targets");
        camera.viewportWidth=288;camera.viewportHeight=144;
        run(false);auto resized=run(true);
        Check(HasDirtyReason(resized.stats.decisions[2].reasons,PassDirtyReason::TargetStorage)
            && Take(scene.RenderData().ResolvePick(table,resized.pixel),"resized pick")==reused,"click after viewport resize redraws and resolves");
        const auto ticks=scene.GetPhysicsTiming().totalSteps;
        camera.view[3][0]=glm::mix(0.f,.5f,.5f); // Presentation-only camera movement.
        run(false);auto movedCamera=run(true);
        Check(HasDirtyReason(movedCamera.stats.decisions[2].reasons,PassDirtyReason::Camera)
            && Take(scene.RenderData().ResolvePick(table,movedCamera.pixel),"camera pick")==reused
            && scene.GetPhysicsTiming().totalSteps==ticks,"tickless camera interpolation invalidates selection");
        bodies[0].AddComponent<RigidBody3DComponent>().Type=BodyType::Kinematic;
        bodies[0].AddComponent<SphereFixture3DComponent>().Radius=.1f;
        scene.OnRuntimeStart();auto* body=bodies[0].GetComponent<RigidBody3DComponent>().RuntimeBody;
        std::unique_ptr<PhysicalShape> shape(body->m_Shape);body->m_LinearVelocity={6,0,0};
        scene.Update(Timestep(_Scene::PhysicsStepSeconds));run(true);
        const auto pose=bodies[0].Transform().GetTransform();const auto fixedTicks=scene.GetPhysicsTiming().totalSteps;
        scene.Update(Timestep(_Scene::PhysicsStepSeconds*.5));
        const auto presentation=scene.GetRenderTransform(bodies[0]).matrix;
        run(false);auto interpolated=run(true,glm::vec3(presentation[3]));
        Check(HasDirtyReason(interpolated.stats.decisions[2].reasons,PassDirtyReason::Transform)
            && Take(scene.RenderData().ResolvePick(table,interpolated.pixel),"interpolated pick")==reused
            && bodies[0].Transform().GetTransform()==pose && scene.GetPhysicsTiming().totalSteps==fixedTicks,
            "tickless object interpolation invalidates picking without simulation mutation");
        scene.OnRuntimeStop();
        if(const auto* output=SDL_getenv("GENGINE_PICKING_MEASURE")) {
            Check(color.OnResize(1280,640) && picking.OnResize(1280,640),"measurement target extent");
            camera.viewportWidth=1280;camera.viewportHeight=640;
            for(unsigned i=4;i<64;++i) {
                auto entity=scene.CreateEntity("readback workload "+std::to_string(i));
                entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,material});
                entity.Transform().Translation={-3.5f+float(i%8),-1.75f+.5f*float(i/8),-.5f};
                entity.Transform().Scale={.2f,.2f,.2f};
            }
            std::ofstream csv(output);csv<<"mode,sample,readback_ns,reads,pick_items,executed\n";
            for(const char* mode:{"idle","cached","dirty"}) {
                const bool requested=std::string_view(mode)!="idle";
                for(int i=-120;i<240;++i) {
                    if(std::string_view(mode)=="dirty") submitter.InvalidatePassContents();
                    const auto before=PickReads::calls;auto measured=run(requested);
                    if(i>=0) csv<<mode<<','<<i<<','<<measured.ns<<','<<(PickReads::calls-before)<<','
                        <<measured.stats.pickDraws<<','<<measured.stats.decisions[2].executed<<'\n';
                }
            }
            Check(bool(csv),"readback CSV output");
            std::println("[PASS] picking-measure 64 boxes / 1280x640 / 120 warmup / 240 samples / idle-cached-dirty");
        }
        std::println("[PASS] picking-architecture idle/click/viewport/DPI/generation/instancing/bounds/interpolation/readback");
    }
    void Invalidation(EngineContext& root,SceneRenderResources& resources,FrameSubmissionDesc desc,
        MeshHandle mesh,MaterialInstanceHandle material)
    {
        auto submitter=Take(FrameSubmission::Create(),"invalidation submitter");
        _Scene scene; auto camera=Camera(scene);
        auto body=scene.CreateEntity("cached body");
        body.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,material});
        body.AddComponent<VisibilityComponent>();
        auto sun=scene.CreateEntity("cached sun"),bulb=scene.CreateEntity("cached bulb");
        RenderLightComponent light;light.castShadows=true;
        sun.AddComponent<RenderLightComponent>(light);
        sun.GetComponent<Transform3DComponent>().QuatRotation=glm::rotation(glm::vec3(0,0,-1),-glm::normalize(glm::vec3(20,50,20)));
        light.kind=RenderLightKind::Point;light.range=100;
        bulb.AddComponent<RenderLightComponent>(light);
        bulb.GetComponent<Transform3DComponent>().Translation={0,3,2};
        auto picking=Take(MousePickFrameBuffer::Create(64,64),"invalidation picking target");
        auto point=Take(PointShadowFrameBuffer::Create(256,256),"invalidation point target");
        auto cascade=Take(CascadeShadowFrameBuffer::Create(256,256,5),"invalidation cascade target");
        FrameSubmissionDesc targets{desc.color,picking,point,cascade,resources.Pipelines(),desc.cascadeSplits,
            desc.cameraFov,desc.cameraAspect,desc.cameraNear,desc.cameraFar,desc.pointNear,desc.pointFar};
        EntityPickTable picks;
        std::vector<ScenePipeline> overrideRoles;
        auto run=[&] {
            auto access=resources.Publication().BeginFrame();
            auto frame=Take(Extract(scene,resources,access,camera),"invalidation extraction");
            targets.pipelines=overrideRoles.empty()?resources.Pipelines():std::span<const ScenePipeline>(overrideRoles);
            return Take(submitter.Submit(frame,targets,picks),"invalidation submission");
        };
        auto expect=[&](PassDirtyReason reason,unsigned mask) {
            auto result=run();
            for(unsigned i=0;i<3;++i) {
                Check(result.decisions[i].executed==bool(mask&(1u<<i)),"exact dirty pass selection");
                if(mask&(1u<<i)) Check(HasDirtyReason(result.decisions[i].reasons,reason),"traceable dirty reason");
                else if(result.decisions[i].requested) Check(result.decisions[i].reasons==PassDirtyReason::None,"unaffected requested pass stays clean");
            }
            return result;
        };
        if(const auto* output=SDL_getenv("GENGINE_STATE_CACHE_MEASURE")) {
            targets.instancingEnabled=false;
            // Identical geometry/material shared by 64 overlapping draws; no
            // simulation/extraction/readback/presentation inside timed Submit.
            for(unsigned i=1;i<64;++i) {
                auto copy=scene.CreateEntity("repeated state "+std::to_string(i));
                copy.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,material});
            }
            auto access=resources.Publication().BeginFrame();
            auto frame=Take(Extract(scene,resources,access,camera),"state measurement frozen frame");
            std::ofstream csv(output);csv<<"iteration,cpu_ns";
            csv<<",UseProgram";
            csv<<",BindVertexArray";
            csv<<",BindFramebuffer";
            csv<<",ActiveTexture";
            csv<<",BindTexture";
            csv<<",BindSampler";
            csv<<",Enable";
            csv<<",Disable";
            csv<<",Viewport";
            csv<<",Scissor";
            csv<<",DepthMask";
            csv<<",DepthFunc";
            csv<<",ColorMask";
            csv<<",StencilMask";
            csv<<",BlendEquationSeparate";
            csv<<",BlendFuncSeparate";
            csv<<",CullFace";
            csv<<",FrontFace";
            csv<<",PolygonMode";
            csv<<",LineWidth";
            csv<<",DepthRange";
            csv<<",ClearDepth";
            csv<<",ClearColor";
            csv<<",DrawBuffer";
            csv<<",ReadBuffer";
            csv<<",BindBufferBase";
            csv<<",DrawArrays";
            csv<<",DrawElements";
            csv<<'\n';
            for(unsigned i=0;i<360;++i) {
                submitter.InvalidatePassContents();
                std::chrono::nanoseconds elapsed;
                {
                    DriverCalls::Observe observe;
                    const auto start=std::chrono::steady_clock::now();
                    auto result=Take(submitter.Submit(frame,targets,picks),"state measurement submission");
                    elapsed=std::chrono::steady_clock::now()-start;
                    Check(result.shadowDraws==128&&result.pickDraws==64&&result.colorDraws==64,"matched 256-draw frozen workload");
                }
                if(i>=120) {csv<<i<<','<<elapsed.count();for(auto count:DriverCalls::counts) csv<<','<<count;csv<<'\n';}
                root.MainWindow()->SwapBuffer();
            }
            const auto pixels=Pixels(desc.color);
            std::ofstream image(std::string(output)+".rgba",std::ios::binary);
            image.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
            Check(csv.good()&&image.good(),"state measurement output");
            std::println("[PASS] state-measure 120 warmup / 240 samples / 256 draws; 64x64 linear RGBA8; 256 shadows; one retained frame; no Physics");
            return;
        }
        if(const auto* output=SDL_getenv("GENGINE_TIMING_FIXTURE")) {
            // A separate frozen fixture keeps shadow/picking work active even
            // when the default application legitimately caches those images.
            PassTiming timing;Check(timing.Initialize(true,output),"fixture timing capture");
            auto measured=Take(FrameSubmission::Create(),"fixture submitter");
            for(unsigned iteration=0;iteration<364;++iteration) {
                Check(timing.BeginFrame(),"fixture frame");
                {
                    PassTiming::Activation active(timing);
                    measured.InvalidatePassContents();
                    auto access=resources.Publication().BeginFrame();
                    auto frame=Take(Extract(scene,resources,access,camera),"fixture frozen extraction");
                    auto result=Take(measured.Submit(frame,targets,picks),"fixture submission");
                    Check(result.shadowDraws==2 && result.pickDraws==1 && result.colorDraws==1,"fixture exact workload");
                }
                timing.EndFrame();
                root.MainWindow()->SwapBuffer(); // Normal submission progress, never a query wait.
            }
            Check(timing.OutputHealthy(),"fixture output healthy");
            std::println("[PASS] timing-fixture 120 warmup / 240 samples / 4 drain frames; 64x64 color, 256 shadows, one box, directional+point, fixed camera; forced invalidation");
        }
        expect(PassDirtyReason::InitialContent,7);
        const auto color=Pixels(desc.color);
        if(!SDL_getenv("GENGINE_STATE_CACHE_MEASURE")) {
            // Poison object-local routing as well as global state, between two
            // independent submissions. The next submission must establish both.
            desc.color.Bind();glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
            picking.Bind();glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
            glEnable(GL_FRAMEBUFFER_SRGB);glEnable(GL_POLYGON_OFFSET_LINE);glEnable(GL_POLYGON_OFFSET_POINT);
            glColorMaski(0,GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glDepthMask(GL_FALSE);
            glActiveTexture(GL_TEXTURE0+7);glBindTexture(GL_TEXTURE_2D,0);glBindSampler(0,0);
            GLuint incomingVao{};glGenVertexArrays(1,&incomingVao);glBindVertexArray(incomingVao);
            glViewport(3,4,17,18);
            submitter.InvalidatePassContents();expect(PassDirtyReason::ExternalWrite,7);
            GLint actual{},viewport[4]{};glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&actual);
            Check(actual==int(incomingVao),"submission restores incoming VAO once");
            glGetIntegerv(GL_VIEWPORT,viewport);Check(viewport[0]==3&&viewport[1]==4&&viewport[2]==17&&viewport[3]==18,"submission restores external viewport");
            Check(Pixels(desc.color)==color,"external routing/color-space/mask boundary preserves exact image");
            Check(!RenderBackend::GLStateCache::Current(),"no cache retained across readback/UI/upload boundaries");
            glBindVertexArray(0);glDeleteVertexArrays(1,&incomingVao);
            std::println("[PASS] state-cache submission-boundary/routing/pass-to-pass/image/VAO-restore");
        }
        const auto firstPixel=Take(picking.ReadPixel(32,32),"cached initial pixel");
        auto unchanged=expect(PassDirtyReason::None,0);
        Check(unchanged.shadowDraws==0 && unchanged.pickDraws==0 && Pixels(desc.color)==color,"unchanged frame skips cacheable draws and preserves color");
        Check(Take(picking.ReadPixel(32,32),"cached reused pixel")==firstPixel && picks.Decode(firstPixel),"cache hit keeps image and table paired");
        body.GetComponent<Transform3DComponent>().Translation.x=.25f;
        expect(PassDirtyReason::Transform,7);
        camera.view[3][0]+=.1f;
        expect(PassDirtyReason::Camera,5); // Point shadows are independent of camera.
        camera.projection[0][0]*=1.05f;
        expect(PassDirtyReason::Camera,5);
        bulb.GetComponent<Transform3DComponent>().Translation.x+=.25f;
        expect(PassDirtyReason::Light,2);
        bulb.GetComponent<RenderLightComponent>().range=80;
        expect(PassDirtyReason::Light,2);
        sun.GetComponent<RenderLightComponent>().intensity=.75f;
        expect(PassDirtyReason::Light,1);
        sun.GetComponent<RenderLightComponent>().castShadows=false;
        auto inactive=run();Check(!inactive.decisions[0].requested && !inactive.decisions[0].executed,"disabled shadow defers execution");
        sun.GetComponent<RenderLightComponent>().castShadows=true;
        expect(PassDirtyReason::Light,1);
        targets.pointNear=.2f;
        expect(PassDirtyReason::ShadowSettings,2);
        float splits[]{.6f,1.f,2.f,5.f,20.f};targets.cascadeSplits=splits;
        expect(PassDirtyReason::ShadowSettings,1);
        body.GetComponent<MeshRendererComponent>().pickable=false;
        expect(PassDirtyReason::SceneMembership,4);
        body.GetComponent<MeshRendererComponent>().pickable=true;
        expect(PassDirtyReason::SceneMembership,4);
        body.GetComponent<MeshRendererComponent>().castShadows=false;
        expect(PassDirtyReason::SceneMembership,3);
        body.GetComponent<MeshRendererComponent>().castShadows=true;
        expect(PassDirtyReason::SceneMembership,3);
        body.GetComponent<VisibilityComponent>().layers=2;camera.visibleLayers=1;
        expect(PassDirtyReason::SceneMembership,4);
        camera.visibleLayers=~0u;
        expect(PassDirtyReason::Camera,4); // Directional fit ignores camera layers.
        body.GetComponent<VisibilityComponent>().enabled=false;
        expect(PassDirtyReason::SceneMembership,7);
        Check(Take(picking.ReadPixel(32,32),"empty picking clear")==-1,"empty dirty pass clears old image");
        body.GetComponent<VisibilityComponent>().enabled=true;
        expect(PassDirtyReason::SceneMembership,7);
        {
            auto cpu=Take(root.Shapes().ExportMesh("Box"),"replacement mesh export");
            auto gpu=Take(GpuMesh::Create(cpu),"replacement GPU mesh");
            auto publication=resources.Publication().BeginPublication();
            Check(resources.Meshes().Replace(publication,mesh,std::move(gpu)),"same-handle mesh publication");
        }
        expect(PassDirtyReason::Mesh,7);
        std::optional<MaterialInstance> changed;
        {
            auto access=resources.Publication().BeginFrame();
            changed.emplace(*Take(resources.Materials().Acquire(access,material),"material revision source"));
        }
        Check(changed->SetParameter("u_tiling",std::array<float,2>{2,2}),"edit material value");
        {
            auto publication=resources.Publication().BeginPublication();
            Check(resources.Materials().Replace(publication,material,std::move(*changed)),"same-handle material publication");
        }
        changed.reset();expect(PassDirtyReason::Material,7);
        // The fixture owns these registries through root. Mutate via their real
        // publication APIs to prove dependency revisions independent of material.
        TextureRegistry* images{};SamplerRegistry* samplers{};
        TextureHandle imageId;SamplerHandle samplerId;SamplerDesc samplerDesc;
        {
            auto access=resources.Publication().BeginFrame();auto bindings=resources.ForFrame(access).bindings;
            images=&const_cast<TextureRegistry&>(bindings.textures);samplers=&const_cast<SamplerRegistry&>(bindings.samplers);
            auto frame=Take(Extract(scene,resources,access,camera),"dependency revision sources");
            const auto& texture=frame.Resources()[0].Material().Textures()[0];
            imageId=texture.texture.Identity();samplerId=texture.sampler.Identity();samplerDesc=texture.sampler->Description();
        }
        {
            TextureDesc td;td.width=td.height=1;td.mips=TextureMipIntent::None;
            const std::byte pixel[]{std::byte{255},std::byte{255},std::byte{255},std::byte{255}};
            auto image=Take(TextureResource::Create(td,{pixel}),"texture replacement");
            auto publication=resources.Publication().BeginPublication();
            Check(images->Replace(publication,imageId,std::move(image)),"same-handle texture publication");
        }
        expect(PassDirtyReason::Material,7);
        {
            auto sampler=Take(GpuSampler::Create(samplerDesc),"sampler replacement");
            auto publication=resources.Publication().BeginPublication();
            Check(samplers->Replace(publication,samplerId,std::move(sampler)),"same-handle sampler publication");
        }
        expect(PassDirtyReason::Material,7);
        targets.pickingEnabled=false;
        body.GetComponent<Transform3DComponent>().Translation.y=.25f;
        auto deferred=expect(PassDirtyReason::Transform,3);
        Check(HasDirtyReason(deferred.decisions[2].reasons,PassDirtyReason::Transform) && !deferred.decisions[2].requested,"dirty picking is deferred without a readback request");
        run();targets.pickingEnabled=true;
        expect(PassDirtyReason::Transform,4);
        expect(PassDirtyReason::None,0);
        Check(picking.OnResize(64,64) && point.OnResize(256,256) && cascade.OnResize(256,256),"no-op target resizes");
        expect(PassDirtyReason::None,0);
        const auto identity=picking.Buffer().StorageIdentity();
        auto moved=std::move(picking);picking=std::move(moved);
        Check(picking.Buffer().StorageIdentity()==identity,"storage identity transfers with owner");
        expect(PassDirtyReason::None,0);
        Check(!picking.OnResize(9000,64) && picking.Buffer().StorageIdentity()==identity,"failed target resize preserves allocation identity");
        expect(PassDirtyReason::None,0);
        picking=Take(MousePickFrameBuffer::Create(64,64),"same-size picking recreation");
        expect(PassDirtyReason::TargetStorage,4);
        point=Take(PointShadowFrameBuffer::Create(256,256),"same-size point recreation");
        expect(PassDirtyReason::TargetStorage,2);
        cascade=Take(CascadeShadowFrameBuffer::Create(256,256,5),"same-size cascade recreation");
        expect(PassDirtyReason::TargetStorage,1);
        Check(picking.OnResize(0,0) && !picking.Buffer().StorageIdentity(),"zero-size storage invalidation");
        targets.pickingEnabled=false;run();
        Check(picking.OnResize(64,64),"restore deferred storage");targets.pickingEnabled=true;
        expect(PassDirtyReason::TargetStorage,4);
        Check(point.OnResize(128,128) && cascade.OnResize(128,128),"shadow storage resize");
        expect(PassDirtyReason::TargetStorage,3);
        body.GetComponent<Transform3DComponent>().Translation.z=.25f;camera.view[3][0]+=.1f;
        auto multiple=expect(PassDirtyReason::Transform,7);
        Check(HasDirtyReason(multiple.decisions[0].reasons,PassDirtyReason::Camera)
            && HasDirtyReason(multiple.decisions[2].reasons,PassDirtyReason::Camera),"multiple dirty reasons retained");
        Check(picking.ClearAttachment(0,-1),"external target overwrite");submitter.InvalidatePassContents();
        expect(PassDirtyReason::ExternalWrite,7);
        glBindBufferBase(GL_UNIFORM_BUFFER,0,0);
        const auto before=Pixels(desc.color);expect(PassDirtyReason::None,0);
        Check(Pixels(desc.color)==before,"cached cascade UBO rebound after external state changes");
        body.GetComponent<Transform3DComponent>().Translation.x+=.05f;
        {
            auto access=resources.Publication().BeginFrame();auto frame=Take(Extract(scene,resources,access,camera),"failed submission input");
            glEnable(0xffffffffu); // Pending driver error, consumed by production submission.
            auto failed=submitter.Submit(frame,targets,picks);
            Check(!failed,"injected driver failure is returned");
            const auto* meshError=std::get_if<GpuMeshError>(&failed.error().cause);
            Check(meshError && meshError->code==GpuMeshErrorCode::Driver,"submission preserves the originating mesh driver diagnostic");
        }
        expect(PassDirtyReason::RetryAfterFailure,7);expect(PassDirtyReason::None,0);
        const auto oldId=Take(scene.RenderData().Identify(body),"old entity lifetime");scene.DestroyEntity(body);
        body=scene.CreateEntity("new generation");body.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,material});
        Check(Take(scene.RenderData().Identify(body),"new entity lifetime")!=oldId,"entity recreation changes generation");
        expect(PassDirtyReason::SceneMembership,7);
        const MaterialParameterDecl helperParameters[]{
            {"u_baseColor",MaterialParameterType::Float4,std::array<float,4>{1,1,1,1}},
            {"u_useVertexColor",MaterialParameterType::Boolean,true}};
        const auto helper=Take(resources.PublishMaterial({SceneMaterialKind::Helper,helperParameters,{},true,3}),"cached helper material");
        body.GetComponent<MeshRendererComponent>()={Take(resources.PublishShape("AxisHelper"),"cached helper mesh"),helper,0,false,false,true};
        run();
        auto pickImage=[&] {
            std::vector<int> result;
            for(int y=0;y<64;++y) for(int x=0;x<64;++x) result.push_back(Take(picking.ReadPixel(x,y),"helper pick image"));
            return result;
        };
        const auto helperImage=pickImage();
        glLineWidth(7);submitter.InvalidatePassContents();run();
        Check(pickImage()==helperImage,"picking establishes helper width independently of previous GL state");
        overrideRoles.assign(resources.Pipelines().begin(),resources.Pipelines().end());
        for(auto& role:overrideRoles) if(role.kind==SceneMaterialKind::Helper) role.lineWidth=7;
        expect(PassDirtyReason::Material,4);
        Check(pickImage()!=helperImage,"helper width edit invalidates and changes picking coverage");
        expect(PassDirtyReason::None,0);
        std::println("[PASS] pass-invalidation unchanged/revisions/targets/deferred/multiple/failure checks={}",checks);
    }
    void ShadowContactDepthFixture(SceneRenderResources& resources,const FrameSubmissionDesc& desc,
        MeshHandle sphere,MeshHandle box,MaterialInstanceHandle material)
    {
        // A convex occluder's shadow depth must precede its center along the
        // light ray. Back-surface depth leaks light near receiver contact even
        // though a shadow draw was submitted and the depth target is nonempty.
        for(const auto mesh:{sphere,box}) {
            _Scene scene;
            const auto camera=Camera(scene);
            auto caster=scene.CreateEntity("contact depth occluder");
            caster.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,material});
            auto sun=scene.CreateEntity("contact depth sun");
            RenderLightComponent light;light.castShadows=true;
            sun.AddComponent<RenderLightComponent>(light);
            // Default light transform points toward +Z; lookAt's Y up is valid.
            auto submitter=Take(FrameSubmission::Create(),"contact depth submitter");
            EntityPickTable picks;
            auto access=resources.Publication().BeginFrame();
            auto frame=Take(Extract(scene,resources,access,camera),"contact depth extraction");
            RenderBackend::PackedFrame gpu{};
            {
                UploadCalls::Observe uploads;
                const auto stats=Take(submitter.Submit(frame,desc,picks),"contact depth submission");
                Check(stats.shadowDraws==1,"one directional occluder draw");
                glGetNamedBufferSubData(UploadCalls::lastFrameBuffer,0,sizeof(gpu),&gpu);
            }
            const auto& storage=desc.cascadeShadow.Buffer().Description();
            std::vector<float> depth(static_cast<std::size_t>(storage.Width)*storage.Height*storage.Layers);
            Check(TextureView(Take(desc.cascadeShadow.DepthView(),"contact cascade view")).Bind(0),"contact depth bind");
            glGetTexImage(GL_TEXTURE_2D_ARRAY,0,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
            Check(glGetError()==GL_NO_ERROR,"contact depth readback succeeds");
            // Both middle and outer cascades are checked when the center is
            // covered; selection/transport comes from the actual uploaded block.
            unsigned covered{};
            for(std::size_t layer=0;layer<desc.cascadeSplits.size();++layer) {
                glm::mat4 matrix;std::memcpy(&matrix,gpu.cascades[layer].data(),sizeof(matrix));
                const auto clip=matrix*glm::vec4(0,0,0,1);
                const auto center=glm::vec3(clip)/clip.w*.5f+.5f;
                const int x=static_cast<int>(center.x*storage.Width),y=static_cast<int>(center.y*storage.Height);
                if(x<1 || y<1 || x>=static_cast<int>(storage.Width)-1 || y>=static_cast<int>(storage.Height)-1
                    || center.z<=0.f || center.z>=1.f) continue;
                const float actual=depth[(layer*storage.Height+y)*storage.Width+x];
                const float depthPerWorldUnit=.5f*glm::length(glm::vec3(matrix[0][2],matrix[1][2],matrix[2][2]));
                std::println("contact-depth mesh={} layer={} center={} actual={} depth/world={}",
                    mesh==sphere?"sphere":"box",layer,center.z,actual,depthPerWorldUnit);
                Check(actual<center.z-.1f*depthPerWorldUnit,
                    "directional shadow stores nearest occluder surface, not far surface");
                ++covered;
            }
            Check(covered>=2,"contact depth sampled across multiple cascades");
        }
        std::println("[PASS] shadow-contact nearest-occluder sphere/box across cascades");
    }

    namespace InstanceCalls
    {
        std::uint64_t calls{},items{};
        GLuint buffer{};
        bool failUpload{}, rejected{};
        decltype(glad_glDrawArraysInstanced) arrays{};
        decltype(glad_glDrawElementsInstanced) elements{};
        decltype(glad_glNamedBufferData) upload{};
        decltype(glad_glGetError) error{};
        void APIENTRY Arrays(GLenum mode,GLint first,GLsizei count,GLsizei instances)
        { ++calls;items+=instances;arrays(mode,first,count,instances); }
        void APIENTRY Elements(GLenum mode,GLsizei count,GLenum type,const void* first,GLsizei instances)
        { ++calls;items+=instances;elements(mode,count,type,first,instances); }
        void APIENTRY Upload(GLuint name,GLsizeiptr bytes,const void* data,GLenum usage)
        {
            if(usage==GL_STREAM_DRAW && bytes%sizeof(RenderBackend::PackedInstance)==0) {
                buffer=name;
                if(failUpload) {failUpload=false;rejected=true;return;}
            }
            upload(name,bytes,data,usage);
        }
        GLenum APIENTRY Error() {if(rejected) {rejected=false;return GL_OUT_OF_MEMORY;}return error();}
        struct Observe {
            Observe() {
                calls=items=0;buffer=0;failUpload=rejected=false;
                arrays=glad_glDrawArraysInstanced;glad_glDrawArraysInstanced=Arrays;
                elements=glad_glDrawElementsInstanced;glad_glDrawElementsInstanced=Elements;
                upload=glad_glNamedBufferData;glad_glNamedBufferData=Upload;
                error=glad_glGetError;glad_glGetError=Error;
            }
            ~Observe() {
                glad_glDrawArraysInstanced=arrays;glad_glDrawElementsInstanced=elements;
                glad_glNamedBufferData=upload;glad_glGetError=error;
            }
        };
    }
    namespace InstanceLimit
    {
        decltype(glad_glGetInteger64v) query{};
        void APIENTRY Query(GLenum key,GLint64* value)
        { query(key,value);if(key==GL_MAX_SHADER_STORAGE_BLOCK_SIZE) *value=8*sizeof(RenderBackend::PackedInstance); }
        struct Scope {
            Scope() {query=glad_glGetInteger64v;glad_glGetInteger64v=Query;}
            ~Scope() {glad_glGetInteger64v=query;}
        };
    }
    void InstancingFixture(SceneRenderResources& resources,FrameSubmissionDesc desc,MeshHandle box,MeshHandle sphere,
        MaterialInstanceHandle material,std::span<const MaterialParameterDecl> parameters,
        std::span<const MaterialTextureAssignment> textures)
    {
        std::optional<MaterialInstance> alternateSource;
        {auto access=resources.Publication().BeginFrame();alternateSource.emplace(*Take(resources.Materials().Acquire(access,material),"instance material clone"));}
        Check(alternateSource->SetTexture("albedoMap",textures.back().value),"visibly distinct material on shared pipeline");
        MaterialInstanceHandle alternate;
        {auto publication=resources.Publication().BeginPublication();alternate=Take(resources.Materials().Create(publication,std::move(*alternateSource)),"instance alternate material");}
        const auto masked=Take(resources.PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,
            AlphaMode::Masked,.5f,1}),"instance masked material");
        const auto transparent=Take(resources.PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,
            AlphaMode::Transparent,.5f,.5f}),"instance transparent material");
        desc.pipelines=resources.Pipelines();
        const auto depthImages=[&] {
            std::vector<float> depth(256*256*11);
            Check(TextureView(Take(desc.cascadeShadow.DepthView(),"instance cascade view")).Bind(0),"instance cascade bind");
            glGetTexImage(GL_TEXTURE_2D_ARRAY,0,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
            Check(TextureView(Take(desc.pointShadow.DepthView(),"instance point view")).Bind(0),"instance point bind");
            for(unsigned face=0;face<6;++face)
                glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data()+256*256*(5+face));
            Check(glGetError()==GL_NO_ERROR,"instance shadow readback");return depth;
        };
        const auto pickingImage=[&] {
            std::vector<int> pixels(64*64);
            desc.picking.Bind();glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,64,64,GL_RED_INTEGER,GL_INT,pixels.data());
            Check(glGetError()==GL_NO_ERROR,"instance picking readback");return pixels;
        };
        // Full-width synthetic identities and nonzero ranges for all three index formats.
        // The first triangle is far outside the image: drawing the wrong range loses every hit.
        if(!SDL_getenv("GENGINE_INSTANCE_MEASURE")) for(auto format:{MeshIndexFormat::None,MeshIndexFormat::UInt16,MeshIndexFormat::UInt32}) {
            const std::array<glm::vec3,6> vertices{{{99,99,0},{100,99,0},{99,100,0},{-.45f,-.4f,0},{.45f,-.4f,0},{0,.45f,0}}};
            const VertexAttribute position{};const SubmeshRange ranges[]{{0,3,0},{3,3,0}};
            const std::array<std::uint16_t,6> shortIndices{0,1,2,3,4,5};const std::array<std::uint32_t,6> longIndices{0,1,2,3,4,5};
            auto source=MeshSourceData::FromVertices<glm::vec3>(vertices,{&position,1});source.submeshes=ranges;
            source.indexFormat=format;
            if(format!=MeshIndexFormat::None) {source.indices=format==MeshIndexFormat::UInt16?std::span<const std::byte>(std::as_bytes(std::span(shortIndices))):std::span<const std::byte>(std::as_bytes(std::span(longIndices)));source.indexCount=6;}
            auto cpuMesh=Take(MeshAsset::Create(source),"instance ranged CPU mesh");MeshHandle handle;
            {auto publication=resources.Publication().BeginPublication();handle=Take(PublishMesh(resources.Meshes(),publication,cpuMesh),"instance ranged publication");}
            auto access=resources.Publication().BeginFrame();auto state=resources.ForFrame(access);
            auto mesh=Take(resources.Meshes().Acquire(access,handle),"instance ranged lease");
            auto instance=Take(resources.Materials().Acquire(access,material),"instance ranged material");
            auto packet=Take(PreparedMaterialBinding::Prepare(instance,access,state.bindings),"instance ranged packet");
            auto builder=Take(RenderFrameBuilder::Create({1,4,0,1}),"instance identity builder");
            _Scene scene;auto camera=Camera(scene);camera.view=glm::mat4(1);camera.projection=glm::ortho(-2.f,2.f,-2.f,2.f,.1f,20.f);
            Check(builder.AddCamera(camera),"instance identity camera");
            const auto resource=Take(builder.AddResources(mesh,std::move(packet)),"instance identity resource");
            for(unsigned i=0;i<4;++i) {
                FrameDrawDesc draw;draw.resources=resource;draw.submesh=1;
                draw.worldTransform=glm::translate(glm::mat4(1),glm::vec3(float(i%2)*2-1,float(i/2)*2-1,-2));
                draw.entity={7,(std::uint64_t{1}<<48)+i+1,(std::uint64_t{1}<<52)+i+1};
                Check(builder.AddDraw(draw),"instance identity draw");
            }
            auto frame=Take(std::move(builder).Finalize(),"instance identity finalize");
            auto submitter=Take(FrameSubmission::Create(),"instance range submitter");EntityPickTable picks;
            desc.instancingEnabled=true;
            {
                InstanceCalls::Observe observer;
                auto stats=Take(submitter.Submit(frame,desc,picks),"instance ranged draw");
                Check(stats.submittedDrawCalls==2 && stats.instancedDrawCalls==2,"nonzero range instanced color/picking draws");
                const auto image=pickingImage();
                for(unsigned i=0;i<4;++i) {
                    const auto pixel=image[(16+32*(i/2))*64+16+32*(i%2)];
                    Check(Take(picks.Decode(pixel),"full-width range picking")==frame.Draws()[i].entity,"high-bit generation/domain picking remains distinct");
                }
                std::array<RenderBackend::PackedInstance,8> uploaded;
                glGetNamedBufferSubData(InstanceCalls::buffer,0,sizeof(uploaded),uploaded.data());
                for(unsigned i=0;i<4;++i) Check(uploaded[4+i].identity0[2]==(1u<<16) && uploaded[4+i].identity1[0]==(1u<<20),"uploaded generation/domain high words preserved");
            }
            Check(mesh->DrawSubmeshInstanced(1,0),"zero instance count no-op");
            auto overflow=mesh->DrawSubmeshInstanced(1,std::size_t(INT_MAX)+1);
            Check(!overflow && overflow.error().code==GpuMeshErrorCode::DeviceLimit,"instance count overflow typed failure");
            Check(!mesh->DrawSubmeshInstanced(2,4),"invalid instanced submesh typed failure");
        }
        const auto* output=SDL_getenv("GENGINE_INSTANCE_MEASURE");
        std::ofstream csv;
        if(output) {csv.open(output);csv<<"workload,mode,iteration,cpu_ns,gpu_ns,draw_calls,instances\n";}
        for(const auto mesh:{sphere,box}) for(const unsigned count:output?std::vector<unsigned>{64}:std::vector<unsigned>{0,1,3,4,5,64})
        for(unsigned variant=0;variant<(output?1u:4u);++variant) {
            _Scene scene;auto camera=Camera(scene);
            camera.view=glm::lookAt(glm::vec3(0,0,12),glm::vec3(0),glm::vec3(0,1,0));
            camera.projection=glm::ortho(-4.f,4.f,-4.f,4.f,.1f,20.f);camera.worldPosition={0,0,12};
            auto sun=scene.CreateEntity("instance sun"),bulb=scene.CreateEntity("instance bulb");
            RenderLightComponent light;light.castShadows=true;sun.AddComponent<RenderLightComponent>(light);
            sun.GetComponent<Transform3DComponent>().QuatRotation=glm::rotation(glm::vec3(0,0,-1),-glm::normalize(glm::vec3(2,5,3)));
            light.kind=RenderLightKind::Point;light.range=50;bulb.AddComponent<RenderLightComponent>(light);
            bulb.GetComponent<Transform3DComponent>().Translation={0,3,5};
            for(unsigned i=0;i<count;++i) {
                auto entity=scene.CreateEntityWithUUID(UUID(100+i),"instance "+std::to_string(i));
                const auto selected=variant==1 && i%2?alternate:variant==2?masked:variant==3?transparent:material;
                entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,selected});
                auto& pose=entity.GetComponent<Transform3DComponent>();
                pose.Translation={float(i%8)-3.5f,float(i/8)-3.5f,float(i%3)*.15f};
                pose.Scale={.3f,.25f,.2f};pose.QuatRotation=glm::angleAxis(float(i)*.12f,glm::normalize(glm::vec3(1,2,3)));
            }
            auto access=resources.Publication().BeginFrame();
            auto frame=Take(Extract(scene,resources,access,camera),"instance immutable frame");
            const std::vector<DrawItem> before(frame.Draws().begin(),frame.Draws().end());
            std::vector<std::byte> referenceColor;std::vector<float> referenceDepth;std::vector<int> referencePicking;
            std::vector<EntityRenderId> referenceIds;
            for(bool instanced:{false,true}) {
                std::optional<FrameSubmission> owner(Take(FrameSubmission::Create(),"instance submitter"));
                auto& submitter=*owner;EntityPickTable picks;
                desc.instancingEnabled=instanced;
                FrameSubmissionStats stats;
                GLuint instanceName{};
                const unsigned iterations=output?360:1;
                std::vector<GLuint> queries(output?iterations:0);
                std::vector<std::int64_t> cpu(iterations);
                if(output) glGenQueries(static_cast<GLsizei>(queries.size()),queries.data());
                for(unsigned iteration=0;iteration<iterations;++iteration) {
                    submitter.InvalidatePassContents(); // Identical four-pass work for both modes.
                    if(output) glBeginQuery(GL_TIME_ELAPSED,queries[iteration]);
                    {
                        InstanceCalls::Observe observer;DriverCalls::Observe serial;
                        const auto start=std::chrono::steady_clock::now();
                        stats=Take(submitter.Submit(frame,desc,picks),"instance submission");
                        cpu[iteration]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
                        Check(stats.instancedDrawCalls==InstanceCalls::calls,"instance counters match real ordinary GL calls");
                        Check(stats.submittedDrawCalls==InstanceCalls::calls+DriverCalls::counts[26]+DriverCalls::counts[27],"total driver draws match reported draws");
                        if(InstanceCalls::buffer) instanceName=InstanceCalls::buffer;
                    }
                    if(output) glEndQuery(GL_TIME_ELAPSED);
                }
                Check(stats.colorDraws==count && stats.pickDraws==count,"logical color and picking work conserved");
                if(variant!=3) Check(stats.shadowDraws==2*count && stats.submittedInstances==4*count,"logical shadow and submitted instance work conserved");
                if(!instanced || count<MinimumRenderInstances || variant==3) Check(stats.instancedDrawCalls==0,"threshold and transparent serial fallback");
                if(instanced && count>=MinimumRenderInstances && variant!=1 && variant!=3)
                    Check(stats.submittedDrawCalls==4 && stats.instancedDrawCalls==4,"repeated mesh reduces four passes to four draws");
                if(instanced && variant==1 && count==64)
                    Check(stats.colorDraws==64 && stats.submittedDrawCalls==194,"mixed material groups stay separate; picking and shadow source order retained");
                if(instanced && variant==0 && count>=MinimumRenderInstances) {
                    std::vector<RenderBackend::PackedInstance> uploaded(count*3);
                    glGetNamedBufferSubData(instanceName,0,static_cast<GLsizeiptr>(uploaded.size()*sizeof(uploaded[0])),uploaded.data());
                    for(unsigned i=0;i<count;++i) {
                        const auto& id=frame.Draws()[i].entity;const auto& packet=uploaded[2*count+i];
                        Check(packet.identity0[0]==id.index && (std::uint64_t(packet.identity0[2])<<32|packet.identity0[1])==id.generation
                            && (std::uint64_t(packet.identity1[0])<<32|packet.identity0[3])==id.registry,"GPU instance payload retains full typed entity identity");
                        Check(std::memcmp(packet.model.data(),glm::value_ptr(frame.Draws()[i].worldTransform),64)==0,"GPU per-instance transform bytes");
                        Check(Take(picks.Decode(static_cast<int>(packet.identity1[1])),"instance encoded pixel")==id,"GPU pixel maps to full entity identity");
                    }
                }
                const auto color=Pixels(desc.color);const auto depth=depthImages();const auto pixels=pickingImage();
                std::vector<EntityRenderId> ids;std::set<int> hits;
                for(auto pixel:pixels) {ids.push_back(pixel<0?EntityRenderId{}:Take(picks.Decode(pixel),"instance pixel decode"));if(pixel>=0) hits.insert(pixel);}
                if(!instanced) {referenceColor=color;referenceDepth=depth;referencePicking=pixels;referenceIds=ids;}
                else {
                    Check(color==referenceColor,"serial/instanced color pixels identical");
                    Check(depth==referenceDepth,"serial/instanced cascade and point shadow transforms identical");
                    Check(pixels==referencePicking && ids==referenceIds,"per-instance picking identity image identical");
                    if(variant!=3) Check(hits.size()==count,"every separate instance remains pickable");
                }
                if(output) {
                    for(unsigned i=0;i<iterations;++i) {
                        GLuint64 gpu{};glGetQueryObjectui64v(queries[i],GL_QUERY_RESULT,&gpu); // Fixture-only collection after CPU samples.
                        if(i>=120) csv<<(mesh==sphere?"sphere-lattice":"box-stack")<<','<<(instanced?"instanced":"serial")<<','<<i<<','
                            <<cpu[i]<<','<<gpu<<','<<stats.submittedDrawCalls<<','<<stats.submittedInstances<<'\n';
                    }
                    glDeleteQueries(static_cast<GLsizei>(queries.size()),queries.data());
                }
                if(instanced && count==4 && variant==0) {
                    // Exact nonzero SSBO range restoration and failed instance upload retry.
                    GLuint foreign{};glCreateBuffers(1,&foreign);GLint alignment{};glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&alignment);
                    glNamedBufferData(foreign,alignment+1024,nullptr,GL_STATIC_DRAW);glBindBufferRange(GL_SHADER_STORAGE_BUFFER,1,foreign,alignment,512);
                    // Cached auxiliary passes shorten the instance batch, requiring a fresh upload.
                    {InstanceCalls::Observe observer;InstanceCalls::failUpload=true;
                        const auto failed=submitter.Submit(frame,desc,picks);
                        Check(!failed && std::get<SubmissionCode>(failed.error().cause)==SubmissionCode::Driver,"instance upload failure is typed");}
                    GLint binding{};GLint64 start{},size{};
                    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,1,&binding);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,1,&start);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,1,&size);
                    Check(binding==foreign && start==alignment && size==512,"instance binding range restored on failure");
                    Take(submitter.Submit(frame,desc,picks),"failed instance upload retry");
                    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,1,&binding);Check(binding==foreign,"instance binding restored after successful retry");
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,0);glDeleteBuffers(1,&foreign);
                }
                if(!output && instanced && count==64 && variant==0) {
                    std::optional<FrameSubmission> limited;
                    {InstanceLimit::Scope limit;limited.emplace(Take(FrameSubmission::Create(),"limited instance storage"));}
                    const auto limitedStats=Take(limited->Submit(frame,desc,picks),"instance storage serial tail");
                    Check(limitedStats.submittedDrawCalls==249 && limitedStats.instancedDrawCalls==1
                        && limitedStats.submittedInstances==256,"bounded instance prefix plus complete serial fallback");
                    Check(Pixels(desc.color)==referenceColor && depthImages()==referenceDepth && pickingImage()==referencePicking,
                        "capacity fallback preserves all raster outputs");
                }
                owner.reset();
                if(instanceName) Check(!glIsBuffer(instanceName),"instance buffer retires with its context-thread owner");
            }
            for(std::size_t i=0;i<before.size();++i) Check(before[i].entity==frame.Draws()[i].entity
                && before[i].worldTransform==frame.Draws()[i].worldTransform && before[i].resources==frame.Draws()[i].resources,"instancing leaves published frame immutable");
        }
        if(output) {Check(csv.good(),"instance measurement output");std::println("[PASS] instance-measure 120 warmup / 240 samples / serial+instanced / lattice+stack");}
        std::println("[PASS] instancing threshold/mixed/transparent/identity/color/picking/shadows/immutable/failure/range");
    }

    void Run(EngineContext& root)
    {
        std::println("Image comparison: renderer={} vendor={} version={}; 64x64 RGBA8 linear target; camera eye=(0,2,8), target=(0,0,0), FOV=45deg, aspect=1, near=.1, far=20; same-driver RGB composition tolerance=2.5/255",
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),reinterpret_cast<const char*>(glGetString(GL_VENDOR)),reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        auto services=Take(root.SceneServices(),"typed scene services");
        Geometry indexed;
        const std::vector<Vec3f> positions{{-1,0,0},{1,0,0},{0,1,0}};
        const std::vector<Vec3f> colors(3,Vec3f(1,0,0)), normals(3,Vec3f(0,0,1));
        const std::vector<Vec2f> uv{{0,0},{1,0},{.5f,1}};
        const std::vector<unsigned> indices{2,0,1};
        indexed.AddAttributes(positions,colors,uv,normals);indexed.AddIndices(indices);
        auto* window=SDL_GL_GetCurrentWindow();auto context=SDL_GL_GetCurrentContext();
        Check(SDL_GL_MakeCurrent(window,nullptr)==0,"detach for CPU shape export");
        auto indexedCpu=Take(indexed.ExportCpuMesh(),"indexed export without context");
        Check(indexedCpu.IndexFormat()==MeshIndexFormat::UInt32 && indexedCpu.IndexCount()==3
            && indexedCpu.Indices().size()==indices.size()*sizeof(unsigned)
            && std::memcmp(indexedCpu.Indices().data(),indices.data(),indexedCpu.Indices().size())==0,
            "CPU index order and bytes preserved");
        Check(indexedCpu.Submeshes()[0].elementCount==3 && indexedCpu.Layout().attributes.size()==4,
            "indexed range and all semantic attributes preserved");
        for (const auto* name:{"Sphere","Box","Diamond","PointLightHelper","AxisHelper","GridHelper","SkyBox","SmoothSphere"})
        {
            auto cpu=Take(services.shapes.ExportMesh(name),"CPU shape export without GL context");
            Check(cpu.VertexCount()>0 && cpu.Submeshes().size()==1,"complete CPU shape");
            for (const auto& attribute:cpu.Layout().attributes) Check(attribute.slot==AttributeSlot(attribute.semantic),"fixed semantic slot");
        }
        Check(!services.shapes.ExportMesh("missing-shape"),"missing shape typed failure");
        Check(SDL_GL_MakeCurrent(window,context)==0,"restore owner context");
        auto resources=Take(SceneRenderResources::Create(root),"resource publisher");
        const auto sphere=Take(resources->PublishShape("Sphere"),"sphere publication");
        Check(Take(resources->PublishShape("Sphere"),"reuse sphere")==sphere,"shared mesh identity");
        Check(!resources->PublishShape("absent"),"missing shape does not publish");
        Check(resources->Meshes().Size()==1,"failed mesh publication rolls back");
        const auto box=Take(resources->PublishShape("Box"),"box publication");
        const auto diamond=Take(resources->PublishShape("Diamond"),"diamond publication");
        const auto axis=Take(resources->PublishShape("AxisHelper"),"axis publication");
        const auto helperMesh=Take(resources->PublishShape("PointLightHelper"),"point helper publication");
        const std::pair<MeshHandle,Geometry*> referenceGeometry[]{
            {sphere,Manager::ShapeManager::GetShape("Sphere")},{box,Manager::ShapeManager::GetShape("Box")},
            {diamond,Manager::ShapeManager::GetShape("Diamond")},{helperMesh,Manager::ShapeManager::GetShape("PointLightHelper")}};
        for(const auto& [handle,geometry]:referenceGeometry) {MeshComponent legacyUpload(geometry);}
        auto* pickShader=Take(Manager::ShaderManager::GetShaderProgram({RuntimeAssets::File("Shaders/mouse_pick.vert"),
            RuntimeAssets::File("Shaders/mouse_pick.frag")}),"legacy reference picking shader");
        const char* names[]{"albedoMap","normalMap","metallicMap","roughnessMap","aoMap"};
        const char* paths[]{"PBR/rustediron/rustediron2_basecolor","PBR/rustediron/rustediron2_normal",
            "PBR/rustediron/rustediron2_metallic","PBR/rustediron/rustediron2_roughness",
            "PBR/subtle_black_granite/subtle-black-granite_ao"};
        std::vector<MaterialTextureAssignment> textures;
        for (int i=0;i<5;++i)
        {
            auto* image=Take(Manager::AssetsManager::GetTexture(paths[i],names[i]),"fixture texture");
            auto sample=Take(Manager::AssetsManager::SampleTexture(image->View()),"fixture sampling");
            textures.push_back({names[i],{sample.TextureIdentity(),sample.SamplerIdentity()}});
        }
        const MaterialParameterDecl parameters[]{
            {"metalness",MaterialParameterType::Float3,std::array<float,3>{.8f,.8f,.8f}},
            {"u_tiling",MaterialParameterType::Float2,std::array<float,2>{1,1}}};
        const auto material=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures}),"PBR material publication");
        const auto materialCount=resources->Materials().Size();
        const MaterialParameterDecl duplicate[]{{"same",MaterialParameterType::Float,1.f},{"same",MaterialParameterType::Float,2.f}};
        Check(!resources->PublishMaterial({SceneMaterialKind::Lit,duplicate,textures}),"invalid template rolls back dependencies");
        Check(resources->Materials().Size()==materialCount,"material rollback count");
        {
            _Scene physicsScene;
            const auto physicsCamera=Camera(physicsScene);
            auto body=physicsScene.CreateEntity("presentation body");
            body.GetComponent<Transform3DComponent>().Translation={0,5,0};
            body.AddComponent<MeshRendererComponent>(MeshRendererComponent{sphere,material});
            RigidBody3DComponent rigid;rigid.Type=BodyType::Dynamic;
            body.AddComponent<RigidBody3DComponent>(rigid);
            SphereFixture3DComponent fixture;fixture.Radius=1;fixture.Property.m_InvMass=1;
            body.AddComponent<SphereFixture3DComponent>(fixture);
            physicsScene.OnRuntimeStart();physicsScene.Update(Timestep(.025f));
            const auto authoritative=body.GetComponent<Transform3DComponent>().Translation;
            const auto expected=physicsScene.GetRenderTransform(body).matrix;
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(physicsScene,*resources,access,physicsCamera),"physics presentation extraction");
            Check(frame.Draws().size()==1 && frame.Draws()[0].worldTransform==expected,"fixed-step presentation/interpolation parity");
            Check(body.GetComponent<Transform3DComponent>().Translation==authoritative,"extraction does not change authoritative pose");
        }
        const MaterialParameterDecl helperParameters[]{
            {"u_baseColor",MaterialParameterType::Float4,std::array<float,4>{1,1,1,1}},
            {"u_useVertexColor",MaterialParameterType::Boolean,true}};
        const auto helper=Take(resources->PublishMaterial({SceneMaterialKind::Helper,helperParameters,{},true,3}),"helper material");
        const auto pointMaterial=Take(resources->PublishMaterial({SceneMaterialKind::PointLight,{},{}}),"point material");
        TextureDesc skyDesc;skyDesc.kind=TextureKind::Cube;skyDesc.colorSpace=TextureColorSpace::Linear;
        skyDesc.mips=TextureMipIntent::None;skyDesc.orientation=ImageOrientation::TopLeft;
        auto* skyImage=Take(Manager::AssetsManager::GetTexture("SkyBox/Day/","u_skyBoxDay",".png",skyDesc),"sky texture");
        auto skySample=Take(Manager::AssetsManager::SampleTexture(skyImage->View()),"sky sampling");
        const MaterialTextureAssignment skyBinding{"u_skyBoxDay",{skySample.TextureIdentity(),skySample.SamplerIdentity()}};
        const auto skyMaterial=Take(resources->PublishMaterial({SceneMaterialKind::Sky,{},{&skyBinding,1},true}),"sky material");
        const auto skyMesh=Take(resources->PublishShape("SkyBox"),"sky mesh");
        auto submitter=Take(FrameSubmission::Create(),"serial submitter");
        RenderTargetDesc targetDesc;
        targetDesc.Storage.Width=targetDesc.Storage.Height=64;
        targetDesc.Storage.Colors[0]=FramebufferFormat::RGBA8;targetDesc.Storage.ColorCount=1;
        targetDesc.Storage.Depth=FramebufferFormat::Depth24Stencil8;
        auto target=Take(RenderTarget::Create(targetDesc),"color target");
        auto picking=Take(MousePickFrameBuffer::Create(64,64),"pick target");
        auto pointTarget=Take(PointShadowFrameBuffer::Create(256,256),"point target");
        auto cascadeTarget=Take(CascadeShadowFrameBuffer::Create(256,256,5),"cascade target");
        const float splits[]{.5f,1.f,2.f,5.f,20.f};
        FrameSubmissionDesc desc{target,picking,pointTarget,cascadeTarget,resources->Pipelines(),splits,glm::radians(45.f),1,.1f,20,.1f,100};
        PickingArchitecture(*resources,desc,box,material);
        if(SDL_getenv("GENGINE_PICKING_MEASURE")) return;
        InstancingFixture(*resources,desc,box,sphere,material,parameters,textures);
        if(SDL_getenv("GENGINE_INSTANCE_MEASURE")) return;
        desc.pipelines=resources->Pipelines();
        desc.instancingEnabled=false; // Historical serial-path regression/reference.
        SortingFixture(root,*resources,desc,box,sphere,parameters,textures);
        if(SDL_getenv("GENGINE_DRAW_SORT_MEASURE")) return;
        UploadFixture(*resources,desc,box,sphere);
        desc.pipelines=resources->Pipelines();
        ShadowContactDepthFixture(*resources,desc,sphere,box,material);
        _Scene scene;
        const auto camera=Camera(scene);
        std::array<_Entity,3> bodies;
        const MeshHandle meshes[]{sphere,box,diamond};
        for (int i=0;i<3;++i)
        {
            bodies[i]=scene.CreateEntity(std::string("body-")+std::to_string(i));
            bodies[i].AddComponent<MeshRendererComponent>(MeshRendererComponent{meshes[i],material});
            bodies[i].GetComponent<Transform3DComponent>().Translation={float(i-1)*2,0,0};
        }
        Check(resources->AttachPhysicsShape(bodies[1],"Box"),"legacy physics-only geometry preserved");
        auto axisEntity=scene.CreateEntity("axis");
        axisEntity.AddComponent<MeshRendererComponent>(MeshRendererComponent{axis,helper,0,false,false,true});
        axisEntity.AddComponent<VisibilityComponent>(VisibilityComponent{false});
        auto sky=scene.CreateEntity("sky");
        sky.AddComponent<MeshRendererComponent>(MeshRendererComponent{skyMesh,skyMaterial,0,false,false,false});
        auto dir=scene.CreateEntity("directional");
        RenderLightComponent directional;directional.color={.7f,.7f,.7f};directional.castShadows=true;
        dir.AddComponent<RenderLightComponent>(directional);
        dir.GetComponent<Transform3DComponent>().QuatRotation=glm::rotation(glm::vec3(0,0,-1),-glm::normalize(glm::vec3(20,50,20)));
        auto point=scene.CreateEntity("point");
        RenderLightComponent pointLight;pointLight.kind=RenderLightKind::Point;pointLight.color={.8f,.2f,.1f};pointLight.range=100;pointLight.castShadows=true;
        point.AddComponent<RenderLightComponent>(pointLight);
        point.GetComponent<Transform3DComponent>().Translation={0,3,2};
        point.AddComponent<MeshRendererComponent>(MeshRendererComponent{helperMesh,pointMaterial,0,false,false,true});
        EntityPickTable picks;
        std::vector<std::byte> original;
        std::optional<RenderFrame> retained;
        {
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(scene,*resources,access,camera),"scene extraction with shared read scope");
            Check(frame.Draws().size()==5,"disabled helper omitted");
            Check(frame.DirectionalLights().size()==1 && frame.PointLights().size()==1,"typed frame lights");
            auto invalidDesc=desc;invalidDesc.cameraFov=0;
            auto invalid=submitter.Submit(frame,invalidDesc,picks);
            Check(!invalid && std::get<SubmissionCode>(invalid.error().cause)==SubmissionCode::InvalidShadowSettings,
                "invalid projection returns typed failure before drawing");
            FrameSubmissionStats stats;
            { UploadCalls::Observe uploads;stats=Take(submitter.Submit(frame,desc,picks),"submit complete frame"); }
            Check(stats.shadowDraws==6 && stats.pickDraws==4 && stats.colorDraws==3 && stats.helperDraws==1 && stats.skyDraws==1,"pass membership and one serial draw per item");
            original=Pixels(target);
            for(const auto& entry:frame.Resources()) if(entry.Material().Instance()==material)
            {
                const auto program=ShaderBackendAccess::Program(*entry.Material().Program());
                RenderBackend::PackedFrame gpu;
                glGetNamedBufferSubData(UploadCalls::lastFrameBuffer,0,sizeof(gpu),&gpu);
                Check(std::abs(gpu.directionalColor[0]-.7f)<1e-6f && std::abs(gpu.directionalColor[1]-.7f)<1e-6f,"typed directional color reaches driver");
                Check(gpu.lightPosition[0]==0 && gpu.lightPosition[1]==3 && gpu.lightPosition[2]==2,"typed point pose reaches driver");
                Check(gpu.planes[1]==100,"typed point range reaches shadow consumer");
                GLint blockBytes{};const auto block=glGetUniformBlockIndex(program,"GEngineFrame");
                glGetActiveUniformBlockiv(program,block,GL_UNIFORM_BLOCK_DATA_SIZE,&blockBytes);
                Check(blockBytes==sizeof(gpu),"driver std140 block byte count matches packed ABI");
                break;
            }
            int hits=0;
            for (int y=0;y<64;++y) for (int x=0;x<64;++x)
            {
                auto pixel=Take(picking.ReadPixel(x,y),"pick read");
                if (pixel>=0) {Check(scene.RenderData().ResolvePick(picks,pixel),"generation-safe picking");++hits;}
            }
            Check(hits>0,"pick image contains geometry");
            PickingParity(frame,scene,camera,picking,referenceGeometry,*pickShader);
            // This retained frame cannot observe subsequent authoring or removal.
            retained.emplace(std::move(frame));
        }
        const auto oldTransform=retained->Draws()[0].worldTransform;
        point.GetComponent<RenderLightComponent>().intensity=0;
        dir.GetComponent<RenderLightComponent>().intensity=0;
        {
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(scene,*resources,access,camera),"zero-intensity light extraction");
            auto stats=Take(submitter.Submit(frame,desc,picks),"zero-intensity lights submit ambient only");
            Check(frame.DirectionalLights().empty() && frame.PointLights().empty() && stats.shadowDraws==0 && stats.helperDraws==0,"disabled contribution and point helper");
            Check(Pixels(target)!=original,"typed light contribution affects image");
        }
        point.GetComponent<RenderLightComponent>().intensity=1;
        dir.GetComponent<RenderLightComponent>().intensity=1;
        std::optional<MaterialInstance> edited;
        {
            auto access=resources->Publication().BeginFrame();
            auto view=Take(resources->Materials().Acquire(access,material),"material authoring lease");
            edited.emplace(*view);
        }
        Check(edited->SetParameter("metalness",std::array<float,3>{.2f,.2f,.2f}),"material edit");
        {
            auto access=resources->Publication().BeginPublication();
            Check(resources->Materials().Replace(access,material,std::move(*edited)),"publish replacement using same handle");
        }
        edited.reset();
        {
            auto access=resources->Publication().BeginPublication();
            Check(!resources->Meshes().Close(access),"live frame prevents premature GPU retirement");
        }
        bodies[0].GetComponent<Transform3DComponent>().Translation.x+=3;
        axisEntity.GetComponent<VisibilityComponent>().enabled=true;
        {
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(scene,*resources,access,camera),"changed transform and helper visibility");
            Check(frame.Draws().size()==6,"helper appears after visibility edit");
            for(const auto& entry:frame.Resources()) if(entry.Material().Instance()==material)
                Check(entry.Material().PublicationRevision()==2,"new frame resolves replacement version");
            for(const auto& entry:retained->Resources()) if(entry.Material().Instance()==material)
                Check(entry.Material().PublicationRevision()==1,"old frame retains prior resource version");
            Check(retained->Draws()[0].worldTransform==oldTransform,"published transform immutable");
            auto stats=Take(submitter.Submit(frame,desc,picks),"submit edited scene");
            Check(stats.helperDraws==2,"editor helper visible");
            Check(Pixels(target)!=original,"presentation edit changes image");
        }
        scene.DestroyEntity(bodies[0]);scene.DestroyEntity(bodies[1]);scene.DestroyEntity(bodies[2]);
        scene.DestroyEntity(dir);scene.DestroyEntity(point);
        scene.DestroyEntity(sky);
        {
            auto access=resources->Publication().BeginFrame();
            auto stats=Take(submitter.Submit(*retained,desc,picks),"retained frame after ECS destruction");
            Check(stats.colorDraws==3 && Pixels(target)==original,"retained frame exact image independent of ECS");
            Check(!scene.RenderData().ResolvePick(picks,0),"destroyed entity picking is stale");
            auto frame=Take(Extract(scene,*resources,access,camera),"fresh frame after removal");
            Check(frame.DirectionalLights().empty() && frame.PointLights().empty(),"light removal published");
            auto emptyStats=Take(submitter.Submit(frame,desc,picks),"lightless helper frame");
            Check(emptyStats.colorDraws==0 && emptyStats.shadowDraws==0,"removed draw/light contribution");
        }
        auto replacement=scene.CreateEntity("replacement after destroy");
        replacement.AddComponent<MeshRendererComponent>(MeshRendererComponent{box,material});
        const auto replacementId=Take(scene.RenderData().Identify(replacement),"replacement generation");
        for (const auto& old:retained->Draws()) Check(old.entity!=replacementId,"recreated entity has new generation identity");
        {
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(scene,*resources,access,camera),"new renderable after destruction");
            Check(frame.Draws().size()==2,"new renderable and visible helper extracted");
        }
        auto spot=scene.CreateEntity("unsupported spot");
        RenderLightComponent spotLight;spotLight.kind=RenderLightKind::Spot;
        spot.AddComponent<RenderLightComponent>(spotLight);
        {
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(scene,*resources,access,camera),"typed spot extraction");
            auto result=submitter.Submit(frame,desc,picks);
            Check(!result && std::get<SubmissionCode>(result.error().cause)==SubmissionCode::UnsupportedLights,"unsupported light schema fails before submission");
        }
        // A click is consumed before BeginUI changes the mouse/viewport snapshot.
        // Exercise real integer pixels, this submission's table, and Entity Tags.
        {
            _Scene clickScene;
            const auto clickCamera=Camera(clickScene);
            auto a=clickScene.CreateEntity("Tag A"), b=clickScene.CreateEntity("Tag B");
            a.AddComponent<MeshRendererComponent>(MeshRendererComponent{box,material});
            b.AddComponent<MeshRendererComponent>(MeshRendererComponent{box,material});
            a.GetComponent<Transform3DComponent>().Translation={-2,0,0};
            b.GetComponent<Transform3DComponent>().Translation={2,0,0};
            const auto idA=Take(clickScene.RenderData().Identify(a),"pick A identity");
            const auto idB=Take(clickScene.RenderData().Identify(b),"pick B identity");
            EntityPickTable clickTable;
            RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager(),&target};
            FrameSceneInput input{clickScene,*resources,submitter,desc,clickTable,{&clickCamera,1}};
            struct Click {
                _Scene& scene; SceneRenderResources& resources;
                const MousePickFrameBuffer& target; EntityPickTable& table;
                ViewportPixelPosition position{}; EntityRenderId resolved{};
                std::string tag; int reads{},ui{},frame{}; bool fail=false;
            } click{clickScene,*resources,picking,clickTable};
            input.pickingReadback={&click,[](void* user)->ScheduleResult {
                auto& c=*static_cast<Click*>(user); ++c.reads;
                Check(ImGui::GetFrameCount()==c.frame,"click readback precedes BeginUI input advance");
                Check(GLContextThread::IsCurrentOwner(),"click readback stays on the owning context thread");
                Check(c.resources.Publication().CanPublish() && !c.scene.RenderData().IsExtracting(),
                    "click Entity/Tag lookup runs after extraction and frame access retire");
                if(c.fail) return std::unexpected(ScheduleError{FrameStage::Pass,ScheduleCode::InvalidInput});
                auto pixel=c.target.ReadPixel(c.position.X,c.position.Y);
                if(!pixel) return std::unexpected(ScheduleError{FrameStage::Pass,pixel.error()});
                c.resolved={}; c.tag="None";
                if(*pixel!=EntityPickTable::InvalidPixel) {
                    c.resolved=Take(c.scene.RenderData().ResolvePick(c.table,*pixel),"current-frame generation-safe click resolution");
                    auto entity=Take(c.scene.RenderData().Resolve(c.resolved),"current-frame clicked Entity");
                    c.tag=_Entity{entity,&c.scene}.GetName();
                }
                return {};
            }};
            context.editorUI={&click,[](void* user)->ScheduleResult {
                auto& c=*static_cast<Click*>(user); ++c.ui;
                Check(ImGui::GetFrameCount()==c.frame+1,"UI begins after picking readback");
                return {};
            }};
            auto request=[&](glm::vec3 world,EntityRenderId expected,const char* tag) {
                const auto clip=clickCamera.projection*clickCamera.view*glm::vec4(world,1);
                const auto ndc=glm::vec3(clip)/clip.w;
                // Offset logical panel and non-unit/nonuniform target scaling;
                // the public converter performs exactly one top-left -> GL Y flip.
                const float windowX=100+(ndc.x*.5f+.5f)*96;
                const float windowY=50+(1-(ndc.y*.5f+.5f))*48;
                auto pixel=ViewportPixelAt(windowX-100,windowY-50,{96,48},{64,64});
                Check(pixel.has_value(),"window -> local -> target picking coordinate");
                click.position=*pixel; click.frame=ImGui::GetFrameCount();
                input.targets.pickingEnabled=true;
                const auto before=click.reads;
                auto frame=Take(FrameScheduler::Render(context,&input),"scheduled click request");
                Check(click.reads==before+1,"scheduler fulfills each picking request exactly once");
                const auto& decision=frame.submission.decisions[2];
                Check(decision.requested && frame.submission.pickDraws==(decision.executed?2:0),"request is independent of dirty picking work");
                Check(std::any_of(frame.trace.Events().begin(),frame.trace.Events().end(),[](auto event) {
                    return event.stage==FrameStage::Pass && event.pass==RenderPass::Picking;
                })==decision.executed,"only executed picking appears in the pass trace");
                if(before>0) Check(!decision.executed && decision.reasons==PassDirtyReason::None,"repeated readbacks reuse unchanged picking image");
                Check(click.resolved==expected && click.tag==tag,"clicked Entity and Tag match, never the previous result");
                std::println("[PASS] scheduled click ({},{}) -> {}",pixel->X,pixel->Y,click.tag);
            };
            request({-2,0,0},idA,"Tag A");
            request({2,0,0},idB,"Tag B");
            request({0,3,0},{},"None");
            request({-2,0,0},idA,"Tag A");
            auto topLeft=ViewportPixelAt(0,0,{96,48},{64,64});
            auto bottomRight=ViewportPixelAt(95.9f,47.9f,{96,48},{64,64});
            Check(topLeft && topLeft->X==0 && topLeft->Y==63 && bottomRight && bottomRight->X==63 && bottomRight->Y==0,
                "picking coordinate corners preserve framebuffer Y orientation");
            Check(!ViewportPixelAt(-1,0,{96,48},{64,64}) && !ViewportPixelAt(96,0,{96,48},{64,64}),"outside panel does not request a pixel");
            const auto reads=click.reads;
            input.targets.pickingEnabled=false; click.frame=ImGui::GetFrameCount();
            auto skipped=Take(FrameScheduler::Render(context,&input),"no click request");
            Check(click.reads==reads && skipped.submission.pickDraws==0,"no readback or Picking work without a request");
            input.targets.pickingEnabled=true; context.visible=false; click.frame=ImGui::GetFrameCount();
            Take(FrameScheduler::Render(context,&input),"hidden viewport click");
            Check(click.reads==reads,"hidden viewport cannot consume an old picking image");
            context.visible=true; click.fail=true; click.frame=ImGui::GetFrameCount();
            const auto ui=click.ui;
            auto failed=FrameScheduler::Render(context,&input);
            Check(!failed && std::get<ScheduleCode>(failed.error().cause)==ScheduleCode::InvalidInput && click.ui==ui,
                "typed readback failure is returned before UI without leaking active frame state");
            click.fail=false;
            request({2,0,0},idB,"Tag B");
        }
        // Phase 46 exercises the production scheduler, not a stand-in order list.
        {
            _Scene scheduledScene;
            const auto scheduledCamera=Camera(scheduledScene);
            auto skyEntity=scheduledScene.CreateEntity("scheduled sky");
            skyEntity.AddComponent<MeshRendererComponent>(MeshRendererComponent{skyMesh,skyMaterial,0,false,false,false});
            auto surface=scheduledScene.CreateEntity("scheduled surface");
            const auto scheduledOpaque=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures}),"matched opaque fixture");
            surface.AddComponent<MeshRendererComponent>(MeshRendererComponent{sphere,scheduledOpaque});
            surface.AddComponent<VisibilityComponent>(VisibilityComponent{false});
            EntityPickTable scheduledPicks;
            RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager(),&target};
            FrameSceneInput input{scheduledScene,*resources,submitter,desc,scheduledPicks,{&scheduledCamera,1}};
            struct Callbacks { SceneRenderResources* resources; _Scene* scene; int updates{},ui{}; bool fail=false; } callbacks{resources.get(),&scheduledScene};
            context.updateResources={&callbacks,[](void* user)->ScheduleResult {
                auto& c=*static_cast<Callbacks*>(user); ++c.updates;
                Check(c.resources->Publication().CanPublish() && !c.scene->RenderData().IsExtracting(),"update runs before resource/ECS freeze");
                if(c.fail) return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources,ScheduleCode::InvalidInput});
                return {};
            }};
            context.editorUI={&callbacks,[](void* user)->ScheduleResult {
                auto& c=*static_cast<Callbacks*>(user); ++c.ui;
                Check(c.resources->Publication().CanPublish() && !c.scene->RenderData().IsExtracting(),"UI starts after frame access and ECS freeze release");
                Check(!glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_STENCIL_TEST) && !glIsEnabled(GL_RASTERIZER_DISCARD),"external UI receives established state");
                return {};
            }};
            auto scheduled=Take(FrameScheduler::Render(context,&input),"scheduled sky frame");
            Check(callbacks.updates==1 && callbacks.ui==1,"one update and UI callback");
            auto hasPass=[](const FrameTrace& trace,RenderPass pass) {
                return std::any_of(trace.Events().begin(),trace.Events().end(),[&](auto event){return event.stage==FrameStage::Pass && event.pass==pass;});
            };
            Check(scheduled.trace.events[0].stage==FrameStage::BeginFrame
                && scheduled.trace.events[1].stage==FrameStage::UpdateFrameResources
                && scheduled.trace.events[2].stage==FrameStage::FreezeFrameInputs
                && scheduled.trace.events[3].stage==FrameStage::BuildRenderFrame,"recorded frame stages");
            const RenderPass ordered[]{RenderPass::Picking,RenderPass::Opaque,RenderPass::Masked,RenderPass::Skybox,
                RenderPass::Transparent,RenderPass::Debug,RenderPass::Resolve,RenderPass::EditorUI,RenderPass::Present};
            Check(scheduled.trace.count==std::size(ordered)+4,"recorded active pass count");
            for(std::size_t i=0;i<std::size(ordered);++i) Check(scheduled.trace.events[i+4].pass==ordered[i],"recorded renderer-owned order");
            auto background=Pixels(target);
            surface.GetComponent<VisibilityComponent>().enabled=true;
            auto opaque=Take(FrameScheduler::Render(context,&input),"opaque over sky");
            auto solid=Pixels(target);
            Check(opaque.submission.opaqueDraws==1 && opaque.submission.skyDraws==1,"opaque category and sky");
            // Publish a new material during UpdateFrameResources. This grows the
            // pipeline role vector and must be visible in this very frame.
            struct Publish { SceneRenderResources* resources; _Entity* entity; std::span<const MaterialParameterDecl> parameters;
                std::span<const MaterialTextureAssignment> textures; MaterialInstanceHandle material; } publish{resources.get(),&surface,parameters,textures};
            context.updateResources={&publish,[](void* user)->ScheduleResult {
                auto& p=*static_cast<Publish*>(user);
                auto created=p.resources->PublishMaterial({SceneMaterialKind::Lit,p.parameters,p.textures,false,1,AlphaMode::Transparent,.5f,.5f});
                if(!created) return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources,created.error()});
                p.material=*created;
                p.entity->GetComponent<MeshRendererComponent>().material=*created;
                return {};
            }};
            auto alpha=Take(FrameScheduler::Render(context,&input),"same-frame synchronous material publication");
            Check(alpha.submission.transparentDraws==1 && alpha.submission.opaqueDraws==0,"new replacement visible before extraction");
            Check(alpha.submission.decisions[2].executed && HasDirtyReason(alpha.submission.decisions[2].reasons,PassDirtyReason::Material),
                "new material/template/pipeline/program invalidates picking");
            auto composed=Pixels(target);
            const auto saveImage=[](const char* path,const std::vector<std::byte>& pixels) {
                std::ofstream output(path,std::ios::binary); output << "P6\n64 64\n255\n";
                for(int y=63;y>=0;--y) for(int x=0;x<64;++x) output.write(reinterpret_cast<const char*>(pixels.data()+(y*64+x)*4),3);
                Check(bool(output),"write alpha comparison evidence");
            };
            saveImage("sky-background.ppm",background);saveImage("opaque-over-sky.ppm",solid);saveImage("transparent-over-sky.ppm",composed);
            int blendedPixels{};
            for(std::size_t i=0;i<solid.size();i+=4) {
                bool differs=false;
                for(std::size_t c=0;c<3;++c) if(std::abs(int(solid[i+c])-int(background[i+c]))>12) differs=true;
                if(!differs) continue;
                ++blendedPixels;
                for(std::size_t c=0;c<3;++c)
                    Check(std::abs(int(composed[i+c])-(int(solid[i+c])+int(background[i+c]))*.5f)<=2.5f,"transparent fragment composes over established sky");
            }
            Check(blendedPixels>20,"transparent-over-sky fixture covers visible fragments");
            context.updateResources={&callbacks,[](void* user)->ScheduleResult {
                auto& c=*static_cast<Callbacks*>(user); ++c.updates;
                Check(c.resources->Publication().CanPublish(),"publication safe point remains available");
                if(c.fail) return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources,ScheduleCode::InvalidInput});
                return {};
            }};
            input.extraction.tasks.workers=2; input.extraction.parallelThreshold=0;
            auto parallel=Take(FrameScheduler::Render(context,&input),"scheduler optional frozen parallel extraction");
            Check(parallel.extraction.tasks.lanes==2 && Pixels(target)==composed,"serial/parallel scheduler frame parity");
            input.extraction={};
            // Previous external clients may leave hostile state. Every pass must
            // establish its declared state before clears or draws.
            glEnable(GL_SCISSOR_TEST);glScissor(0,0,0,0);glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_NEVER,0,~0u);glDepthMask(GL_FALSE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
            glEnable(GL_BLEND);glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);glCullFace(GL_FRONT);glFrontFace(GL_CW);
            glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);glEnable(GL_RASTERIZER_DISCARD);
            glDepthRange(1,0);glClearDepth(0);glEnable(GL_SAMPLE_COVERAGE);glSampleCoverage(0,GL_FALSE);
            Take(FrameScheduler::Render(context,&input),"state contamination recovery");
            Check(Pixels(target)==composed,"pass state independent of preceding external client");
            const auto masked=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,AlphaMode::Masked,.5f,.25f}),"masked material");
            surface.GetComponent<MeshRendererComponent>().material=masked;
            auto maskFrame=Take(FrameScheduler::Render(context,&input),"masked color and picking");
            Check(maskFrame.submission.maskedDraws==1 && Pixels(target)==background,"masked category discards below-cutoff color");
            for(int y=0;y<64;++y) for(int x=0;x<64;++x) Check(Take(picking.ReadPixel(x,y),"masked picking read")==-1,"masked picking discards same coverage");
            auto sun=scheduledScene.CreateEntity("scheduled directional");
            sun.AddComponent<RenderLightComponent>(directional);
            sun.GetComponent<Transform3DComponent>().QuatRotation=glm::rotation(glm::vec3(0,0,-1),-glm::normalize(glm::vec3(20,50,20)));
            auto bulb=scheduledScene.CreateEntity("scheduled point");bulb.AddComponent<RenderLightComponent>(pointLight);
            bulb.GetComponent<Transform3DComponent>().Translation={0,3,2};
            auto shadowed=Take(FrameScheduler::Render(context,&input),"masked directional and point passes");
            if(root.MainWindow()->Timings().Enabled()) {
                const auto timings=root.MainWindow()->Timings().Current();
                std::size_t shadowCount{},colorCount{},pickCount{};
                for(const auto& timing:timings) {
                    Check(timing.completed && PassLabel(timing.pass)!="unknown","scheduled pass timing labels and success");
                    if(timing.pass==RenderPass::DirectionalShadow || timing.pass==RenderPass::PointShadow) shadowCount+=timing.submittedDraws;
                    if(timing.pass==RenderPass::Picking) pickCount+=timing.submittedDraws;
                    if(timing.pass==RenderPass::Opaque || timing.pass==RenderPass::Masked || timing.pass==RenderPass::Transparent) colorCount+=timing.submittedDraws;
                }
                Check(shadowCount==shadowed.submission.shadowDraws && colorCount==shadowed.submission.colorDraws
                    && pickCount==shadowed.submission.pickDraws,"pass timing counts match actual production submission");
            }
            Check(shadowed.trace.events[4].pass==RenderPass::DirectionalShadow && shadowed.trace.events[5].pass==RenderPass::PointShadow,"directional precedes point shadow");
            std::vector<float> depth(256*256*cascadeTarget.Buffer().Description().Layers);
            Check(TextureView(Take(cascadeTarget.DepthView(),"cascade view")).Bind(0),"cascade readback bind");
            glGetTexImage(GL_TEXTURE_2D_ARRAY,0,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
            Check(std::all_of(depth.begin(),depth.end(),[](float v){return v==1.f;}),"masked cascade coverage matches color");
            const auto pointDepthIsClear=[&] {
                Check(TextureView(Take(pointTarget.DepthView(),"point view")).Bind(0),"point readback bind");
                GLint cube{},previousRead{};glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&cube);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);
                GLuint reader{};glGenFramebuffers(1,&reader);glBindFramebuffer(GL_READ_FRAMEBUFFER,reader);
                depth.resize(256*256);
                bool allClear=true;
                // Read each attached face as depth. Raw cube texture downloads
                // on this maintained driver returned undefined data for a cleared
                // face; attachment readback covers the actual depth image.
                for(int face=0;face<6;++face) {
                    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,cube,0);
                    glReadBuffer(GL_NONE);
                    Check(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"point face reader complete");
                    std::fill(depth.begin(),depth.end(),-5.f);
                    glReadPixels(0,0,256,256,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
                    Check(glGetError()==GL_NO_ERROR,"point depth readback has no driver error");
                    allClear &= std::all_of(depth.begin(),depth.end(),[](float v){return v==1.f;});
                }
                glBindFramebuffer(GL_READ_FRAMEBUFFER,previousRead);glDeleteFramebuffers(1,&reader);
                return allClear;
            };
            Check(pointDepthIsClear(),"masked point coverage matches color on all six faces");
            const auto visibleMasked=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,AlphaMode::Masked,.5f,.75f}),"visible mask positive control");
            surface.GetComponent<MeshRendererComponent>().material=visibleMasked;
            Take(FrameScheduler::Render(context,&input),"above-cutoff masked frame");
            Check(!pointDepthIsClear(),"above-cutoff positive control writes point depth");
            Check(Pixels(target)!=background,"above-cutoff masked color is visible");
            // Required targets fail before any pass, preserving the last image.
            auto smallPick=Take(MousePickFrameBuffer::Create(16,16),"incompatible pick target");
            FrameSubmissionDesc bad{target,smallPick,pointTarget,cascadeTarget,resources->Pipelines(),splits,glm::radians(45.f),1,.1f,20,.1f,100};
            FrameSceneInput badInput{scheduledScene,*resources,submitter,bad,scheduledPicks,{&scheduledCamera,1}};
            auto beforeFailure=Pixels(target);
            auto failed=FrameScheduler::Render(context,&badInput);
            Check(!failed && std::get<SubmissionCode>(std::get<SubmissionError>(failed.error().cause).cause)==SubmissionCode::InvalidTarget,"required picking target failure is typed");
            Check(Pixels(target)==beforeFailure && resources->Publication().CanPublish() && !scheduledScene.RenderData().IsExtracting(),"failed frame releases freeze and preserves image");
            auto missingShadow=Take(CascadeShadowFrameBuffer::Create(0,0,5),"deferred shadow target");
            FrameSubmissionDesc noShadow{target,picking,pointTarget,missingShadow,resources->Pipelines(),splits,glm::radians(45.f),1,.1f,20,.1f,100};
            FrameSceneInput noShadowInput{scheduledScene,*resources,submitter,noShadow,scheduledPicks,{&scheduledCamera,1}};
            auto shadowFailure=FrameScheduler::Render(context,&noShadowInput);
            Check(!shadowFailure && std::get<SubmissionCode>(std::get<SubmissionError>(shadowFailure.error().cause).cause)==SubmissionCode::InvalidTarget,"active directional shadow requires a live target");
            badInput.targets.pickingEnabled=false;
            auto skipped=Take(FrameScheduler::Render(context,&badInput),"optional picking skips unusable target");
            Check(!hasPass(skipped.trace,RenderPass::Picking) && skipped.submission.pickDraws==0,"optional pass omitted from actual order");
            Check(!scheduledScene.RenderData().ResolvePick(scheduledPicks,0),"skipped picking invalidates old lookup");
            callbacks.fail=true; const int priorUI=callbacks.ui;
            auto updateFailure=FrameScheduler::Render(context,&input);
            Check(!updateFailure && updateFailure.error().stage==FrameStage::UpdateFrameResources && callbacks.ui==priorUI,"update failure stops before extraction/UI/present");
            callbacks.fail=false;
            {
                auto held=resources->Publication().BeginFrame();
                const auto priorUpdates=callbacks.updates;
                auto busy=FrameScheduler::Render(context,&input);
                Check(!busy && std::get<ScheduleCode>(busy.error().cause)==ScheduleCode::FrameActive
                    && callbacks.updates==priorUpdates,"active resource frame rejects before update callback");
            }
            auto updateCallback=context.updateResources;
            context.updateResources={&context,[](void* user)->ScheduleResult {
                auto nested=FrameScheduler::Render(*static_cast<RenderContext*>(user));
                Check(!nested && std::get<ScheduleCode>(nested.error().cause)==ScheduleCode::FrameActive,"reentrant scheduler returns typed error");
                return {};
            }};
            Take(FrameScheduler::Render(context,&input),"outer frame survives rejected reentry");
            context.updateResources=updateCallback;
            auto invalidCamera=scheduledCamera; invalidCamera.viewportWidth=32;
            auto oldCameras=input.cameras; input.cameras={&invalidCamera,1};
            auto cameraFailure=FrameScheduler::Render(context,&input);
            Check(!cameraFailure && resources->Publication().CanPublish(),"invalid viewport returns typed failure and releases frame");
            input.cameras=oldCameras;
            bool threadRejected=false;
            std::thread wrongThread([&] { auto result=FrameScheduler::Render(context,&input);
                threadRejected=!result && std::get<ScheduleCode>(result.error().cause)==ScheduleCode::Context; });
            wrongThread.join(); Check(threadRejected,"scheduler rejects worker without issuing GL");
            context.visible=false;
            auto hidden=Take(FrameScheduler::Render(context,&input),"collapsed viewport preserves UI lifecycle");
            Check(!hasPass(hidden.trace,RenderPass::Opaque) && hasPass(hidden.trace,RenderPass::EditorUI) && hasPass(hidden.trace,RenderPass::Present),"collapsed optional scene skip");
            context.visible=true;
            scheduledScene.DestroyEntity(surface);scheduledScene.DestroyEntity(skyEntity);
            scheduledScene.DestroyEntity(sun);scheduledScene.DestroyEntity(bulb);
            input.targets.pickingEnabled=false;
            auto empty=Take(FrameScheduler::Render(context,&input),"empty scene scheduler");
            Check(empty.extraction.draws==0 && empty.submission.colorDraws==0 && empty.submission.shadowDraws==0
                && !hasPass(empty.trace,RenderPass::DirectionalShadow) && !hasPass(empty.trace,RenderPass::PointShadow),"empty scene clears and skips optional shadow work");
            Take(FrameScheduler::Render(context,&noShadowInput),"inactive shadow needs no live target");
            Check(target.SetSamples(4),"multisample target configuration");
            auto resolved=Take(FrameScheduler::Render(context,&input),"scheduled multisample resolve");
            Check(hasPass(resolved.trace,RenderPass::Resolve) && Pixels(target).size()==64*64*4,"resolved output is readable after scheduled resolve");
            Check(target.SetSamples(1),"restore single sample target");
            for(const auto& event:shadowed.trace.Events()) if(event.stage==FrameStage::Pass) {
                Check(!event.contract.scissor && !event.contract.stencilWrite,"declared scissor and stencil policy");
                if(event.pass==RenderPass::Transparent) Check(event.contract.blend && !event.contract.depthWrite,"transparent state declaration");
                if(event.pass==RenderPass::Skybox) Check(!event.contract.depthWrite,"sky retains scene depth");
            }
            Check(root.MainWindow()->IsCurrent(),"scheduler restores owning context");
            std::println("[PASS] frame-scheduler order/skip/targets/publication/freeze/failure/alpha/state/empty checks={}",checks);
        }
        Invalidation(root,*resources,desc,box,material);
        // Dropping CPU leases on a worker cannot perform GPU retirement.
        std::thread release([frame=std::move(retained)]() mutable {frame.reset();});release.join();
        Check(glGetError()==GL_NO_ERROR,"no driver errors");
        std::println("[PASS] frame-submission checks={} (owner/resource destruction follows)",checks);
    }
}
int main()
{
    OrderingTests();
    RuntimeAssets::Initialize("RigidBodySimulation");
    EngineContext root;
    Check(!root.SceneServices(),"uninitialized service typed failure");
    WindowProperties properties;properties.m_WinPos=winProp.m_WinPos;properties.m_Title="Phase 53 hidden submission validation";
    properties.flag={WindowFlags::INVISIBLE};properties.m_Width=properties.m_Height=64;
    properties.m_MinWidth=properties.m_MinHeight=64;properties.m_IsVsync=false;
    Check(root.Initialize({properties}),"owner root initialization");
    WindowPlacementTests(root,properties);
    if(!SDL_getenv("GENGINE_STATE_CACHE_MEASURE")) StateCacheTests();
    TimingTests(root);
    Run(root);
    std::println("[PASS] frame-submission-retirement");
}
#endif
