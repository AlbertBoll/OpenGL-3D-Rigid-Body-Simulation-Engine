#include "Core/RenderTarget.h"
#include <type_traits>
static_assert(std::is_constructible_v<::GEngine::ApplicationInitializationError, ::GEngine::ModelImportError>);
#ifndef MODEL_IMPORT_SCHEMA_ONLY
#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/Scene.h"
#include <sdl2/SDL_ttf.h>
#include <print>
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
namespace Fixture
{
    bool fail=false,valid=true;
    unsigned runs=0,destructors=0;
    void Check(bool ok,const char* reason){if(!ok){valid=false;std::println("[FAIL] {}",reason);}}
    class App final : public ::GEngine::BaseApp
    {
    public:
        ::GEngine::ApplicationInitializationResult Initialize(const std::initializer_list<::GEngine::WindowProperties>& props) override
        {
            auto initialized=BaseApp::Initialize(props);if(!initialized)return initialized;
            if(fail)
            {
                ::GEngine::ModelImportError error{::GEngine::ModelImportErrorCode::ImportFailed,
                    "ModelImportFixture::Create","Models/fixture.obj","Importer rejected model; detail=invalid face 37"};
                ::GEngine::ApplicationInitializationError transport=error;
                const auto* observed=std::get_if<::GEngine::ModelImportError>(&transport);
                Check(observed && observed->code==error.code && observed->operation==error.operation
                    && observed->source==error.source && observed->message==error.message,"Startup variant lost model diagnostics");
                return std::unexpected(std::move(transport));
            }
            return {};
        }
        ::GEngine::ApplicationRunResult Run() override {++runs;return {};}
        ~App() override {++destructors;Check(SDL_GL_GetCurrentContext()!=nullptr,"Application retired after its context");}
    };
}
::GEngine::WindowProperties winProp=[] {
    ::GEngine::WindowProperties p;p.m_Title="Model startup error validation";p.m_Width=p.m_Height=64;
    p.m_MinWidth=p.m_MinHeight=32;p.m_IsVsync=false;
    p.flag=::GEngine::BitFlags<::GEngine::WindowFlags,uint8_t>{::GEngine::WindowFlags::INVISIBLE};return p;
}();
::GEngine::BaseApp* CreateApp(){return new Fixture::App;}
int main(int argc,char* argv[])
{
    if(argc!=2)return 2;
    const std::string_view mode=argv[1];if(mode!="success"&&mode!="failure")return 2;
    Fixture::fail=mode=="failure";SDL_SetMainReady();
    const int status=ProductionEntryPoint(argc,argv);
    Fixture::Check(status==(Fixture::fail?1:0),"Actual EntryPoint exit status changed");
    Fixture::Check(Fixture::runs==(Fixture::fail?0u:1u)&&Fixture::destructors==1,"Post-failure Run or missing application retirement");
    Fixture::Check(!::GEngine::EngineContext::TryGet() && SDL_WasInit(0)==0 && TTF_WasInit()==0,"Root/platform survived model failure");
    if(!Fixture::valid)return 97;
    std::println("[PASS] model startup {} exit={} runs={} teardown=1",mode,status,Fixture::runs);return status;
}
#endif
