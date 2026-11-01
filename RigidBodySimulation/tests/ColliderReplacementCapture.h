#pragma once
// Isolated, opt-in user-selection capture. Metadata helpers never step Physics.
// AUTO_FIT queues an explicit fitted command for the next ordinary update boundary.
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
// Diagnostic builds explicitly bind their isolated evidence root.
// Ordinary builds do not expose or evaluate this confinement configuration.
#ifndef GENGINE_COLLIDER_VALIDATION_ROOT
#error Define GENGINE_COLLIDER_VALIDATION_ROOT as the absolute isolated worktree path with a trailing slash.
#endif
static_assert(sizeof(GENGINE_COLLIDER_VALIDATION_ROOT)>2 &&
    GENGINE_COLLIDER_VALIDATION_ROOT[sizeof(GENGINE_COLLIDER_VALIDATION_ROOT)-2]=='/');
#include "RestingContactDiagnosis.h"
#include "Physics/ShapeCapsule.h"
#include "Camera/CameraSnapshot.h"
#include "Renderer/SceneRenderResources.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <span>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <cmath>

namespace Rbs
{
    std::expected<void,std::string> ApplyCapturedMeshFit(::GEngine::_Scene&,::GEngine::SceneRenderResources&,::GEngine::_Entity,const ::GEngine::CameraSnapshot*);
}

