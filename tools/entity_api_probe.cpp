#include <concepts>
#include <exception>
#include <functional>
// CPU fixture linked to the production GEngine library; no GL context is required.
#include <Scene/_Entity.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <limits>
#include <cstdlib>

#if defined(SCENE_CREATION_RBS_PROBE)
#include "Physics/ShapeSphere.h"
#include "Physics/ShapeBox.h"
#include "Physics/ShapeConvex.h"
#define main Phase66UnusedShutdownMain
#include "shutdown_probe.cpp"
#undef main
#include "Physics/PhysicsWorld.h"
#include "Physics/PhysicsSystem.h"
namespace RbsShapeAudit
{
    void Require(bool valid,const char* message)
    { if(!valid){std::cerr<<"[FAIL] "<<message<<std::endl;std::exit(2);} }
    bool fail=false;
    unsigned queries=0,created=0,retired=0,spheres=0,boxes=0;
    std::unordered_set<const void*> live;
    void Created(const void* pointer,bool box)
    {RbsShapeAudit::Require(live.insert(pointer).second,"Duplicate concrete startup owner");++created;if(box)++boxes;else ++spheres;}
    void Retired(const void* pointer)
    {RbsShapeAudit::Require(live.erase(pointer)==1,"Concrete startup owner retired twice");++retired;}
}
namespace GEngine
{
    class Phase66StartupSphere final : public ShapeSphere
    {
    public:
        explicit Phase66StartupSphere(ShapeSphere&& shape) : ShapeSphere(std::move(shape))
        {RbsShapeAudit::Created(this,false);}
        ~Phase66StartupSphere() override {RbsShapeAudit::Retired(this);}
        static std::expected<ShapeSphere,PhysicsShapeError> Create(float radius)
        {
            ++RbsShapeAudit::queries;
            return ShapeSphere::Create(RbsShapeAudit::fail && RbsShapeAudit::queries==3 ? -2.5f : radius);
        }
    };
    class Phase66StartupBox final : public ShapeBox
    {
    public:
        explicit Phase66StartupBox(ShapeBox&& shape) : ShapeBox(std::move(shape))
        {RbsShapeAudit::Created(this,true);}
        ~Phase66StartupBox() override {RbsShapeAudit::Retired(this);}
    };
}
// Instrument only the test's private Scene translation unit. Factory failure is the
// real typed invalid-radius result; application and production library stay unchanged.
#define ShapeSphere Phase66StartupSphere
#define ShapeBox Phase66StartupBox
#include "../GEngine/src/Scene/_Scene.cpp"
#undef ShapeBox
#undef ShapeSphere
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
#define CreateApp Phase66UnusedRbsFactory
#include "../RigidBodySimulation/src/RigidBodySimulation.cpp"
#undef CreateApp
namespace SceneRbsAudit
{
    std::string_view mode;
    unsigned runs{}, renders{}, ui{}, destructors{}, uuidQueries{}, sceneQueries{}, failAt{};
    RigidBodySimulationApp* activeApp{};
    template<typename Tag, typename Tag::Type Member>
    struct Access { friend typename Tag::Type SceneMember(Tag) { return Member; } };
    struct ActiveSceneTag {
        using Type = RefPtr<_Scene> RigidBodySimulationApp::*;
        friend Type SceneMember(ActiveSceneTag);
    };
    template struct Access<ActiveSceneTag, &RigidBodySimulationApp::m_ActiveScene>;
    bool HasActiveScene() { return activeApp && bool(activeApp->*SceneMember(ActiveSceneTag{})); }
    bool runtimeArmed{}, injected{};
    std::uint64_t drawCalls{}, drawsAtFailure{};
    PFNGLDRAWARRAYSPROC drawArrays{};
    PFNGLDRAWELEMENTSPROC drawElements{};
    PFNGLDRAWARRAYSINSTANCEDPROC drawArraysInstanced{};
    PFNGLDRAWELEMENTSINSTANCEDPROC drawElementsInstanced{};
    void APIENTRY Arrays(GLenum mode, GLint first, GLsizei count)
    {++drawCalls;drawArrays(mode,first,count);}
    void APIENTRY Elements(GLenum mode, GLsizei count, GLenum type, const void* offset)
    {++drawCalls;drawElements(mode,count,type,offset);}
    void APIENTRY ArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
    {++drawCalls;drawArraysInstanced(mode,first,count,instances);}
    void APIENTRY ElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* offset, GLsizei instances)
    {++drawCalls;drawElementsInstanced(mode,count,type,offset,instances);}
    std::unique_ptr<ModelNames> names;
    class App final : public RigidBodySimulationApp
    {
    public:
        ApplicationInitializationResult Initialize(const std::initializer_list<WindowProperties>& props) override
        {
            auto result=RigidBodySimulationApp::Initialize(props);
            if(mode.starts_with("startup-")) {
                RbsShapeAudit::Require(!result,"Actual RBS accepted injected creation failure");
                const auto* error=std::get_if<SceneError>(&result.error());
                RbsShapeAudit::Require(error && error->code==SceneErrorCode::InvalidIdentity && error->entity==0
                    && error->operation=="_Scene::CreateEntityWithUUID"
                    && error->message=="Entity UUID must be nonzero and unique in its scene",
                    "Actual RBS startup lost complete Scene diagnostics");
            } else RbsShapeAudit::Require(result.has_value(),"Actual RBS startup unexpectedly failed");
            return result;
        }
        void ProcessInput(Timestep) override {}
        void Update(Timestep) override {}
        void ImGuiRender() override {++ui;}
        ApplicationRunResult Run() override
        {
            ++runs;runtimeArmed=mode=="runtime-failure";
            SDL_Event event{};while(SDL_PollEvent(&event)){}
            m_WindowHidden=false;SetManualFrameRateLimit(0);
            auto result=BaseApp::Run();
            RbsShapeAudit::Require(result.has_value()==(mode=="success"),"Actual RBS runtime status changed");
            if(!result) {
                const auto& error=result.error();
                RbsShapeAudit::Require(error.subsystem=="Scene" && error.subsystemCode=="3"
                    && error.operation=="_Scene::CreateEntityWithUUID" && error.context=="entity=0"
                    && error.message.find("Entity UUID must be nonzero and unique in its scene")!=std::string::npos,
                    "Actual RBS runtime lost complete Scene diagnostics");
                const auto previous=renders;auto again=BaseApp::Run();
                RbsShapeAudit::Require(!again && renders==previous,"Failed RBS resumed rendering");
            }
            return result;
        }
        void Render() override
        {
            ++renders;const auto previousUI=ui;
            RbsShapeAudit::Require(renders<=512,"Bounded actual RBS async import never reached creation gate");
            RigidBodySimulationApp::Render();
            RbsShapeAudit::Require(!GetWindow()->Timings().Active(),"RBS frame lifetime escaped scheduler");
            if(injected) RbsShapeAudit::Require(drawCalls==drawsAtFailure && ui==previousUI,
                "RBS performed post-failure rendering or UI");
            if(mode=="success")ShutDown();
        }
        ~App() override {++destructors;RbsShapeAudit::Require(SDL_GL_GetCurrentContext()!=nullptr,"RBS outlived context");}
    };
}
namespace GEngine
{
    UUID::UUID() : m_UUID(10000 + ++SceneRbsAudit::uuidQueries)
    {
        const bool sceneCreation = SceneRbsAudit::HasActiveScene();
        if(sceneCreation)++SceneRbsAudit::sceneQueries;
        if(sceneCreation && (SceneRbsAudit::runtimeArmed
            || (SceneRbsAudit::failAt && SceneRbsAudit::sceneQueries==SceneRbsAudit::failAt))) {
            m_UUID=0;SceneRbsAudit::injected=true;SceneRbsAudit::drawsAtFailure=SceneRbsAudit::drawCalls;
        }
    }
    UUID::UUID(std::uint64_t id) : m_UUID(id) {}
}
extern "C" int Phase66RealGladLoadGL(void);
extern "C" int gladLoadGL(void)
{
    const int result=Phase66RealGladLoadGL();
    if(result) {
        using namespace SceneRbsAudit;
        names=std::make_unique<ModelNames>();
        drawArrays=glad_glDrawArrays;glad_glDrawArrays=Arrays;
        drawElements=glad_glDrawElements;glad_glDrawElements=Elements;
        drawArraysInstanced=glad_glDrawArraysInstanced;glad_glDrawArraysInstanced=ArraysInstanced;
        drawElementsInstanced=glad_glDrawElementsInstanced;glad_glDrawElementsInstanced=ElementsInstanced;
    }
    return result;
}
::GEngine::BaseApp* CreateApp(){auto* app=new SceneRbsAudit::App;SceneRbsAudit::activeApp=app;return app;}
int main(int argc,char** argv)
{
    using namespace SceneRbsAudit;
    if(argc!=2)return 2;
    mode=argv[1];if(mode!="success" && mode!="startup-early" && mode!="startup-late" && mode!="runtime-failure")return 2;
    failAt=mode=="startup-early"?1:mode=="startup-late"?12:0;
    if(mode=="runtime-failure")_putenv_s("GENGINE_ASYNC_MESH_SMOKE","1");
    SDL_SetMainReady();const int status=ProductionEntryPoint(argc,argv);
    const bool startup=mode.starts_with("startup-");
    RbsShapeAudit::Require(status==(mode=="success"?0:1) && runs==(startup?0u:1u)
        && (startup?renders==0:renders>0) && destructors==1,"RBS entry/startup/run/retirement contract failed");
    RbsShapeAudit::Require(names && ModelNames::valid && ModelNames::buffers.empty() && ModelNames::arrays.empty()
        && ModelNames::generatedBuffers==ModelNames::retiredBuffers && ModelNames::generatedArrays==ModelNames::retiredArrays,
        "RBS GL resources leaked or retired outside their owning context");
    names->Empty();names.reset();PlatformGone();
    RbsShapeAudit::Require(RbsShapeAudit::created==RbsShapeAudit::retired && RbsShapeAudit::live.empty(),
        "RBS shape owners leaked or retired twice");
    if(!startup)RbsShapeAudit::Require(RbsShapeAudit::spheres>0 && RbsShapeAudit::boxes>0,"Mixed shape ownership not covered");
    std::cout<<"[PASS] Scene actual RBS "<<mode<<" exit="<<status<<" runs="<<runs<<" renders="<<renders
        <<" concrete="<<RbsShapeAudit::created<<"/"<<RbsShapeAudit::retired<<" live=0 buffers="
        <<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers<<" arrays="<<ModelNames::generatedArrays
        <<"/"<<ModelNames::retiredArrays<<" owner=1 root=0 SDL=0 TTF=0"<<std::endl;
    return status;
}
#elif defined(SCENE_CREATION_SCHEMA_ONLY)
static_assert(std::same_as<decltype(GEngine::_Scene::Copy({})), std::expected<GEngine::RefPtr<GEngine::_Scene>, GEngine::SceneError>>);
static_assert(std::same_as<decltype(std::declval<GEngine::_Scene&>().CreateEntity()), std::expected<GEngine::_Entity, GEngine::SceneError>>);
static_assert(std::same_as<decltype(std::declval<GEngine::_Scene&>().DuplicateEntity({})), std::expected<GEngine::_Entity, GEngine::SceneError>>);
static_assert(std::same_as<decltype(std::declval<GEngine::_Scene&>().PushToRenderList()), std::expected<void, GEngine::SceneError>>);
static_assert(std::same_as<decltype(std::declval<GEngine::_Scene&>().PushToRenderList(GEngine::_Entity{}, GEngine::_Entity{})), std::expected<void, GEngine::SceneError>>);
static_assert(std::is_trivially_copyable_v<GEngine::SceneError>);
#elif defined(SCENE_DESTROY_SCHEMA_ONLY)
static_assert(std::same_as<decltype(std::declval<GEngine::_Scene&>().DestroyEntity(std::declval<GEngine::_Entity>())),
    std::expected<void, GEngine::SceneError>>);
