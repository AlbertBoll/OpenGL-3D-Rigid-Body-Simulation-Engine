#pragma once
#include "RestingContactDiagnosis.h"
#include "TransformMutationChecks.h"
#include "GeometryAuthoringChecks.h"
#include "Physics/ShapeCapsule.h"
#include "Renderer/RenderExtraction.h"
#include <optional>

namespace PreEditorValidation
{
    inline ::GEngine::PlatformResult Phase24Failure(const char* name)
    {
        ::GEngine::Log::GetCoreLogger()->error("PHASE24_FAIL {}", name);
        return std::unexpected(::GEngine::PlatformError{
            ::GEngine::PlatformErrorCode::Initialization, "Phase24", name});
    }
    inline bool Phase24Close(float a, float b) { return std::abs(a-b) <= 1e-4f; }
    inline bool Phase24Same(const ::GEngine::PhysicsBodyState& a, const ::GEngine::PhysicsBodyState& b)
    {
        return a.handle==b.handle && a.type==b.type && a.position==b.position && a.orientation==b.orientation &&
            a.linearVelocity==b.linearVelocity && a.angularVelocity==b.angularVelocity && a.inverseMass==b.inverseMass &&
            a.material==b.material && a.shapeType==b.shapeType && a.localBounds.mins==b.localBounds.mins &&
            a.localBounds.maxs==b.localBounds.maxs && a.worldBounds.mins==b.worldBounds.mins &&
            a.worldBounds.maxs==b.worldBounds.maxs && a.centerOfMass==b.centerOfMass &&
            a.inverseInertiaBody==b.inverseInertiaBody && a.inverseInertiaWorld==b.inverseInertiaWorld &&
            a.sleeping==b.sleeping && a.inactiveSeconds==b.inactiveSeconds;
    }
    inline std::optional<::GEngine::_Entity> Phase24Sphere(::GEngine::_Scene& scene, const char* name,
        ::GEngine::Component::BodyType type, ::GEngine::Vec3f position)
    {
        auto entity=scene.CreateEntity(name); if (!entity) return {};
        ::GEngine::Component::RigidBody3DComponent body;body.Type=type;
        entity->AddOrReplaceComponent<::GEngine::Component::RigidBody3DComponent>(body);
        entity->AddOrReplaceComponent<::GEngine::Component::SphereFixture3DComponent>();
        entity->Transform().SetTranslation(position); return *entity;
    }
#define P24_REQUIRE(condition, name) do { if (!(condition)) return Phase24Failure(name); ++checks; } while(false)
    inline ::GEngine::PlatformResult CheckPhysicsSceneTransforms(::GEngine::SceneRenderResources& resources,
        ::GEngine::Asset::MeshHandle mesh, ::GEngine::Asset::MaterialInstanceHandle material)
    {
        using namespace ::GEngine; using namespace ::GEngine::Component;
        std::size_t checks=0;
        const float nan=std::numeric_limits<float>::quiet_NaN(), inf=std::numeric_limits<float>::infinity();
        {
            auto scene=CreateRefPtr<_Scene>();
            auto sphere=Phase24Sphere(*scene,"scale-sphere",BodyType::Dynamic,{});
            auto box=scene->CreateEntity("scale-box"), hull=scene->CreateEntity("scale-hull"), capsule=scene->CreateEntity("scale-capsule");
            P24_REQUIRE(sphere && box && hull && capsule,"create scale fixtures");
            sphere->GetComponent<SphereFixture3DComponent>().Radius=2;
            sphere->Transform().Scale=Vec3f(3);
            for (auto entity:{*box,*hull,*capsule}) entity.AddOrReplaceComponent<RigidBody3DComponent>();
            box->AddOrReplaceComponent<BoxFixture3DComponent>();box->Transform().Scale={2,3,4};
            ConvexFixture3DComponent convex;
            for (unsigned i=0;i<8;++i) convex.Shape.points.push_back({i&1?.5f:-.5f,i&2?.5f:-.5f,i&4?.5f:-.5f});
            hull->AddOrReplaceComponent<ConvexFixture3DComponent>(convex);hull->Transform().Scale=Vec3f(3);
            capsule->AddOrReplaceComponent<CapsuleFixture3DComponent>();capsule->Transform().Scale=Vec3f(2);
            P24_REQUIRE(scene->OnRuntimeStart(),"start scale fixtures");
            auto* system=scene->GetPhysicsSystem();auto* world=system->GetPhysicsWorld();world->SetGravity({});
            auto sh=*scene->GetBodyBinding(*sphere), bh=*scene->GetBodyBinding(*box), hh=*scene->GetBodyBinding(*hull), ch=*scene->GetBodyBinding(*capsule);
            P24_REQUIRE(world->ReadBody(sh)->localBounds.WidthX()==4 && world->ReadBody(bh)->localBounds.WidthY()==3 &&
                world->ReadBody(hh)->localBounds.WidthX()==1 && world->ReadBody(ch)->localBounds.WidthX()==4,"distinct initial shape bases");
            unsigned signals=0,commits=0;
            auto pre=sphere->Transform().OnScaleChanged.ConnectScoped([&](const Vec3f&){++signals;});
            auto post=scene->SubscribeToAuthoritativeTransformChanges([&](const AuthoritativeTransformChange&){++commits;});
            P24_REQUIRE(pre && post,"scale observers");
            P24_REQUIRE(scene->SetLocalScale(*sphere,{2,9,-4}) && world->ReadBody(sh)->localBounds.WidthX()==8 && signals==1 && commits==1,"sphere X-only scale once");
            const auto rev=PhysicsTestAccess::Body(*sphere)->m_Shape->GetRevision();
            P24_REQUIRE(scene->SetLocalScale(*sphere,{2,8,-3}) && PhysicsTestAccess::Body(*sphere)->m_Shape->GetRevision()==rev,"valid unchanged sphere geometry");
            P24_REQUIRE(scene->SetLocalScale(*box,{-.5f,1.5f,2}) && world->ReadBody(bh)->localBounds.WidthX()==1 &&
                world->ReadBody(bh)->localBounds.WidthY()==4.5f && world->ReadBody(bh)->localBounds.WidthZ()==8,"box stored initial-scale base");
            P24_REQUIRE(scene->SetLocalScale(*hull,{-.5f,1.5f,2}) && world->ReadBody(hh)->localBounds.WidthX()==.5f,"hull unscaled initial base");
            P24_REQUIRE(scene->SetLocalScale(*capsule,3.f) && world->ReadBody(ch)->localBounds.WidthX()==6,"capsule original base");
            for (auto entity:{*sphere,*box,*hull,*capsule})
            {
                const auto handle=*scene->GetBodyBinding(entity);
                for (auto bad:{Vec3f{0,1,1},Vec3f{nan,1,1},Vec3f{inf,1,1},Vec3f(std::numeric_limits<float>::max())})
                {
                    const auto before=*world->ReadBody(handle);const auto transform=entity.Transform();
                    auto* owner=PhysicsTestAccess::Body(entity)->m_Shape;const auto revision=owner->GetRevision();
                    const auto history=*scene->ReadBodyTransform(entity);const auto events=commits, hooks=signals;
                    const auto rejected=scene->SetLocalScale(entity,bad);
                    P24_REQUIRE(!rejected && Phase24Same(before,*world->ReadBody(handle)) && entity.Transform().Scale==transform.Scale &&
                        PhysicsTestAccess::Body(entity)->m_Shape==owner && owner->GetRevision()==revision && commits==events && signals==hooks &&
                        scene->ReadBodyTransform(entity)->previous.translation==history.previous.translation,"rejection preserves all observed state");
                }
                const auto events=commits;const auto same=scene->SetLocalScale(entity,entity.Transform().Scale);
                P24_REQUIRE(same && !same->changed && commits==events,"exact scale no-op silent");
            }
            P24_REQUIRE(!scene->SetLocalScale(*capsule,{2,3,2}) && !scene->SetLocalScale(*sphere,{-1,2,3}),"shape-specific unsupported scale rejected");
            const auto base=*world->ReadBody(bh);const auto revision=PhysicsTestAccess::Body(*box)->m_Shape->GetRevision();
            P24_REQUIRE(scene->SetLocalScale(*box,2.f) && scene->SetLocalScale(*box,{-.5f,1.5f,2}) &&
                world->ReadBody(bh)->localBounds.mins==base.localBounds.mins && PhysicsTestAccess::Body(*box)->m_Shape->GetRevision()==revision+2,"absolute repeated scaling with no callback double application");
            const auto old=*world->ReadBody(sh);auto* oldShape=PhysicsTestAccess::Body(*sphere)->m_Shape;
            P24_REQUIRE(!scene->ReplaceBodyCollider(*sphere,CapsuleShapeDesc{-1,.5f}) && Phase24Same(old,*world->ReadBody(sh)) &&
                PhysicsTestAccess::Body(*sphere)->m_Shape==oldShape,"failed replacement preserves world owner");
            P24_REQUIRE(scene->ReplaceBodyCollider(*sphere,SphereShapeDesc{3}) && *scene->GetBodyBinding(*sphere)==sh &&
                world->GetBodyCount()==4 && PhysicsTestAccess::Body(*sphere)->m_Shape!=oldShape && world->ReadBody(sh)->localBounds.WidthX()==6,"replacement preserves body handle and world ownership");
            P24_REQUIRE(scene->SetLocalScale(*sphere,3.f) && world->ReadBody(sh)->localBounds.WidthX()==18,"replacement establishes explicit new base");
            P24_REQUIRE(world->SetBodyVelocity(sh,{},{}),"quiet sleeping fixture velocity");
            auto* sleepy=PhysicsTestAccess::Body(*sphere);
            sleepy->UpdateSleepTimer(sleepy->GetSleepSettings().inactivitySeconds);
            P24_REQUIRE(world->TrySleepBody(sh) && world->ReadBody(sh)->sleeping,"sleeping scale fixture");
            P24_REQUIRE(!scene->SetLocalScale(*sphere,0.f) && world->ReadBody(sh)->sleeping,"rejection does not wake");
            P24_REQUIRE(scene->SetLocalScale(*sphere,2.f) && !world->ReadBody(sh)->sleeping,"accepted shape change wakes");
            sphere->Transform().SetScale({-1,2,3});
            P24_REQUIRE(sphere->Transform().Scale==Vec3f(-1,2,3) && world->ReadBody(sh)->localBounds.WidthX()==12,"legacy Component rejection remains independent");
            auto copy=_Scene::Copy(scene);auto duplicate=scene->DuplicateEntity(*sphere);
            P24_REQUIRE(copy && duplicate && !duplicate->GetComponent<RigidBody3DComponent>().RuntimeBody &&
                !(*copy)->GetEntityByUUID(sphere->GetUUID()).GetComponent<RigidBody3DComponent>().RuntimeBody,"copy and duplicate clear runtime ownership");
            const auto count=world->GetBodyCount();box->RemoveComponent<RigidBody3DComponent>();
            P24_REQUIRE(world->GetBodyCount()==count-1 && !world->ReadBody(bh) && !box->Transform().OnScaleChanged,"body component removal retires binding");
            hull->RemoveComponent<Transform3DComponent>();
            P24_REQUIRE(!world->ReadBody(hh) && !hull->GetComponent<RigidBody3DComponent>().RuntimeBody,"Transform removal retires binding");
            P24_REQUIRE(scene->DestroyEntity(*hull) && scene->DestroyEntity(*box),"destroy detached entities");
            scene->OnRuntimeStop();
            P24_REQUIRE(!capsule->Transform().OnScaleChanged && !capsule->GetComponent<RigidBody3DComponent>().RuntimeBody,"stop disconnects scale bridge");
            sphere->Transform().Scale=Vec3f(1);
            P24_REQUIRE(scene->OnRuntimeStart() && *scene->GetBodyBinding(*sphere)!=sh,"restart gives fresh identity");
            P24_REQUIRE(!system->SetBodyScale(sh,Vec3f(2)),"stale handle rejected after restart");
        }
        {
            auto scene=CreateRefPtr<_Scene>();auto valid=Phase24Sphere(*scene,"rollback-valid",BodyType::Static,{});
            P24_REQUIRE(valid && scene->OnRuntimeStart(),"existing world before failed restart");
            auto bad=scene->CreateEntity("rollback-invalid");P24_REQUIRE(bad,"invalid startup entity");
            bad->AddOrReplaceComponent<RigidBody3DComponent>();CapsuleFixture3DComponent invalid;invalid.Shape.radius=0;
            bad->AddOrReplaceComponent<CapsuleFixture3DComponent>(invalid);
            P24_REQUIRE(!scene->OnRuntimeStart() && !scene->GetPhysicsSystem()->GetPhysicsWorld() &&
                !valid->GetComponent<RigidBody3DComponent>().RuntimeBody && !valid->Transform().OnScaleChanged,
                "malformed startup retires old world and leaves no partial binding");
        }
        for (double step:{1.0/60.0,1.0/120.0})
        {
            auto scene=CreateRefPtr<_Scene>();P24_REQUIRE(scene->SetPhysicsStep(Seconds(step)),"choose admitted clock");
            std::array<_Entity,3> bodies;
            unsigned i=0;
            for (auto type:{BodyType::Static,BodyType::Kinematic,BodyType::Dynamic})
            {
                auto entity=Phase24Sphere(*scene,"clock",type,{float(i)*10,0,0});P24_REQUIRE(entity,"clock entity");
                entity->GetComponent<SphereFixture3DComponent>().Property.m_LinearVelocity={6,0,0};bodies[i++]=*entity;
            }
            P24_REQUIRE(scene->OnRuntimeStart(),"clock world");auto* world=scene->GetPhysicsSystem()->GetPhysicsWorld();world->SetGravity({});
            scene->Update(Timestep(step*1.5));
            P24_REQUIRE(scene->GetPhysicsTiming().totalSteps==1,"one fixed step at fractional presentation");
            for (unsigned j=0;j<3;++j)
            {
                auto pose=scene->ReadBodyTransform(bodies[j]);const float displacement=j?float(6*step):0;
                P24_REQUIRE(pose && Phase24Close(pose->current.translation.x,float(j)*10+displacement) &&
                    Phase24Close(pose->presentation.translation.x,float(j)*10+displacement*.5f),"mode motion and render-only interpolation");
                const auto before=*world->ReadBody(pose->body);const auto ticks=scene->GetPhysicsTiming().totalSteps;
                for (unsigned n=0;n<8;++n) (void)scene->ReadBodyTransform(bodies[j]);
                P24_REQUIRE(Phase24Same(before,*world->ReadBody(pose->body)) && scene->GetPhysicsTiming().totalSteps==ticks,"presentation is pure");
                P24_REQUIRE(!scene->SetLocalPose(bodies[j],{nan,0,0},{1,0,0,0}) && Phase24Same(before,*world->ReadBody(pose->body)),"invalid teleport preserves body");
                P24_REQUIRE(scene->SetLocalTranslation(bodies[j],{float(j)*10,5,0}) && scene->ReadBodyTransform(bodies[j])->presentation.translation.y==5 &&
                    world->ReadBody(pose->body)->linearVelocity==before.linearVelocity,"teleport snaps and preserves velocity");
            }
            scene->SetPaused(true);const auto ticks=scene->GetPhysicsTiming().totalSteps;scene->Update(Timestep(1));
            P24_REQUIRE(scene->GetPhysicsTiming().totalSteps==ticks,"paused clock does not advance");
            scene->SetPaused(false);scene->Update(Timestep(1));
            P24_REQUIRE(scene->GetPhysicsTiming().stepsLastUpdate==2 && scene->GetPhysicsTiming().discardedSeconds>=.75 &&
                scene->GetRenderInterpolationAlpha()==1,"bounded backlog and clamped presentation");
            PhysicsWorld other;P24_REQUIRE(!other.ReadBody(*scene->GetBodyBinding(bodies[1])),"foreign world handle rejected");
            const auto removed=*scene->GetBodyBinding(bodies[1]);P24_REQUIRE(scene->DestroyEntity(bodies[1]) && !world->ReadBody(removed),"body deletion invalidates handle");
        }
        {
            auto scene=CreateRefPtr<_Scene>();auto floor=Phase24Sphere(*scene,"contact-static",BodyType::Static,{});
            auto moving=Phase24Sphere(*scene,"contact-dynamic",BodyType::Dynamic,{0,1.9f,0});
            P24_REQUIRE(floor && moving && scene->OnRuntimeStart(),"contact fixture");
            scene->GetPhysicsSystem()->GetPhysicsWorld()->SetGravity({});scene->Update(Timestep(1.0/60.0));
            const auto islands=scene->GetPhysicsSystem()->GetContactIslands().size();P24_REQUIRE(islands>0,"contact invalidation premise");
            P24_REQUIRE(!scene->SetLocalScale(*moving,0.f) && scene->GetPhysicsSystem()->GetContactIslands().size()==islands,"rejection preserves contacts");
            P24_REQUIRE(scene->SetLocalScale(*moving,1.1f) && scene->GetPhysicsSystem()->GetContactIslands().empty(),"accepted scale invalidates contact graph");
        }
        {
            auto scene=CreateRefPtr<_Scene>();auto entity=Phase24Sphere(*scene,"retained",BodyType::Kinematic,{});
            P24_REQUIRE(entity && resources.AssignRenderable(*entity,{mesh,material}) && scene->OnRuntimeStart(),"render fixture");
            P24_REQUIRE(scene->GetPhysicsSystem()->GetPhysicsWorld()->SetBodyVelocity(*scene->GetBodyBinding(*entity),{6,0,0},{}),"render velocity");
            scene->Update(Timestep(1.0/40.0));std::optional<RenderFrame> retained;
            std::array<FrameDebugLine,48> storage;DebugGeometry outline(storage);auto pose=scene->ReadBodyTransform(*entity);
            P24_REQUIRE(pose && scene->GetPhysicsSystem()->AppendPresentationGeometry(outline,pose->body,pose->presentation.translation,pose->presentation.rotation),"presentation outline");
            {
                auto access=resources.Publication().BeginFrame();RenderExtractionStats stats;
                auto frame=ExtractRenderFrame(*scene,resources.ForFrame(access),stats,{},outline.Lines());
                P24_REQUIRE(frame && frame->Draws().size()==1 && frame->DebugLines().size()==48,"frozen draw and outline");
                retained.emplace(std::move(*frame));
            }
            const auto matrix=retained->Draws()[0].worldTransform;const auto line=retained->DebugLines()[0];
            P24_REQUIRE(Phase24Close(matrix[3].x,.05f),"frame captures interpolated translation");
            P24_REQUIRE(scene->SetLocalTranslation(*entity,{9,0,0}) && scene->SetLocalScale(*entity,2.f),"edit after retained frame");
            P24_REQUIRE(retained->Draws()[0].worldTransform==matrix && retained->DebugLines()[0].start==line.start,"retained frame immutable across body edits");
            std::array<FrameDebugLine,1> shortStorage;DebugGeometry shortOutline(shortStorage);
            P24_REQUIRE(!scene->GetPhysicsSystem()->AppendPresentationGeometry(shortOutline,pose->body,{},{1,0,0,0}) && shortOutline.Lines().empty(),"presentation primitive capacity atomic");
        }
        if (auto old=CheckAuthoritativeTransformMutations(resources,mesh,material); !old) return old;
        Log::GetCoreLogger()->info("PHASE24_FOCUSED_PASS checks={}",checks);
        return {};
    }
#undef P24_REQUIRE

