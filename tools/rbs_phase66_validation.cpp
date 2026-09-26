// Test-only instrumentation. This translation unit is excluded from production projects.
// The executable is RigidBodySimulation: its real application, EntryPoint and runtime run below.
#include "gepch.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "RigidBodySimulation.h"
#include "Material/AAScreenMaterial.h"
#include "../GEngine/src/Assets/TextureBackend.h"
#include <imgui/imgui_internal.h>
#include <sdl2/SDL_ttf.h>
#include <array>
#include <unordered_set>
#include <string_view>
#define main ProductionRbsEntryPoint
#include "EntryPoint.h"
#undef main
#define CreateApp Phase66OriginalRbsFactory
#include "../RigidBodySimulation/src/RigidBodySimulation.cpp"
#undef CreateApp
extern "C" {
    extern SDL_GLContext (SDLCALL* __imp_SDL_GL_CreateContext)(SDL_Window*);
    extern int (SDLCALL* __imp_SDL_GL_MakeCurrent)(SDL_Window*,SDL_GLContext);
    extern void (SDLCALL* __imp_SDL_GL_DeleteContext)(SDL_GLContext);
    extern SDL_Window* (SDLCALL* __imp_SDL_CreateWindow)(const char*,int,int,int,int,Uint32);
    extern void (SDLCALL* __imp_SDL_DestroyWindow)(SDL_Window*);
}
namespace RbsValidation
{
    void Require(bool valid,const char* reason)
    {if(!valid){std::cerr<<"[FAIL] RBS "<<reason<<std::endl;std::exit(2);}}
    enum Kind {Buffer,Array,Texture,Framebuffer,Renderbuffer,Program,Shader,Sampler,KindCount};
    constexpr std::array<const char*,KindCount> labels{"buffer","array","texture","framebuffer","renderbuffer","program","shader","sampler"};
    struct Ownership {std::unordered_set<GLuint> live;unsigned generated{},retired{};};
    std::array<Ownership,KindCount> owners;
    SDL_threadID ownerThread{};
    bool valid=true;
    std::string_view mode;
    unsigned screenChecks{};
    uint64_t failureDraws{};
    bool registrationInjected{},activationArmed{},activationInjected{};
    unsigned postFailureDraws{},nativeCreates{},nativeRetires{},windowCreates{},windowRetires{};
    std::unordered_set<SDL_GLContext> nativeContexts;
    std::unordered_set<SDL_Window*> nativeWindows;

