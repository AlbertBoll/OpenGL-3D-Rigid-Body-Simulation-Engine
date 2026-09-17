#include "gepch.h"
#include "Core/RenderTarget.h"
#include "Core/BaseApp.h"
#include "Windows/SDLWindow.h"
#include "../GEngine/src/Core/FramebufferBackend.h"
#include "../GEngine/src/Assets/TextureBackend.h"
#include <new>
#include <print>

static int denyOwnerAfter = -1;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if (denyOwnerAfter == 0) { denyOwnerAfter = -1; return nullptr; }
    if (denyOwnerAfter > 0) --denyOwnerAfter;
    return std::malloc(size ? size : 1);
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace
{
    using namespace GEngine;
    using FB = FramebufferDetail::Backend;
    using Format = FramebufferFormat;
    unsigned checks = 0, errors = 0;
    template<class T> void Check(const T& value, const char* message)
    { ++checks; if (!static_cast<bool>(value)) { std::println(stderr, "[FAIL] {}", message); std::exit(1); } }
    void APIENTRY Diagnostic(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
    { if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) { ++errors; std::println(stderr, "[GL] {}", message); } }
    enum class Fault { None, Framebuffer, Texture, Renderbuffer, Image2D, Image3D, ImageMS, RboStorage, Completeness };
    struct Observer
    {
        inline static Fault fault = Fault::None;
        inline static PFNGLGENFRAMEBUFFERSPROC genF; inline static PFNGLDELETEFRAMEBUFFERSPROC delF;
        inline static PFNGLGENTEXTURESPROC genT; inline static PFNGLDELETETEXTURESPROC delT;
        inline static PFNGLGENRENDERBUFFERSPROC genR; inline static PFNGLDELETERENDERBUFFERSPROC delR;
        inline static PFNGLTEXIMAGE2DPROC image2; inline static PFNGLTEXIMAGE3DPROC image3;
        inline static PFNGLTEXIMAGE2DMULTISAMPLEPROC imageMS;
        inline static PFNGLRENDERBUFFERSTORAGEPROC rbo; inline static PFNGLCHECKFRAMEBUFFERSTATUSPROC status;
        inline static std::array<std::unordered_set<GLuint>, 3> live;
        inline static unsigned created = 0, deleted = 0;
        inline static SDL_GLContext context;
        inline static std::thread::id thread;
        static bool Fail(Fault f) { if (fault != f) return false; fault = Fault::None; return true; }
        static void Generated(unsigned kind, GLsizei count, const GLuint* names)
        { for (int i = 0; i < count; ++i) { Check(names[i] && live[kind].insert(names[i]).second, "Duplicate generated name"); ++created; } }
        static void Deleted(unsigned kind, GLsizei count, const GLuint* names)
        {
            Check(context == SDL_GL_GetCurrentContext() && thread == std::this_thread::get_id(), "Wrong deletion context/thread");
            for (int i = 0; i < count; ++i) { Check(names[i] && live[kind].erase(names[i]) == 1, "Double or unowned deletion"); ++deleted; }
        }
        static void APIENTRY GenF(GLsizei n, GLuint* p) { if (Fail(Fault::Framebuffer)) { std::fill_n(p,n,0); return; } genF(n,p); Generated(0,n,p); }
        static void APIENTRY GenT(GLsizei n, GLuint* p) { if (Fail(Fault::Texture)) { std::fill_n(p,n,0); return; } genT(n,p); Generated(1,n,p); }
        static void APIENTRY GenR(GLsizei n, GLuint* p) { if (Fail(Fault::Renderbuffer)) { std::fill_n(p,n,0); return; } genR(n,p); Generated(2,n,p); }
        static void APIENTRY DelF(GLsizei n, const GLuint* p) { Deleted(0,n,p); delF(n,p); }
        static void APIENTRY DelT(GLsizei n, const GLuint* p) { Deleted(1,n,p); delT(n,p); }
        static void APIENTRY DelR(GLsizei n, const GLuint* p) { Deleted(2,n,p); delR(n,p); }
        static void APIENTRY Image2(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void* i)
        { if (!Fail(Fault::Image2D)) image2(a,b,c,d,e,f,g,h,i); }
        static void APIENTRY Image3(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLsizei f, GLint g, GLenum h, GLenum i, const void* j)
        { if (!Fail(Fault::Image3D)) image3(a,b,c,d,e,f,g,h,i,j); }
        static void APIENTRY ImageMS(GLenum a, GLsizei b, GLenum c, GLsizei d, GLsizei e, GLboolean f)
        { if (!Fail(Fault::ImageMS)) imageMS(a,b,c,d,e,f); }
        static void APIENTRY Rbo(GLenum a, GLenum b, GLsizei c, GLsizei d) { if (!Fail(Fault::RboStorage)) rbo(a,b,c,d); }
        static GLenum APIENTRY Status(GLenum target)
        { return Fail(Fault::Completeness) ? GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT : status(target); }
        Observer()
        {
            context = SDL_GL_GetCurrentContext(); thread = std::this_thread::get_id(); created = deleted = 0;
            for (auto& names : live) names.clear();
            genF=glad_glGenFramebuffers; glad_glGenFramebuffers=GenF; delF=glad_glDeleteFramebuffers; glad_glDeleteFramebuffers=DelF;
            genT=glad_glGenTextures; glad_glGenTextures=GenT; delT=glad_glDeleteTextures; glad_glDeleteTextures=DelT;
            genR=glad_glGenRenderbuffers; glad_glGenRenderbuffers=GenR; delR=glad_glDeleteRenderbuffers; glad_glDeleteRenderbuffers=DelR;
            image2=glad_glTexImage2D; glad_glTexImage2D=Image2; image3=glad_glTexImage3D; glad_glTexImage3D=Image3;
            imageMS=glad_glTexImage2DMultisample; glad_glTexImage2DMultisample=ImageMS;
            rbo=glad_glRenderbufferStorage; glad_glRenderbufferStorage=Rbo; status=glad_glCheckFramebufferStatus; glad_glCheckFramebufferStatus=Status;
        }
        ~Observer()
        {
            Check(created==deleted && live[0].empty() && live[1].empty() && live[2].empty(), "Framebuffer storage leak");
            glad_glGenFramebuffers=genF; glad_glDeleteFramebuffers=delF; glad_glGenTextures=genT; glad_glDeleteTextures=delT;
            glad_glGenRenderbuffers=genR; glad_glDeleteRenderbuffers=delR; glad_glTexImage2D=image2; glad_glTexImage3D=image3;
            glad_glTexImage2DMultisample=imageMS; glad_glRenderbufferStorage=rbo; glad_glCheckFramebufferStatus=status;
            std::println("[PASS] framebuffer ownership created={} deleted={}",created,deleted);
        }
    };
    FrameBufferSpecification Description(unsigned w=13,unsigned h=29,unsigned samples=1)
    {
        FrameBufferSpecification d; d.Width=w; d.Height=h; d.Samples=samples;
        d.Colors[0]=Format::RGBA8; d.Colors[1]=Format::RedInteger; d.ColorCount=2; d.Depth=Format::Depth24; return d;
    }
    void Complete(const FrameBuffer& b)
    { b.Bind(); Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Incomplete target"); }
    void Dimensions(const FrameBuffer& b)
    {
        const auto& d=b.Description(); Complete(b);
        for(unsigned i=0;i<d.ColorCount;++i)
        {
            GLint w=0,h=0; glGetTextureLevelParameteriv(FB::Color(b,i),0,GL_TEXTURE_WIDTH,&w); glGetTextureLevelParameteriv(FB::Color(b,i),0,GL_TEXTURE_HEIGHT,&h);
            Check(w==int(d.Width)&&h==int(d.Height),"Width/height mismatch");
            GLint draw=0;glGetIntegerv(GL_DRAW_BUFFER0+i,&draw);Check(draw==GLint(GL_COLOR_ATTACHMENT0+i),"Missing attached draw buffer");
        }
        GLint next=0;glGetIntegerv(GL_DRAW_BUFFER0+d.ColorCount,&next);Check(next==GL_NONE,"Unattached draw buffer enabled");
        if(!d.ColorCount){GLint read=0;glGetIntegerv(GL_READ_BUFFER,&read);Check(read==GL_NONE,"Depth target enabled color reads");}
    }
    void OwnershipAndFailure()
    {
        static_assert(!std::is_copy_constructible_v<FrameBuffer> && std::is_nothrow_move_constructible_v<FrameBuffer> && std::is_nothrow_move_assignable_v<FrameBuffer>);
        static_assert(!std::is_copy_constructible_v<PointShadowFrameBuffer> && std::is_nothrow_move_constructible_v<MousePickFrameBuffer>);
        auto d=Description();
        {
            auto a=FrameBuffer::Create(d).value(); auto b=FrameBuffer::Create(Description(17,17)).value();
            const auto first=FB::Name(a), retired=FB::Name(b);Check(a.ClearInteger(1,173),"Integer clear");
            b=std::move(a);Check(!a && !a.Description().Width && !a.Description().Samples && FB::Name(b)==first && !glIsFramebuffer(retired),"Occupied move");
            b=std::move(b);Check(b.ReadInteger(1,12,28).value()==173,"Move lost pixels");
            std::vector<FrameBuffer> owners;owners.push_back(std::move(b));owners.reserve(20);Check(!b&&FB::Name(owners[0])==first,"Relocation ownership");
            const auto before=Observer::live;owners[0].Bind();
            for(auto fault:{Fault::Framebuffer,Fault::Texture,Fault::Image2D,Fault::Completeness})
            {
                Observer::fault=fault;auto failed=owners[0].Resize(21,37);Check(!failed,"Injected failure accepted");
                Check(failed.error().code==(fault==Fault::Completeness?FramebufferErrorCode::Incomplete:fault==Fault::Image2D?FramebufferErrorCode::Storage:FramebufferErrorCode::Allocation),"Incorrect typed failure");
                GLint binding=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&binding);
                Check(Observer::live==before&&FB::Name(owners[0])==first&&binding==GLint(first)&&owners[0].ReadInteger(1,12,28).value()==173,"Failed replacement changed resource/state");
            }
            denyOwnerAfter=0;auto failed=FrameBuffer::Create(d);Check(!failed&&failed.error().code==FramebufferErrorCode::Allocation,"CPU allocation failure");
            Check(Observer::live==before,"CPU failure leaked");
            auto view=owners[0].ColorView(1).value(); auto old=FB::Color(owners[0],1);
            Check(owners[0].Resize(31,7),"Successful resize");
            Check(!glIsTexture(old)&&Asset::AssetDetail::TextureBackend::Name(Asset::TextureView(view)).value()==FB::Color(owners[0],1),"View cached obsolete storage");
            view={};Check(glIsTexture(FB::Color(owners[0],1)),"Non-owning view deleted storage");
        }
        for(auto fault:{Fault::Renderbuffer,Fault::RboStorage,Fault::ImageMS,Fault::Image3D})
        {
            auto desc=Description();desc.DepthRenderbuffer=true;
            if(fault==Fault::ImageMS)desc.Samples=4;
            if(fault==Fault::Image3D){desc.Kind=FramebufferKind::Array;desc.ColorCount=0;desc.Colors={};desc.Layers=3;desc.DepthRenderbuffer=false;}
            const auto before=Observer::live;Observer::fault=fault;auto result=FrameBuffer::Create(desc);
            Check(!result && Observer::fault==Fault::None && Observer::live==before,"Partial attachment failure leaked or was not reached");
            Check(FrameBuffer::Create(desc),"Recovery after attachment failure");
        }
        for(unsigned which=0;which<10;++which)
        {
            auto bad=d;
            if(which==0)bad.Width=0;if(which==1)bad.Height=UINT32_MAX;if(which==2)bad.Samples=0;
            if(which==3)bad.ColorCount=5;if(which==4)bad.Colors[0]=Format::Depth24;
            if(which==5)bad.Kind=static_cast<FramebufferKind>(99);if(which==6)bad.Layers=0;
            if(which==7)bad.Depth=static_cast<Format>(99);if(which==8)bad.Samples=3;
            if(which==9){bad.ColorCount=0;bad.Colors={};bad.Depth=Format::None;}
            const auto before=Observer::live;Check(!FrameBuffer::Create(bad)&&Observer::live==before,"Invalid descriptor allocated resources");
        }
        bool unavailable=false;std::thread worker([&]{auto result=FrameBuffer::Create(d);unavailable=!result&&result.error().code==FramebufferErrorCode::ContextUnavailable;});worker.join();Check(unavailable,"Worker creation forwarded to driver");
        std::println("[PASS] moves, views, descriptor/allocation/completeness failures and rollback");
    }
    GLuint Shader(GLenum stage,const char* source)
    { auto shader=glCreateShader(stage);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);Check(ok,"Probe shader compilation");return shader; }
    void IntegerSampling(const FrameBuffer& source)
    {
        auto output=FrameBuffer::Create([]{auto d=Description(2,2);d.ColorCount=1;d.Colors[1]=Format::None;return d;}()).value();
        auto vs=Shader(GL_VERTEX_SHADER,"#version 450 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}");
        auto fs=Shader(GL_FRAGMENT_SHADER,"#version 450 core\nlayout(binding=0) uniform isampler2D image;layout(location=0) out vec4 color;void main(){color=texture(image,vec2(.5)).r==173?vec4(0,1,0,1):vec4(1,0,0,1);}");
        auto program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);Check(ok,"Probe program linking");
        GLuint vao=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glUseProgram(program);
        Check(Asset::TextureView(source.ColorView(1).value()).Bind(0),"Integer attachment bind");
        output.Bind();glViewport(0,0,2,2);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDrawArrays(GL_TRIANGLES,0,3);
        std::array<std::byte,16> pixels{};Check(output.ReadColor(0,pixels),"Integer sampling readback");
        Check(pixels[0]==std::byte{0}&&pixels[1]==std::byte{255}&&pixels[2]==std::byte{0},"Integer texture incomplete or wrong filter/target");
        glUseProgram(0);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);glDeleteVertexArrays(1,&vao);
    }
    void PixelsAndTargets()
    {
        for(auto size:{std::pair{17u,17u},std::pair{13u,29u},std::pair{31u,7u}})
        {
            auto final=FinalFrameBuffer::Create(size.first,size.second).value();Dimensions(final.Buffer());
            auto pick=MousePickFrameBuffer::Create(size.first,size.second).value();Dimensions(pick.Buffer());
            glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);glColorMaski(0,GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
            Check(final.ClearMousePickAttachment(173)&&pick.ClearAttachment(0,51),"Picking clear");
            GLboolean mask[4]{};glGetBooleani_v(GL_COLOR_WRITEMASK,0,mask);
            Check(glIsEnabled(GL_SCISSOR_TEST)&&!mask[0],"Clear did not restore scissor/write mask");
            glDisable(GL_SCISSOR_TEST);glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            Check(final.ReadPixel(int(size.first)-1,int(size.second)-1).value()==173&&pick.ReadPixel(0,int(size.second)-1).value()==51,"Rectangular integer read or wrong attachment");
            Check(!final.ReadPixel(int(size.first),0)&&!final.ReadPixel(0,-1)&&!pick.ClearAttachment(1,0),"Invalid read/attachment accepted");
            GLint filter=0;glGetTextureParameteriv(FB::Color(final.Buffer(),1),GL_TEXTURE_MAG_FILTER,&filter);Check(filter==GL_NEAREST,"Integer filter not nearest");
            IntegerSampling(final.Buffer());
        }
        auto ms=FrameBuffer::Create(Description(13,29,4)).value();auto resolved=FrameBuffer::Create(Description()).value();Dimensions(ms);Dimensions(resolved);
        ms.Bind();const float color[]{.25f,.5f,.75f,1};glClearBufferfv(GL_COLOR,0,color);Check(ms.ClearInteger(1,173),"MS integer clear");
        glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
        Check(ms.ResolveTo(resolved,0,0)&&ms.ResolveTo(resolved,1,1),"MS resolve failed");
        Check(glIsEnabled(GL_SCISSOR_TEST),"Resolve changed scissor state");glDisable(GL_SCISSOR_TEST);
        std::vector<std::byte> pixels(13*29*4);Check(resolved.ReadColor(0,pixels),"Resolve read");
        Check(std::abs(int(pixels[pixels.size()-4])-64)<=1&&std::abs(int(pixels[1])-128)<=1&&std::abs(int(pixels[2])-191)<=1&&resolved.ReadInteger(1,12,28).value()==173,"MS resolved color/integer pixels");
        GLuint pack=0;glGenBuffers(1,&pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);glBufferData(GL_PIXEL_PACK_BUFFER,16,nullptr,GL_STATIC_DRAW);
        glPixelStorei(GL_PACK_ROW_LENGTH,99);glPixelStorei(GL_PACK_SKIP_PIXELS,5);glPixelStorei(GL_PACK_SWAP_BYTES,GL_TRUE);
        Check(resolved.ReadInteger(1,12,28).value()==173&&resolved.ReadColor(0,pixels),"Readback inherited pack/PBO state");
        GLint bound=0,row=0,skip=0,swap=0;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&bound);glGetIntegerv(GL_PACK_ROW_LENGTH,&row);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip);glGetIntegerv(GL_PACK_SWAP_BYTES,&swap);
        Check(bound==GLint(pack)&&row==99&&skip==5&&swap==GL_TRUE,"Readback changed caller pack state");
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&pack);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SWAP_BYTES,GL_FALSE);
        Check(!ms.ResolveTo(resolved,0,1)&&!resolved.ResolveTo(ms)&&!ms.ReadInteger(1,0,0)&&!ms.ColorView(),"Invalid resolve/read/view accepted");
        FrameBufferSpecification depth;depth.Width=19;depth.Height=11;depth.Depth=Format::Depth24;
        auto depthOnly=FrameBuffer::Create(depth).value();Dimensions(depthOnly);
        auto point=PointShadowFrameBuffer::Create(17,17).value();auto cascade=CascadeShadowFrameBuffer::Create(13,29,2).value();Dimensions(point.Buffer());Dimensions(cascade.Buffer());
        Check(!PointShadowFrameBuffer::Create(17,19),"Nonsquare cube accepted");
        Check(cascade.OnResize(31,7)&&cascade.Buffer().Description().Layers==3,"Cascade resize changed layer count");
        GLint layers=0;glGetTextureLevelParameteriv(FB::Depth(cascade.Buffer()),0,GL_TEXTURE_DEPTH,&layers);Check(layers==3,"Cascade layers not allocated");
        glBindTexture(GL_TEXTURE_CUBE_MAP,FB::Depth(point.Buffer()));
        for(unsigned face=0;face<6;++face){GLint w=0;glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_TEXTURE_WIDTH,&w);Check(w==17,"Missing cube face");}
        auto target=RenderTarget::Create(13,29,4).value();Dimensions(target.Buffer());Dimensions(target.Buffer(RenderTargetSurface::Resolved));
        Check(target.BindAndBlitToScreen()&&target.OnResize(31,7)&&target.SetSamples(1),"RenderTarget resolve/resize/sample transition");
        Check(target.ColorView()&&target.Buffer().Description().Width==31,"Single-sample target transition");
        std::println("[PASS] square/rectangular integer sampling, MS color/integer resolve, depth-only, cube and cascade targets");
    }
    void APIENTRY ForbiddenDelete(GLsizei,const GLuint*){std::_Exit(87);}
}
int main(int argc,char** argv)
{
    using namespace GEngine;
    RuntimeAssets::Initialize("GEngineEditor");
    if (argc > 1 && std::string_view(argv[1]) == "--startup-failure")
    {
        struct StartupApp final : BaseApp
        {
            bool HasTargets() const { return m_RenderTarget || m_FinalFrameBuffer || m_MousePickFrameBuffer || m_PointShadowFrameBuffer || m_CascadeShadowFrameBuffer; }
        };
        for (const int allocation : {0, 3, 6, 10})
        {
            {
                StartupApp app;
                WindowProperties p; p.m_Width=p.m_Height=64; p.m_MinWidth=p.m_MinHeight=32;
                p.flag=BitFlags<WindowFlags,uint8_t>{WindowFlags::INVISIBLE};
                denyOwnerAfter=allocation;
                auto result=app.Initialize(p);
                Check(!result && std::holds_alternative<FramebufferError>(result.error()),"Startup did not propagate framebuffer failure");
                Check(std::get<FramebufferError>(result.error()).code==FramebufferErrorCode::Allocation && denyOwnerAfter==-1,"Wrong startup failure or injection not reached");
                Check(!app.HasTargets(),"Failed startup published partial framebuffer ownership");
                if constexpr(RenderCounters::Enabled)
                    for(const auto kind:{RenderCounters::Resource::Framebuffer,RenderCounters::Resource::Renderbuffer,RenderCounters::Resource::Texture})
                        Check(RenderCounters::Current().liveNames[static_cast<unsigned>(kind)]==0,"Failed startup retained GPU storage");
            }
            Check(!SDL_GL_GetCurrentContext(),"Failed startup did not retire its context");
        }
        std::println("[PASS] typed startup failures and partial ownership cleanup");return 0;
    }
    for(int cycle=0;cycle<2;++cycle)
    {
        auto root=std::make_unique<EngineContext>();WindowProperties p;p.m_Title="Framebuffer validation";
        p.m_Width=p.m_Height=64;p.m_MinWidth=p.m_MinHeight=32;p.m_IsVsync=false;p.flag=BitFlags<WindowFlags,uint8_t>{WindowFlags::INVISIBLE};root->Initialize({p});
        Check(glGetError()==GL_NO_ERROR,"Context initialization GL errors");
        glEnable(GL_DEBUG_OUTPUT);glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);glDebugMessageCallback(Diagnostic,nullptr);
        std::println("[GL] {} renderer={}",reinterpret_cast<const char*>(glGetString(GL_VERSION)),reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        if(argc>1)
        {
            auto owner=FrameBuffer::Create(Description()).value();
            std::set_terminate([]{std::println(stderr,"[EXPECTED] framebuffer ownership invariant");std::_Exit(86);});
            glad_glDeleteFramebuffers=ForbiddenDelete;
            if(std::string_view(argv[1])=="--reject-worker")
            {std::thread worker([b=std::move(owner)]()mutable{
                std::set_terminate([]{std::println(stderr,"[EXPECTED] framebuffer ownership invariant");std::_Exit(86);});b={};});worker.join();}
            else {Check(SDL_GL_CreateContext(root->MainWindow()->GetSDLWindow()),"Second context creation");owner={};}
            return 89;
        }
        {Observer observer;OwnershipAndFailure();PixelsAndTargets();}
        Check(glGetError()==GL_NO_ERROR&&errors==0,"Framebuffer operations generated GL errors");root.reset();
        Check(!SDL_GL_GetCurrentContext(),"Context remained after retirement");
    }
    std::println("[PASS] framebuffers cycles=2 checks={}",checks);
}
