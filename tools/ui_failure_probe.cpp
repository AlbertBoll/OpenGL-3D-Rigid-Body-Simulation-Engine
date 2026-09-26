#include "Core/Window.h"
#include "Renderer/FrameScheduler.h"
#include <type_traits>
static_assert(std::same_as<decltype(std::declval<GEngine::Window&>().EndUI()), GEngine::PlatformResult>);
#ifdef UI_FAILURE_AFTER
static_assert(std::same_as<decltype(std::declval<GEngine::Window&>().EndUI(GEngine::UIFrameDisposition::Discard)), GEngine::PlatformResult>);
#endif
#ifndef UI_FAILURE_SCHEMA_ONLY
#define main Phase66UnusedShutdownMain
#include "shutdown_probe.cpp"
#undef main
#include <imgui/imgui_internal.h>
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
namespace UiFailure
{
    using namespace GEngine;
    std::string_view mode;
    unsigned runs{}, renders{}, authors{}, destructors{};
    bool failNext{}, submittedFailure{}, balanced=true;
    int authoredFrame{}, renderedBefore{}, renderedAfter{};
    std::unique_ptr<ModelNames> names;
    SceneError Failure() {return {SceneErrorCode::InvalidProgram,"ui-fixture-authoring",
        "complete UI authoring failure: source=ray image; upload unavailable",37};}
    class App final : public BaseApp
    {
    public:
        ApplicationInitializationResult Initialize(const std::initializer_list<WindowProperties>& props) override
        {
            auto result=BaseApp::Initialize(props);if(!result)return result;
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
                const auto& cause=std::get<SceneError>(result.error().cause);auto expected=Failure();
                Check(cause.code==expected.code && cause.operation==expected.operation
                    && cause.message==expected.message && cause.entity==37,"Original UI failure diagnostic lost");
                if(mode=="recovery") {
                    failNext=false;auto recovered=Frame();Check(bool(recovered),"Balanced UI failure prevented explicit later frame");ShutDown();
                } else FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"UI","4",
                    std::string(cause.operation),"entity=37",DescribeScheduleError(result.error())});
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
    const int result=Phase66RealGladLoadGL();if(result)UiFailure::names=std::make_unique<ModelNames>();return result;
}
::GEngine::WindowProperties winProp=Properties();
::GEngine::BaseApp* CreateApp(){return new UiFailure::App;}
int main(int argc,char** argv)
{
    using namespace UiFailure;
    if(argc!=2)return 2;mode=argv[1];if(mode!="failure" && mode!="success" && mode!="recovery")return 2;
    SDL_SetMainReady();const auto status=ProductionEntryPoint(argc,argv);
    Check(status==(mode=="failure"?1:0) && runs==1 && renders==1 && destructors==1,"UI entry/exit/retirement failed");
    Check(balanced && authors==(mode=="recovery"?2u:1u),"UI scope/timing retirement failed");
    Check(names!=nullptr,"Loader not reached");names->Empty();names.reset();PlatformGone();
    std::cout<<"[OBSERVE] UI "<<mode<<" exit="<<status<<" submitted-after-failure="<<submittedFailure
        <<" before="<<renderedBefore<<" after="<<renderedAfter<<" authored="<<authoredFrame
        <<" buffers="<<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers<<" arrays="
        <<ModelNames::generatedArrays<<"/"<<ModelNames::retiredArrays<<" owner=1 root=0 SDL=0 TTF=0"<<std::endl;
    if(submittedFailure){std::cerr<<"[FAIL] Scheduler submitted a typed-failed UI frame"<<std::endl;return 2;}
    std::cout<<"[PASS] UI failure retirement "<<mode<<" balanced=1 no-failed-submit=1"<<std::endl;return status;
}
#endif
