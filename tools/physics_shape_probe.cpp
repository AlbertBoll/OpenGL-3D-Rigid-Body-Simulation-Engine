#include "Physics/PhysicsShapeError.h"
#include "Physics/ShapeSphere.h"
#include "Physics/ShapeBox.h"
#include "Physics/ShapeConvex.h"
#include <memory>
#include <concepts>
#include <type_traits>
static_assert(std::same_as<decltype(::GEngine::ShapeSphere::Create(1.f)),
    std::expected<::GEngine::ShapeSphere,::GEngine::PhysicsShapeError>>);
static_assert(std::same_as<decltype(::GEngine::ShapeBox::Create({})),
    std::expected<::GEngine::ShapeBox,::GEngine::PhysicsShapeError>>);
static_assert(std::is_trivially_copyable_v<::GEngine::PhysicsShapeError>);
static_assert(std::is_nothrow_destructible_v<::GEngine::ShapeSphere>);
static_assert(std::is_nothrow_destructible_v<::GEngine::ShapeBox>);
static_assert(!std::is_default_constructible_v<::GEngine::ShapeBox>);
static_assert(std::is_default_constructible_v<::GEngine::ShapeSphere>);
static_assert(!std::is_constructible_v<::GEngine::ShapeSphere, float>);
static_assert(!std::is_constructible_v<::GEngine::ShapeBox, const std::vector<::GEngine::Vec3f>&>);
#if defined(PHYSICS_SHAPE_SCENE_SCHEMA_ONLY)
#include "Scene/_Scene.h"
static_assert(std::same_as<decltype(std::declval<::GEngine::_Scene&>().OnRuntimeStart()),
    std::expected<void,::GEngine::PhysicsShapeError>>);
#elif defined(PHYSICS_SHAPE_POLYMORPHIC_SCHEMA_ONLY)
static_assert(std::has_virtual_destructor_v<::GEngine::PhysicalShape>);
static_assert(std::is_move_constructible_v<::GEngine::ShapeBox>);
static_assert(!std::is_nothrow_move_constructible_v<::GEngine::ShapeBox>);
static_assert(std::is_move_assignable_v<::GEngine::ShapeBox>);
static_assert(!std::is_nothrow_move_assignable_v<::GEngine::ShapeBox>);
static_assert(std::is_copy_constructible_v<::GEngine::ShapeBox>);
#elif defined(PHYSICS_SHAPE_STARTUP_SCHEMA_ONLY)
#include "Core/RenderTarget.h"
static_assert(std::is_constructible_v<::GEngine::ApplicationInitializationError,::GEngine::PhysicsShapeError>);
static_assert(std::same_as<::GEngine::ApplicationInitializationResult,
    std::expected<void,::GEngine::ApplicationInitializationError>>);