    struct Phase24FrameProbe
    {
        ::GEngine::_Scene* scene{}; ::GEngine::_Entity selected;
        unsigned frames{}, observations{}, picks{};bool failed{};const char* failure{};
        ::GEngine::Mat4 matrix{1};::GEngine::Vec3f position{};
        void Observe(const ::GEngine::RenderFrame& frame)
        {
            if (!scene || !selected) return;
            const auto pose=scene->ReadBodyTransform(selected);const auto id=scene->RenderData().Identify(selected);
            if (!pose || !id) {failed=true;failure="missing frame body identity";return;}
            const auto before=scene->GetPhysicsSystem()->ReadBodyPose(pose->body);const auto ticks=scene->GetPhysicsTiming().totalSteps;
            bool found=false,outline=false;position=pose->presentation.translation;
            for (const auto& draw:frame.Draws()) if (draw.entity==*id)
            { matrix=draw.worldTransform;found=glm::length(::GEngine::Vec3f(matrix[3])-position)<=1e-4f; }
            for (const auto& line:frame.DebugLines()) if (line.color==glm::vec4(1,.2f,1,1)) outline=true;
            const auto after=scene->GetPhysicsSystem()->ReadBodyPose(pose->body);
            if (!found || !outline || !before || !after || before->position!=after->position || before->orientation!=after->orientation || ticks!=scene->GetPhysicsTiming().totalSteps)
            {failed=true;failure="frozen frame pose/outline or pure sampling";}
            ++observations;
        }
    };
    inline Phase24FrameProbe phase24Frame;
}

