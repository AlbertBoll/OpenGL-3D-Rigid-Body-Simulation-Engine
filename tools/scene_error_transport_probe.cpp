#include "Core/RenderTarget.h"
#include "Renderer/FrameScheduler.h"
#include <type_traits>
static_assert(std::is_constructible_v<::GEngine::ApplicationInitializationError, ::GEngine::SceneError>);
static_assert(std::is_constructible_v<::GEngine::ScheduleCause, ::GEngine::SceneError>);
static_assert(std::is_same_v<::GEngine::ScheduleResult, std::expected<void, ::GEngine::ScheduleError>>);
static_assert(std::is_same_v<decltype(::GEngine::SceneError::transform), std::optional<::GEngine::TransformError>>);
static_assert(std::is_trivially_copyable_v<::GEngine::SceneError>);
#ifndef SCENE_TRANSPORT_SCHEMA_ONLY
#define main Phase66UnusedShutdownMain
#include "shutdown_probe.cpp"
#undef main
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
#include <format>
namespace SceneTransport
{
    using namespace GEngine;
    std::string_view mode;
    unsigned runs{}, renders{}, updates{}, legacy{}, ui{}, destructors{}, callbackRetired{};
    std::unique_ptr<ModelNames> names;
    spdlog::logger* logger{};
    std::vector<spdlog::sink_ptr> sinks;
    spdlog::level::level_enum level{};
    SceneError Failure()
    {
        if (mode.ends_with("-parent")) return {SceneErrorCode::Parenting, "scene-fixture-operation",
            "full scene diagnostic: source=fixture; entity ownership unavailable", 37,
            TransformError{TransformErrorCode::IdentityExhausted, UUID(37), UUID(41)}};
        return {mode=="startup-missing" ? SceneErrorCode::MissingIdentity : SceneErrorCode::ForeignEntity,
            "scene-fixture-operation", "full scene diagnostic: source=fixture; entity ownership unavailable", 37};
    }
    void CheckError(const SceneError& error)
    {
        auto expected=Failure();
        Check(error.code==expected.code && error.operation==expected.operation && error.message==expected.message
            && error.entity==expected.entity,"Scene diagnostic payload was lost");
        Check(error.transform.has_value()==expected.transform.has_value(),"Nested transform cause presence changed");
        if (expected.transform) Check(error.transform->code==expected.transform->code
            && error.transform->entity==expected.transform->entity && error.transform->parent==expected.transform->parent,
            "Nested transform cause fields were lost");
    }
    class App final : public BaseApp
    {
    public:
        ApplicationInitializationResult Initialize(const std::initializer_list<WindowProperties>& props) override
        {
            auto initialized=BaseApp::Initialize(props);if(!initialized)return initialized;
            const auto& core=Log::GetCoreLogger();logger=core.get();sinks=core->sinks();level=core->level();
            Check(core->name()=="GENGINE","Scene diagnostic category changed");
            core->set_level(spdlog::level::critical);
            ReportApplicationError(ApplicationInitializationError{SceneError{SceneErrorCode::ForeignEntity,
                "phase66-filtered-scene","phase66-filtered-message",99}});
            core->set_level(level);
            ApplicationInitializationError copied=Failure();auto moved=std::move(copied);
            CheckError(std::get<SceneError>(moved));
            ScheduleError scheduled{FrameStage::UpdateFrameResources,Failure()};auto scheduleCopy=scheduled;
            CheckError(std::get<SceneError>(scheduleCopy.cause));
            const auto description=DescribeScheduleError(scheduleCopy);
            Check(description.find("operation=scene-fixture-operation")!=std::string::npos
                && description.find("entity=37")!=std::string::npos
                && description.find(Failure().message)!=std::string::npos,"Scheduler diagnostic is incomplete");
            if (mode.ends_with("-parent")) Check(description.find("transform-code=7")!=std::string::npos
                && description.find("transform-entity=37")!=std::string::npos
                && description.find("transform-parent=41")!=std::string::npos,"Scheduler nested cause is incomplete");
            if(mode.starts_with("startup-"))return std::unexpected(Failure());
            SDL_Event event{};while(SDL_PollEvent(&event)){}
            m_WindowHidden=false;SetManualFrameRateLimit(0);
            return {};
        }
        void ProcessInput(Timestep) override {}
        void Update(Timestep) override {}
        void Render() override
        {
            ++renders;
            RenderContext context{*GetWindow(),*GetWindowManager()};
            context.updateResources={nullptr,[](void*)->ScheduleResult {
                ++updates;
                struct Retire {~Retire(){++callbackRetired;}} retire;
                if((mode=="runtime-failure" || mode=="runtime-parent"))return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources,Failure()});
                return {};
            }};
            context.legacyScene={nullptr,[](void*)->ScheduleResult {++legacy;return {};}};
            context.editorUI={nullptr,[](void*)->ScheduleResult {++ui;return {};}};
            auto result=FrameScheduler::Render(context);
            Check(!GetWindow()->Timings().Active(),"Scheduler failed to retire frame scope");
            Check(callbackRetired==1,"Resource callback owner was not retired once");
            if((mode=="runtime-failure" || mode=="runtime-parent")) {
                Check(!result && result.error().stage==FrameStage::UpdateFrameResources,
                    "Resource failure lost its stage");
                const auto& cause=std::get<SceneError>(result.error().cause);CheckError(cause);
                Check(legacy==0 && ui==0,"Scheduler ran post-failure render/UI");
                FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"scene",std::to_string(int(cause.code)),
                    std::string(cause.operation),std::format("entity={}",cause.entity),DescribeScheduleError(result.error())});
            } else {
                Check(result.has_value() && legacy==1 && ui==1,"Successful scheduler callback sequence changed");
                ShutDown();
            }
        }
        ApplicationRunResult Run() override
        {
            ++runs;auto result=BaseApp::Run();
            Check(result.has_value()==(mode=="success"),"Scene runtime status was lost");
            if(!result) {
                Check(result.error().operation==Failure().operation && result.error().context=="entity=37"
                    && result.error().message.find(Failure().message)!=std::string::npos,"Runtime diagnostic was lost");
                auto repeated=BaseApp::Run();
                Check(!repeated && renders==1 && updates==1,"Failed runtime resumed rendering");
            }
            return result;
        }
        ~App() override {++destructors;Check(SDL_GL_GetCurrentContext()!=nullptr,"Scene app outlived its context");}
    };
}
extern "C" int Phase66RealGladLoadGL(void);
extern "C" int gladLoadGL(void)
{
    const int result=Phase66RealGladLoadGL();
    if(result)SceneTransport::names=std::make_unique<ModelNames>();
    return result;
}
::GEngine::WindowProperties winProp=Properties();
::GEngine::BaseApp* CreateApp(){return new SceneTransport::App;}
int main(int argc,char** argv)
{
    using namespace SceneTransport;
    if(argc!=2)return 2;mode=argv[1];
    if(mode!="startup-foreign" && mode!="startup-missing" && mode!="startup-parent"
        && mode!="runtime-failure" && mode!="runtime-parent" && mode!="success")return 2;
    SDL_SetMainReady();const int status=ProductionEntryPoint(argc,argv);
    const bool startup=mode.starts_with("startup-");
    Check(status==(mode=="success"?0:1) && runs==(startup?0u:1u)
        && renders==(startup?0u:1u) && updates==(startup?0u:1u) && destructors==1,
        "Scene failure exit/Run/render/teardown order changed");
    Check(names!=nullptr,"GL loader lifetime gate was not reached");names->Empty();names.reset();PlatformGone();
    Check(ModelNames::generatedBuffers>=108 && ModelNames::generatedArrays>=30,"Base GPU owners were not covered");
    const auto& core=::GEngine::Log::GetCoreLogger();
    Check(core.get()==logger && core->sinks()==sinks && core->level()==level,"Scene reporter changed logger/sinks/filtering");
    std::cout<<"[PASS] scene transport "<<mode<<" exit="<<status<<" runs="<<runs<<" renders="<<renders
        <<" updates="<<updates<<" legacy="<<legacy<<" ui="<<ui<<" callback-retired="<<callbackRetired
        <<" buffers="<<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers
        <<" arrays="<<ModelNames::generatedArrays<<"/"<<ModelNames::retiredArrays<<" owner=1 root=0 SDL=0 TTF=0 teardown=1\n";
    return status;
}
#endif