static_assert(std::same_as<decltype(std::declval<GEngine::_Scene&>().DestroyEntity(GEngine::UUID(0))),
    std::expected<void, GEngine::SceneError>>);
static_assert(std::is_trivially_copyable_v<GEngine::SceneError>);
static_assert(std::is_nothrow_destructible_v<std::expected<void, GEngine::SceneError>>);
#elif defined(ENTITY_CHILDREN_SCHEMA_ONLY)
static_assert(std::is_same_v<decltype(std::declval<GEngine::_Entity&>().Children()),
    std::expected<std::reference_wrapper<std::vector<GEngine::UUID>>, GEngine::EntityChildrenError>>);
static_assert(std::is_same_v<decltype(std::declval<const GEngine::_Entity&>().Children()), const std::vector<GEngine::UUID>&>);
static_assert(std::is_trivially_copyable_v<GEngine::EntityChildrenError>);
static_assert(std::is_nothrow_destructible_v<decltype(std::declval<GEngine::_Entity&>().Children())>);
#else
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/ShapeSphere.h"
int RunProgramGroupingRegression();

namespace
{
    // Test-only preparation for bounded Scene value/void result migrations.
    template<std::invocable Operation>
    auto SceneOperationChecked(Operation&& operation)
    {
        using Result = std::remove_cvref_t<std::invoke_result_t<Operation>>;
        if constexpr (std::is_void_v<Result>) {
            std::invoke(std::forward<Operation>(operation));
        } else {
            auto result = std::invoke(std::forward<Operation>(operation));
            if constexpr (requires { typename Result::error_type; typename Result::value_type; }) {
                if (!result) {
                    const auto& error = result.error();
                    std::cerr << "[FAIL] Valid Scene fixture: operation=" << error.operation
                        << " code=" << static_cast<unsigned>(error.code) << " entity=" << error.entity
                        << ": " << error.message << '\n';
                    std::exit(1);
                }
                if constexpr (std::is_void_v<typename Result::value_type>) return;
                else return std::move(*result);
            } else return result;
        }
    }