#ifdef GENGINE_PHASE24_COLLIDER_VALIDATION
#include "Renderer/FrameSubmission.h"
#include <stb_image/stb_image_write.h>
#include <cstring>
namespace Rbs
{
    std::expected<::GEngine::CollisionShapeDesc, ::GEngine::GeometryAuthoringCode>
        FitMeshCollider(const ::GEngine::MeshAsset&, const ::GEngine::Vec3f&, ::GEngine::ShapeType);
    std::expected<void, std::string> ReplaceMeshCollider(::GEngine::_Scene&, ::GEngine::SceneRenderResources&,
                                                       ::GEngine::_Entity, ::GEngine::ShapeType);
    std::uint64_t MeshColliderFitCalls();
}
namespace GEngine
{
    struct Phase24ColliderCheckAccess
    {
        static auto& Manifolds(PhysicsSystem& s) {return s.m_Manifolds;}
        static const auto& Pairs(PhysicsSystem& s) {return s.m_CollisionPairs;}
        static const auto& Contacts(PhysicsSystem& s) {return s.m_Contacts;}
        static const auto& Broadphase(PhysicsSystem& s) {return s.m_Broadphase.GetLastStats();}
    };
}

#ifdef GENGINE_PHASE24_REPLACEMENT_MEASUREMENT
#if !defined(GENGINE_PHASE24_COLLIDER_VALIDATION) || !defined(GENGINE_PHASE24_ISOLATED_BUILD) || defined(GENGINE_PHASE24_OUTLINE_QUALIFICATION)
#error Replacement measurement belongs only to its isolated collider qualification EXE.
#endif
#include <atomic>
#include <new.h>
#include <malloc.h>
namespace Phase24ReplacementAllocation
{
    inline std::atomic<std::uint64_t> count{},bytes{};
    inline thread_local bool enabled{};
    inline void Record(std::size_t size) noexcept
    { if(enabled){count.fetch_add(1,std::memory_order_relaxed);bytes.fetch_add(size,std::memory_order_relaxed);} }
    inline void* Allocate(std::size_t size,std::size_t alignment=0)
    {
        const auto requested=size;size=(std::max)(size,std::size_t{1});
        for(;;) {
            void* result=alignment?_aligned_malloc(size,alignment):std::malloc(size);
            if(result) {Record(requested);return result;}
            if(!_callnewh(size)) throw std::bad_alloc();
        }
    }
}
void* operator new(std::size_t size) {return Phase24ReplacementAllocation::Allocate(size);}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete[](void* value) noexcept {::operator delete(value);}
void operator delete(void* value,std::size_t) noexcept {::operator delete(value);}
void operator delete[](void* value,std::size_t) noexcept {::operator delete(value);}
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {try{return ::operator new(size);}catch(...){return nullptr;}}
void* operator new[](std::size_t size,const std::nothrow_t&) noexcept {try{return ::operator new[](size);}catch(...){return nullptr;}}
void operator delete(void* value,const std::nothrow_t&) noexcept {::operator delete(value);}
void operator delete[](void* value,const std::nothrow_t&) noexcept {::operator delete[](value);}
void* operator new(std::size_t size,std::align_val_t alignment) {return Phase24ReplacementAllocation::Allocate(size,std::size_t(alignment));}
void* operator new[](std::size_t size,std::align_val_t alignment) {return ::operator new(size,alignment);}
void operator delete(void* value,std::align_val_t) noexcept {_aligned_free(value);}
void operator delete[](void* value,std::align_val_t alignment) noexcept {::operator delete(value,alignment);}
void operator delete(void* value,std::size_t,std::align_val_t alignment) noexcept {::operator delete(value,alignment);}
void operator delete[](void* value,std::size_t,std::align_val_t alignment) noexcept {::operator delete(value,alignment);}
void* operator new(std::size_t size,std::align_val_t alignment,const std::nothrow_t&) noexcept {try{return ::operator new(size,alignment);}catch(...){return nullptr;}}
void* operator new[](std::size_t size,std::align_val_t alignment,const std::nothrow_t&) noexcept {try{return ::operator new[](size,alignment);}catch(...){return nullptr;}}
void operator delete(void* value,std::align_val_t alignment,const std::nothrow_t&) noexcept {::operator delete(value,alignment);}
void operator delete[](void* value,std::align_val_t alignment,const std::nothrow_t&) noexcept {::operator delete(value,alignment);}


#endif

