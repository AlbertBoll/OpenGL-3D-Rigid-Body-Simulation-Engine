#if defined(RAY_APP_PROBE)
#include <set>
#define main Phase66UnusedShutdownMain
#include "shutdown_probe.cpp"
#undef main
#include "Core/SimpleRenderer.h"
#include "Camera/RayTracingCamera.h"
#include <imgui/imgui_internal.h>
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
namespace RayActual {
    std::string_view mode;
    unsigned runs{}, renders{}, imageRenders{}, resizes{}, destructors{}, failedCalls{};
    bool balanced=true, submittedFailure=false;
    int rejectedFrame=-1;
    std::optional<::GEngine::ImageError> failure;
    std::unique_ptr<ModelNames> names;
    unsigned allocations{}, updates{};
    PFNGLTEXIMAGE2DPROC allocate;
    PFNGLTEXSUBIMAGE2DPROC update;
    unsigned allocationsAtFailure{}, updatesAtFailure{};
    bool inImage{},injected{};
    GLenum pendingError=GL_NO_ERROR;
    PFNGLGETERRORPROC getError;
    PFNGLGENTEXTURESPROC generate;
    PFNGLDELETETEXTURESPROC destroy;
    std::set<GLuint> textures;
    unsigned generatedTextures{},retiredTextures{};
    SDL_threadID owner;
    GLuint presentedTexture{};
    bool imageUVs{};
    GLenum APIENTRY Error(){if(pendingError!=GL_NO_ERROR)return std::exchange(pendingError,GL_NO_ERROR);return getError();}
    void APIENTRY Generate(GLsizei count,GLuint* ids){
        Check(SDL_ThreadID()==owner && SDL_GL_GetCurrentContext(),"Ray texture generation outside owner context");
        generate(count,ids);for(int i=0;i<count;++i)if(ids[i]){Check(textures.insert(ids[i]).second,"Duplicate texture generation");++generatedTextures;}
    }
    void APIENTRY Destroy(GLsizei count,const GLuint* ids){
        Check(SDL_ThreadID()==owner && SDL_GL_GetCurrentContext(),"Ray texture retirement outside owner context");
        for(int i=0;i<count;++i)if(ids[i]){Check(textures.erase(ids[i])==1,"Duplicate/unowned texture retirement");++retiredTextures;}destroy(count,ids);
    }
    void APIENTRY Allocate(GLenum target,GLint level,GLint format,GLsizei width,GLsizei height,GLint border,GLenum source,GLenum type,const void* data)
    {++allocations;if(inImage && mode=="backend" && !injected){injected=true;pendingError=GL_OUT_OF_MEMORY;return;}allocate(target,level,format,width,height,border,source,type,data);}
    void APIENTRY Update(GLenum target,GLint level,GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,const void* data)
    {++updates;update(target,level,x,y,width,height,format,type,data);}
    void Rejected(const ::GEngine::ImageResult& result) {
        if(result)return;
        ++failedCalls;failure=result.error();rejectedFrame=GImGui->FrameCount;
        allocationsAtFailure=allocations;updatesAtFailure=updates;
    }
}
namespace GEngine {
    class Phase66RayRenderer : public SimpleRenderer {
    public:
        ImageResult OnResize(uint32_t width,uint32_t height) {
            ++RayActual::resizes;
            auto result=RayActual::mode=="extent" ? SimpleRenderer::OnResize(UINT32_MAX,UINT32_MAX) : SimpleRenderer::OnResize(width,height);
            RayActual::Rejected(result);return result;
        }
        ImageResult Render(const RayTracingScene& scene,const RayTracingCamera& camera) {
            ++RayActual::imageRenders;
            RayTracingCamera mismatch(45.f,.1f,100.f);
            RayActual::inImage=true;
            auto result=SimpleRenderer::Render(scene,RayActual::mode=="camera"?mismatch:camera);
            RayActual::inImage=false;
            if(result){GLint name=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&name);RayActual::presentedTexture=name;}
            RayActual::Rejected(result);return result;
        }
    };
}
// Test-only substitution invokes the real methods with rejected inputs. The production
// Ray UI, GenerateImage, scheduler, runtime channel and EntryPoint run unchanged.
#define SimpleRenderer Phase66RayRenderer
#define CreateApp Phase66OriginalRayCreateApp
#include "../RayTracing/src/RayTracing.cpp"
#undef CreateApp
#undef SimpleRenderer
namespace RayActual {
    class App final : public ::GEngine::RayTracingAPP {
    public:
        ::GEngine::ApplicationInitializationResult Initialize(const std::initializer_list<::GEngine::WindowProperties>& props) override {
            auto result=RayTracingAPP::Initialize(props);if(!result)return result;
            SDL_Event event{};while(SDL_PollEvent(&event)){}
            m_WindowHidden=false;SetManualFrameRateLimit(0);return {};
        }
        void ProcessInput(::GEngine::Timestep) override {}
        void Update(::GEngine::Timestep) override {}
        void OnUIRender() override {
            RayTracingAPP::OnUIRender();
            if(presentedTexture && !failure) {
                auto* viewport=ImGui::FindWindowByName("Viewport");Check(viewport!=nullptr,"Actual viewport missing");
                std::cout<<"[OBSERVE] Ray UI frame="<<GImGui->FrameCount<<" texture="<<presentedTexture
                    <<" hidden="<<viewport->Hidden<<" commands="<<viewport->DrawList->CmdBuffer.Size<<std::endl;
                for(const auto& command:viewport->DrawList->CmdBuffer) {
                    if(command.TextureId!=reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(presentedTexture)))continue;
                    const auto& list=*viewport->DrawList;
                    Check(command.ElemCount==6,"Ray semantic image draw changed");
                    const auto start=list.IdxBuffer[command.IdxOffset]+command.VtxOffset;
                    const std::array<ImVec2,4> expected{{{0,1},{1,1},{1,0},{0,0}}};
                    for(unsigned i=0;i<4;++i)Check(list.VtxBuffer[start+i].uv.x==expected[i].x && list.VtxBuffer[start+i].uv.y==expected[i].y,"Ray image UV changed");
                    imageUVs=true;
                }
            }
        }
        void Render() override {
            ++renders;RayTracingAPP::Render();
            balanced=balanced && !GImGui->WithinFrameScope && GImGui->FrameCountEnded==GImGui->FrameCount && !GetWindow()->Timings().Active();
            if(failure) {
                submittedFailure=GImGui->FrameCountRendered==rejectedFrame;
                Check(allocationsAtFailure==allocations && updatesAtFailure==updates,"GPU upload after rejected image");
            } else {
                Check(GImGui->FrameCountRendered==GImGui->FrameCount,"Successful Ray UI not submitted");
                if(imageUVs) ShutDown();
                else {Check(renders<3,"Actual viewport did not produce a visible image");
                    ImGui::SetWindowPos("Viewport",{20,20});ImGui::SetWindowSize("Viewport",{160,120});}
            }
        }
        ::GEngine::ApplicationRunResult Run() override {
            ++runs;auto result=BaseApp::Run();Check(result.has_value()==(mode=="success"),"Actual Ray runtime result changed");
            if(!result) {const auto before=renders;auto again=BaseApp::Run();Check(!again && renders==before,"Failed Ray runtime resumed");}
            return result;
        }
        ~App() override {++destructors;Check(SDL_GL_GetCurrentContext()!=nullptr,"Ray application outlived context");}
    };
}
extern "C" int Phase66RealGladLoadGL(void);
extern "C" int gladLoadGL(void) {
    const int result=Phase66RealGladLoadGL();if(result) {
        RayActual::names=std::make_unique<ModelNames>();
        RayActual::owner=SDL_ThreadID();
        RayActual::getError=glad_glGetError;glad_glGetError=RayActual::Error;
        RayActual::generate=glad_glGenTextures;RayActual::destroy=glad_glDeleteTextures;
        glad_glGenTextures=RayActual::Generate;glad_glDeleteTextures=RayActual::Destroy;
        RayActual::allocate=glad_glTexImage2D;RayActual::update=glad_glTexSubImage2D;
        glad_glTexImage2D=RayActual::Allocate;glad_glTexSubImage2D=RayActual::Update;
    }return result;
}
::GEngine::WindowProperties winProp=Properties();
::GEngine::BaseApp* CreateApp(){return new RayActual::App;}
int RunRayActual(int argc,char** argv) {
    using namespace RayActual;
    if(argc!=2)return 2;mode=argv[1];if(mode!="extent" && mode!="camera" && mode!="backend" && mode!="success")return 2;
    SDL_SetMainReady();const auto status=ProductionEntryPoint(argc,argv);
    Check(status==(mode=="success"?0:1) && runs==1 && destructors==1 && (mode=="success"?resizes==imageRenders:resizes==1),"Actual Ray entry/exit/retirement failed");
    Check(balanced && !submittedFailure,"Actual Ray UI scope/submission failed");
    Check((mode=="success" ? imageRenders>0 && imageRenders<=3 : imageRenders==(mode=="extent"?0u:1u)) && failedCalls==(mode=="success"?0u:1u),"Post-failure image processing occurred");
    if(failure) {
        const auto& e=*failure;
        Check(e.code==(mode=="extent"?::GEngine::ImageErrorCode::InvalidExtent : mode=="backend"?::GEngine::ImageErrorCode::Backend : ::GEngine::ImageErrorCode::CameraMismatch),"Actual renderer failure code lost");
        std::cout<<"[PAYLOAD] width="<<e.width<<" height="<<e.height<<" source="<<e.sourceWidth<<"x"<<e.sourceHeight
            <<" expected="<<e.expectedElements<<" actual="<<e.actualElements<<" backend="<<e.backendCode<<std::endl;
    }
    Check(names!=nullptr,"Loader not reached");names->Empty();names.reset();PlatformGone();
    Check(textures.empty() && generatedTextures==retiredTextures,"Actual Ray texture ownership leak");
    Check(mode!="success" || imageUVs,"Successful semantic Image was not presented with preserved UVs");
    std::cout<<"[PASS] actual Ray textures="<<generatedTextures<<"/"<<retiredTextures<<" live=0 UV="<<imageUVs<<std::endl;
    std::cout<<"[OBSERVE] actual Ray "<<mode<<" exit="<<status<<" renders="<<renders<<" image-renders="<<imageRenders
        <<" buffers="<<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers<<" arrays="<<ModelNames::generatedArrays<<"/"<<ModelNames::retiredArrays
        <<" owner=1 root=0 SDL=0 TTF=0"<<std::endl;
    std::cout<<"[PASS] actual Ray "<<mode<<" balanced=1 no-failed-submit=1"<<std::endl;return status;
}
int main(int argc,char** argv) {
    try { return RunRayActual(argc,argv); }
    catch(const std::exception& error) {std::cerr<<"[FAIL] actual Ray fixture: "<<error.what()<<std::endl;return 2;}
}
#else
#include "Core/ImageError.h"
#include "Renderer/FrameScheduler.h"
#include <type_traits>
static_assert(std::is_constructible_v<GEngine::ScheduleCause, GEngine::ImageError>);
static_assert(std::same_as<GEngine::ImageResult, std::expected<void, GEngine::ImageError>>);
static_assert(std::same_as<decltype(GEngine::ImageError::format), GEngine::ImageFormat>);
static_assert(std::is_trivially_copyable_v<GEngine::ImageError>);
static_assert(std::is_nothrow_destructible_v<GEngine::ImageResult>);
#ifndef RAY_ERROR_SCHEMA_ONLY
#define main Phase66UnusedShutdownMain
#include "shutdown_probe.cpp"
#undef main
#include <imgui/imgui_internal.h>
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
namespace RayBoundary
{
    using namespace GEngine;
    std::string_view mode;
    unsigned runs{}, renders{}, authors{}, destructors{};
    bool failNext{}, submittedFailure{}, balanced=true;
    int authoredFrame{}, renderedBefore{}, renderedAfter{};
    std::unique_ptr<ModelNames> names;
    ImageError Failure() {return {.code=ImageErrorCode::Backend, .operation="image-fixture-operation",
        .message="complete image diagnostic: source=ray fixture; upload unavailable", .width=37, .height=41,
        .format=ImageFormat::RGBA32F, .sourceWidth=43, .sourceHeight=47,
        .expectedElements=1517, .actualElements=2021, .backendCode=1285};}
    void CheckImage(const ImageError& error)
    {
        const auto expected=Failure();
        Check(error.code==expected.code && error.operation==expected.operation && error.message==expected.message
            && error.width==37 && error.height==41 && error.format==ImageFormat::RGBA32F
            && error.sourceWidth==43 && error.sourceHeight==47 && error.expectedElements==1517
            && error.actualElements==2021 && error.backendCode==1285,"Complete typed ImageError payload lost");
    }
    class App final : public BaseApp
    {
    public:
        ApplicationInitializationResult Initialize(const std::initializer_list<WindowProperties>& props) override
        {
            auto result=BaseApp::Initialize(props);if(!result)return result;
            ScheduleError scheduled{FrameStage::Pass,Failure()};auto copied=scheduled;auto moved=std::move(scheduled);
            CheckImage(std::get<ImageError>(copied.cause));CheckImage(std::get<ImageError>(moved.cause));
            ImageResult image=std::unexpected(Failure());auto imageCopy=image;auto imageMove=std::move(image);
            CheckImage(imageCopy.error());CheckImage(imageMove.error());
            SDL_Event event{};while(SDL_PollEvent(&event)){}
            m_WindowHidden=false;SetManualFrameRateLimit(0);return {};
        }
        void ProcessInput(Timestep) override {}
        void Update(Timestep) override {}
        std::expected<ScheduledFrameStats,ScheduleError> Frame()
        {
            RenderContext context{*GetWindow(),*GetWindowManager()};
            context.editorUI={nullptr,[](void*)->ScheduleResult {
                ++authors;authoredFrame=GImGui->FrameCount;renderedBefore=GImGui->FrameCountRendered;
                ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({300,150});
                ImGui::Begin("Phase66 authored UI",nullptr,ImGuiWindowFlags_NoSavedSettings);
                ImGui::TextUnformatted("Visible content must not submit after authoring failure");ImGui::End();
                if(failNext)return std::unexpected(ScheduleError{FrameStage::Pass,Failure()});
                return {};
            }};
            auto result=FrameScheduler::Render(context);
            balanced=balanced && !GImGui->WithinFrameScope && GImGui->FrameCountEnded==authoredFrame
                && !GetWindow()->Timings().Active();
            renderedAfter=GImGui->FrameCountRendered;
            if(failNext)submittedFailure=submittedFailure || renderedAfter==authoredFrame;
            else Check(renderedAfter==authoredFrame,"Successful UI was not rendered");
            return result;
        }
        void Render() override
        {
            ++renders;failNext=mode!="success";auto result=Frame();
            if(failNext) {
                Check(!result && result.error().stage==FrameStage::Pass,"UI failure stage lost");
                const auto& cause=std::get<ImageError>(result.error().cause);CheckImage(cause);
                if(mode=="recovery") {
                    failNext=false;auto recovered=Frame();Check(bool(recovered),"Balanced UI failure prevented explicit later frame");ShutDown();
                } else FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Image","6",
                    std::string(cause.operation),"extent=37x41",DescribeScheduleError(result.error())});
            } else ShutDown();
        }
        ApplicationRunResult Run() override
        {
            ++runs;auto result=BaseApp::Run();Check(result.has_value()==(mode!="failure"),"UI runtime status changed");
            if(!result) {const auto before=renders;auto again=BaseApp::Run();Check(!again && renders==before,"Failed UI runtime resumed");}
            return result;
        }
        ~App() override {++destructors;Check(SDL_GL_GetCurrentContext()!=nullptr,"UI app outlived context");}
    };
}
extern "C" int Phase66RealGladLoadGL(void);
extern "C" int gladLoadGL(void)
{
    const int result=Phase66RealGladLoadGL();if(result)RayBoundary::names=std::make_unique<ModelNames>();return result;
}
::GEngine::WindowProperties winProp=Properties();
::GEngine::BaseApp* CreateApp(){return new RayBoundary::App;}
int main(int argc,char** argv)
{
    using namespace RayBoundary;
    if(argc!=2)return 2;mode=argv[1];if(mode!="failure" && mode!="success" && mode!="recovery")return 2;
    SDL_SetMainReady();const auto status=ProductionEntryPoint(argc,argv);
    Check(status==(mode=="failure"?1:0) && runs==1 && renders==1 && destructors==1,"UI entry/exit/retirement failed");
    Check(balanced && authors==(mode=="recovery"?2u:1u),"UI scope/timing retirement failed");
    Check(names!=nullptr,"Loader not reached");names->Empty();names.reset();PlatformGone();
    std::cout<<"[OBSERVE] image transport "<<mode<<" exit="<<status<<" submitted-after-failure="<<submittedFailure
        <<" before="<<renderedBefore<<" after="<<renderedAfter<<" authored="<<authoredFrame
        <<" buffers="<<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers<<" arrays="
        <<ModelNames::generatedArrays<<"/"<<ModelNames::retiredArrays<<" owner=1 root=0 SDL=0 TTF=0"<<std::endl;
    if(submittedFailure){std::cerr<<"[FAIL] Scheduler submitted a typed-failed UI frame"<<std::endl;return 2;}
    std::cout<<"[PASS] image transport "<<mode<<" balanced=1 no-failed-submit=1"<<std::endl;return status;
}
#endif

#endif