    using namespace GEngine;
    using namespace GEngine::Component;
    int checks = 0;

    void Require(bool condition, const char* message)
    {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    }

    void CheckDestroyError(std::expected<void, SceneError> result, SceneErrorCode code,
        std::uint64_t entity, std::string_view message)
    {
        Require(!result && result.error().code == code && result.error().entity == entity
            && result.error().operation == "_Scene::DestroyEntity" && result.error().message == message,
            "Destroy error lost complete typed diagnostics");
        auto copied = result;
        auto moved = std::move(result);
        Require(!copied && !moved && copied.error().code == code && moved.error().entity == entity
            && copied.error().message == message && moved.error().operation == "_Scene::DestroyEntity",
            "Error result copy/move changed diagnostic ownership");
    }

    void ForeignFrozenError()
    {
        _Scene scene, foreign;
        auto alien = SceneOperationChecked([&] { return foreign.CreateEntityWithUUID(733, "foreign frozen"); });
        {
            auto frozen = foreign.RenderData().BeginExtraction();
            Require(frozen.has_value(), "Foreign freeze fixture failed");
            const auto previous = std::set_terminate([] {
                std::cerr << "[FAIL] Foreign rejection accessed the frozen foreign registry\n";
                std::_Exit(86);
            });
            auto result = scene.DestroyEntity(alien);
            std::set_terminate(previous);
            CheckDestroyError(std::move(result), SceneErrorCode::ForeignEntity, 0,
                "Destroy entity does not belong to this scene");
            Require(foreign.RenderData().IsExtracting(), "Foreign rejection ended its owner's freeze");
        }
        Require(alien && foreign.GetEntityByUUID(733) == alien && !scene.GetEntityByUUID(733),
            "Foreign rejection mutated either Scene");
        std::cout << "[PASS] foreign-frozen destruction diagnostic preserves both scenes\n";
    }

    std::vector<UUID>& MutableChildren(_Entity& entity)
    {
        auto result = entity.Children();
        if (!result) {
            std::cerr << "[FAIL] Valid mutable child query: operation=" << result.error().operation
                << " code=" << static_cast<unsigned>(result.error().code) << ": " << result.error().message << '\n';
            std::exit(1);
        }
        return result->get();
    }

