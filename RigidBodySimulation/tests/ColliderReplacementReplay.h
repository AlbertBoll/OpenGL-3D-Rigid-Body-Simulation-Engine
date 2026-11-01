#pragma once
#include "Scene/_Scene.h"
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
#include "ColliderReplacementObservation.h"
#include "Physics/GJK.h"
#include <bit>
#include <set>
#include <fstream>
#include <map>

namespace Rbs {
    std::expected<::GEngine::CollisionShapeDesc,::GEngine::GeometryAuthoringCode> FitMeshCollider(const ::GEngine::MeshAsset&,const ::GEngine::Vec3f&,::GEngine::ShapeType);
    std::expected<void,std::string> ReplaceMeshCollider(::GEngine::_Scene&,::GEngine::SceneRenderResources&,::GEngine::_Entity,::GEngine::ShapeType);
}
namespace Rbs::ColliderReplay
{
    using namespace ::GEngine;
    namespace PD=PhysicsDiagnostic;
    struct Identity {std::string key;std::uint64_t uuid{},initial{},before{};unsigned ordinal{};bool created{};};
    struct Request {
        bool checked{},enabled{},failed{},initialized{},performed{};const char* failure="";
        unsigned variant{};std::uint64_t selected{},triggerFrame{},triggerSteps{},frames{},frame{},verticesHash{},indicesHash{};
        BoxShapeDesc box{};CapsuleShapeDesc fitted{},manual{};Vec3f freePosition{30,10,30};
        std::array<Identity,22> identity{};std::vector<double> elapsed;std::vector<std::uint8_t> paused;std::filesystem::path initialCache,commandCache;
    };
    inline Request request;
    inline void Fail(const char* reason){if(!request.failed){request.failed=true;request.failure=reason;}}
    inline const char* Failure(){return request.failure;}
    inline bool Load()
    {
        if(request.checked)return request.enabled;request.checked=true;
        char* raw=nullptr;std::size_t size{};_dupenv_s(&raw,&size,"GENGINE_COLLIDER_REPLAY_REQUEST");
        if(!raw)return false;std::filesystem::path path(raw);std::free(raw);request.enabled=true;
        try {
            path=path.lexically_normal();const auto full=path.generic_string();
            if(!path.is_absolute()||full.rfind(GENGINE_COLLIDER_VALIDATION_ROOT,0)!=0){Fail("replay request escapes isolated worktree");return true;}
            for(auto at=path;!at.empty();at=at.parent_path()){const auto attributes=GetFileAttributesW(at.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)){Fail("replay request missing or reparse ancestor");return true;}if(at==at.root_path())break;}
            std::ifstream f(path);std::string magic;f>>magic>>request.variant>>request.selected>>request.triggerFrame>>request.triggerSteps>>request.frames>>request.verticesHash>>request.indicesHash;
            if(magic!="COLLIDER_REPLAY_V1"||request.variant>4||!request.selected||!request.triggerFrame||request.frames>250000||request.frames<request.triggerFrame){Fail("invalid replay header");return true;}
            std::string initialCache,commandCache;f>>std::quoted(initialCache)>>std::quoted(commandCache);request.initialCache=initialCache;request.commandCache=commandCache;
            for(const auto& cache:{request.initialCache,request.commandCache}){if(!cache.is_absolute()||cache.lexically_normal().generic_string().rfind(GENGINE_COLLIDER_VALIDATION_ROOT,0)!=0||!std::filesystem::is_regular_file(cache)){Fail("missing or unconfined reference cache image");return true;}for(auto at=cache;!at.empty();at=at.parent_path()){const auto attributes=GetFileAttributesW(at.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)){Fail("reference cache has reparse ancestor");return true;}if(at==at.root_path())break;}}
            f>>request.box.halfExtents.x>>request.box.halfExtents.y>>request.box.halfExtents.z>>request.box.center.x>>request.box.center.y>>request.box.center.z>>request.fitted.radius>>request.fitted.halfLength>>request.manual.radius>>request.manual.halfLength>>request.freePosition.x>>request.freePosition.y>>request.freePosition.z;
            if(!Math::IsFinite(request.box.halfExtents)||!Math::IsFinite(request.box.center)||request.box.center!=Vec3f(0)||request.box.halfExtents.x<=0||request.box.halfExtents.y<=0||request.box.halfExtents.z<=0||request.freePosition!=Vec3f(30,10,30)||!std::isfinite(request.fitted.radius)||!std::isfinite(request.fitted.halfLength)||request.fitted.radius<=0||request.fitted.halfLength<0||request.manual.radius!=.5f*(std::min)({request.box.halfExtents.x,request.box.halfExtents.y,request.box.halfExtents.z})||request.manual.halfLength!=.5f*request.box.halfExtents.y){Fail("unadmitted replay geometry");return true;}
            std::set<std::string> keys;std::set<std::uint64_t> ids;std::set<unsigned> ordinals;bool selectedBox=false;
            for(auto& i:request.identity){f>>i.key>>i.uuid>>i.ordinal>>i.initial>>i.before;keys.insert(i.key);ids.insert(i.uuid);ordinals.insert(i.ordinal);if(i.uuid==request.selected)selectedBox=i.key.rfind("box_",0)==0;}
            if(keys.size()!=22||ids.size()!=22||ids.contains(0)||ordinals.size()!=22||*ordinals.begin()!=0||*ordinals.rbegin()!=21||!selectedBox){Fail("invalid or ambiguous replay identities");return true;}
            for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)if(!keys.contains("box_"+std::to_string(y)+"_"+std::to_string(x))){Fail("missing authored box key");return true;}
            for(const char* k:{"wood_sphere_0","wood_plane","wall_entity_1","wall_entity_2","wall_entity_3","wall_entity_4"})if(!keys.contains(k)){Fail("missing authored boundary/projectile key");return true;}
            request.elapsed.resize(request.frames);request.paused.resize(request.frames);for(std::size_t i=0;i<request.frames;++i){unsigned pause{};f>>request.elapsed[i]>>pause;if(!std::isfinite(request.elapsed[i])||request.elapsed[i]<0||pause>1){Fail("invalid captured frame duration/pause");return true;}request.paused[i]=std::uint8_t(pause);}
            if(!f){Fail("incomplete replay manifest values");return true;}f>>std::ws;if(!f.eof()){Fail("incomplete replay manifest");return true;}
        }catch(...){Fail("replay manifest read/allocation failure");}
        return true;
    }
    inline bool Enabled(){return Load();}
    inline std::expected<_Entity,SceneError> Create(_Scene& scene,const std::string& tag,const std::string& key)
    {
        if(!Load())return scene.CreateEntity(tag);
        if(request.failed)return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Collider replay",Failure()});
        for(auto& i:request.identity)if(i.key==key){if(i.created){Fail("duplicate replay factory key");break;}i.created=true;return scene.CreateEntityWithUUID(UUID{i.uuid},tag);}
        if(!request.failed)Fail("unrecognized replay factory key");
        return std::unexpected(SceneError{SceneErrorCode::InvalidScene,"Collider replay",Failure()});
    }
    inline std::uint64_t PhysicalImage(const RigidBody3D& b,const _Entity& entity)
    {
        // 52 little-endian uint32 words in this exact order. No pointers/registry domains.
        // FNV1a64 standard offset 14695981039346656037 and prime1099511628211.
        std::uint64_t hash=14695981039346656037ull;
        const auto word=[&](std::uint32_t x){for(unsigned j=0;j<4;++j){hash^=(x>>(8*j))&255u;hash*=1099511628211ull;}};
        const auto f=[&](float x){word(std::bit_cast<std::uint32_t>(x));};
        const auto d=[&](double x){const auto v=std::bit_cast<std::uint64_t>(x);word(std::uint32_t(v));word(std::uint32_t(v>>32));};
        const auto v=[&](const Vec3f& x){f(x.x);f(x.y);f(x.z);};
        word(unsigned(b.Type));f(b.m_InvMass);f(b.m_Friction);f(b.m_Elasticity);f(b.GetSpinResistanceLength());f(b.GetRollingResistanceLength());word(b.m_CollisionLayer);word(b.m_CollisionMask);
        v(b.m_Position);f(b.m_Orientation.w);f(b.m_Orientation.x);f(b.m_Orientation.y);f(b.m_Orientation.z);v(b.m_LinearVelocity);v(b.m_AngularVelocity);word(b.IsSleeping()?1:0);d(b.GetInactiveSeconds());
        const auto& sleep=b.GetSleepSettings();f(sleep.linearSpeedThreshold);f(sleep.angularSpeedThreshold);d(sleep.inactivitySeconds);
        word(unsigned(b.m_Shape->GetShapeType()));const auto bounds=b.m_Shape->GetBounds();v(bounds.mins);v(bounds.maxs);v(b.m_Shape->GetCenterOfMass());const auto inertia=b.m_Shape->InertiaTensor();for(unsigned c=0;c<3;++c)v(inertia[c]);const auto revision=b.m_Shape->GetRevision();word(std::uint32_t(revision));word(std::uint32_t(revision>>32));v(entity.GetComponent<Component::Transform3DComponent>().Scale);return hash;
    }
    inline bool Verify(_Scene& scene,bool initial)
    {
        auto* system=scene.GetPhysicsSystem();if(!system||!system->GetPhysicsWorld()||scene.GetPhysicsStep().count()!=1./60.||system->GetSolverIterations()!=1||!system->IsSleepingEnabled()){Fail("replay default policy mismatch");return false;}
        const auto& list=PhysicsTestAccess::Bodies(system->GetPhysicsWorld());if(list.size()!=22){Fail("replay body membership mismatch");return false;}
        for(const auto& i:request.identity){auto entity=scene.GetEntityByUUID(UUID{i.uuid});if(!i.created||!entity||PhysicsTestAccess::Body(entity)!=list[i.ordinal]||PhysicalImage(*list[i.ordinal],entity)!=(initial?i.initial:i.before)){Fail(initial?"captured initial Physics image mismatch":"captured pre-command Physics image mismatch");return false;}}
        return true;
    }
    inline double FrameElapsed(double live)
    {
        if(!Load())return live;if(request.failed)return 0;
        if(request.frame>=request.elapsed.size()){Fail("captured frame sequence exhausted");return 0;}
        return request.elapsed[request.frame++];
    }
    inline bool FramePaused(bool live)
    {
        if(!Load())return live;if(request.failed)return live;
        if(!request.frame||request.frame>request.paused.size()){Fail("captured pause frame sequence mismatch");return live;}
        return request.paused[request.frame-1]!=0;
    }
    inline bool VerifyFree(_Scene& scene,_Entity target,const CollisionShapeDesc& descriptor,bool committed=false)
    {
        auto shape=CreateCollisionShape(descriptor);if(!shape){Fail("free-location shape staging failed");return false;}
        auto* live=PhysicsTestAccess::Body(target);if(!live){Fail("free-location body missing");return false;}
        RigidBody3D trial=*live;trial.m_Shape=shape->get();trial.m_Position=request.freePosition;
        const auto& list=PhysicsTestAccess::Bodies(scene.GetPhysicsSystem()->GetPhysicsWorld());
        const auto file=ColliderReplacementObservation::session->output/(committed?"replay-free-location-after-queries.csv":"replay-free-location-before-queries.csv");
        if(std::filesystem::exists(file)){Fail("free-location query output exists");return false;}
        if(committed&&live->m_Position!=request.freePosition){Fail("explicit diagnostic relocation did not commit");return false;}
        auto* saved=PD::queryObserver;PD::queryObserver=nullptr;
        bool valid=true;std::ofstream f(file);f<<"ordinal,contact_status,gjk_reason,epa_reason,closest_reason,separation\n"<<std::setprecision(17);
        for(unsigned i=0;i<list.size();++i){if(list[i]==live)continue;RigidBody3D other=*list[i];Vec3f a{},b{};GjkDiagnostics contact{},distance{};const auto status=GJK_GetContact(&trial,&other,.001f,a,b,&contact);GJK_ClosestPoints(&trial,&other,a,b,&distance);const auto separation=glm::length(a-b);
            f<<i<<','<<int(status)<<','<<int(contact.termination)<<','<<int(contact.epa.termination)<<','<<int(distance.termination)<<','<<separation<<'\n';
            valid &= status==GjkContactStatus::Separated&&(distance.termination==GjkTermination::DuplicateSupport||distance.termination==GjkTermination::NoProgress)&&Math::IsFinite(a)&&Math::IsFinite(b)&&Math::IsFinite(separation)&&separation>0;}
        PD::queryObserver=saved;if(!f||!valid){Fail("diagnostic free location not certified separated");return false;}return true;
    }
    inline bool SameImage(const std::filesystem::path& expected,const std::filesystem::path& actual)
    {
        if(!std::filesystem::is_regular_file(expected)||!std::filesystem::is_regular_file(actual)||std::filesystem::file_size(expected)!=std::filesystem::file_size(actual)||std::filesystem::file_size(expected)>4*1024*1024)return false;
        std::ifstream a(expected,std::ios::binary),b(actual,std::ios::binary);std::array<char,4096> x{},y{};
        while(a){a.read(x.data(),x.size());b.read(y.data(),y.size());if(a.gcount()!=b.gcount()||!std::equal(x.begin(),x.begin()+a.gcount(),y.begin()))return false;}
        return a.eof()&&b.eof();
    }
    inline bool BeforeUpdate(_Scene& scene,SceneRenderResources& resources)
    {
        if(!Load())return true;if(request.failed)return false;
        if(!request.initialized){if(!Verify(scene,true))return false;request.initialized=true;}
        if(request.performed)return true;
        if(request.frame<request.triggerFrame)return true;
        if(request.frame!=request.triggerFrame||scene.GetPhysicsTiming().totalSteps!=request.triggerSteps){Fail("captured replacement frame/step boundary mismatch");return false;}
        if(!Verify(scene,false))return false;
        auto target=scene.GetEntityByUUID(UUID{request.selected});auto* body=PhysicsTestAccess::Body(target);
        if(!body||body->m_Shape->GetShapeType()!=ShapeType::Box){Fail("captured selected Box not live");return false;}
        const auto bounds=body->m_Shape->GetBounds();if((bounds.maxs-bounds.mins)*.5f!=request.box.halfExtents||(bounds.maxs+bounds.mins)*.5f!=request.box.center){Fail("captured original Box descriptor mismatch");return false;}
        const auto& mesh=target.GetComponent<Component::MeshRendererComponent>();auto source=resources.GeometrySource(mesh.mesh);if(!source||!source->source){Fail("captured mesh missing");return false;}
        if(ColliderCapture::Hash(source->source->Vertices())!=request.verticesHash||ColliderCapture::Hash(source->source->Indices())!=request.indicesHash){Fail("captured mesh byte identity mismatch");return false;}
        auto fitted=Rbs::FitMeshCollider(*source->source,target.GetComponent<Component::Transform3DComponent>().Scale,ShapeType::Capsule);
        if(!fitted||!std::holds_alternative<CapsuleShapeDesc>(*fitted)||std::get<CapsuleShapeDesc>(*fitted).radius!=request.fitted.radius||std::get<CapsuleShapeDesc>(*fitted).halfLength!=request.fitted.halfLength){Fail("captured fitted descriptor mismatch");return false;}
        if(!ColliderReplacementObservation::session||ColliderReplacementObservation::session->attempted){Fail("replay observer unavailable/already attempted");return false;}
        auto& observer=*ColliderReplacementObservation::session;
        const char* names[]={"no replacement","identical Box transaction","fitted Capsule at declared free location","fitted Capsule at original location","contained manual Capsule diagnostic"};
        if(std::filesystem::exists(observer.output/"replacement-before.json")||std::filesystem::exists(observer.output/"replacement-after.json")){Fail("replay output exists");return false;}
        {std::ofstream f(observer.output/"replacement-before.json");f<<std::setprecision(17)<<"{\"schema\":1,\"operation\":\""<<names[request.variant]<<"\",\"capture_kind\":\"deterministic replay of captured selected UUID\",\"historical_owner_uuid_recovered\":false,\"selected_uuid\":"<<request.selected<<",\"variant\":"<<request.variant<<",\"trigger_frame\":"<<request.triggerFrame<<",\"command_total_steps\":"<<request.triggerSteps<<",\"fitted_capsule\":{\"radius\":"<<request.fitted.radius<<",\"half_length\":"<<request.fitted.halfLength<<"},\"world\":";ColliderCapture::Snapshot(f,scene);f<<"}\n";if(!f){Fail("replay before snapshot failed");return false;}}
        ColliderReplacementObservation::BeginReplacementCapture(scene,target);
        if(observer.failed){Fail("observer pre-command admission failed");return false;}
        if(!SameImage(request.initialCache,observer.output/"observer-cache-initial.bin")||!SameImage(request.commandCache,observer.output/"observer-cache-command.bin")){Fail("captured full initial/command cache image mismatch");return false;}
        bool success=true;
        if(request.variant==1)success=bool(scene.ReplaceBodyCollider(target,request.box));
        if(request.variant==2){if(!VerifyFree(scene,target,*fitted))return false;success=bool(scene.SetLocalPose(target,request.freePosition,body->m_Orientation));}
        if(success&&(request.variant==2||request.variant==3))success=bool(Rbs::ReplaceMeshCollider(scene,resources,target,ShapeType::Capsule));
        if(success&&request.variant==2)success=VerifyFree(scene,target,*fitted,true);
        if(request.variant==4)success=bool(scene.ReplaceBodyCollider(target,request.manual));
        {std::ofstream f(observer.output/"replacement-after.json");f<<std::setprecision(17)<<"{\"success\":"<<success<<",\"world\":";ColliderCapture::Snapshot(f,scene);f<<"}\n";if(!f){Fail("replay after snapshot failed");return false;}}
        ColliderReplacementObservation::EndReplacementCapture(scene,target,success);request.performed=true;if(!success){Fail("replay typed operation rejected");return false;}return !observer.failed;
    }
}
#else
namespace Rbs::ColliderReplay
{
    inline auto Create(::GEngine::_Scene& scene,const std::string& tag,const std::string&){return scene.CreateEntity(tag);}
}
#endif