    unsigned loads{},initializes{},runs{},updates{},renders{},destructors{};
    uint64_t draws{};
    void Owner(){const bool ok=SDL_ThreadID()==ownerThread && SDL_GL_GetCurrentContext()!=nullptr;
        if(!ok)std::cerr<<"[AUDIT] Wrong GL owner thread/context"<<std::endl;valid &= ok;}
    void Created(Kind kind,GLsizei count,const GLuint* names) {
        Owner();auto& set=owners[kind];for(int i=0;i<count;++i)if(names[i]){const bool unique=set.live.insert(names[i]).second;if(!unique)std::cerr<<"[AUDIT] Duplicate "<<labels[kind]<<" "<<names[i]<<std::endl;valid &= unique;++set.generated;}
    }
    void Retired(Kind kind,GLsizei count,const GLuint* names) {
        Owner();auto& set=owners[kind];for(int i=0;i<count;++i)if(names[i]){const bool found=set.live.erase(names[i])==1;if(!found)std::cerr<<"[AUDIT] Untracked "<<labels[kind]<<" "<<names[i]<<std::endl;valid &= found;++set.retired;}
    }
    template<Kind K> struct Names {
        inline static void(APIENTRY *generate)(GLsizei,GLuint*);
        inline static void(APIENTRY *destroy)(GLsizei,const GLuint*);
        static void APIENTRY Generate(GLsizei count,GLuint* names){generate(count,names);Created(K,count,names);}
        static void APIENTRY Destroy(GLsizei count,const GLuint* names){Retired(K,count,names);destroy(count,names);}
    };
    PFNGLCREATEBUFFERSPROC createBuffers;
    void APIENTRY CreateBuffers(GLsizei count,GLuint* names){createBuffers(count,names);Created(Buffer,count,names);}
    PFNGLCREATEPROGRAMPROC createProgram;
    PFNGLDELETEPROGRAMPROC deleteProgram;
    PFNGLCREATESHADERPROC createShader;
    PFNGLDELETESHADERPROC deleteShader;
    GLuint APIENTRY CreateProgram(){auto name=createProgram();Created(Program,1,&name);return name;}
    void APIENTRY DeleteProgram(GLuint name){Retired(Program,1,&name);deleteProgram(name);}
    GLuint APIENTRY CreateShader(GLenum kind){auto name=createShader(kind);Created(Shader,1,&name);return name;}
    void APIENTRY DeleteShader(GLuint name){Retired(Shader,1,&name);deleteShader(name);}
    PFNGLCREATETEXTURESPROC createTextures;
    void APIENTRY CreateTextures(GLenum target,GLsizei count,GLuint* names){createTextures(target,count,names);Created(Texture,count,names);}
    PFNGLDRAWARRAYSPROC drawArrays;
    PFNGLDRAWELEMENTSPROC drawElements;
    PFNGLDRAWARRAYSINSTANCEDPROC drawArraysInstanced;
    PFNGLDRAWELEMENTSINSTANCEDPROC drawElementsInstanced;
    void APIENTRY Arrays(GLenum mode,GLint first,GLsizei count){Owner();if(activationInjected)++postFailureDraws;++draws;drawArrays(mode,first,count);}
    void APIENTRY Elements(GLenum mode,GLsizei count,GLenum type,const void* indices){Owner();if(activationInjected)++postFailureDraws;++draws;drawElements(mode,count,type,indices);}
    void APIENTRY ArraysInstanced(GLenum mode,GLint first,GLsizei count,GLsizei instances){Owner();if(activationInjected)++postFailureDraws;++draws;drawArraysInstanced(mode,first,count,instances);}
    void APIENTRY ElementsInstanced(GLenum mode,GLsizei count,GLenum type,const void* indices,GLsizei instances){Owner();if(activationInjected)++postFailureDraws;++draws;drawElementsInstanced(mode,count,type,indices,instances);}
    decltype(__imp_SDL_GL_CreateContext) nativeCreate;
    decltype(__imp_SDL_GL_MakeCurrent) nativeActivate;
    decltype(__imp_SDL_GL_DeleteContext) nativeDelete;
    decltype(__imp_SDL_CreateWindow) nativeWindow;
    decltype(__imp_SDL_DestroyWindow) nativeWindowDelete;
    SDL_GLContext SDLCALL CreateContext(SDL_Window* window) {
        Require(SDL_ThreadID()==ownerThread,"native create left owner thread");
        auto context=nativeCreate(window);
        if(context){Require(nativeContexts.insert(context).second,"native context registered twice");++nativeCreates;}
        return context;
    }
    int SDLCALL ActivateContext(SDL_Window* window,SDL_GLContext context) {
        Require(SDL_ThreadID()==ownerThread,"native activation left owner thread");
        if(activationArmed && context) {
            activationArmed=false;activationInjected=true;failureDraws=draws;
            return SDL_SetError("phase66 injected context activation; native=81; owner-preserved");
        }
        return nativeActivate(window,context);
    }
    void SDLCALL DeleteContext(SDL_GLContext context) {
        Require(SDL_ThreadID()==ownerThread,"native retirement left owner thread");
        if(context) {
            Require(nativeContexts.contains(context),"untracked native context retired");
            if(nativeContexts.size()==1)for(const auto& item:owners)Require(item.live.empty(),"GL owner survived final context");
            nativeContexts.erase(context);++nativeRetires;
        }
        nativeDelete(context);
    }
    SDL_Window* SDLCALL CreateNativeWindow(const char* title,int x,int y,int w,int h,Uint32 flags) {
        auto* window=nativeWindow(title,x,y,w,h,flags);
        if(window){Require(nativeWindows.insert(window).second,"native window owned twice");++windowCreates;}
        return window;
    }
    void SDLCALL DeleteWindow(SDL_Window* window) {
        if(window){Require(nativeWindows.erase(window)==1,"untracked native window retired");++windowRetires;}
        nativeWindowDelete(window);
    }
    template<class T> void PatchImport(T& slot,T replacement,T& original) {
        DWORD previousProtection{};
        Require(VirtualProtect(&slot,sizeof(slot),PAGE_READWRITE,&previousProtection)!=0,"cannot observe current-process SDL import");
        original=slot;slot=replacement;
        DWORD restoredProtection{};
        Require(VirtualProtect(&slot,sizeof(slot),previousProtection,&restoredProtection)!=0,"cannot restore SDL import protection");
    }
    void InstallNative() {
        ownerThread=SDL_ThreadID();activationArmed=mode=="context-activation";
        PatchImport(__imp_SDL_GL_CreateContext,&CreateContext,nativeCreate);
        PatchImport(__imp_SDL_GL_MakeCurrent,&ActivateContext,nativeActivate);
        PatchImport(__imp_SDL_GL_DeleteContext,&DeleteContext,nativeDelete);
        PatchImport(__imp_SDL_CreateWindow,&CreateNativeWindow,nativeWindow);
        PatchImport(__imp_SDL_DestroyWindow,&DeleteWindow,nativeWindowDelete);
    }
    void Install() {
        ++loads;Require(loads==1,"unexpected loader lifetime");ownerThread=SDL_ThreadID();
#define TRACK(K,Name) Names<K>::generate=glad_glGen##Name;Names<K>::destroy=glad_glDelete##Name;glad_glGen##Name=Names<K>::Generate;glad_glDelete##Name=Names<K>::Destroy
        TRACK(Buffer,Buffers);TRACK(Array,VertexArrays);TRACK(Texture,Textures);TRACK(Framebuffer,Framebuffers);TRACK(Renderbuffer,Renderbuffers);TRACK(Sampler,Samplers);
#undef TRACK
        createBuffers=glad_glCreateBuffers;glad_glCreateBuffers=CreateBuffers;
        createProgram=glad_glCreateProgram;glad_glCreateProgram=CreateProgram;
        deleteProgram=glad_glDeleteProgram;glad_glDeleteProgram=DeleteProgram;
        createShader=glad_glCreateShader;glad_glCreateShader=CreateShader;
        deleteShader=glad_glDeleteShader;glad_glDeleteShader=DeleteShader;
        createTextures=glad_glCreateTextures;glad_glCreateTextures=CreateTextures;
        drawArrays=glad_glDrawArrays;glad_glDrawArrays=Arrays;
        drawElements=glad_glDrawElements;glad_glDrawElements=Elements;
        drawArraysInstanced=glad_glDrawArraysInstanced;glad_glDrawArraysInstanced=ArraysInstanced;
        drawElementsInstanced=glad_glDrawElementsInstanced;glad_glDrawElementsInstanced=ElementsInstanced;
    }
    static_assert(std::is_same_v<decltype(::GEngine::EngineContext::TryCurrent()),
        std::expected<::GEngine::EngineContext*, ::GEngine::PlatformError>>);
    static_assert(std::is_same_v<decltype(std::declval<::GEngine::EngineContext&>().AssetPublications()),
        std::expected<::GEngine::Asset::AssetPublication*, ::GEngine::PlatformError>>);
    static_assert(std::is_same_v<decltype(::GEngine::Manager::ShapeManager::GetShape("Box")),
        ::GEngine::Manager::ShapeManager::LookupResult>);
    static_assert(std::is_same_v<decltype(::GEngine::Manager::ShapeManager::UnRegister("Box")), ::GEngine::PlatformResult>);
    class Application final : public RigidBodySimulationApp {
        std::shared_ptr<::GEngine::AAScreenMaterial> screenMaterial;
        void CheckScreenBinding() {
            auto attachment=m_RenderTarget->ColorView();Require(bool(attachment),"RBS target view lost");
            auto name=::GEngine::Asset::AssetDetail::TextureBackend::Name(::GEngine::Asset::TextureView(*attachment));
            Require(bool(name),"actual RBS attachment expired");
            GLint previousActive{};glGetIntegerv(GL_ACTIVE_TEXTURE,&previousActive);
            screenMaterial->BindTextureUniforms();glActiveTexture(GL_TEXTURE1);
            GLint bound{};glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
            Require(static_cast<GLuint>(bound)==*name,"screen view cached a stale or different RBS image");
            glActiveTexture(previousActive);++screenChecks;
        }
    public:
        ::GEngine::ApplicationInitializationResult Initialize(const std::initializer_list<::GEngine::WindowProperties>& props) override {
            ++initializes;
            if(mode=="root-success") {
                auto& root=GetEngineContext();
                auto current=::GEngine::EngineContext::TryCurrent();
                auto assets=root.Assets();auto shapes=root.Shapes();auto publication=root.AssetPublications();
                auto scene=root.SceneServices();auto activation=root.MakeCurrent();
                Require(current && *current==&root && !assets && !shapes && !publication && !scene && !activation,
                    "uninitialized root published services or lost registration");
                Require(assets.error().code==::GEngine::PlatformErrorCode::InvalidState
                    && assets.error().operation=="manager access" && assets.error().message=="EngineContext managers are not available",
                    "unavailable manager diagnostic changed");
            }
            auto initialized=RigidBodySimulationApp::Initialize(props);
            if(!initialized)return initialized;
            if(mode.starts_with("root-")) {
                using namespace ::GEngine;
                auto& root=GetEngineContext();
                auto assets=root.Assets();auto shapes=root.Shapes();auto publication=root.AssetPublications();auto scene=root.SceneServices();
                Require(assets && shapes && publication && scene && *shapes==&scene->shapes && *publication==&scene->publication,
                    "root services changed owner identity");
                Require(&EngineContext::Current()==&root && &BaseApp::GetEngine()==&root.LegacyEngine(),"legacy callback root identity changed");
                if(mode=="root-owner") {
                    std::optional<PlatformError> failure;
                    std::thread worker([&] {
                        auto lookup=EngineContext::TryCurrent();auto a=root.Assets();auto g=root.Shapes();auto p=root.AssetPublications();
                        auto sc=root.SceneServices();auto activate=root.MakeCurrent();auto shader=root.Shaders();
                        auto texture=Manager::AssetsManager::GetTexture("white");auto shape=Manager::ShapeManager::FindShape("Box");
                        Require(!lookup && !a && !g && !p && !sc && !activate && !shader && !texture && !shape,
                            "worker reached an owner service");
                        Require(sc.error().code==PlatformErrorCode::InvalidState && sc.error().operation=="scene resources"
                            && sc.error().message=="Scene resources require the owner thread","worker error lost cause");
                        failure=sc.error();
                    });worker.join();failureDraws=draws;
                    Require(bool(failure),"worker failure absent");return std::unexpected(ApplicationInitializationError{*failure});
                }
                if(mode=="root-manager-activation") {
                    auto* window=SDL_GL_GetCurrentWindow();
                    Require(SDL_GL_MakeCurrent(window,nullptr)==0 && !SDL_GL_GetCurrentContext(),"test context detach failed");
                    activationArmed=true;auto denied=root.AssetPublications();
                    Require(!denied && denied.error().code==PlatformErrorCode::ContextActivation,"manager activation error missing");
                    return std::unexpected(ApplicationInitializationError{denied.error()});
                }
                if(mode=="root-scene-activation") {
                    activationArmed=true;auto denied=root.SceneServices();
                    Require(!denied && denied.error().code==PlatformErrorCode::ContextActivation,"scene service activation error missing");
                    return std::unexpected(ApplicationInitializationError{denied.error()});
                }
                if(mode=="root-shader-activation") {
                    activationArmed=true;auto denied=root.Shaders();
                    Require(!denied && denied.error().code==Asset::ShaderErrorCode::ContextUnavailable
                        && denied.error().log.find("Platform operation=")!=std::string::npos
                        && denied.error().log.find("native=81; owner-preserved")!=std::string::npos,"shader mapping lost platform diagnostic");
                    return std::unexpected(ApplicationInitializationError{denied.error()});
                }
                if(mode=="root-activation") {
                    activationArmed=true;auto denied=root.MakeCurrent();
                    Require(!denied && denied.error().code==PlatformErrorCode::ContextActivation,"root activation error missing");
                    return std::unexpected(ApplicationInitializationError{denied.error()});
                }
                auto repeated=root.Initialize(props);
                Require(!repeated && root.IsReady() && std::get<PlatformError>(repeated.error()).message=="Initialization may only be attempted once",
                    "repeated initialization replaced owner resources");
                std::cout<<"[ROOT] unavailable/ready/repeated/legacy identities checked"<<std::endl;
            }
            if(mode=="shape-lookup" || mode=="shape-retire" || mode=="shape-owner") {
                auto shape=::GEngine::Manager::ShapeManager::FindShape("Box");
                auto missing=::GEngine::Manager::ShapeManager::FindShape("phase66-absent-shape");
                Require(shape && *shape && missing && !*missing,"typed lookup changed present/missing semantics");
                if(mode=="shape-owner") {
                    std::optional<::GEngine::Manager::ShapeManager::LookupResult> result;
                    std::thread worker([&]{result=::GEngine::Manager::ShapeManager::FindShape("Box");});worker.join();
                    Require(result && !*result && result->error().code==::GEngine::PlatformErrorCode::InvalidState
                        && result->error().operation.starts_with("ShapeManager::FindShape / ")
                        && result->error().message.find("owner thread")!=std::string::npos,"shape owner failure lost typed cause");
                    failureDraws=draws;return std::unexpected(::GEngine::ApplicationInitializationError{result->error()});
                }
                if(mode=="shape-retire") {
                    std::array<unsigned,KindCount> retired{};for(unsigned k=0;k<KindCount;++k)retired[k]=owners[k].retired;
                    Require(bool(::GEngine::Manager::ShapeManager::RetireShape("Box")),"shape retirement failed");
                    Require(bool(::GEngine::Manager::ShapeManager::RetireShape("Box")),"repeated retirement changed absent behavior");
                    auto absent=::GEngine::Manager::ShapeManager::FindShape("Box");Require(absent && !*absent,"retired lookup remained visible");
                    for(unsigned k=0;k<KindCount;++k)Require(retired[k]==owners[k].retired,"lookup retirement deleted a borrowed GPU owner");
                }
                std::cout<<"[SHAPE] typed lookup/retirement checked on actual RBS root"<<std::endl;
            }
            if(mode=="debug-shape-success" || mode=="debug-shape-missing") {
                using namespace ::GEngine;
                auto bounds=Component::DebugAABBBoundingBoxMeshComponent::Create();
                auto tree=Component::DebugKDTreeVisualizer::Create();
                Require(bounds && tree && bounds->m_AABB==*Manager::ShapeManager::FindShape("AABBBoundingBox")
                    && tree->m_KDTree==*Manager::ShapeManager::FindShape("KDTreeVisualizer"),"debug helper borrowed a different owner");
                if(mode=="debug-shape-missing") {
                    Require(bool(Manager::ShapeManager::RetireShape("KDTreeVisualizer")),"debug shape lookup retirement failed");
                    auto denied=Component::DebugKDTreeVisualizer::Create();
                    Require(!denied && denied.error().code==PlatformErrorCode::InvalidState
                        && denied.error().operation=="DebugKDTreeVisualizer::Create"
                        && denied.error().message=="Required KDTreeVisualizer geometry is unavailable","debug helper missing-shape diagnostic changed");
                    failureDraws=draws;return std::unexpected(ApplicationInitializationError{denied.error()});
                }
            }
            if(!mode.starts_with("screen-"))return initialized;
            auto attachment=m_RenderTarget->ColorView();Require(bool(attachment),"actual RBS color view unavailable");
            const auto textureCreates=owners[Texture].generated;
            const auto view=mode=="screen-invalid" ? ::GEngine::Asset::TextureView{} : ::GEngine::Asset::TextureView(*attachment);
            auto material=::GEngine::AAScreenMaterial::Create(view,
                mode=="screen-shader" ? "Shaders/phase66-screen-missing.vert" : "Shaders/aa_post.vert");
            Require(owners[Texture].generated==textureCreates,"screen material copied the actual RBS image");
            if(!material) {
                failureDraws=draws;
                if(auto* shader=std::get_if<::GEngine::Asset::ShaderError>(&material.error())) {
                    Require(mode=="screen-shader" && !shader->source.empty() && !shader->log.empty(),"screen shader cause lost");
                    return std::unexpected(::GEngine::ApplicationInitializationError{*shader});
                }
                const auto& sampling=std::get<::GEngine::Asset::SamplingError>(material.error());
                const auto* texture=std::get_if<::GEngine::Asset::TextureError>(&sampling.cause);
                Require(mode=="screen-invalid" && texture && texture->code==::GEngine::Asset::TextureErrorCode::InvalidView
                    && !texture->message.empty(),"screen texture cause lost");
                return std::unexpected(::GEngine::ApplicationInitializationError{*texture});
            }
            Require(mode=="screen-success","screen failure did not reject candidate");
            screenMaterial=std::move(*material);CheckScreenBinding();
            return {};
        }
        ::GEngine::ApplicationRunResult Run() override {++runs;if(mode=="context-loop")activationArmed=true;return RigidBodySimulationApp::Run();}
        void Update(::GEngine::Timestep step) override {++updates;RigidBodySimulationApp::Update(step);
            if(mode=="context-runtime" && updates==1)activationArmed=true;}
        void Render() override {
            ++renders;RigidBodySimulationApp::Render();
            Require(!GetWindow()->Timings().Active() && !GImGui->WithinFrameScope,"frame/UI lifetime escaped scheduler");
            if(screenMaterial)CheckScreenBinding();
            if(renders==2)ShutDown();
        }
        ~Application() override {++destructors;if(GetEngineContext().IsReady() && mode!="root-manager-activation")Require(SDL_GL_GetCurrentContext()!=nullptr,"application retired after context");}
    };
}
void* operator new(std::size_t size,const std::nothrow_t&) noexcept
{
    using namespace RbsValidation;
    if(mode=="context-registration" && !registrationInjected && loads==0
        && size==sizeof(::GEngine::GLContextThread::Detail::Registration) && SDL_GL_GetCurrentContext())
    {registrationInjected=true;return nullptr;}
    return std::malloc(size ? size : 1);
}
void operator delete(void* value,const std::nothrow_t&) noexcept {std::free(value);}
extern "C" int Phase66RealGladLoadGL(void);
extern "C" int gladLoadGL(void){const int result=Phase66RealGladLoadGL();if(result)RbsValidation::Install();return result;}
::GEngine::BaseApp* CreateApp(){return new RbsValidation::Application;}
int main(int argc,char** argv)
{
    using namespace RbsValidation;
    if(argc!=2)return 2;mode=argv[1];
    if(mode!="success" && mode!="screen-success" && mode!="screen-invalid" && mode!="screen-shader"
        && mode!="context-registration" && mode!="context-activation" && mode!="context-runtime" && mode!="context-loop" && mode!="shape-lookup" && mode!="shape-retire" && mode!="shape-owner" && mode!="debug-shape-success" && mode!="debug-shape-missing" && mode!="root-success" && mode!="root-duplicate" && mode!="root-owner"
        && mode!="root-manager-activation" && mode!="root-scene-activation" && mode!="root-shader-activation" && mode!="root-activation")return 2;
    const bool failure=mode=="screen-invalid" || mode=="screen-shader" || mode=="context-registration" || mode=="context-activation" || mode=="shape-owner" || mode=="debug-shape-missing" || (mode.starts_with("root-") && mode!="root-success");
    const bool runtimeFailure=mode=="context-runtime" || mode=="context-loop";
    InstallNative();
    auto absent=::GEngine::EngineContext::TryCurrent();
    Require(!absent && absent.error().operation=="root lookup" && absent.error().message=="No live application EngineContext",
        "outside-lifetime root lookup did not preserve typed cause");
    std::unique_ptr<::GEngine::EngineContext> existingRoot;
    if(mode=="root-duplicate")existingRoot=std::make_unique<::GEngine::EngineContext>();
    SDL_SetMainReady();const auto status=ProductionRbsEntryPoint(argc,argv);
    if(existingRoot) {
        Require(::GEngine::EngineContext::TryGet()==existingRoot.get() && existingRoot->GetState()==::GEngine::EngineContext::State::Uninitialized
            && nativeCreates==0 && windowCreates==0,"duplicate root replaced/unregistered original or started platform");
        existingRoot.reset();
    }
    std::cout<<"[CALLBACK] status="<<status<<" initializes="<<initializes<<" runs="<<runs<<" updates="<<updates<<" renders="<<renders<<" destructors="<<destructors<<std::endl;
    Require(initializes==1 && destructors==1,"real application lifetime contract");
    Require(failure ? status==1 && runs==0 && renders==0 && updates==0 && draws==failureDraws
        : runtimeFailure ? status==1 && runs==1 && draws==failureDraws
            && (mode=="context-loop" ? renders==0 && updates==0 : renders==1 && updates>0)
        : status==0 && runs==1 && renders==2 && updates>0,"real application callback/exit contract");
    Require(mode!="screen-success" || screenChecks==3,"screen binding not observed through real RBS frames");
    std::cout<<"[OBSERVE] RBS draws="<<draws<<" ownership-valid="<<valid<<std::endl;
    Require(valid && (failure || runtimeFailure || draws>0),"draws/GL ownership invalid");
    for(unsigned kind=0;kind<KindCount;++kind){
        const auto& item=owners[kind];Require(item.live.empty() && item.generated==item.retired,"native resource not retired exactly once");
        std::cout<<"[OWNER] "<<labels[kind]<<"="<<item.generated<<"/"<<item.retired<<" live=0"<<std::endl;
    }
    Require(::GEngine::EngineContext::TryGet()==nullptr && SDL_WasInit(0)==0 && TTF_WasInit()==0,"root/platform retirement incomplete");
    Require(::GEngine::GLContextThread::Detail::Contexts().contexts.empty(),"GL context registration survived shutdown");
    Require(nativeContexts.empty() && nativeWindows.empty() && nativeCreates==nativeRetires && windowCreates==windowRetires,
        "native window/context ownership escaped application");
    Require(!activationInjected || postFailureDraws==0,"post-failure draw/submission");
    if(mode=="context-registration")Require(registrationInjected && loads==0 && nativeCreates==1 && nativeRetires==1,
        "registration failure did not roll back exactly once before loader (or debug fallback hid it)");
    if(mode=="context-activation" || runtimeFailure)Require(activationInjected,"native activation failure was not reached");
    std::cout<<"[PLATFORM] contexts="<<nativeCreates<<"/"<<nativeRetires<<" windows="<<windowCreates<<"/"<<windowRetires
        <<" post-failure-draws="<<postFailureDraws<<std::endl;
    std::cout<<"[PASS] RBS "<<mode<<" screen-checks="<<screenChecks<<" real-update="<<updates<<" real-render="<<renders<<" draws="<<draws<<" owner=1 root=0 contexts=0 SDL=0 TTF=0"<<std::endl;
    return status;
}