namespace Rbs::ColliderCapture
{
    using namespace ::GEngine;
    using namespace ::GEngine::Component;
    inline std::filesystem::path directory;
    inline bool attempted{}, completed{}, autoConsumed{};
    inline std::uint64_t pendingUuid{};
    struct Selection { bool valid{}; std::uint64_t uuid{}; int readbackX{},readbackY{}; POINT screen{};
        bool screenValid{},cameraValid{}; Vec2f logicalMouse{}; std::uint64_t totalSteps{}; std::int64_t utcNanoseconds{}; CameraSnapshotDesc camera{}; };
    inline Selection selected;
    inline bool Enabled() noexcept
    {
        try {
            if(!directory.empty())return true;
            char* value=nullptr;std::size_t size{};_dupenv_s(&value,&size,"GENGINE_COLLIDER_CAPTURE_OUTPUT");
            if(!value)return false;std::filesystem::path p(value);std::free(value);
            p=p.lexically_normal();const auto path=p.generic_string();
            if(!p.is_absolute()||path.rfind(GENGINE_COLLIDER_VALIDATION_ROOT,0)!=0||!std::filesystem::is_directory(p))return false;
            for(auto at=p;!at.empty();at=at.parent_path()){
                const auto attributes=GetFileAttributesW(at.c_str());
                if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
                if(at==at.root_path())break;
            }
            directory=p;return true;
        }catch(...){return false;}
    }
    inline bool AutoFitEnabled() noexcept
    {
        static const bool enabled=[](){char* value=nullptr;std::size_t n{};_dupenv_s(&value,&n,"GENGINE_COLLIDER_CAPTURE_AUTO_FIT");
            const bool yes=value&&std::string_view(value)=="1";std::free(value);return yes;}();
        return enabled;
    }
    inline bool DynamicBox(const _Entity& entity) noexcept
    {
        if(!entity||!entity.HasAllComponents<MeshRendererComponent,Transform3DComponent>())return false;
        auto* scene=entity.GetSceneContext();auto* system=scene->GetPhysicsSystem();if(!system||!system->GetPhysicsWorld())return false;
        const auto binding=scene->GetBodyBinding(entity);if(!binding)return false;
        const auto* body=PhysicsTestAccess::Resolve(*system->GetPhysicsWorld(),*binding);
        return body&&body->Type==BodyType::Dynamic&&body->m_InvMass>0&&body->m_Shape&&body->m_Shape->GetShapeType()==ShapeType::Box;
    }
    inline _Entity TakePending(_Scene& scene)
    {
        if(!Enabled()||!AutoFitEnabled()||!pendingUuid||autoConsumed)return {};
        const auto uuid=pendingUuid;pendingUuid=0;autoConsumed=true;
        auto entity=scene.GetEntityByUUID(UUID{uuid});
        if(!DynamicBox(entity)){Log::GetCoreLogger()->error("Collider auto-fit selection retired or changed; no command issued");return {};}
        return entity;
    }
    inline bool PersistSelection(const _Entity& entity) noexcept;
    inline void RecordSelection(_Entity entity,const CameraSnapshot* camera,int x,int y,Vec2f logicalMouse={}) noexcept
    {
        if(!Enabled()||(AutoFitEnabled()&&(pendingUuid||autoConsumed||attempted)))return;
        selected={};if(!entity)return;selected.valid=true;selected.uuid=std::uint64_t(entity.GetUUID());
        selected.totalSteps=entity.GetSceneContext()->GetPhysicsTiming().totalSteps;selected.utcNanoseconds=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();selected.logicalMouse=logicalMouse;selected.readbackX=x;selected.readbackY=y;selected.screenValid=GetCursorPos(&selected.screen)!=FALSE;
        if(camera){selected.cameraValid=true;selected.camera=camera->Description();}
        if(AutoFitEnabled()&&DynamicBox(entity)){if(!PersistSelection(entity)){Log::GetCoreLogger()->error("Collider selection persistence failed for UUID {} at step {}; no automatic command queued",selected.uuid,selected.totalSteps);return;}pendingUuid=selected.uuid;Log::GetCoreLogger()->info("Collider capture selected dynamic Box UUID {}; fitted Capsule command queued for next update",selected.uuid);}
    }
    inline void Number(std::ostream& out,double x){if(std::isfinite(x))out<<x;else out<<"null";}
    template<class V> inline void Vector(std::ostream& out,const V& v)
    {out<<'[';Number(out,v.x);out<<',';Number(out,v.y);out<<',';Number(out,v.z);out<<']';}
    template<class Q> inline void Quaternion(std::ostream& out,const Q& q)
    {out<<'[';Number(out,q.w);out<<',';Number(out,q.x);out<<',';Number(out,q.y);out<<',';Number(out,q.z);out<<']';}
    inline void Matrix(std::ostream& out,const Mat3& m)
    {out<<'[';for(unsigned col=0;col<3;++col){if(col)out<<',';Vector(out,m[col]);}out<<']';}
    inline void String(std::ostream& out,const std::string& text)
    {out<<'"';for(const unsigned char c:text){if(c=='"'||c=='\\')out<<'\\'<<c;else if(c>=32&&c<127)out<<c;else out<<'?';}out<<'"';}
    template<class H> inline void Handle(std::ostream& out,const H& h)
    {out<<"{\"index\":"<<h.index<<",\"generation\":"<<h.generation<<",\"registry\":"<<h.registry<<'}';}
    inline void Camera(std::ostream& out,const CameraSnapshotDesc& d)
    {
        out<<"{\"position\":";Vector(out,d.position);out<<",\"orientation_wxyz\":";Quaternion(out,d.orientation);
        out<<",\"vertical_fov_radians\":"<<d.verticalFovRadians<<",\"near\":"<<d.nearPlane<<",\"far\":"<<d.farPlane
           <<",\"projection\":"<<int(d.projection)<<",\"logical_origin\":["<<d.viewport.logicalOrigin.x<<','<<d.viewport.logicalOrigin.y
           <<"],\"logical_size\":["<<d.viewport.logicalSize.x<<','<<d.viewport.logicalSize.y<<"],\"pixel_origin\":["<<d.viewport.pixelOrigin.x<<','<<d.viewport.pixelOrigin.y
           <<"],\"pixel_size\":["<<d.viewport.pixelSize.x<<','<<d.viewport.pixelSize.y<<"]}";
    }
    inline bool PersistSelection(const _Entity& entity) noexcept
    {
        try{
            auto* scene=entity.GetSceneContext();auto* world=scene->GetPhysicsSystem()->GetPhysicsWorld();
            const auto binding=scene->GetBodyBinding(entity);if(!binding)return false;
            const auto* body=PhysicsTestAccess::Resolve(*world,*binding);if(!body)return false;
            const auto& bodies=PhysicsTestAccess::Bodies(*world);std::size_t ordinal=0;
            for(;ordinal<bodies.size()&&bodies[ordinal]!=body;++ordinal){}if(ordinal==bodies.size())return false;
            std::ostringstream out;out<<std::setprecision(17);
            out<<"{\"schema\":1,\"trigger_mode\":\"actual_pick_auto_fit_next_update\",\"selected_uuid\":"<<selected.uuid<<",\"body_ordinal\":"<<ordinal<<",\"body_handle\":";Handle(out,*binding);
            out<<",\"pick_total_steps\":"<<selected.totalSteps<<",\"pick_observed_utc_ns\":"<<selected.utcNanoseconds<<",\"pick_readback_pixel\":["<<selected.readbackX<<','<<selected.readbackY<<"],\"logical_mouse_at_readback\":["<<selected.logicalMouse.x<<','<<selected.logicalMouse.y<<"],\"cursor_screen_at_readback\":["<<selected.screen.x<<','<<selected.screen.y<<"],\"cursor_screen_valid\":"<<selected.screenValid<<",\"pick_camera\":";
            if(selected.cameraValid)Camera(out,selected.camera);else out<<"null";
            out<<",\"position\":";Vector(out,body->m_Position);out<<",\"orientation_wxyz\":";Quaternion(out,body->m_Orientation);out<<",\"persisted_before_command\":true,\"historical_owner_uuid_recovered\":false}\n";
            const auto bytes=out.str();if(bytes.size()>16384)return false;
            const auto file=CreateFileW((directory/"selection.json").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE)return false;
            DWORD written{};const bool saved=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
            const bool flushed=saved&&FlushFileBuffers(file);CloseHandle(file);return flushed;
        }catch(...){return false;}
    }
    inline void Geometry(std::ostream& out,const PhysicalShape& shape)
    {
        const auto bounds=shape.GetBounds();out<<"{\"type\":"<<int(shape.GetShapeType())<<",\"revision\":"<<shape.GetRevision()<<",\"local_min\":";
        Vector(out,bounds.mins);out<<",\"local_max\":";Vector(out,bounds.maxs);out<<",\"center_of_mass\":";Vector(out,shape.GetCenterOfMass());
        out<<",\"unit_mass_inertia_columns\":";Matrix(out,shape.InertiaTensor());
        if(shape.GetShapeType()==ShapeType::Sphere)out<<",\"radius\":"<<static_cast<const ShapeSphere&>(shape).GetRadius();
        if(shape.GetShapeType()==ShapeType::Capsule){const auto& c=static_cast<const ShapeCapsule&>(shape);out<<",\"radius\":"<<c.GetRadius()<<",\"half_length\":"<<c.GetHalfLength();}
        if(shape.GetShapeType()==ShapeType::Convex){out<<",\"points\":[";bool first=true;for(auto v:static_cast<const ShapeConvex&>(shape).GetPoints()){if(!first)out<<',';first=false;Vector(out,v);}out<<']';}
        out<<'}';
    }
    inline std::uint64_t Hash(std::span<const std::byte> bytes)
    {std::uint64_t value=14695981039346656037ull;for(auto b:bytes){value^=std::to_integer<unsigned char>(b);value*=1099511628211ull;}return value;}
    inline void Binary(const char* name,std::span<const std::byte> bytes)
    {if(bytes.size()>64*1024*1024||std::filesystem::exists(directory/name))throw std::runtime_error("capture binary bound/destination");std::ofstream f(directory/name,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!f)throw std::runtime_error("capture binary write");}
    inline void Snapshot(std::ostream& out,_Scene& scene)
    {
        auto* system=scene.GetPhysicsSystem();auto* world=system->GetPhysicsWorld();const auto& bodies=PhysicsTestAccess::Bodies(*world);
        if(bodies.size()>512)throw std::runtime_error("capture body bound");
        out<<"{\"solver_passes\":"<<system->GetSolverIterations()<<",\"sleep_enabled\":"<<system->IsSleepingEnabled()<<",\"step_seconds\":"<<scene.GetPhysicsStep().count()<<",\"gravity\":";Vector(out,world->GetGravity());out<<",\"bodies\":[";
        const auto& registry=scene.ReadRegistry();
        for(unsigned i=0;i<bodies.size();++i){if(i)out<<',';const auto& b=*bodies[i];const auto handle=PhysicsTestAccess::Handle(*world,b);
            out<<"{\"ordinal\":"<<i<<",\"handle\":";Handle(out,handle);out<<",\"identity_slot\":"<<b.GetIdentity().GetSlot()<<",\"identity_generation\":"<<b.GetIdentity().GetGeneration();
            for(auto id:registry.view<const IDComponent,const RigidBody3DComponent>()){
                const auto& rb=registry.get<RigidBody3DComponent>(id);if(rb.RuntimeBody!=handle)continue;
                const _Entity e(id,&scene);out<<",\"entity_uuid\":"<<std::uint64_t(e.GetUUID())<<",\"entity_ecs\":"<<std::uint32_t(id);
                if(registry.all_of<TagComponent>(id)){out<<",\"name\":";String(out,registry.get<TagComponent>(id).Name);}
                if(registry.all_of<Transform3DComponent>(id)){const auto& tr=registry.get<Transform3DComponent>(id);out<<",\"transform_translation\":";Vector(out,tr.Translation);out<<",\"transform_orientation_wxyz\":";Quaternion(out,tr.QuatRotation);out<<",\"transform_scale\":";Vector(out,tr.Scale);}
                if(registry.all_of<MeshRendererComponent>(id)){out<<",\"mesh_handle\":";Handle(out,registry.get<MeshRendererComponent>(id).mesh);}
                if(auto tr=scene.ReadBodyTransform(e);tr){out<<",\"presentation_position\":";Vector(out,tr->presentation.translation);out<<",\"presentation_orientation_wxyz\":";Quaternion(out,tr->presentation.rotation);}
                break;
            }
            out<<",\"position\":";Vector(out,b.m_Position);out<<",\"orientation_wxyz\":";Quaternion(out,b.m_Orientation);out<<",\"linear_velocity\":";Vector(out,b.m_LinearVelocity);out<<",\"angular_velocity\":";Vector(out,b.m_AngularVelocity);
            out<<",\"type\":"<<int(b.Type)<<",\"inverse_mass\":"<<b.m_InvMass<<",\"friction\":"<<b.m_Friction<<",\"restitution\":"<<b.m_Elasticity<<",\"spin_length\":"<<b.GetSpinResistanceLength()<<",\"rolling_length\":"<<b.GetRollingResistanceLength()<<",\"layer\":"<<b.m_CollisionLayer<<",\"mask\":"<<b.m_CollisionMask<<",\"sleeping\":"<<b.IsSleeping()<<",\"inactive_seconds\":"<<b.GetInactiveSeconds();
            const auto& ss=b.GetSleepSettings();out<<",\"sleep_linear_threshold\":"<<ss.linearSpeedThreshold<<",\"sleep_angular_threshold\":"<<ss.angularSpeedThreshold<<",\"sleep_dwell\":"<<ss.inactivitySeconds<<",\"geometry\":";Geometry(out,*b.m_Shape);out<<'}';
        }out<<"]}";
    }
    inline bool Before(_Scene& scene,SceneRenderResources& resources,const _Entity& entity,const CollisionShapeDesc& fitted,const CameraSnapshot* camera) noexcept
    {
        if(!Enabled()||attempted)return false;attempted=true;
        try{
            if(std::filesystem::exists(directory/"replacement-before.json"))throw std::runtime_error("capture destination exists");
            std::ofstream out(directory/"replacement-before.json");out<<std::setprecision(17);
            out<<"{\"schema\":1,\"operation\":\"Fit current mesh ON runtime Capsule replacement\",\"selected_uuid\":"<<std::uint64_t(entity.GetUUID())<<",\"historical_owner_uuid_recovered\":false,\"capture_kind\":\"new actual user selection\",\"pick_matches_selected\":"<<(selected.valid&&selected.uuid==std::uint64_t(entity.GetUUID()));
            out<<",\"trigger_mode\":\""<<(autoConsumed?"actual_pick_auto_fit_next_update":"explicit_Fit_ON_Capsule_UI_button")<<"\",\"command_observed_utc_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()<<",\"command_total_steps\":"<<scene.GetPhysicsTiming().totalSteps;
            out<<",\"pick_readback_pixel\":["<<selected.readbackX<<','<<selected.readbackY<<"],\"cursor_screen_at_readback\":["<<selected.screen.x<<','<<selected.screen.y<<"],\"cursor_screen_valid\":"<<selected.screenValid;
            out<<",\"pick_observed_utc_ns\":"<<selected.utcNanoseconds<<",\"pick_total_steps\":"<<selected.totalSteps<<",\"logical_mouse_at_readback\":["<<selected.logicalMouse.x<<','<<selected.logicalMouse.y<<"],\"coordinate_note\":\"Logical input and OS cursor observed in actual OnMouseClicked readback callback; not the later Replace button cursor and not an archived historical click.\"";
            out<<",\"pick_camera\":";if(selected.cameraValid)Camera(out,selected.camera);else out<<"null";out<<",\"command_camera\":";if(camera)Camera(out,camera->Description());else out<<"null";
            out<<",\"cache_observer_limit\":\"No live body lazy inertia/bounds getters. Shape unit-mass inertia is recomputed, not an observed cached tensor. Scene presentation read is separate from raw body pose.\"";
            if(const auto* cap=std::get_if<CapsuleShapeDesc>(&fitted))out<<",\"fitted_capsule\":{\"radius\":"<<cap->radius<<",\"half_length\":"<<cap->halfLength<<'}';
            const auto& component=entity.GetComponent<MeshRendererComponent>();out<<",\"mesh_handle\":";Handle(out,component.mesh);
            auto source=resources.GeometrySource(component.mesh);if(!source||!source->source)throw std::runtime_error("capture source unavailable");const auto& mesh=*source->source;
            Binary("selected-mesh-vertices.bin",mesh.Vertices());Binary("selected-mesh-indices.bin",mesh.Indices());
            out<<",\"mesh_vertex_count\":"<<mesh.VertexCount()<<",\"mesh_index_count\":"<<mesh.IndexCount()<<",\"vertex_stride\":"<<mesh.Layout().strideBytes<<",\"mesh_vertices_fnv1a64\":"<<Hash(mesh.Vertices())<<",\"mesh_indices_fnv1a64\":"<<Hash(mesh.Indices());
            out<<",\"world\":";Snapshot(out,scene);out<<"}\n";if(!out)throw std::runtime_error("capture write");return true;
        }catch(const std::exception& error){Log::GetCoreLogger()->error("Collider capture before failed: {}",error.what());return false;}
        catch(...){Log::GetCoreLogger()->error("Collider capture before failed");return false;}
    }
    inline void After(_Scene& scene,bool success,std::int64_t commandStartedUtc=0,std::int64_t commandEndedUtc=0) noexcept
    {
        try{if(!Enabled()||completed)return;completed=true;if(std::filesystem::exists(directory/"replacement-after.json"))return;
            std::ofstream out(directory/"replacement-after.json");out<<std::setprecision(17)<<"{\"success\":"<<success<<",\"typed_command_start_utc_ns\":"<<commandStartedUtc<<",\"typed_command_end_utc_ns\":"<<commandEndedUtc<<",\"command_completed_utc_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()<<",\"command_total_steps\":"<<scene.GetPhysicsTiming().totalSteps<<",\"world\":";Snapshot(out,scene);out<<"}\n";
            if(!out)throw std::runtime_error("capture after write");
        }catch(const std::exception& error){Log::GetCoreLogger()->error("Collider capture after failed: {}",error.what());}catch(...){}
    }
}
#endif