namespace PreEditorValidation::ColliderReplacement
{
    using namespace ::GEngine;
    inline std::filesystem::path output;
    inline std::ofstream rows,contacts,checks,references;
    inline unsigned failures{},checkCount{},caseIndex{},tick{},lastReplacement{};
    inline _Scene* active{};
    inline _Entity target;
    inline PhysicsDiagnostic::Observer observer;
    inline void Check(bool passed,const char* label)
    {++checkCount;if(!passed)++failures;checks<<caseIndex<<','<<tick<<','<<passed<<','<<label<<'\n';}
    inline _Entity Make(_Scene& scene,const char* name,int shape,Vec3f position,Quat rotation={1,0,0,0},Vec3f scale=Vec3f(1),bool dynamic=true)
    {
        auto made=scene.CreateEntity(name);if(!made)return {};
        auto e=*made;RigidBody3DComponent body;body.Type=dynamic?BodyType::Dynamic:BodyType::Static;e.AddOrReplaceComponent<RigidBody3DComponent>(body);
        if(shape==0)e.AddOrReplaceComponent<SphereFixture3DComponent>();
        else if(shape==1)e.AddOrReplaceComponent<BoxFixture3DComponent>();
        else e.AddOrReplaceComponent<CapsuleFixture3DComponent>();
        e.Transform().SetTranslation(position);e.Transform().SetRotation(rotation);e.Transform().SetScale(scale);return e;
    }
    inline RefPtr<_Scene> Fixture(int shape,Vec3f position,Quat rotation,Vec3f scale,_Entity& body)
    {
        auto scene=CreateRefPtr<_Scene>();auto floor=Make(*scene,"replacement floor",1,{}, {1,0,0,0},Vec3f(1),false);
        floor.GetComponent<BoxFixture3DComponent>().Shape.halfExtents={50,.5f,50};
        body=Make(*scene,"replacement Sphere mesh",shape,position,rotation,scale);
        if(!scene->OnRuntimeStart())return {};return scene;
    }
    inline CollisionShapeDesc Description(int shape)
    {if(shape==1)return BoxShapeDesc{Vec3f(.5f)};if(shape==2)return CapsuleShapeDesc{1,.5f};return SphereShapeDesc{1};}
    inline double Difference(const Mat3& a,const Mat3& b)
    {double d=0;for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)d=(std::max)(d,double(std::abs(a[c][r]-b[c][r])));return d;}
    inline RigidBody3D Fresh(const RigidBody3D& b)
    {RigidBody3D copy;copy.Type=b.Type;copy.m_Position=b.m_Position;copy.m_Orientation=b.m_Orientation;copy.m_InvMass=b.m_InvMass;copy.m_Shape=b.m_Shape;return copy;}
    inline void Vector(std::ostream& s,Vec3f v){s<<','<<v.x<<','<<v.y<<','<<v.z;}
    inline void Matrix(std::ostream& s,const Mat3& m){for(unsigned c=0;c<3;++c)Vector(s,m[c]);}
    inline void Snapshot(const char* boundary,int stage=-1)
    {
        if(!active || !target)return;auto* system=active->GetPhysicsSystem();auto* b=PhysicsTestAccess::Body(target);if(!b || !b->m_Shape)return;
        const auto handle=*active->GetBodyBinding(target);const auto cache=*system->InspectColliderCache(handle);
        auto fresh=Fresh(*b);const auto bounds=b->m_Shape->GetBounds(b->m_Position,b->m_Orientation),local=b->m_Shape->GetBounds();
        const auto lower=b->m_Shape->Support({0,-1,0},b->m_Position,b->m_Orientation,0),upper=b->m_Shape->Support({0,1,0},b->m_Position,b->m_Orientation,0);
        double analyticLower=bounds.mins.y;
        if(b->m_Shape->GetShapeType()==ShapeType::Sphere)analyticLower=b->m_Position.y-static_cast<ShapeSphere*>(b->m_Shape)->GetRadius();
        if(b->m_Shape->GetShapeType()==ShapeType::Capsule){auto* c=static_cast<ShapeCapsule*>(b->m_Shape);analyticLower=b->m_Position.y-c->GetRadius()-c->GetHalfLength()*std::abs((b->m_Orientation*Vec3f(0,1,0)).y);}
        rows<<caseIndex<<','<<tick<<','<<boundary<<','<<stage<<','<<handle.index<<','<<handle.generation<<','<<handle.registry<<','<<int(b->m_Shape->GetShapeType())<<','<<std::uintptr_t(cache.shape)<<','<<std::uintptr_t(cache.owner)<<','<<cache.revision;
        Vector(rows,b->m_Position);rows<<','<<b->m_Orientation.w<<','<<b->m_Orientation.x<<','<<b->m_Orientation.y<<','<<b->m_Orientation.z;
        Vector(rows,b->m_LinearVelocity);Vector(rows,b->m_AngularVelocity);rows<<','<<b->m_InvMass;Vector(rows,target.Transform().Scale);
        Vector(rows,local.mins);Vector(rows,local.maxs);Vector(rows,bounds.mins);Vector(rows,bounds.maxs);Vector(rows,lower);Vector(rows,upper);rows<<','<<analyticLower<<','<<b->m_Position.y-lower.y;
        Vector(rows,b->m_Shape->GetCenterOfMass());Matrix(rows,b->m_Shape->InertiaTensor());Matrix(rows,fresh.GetInverseInertiaTensorBodySpace());Matrix(rows,fresh.GetInverseInertiaTensorWorldSpace());
        rows<<','<<cache.inertiaValid<<','<<std::uintptr_t(cache.inertiaShape)<<','<<cache.inertiaRevision;Matrix(rows,cache.inverseInertiaBody);
        rows<<','<<cache.boundsValid<<','<<std::uintptr_t(cache.boundsShape)<<','<<cache.boundsRevision;Vector(rows,cache.bounds.mins);Vector(rows,cache.bounds.maxs);
        const auto& broad=Phase24ColliderCheckAccess::Broadphase(*system);
        rows<<','<<b->IsSleeping()<<','<<b->GetInactiveSeconds()<<','<<Phase24ColliderCheckAccess::Pairs(*system).size()<<','<<broad.fullSortCount<<','<<broad.axisOverlapCount<<','<<broad.aabbRejectedCount<<','<<Phase24ColliderCheckAccess::Manifolds(*system).GetContactCount()<<','<<Phase24ColliderCheckAccess::Contacts(*system).size();
        const auto presented=active->ReadBodyTransform(target);rows<<','<<active->GetRenderInterpolationAlpha();
        if(presented){Vector(rows,presented->previous.translation);Vector(rows,presented->current.translation);Vector(rows,presented->presentation.translation);rows<<','<<presented->presentation.rotation.w<<','<<presented->presentation.rotation.x<<','<<presented->presentation.rotation.y<<','<<presented->presentation.rotation.z;}else rows<<",,,,,,,,,,,,,";
        rows<<'\n';
        auto& manifolds=Phase24ColliderCheckAccess::Manifolds(*system).m_Manifolds;
        for(unsigned m=0;m<manifolds.size();++m)for(int i=0;i<manifolds[m].GetNumContacts();++i){const auto c=manifolds[m].GetContact(i);contacts<<caseIndex<<','<<tick<<','<<boundary<<','<<stage<<','<<m<<','<<i<<','<<c.m_BodyA->GetIdentity().GetSlot()<<','<<c.m_BodyB->GetIdentity().GetSlot();Vector(contacts,c.normal);Vector(contacts,c.ptOnA_WorldSpace);Vector(contacts,c.ptOnB_WorldSpace);Vector(contacts,c.ptOnA_LocalSpace);Vector(contacts,c.ptOnB_LocalSpace);contacts<<','<<c.separationDistance<<','<<c.timeOfImpact<<','<<c.featureA<<','<<c.featureB<<'\n';}
        Check(Math::IsFinite(b->m_Position) && Math::IsFinite(b->m_Orientation) && Math::IsFinite(b->m_LinearVelocity) && Math::IsFinite(b->m_AngularVelocity),"finite physical state");
        Check(std::abs(lower.y-analyticLower)<=1e-4,"analytic world support matches actual shape support");
        Check(cache.shape==cache.owner,"world owns current collider");
    }
    inline void Boundary(void*,PhysicsDiagnostic::Stage stage,const PhysicsWorld&,ManifoldCollector&,std::span<const ContactIsland>,std::size_t,int) noexcept
    {if(tick-lastReplacement<=16 || (PhysicsTestAccess::Body(target)->m_Position.y<.7f))Snapshot("physics-stage",int(stage));}
    inline bool SameMotion(const PhysicsDiagnostic::BodyState& a,const PhysicsDiagnostic::BodyState& b)
    {return a.position==b.position && a.orientation==b.orientation && a.linear==b.linear && a.angular==b.angular;}
    inline void Replace(const CollisionShapeDesc& desc)
    {
        auto* body=PhysicsTestAccess::Body(target);const auto motion=PhysicsDiagnostic::Capture(*body);const auto handle=*active->GetBodyBinding(target);const auto mass=body->m_InvMass;
        Snapshot("before-replacement");Check(bool(active->ReplaceBodyCollider(target,desc)),"runtime Scene replacement accepted");Snapshot("after-replacement-before-step");
        Check(*active->GetBodyBinding(target)==handle && PhysicsTestAccess::Body(target)==body,"body handle and address preserved");
        Check(SameMotion(motion,PhysicsDiagnostic::Capture(*body)) && body->m_InvMass==mass,"replacement preserves pose velocities and mass");
        const auto cache=active->GetPhysicsSystem()->InspectColliderCache(handle);
        Check(cache && !cache->inertiaValid && !cache->boundsValid,"accepted replacement invalidates derived caches independently of allocator reuse");
        unsigned touching=0;auto* system=active->GetPhysicsSystem();
        for(auto& m:Phase24ColliderCheckAccess::Manifolds(*system).m_Manifolds)for(int i=0;i<m.GetNumContacts();++i){const auto c=m.GetContact(i);touching+=c.m_BodyA==body || c.m_BodyB==body;}
        for(const auto& c:Phase24ColliderCheckAccess::Contacts(*system))touching+=c.m_BodyA==body || c.m_BodyB==body;
        Check(!body->IsSleeping() && touching==0 && Phase24ColliderCheckAccess::Pairs(*system).empty(),"replacement wakes and retires affected contacts and broadphase pairs");
        // Presentation retains the existing interpolation/normalization math;
        // even mix(p,p,alpha) can differ by one ULP. Physics invariants above
        // remain exact; presentation uses the established Phase24 tolerance.
        const auto pose=active->ReadBodyTransform(target);Check(pose &&
            glm::length(pose->presentation.translation-body->m_Position)<=1e-4f &&
            (std::min)(glm::length(pose->presentation.rotation-body->m_Orientation),glm::length(pose->presentation.rotation+body->m_Orientation))<=1e-4f,
            "replacement presentation matches preserved body within existing numerical criterion");
        lastReplacement=tick;
    }
    inline bool SameContact(const contact_t& a,const contact_t& b)
    {
        return a.m_BodyA==b.m_BodyA && a.m_BodyB==b.m_BodyB && a.normal==b.normal &&
            a.ptOnA_WorldSpace==b.ptOnA_WorldSpace && a.ptOnB_WorldSpace==b.ptOnB_WorldSpace &&
            a.ptOnA_LocalSpace==b.ptOnA_LocalSpace && a.ptOnB_LocalSpace==b.ptOnB_LocalSpace &&
            a.separationDistance==b.separationDistance && a.timeOfImpact==b.timeOfImpact &&
            a.featureA==b.featureA && a.featureB==b.featureB;
    }
    inline Vec3f MeshPosition(const MeshAsset& mesh,std::size_t index)
    {
        const auto layout=mesh.Layout();for(const auto& a:layout.attributes)if(a.semantic==VertexSemantic::Position){
            std::array<float,3> p;std::memcpy(p.data(),mesh.Vertices().data()+index*layout.strideBytes+a.offsetBytes,sizeof(p));return {p[0],p[1],p[2]};}
        return Vec3f(std::numeric_limits<float>::quiet_NaN());
    }
    inline void CheckFittedGeometry(const PhysicalShape& shape,const MeshAsset& mesh,Vec3f scale)
    {
        const auto bounds=shape.GetBounds();bool contains=true;
        for(std::size_t i=0;i<mesh.VertexCount();++i){const auto p=MeshPosition(mesh,i)*scale;
            if(shape.GetShapeType()==ShapeType::Sphere)contains&=glm::length(p)<=static_cast<const ShapeSphere&>(shape).GetRadius()+1e-4f;
            else if(shape.GetShapeType()==ShapeType::Capsule){const auto& c=static_cast<const ShapeCapsule&>(shape);const Vec3f d(p.x,(std::max)(0.f,std::abs(p.y)-c.GetHalfLength()),p.z);contains&=glm::length(d)<=c.GetRadius()+1e-4f;}
            else contains&=glm::all(glm::greaterThanEqual(p,bounds.mins-Vec3f(1e-4f))) && glm::all(glm::lessThanEqual(p,bounds.maxs+Vec3f(1e-4f)));}
        Check(contains,"fitted collider contains actual scaled mesh vertices around unchanged pivot");
    }
    inline std::uint64_t expectedRuntimeFitCalls{};
    inline void FitReplace(SceneRenderResources& resources,ShapeType type)
    {
        auto* b=PhysicsTestAccess::Body(target);const auto before=PhysicsDiagnostic::Capture(*b);
        const auto handle=*active->GetBodyBinding(target);const auto mass=b->m_InvMass;
        const auto render=target.GetComponent<MeshRendererComponent>();const auto scale=target.Transform().Scale;
        Snapshot("before-mesh-fit");const auto calls=Rbs::MeshColliderFitCalls();
        const auto result=Rbs::ReplaceMeshCollider(*active,resources,target,type);
        Check(bool(result),"shared RBS mesh-fit switch operation accepted");
        Check(Rbs::MeshColliderFitCalls()==calls+1,"exactly one command-only fit per replacement");
        expectedRuntimeFitCalls=Rbs::MeshColliderFitCalls();Snapshot("after-mesh-fit-before-step");
        Check(*active->GetBodyBinding(target)==handle && PhysicsTestAccess::Body(target)==b && SameMotion(before,PhysicsDiagnostic::Capture(*b)) && b->m_InvMass==mass,"mesh fit preserves handle body pose velocities and mass");
        const auto& afterRender=target.GetComponent<MeshRendererComponent>();
        Check(afterRender.mesh==render.mesh && afterRender.material==render.material && target.Transform().Scale==scale,"mesh fit preserves mesh material and Transform scale");
        const auto cache=*active->GetPhysicsSystem()->InspectColliderCache(handle);
        Check(cache.owner==cache.shape && !cache.inertiaValid && !cache.boundsValid && !b->IsSleeping(),"mesh-fit commit preserves ownership invalidates derived data and wakes");
        const auto source=resources.GeometrySource(render.mesh);
        Check(bool(source),"fitted actual mesh source remains available");if(source)CheckFittedGeometry(*b->m_Shape,*source->source,scale);
        auto fresh=Fresh(*b);Check(Difference(b->GetInverseInertiaTensorBodySpace(),fresh.GetInverseInertiaTensorBodySpace())<=1e-4,"fitted inertia recomputes for accepted geometry");
        lastReplacement=tick;
    }
    inline void FittingChecks(SceneRenderResources& resources,Asset::MeshHandle sphereMesh,Asset::MaterialInstanceHandle material)
    {
        caseIndex=20;tick=0;
        GeometryTemplates::Cube cube{2,4,6};cube.Options.Pivot=GeometryTemplates::PivotLocation::MinimumCorner;
        GeometryTemplates::Capsule capsule{.7f,4};capsule.Options.Pivot=GeometryTemplates::PivotLocation::Base;
        const auto boxMesh=resources.PublishGeometry(cube),capsuleMesh=resources.PublishGeometry(capsule);
        Check(boxMesh && capsuleMesh,"offset Box and Capsule mesh fitting fixtures");
        if(!boxMesh || !capsuleMesh)return;
        std::ofstream fits(output/"mesh-fit-geometry.csv");fits<<std::setprecision(17)<<"mesh,shape,sx,sy,sz,min_x,min_y,min_z,max_x,max_y,max_z,radius,half_length\n";
        const std::array handles{sphereMesh,*boxMesh,*capsuleMesh};
        const std::array scales{Vec3f(1),Vec3f(1.5f),Vec3f(1,2,.75f),Vec3f(-1,2,.75f)};
        for(unsigned m=0;m<handles.size();++m){const auto source=resources.GeometrySource(handles[m]);Check(bool(source),"valid immutable CPU mesh view");if(!source)continue;
            const auto* bytes=source->source->Vertices().data();
            for(const auto scale:scales)for(const auto type:{ShapeType::Sphere,ShapeType::Box,ShapeType::Capsule}){
                const auto desc=Rbs::FitMeshCollider(*source->source,scale,type);Check(bool(desc),"actual mesh fit produces supported primitive");if(!desc)continue;
                const auto shape=CreateCollisionShape(*desc);Check(bool(shape),"fitted dimensions accepted by unchanged Physics factory");if(!shape)continue;
                CheckFittedGeometry(**shape,*source->source,scale);const auto b=(*shape)->GetBounds();
                fits<<m<<','<<int(type);Vector(fits,scale);Vector(fits,b.mins);Vector(fits,b.maxs);
                fits<<','<<(type==ShapeType::Sphere?static_cast<const ShapeSphere&>(**shape).GetRadius():type==ShapeType::Capsule?static_cast<const ShapeCapsule&>(**shape).GetRadius():0)<<','<<(type==ShapeType::Capsule?static_cast<const ShapeCapsule&>(**shape).GetHalfLength():0)<<'\n';
                if(m==0 && scale==Vec3f(1)){Check(Phase24Close(b.mins.y,-1) && Phase24Close(b.maxs.y,1),"SphereLattice replacement fits actual mesh height without default half-size Box");}
            }
            Check(source->source->Vertices().data()==bytes,"fitting does not replace or duplicate source vertex storage");
            Check(!Rbs::FitMeshCollider(*source->source,Vec3f(std::numeric_limits<float>::quiet_NaN()),ShapeType::Sphere),"nonfinite fitting scale rejected");
            Check(!Rbs::FitMeshCollider(*source->source,Vec3f(0),ShapeType::Box),"degenerate fitted Box rejected");
            Check(!Rbs::FitMeshCollider(*source->source,Vec3f(1),ShapeType::Convex),"unsupported fitted shape rejected");
        }
        for(unsigned scenario=0;scenario<3;++scenario){caseIndex=21+scenario;tick=0;lastReplacement=0;
            _Entity entity;auto scene=Fixture(0,{0,scenario==2?1.4f:1.5f,0},{1,0,0,0},Vec3f(1),entity);
            Check(bool(scene),"mesh-fitting runtime trajectory fixture");if(!scene)continue;
            active=scene.get();target=entity;Check(bool(resources.AssignRenderable(entity,{sphereMesh,material})),"actual Sphere mesh for fitted repeated transitions");
            auto* b=PhysicsTestAccess::Body(entity);auto* world=scene->GetPhysicsSystem()->GetPhysicsWorld();const auto handle=*scene->GetBodyBinding(entity);
            if(scenario==1)Check(bool(world->SetBodyVelocity(handle,{.4f,0,.2f},{.2f,.4f,.3f})),"fitted moving fixture uses normal velocities");
            scene->SetPaused(true);const auto steps=scene->GetPhysicsTiming().totalSteps;
            for(unsigned i=0;i<8;++i)for(const auto type:{ShapeType::Sphere,ShapeType::Box,ShapeType::Capsule})FitReplace(resources,type);
            Check(scene->GetPhysicsTiming().totalSteps==steps,"paused fitted replacement never advances Physics");
            const auto before=*world->ReadBody(handle);const auto oldShape=scene->GetPhysicsSystem()->InspectColliderCache(handle)->shape;
            Check(!Rbs::ReplaceMeshCollider(*scene,resources,entity,ShapeType::Convex) && Phase24Same(before,*world->ReadBody(handle)) && scene->GetPhysicsSystem()->InspectColliderCache(handle)->shape==oldShape,"fitting rejection preserves complete body and collider state");
            Check(bool(scene->ReplaceBodyCollider(entity,BoxShapeDesc{Vec3f(.375f)})) && Phase24Close(b->m_Shape->GetBounds().maxs.y,.375f),"explicit authored dimensions remain unchanged by RBS fitting policy");
            FitReplace(resources,ShapeType::Sphere);scene->SetPaused(false);
            observer={};observer.boundary=Boundary;PhysicsDiagnostic::observer=&observer;
            float terminalMin=std::numeric_limits<float>::max(),terminalMax=-terminalMin;
            for(tick=0;tick<900;++tick){
                if(tick>=120 && tick<480 && tick%30==0){const std::array types{ShapeType::Box,ShapeType::Capsule,ShapeType::Sphere};FitReplace(resources,types[(tick/30)%3]);}
                if(tick==480)FitReplace(resources,ShapeType::Sphere);
                scene->Update(Timestep(1.0/60.0));Snapshot("fitted-fixed-step-end");
                Check(b->m_Shape->Support({0,1,0},b->m_Position,b->m_Orientation,0).y>=-.5f,"fitted collider has not crossed floor");
                if(tick>=840 && scenario!=1){const auto bottom=b->m_Shape->Support({0,-1,0},b->m_Position,b->m_Orientation,0).y;
                    Check(.5f-bottom<=.035f && glm::length(b->m_LinearVelocity)<=.01f,"fitted terminal Sphere preserves existing penetration and speed gates");
                    terminalMin=(std::min)(terminalMin,b->m_Position.y);terminalMax=(std::max)(terminalMax,b->m_Position.y);
                    Check(terminalMax-terminalMin<=.005f,"fitted terminal Sphere preserves existing height stability gate");}
            }
            PhysicsDiagnostic::observer=nullptr;
            Check(bool(scene->DestroyEntity(entity)) && !Rbs::ReplaceMeshCollider(*scene,resources,entity,ShapeType::Box),"deleted fitted body safely rejects shared UI operation");
            scene->OnRuntimeStop();active=nullptr;target={};
        }
    }