    void TypedChildren()
    {
        const auto reject = [](_Entity entity) {
            auto result = entity.Children();
            Require(!result && result.error().code == TransformErrorCode::InvalidEntity
                && result.error().operation == "_Entity::Children"
                && result.error().message == "Children requires a live scene entity", "Mutable child error lost diagnostics");
        };
        reject({});
        for (int cycle = 0; cycle != 3; ++cycle) {
            _Scene scene;
            auto entity = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(900 + cycle); });
            Require(!entity.HasAllComponents<RelationshipComponent>(), "New child fixture already has a relationship");
            {
                auto children = entity.Children();
                Require(children.has_value() && entity.HasAllComponents<RelationshipComponent>(), "Mutable child query did not create its relationship");
                auto& storage = entity.GetComponent<RelationshipComponent>().Children;
                Require(&children->get() == &storage, "Mutable child result owns a copy");
                auto copied = children;
                auto moved = std::move(children);
                Require(&copied->get() == &storage && &moved->get() == &storage, "Result copy/move changed the borrowed vector");
                moved->get().push_back(1000 + cycle);
                Require(std::as_const(entity).Children() == std::vector<UUID>{1000 + cycle}, "Borrowed mutation did not reach the const view");
            }
            Require(entity.GetComponent<RelationshipComponent>().Children.size() == 1, "Result destruction retired Scene-owned storage");
            entity.GetComponent<RelationshipComponent>().Children.clear();
            auto raw = _Entity(scene.Reg().create(), &scene);
            Require(raw.Children().has_value(), "Live raw entity lost existing mutable-query eligibility");
            scene.Reg().destroy(raw);
            reject(raw);
            reject(_Entity(static_cast<entt::entity>(entity), nullptr));
            SceneOperationChecked([&] { return scene.DestroyEntity(entity); });
            reject(entity);
            Require(!entity && !scene.GetEntityByUUID(900 + cycle), "Borrowed child result extended the entity lifetime");
        }
        std::cout << "[PASS] typed children diagnostics/borrow/copy/move/retirement cycles=3\n";
    }

    void CopyRuntimeOwnership(std::string_view mode)
    {
        // Keep successful shape owners alive until both Scene borrowers retire.
        std::vector<std::unique_ptr<PhysicalShape>> shapes;
        int authoredCallbacks = 0;
        auto scene = CreateRefPtr<_Scene>();
        auto source = SceneOperationChecked([&] { return scene->CreateEntityWithUUID(UUID(701), "runtime source"); });
        source.AddComponent<RigidBody3DComponent>().Type = BodyType::Dynamic;
        source.AddComponent<SphereFixture3DComponent>().Radius = .5f;
        source.Transform().OnScaleChanged.Connect([&authoredCallbacks](const Vec3f&) { ++authoredCallbacks; });
        Require(scene->OnRuntimeStart().has_value(), "Copy ownership source startup failed");
        auto* world = scene->GetPhysicsSystem()->GetPhysicsWorld();
        Require(world && world->GetPhysicsBodies().size() == 1, "Source body was not published");
        for (auto* body : world->GetPhysicsBodies()) shapes.emplace_back(body->m_Shape);
        auto* body = source.GetComponent<RigidBody3DComponent>().RuntimeBody;
        const auto identity = body->GetIdentity();
        auto* shape = static_cast<ShapeSphere*>(shapes.front().get());
        const float radius = shape->GetRadius();
        if (mode == "--stopped-scale") {
            scene->OnRuntimeStop();
            source.Transform().SetScale(2.f);
            Require(shape->GetRadius() == radius, "Stopped Scene retained a source-owned physics scale callback");
            Require(authoredCallbacks == 1, "Stopping physics removed authored callbacks");
        } else if (mode == "--copy-body" || mode == "--copy-scale") {
            auto copied = SceneOperationChecked([&] { return _Scene::Copy(scene); });
            auto clone = copied->GetEntityByUUID(UUID(701));
            if (mode == "--copy-body") Require(clone.GetComponent<RigidBody3DComponent>().RuntimeBody == nullptr,
                "Scene copy retained a body borrowed from the source world");
            else {
                clone.Transform().SetScale(2.f);
                Require(shape->GetRadius() == radius, "Scene copy scale callback reached the source-owned shape");
                Require(authoredCallbacks == 1, "Scene copy lost authored callbacks");
            }
            copied.reset();
            Require(world->IsBodyIdentityValid(identity), "Scene copy retirement removed the source body");
        } else {
            auto duplicate = SceneOperationChecked([&] { return scene->DuplicateEntity(source); });
            if (mode == "--duplicate-scale") {
                duplicate.Transform().SetScale(2.f);
                Require(shape->GetRadius() == radius, "Duplicate scale callback reached the source-owned shape");
                Require(authoredCallbacks == 1, "Duplication lost authored callbacks");
            }
            SceneOperationChecked([&] { return scene->DestroyEntity(duplicate); });
            Require(world->IsBodyIdentityValid(identity) && world->GetPhysicsBodies().size() == 1,
                "Retiring a duplicate removed the source-owned physics body");
        }
        scene->OnRuntimeStop();
        Require(!source.GetComponent<RigidBody3DComponent>().RuntimeBody && !scene->IsRunning(),
            "Source runtime body did not retire on stop");
        std::cout << "[PASS] Scene copy runtime ownership " << mode << " source-body=preserved authored-callbacks=preserved\n";
    }

    template<typename Action>
    void Reject(Action action)
    {
        const bool rejected = !action().has_value();
        Require(rejected, "Invalid entity operation was accepted");
    }

    template<typename Value>
    void CheckSceneError(std::expected<Value, SceneError> result, SceneErrorCode code,
        std::string_view operation, std::uint64_t entity, std::string_view message)
    {
        Require(!result && result.error().code == code && result.error().operation == operation
            && result.error().entity == entity && result.error().message == message, "Scene typed diagnostic incomplete");
        auto copied = result; auto moved = std::move(result);
        Require(!copied && !moved && copied.error().operation == operation && moved.error().message == message
            && copied.error().entity == entity && moved.error().code == code, "Scene error copy/move changed diagnostic");
    }

    void SceneCreationContracts()
    {
        _Scene scene, foreign;
        CheckSceneError(scene.CreateEntityWithUUID(0), SceneErrorCode::InvalidIdentity,
            "_Scene::CreateEntityWithUUID", 0, "Entity UUID must be nonzero and unique in its scene");
        auto source = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(801, "source"); });
        CheckSceneError(scene.CreateEntityWithUUID(801), SceneErrorCode::InvalidIdentity,
            "_Scene::CreateEntityWithUUID", 801, "Entity UUID must be nonzero and unique in its scene");
        Require(scene.Reg().view<IDComponent>().size() == 1 && scene.GetEntityByUUID(801) == source,
            "Failed identity creation changed scene membership");
        CheckSceneError(_Scene::Copy({}), SceneErrorCode::InvalidScene, "_Scene::Copy", 0,
            "Scene copy requires a source scene");
        auto alien = SceneOperationChecked([&] { return foreign.CreateEntityWithUUID(802); });
        {
            auto frozen = foreign.RenderData().BeginExtraction(); Require(bool(frozen), "Foreign freeze failed");
            CheckSceneError(scene.DuplicateEntity(alien), SceneErrorCode::ForeignEntity,
                "_Scene::DuplicateEntity", 0, "Duplicate requires a live entity in this scene");
            CheckSceneError(scene.PushToRenderList(alien), SceneErrorCode::ForeignEntity,
                "_Scene::PushToRenderList", 0, "Render-list entity does not belong to this scene");
        }
        CheckSceneError(scene.DuplicateEntity({}), SceneErrorCode::ForeignEntity,
            "_Scene::DuplicateEntity", 0, "Duplicate requires a live entity in this scene");
        _Entity noId{scene.Reg().create(), &scene};
        CheckSceneError(scene.DuplicateEntity(noId), SceneErrorCode::MissingIdentity,
            "_Scene::DuplicateEntity", 0, "Duplicate requires a live entity in this scene");
        Require(scene.PushToRenderList(noId).has_value(), "Non-render entity no-op changed");
        scene.Reg().destroy(noId);
        CheckSceneError(scene.DuplicateEntity(noId), SceneErrorCode::ForeignEntity,
            "_Scene::DuplicateEntity", 0, "Duplicate requires a live entity in this scene");
        auto parent = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(803); });
        Require(source.SetParent(parent).has_value(), "Parent fixture setup failed");
        auto& loop = parent.GetComponent<RelationshipComponent>();
        loop.ParentHandle = parent.GetUUID(); loop.ParentIdentity = *scene.RenderData().Identify(parent);
        const auto children = std::as_const(parent).Children();
        auto failed = scene.DuplicateEntity(source);
        Require(!failed && failed.error().code == SceneErrorCode::Parenting && failed.error().entity == 801
            && failed.error().operation == "_Scene::DuplicateEntity" && failed.error().transform
            && failed.error().transform->code == TransformErrorCode::Cycle
            && failed.error().transform->parent == 803, "Duplicate discarded typed parenting cause");
        auto failedId = failed.error().transform->entity;
        Require(failedId != 0 && failedId != 801 && !scene.GetEntityByUUID(failedId)
            && scene.Reg().view<IDComponent>().size() == 2 && std::as_const(parent).Children() == children,
            "Failed duplicate did not retire only its fresh entity");
        auto copied = failed; auto moved = std::move(failed);
        Require(copied.error().transform->entity == failedId && moved.error().transform->parent == 803,
            "Nested parenting cause copy/move lost ownership");
        parent.GetComponent<RelationshipComponent>().ParentHandle = UUID(0);
        parent.GetComponent<RelationshipComponent>().ParentIdentity = {};
        auto valid = scene.DuplicateEntity(source);
        Require(valid && valid->GetParent() == parent && source.GetParent() == parent,
            "Valid duplicate after rejected parent did not recover");
        Require(scene.PushToRenderList().has_value(), "Empty render-list batch failed");
        std::cout << "[PASS] scene typed creation/copy/duplicate diagnostics and transactional parent rollback\n";
    }

    void Parenting()
    {
        _Scene scene, foreign;
        auto root = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(1, "root"); });
        auto other = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(2, "other"); });
        auto child = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(3, "child"); });
        auto leaf = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(4, "leaf"); });
        const auto empty = SceneOperationChecked([&] { return scene.CreateEntity("empty"); });
        Require(!empty.GetParent() && empty.Children().empty(), "Missing relationships are not empty");
        Require(!empty.HasAllComponents<RelationshipComponent>(), "Const query added a component");
        Require(child.SetParent({}).has_value(), "Valid parenting failed");
        Require(child.SetParent(root).has_value(), "Valid parenting failed");
        Require(leaf.SetParent(child).has_value(), "Valid parenting failed");
        Require(child.GetParent() == root && MutableChildren(root) == std::vector<UUID>{3}, "Parent link is not reciprocal");
        Require(child.SetParent(root).has_value(), "Valid parenting failed");
        Require(MutableChildren(root).size() == 1, "Same-parent operation duplicated child");
        Require(root.IsAncesterOf(leaf) && leaf.IsDescendantOf(root)
            && !leaf.IsAncesterOf(root) && !root.IsAncesterOf(root), "Ancestor query is wrong");
        Require(!child.SetParent(child), "Invalid parenting returned success");
        Require(!root.SetParent(child), "Invalid parenting returned success");
        Require(!root.SetParent(leaf), "Invalid parenting returned success");
        Require(!root.SetParentUUID(leaf.GetUUID()), "Invalid parenting returned success");
        auto alien = SceneOperationChecked([&] { return foreign.CreateEntityWithUUID(1, "same UUID, foreign scene"); });
        Require(!child.SetParent(alien), "Invalid parenting returned success");
        Require(!child.SetParentUUID(999), "Invalid parenting returned success");
        auto stale = SceneOperationChecked([&] { return scene.CreateEntity("stale"); });
        SceneOperationChecked([&] { return scene.DestroyEntity(stale); });
        Require(!child.SetParent(stale), "Invalid parenting returned success");
        Require(!child.SetParent(_Entity((entt::entity)root, nullptr)), "Invalid parenting returned success");
        Require(!_Entity{}.SetParent(root), "Invalid parenting returned success");
        Require(!stale.SetParent(root), "Invalid parenting returned success");
        auto raw = _Entity(scene.Reg().create(), &scene);
        Require(!child.SetParent(raw), "Invalid parenting returned success");
        Require(!raw.SetParentUUID(999), "Raw entity without an ID accepted a UUID operation");
        CheckDestroyError(scene.DestroyEntity(raw), SceneErrorCode::MissingIdentity, 0, "Destroy requires a scene entity with an ID");
        scene.Reg().destroy(raw);
        Require(child.GetParent() == root && MutableChildren(root) == std::vector<UUID>{3}
            && leaf.GetParent() == child && MutableChildren(child) == std::vector<UUID>{4}, "Rejected parenting mutated graph");

        Require(child.SetParentUUID(other.GetUUID()).has_value(), "Valid parenting failed");
        Require(MutableChildren(root).empty() && MutableChildren(other) == std::vector<UUID>{3}
            && child.GetParent() == other && leaf.GetParent() == child, "Reparenting broke links");
        Require(child.SetParentUUID(0).has_value(), "Valid parenting failed");
        Require(!child.GetParent() && MutableChildren(other).empty() && leaf.GetParent() == child, "Null UUID detach broke descendants");
        Require(child.SetParent(root).has_value(), "Valid parenting failed");
        Require(root.RemoveChild(child) && !child.GetParent() && MutableChildren(root).empty(), "RemoveChild left a back-link");
        Require(!root.RemoveChild(child) && !root.RemoveChild(alien) && !root.RemoveChild({}), "RemoveChild accepted a non-child");
        Require(child.SetParent(root).has_value(), "Valid parenting failed");
        Require(child.SetParent(_Entity(entt::null, &scene)).has_value(), "Valid parenting failed");
        Require(!child.GetParent() && MutableChildren(root).empty(), "Scene-bound null did not detach");

        // Raw component writes are outside SetParent's contract; queries must still terminate.
        MutableChildren(root);
        MutableChildren(other);
        root.GetComponent<RelationshipComponent>().ParentHandle = other.GetUUID();
        other.GetComponent<RelationshipComponent>().ParentHandle = root.GetUUID();
        Require(!child.SetParent(root), "Invalid parenting returned success");
        Require(!leaf.IsAncesterOf(root), "Malformed ancestor cycle did not terminate");
        other.GetComponent<RelationshipComponent>().ParentHandle = 999;
        Require(!child.SetParent(root), "Invalid parenting returned success");
    }

    void DestructionAndReuse()
    {
        _Scene scene, foreign;
        SceneOperationChecked([&] { return scene.DestroyEntity(_Entity{}); });
        SceneOperationChecked([&] { return scene.DestroyEntity(UUID(0)); });
        SceneOperationChecked([&] { return scene.DestroyEntity(UUID(999)); });
        auto entity = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(10, "lvalue"); });
        const auto handle = (entt::entity)entity;
        const auto borrowed = entity;
        { auto copy = entity; Require(copy == entity, "Entity copy changed identity"); }
        Require(entity && borrowed, "Wrapper destruction owned an entity");
        SceneOperationChecked([&] { return scene.DestroyEntity(entity); });
        Require(!entity && !borrowed && !scene.GetEntityByUUID(10)
            && !entity.HasAllComponents<IDComponent>(), "Lvalue destroy left a valid entity");
        auto reused = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(10, "reused UUID"); });
        Require(entt::to_entity((entt::entity)reused) == entt::to_entity(handle)
            && (entt::entity)reused != handle, "Fixture did not reuse an EnTT slot with a new generation");
        SceneOperationChecked([&] { return scene.DestroyEntity(entity); });
        Require(reused && !entity, "Stale destroy removed reused entity");
        auto alien = SceneOperationChecked([&] { return foreign.CreateEntityWithUUID(10, "foreign"); });
        CheckDestroyError(scene.DestroyEntity(alien), SceneErrorCode::ForeignEntity, 0, "Destroy entity does not belong to this scene");
        Require(alien && reused, "Foreign destruction mutated either scene");
        SceneOperationChecked([&] { return scene.DestroyEntity(scene.GetEntityByUUID(10)); });
        Require(!reused && !scene.GetEntityByUUID(10), "Rvalue destroy differs from lvalue");
        SceneOperationChecked([&] { return scene.DestroyEntity(SceneOperationChecked([&] { return scene.CreateEntityWithUUID(11); })); });
        Require(!scene.GetEntityByUUID(11), "Immediate temporary destroy failed");
        Reject([&] { return scene.CreateEntityWithUUID(0); });
        auto unique = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(12); });
        Reject([&] { return scene.CreateEntityWithUUID(12); });
        Require(scene.GetEntityByUUID(12) == unique, "Duplicate UUID replaced the existing identity");
        SceneOperationChecked([&] { return scene.DestroyEntity(unique.GetUUID()); });
        Require(!unique, "UUID destroy left entity valid");

        for (int i = 0; i < 256; ++i)
        {
            auto next = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(100); });
            Require(!entity && next.GetParentUUID() == 0, "Reuse inherited stale validity or relationships");
            SceneOperationChecked([&] { return scene.DestroyEntity(next); });
            Require(!next && !scene.GetEntityByUUID(100), "Repeated reuse failed");
        }

        auto root = SceneOperationChecked([&] { return scene.CreateEntity("root"); });
        auto parent = SceneOperationChecked([&] { return scene.CreateEntity("parent"); });
        auto child = SceneOperationChecked([&] { return scene.CreateEntity("child"); });
        auto grandchild = SceneOperationChecked([&] { return scene.CreateEntity("grandchild"); });
        Require(parent.SetParent(root).has_value(), "Valid parenting failed"); Require(child.SetParent(parent).has_value(), "Valid parenting failed"); Require(grandchild.SetParent(child).has_value(), "Valid parenting failed");
        SceneOperationChecked([&] { return scene.DestroyEntity(parent, true); });
        Require(!parent && child && grandchild && !child.GetParent() && grandchild.GetParent() == child
            && MutableChildren(root).empty(), "excludeChildren did not detach surviving children");
        Require(child.SetParent(root).has_value(), "Valid parenting failed");
        SceneOperationChecked([&] { return scene.DestroyEntity(child, false, false); });
        Require(!child && !grandchild && MutableChildren(root).empty(), "Legacy first=false left stale links");

        std::vector<_Entity> descendants;
        for (int i = 0; i < 64; ++i)
        {
            auto a = SceneOperationChecked([&] { return scene.CreateEntity("sibling"); }); Require(a.SetParent(root).has_value(), "Valid parenting failed");
            auto b = SceneOperationChecked([&] { return scene.CreateEntity("grandchild"); }); Require(b.SetParent(a).has_value(), "Valid parenting failed");
            descendants.push_back(a); descendants.push_back(b);
        }
        SceneOperationChecked([&] { return scene.DestroyEntity(root.GetUUID()); });
        Require(!root && std::all_of(descendants.begin(), descendants.end(), [] (const auto& e) { return !e; }),
            "Recursive destruction skipped relocated components or siblings");
    }

    void CopiesAndConstAccess()
    {
        auto scene = CreateRefPtr<_Scene>();
        auto root = SceneOperationChecked([&] { return scene->CreateEntity("root"); });
        auto child = SceneOperationChecked([&] { return scene->CreateEntity("child"); }); Require(child.SetParent(root).has_value(), "Valid parenting failed");
        auto leaf = SceneOperationChecked([&] { return scene->CreateEntity("leaf"); }); Require(leaf.SetParent(child).has_value(), "Valid parenting failed");
        auto duplicate = SceneOperationChecked([&] { return scene->DuplicateEntity(child); });
        Require(duplicate.GetParent() == root && MutableChildren(duplicate).empty()
            && leaf.GetParent() == child && MutableChildren(root).size() == 2, "Duplicate aliased source relationships");
        SceneOperationChecked([&] { return scene->DestroyEntity(duplicate); });
        Require(child && leaf && MutableChildren(root) == std::vector<UUID>{child.GetUUID()}, "Destroy duplicate affected original tree");
        auto copied = SceneOperationChecked([&] { return _Scene::Copy(scene); });
        auto copiedLeaf = copied->GetEntityByUUID(leaf.GetUUID());
        Require(copiedLeaf.GetParent() == copied->GetEntityByUUID(child.GetUUID()), "Scene copy lost internal relationships");
        SceneOperationChecked([&] { return copied->DestroyEntity(root.GetUUID()); });
        Require(!copiedLeaf && root && child && leaf, "Scene copy borrowed original entity ownership");

        const auto& constant = child;
        static_assert(std::is_same_v<decltype(constant.Transform()), Mat4>);
        static_assert(std::is_same_v<decltype(constant.GetComponent<Transform3DComponent>()), const Transform3DComponent&>);
        static_assert(std::is_same_v<decltype(constant.Children()), const std::vector<UUID>&>);
        child.Transform().Translation = { 3.0f, 4.0f, 5.0f };
        child.Transform().Scale = { 2.0f, 3.0f, 4.0f };
        child.Transform().SetRotation(Vec3f{0.1f, 0.2f, 0.3f});
        const auto& matrix = constant.Transform(); // Lifetime extends the returned value.
        const auto expected = child.Transform().GetTransform();
        Require(matrix == expected && !child.HasAllComponents<TransformComponent>(), "Const access used the wrong transform");
        Require(&constant.GetComponent<Transform3DComponent>() == &child.Transform(), "Const component access returned a copy");
        Require(&constant.Children() == &MutableChildren(child) && &constant.Name() == &child.Name(), "Const access returned dangling subobjects");
        child.Transform().Translation.x = 20.0f;
        Require(matrix == expected && constant.Transform() != expected, "Const matrix value did not retain its lifetime");
        const _Entity invalid;
        Require(!invalid && !invalid.GetParent() && invalid.Children().empty() && invalid.Name() == "Unnamed",
            "Null read-only relationship queries are unsafe");
    }
    const WorldTransform& World(const WorldTransformUpdate& frame, _Entity entity)
    {
        const auto id = entity.GetSceneContext()->RenderData().Identify(entity);
        Require(id.has_value(), "Entity identity missing");
        const auto found = std::find_if(frame.transforms.begin(), frame.transforms.end(),
            [&](const WorldTransform& transform) { return transform.entity == *id; });
        Require(found != frame.transforms.end(), "World transform missing");
        return *found;
    }

    WorldTransformUpdate Evaluate(_Scene& scene)
    {
        auto result = scene.UpdateWorldTransforms();
        Require(result.has_value(), "Hierarchy evaluation failed");
        return std::move(*result);
    }

    void Close(const Mat4& actual, const Mat4& expected)
    {
        for (int c = 0; c != 4; ++c)
            for (int r = 0; r != 4; ++r)
                Require(std::abs(actual[c][r] - expected[c][r]) < 0.0001f, "World matrix mismatch");
    }

    void Hierarchy()
    {
        auto scene = CreateRefPtr<_Scene>();
        // Deliberately create a child before its parent and use descending UUIDs.
        auto leaf = SceneOperationChecked([&] { return scene->CreateEntityWithUUID(1, "leaf"); });
        auto child = SceneOperationChecked([&] { return scene->CreateEntityWithUUID(2, "child"); });
        auto root = SceneOperationChecked([&] { return scene->CreateEntityWithUUID(3, "root"); });
        auto other = SceneOperationChecked([&] { return scene->CreateEntityWithUUID(4, "other"); });
        Require(leaf.SetParent(child).has_value() && child.SetParent(root).has_value(), "Valid hierarchy rejected");
        root.Transform().Translation = {2, 3, 4};
        root.Transform().Scale = {-2, 3, 0.5f};
        root.Transform().SetRotation(Vec3f{0.2f, 0.3f, 0.4f});
        child.Transform().Translation = {1, 2, 3};
        child.Transform().SetRotation(Vec3f{0.5f, 0.1f, 0.2f});
        leaf.Transform().Translation = {4, 5, 6};
        other.Transform().Translation = {20, 0, 0};
        const auto authored = leaf.Transform().GetTransform();
        const auto first = Evaluate(*scene);
        Require(first.recomputed == 4, "Initial frame did not compute every world");
        Require(first.transforms[0].entity == World(first, root).entity
            && first.transforms[1].entity == World(first, child).entity
            && first.transforms[2].entity == World(first, leaf).entity, "Order is not parent before child");
        Close(World(first, leaf).matrix, root.Transform().GetTransform() * child.Transform().GetTransform() * authored);
        Require(leaf.Transform().GetTransform() == authored, "World evaluation changed authored local TRS");
        auto unchanged = Evaluate(*scene);
        Require(unchanged.recomputed == 0 && World(first, leaf).revision == World(unchanged, leaf).revision,
            "Unchanged frame recomputed or changed revision");
        root.Transform().Translation.x += 5;
        auto moved = Evaluate(*scene);
        Require(moved.recomputed == 3 && World(moved, other).revision == World(first, other).revision,
            "Dirty propagation crossed an unrelated root");
        Require(World(moved, leaf).revision == World(first, leaf).revision + 1, "Descendant did not update");
        Require(child.SetParent(other).has_value(), "Reparent failed");
        auto reparented = Evaluate(*scene);
        Require(reparented.recomputed == 2, "Reparent did not update exactly its subtree");
        Close(World(reparented, leaf).matrix, other.Transform().GetTransform() * child.Transform().GetTransform() * authored);
        Require(!root.SetParent(root) && !other.SetParent(leaf), "Self/cycle accepted");
        Require(Evaluate(*scene).recomputed == 0, "Rejected parenting changed worlds");

        // Value snapshots survive later authoring and registry mutation.
        Close(World(first, leaf).matrix, glm::translate(Mat4(1), Vec3f{-5, 0, 0})
            * root.Transform().GetTransform() * child.Transform().GetTransform() * authored);
        auto duplicate = SceneOperationChecked([&] { return scene->DuplicateEntity(child); });
        auto copied = SceneOperationChecked([&] { return _Scene::Copy(scene); });
        auto copiedFrame = Evaluate(*copied);
        Close(World(copiedFrame, copied->GetEntityByUUID(leaf.GetUUID())).matrix, World(reparented, leaf).matrix);
        auto duplicated = Evaluate(*scene);
        Close(World(duplicated, duplicate).matrix, World(duplicated, child).matrix);
        Require(World(copiedFrame, copied->GetEntityByUUID(leaf.GetUUID())).entity.registry
            != World(duplicated, leaf).entity.registry, "Copy retained source runtime identity");

        SceneOperationChecked([&] { return scene->DestroyEntity(other, true); });
        Require(!child.GetParent() && !duplicate.GetParent(), "Destroy/detach retained parent");
        auto detached = Evaluate(*scene);
        Close(World(detached, leaf).matrix, child.Transform().GetTransform() * authored);
        SceneOperationChecked([&] { return scene->DestroyEntity(child); });
        Require(!child && !leaf && duplicate, "Recursive parent destruction missed descendants");
        Evaluate(*scene);
        std::cout << "[PASS] hierarchy composition/reparent/copy/destroy/unchanged\n";
    }

    void HierarchyFailuresAndAnchors()
    {
        _Scene scene, foreign;
        auto parent = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(10); });
        auto child = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(20); });
        Require(child.SetParent(parent).has_value(), "Parent setup failed");
        auto original = Evaluate(scene);
        auto badParent = child.SetParent(SceneOperationChecked([&] { return foreign.CreateEntity(); }));
        Require(!badParent && badParent.error().code == TransformErrorCode::ForeignEntity, "Foreign error lost type");
        auto self = parent.SetParent(parent);
        Require(!self && self.error().code == TransformErrorCode::Cycle && self.error().entity == parent.GetUUID(),
            "Cycle error lost identity");

        // Corruption through the legacy raw registry is detected without recursion.
        auto& link = parent.AddOrReplaceComponent<RelationshipComponent>();
        link.ParentHandle = child.GetUUID();
        link.ParentIdentity = *scene.RenderData().Identify(child);
        auto cycle = scene.UpdateWorldTransforms();
        Require(!cycle && cycle.error().code == TransformErrorCode::Cycle, "Raw cycle was not rejected");
        Require(parent.SetParent({}).has_value(), "Detach could not repair a malformed graph");
        Require(Evaluate(scene).recomputed == 0, "Invalid graph published caches");

        child.Transform().Translation.x = std::numeric_limits<float>::infinity();
        auto nonfinite = scene.UpdateWorldTransforms();
        Require(!nonfinite && nonfinite.error().code == TransformErrorCode::NonFiniteTransform, "Nonfinite local accepted");
        child.Transform().Translation.x = 0;
        child.Transform().QuatRotation = Quat{0, 0, 0, 0};
        auto rotation = scene.UpdateWorldTransforms();
        Require(!rotation && rotation.error().code == TransformErrorCode::InvalidRotation, "Zero rotation accepted");
        child.Transform().QuatRotation = Quat{1, 0, 0, 0};
        parent.Transform().Scale = Vec3f{std::numeric_limits<float>::max()};
        child.Transform().Scale = Vec3f{2};
        auto overflow = scene.UpdateWorldTransforms();
        Require(!overflow && overflow.error().code == TransformErrorCode::NonFiniteTransform, "Composed overflow accepted");
        parent.Transform().Scale = child.Transform().Scale = Vec3f{1};
        Require(Evaluate(scene).recomputed == 0, "Failed evaluation committed partial cache updates");
        child.RemoveComponent<Transform3DComponent>();
        auto missing = scene.UpdateWorldTransforms();
        Require(!missing && missing.error().code == TransformErrorCode::MissingTransform, "Missing transform accepted");
        child.AddComponent<Transform3DComponent>();

        // Bypass scene destruction deliberately, then recreate the exact UUID.
        const auto oldId = *scene.RenderData().Identify(parent);
        scene.Reg().destroy(parent);
        auto replacement = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(10); });
        auto stale = scene.UpdateWorldTransforms();
        Require(!stale && stale.error().code == TransformErrorCode::InvalidParent && !child.GetParent(),
            "UUID reuse resurrected a dead parent");
        Require(!scene.RenderData().Resolve(oldId), "Destroyed parent render identity survived");
        // A stale child UUID in the replacement's reverse list cannot give it ownership.
        MutableChildren(replacement).push_back(child.GetUUID());
        SceneOperationChecked([&] { return scene.DestroyEntity(replacement); });
        Require(child, "Destroying replacement adopted an old generation's child");
        replacement = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(10); });
        Require(child.SetParent(replacement).has_value(), "Explicit repair/reparent failed");

        replacement.Transform().Translation = {100, 0, 0};
        child.Transform().Translation = {3, 0, 0};
        child.AddComponent<RigidBody3DComponent>();
        auto descendant = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(30); });
        descendant.Transform().Translation = {2, 0, 0};
        Require(descendant.SetParent(child).has_value(), "Anchor descendant rejected");
        auto anchored = Evaluate(scene);
        Close(World(anchored, child).matrix, glm::translate(Mat4(1), Vec3f{3, 0, 0}));
        Close(World(anchored, descendant).matrix, glm::translate(Mat4(1), Vec3f{5, 0, 0}));
        replacement.Transform().Translation.x += 1;
        Require(Evaluate(scene).recomputed == 1, "Parent movement altered world-space physics anchor");
        child.RemoveComponent<RigidBody3DComponent>();
        auto composed = Evaluate(scene);
        Close(World(composed, descendant).matrix, glm::translate(Mat4(1), Vec3f{106, 0, 0}));
        std::cout << "[PASS] hierarchy typed failures/transaction/lifetime/physics anchors\n";
    }

    void DeepHierarchy()
    {
        _Scene scene;
        constexpr int depth = 8192;
        std::vector<_Entity> entities;
        for (int i = 0; i != depth; ++i)
        {
            entities.push_back(SceneOperationChecked([&] { return scene.CreateEntityWithUUID(depth - i); }));
            entities.back().Transform().Translation = {1, 0, 0};
        }
        // Build from the leaf upward so setup itself is linear, with adversarial UUID order.
        for (int i = depth - 1; i > 0; --i)
            Require(entities[i].SetParent(entities[i - 1]).has_value(), "Deep parent link failed");
        auto frame = Evaluate(scene);
        Require(frame.transforms.size() == depth && frame.recomputed == depth, "Deep graph not fully evaluated");
        for (int i = 0; i != depth; ++i)
        {
            Require(frame.transforms[i].matrix[3].x == static_cast<float>(i + 1), "Deep world/order incorrect");
            Require(frame.transforms[i].entity == *scene.RenderData().Identify(entities[i]), "Deep identity order incorrect");
        }
        Require(Evaluate(scene).recomputed == 0, "Deep unchanged frame recomputed");
        entities[depth / 2].Transform().Translation.x = 2;
        Require(Evaluate(scene).recomputed == depth / 2, "Deep dirty subtree did not propagate");
        Require(!entities.front().SetParent(entities.back()), "Deep cycle was accepted");
        // Duplicate raw reverse links must not double-destroy descendants.
        MutableChildren(entities.front()).push_back(entities[1].GetUUID());
        SceneOperationChecked([&] { return scene.DestroyEntity(entities.front()); });
        Require(Evaluate(scene).transforms.empty(), "Deep destruction left descendants alive");
        std::cout << "[PASS] hierarchy depth=8192 deterministic order and cached subtree\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--foreign-frozen") { ForeignFrozenError(); return 0; }
        if (argc == 2) {
            const std::string_view mode(argv[1]);
            if (mode == "--duplicate-body" || mode == "--duplicate-scale" || mode == "--copy-body"
                || mode == "--copy-scale" || mode == "--stopped-scale") { CopyRuntimeOwnership(mode); return 0; }
        }
        ForeignFrozenError();
        SceneCreationContracts();
        TypedChildren();
        Parenting();
        DestructionAndReuse();
        CopiesAndConstAccess();
        Hierarchy();
        HierarchyFailuresAndAnchors();
        DeepHierarchy();
        if (RunProgramGroupingRegression() != 0) return 1;
        std::cout << "[PASS] entity-api " << checks << " checks plus program-grouping regression\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] entity-api after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}

#endif