#elif defined(PHYSICS_SHAPE_STARTUP_PROBE)
#define main Phase66UnusedShutdownMain
#include "shutdown_probe.cpp"
#undef main
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
namespace PhysicsStartup
{
    std::string_view mode;
    unsigned runs=0,renders=0,destructors=0;
    std::unique_ptr<ModelNames> names;
    spdlog::logger* logger=nullptr;
    std::vector<spdlog::sink_ptr> sinks;
    spdlog::level::level_enum level=spdlog::level::trace;
    class App final : public ::GEngine::BaseApp
    {
    public:
        ::GEngine::ApplicationInitializationResult Initialize(const std::initializer_list<::GEngine::WindowProperties>& props) override
        {
            auto initialized=BaseApp::Initialize(props);if(!initialized)return initialized;
            auto& core=::GEngine::Log::GetCoreLogger();logger=core.get();sinks=core->sinks();level=core->level();
            Check(core->name()=="GENGINE","Physics reporter category changed");
            core->set_level(spdlog::level::critical);
            ::GEngine::ReportApplicationError(::GEngine::ApplicationInitializationError{
                ::GEngine::PhysicsShapeError{::GEngine::PhysicsShapeErrorCode::Allocation,
                    "phase66-filtered-physics","phase66-filtered-message",3.5f,17,23}});
            core->set_level(level);
            auto sphere=::GEngine::ShapeSphere::Create(mode=="radius-failure"?-2.5f:2.5f);
            if(!sphere){auto error=sphere.error();error.entity=37;return std::unexpected(error);}
            const std::vector<::GEngine::Vec3f> points=mode=="points-failure"
                ?std::vector<::GEngine::Vec3f>{{0.f,0.f,0.f},{1.f,1.f,1.f},{2.f,2.f,2.f}}
                :std::vector<::GEngine::Vec3f>{{-2.f,-3.f,-4.f},{2.f,3.f,4.f}};
            // A flat set has zero extent on z under the existing box validation policy.
            auto input=points;
            if(mode=="points-failure")for(auto& point:input)point.z=0.f;
            auto box=::GEngine::ShapeBox::Create(input);
            if(!box){auto error=box.error();error.entity=41;return std::unexpected(error);}
            Check(sphere->GetRadius()==2.5f && box->IsValid(),"Successful startup shape payload changed");
            return {};
        }
        ::GEngine::ApplicationRunResult Run() override {++runs;Render();return {};}
        void Render() override {++renders;}
        ~App() override {++destructors;Check(SDL_GL_GetCurrentContext()!=nullptr,"Physics startup app outlived context");}
    };
}
extern "C" int Phase66RealGladLoadGL(void);
extern "C" int gladLoadGL(void)
{
    const int result=Phase66RealGladLoadGL();
    if(result)PhysicsStartup::names=std::make_unique<ModelNames>();
    return result;
}
::GEngine::WindowProperties winProp=Properties();
::GEngine::BaseApp* CreateApp(){return new PhysicsStartup::App;}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    PhysicsStartup::mode=argv[1];const auto mode=PhysicsStartup::mode;
    if(mode!="radius-failure" && mode!="points-failure" && mode!="success")return 2;
    SDL_SetMainReady();const int status=ProductionEntryPoint(argc,argv);const bool success=mode=="success";
    Check(status==(success?0:1) && PhysicsStartup::runs==(success?1u:0u)
        && PhysicsStartup::renders==(success?1u:0u) && PhysicsStartup::destructors==1,
        "Physics startup failure did not stop Run/render or retire application");
    Check(PhysicsStartup::names!=nullptr,"Physics startup GL loader not reached");
    PhysicsStartup::names->Empty();PhysicsStartup::names.reset();PlatformGone();
    Check(ModelNames::generatedBuffers==108 && ModelNames::generatedArrays==30,
        "Physics startup did not cover exact base resource owners");
    const auto& core=::GEngine::Log::GetCoreLogger();
    Check(core.get()==PhysicsStartup::logger && core->name()=="GENGINE" && core->sinks()==PhysicsStartup::sinks
        && core->level()==PhysicsStartup::level,"Physics reporting changed logger/sinks/filtering");
    std::cout<<"[PASS] physics startup "<<mode<<" exit="<<status<<" runs="<<PhysicsStartup::runs
        <<" renders="<<PhysicsStartup::renders<<" buffers="<<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers
        <<" arrays="<<ModelNames::generatedArrays<<"/"<<ModelNames::retiredArrays<<" owner=1 root=0 SDL=0 TTF=0 teardown=1\n";
    return status;
}
#elif defined(PHYSICS_SHAPE_RBS_PROBE)
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
namespace RbsStartup
{
    unsigned runs=0,renders=0,destructors=0;
    std::unique_ptr<ModelNames> names;
    std::optional<::GEngine::PhysicsShapeError> error;
    class App final : public RigidBodySimulationApp
    {
    public:
        ::GEngine::ApplicationInitializationResult Initialize(const std::initializer_list<::GEngine::WindowProperties>& props) override
        {
            auto result=RigidBodySimulationApp::Initialize(props);
            if(!result)
            {
                const auto* physics=std::get_if<::GEngine::PhysicsShapeError>(&result.error());
                RbsShapeAudit::Require(physics && physics->code==::GEngine::PhysicsShapeErrorCode::InvalidRadius
                    && physics->operation=="ShapeSphere::Create" && physics->entity!=0 && physics->radius==-2.5f
                    && physics->pointCount==0 && physics->message=="ShapeSphere requires a finite positive radius",
                    "Actual RBS failed through an unexpected or incomplete channel");
                error=*physics;
            }
            return result;
        }
        ::GEngine::ApplicationRunResult Run() override {++runs;Render();return {};}
        void Render() override {++renders;}
        ~App() override {++destructors;RbsShapeAudit::Require(SDL_GL_GetCurrentContext()!=nullptr,"RBS app outlived context");}
    };
}
extern "C" int Phase66RealGladLoadGL(void);
extern "C" int gladLoadGL(void)
{
    const int result=Phase66RealGladLoadGL();
    if(result)RbsStartup::names=std::make_unique<ModelNames>();
    return result;
}
::GEngine::BaseApp* CreateApp(){return new RbsStartup::App;}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    const std::string_view mode=argv[1];if(mode!="failure" && mode!="success")return 2;
    RbsShapeAudit::fail=mode=="failure";SDL_SetMainReady();
    const int status=ProductionEntryPoint(argc,argv);const bool fail=RbsShapeAudit::fail;
    std::cout<<"[OBSERVE] actual RBS entry returned exit="<<status<<" runs="<<RbsStartup::runs<<" renders="<<RbsStartup::renders<<std::endl;
    RbsShapeAudit::Require(status==(fail?1:0) && RbsStartup::runs==(fail?0u:1u) && RbsStartup::renders==(fail?0u:1u)
        && RbsStartup::destructors==1,"Actual RBS exit/Run/render/destructor contract failed");
    RbsShapeAudit::Require(RbsStartup::names!=nullptr,"Actual RBS loader not reached");
    RbsShapeAudit::Require(ModelNames::valid && ModelNames::buffers.empty() && ModelNames::arrays.empty()
        && ModelNames::generatedBuffers==ModelNames::retiredBuffers && ModelNames::generatedArrays==ModelNames::retiredArrays,
        "Actual RBS GL resource retirement failed");
    RbsStartup::names->Empty();RbsStartup::names.reset();PlatformGone();
    if(RbsStartup::error)
    {
        const auto& e=*RbsStartup::error;
        std::cout<<"[EXPECTED] Application physics shape operation="<<e.operation<<" code="<<static_cast<unsigned>(e.code)
            <<" entity="<<e.entity<<" radius="<<e.radius<<" points="<<e.pointCount<<": "<<e.message<<'\n';
    }
    std::cout<<"[OBSERVE] actual RBS "<<mode<<" concrete="<<RbsShapeAudit::created<<"/"<<RbsShapeAudit::retired
        <<" live="<<RbsShapeAudit::live.size()<<" sphere="<<RbsShapeAudit::spheres<<" box="<<RbsShapeAudit::boxes
        <<" buffers="<<ModelNames::generatedBuffers<<"/"<<ModelNames::retiredBuffers
        <<" arrays="<<ModelNames::generatedArrays<<"/"<<ModelNames::retiredArrays<<" root=0 SDL=0 TTF=0"<<std::endl;
    RbsShapeAudit::Require(RbsShapeAudit::created>0 && RbsShapeAudit::spheres>0 && RbsShapeAudit::boxes>0,
        "Actual startup did not cover mixed concrete owners");
    RbsShapeAudit::Require(RbsShapeAudit::created==RbsShapeAudit::retired && RbsShapeAudit::live.empty(),
        "Actual RBS startup retained caller-owned physical shapes");
    std::cout<<"[PASS] actual RBS physics startup "<<mode<<" exit="<<status<<" runs="<<RbsStartup::runs
        <<" renders="<<RbsStartup::renders<<" owner=1 concrete=0 root=0 SDL=0 TTF=0\n";
    return status;
}
#elif !defined(PHYSICS_SHAPE_SCHEMA_ONLY)
#include "Core/Log.h"
#ifdef PHYSICS_SHAPE_SCENE_PROBE
#include <functional>
#include <iostream>
#include "Scene/_Entity.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/PhysicsSystem.h"
#endif
#include <array>
#include <cstdlib>
#include <new>
#include <print>
#include <limits>
namespace Audit
{
    struct Entry { void* pointer=nullptr; bool live=false; };
    std::array<Entry,1024> entries{};
    bool enabled=false,valid=true;
    unsigned allocated=0,retired=0,live=0;
    int denyAfter=-1;
    void Created(void* pointer)
    {
        if(!enabled)return;
        for(const auto& entry:entries)if(entry.live && entry.pointer==pointer)valid=false;
        for(auto& entry:entries)if(!entry.live){entry={pointer,true};++allocated;++live;return;}
        std::abort();
    }
    void Retired(void* pointer)
    {
        for(auto& entry:entries)if(entry.live && entry.pointer==pointer){entry.live=false;++retired;--live;return;}
    }
}
void* operator new(std::size_t bytes)
{
    void* pointer=std::malloc(bytes?bytes:1);
    if(!pointer)throw std::bad_alloc{}; // Test allocator only; production allocation behavior is unchanged.
    Audit::Created(pointer);return pointer;
}
void operator delete(void* pointer) noexcept { Audit::Retired(pointer);std::free(pointer); }
void operator delete(void* pointer,std::size_t) noexcept { ::operator delete(pointer); }
#ifdef PHYSICS_SHAPE_SCENE_PROBE
void* operator new(std::size_t bytes,const std::nothrow_t&) noexcept
{
    if(Audit::denyAfter>0 && --Audit::denyAfter==0)return nullptr;
    void* pointer=std::malloc(bytes?bytes:1);if(pointer)Audit::Created(pointer);return pointer;
}
void operator delete(void* pointer,const std::nothrow_t&) noexcept { ::operator delete(pointer); }
#endif
namespace
{
    using namespace ::GEngine;
#ifdef PHYSICS_SHAPE_SCENE_PROBE
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

#endif
    unsigned checks=0;
    void Check(bool valid,const char* message)
    {++checks;if(!valid){std::println(stderr,"[FAIL] {}",message);std::exit(1);}}
    bool Near(float a,float b){return std::abs(a-b)<1.e-5f;}
    std::vector<Vec3f> Points(Vec3f half)
    {
        std::vector<Vec3f> points;
        for(float x:{-1.f,1.f})for(float y:{-1.f,1.f})for(float z:{-1.f,1.f})
            points.push_back(half*Vec3f(x,y,z));
        return points;
    }
    unsigned concreteDestructors=0;
    template<std::derived_from<PhysicalShape> Shape>
    struct ObservedShape final : Shape
    {
        explicit ObservedShape(Shape&& shape) : Shape(std::move(shape)) {}
        // Intentionally valid before and after the base-destructor prerequisite.
        ~ObservedShape() { ++concreteDestructors; }
    };
    void PolymorphicRetirement()
    {
        const auto points=Points({2.f,3.f,4.f});
        { ShapeConvex warm(points); Check(warm.IsValid(),"Convex lifetime fixture must be valid"); }
        for(unsigned cycle=0;cycle<2;++cycle)
        {
            Audit::enabled=true;
            {
                auto sphere=ShapeSphere::Create(2.5f);auto box=ShapeBox::Create(points);
                Check(sphere && box,"Typed shape lifetime fixtures must be valid");
                std::unique_ptr<PhysicalShape> sphereOwner(new ObservedShape<ShapeSphere>(std::move(*sphere)));
                std::unique_ptr<PhysicalShape> boxOwner(new ObservedShape<ShapeBox>(std::move(*box)));
                std::unique_ptr<PhysicalShape> convexOwner(new ObservedShape<ShapeConvex>(ShapeConvex(points)));
                Check(sphereOwner->IsValid() && boxOwner->IsValid() && convexOwner->IsValid(),
                    "Polymorphic owner handoff lost valid payload");
                Check(boxOwner->GetBounds().maxs==Vec3f(2,3,4),"Polymorphic owner handoff changed bounds");
            }
            Audit::enabled=false;
            std::println("[OBSERVE] polymorphic retirement cycle={} concrete={}/{} allocations={}/{} live={}",
                cycle+1,concreteDestructors,3*(cycle+1),Audit::allocated,Audit::retired,Audit::live);
            Check(concreteDestructors==3*(cycle+1) && Audit::valid && Audit::live==0 && Audit::allocated==Audit::retired,
                "Base-pointer retirement skipped concrete destructors or leaked derived shape payload");
        }
        std::println("[PASS] polymorphic shape retirement cycles=2 concrete=6/6 allocations={}/{} live=0",
            Audit::allocated,Audit::retired);
    }
#ifdef PHYSICS_SHAPE_SCENE_PROBE
    void SceneCase(std::string_view mode)
    {
        using namespace ::GEngine::Component;
        std::vector<std::unique_ptr<PhysicalShape>> shapes; // Existing successful caller ownership.
        _Scene scene;
        std::array<_Entity,3> entities;
        const std::uint64_t ids[]{23,37,41};
        for(unsigned i=0;i<3;++i)
        {
            entities[i]=SceneOperationChecked([&] { return scene.CreateEntityWithUUID(UUID(ids[i]),"startup"); });
            entities[i].AddComponent<RigidBody3DComponent>().Type=BodyType::Kinematic;
            entities[i].AddComponent<SphereFixture3DComponent>().Radius=.5f;
        }
        if(mode=="radius")entities[1].GetComponent<SphereFixture3DComponent>().Radius=-2.5f;
        Audit::denyAfter=mode=="world-allocation"?1:(mode=="shape-allocation"?3:-1);
        auto started=scene.OnRuntimeStart();
        if(mode!="success")
        {
            Check(!started,"Invalid or denied startup unexpectedly succeeded");
            const auto& error=started.error();
            if(mode=="radius")Check(error.code==PhysicsShapeErrorCode::InvalidRadius && error.entity==37
                && error.radius==-2.5f && error.pointCount==0 && error.operation=="ShapeSphere::Create"
                && error.message=="ShapeSphere requires a finite positive radius","Scene radius diagnostic lost detail");
            else Check(error.code==PhysicsShapeErrorCode::Allocation && Audit::denyAfter==0
                && error.operation=="_Scene::OnPhysics3DStart" && error.entity==(mode=="world-allocation"?0:37)
                && error.radius==(mode=="world-allocation"?0.f:.5f) && error.pointCount==0
                && error.message==(mode=="world-allocation"?"Physics world allocation failed":"Sphere shape allocation failed"),
                "Scene allocation diagnostic lost detail");
            Check(!scene.IsRunning() && scene.GetPhysicsSystem()->GetPhysicsWorld()==nullptr,
                "Failed startup retained running/world state");
            for(auto entity:entities)
            {
                Check(entity.GetComponent<RigidBody3DComponent>().RuntimeBody==nullptr && !entity.Transform().OnScaleChanged,
                    "Failed startup retained body or new shape callback");
                entity.Transform().SetScale(Vec3f(2)); // No retired callback may be invoked.
            }
            const auto& timing=scene.GetPhysicsTiming();
            Check(timing.pendingSeconds==0 && timing.totalSteps==0 && timing.stepsLastUpdate==0,
                "Failed startup retained runtime history");
            Audit::denyAfter=-1;entities[1].GetComponent<SphereFixture3DComponent>().Radius=.5f;
            started=scene.OnRuntimeStart();
        }
        Audit::denyAfter=-1;
        Check(started && scene.IsRunning(),"Clean startup retry did not succeed");
        auto* world=scene.GetPhysicsSystem()->GetPhysicsWorld();Check(world && world->GetPhysicsBodies().size()==3,
            "Successful startup lost body population");
        for(auto* body:world->GetPhysicsBodies())
        {
            Check(body->m_Shape && body->m_Shape->IsValid() && body->m_Shape->GetShapeType()==ShapeType::Sphere,
                "Successful startup changed shape payload");
            shapes.emplace_back(body->m_Shape);
        }
        scene.OnRuntimeStop();
        Check(!scene.IsRunning() && !scene.GetPhysicsSystem()->GetPhysicsWorld(),"Stop retained runtime state");
        for(auto entity:entities)Check(!entity.GetComponent<RigidBody3DComponent>().RuntimeBody,"Stop retained body borrower");
    }
    void SceneStartup()
    {
        SceneCase("success"); // Warm type/registry/logger state before observing owned allocations.
        for(const std::string_view mode:{"radius","world-allocation","shape-allocation","success"})
        {
            Audit::enabled=true;SceneCase(mode);Audit::enabled=false;
            std::println("[OBSERVE] scene startup {} allocations={}/{} live={}",mode,Audit::allocated,Audit::retired,Audit::live);
            Check(Audit::valid && Audit::allocated==Audit::retired && Audit::live==0,
                "Scene startup rollback/retry/caller ownership leaked allocations");
        }
        std::println("[PASS] scene physics startup diagnostics/rollback/callbacks/retry/ownership cases=4 live=0");
    }
#endif
    void Factories()
    {
        auto input=Points({2.f,3.f,4.f});
        auto flat=Points({1.f,1.f,0.f}), nonfinite=Points({1.f,1.f,1.f}), infinite=nonfinite;
        nonfinite[0].x=std::numeric_limits<float>::quiet_NaN();
        infinite[0].z=std::numeric_limits<float>::infinity();
        const std::array<std::vector<Vec3f>,6> invalidPoints{{{}, {Vec3f(0.f)}, flat, nonfinite, infinite,
            Points(Vec3f(Math::NumericalEpsilon*.1f))}};
        const float invalidRadii[]{0.f,-0.f,-1.f,std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
        // Warm logger formatting before observing factory-owned value allocations.
        {auto warm=ShapeBox::Create(input);Check(warm.has_value(),"Warm valid box failed");}
        for(int cycle=0;cycle<2;++cycle)
        {
            input=Points({2.f,3.f,4.f});
            Audit::enabled=true;
            {
                for(float radius:invalidRadii)
                {
                    auto failed=ShapeSphere::Create(radius);Check(!failed,"Invalid radius succeeded");
                    const auto& error=failed.error();
                    Check(error.code==PhysicsShapeErrorCode::InvalidRadius && error.operation=="ShapeSphere::Create"
                        && error.message=="ShapeSphere requires a finite positive radius"
                        && (std::isnan(radius)?std::isnan(error.radius):error.radius==radius)
                        && error.pointCount==0 && error.entity==0,"Radius error lost complete input/diagnostic");
                }
                for(const auto& points:invalidPoints)
                {
                    auto failed=ShapeBox::Create(points);Check(!failed,"Invalid point set succeeded");
                    const auto& error=failed.error();
                    Check(error.code==PhysicsShapeErrorCode::InvalidPointSet && error.operation=="ShapeBox::Create"
                        && error.message=="ShapeBox requires finite points with non-zero extents"
                        && error.pointCount==points.size() && error.radius==0.f && error.entity==0,
                        "Point-set error lost complete input/diagnostic");
                }
                for(float radius:{(std::numeric_limits<float>::min)(),(std::numeric_limits<float>::max)()})
                {
                    auto edge=ShapeSphere::Create(radius);
                    Check(edge && edge->IsValid() && edge->GetRadius()==radius && edge->GetRevision()==0,
                        "Factory changed the existing finite-positive radius acceptance policy");
                }
                auto sphere=ShapeSphere::Create(2.5f);Check(sphere.has_value(),"Valid sphere failed");
                Check(sphere->IsValid() && sphere->GetShapeType()==ShapeType::Sphere && sphere->GetRevision()==0
                    && sphere->GetRadius()==2.5f && sphere->GetCenterOfMass()==Vec3f(0.f),"Sphere metadata changed");
                const auto tensor=sphere->InertiaTensor();
                Check(tensor==Mat3(2.5f) && sphere->GetBounds().mins==Vec3f(-2.5f)
                    && sphere->GetBounds().maxs==Vec3f(2.5f),"Sphere analytic mass/bounds changed");
                Check(sphere->Support({1,0,0},{2,3,4},Quat(1,0,0,0),.5f)==Vec3f(5,3,4),"Sphere analytic support changed");
                auto movedSphere=std::move(*sphere);
                movedSphere.HandleScaleChanged({2,9,9});
                Check(movedSphere.GetRadius()==5.f && movedSphere.GetRevision()==1,"Factory lost absolute base radius or X-axis policy");
                movedSphere.HandleScaleChanged({2,4,4});
                Check(movedSphere.GetRevision()==1,"Repeated sphere scale changed revision");
                auto box=ShapeBox::Create(input);Check(box.has_value(),"Valid box failed");
                Check(box->IsValid() && box->GetShapeType()==ShapeType::Box && box->GetRevision()==1
                    && box->GetCenterOfMass()==Vec3f(0.f),"Box metadata changed");
                Check(box->GetBounds().mins==Vec3f(-2,-3,-4) && box->GetBounds().maxs==Vec3f(2,3,4),"Box bounds changed");
                const auto inertia=box->InertiaTensor();
                Check(Near(inertia[0][0],100.f/12.f) && Near(inertia[1][1],80.f/12.f) && Near(inertia[2][2],52.f/12.f)
                    && inertia[0][1]==0 && inertia[0][2]==0 && inertia[1][2]==0,"Box analytic inertia changed");
                Check(box->Support({1,1,1},{0,0,0},Quat(1,0,0,0),0)==Vec3f(2,3,4),"Box analytic support changed");
                auto movedBox=std::move(*box);input.assign(8,Vec3f(100));
                movedBox.HandleScaleChanged({2,1,.5f});
                Check(movedBox.GetRevision()==2 && movedBox.GetBounds().mins==Vec3f(-4,-3,-2)
                    && movedBox.GetBounds().maxs==Vec3f(4,3,2),"Moved factory box borrowed caller points or lost its base source");
                movedBox.HandleScaleChanged(Vec3f(1));
                Check(movedBox.GetRevision()==3 && movedBox.GetBounds().maxs==Vec3f(2,3,4),"Factory source did not survive scale rebuild");
            }
            Audit::enabled=false;
            Check(Audit::valid && Audit::live==0 && Audit::allocated==Audit::retired,
                "Success/error/moved factory scopes leaked or duplicated owned allocations");
        }
        std::println("[PASS] physics factories diagnostics/analytic-payload/source/move/retirement cycles=2 allocations={}/{} checks={}",
            Audit::allocated,Audit::retired,checks);
    }
}
int main(int argc,char** argv)
{
    ::GEngine::Log::Initialize();
    if(argc==1)Factories();
    else if(argc==2 && std::string_view(argv[1])=="--polymorphic-retirement")PolymorphicRetirement();
#ifdef PHYSICS_SHAPE_SCENE_PROBE
    else if(argc==2 && std::string_view(argv[1])=="--scene-startup")SceneStartup();
#endif
    else return 2;
}
#endif