#ifdef GENGINE_PHASE24_REPLACEMENT_MEASUREMENT
    namespace Cost
    {
        struct Span {std::uint64_t ns{},count{},bytes{};};
        struct Row {unsigned mode{},index{},shape{};std::array<Span,4> spans;std::array<Span,12> stages;
            PhysicsDiagnostic::BodyState state;float bottom{},top{};std::size_t pairs{},contacts{};bool valid{};};
        inline Row* current{};inline std::uint64_t stamp{},count{},bytes{};
        template<class F> Span Measure(F&& fn)
        {
            auto& c=Phase24ReplacementAllocation::count;auto& b=Phase24ReplacementAllocation::bytes;
            const auto c0=c.load(std::memory_order_relaxed),b0=b.load(std::memory_order_relaxed);
            Phase24ReplacementAllocation::enabled=true;const auto start=PhysicsDiagnostic::Nanoseconds();fn();
            const auto ns=PhysicsDiagnostic::Nanoseconds()-start;Phase24ReplacementAllocation::enabled=false;
            return {ns,c.load(std::memory_order_relaxed)-c0,b.load(std::memory_order_relaxed)-b0};
        }
        inline void Boundary(void*,PhysicsDiagnostic::Stage stage,const PhysicsWorld&,ManifoldCollector&,
                             std::span<const ContactIsland>,std::size_t,int) noexcept
        {
            const auto now=PhysicsDiagnostic::Nanoseconds(),c=Phase24ReplacementAllocation::count.load(std::memory_order_relaxed),b=Phase24ReplacementAllocation::bytes.load(std::memory_order_relaxed);
            if(current && stage!=PhysicsDiagnostic::Stage::Begin)current->stages[unsigned(stage)]={now-stamp,c-count,b-bytes};
            stamp=now;count=c;bytes=b;
        }
        inline PlatformResult Run(SceneRenderResources& resources,Asset::MeshHandle mesh,Asset::MaterialInstanceHandle material)
        {
            const auto* destination=std::getenv("GENGINE_PHASE24_COLLIDER_OUTPUT");
            if(!destination || !std::filesystem::is_directory(destination))return Phase24Failure("cost evidence destination");
            const auto source=resources.GeometrySource(mesh);if(!source)return Phase24Failure("cost CPU mesh source");
            constexpr unsigned Warmup=768,Samples=3072,Modes=3;
            auto data=std::make_unique<Row[]>(Samples*Modes);
            std::array<Span,1024> emptySpans;
            for(auto& empty:emptySpans)empty=Measure([]{});
            const std::array types{ShapeType::Sphere,ShapeType::Box,ShapeType::Capsule};
            std::array<CollisionShapeDesc,3> fitted;
            for(unsigned i=0;i<3;++i){auto d=Rbs::FitMeshCollider(*source->source,Vec3f(1),types[i]);if(!d)return Phase24Failure("cost fit preparation");fitted[i]=*d;}
            bool valid=true;PhysicsDiagnostic::Observer sink{};sink.boundary=Boundary;
            for(unsigned mode=0;mode<Modes;++mode){
                _Entity entity;auto scene=Fixture(0,{0,1.5f,0},{1,0,0,0},Vec3f(1),entity);
                if(!scene || !resources.AssignRenderable(entity,{mesh,material}))return Phase24Failure("cost fixture");
                auto* body=PhysicsTestAccess::Body(entity);auto* system=scene->GetPhysicsSystem();const auto handle=*scene->GetBodyBinding(entity);
                PhysicsDiagnostic::observer=&sink;
                for(unsigned i=0;i<Warmup+Samples;++i){
                    Row warm{};auto& row=i<Warmup?warm:data[mode*Samples+i-Warmup];row.mode=mode;row.index=i;row.shape=unsigned(types[i%3]);
                    bool fittedOk=false,replaced=false;const auto before=PhysicsDiagnostic::Capture(*body);const auto mass=body->m_InvMass;
                    row.spans[0]=Measure([&]{fittedOk=bool(Rbs::FitMeshCollider(*source->source,Vec3f(1),types[i%3]));});
                    row.spans[1]=Measure([&]{replaced=mode==2?bool(Rbs::ReplaceMeshCollider(*scene,resources,entity,types[i%3])):
                        bool(scene->ReplaceBodyCollider(entity,mode==0?Description(int(i%3)):fitted[i%3]));});
                    const auto cache=system->InspectColliderCache(handle);
                    row.valid=fittedOk && replaced && row.spans[0].count==0 && row.spans[0].bytes==0 &&
                        SameMotion(before,PhysicsDiagnostic::Capture(*body)) && mass==body->m_InvMass &&
                        *scene->GetBodyBinding(entity)==handle && cache && cache->owner==cache->shape && !cache->inertiaValid && !cache->boundsValid;
                    row.spans[2]=Measure([&]{(void)body->GetInverseInertiaTensorBodySpace();(void)body->GetInverseInertiaTensorWorldSpace();(void)body->GetWorldBounds();});
                    current=&row;row.spans[3]=Measure([&]{scene->Update(Timestep(1.0/60.0));});current=nullptr;
                    row.state=PhysicsDiagnostic::Capture(*body);row.bottom=body->m_Shape->Support({0,-1,0},body->m_Position,body->m_Orientation,0).y;
                    row.top=body->m_Shape->Support({0,1,0},body->m_Position,body->m_Orientation,0).y;
                    row.pairs=Phase24ColliderCheckAccess::Pairs(*system).size();row.contacts=Phase24ColliderCheckAccess::Manifolds(*system).GetContactCount();
                    row.valid&=body->HasFiniteState() && row.top>=-.5f;valid&=row.valid;
                }
                PhysicsDiagnostic::observer=nullptr;scene->OnRuntimeStop();
            }
            bool equivalent=true;for(unsigned i=0;i<Samples;++i)equivalent&=SameMotion(data[Samples+i].state,data[2*Samples+i].state);
            const auto path=std::filesystem::path(destination);std::ofstream file(path/"replacement-cost.csv");file<<std::setprecision(17);
            std::ofstream overhead(path/"replacement-observer-overhead.csv");overhead<<"ns,new_count,new_bytes\n";
            for(const auto& s:emptySpans)overhead<<s.ns<<','<<s.count<<','<<s.bytes<<'\n';
            file<<"mode,index,shape,valid";for(const char* name:{"fit","replace","cache","step"})file<<','<<name<<"_ns,"<<name<<"_new,"<<name<<"_bytes";
            for(unsigned i=0;i<12;++i)file<<",stage"<<i<<"_ns,stage"<<i<<"_new,stage"<<i<<"_bytes";
            file<<",px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,bottom,top,pairs,contacts\n";
            for(unsigned i=0;i<Samples*Modes;++i){const auto& r=data[i];file<<r.mode<<','<<r.index<<','<<r.shape<<','<<r.valid;
                for(const auto& s:r.spans)file<<','<<s.ns<<','<<s.count<<','<<s.bytes;
                for(const auto& s:r.stages)file<<','<<s.ns<<','<<s.count<<','<<s.bytes;
                Vector(file,r.state.position);file<<','<<r.state.orientation.w<<','<<r.state.orientation.x<<','<<r.state.orientation.y<<','<<r.state.orientation.z;
                Vector(file,r.state.linear);Vector(file,r.state.angular);file<<','<<r.bottom<<','<<r.top<<','<<r.pairs<<','<<r.contacts<<'\n';}
            std::ofstream report(path/"replacement-cost-result.txt");report<<"samples="<<Samples*Modes<<"\nvalid="<<valid<<"\nmatched_fitted_trajectories="<<equivalent<<"\nrow_storage_bytes="<<sizeof(Row)*Samples*Modes<<"\nshape_sphere_bytes="<<sizeof(ShapeSphere)<<"\nshape_box_bytes="<<sizeof(ShapeBox)<<"\nshape_capsule_bytes="<<sizeof(ShapeCapsule)<<"\nfit_retained_bytes=0\nfit_gpu_bytes=0\nstatus="<<(valid && equivalent?"DESCRIPTIVE_CAPTURE_COMPLETE":"FAIL")<<'\n';
            return valid && equivalent?PlatformResult{}:Phase24Failure("replacement cost capture invariant");
        }
    }
