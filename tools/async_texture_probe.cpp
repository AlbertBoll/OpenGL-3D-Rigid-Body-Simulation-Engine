#include "Assets/Textures/AsyncTexture.h"
#include "Managers/AssetsManager.h"
#include "Renderer/FrameScheduler.h"
#include <type_traits>
using namespace GEngine;
using namespace GEngine::Asset;
static_assert(!std::is_copy_constructible_v<AsyncTextureLoader>);
static_assert(std::same_as<decltype(std::declval<AsyncTextureLoader&>().Request("file")), std::expected<UploadTicket, AsyncTextureError>>);
#ifndef TEXTURE_SCHEMA_ONLY
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLContextThread.h"
#include "../GEngine/src/Assets/TextureBackend.h"
#include <array>
#include <chrono>
#include <fstream>
#include <print>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <crtdbg.h>
using namespace std::chrono_literals;

namespace
{
    int checks{};
    template<class T> void Check(const T& value, const char* label)
    { ++checks; if (!static_cast<bool>(value)) {std::println(stderr,"[FAIL] {}",label);std::exit(1);} }
    template<class T,class E> T Take(std::expected<T,E> value, const char* label)
    {
        if (!value) {
            if constexpr (std::same_as<E,AsyncTextureError>) std::println(stderr,"{}",DescribeAsyncTextureError(value.error()));
            if constexpr (std::same_as<E,ScheduleError>) std::println(stderr,"{}",DescribeScheduleError(value.error()));
        }
        Check(value,label);return std::move(*value);
    }
    auto realUpload=glad_glTexImage2D;
    std::size_t uploadCalls{};
    void APIENTRY Upload(GLenum target,GLint level,GLint format,GLsizei width,GLsizei height,
        GLint border,GLenum external,GLenum type,const void* data)
    {
        Check(GLContextThread::IsCurrentOwner(),"no worker GL upload");++uploadCalls;
        realUpload(target,level,format,width,height,border,external,type,data);
    }
    void WriteTga(const std::filesystem::path& path,bool alpha)
    {
        std::array<unsigned char,18> header{};
        header[2]=2;header[12]=3;header[14]=2;header[16]=alpha?32:24;header[17]=0x20|(alpha?8:0);
        std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(header.data()),header.size());
        for(int pixel=0;pixel<6;++pixel) {
            const unsigned char color[]{static_cast<unsigned char>(3+pixel),static_cast<unsigned char>(20+pixel),
                static_cast<unsigned char>(90+pixel),static_cast<unsigned char>(120+pixel)};
            file.write(reinterpret_cast<const char*>(color),alpha?4:3);
        }
    }
    AsyncTextureLimits Limits()
    {
        AsyncTextureLimits result;result.requestBytes=1024*1024;
        result.queue={32,2,16,16*1024*1024,16,16*1024*1024,4*1024*1024,1s};
        return result;
    }
    AsyncTextureStatus Await(AsyncTextureLoader& loader,UploadTicket ticket,AsyncAssetState state)
    {
        const auto deadline=std::chrono::steady_clock::now()+10s;
        for(;;) {
            auto status=Take(loader.Status(ticket),"texture status");
            if(status.state==state)return status;
            if(status.state==AsyncAssetState::Failed && state!=AsyncAssetState::Failed && status.error)
                std::println(stderr,"{}",DescribeAsyncTextureError(*status.error));
            Check(status.state!=AsyncAssetState::Failed || state==AsyncAssetState::Failed,"unexpected async failure");
            Check(std::chrono::steady_clock::now()<deadline,"async state deadline");std::this_thread::yield();
        }
    }
    void Queued(AsyncTextureLoader& loader,std::size_t count)
    {
        const auto deadline=std::chrono::steady_clock::now()+10s;
        while(loader.Queue().Stats().queuedRequests<count) {
            Check(std::chrono::steady_clock::now()<deadline,"queued deadline");std::this_thread::yield();
        }
    }
    void Drain(EngineContext& root,AsyncTextureLoader& loader)
    {
        RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager()};
        context.visible=false;context.uploads=&loader.Queue();
        Take(FrameScheduler::Render(context),"texture scheduler drain");
    }
    TextureView View(AssetPublication& publication,TextureRegistry& registry,TextureHandle handle)
    {auto read=publication.BeginFrame();return TextureView(Take(registry.Acquire(read,handle),"resolve image"));}
    void Pixels(const TextureView& view,bool alpha,bool flipped,bool srgb,bool mip)
    {
        auto name=Take(AssetDetail::TextureBackend::Name(view),"backend test name");
        glBindTexture(GL_TEXTURE_2D,name);
        GLint format{},width{},height{},last{};
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,1,GL_TEXTURE_WIDTH,&last);
        Check(width==3 && height==2,"decoded extent preserved");
        Check(format==(srgb?(alpha?GL_SRGB8_ALPHA8:GL_SRGB8):(alpha?GL_RGBA8:GL_RGB8)),"format and color space preserved");
        Check(last==(mip?1:0),"mip intent preserved");
        std::array<unsigned char,24> pixels{};glPixelStorei(GL_PACK_ALIGNMENT,1);
        glGetTexImage(GL_TEXTURE_2D,0,alpha?GL_RGBA:GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
        for(int row=0;row<2;++row)for(int x=0;x<3;++x) {
            const int src=(flipped?1-row:row)*3+x,offset=(row*3+x)*(alpha?4:3);
            Check(pixels[offset]==90+src && pixels[offset+1]==20+src && pixels[offset+2]==3+src,"odd RGB stride/orientation pixel parity");
            if(alpha)Check(pixels[offset+3]==120+src,"alpha preserved");
        }
    }
    void Run(EngineContext& root)
    {
        const auto folder=std::filesystem::current_path()/"images";std::filesystem::create_directories(folder);
        WriteTga(folder/"rgb.tga",false);WriteTga(folder/"rgba.tga",true);
        {std::ofstream bad(folder/"corrupt.tga");bad<<"not an image";}
        {std::ofstream bad(folder/"empty.tga");}
        std::error_code linkError;std::filesystem::create_hard_link(folder/"rgb.tga",folder/"alias.tga",linkError);
        Check(!linkError,"hard-link fixture");
        auto& pub=root.AssetPublications();TextureRegistry registry(pub);
        auto loader=Take(AsyncTextureLoader::Create(pub,registry,folder,Limits()),"async texture loader");
        TextureDesc rgb;rgb.format=TextureFormat::RGB8;rgb.colorSpace=TextureColorSpace::Linear;rgb.mips=TextureMipIntent::None;rgb.orientation=ImageOrientation::TopLeft;
        const auto initialUpload=uploadCalls;
        auto a=Take(loader->Request("rgb.tga",rgb),"RGB request");
        auto same=Take(loader->Request("./rgb.tga",rgb),"lexical duplicate");Check(a==same,"exact duplicate ticket");
        auto alias=Take(loader->Request("alias.tga",rgb),"canonical duplicate");
        TextureDesc srgb=rgb;srgb.colorSpace=TextureColorSpace::SRGB;srgb.mips=TextureMipIntent::Generate;
        auto b=Take(loader->Request("rgb.tga",srgb),"same path different color space");
        TextureDesc rgba=rgb;rgba.format=TextureFormat::RGBA8;rgba.orientation=ImageOrientation::BottomLeft;
        auto c=Take(loader->Request("rgba.tga",rgba),"RGBA bottom-left request");
        Queued(*loader,4);
        Check(uploadCalls==initialUpload && loader->Stats().decodes==3,"all decode on workers; duplicate coalesced before decoding");
        Check(loader->Stats().fileReads==3 && loader->Stats().peakRequestBytes<=Limits().requestBytes,"bounded encoded and decoded allocations");
        Check(!Take(loader->Status(a),"pending state").image,"pending caller retains fallback");
        const auto fallback=Take(Manager::AssetsManager::FallbackTexture(rgb),"shared fallback");
        const auto fallbackView=Take(Manager::AssetsManager::ResolveTexture(fallback),"fallback visible while loading");
        Check(fallbackView && Take(loader->Status(a),"still pending").state==AsyncAssetState::CpuReady,"fallback while CPU-ready");
        GLuint unpack{};glGenBuffers(1,&unpack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);glBufferData(GL_PIXEL_UNPACK_BUFFER,128,nullptr,GL_STATIC_DRAW);
        glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,13);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,1);glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);
        Drain(root,*loader);
        const auto ready=Await(*loader,a,AsyncAssetState::Ready);
        const auto duplicate=Await(*loader,alias,AsyncAssetState::Ready);
        const auto nonlinear=Await(*loader,b,AsyncAssetState::Ready);
        const auto alpha=Await(*loader,c,AsyncAssetState::Ready);
        Check(ready.image==duplicate.image && ready.image!=nonlinear.image,"canonical sharing and resource-aware separation");
        Check(loader->Stats().uploads==3,"one upload per canonical resource descriptor");
        GLint alignment{},rowLength{},buffer{},skipRows{},skipPixels{};
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&rowLength);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skipRows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skipPixels);
        Check(alignment==8 && rowLength==13 && GLuint(buffer)==unpack && skipRows==1 && skipPixels==2,"upload restores unpack and PBO state");
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&unpack);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        Pixels(View(pub,registry,ready.image),false,false,false,false);
        Pixels(View(pub,registry,nonlinear.image),false,false,true,true);
        Pixels(View(pub,registry,alpha.image),true,true,false,false);
        Check(ready.description==TextureDesc{TextureKind::Image2D,TextureFormat::RGB8,TextureColorSpace::Linear,
            TextureMipIntent::None,ImageOrientation::TopLeft,TextureUsage::Sampled,3,2},"ready semantic descriptor");
        const auto missing=Take(loader->Request("missing.tga",rgb),"missing accepted asynchronously");
        const auto corrupt=Take(loader->Request("corrupt.tga",rgb),"corrupt accepted asynchronously");
        const auto empty=Take(loader->Request("empty.tga",rgb),"empty accepted asynchronously");
        auto absent=Await(*loader,missing,AsyncAssetState::Failed);
        Check(std::get<TextureError>(*absent.error).code==TextureErrorCode::FileSystem && std::get<TextureError>(*absent.error).system,"missing structured filesystem cause");
        Check(std::get<TextureError>(*Await(*loader,corrupt,AsyncAssetState::Failed).error).code==TextureErrorCode::Decode,"corrupt typed decode failure");
        Check(std::get<TextureError>(*Await(*loader,empty,AsyncAssetState::Failed).error).code==TextureErrorCode::Decode,"empty typed decode failure");
        Check(loader->Cancel(corrupt),"cancel failed is idempotent");Await(*loader,corrupt,AsyncAssetState::Failed);
        auto invalid=rgb;invalid.kind=TextureKind::Cube;
        Check(!loader->Request("rgb.tga",invalid),"unselected cube contract rejected explicitly");
        auto tinyLimits=Limits();tinyLimits.requestBytes=32;
        auto tiny=Take(AsyncTextureLoader::Create(pub,registry,folder,tinyLimits),"small bounded decoder");
        auto huge=Take(tiny->Request("rgb.tga",rgb),"bounded file request");
        Check(std::get<TextureError>(*Await(*tiny,huge,AsyncAssetState::Failed).error).code==TextureErrorCode::Allocation,"oversized file reports typed allocation failure");
        auto decodeLimits=Limits();decodeLimits.requestBytes=80;
        auto decodeBound=Take(AsyncTextureLoader::Create(pub,registry,folder,decodeLimits),"decoder scratch bound");
        auto scratch=Take(decodeBound->Request("rgb.tga",rgb),"scratch bounded request");
        Check(std::get<TextureError>(*Await(*decodeBound,scratch,AsyncAssetState::Failed).error).code==TextureErrorCode::Allocation,"decoder allocations obey reservation");

        auto cancelLoader=Take(AsyncTextureLoader::Create(pub,registry,folder,Limits()),"cancel loader");
        auto cancelA=Take(cancelLoader->Request("rgb.tga",rgb),"cancel queued");
        auto cancelAlias=Take(cancelLoader->Request("alias.tga",rgb),"cancel canonical alias");Queued(*cancelLoader,2);
        Check(cancelLoader->Cancel(cancelAlias),"alias cancellation cancels canonical operation");
        Await(*cancelLoader,cancelA,AsyncAssetState::Cancelled);Drain(root,*cancelLoader);
        Check(cancelLoader->Stats().uploads==0,"cancelled canonical completion not published");
        auto closedLimits=Limits();closedLimits.queue.queuedRequests=1;
        auto closing=Take(AsyncTextureLoader::Create(pub,registry,folder,closedLimits),"shutdown pressure loader");
        auto closeA=Take(closing->Request("rgb.tga",rgb),"shutdown first");Queued(*closing,1);
        auto closeB=Take(closing->Request("rgba.tga",rgba),"shutdown blocked worker");Await(*closing,closeB,AsyncAssetState::CpuReady);
        closing->Shutdown();Await(*closing,closeA,AsyncAssetState::Cancelled);Await(*closing,closeB,AsyncAssetState::Cancelled);
        Check(closing->Queue().Stats().reservedBytes==0 && !closing->Request("rgb.tga",rgb),"closed queue joins and frees CPU payloads");

        // Live selected PNG: compare every byte to the existing synchronous decoder
        // after the asynchronous upload, including the worker's orientation policy.
        auto real=Take(Manager::AssetsManager::AsyncTextures(),"root-owned async textures");
        auto wood=Take(real->Request("Sphere/wood_diffuse"),"selected wood request");
        Await(*real,wood,AsyncAssetState::CpuReady);Queued(*real,1);Drain(root,*real);
        auto woodState=Await(*real,wood,AsyncAssetState::Ready);
        auto woodView=Take(Manager::AssetsManager::ResolveTexture(woodState.image),"selected image view");
        auto synchronous=Take(Manager::AssetsManager::LoadTexture("Sphere/wood_diffuse"),"existing decode reference");
        auto syncView=Take(Manager::AssetsManager::ResolveTexture(synchronous),"reference view");
        const auto bytes=std::size_t(woodState.description.width)*woodState.description.height*4;
        std::vector<std::byte> asyncPixels(bytes),syncPixels(bytes);
        glBindTexture(GL_TEXTURE_2D,Take(AssetDetail::TextureBackend::Name(woodView),"async readback"));glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,asyncPixels.data());
        glBindTexture(GL_TEXTURE_2D,Take(AssetDetail::TextureBackend::Name(syncView),"sync readback"));glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,syncPixels.data());
        Check(asyncPixels==syncPixels && real->Stats().decodes==1 && real->Stats().uploads==1,"selected PNG byte-exact reference parity");
        Check(real->Stats().peakRequestBytes<=AsyncTextureLimits{}.requestBytes,"selected PNG bounded scratch");
        Check(glGetError()==GL_NO_ERROR,"no unexpected GL errors");
        loader->Shutdown();
        {auto publication=pub.BeginPublication();Check(registry.Close(publication),"all owned registry resources retire");}
        std::println("[PASS] async-texture RGB/RGBA/odd-width/orientation/color-space/mips/canonical/failures/cancel/shutdown/bounds/worker-GL/PNG-parity checks={}",checks);
    }
}
int main()
{
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    RuntimeAssets::Initialize("RigidBodySimulation");EngineContext root;
    WindowProperties properties;properties.m_Title="Phase 50 async texture validation";properties.flag={WindowFlags::INVISIBLE};
    properties.m_Width=properties.m_Height=properties.m_MinWidth=properties.m_MinHeight=64;properties.m_IsVsync=false;
    Check(root.Initialize({properties}),"root context");realUpload=glad_glTexImage2D;glad_glTexImage2D=Upload;
    Run(root);glad_glTexImage2D=realUpload;
    std::println("[PASS] async-texture-retirement");
}
#endif