#endif

    inline PlatformResult Run(SceneRenderResources& resources,Asset::MeshHandle mesh,Asset::MaterialInstanceHandle material)
    {
        const auto* destination=std::getenv("GENGINE_PHASE24_COLLIDER_OUTPUT");if(!destination)return Phase24Failure("collider diagnostic output required");output=destination;
        if(!output.is_absolute() || !std::filesystem::is_directory(output))return Phase24Failure("existing collider output path required");
        const std::string_view group=std::getenv("GENGINE_PHASE24_COLLIDER_CASES")?std::getenv("GENGINE_PHASE24_COLLIDER_CASES"):"all";
        if(group!="all" && group!="historical" && group!="mesh")return Phase24Failure("unknown collider case group");
        rows.open(output/"states.csv");contacts.open(output/"contacts.csv");checks.open(output/"checks.csv");rows<<std::setprecision(17);contacts<<std::setprecision(17);
        references.open(output/"references.csv");references<<std::setprecision(17)<<"case,tick,variant,px,py,pz,vx,vy,vz,wx,wy,wz,pairs,contacts\n";
        const auto referenceRow=[](const char* variant,_Scene& scene,_Entity entity) {
            const auto* b=PhysicsTestAccess::Body(entity);auto* s=scene.GetPhysicsSystem();references<<caseIndex<<','<<tick<<','<<variant;Vector(references,b->m_Position);Vector(references,b->m_LinearVelocity);Vector(references,b->m_AngularVelocity);references<<','<<Phase24ColliderCheckAccess::Pairs(*s).size()<<','<<Phase24ColliderCheckAccess::Manifolds(*s).GetContactCount()<<'\n';
        };
        rows<<"case,tick,boundary,stage,index,generation,world,shape,shape_pointer,owner_pointer,shape_revision,px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,inverse_mass,sx,sy,sz,local_min_x,local_min_y,local_min_z,local_max_x,local_max_y,local_max_z,bounds_min_x,bounds_min_y,bounds_min_z,bounds_max_x,bounds_max_y,bounds_max_z,support_down_x,support_down_y,support_down_z,support_up_x,support_up_y,support_up_z,analytic_bottom,down_height,com_x,com_y,com_z";
        for(const auto name:{"shape_inertia","expected_inverse_body","expected_inverse_world","cached_inverse_body"}) {
            if(std::string_view(name)=="cached_inverse_body")rows<<",cache_inertia_valid,cache_inertia_shape,cache_inertia_revision";
            for(unsigned i=0;i<9;++i)rows<<','<<name<<i;
        }
        rows<<",cache_bounds_valid,cache_bounds_shape,cache_bounds_revision,cached_min_x,cached_min_y,cached_min_z,cached_max_x,cached_max_y,cached_max_z,sleeping,inactive,pairs,broad_full_sort,broad_axis_overlap,broad_aabb_rejected,manifold_contacts,toi_contacts,alpha,previous_x,previous_y,previous_z,current_x,current_y,current_z,presentation_x,presentation_y,presentation_z,presentation_qw,presentation_qx,presentation_qy,presentation_qz\n";
        contacts<<"case,tick,boundary,stage,manifold,slot,body_a,body_b,nx,ny,nz,ax,ay,az,bx,by,bz,lax,lay,laz,lbx,lby,lbz,separation,toi,feature_a,feature_b\n";checks<<"case,tick,pass,check\n";
        // Multiple paused replacements intentionally have no live lazy-cache
        // getter between them; raw snapshots above cannot mask pointer reuse.
        if(group!="mesh") {
        {
            _Entity body;auto scene=Fixture(0,{0,3,0},{1,0,0,0},Vec3f(1),body);if(!scene)return Phase24Failure("paused cache fixture");active=scene.get();target=body;caseIndex=0;tick=0;
            auto* b=PhysicsTestAccess::Body(body);auto* world=scene->GetPhysicsSystem()->GetPhysicsWorld();const auto h=*scene->GetBodyBinding(body);
            (void)world->ReadBody(h);scene->SetPaused(true);
            for(unsigned i=0;i<32;++i){tick=i;Replace(BoxShapeDesc{Vec3f(.5f)});Replace(SphereShapeDesc{1.f+float(i%3)});
                auto fresh=Fresh(*b);Snapshot("before-cache-consumption");
                const bool inertiaOk=Difference(b->GetInverseInertiaTensorBodySpace(),fresh.GetInverseInertiaTensorBodySpace())<=1e-4;
                const bool boundsOk=b->GetWorldBounds().mins==fresh.GetWorldBounds().mins && b->GetWorldBounds().maxs==fresh.GetWorldBounds().maxs;
                Check(inertiaOk,"paused repeated replacement fresh inertia");Check(boundsOk,"paused repeated replacement fresh bounds");
                Snapshot("after-cache-consumption");
                if(!inertiaOk || !boundsOk) {
                    _Entity refEntity;auto reference=Fixture(0,b->m_Position,b->m_Orientation,Vec3f(1),refEntity);
                    Check(reference && bool(reference->ReplaceBodyCollider(refEntity,SphereShapeDesc{1.f+float(i%3)})),"ABA fresh equivalent shape reference");
                    if(reference) {
                        auto* refWorld=reference->GetPhysicsSystem()->GetPhysicsWorld();Check(bool(refWorld->SetBodyVelocity(*reference->GetBodyBinding(refEntity),b->m_LinearVelocity,b->m_AngularVelocity)),"ABA reference velocity");
                        referenceRow("before-replaced",*scene,body);referenceRow("before-fresh",*reference,refEntity);
                        reference->Update(Timestep(1.0/60.0));observer={};observer.boundary=Boundary;lastReplacement=tick;PhysicsDiagnostic::observer=&observer;
                        scene->SetPaused(false);scene->Update(Timestep(1.0/60.0));scene->SetPaused(true);PhysicsDiagnostic::observer=nullptr;
                        referenceRow("after-replaced",*scene,body);referenceRow("after-fresh",*reference,refEntity);Snapshot("ABA-first-physics-tick");
                    }
                }
                const auto before=PhysicsDiagnostic::Capture(*b);const auto count=scene->GetPhysicsTiming().totalSteps;scene->Update(Timestep(1.0/60.0));Check(SameMotion(before,PhysicsDiagnostic::Capture(*b)) && scene->GetPhysicsTiming().totalSteps==count,"paused replacement does not advance Physics");
            }
            scene->SetPaused(false);scene->Update(Timestep(1.0/60.0));Snapshot("paused-resume-one-tick");
            const auto before=*world->ReadBody(h);const auto cache=*scene->GetPhysicsSystem()->InspectColliderCache(h);const auto islands=scene->GetPhysicsSystem()->GetContactIslands().size();
            Check(!scene->ReplaceBodyCollider(body,SphereShapeDesc{-1}) && Phase24Same(before,*world->ReadBody(h)) && scene->GetPhysicsSystem()->InspectColliderCache(h)->shape==cache.shape && scene->GetPhysicsSystem()->GetContactIslands().size()==islands,"invalid replacement transactional");
            Check(bool(scene->DestroyEntity(body)) && !scene->GetPhysicsSystem()->ReplaceBodyCollider(h,SphereShapeDesc{1}),"destroyed handle rejects replacement");active=nullptr;target={};scene->OnRuntimeStop();
        }
        {
            _Entity body;auto scene=Fixture(0,{0,1.49f,0},{1,0,0,0},Vec3f(1),body);
            if(!scene)return Phase24Failure("contact rejection fixture");active=scene.get();target=body;caseIndex=10;tick=0;
            scene->Update(Timestep(1.0/60.0));auto* system=scene->GetPhysicsSystem();auto* world=system->GetPhysicsWorld();
            const auto h=*scene->GetBodyBinding(body);const auto before=*world->ReadBody(h);
            auto& manifolds=Phase24ColliderCheckAccess::Manifolds(*system);const auto count=manifolds.GetContactCount();
            Check(count>0,"rejection has a retained floor contact");
            const auto pairs=Phase24ColliderCheckAccess::Pairs(*system);const auto islands=system->GetContactIslands().size();
            std::vector<contact_t> saved;for(auto& m:manifolds.m_Manifolds)for(int i=0;i<m.GetNumContacts();++i)saved.push_back(m.GetContact(i));
            unsigned signals=0;auto subscription=scene->SubscribeToAuthoritativeTransformChanges([&](const AuthoritativeTransformChange&){++signals;});
            const auto cache=*system->InspectColliderCache(h);const auto history=*scene->ReadBodyTransform(body);Snapshot("rejection-with-contacts-before");
            Check(!scene->ReplaceBodyCollider(body,CapsuleShapeDesc{-1,.5f}),"invalid contacted replacement rejected");
            Check(Phase24Same(before,*world->ReadBody(h)) && system->InspectColliderCache(h)->shape==cache.shape &&
                system->InspectColliderCache(h)->owner==cache.owner && signals==0 &&
                scene->ReadBodyTransform(body)->previous.translation==history.previous.translation &&
                scene->ReadBodyTransform(body)->current.rotation==history.current.rotation,"rejection preserves body owner history and notifications");
            bool same=count==manifolds.GetContactCount() && pairs==Phase24ColliderCheckAccess::Pairs(*system) && islands==system->GetContactIslands().size();
            unsigned ordinal=0;for(auto& m:manifolds.m_Manifolds)for(int i=0;i<m.GetNumContacts();++i){same&=ordinal<saved.size() && SameContact(saved[ordinal],m.GetContact(i));++ordinal;}
            Check(same,"rejection preserves contact anchors normals features and pair graph");Snapshot("rejection-with-contacts-after");
            Replace(CapsuleShapeDesc{1,.5f});scene->Update(Timestep(1.0/60.0));Snapshot("accepted-replacement-rebuilt-contact");
            Check(manifolds.GetContactCount()>0,"accepted overlapping replacement rebuilds floor contacts");
            scene->OnRuntimeStop();Check(!system->ReplaceBodyCollider(h,SphereShapeDesc{1}),"closed world replacement rejected");
            Check(bool(scene->OnRuntimeStart()) && *scene->GetBodyBinding(body)!=h && !system->ReplaceBodyCollider(h,SphereShapeDesc{1}),"restart rejects old handle");
            active=nullptr;target={};
        }
        // Predetermined bounded cases: shape changes may initially create overlap.
        // Geometric floor crossing is recorded separately from legitimate settling.
        for(unsigned scenario=0;scenario<9;++scenario) {
            caseIndex=scenario+1;tick=0;lastReplacement=0;
            const int initial=scenario==1?1:0,replacement=(scenario==2 || scenario==3 || scenario>=7)?2:scenario==1?0:1;
            const Quat rotation=scenario==3?glm::angleAxis(Math::Pi*.5f,Vec3f(0,0,1)):scenario>=7?Quat(Vec3f(.7f,.4f,.2f)):Quat(1,0,0,0);
            const float scale=scenario==6?1.5f:1.f;
            _Entity entity;auto scene=Fixture(initial,{0,.5f+(initial==1?.5f:scale)-(scenario==5?.1f:0.f),0},rotation,Vec3f(scale),entity);if(!scene)return Phase24Failure("replacement trajectory fixture");
            active=scene.get();target=entity;auto* b=PhysicsTestAccess::Body(entity);auto* system=scene->GetPhysicsSystem();auto* world=system->GetPhysicsWorld();const auto handle=*scene->GetBodyBinding(entity);
            Check(bool(resources.AssignRenderable(entity,{mesh,material})),"actual Sphere visual mesh assigned independently");
            if(scenario==4 || scenario>=7)Check(bool(world->SetBodyVelocity(handle,{.4f,0,.2f},{.2f,.4f,.3f})),"bounded moving fixture velocity");
            observer={};observer.boundary=Boundary;PhysicsDiagnostic::observer=&observer;
            float terminalMin=std::numeric_limits<float>::max(),terminalMax=-terminalMin;
            for(tick=0;tick<900;++tick) {
                if(tick==120 || tick==480 || (scenario==5 && tick>=150 && tick<480 && tick%30==0) || (scenario==8 && tick>=126 && tick<480 && tick%6==0)) {
                    const int next=tick==480?initial:scenario==8?int((tick/6)%3):(scenario==5 && tick%60==0?0:replacement);Replace(Description(next));
                    _Entity referenceEntity;auto reference=Fixture(next,b->m_Position,b->m_Orientation,Vec3f(1),referenceEntity);
                    Check(bool(reference),"fresh-shape reference construction");
                    if(reference){auto* ref=PhysicsTestAccess::Body(referenceEntity);auto* refWorld=reference->GetPhysicsSystem()->GetPhysicsWorld();
                        Check(bool(refWorld->SetBodyVelocity(*reference->GetBodyBinding(referenceEntity),b->m_LinearVelocity,b->m_AngularVelocity)),"fresh-shape reference velocity");
                        auto* saved=PhysicsDiagnostic::observer;PhysicsDiagnostic::observer=nullptr;reference->Update(Timestep(1.0/60.0));PhysicsDiagnostic::observer=saved;
                        scene->Update(Timestep(1.0/60.0));Snapshot("replacement-first-tick");referenceRow("replaced-first-tick",*scene,entity);referenceRow("fresh-first-tick",*reference,referenceEntity);
                        Check(glm::length(b->m_Position-ref->m_Position)<=1e-4f && glm::length(b->m_LinearVelocity-ref->m_LinearVelocity)<=1e-4f && glm::length(b->m_AngularVelocity-ref->m_AngularVelocity)<=1e-4f,"first tick matches fresh-shape reference");
                    }
                }else scene->Update(Timestep(1.0/60.0));
                Snapshot("fixed-step-end");auto fresh=Fresh(*b);
                Check(Difference(b->GetInverseInertiaTensorBodySpace(),fresh.GetInverseInertiaTensorBodySpace())<=1e-4,"live inertia matches current shape");
                const float top=b->m_Shape->Support({0,1,0},b->m_Position,b->m_Orientation,0).y;
                Check(top>=-.5f,"collider has not crossed completely below floor");
                const auto pose=scene->ReadBodyTransform(entity);Check(pose && Math::IsFinite(pose->presentation.translation) && Math::IsFinite(pose->presentation.rotation),"valid presentation pose");
                if(tick>=840 && initial==0 && scenario!=4 && scenario!=6 && scenario<7) {
                    const float bottom=b->m_Shape->Support({0,-1,0},b->m_Position,b->m_Orientation,0).y;
                    Check(.5f-bottom<=.035f,"existing terminal SphereBox penetration criterion");
                    Check(glm::length(b->m_LinearVelocity)<=.01f,"existing terminal SphereBox speed criterion");
                    terminalMin=(std::min)(terminalMin,b->m_Position.y);terminalMax=(std::max)(terminalMax,b->m_Position.y);
                    Check(terminalMax-terminalMin<=.005f,"existing terminal SphereBox height range criterion");
                }
            }
            PhysicsDiagnostic::observer=nullptr;scene->OnRuntimeStop();Check(!entity.GetComponent<RigidBody3DComponent>().RuntimeBody && !entity.Transform().OnScaleChanged,"clean Scene collider shutdown");active=nullptr;target={};
        }
        }
        if(group!="historical")FittingChecks(resources,mesh,material);
        rows.close();contacts.close();checks.close();references.close();std::ofstream summary(output/"diagnostic-result.txt");summary<<"checks="<<checkCount<<"\nfailures="<<failures<<"\nstatus="<<(failures?"FAIL":"PASS")<<"\n";summary.close();
        Log::GetCoreLogger()->info("PHASE24_COLLIDER_DIAGNOSTIC checks={} failures={}",checkCount,failures);
        // Diagnostic process failure is intentional and retained. No candidate
        // can become valid merely because the application initialized.
        return failures?Phase24Failure("collider replacement diagnostic assertions"):PlatformResult{};
    }

    inline unsigned runtimeFrames{},observedFrames{};
    inline bool runtimeSelected{};
    inline Asset::MeshHandle runtimeMesh;
    inline Asset::MaterialInstanceHandle runtimeMaterial;
    inline const MeshAsset* runtimeSource{};
    inline std::ofstream rendered;
    inline void BeginRuntime(_Scene& scene,_Entity entity,Asset::MeshHandle mesh,Asset::MaterialInstanceHandle material,const MeshAsset& source)
    {
        active=&scene;target=entity;caseIndex=100;tick=0;runtimeMesh=mesh;runtimeMaterial=material;runtimeSource=&source;expectedRuntimeFitCalls=Rbs::MeshColliderFitCalls();
        rows.open(output/"states.csv",std::ios::app);contacts.open(output/"contacts.csv",std::ios::app);checks.open(output/"checks.csv",std::ios::app);
        rendered.open(output/"rendered.csv");rendered<<std::setprecision(17)<<"frame,physics_steps,shape,body_y,presentation_y,render_y,collider_bottom,mesh_vertex_bottom,mesh_vertex_top,cyan,magenta\n";
    }
    inline void ObserveRuntime(const RenderFrame& frame)
    {
        if(!active || !target)return;
        Check(Rbs::MeshColliderFitCalls()==expectedRuntimeFitCalls,"ordinary render/update frames never invoke collider fitting");
        const auto pose=active->ReadBodyTransform(target);const auto id=active->RenderData().Identify(target);
        Check(bool(pose) && bool(id),"runtime render has body and entity identity");if(!pose || !id)return;
        auto* b=PhysicsTestAccess::Body(target);const auto before=PhysicsDiagnostic::Capture(*b);const auto steps=active->GetPhysicsTiming().totalSteps;
        const Mat4 expected=glm::translate(Mat4(1),pose->presentation.translation)*glm::mat4_cast(pose->presentation.rotation)*glm::scale(Mat4(1),target.Transform().Scale);
        unsigned found=0,cyan=0,magenta=0;float renderY=0,meshBottom=0,meshTop=0;
        for(const auto& draw:frame.Draws())if(draw.entity==*id){++found;renderY=draw.worldTransform[3].y;
            double difference=0;for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)difference=(std::max)(difference,double(std::abs(draw.worldTransform[c][r]-expected[c][r])));
            Check(difference<=1e-4 && draw.mesh==runtimeMesh && draw.material==runtimeMaterial,"frozen mesh matches exact presentation pose and original resources");
            const auto bounds=frame.Resources()[draw.resources].Mesh()->Bounds();Check(!bounds.empty,"actual Sphere mesh bounds available");
            meshBottom=std::numeric_limits<float>::max();meshTop=-meshBottom;
            for(std::size_t vertex=0;vertex<runtimeSource->VertexCount();++vertex){const auto p=draw.worldTransform*glm::vec4(MeshPosition(*runtimeSource,vertex),1);meshBottom=(std::min)(meshBottom,p.y);meshTop=(std::max)(meshTop,p.y);}
        }
        for(const auto& line:frame.DebugLines()){cyan+=line.color==glm::vec4(0,1,1,1);magenta+=line.color==glm::vec4(1,.2f,1,1);}
        Check(found==1 && cyan>0 && (!runtimeSelected || magenta>0),"actual selected mesh and distinct collider diagnostics rendered");
        Check(SameMotion(before,PhysicsDiagnostic::Capture(*b)) && steps==active->GetPhysicsTiming().totalSteps,"runtime presentation sampling leaves Physics untouched");
        rendered<<runtimeFrames<<','<<steps<<','<<int(b->m_Shape->GetShapeType())<<','<<b->m_Position.y<<','<<pose->presentation.translation.y<<','<<renderY<<','<<b->m_Shape->Support({0,-1,0},b->m_Position,b->m_Orientation,0).y<<','<<meshBottom<<','<<meshTop<<','<<cyan<<','<<magenta<<'\n';++observedFrames;
    }
    inline void CaptureRuntime(const RenderTarget& color,const char* name)
    {
        const auto& d=color.Description().Storage;std::vector<std::byte> pixels(std::size_t(d.Width)*d.Height*4);
        Check(bool(color.ReadColor(pixels)),"actual runtime viewport readback");stbi_flip_vertically_on_write(1);
        Check(stbi_write_png((output/(std::string(name)+".png")).string().c_str(),d.Width,d.Height,4,pixels.data(),d.Width*4)!=0,"runtime image retained");
    }
    inline void EndRuntime()
    {
        rows.close();contacts.close();checks.close();rendered.close();
        std::ofstream result(output/"runtime-result.txt");result<<"frames="<<runtimeFrames<<"\nobservations="<<observedFrames<<"\nchecks="<<checkCount<<"\nfailures="<<failures<<"\nstatus="<<(failures?"FAIL":"PASS")<<'\n';
        active=nullptr;target={};
    }
}
#endif
