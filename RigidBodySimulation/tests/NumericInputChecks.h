#pragma once
#include "RbsNumericInput.h"
#include "RbsComponentEditing.h"
#include "RbsEditorWidgets.h"
#include "Physics/Shape.h"
#include "Physics/PhysicsSystem.h"
#include "Scene/_Scene.h"
#include <cstdio>

namespace Rbs::NumericInputChecks
{
#define N_CHECK(x) do{if(!(x)){std::fprintf(stderr,"NUMERIC_CHECK_FAIL line=%d %s\n",__LINE__,#x);return false;}}while(false)
    inline bool Parsing()
    {
        using namespace Editor::Numeric;
        float value=.5f;
        for(const char* text:{"", "-", "+-1", "++1", "1junk", "1 2", "nan", "inf", "1e999", "1e-999", "0x10", "-0.1", "1.1"})
        {N_CHECK(Parse(text,value,Closed(0.f,1.f)));N_CHECK(value==.5f);}
        N_CHECK(!Parse("0",value,Closed(0.f,1.f)) && value==0);
        N_CHECK(!Parse(" +1 \t",value,Closed(0.f,1.f)) && value==1);
        N_CHECK(Parse("0",value,Positive<float>()) && value==1);
        N_CHECK(!Parse("1e-12",value,Positive<float>()) && value>0);
        N_CHECK(!Parse("-1e30",value,Bounds<float>{}) && value==-1e30f);
        N_CHECK(!Parse("1e30",value,Bounds<float>{}) && value==1e30f);
        int integer=96;N_CHECK(Parse("96.5",integer,Closed(96,320)) && integer==96);
        N_CHECK(!Parse("320",integer,Closed(96,320)) && integer==320);
        std::uint64_t wide=1;N_CHECK(Parse("-1",wide,Positive<std::uint64_t>()));
        N_CHECK(!Parse("18446744073709551615",wide,Positive<std::uint64_t>()) && wide==UINT64_MAX);
        const Bounds<float> open{0.f,1.f,true,true};N_CHECK(open.Lower()>0 && open.Upper()<1);
        N_CHECK(!Validate(open.Lower(),open) && !Validate(open.Upper(),open));
        N_CHECK(Validate(0.f,open) && Validate(1.f,open));
        ::GEngine::Component::RenderCameraComponent camera;
        camera.nearPlane=camera.farPlane;N_CHECK(Editor::ValidateCamera(camera));
        camera={};camera.nearPlane=0;N_CHECK(Editor::ValidateCamera(camera));
        ::GEngine::Component::RenderLightComponent light;light.innerConeRadians=light.outerConeRadians+1;N_CHECK(Editor::ValidateLight(light));
        N_CHECK(!::GEngine::CreateCollisionShape(::GEngine::SphereShapeDesc{std::numeric_limits<float>::denorm_min()}));
        N_CHECK(!::GEngine::CreateCollisionShape(::GEngine::CapsuleShapeDesc{1.f,std::numeric_limits<float>::max()}));
        std::puts("NUMERIC_PARSE_PASS complete_tokens finite open_bounds integer overflow unbounded semantic_camera_light_shape");return true;
    }
    struct Fixture
    {
        ImGuiContext* context{};ImVec2 center{};float scale{};
        explicit Fixture(float size):scale(size)
        {
            context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
            io.DisplaySize={800*scale,600*scale};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();io.Fonts->Build();
            io.FontGlobalScale=scale;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigInputTrickleEventQueue=false;ImGui::GetStyle().ScaleAllSizes(scale);Editor::Numeric::Reset();
        }
        ~Fixture(){Editor::Numeric::Reset();ImGui::DestroyContext(context);}
        template<class T,class Validator=Editor::Numeric::Accept>
        void Frame(T& value,Editor::Numeric::Bounds<T> bounds,bool slider=false,Validator validator={})
        {
            ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({500*scale,350*scale});
            ImGui::Begin("Numeric",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove);
            ImGui::SetNextItemWidth(220*scale);
            Editor::Numeric::Scalar("Field",value,.1f,bounds,validator,slider,nullptr,false);
            auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();center={(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f};
            ImGui::Button("Other");ImGui::End();ImGui::Render();
        }
        void Mouse(ImVec2 position,bool down)
        {auto& io=ImGui::GetIO();io.AddMousePosEvent(position.x,position.y);io.AddMouseButtonEvent(0,down);}
        template<class T> void Enter(T& value,Editor::Numeric::Bounds<T> bounds,const char* text,bool slider=false,bool escape=false)
        {
            using namespace Editor::Numeric;
            auto& io=ImGui::GetIO();for(int i=0;i<25;++i)Frame(value,bounds,slider);
            io.AddKeyEvent(ImGuiKey_ModCtrl,true);Mouse(center,true);Frame(value,bounds,slider);
            Mouse(center,false);io.AddKeyEvent(ImGuiKey_ModCtrl,false);Frame(value,bounds,slider);Frame(value,bounds,slider);
            io.AddInputCharactersUTF8(text);Frame(value,bounds,slider);
            io.AddKeyEvent(escape?ImGuiKey_Escape:ImGuiKey_Enter,true);Frame(value,bounds,slider);
            io.AddKeyEvent(escape?ImGuiKey_Escape:ImGuiKey_Enter,false);Frame(value,bounds,slider);
        }
    };
    inline bool ComponentIdentity(float scale)
    {
        Fixture ui(scale);
        ImGuiID previous=0;
        for(int frame=0;frame<2;++frame)
        {
            ImGui::NewFrame();ImGui::SetNextWindowSize({500*scale,350*scale});
            ImGui::Begin("Properties",nullptr,ImGuiWindowFlags_NoSavedSettings);
            const auto child=ImGui::GetID("Existing field");
            const bool open=frame==0?Editor::Component("Surface material",true):
                Editor::Component("Surface Material",true,"Surface material");
            const auto id=ImGui::GetItemID();
            const bool valid=ImGui::GetID("Existing field")==child &&
                (frame==0?open:(!open && previous==id));
            previous=id;ImGui::GetStateStorage()->SetInt(id,0);
            ImGui::End();ImGui::Render();N_CHECK(valid);
        }
        std::printf("PROPERTIES_HEADER_PASS scale=%.2f renamed_id collapsed_state child_scope\n",scale);
        return true;
    }
    inline bool Interaction(float scale)
    {
        using namespace Editor::Numeric;
        Fixture ui(scale);float value=.5f;const auto bounds=Closed(0.f,1.f);auto& io=ImGui::GetIO();
        ui.Frame(value,bounds);ui.Frame(value,bounds);
        // One-frame overshoot far outside the widget AND display, with Shift acceleration.
        io.AddKeyEvent(ImGuiKey_ModShift,true);ui.Mouse(ui.center,true);ui.Frame(value,bounds);
        ui.Mouse({10000.f,ui.center.y},true);ui.Frame(value,bounds);N_CHECK(value==1);
        ui.Mouse({-10000.f,ui.center.y},true);ui.Frame(value,bounds);N_CHECK(value==0);
        ui.Mouse(ui.center,false);io.AddKeyEvent(ImGuiKey_ModShift,false);ui.Frame(value,bounds);
        ui.Enter(value,bounds,"0.75");N_CHECK(value==.75f && !state.error);
        N_CHECK(TakeSelectionHandoff() && !TakeSelectionHandoff());
        for(const char* text:{"5", "-1", "1junk", "nan", "inf", "++1", "1e999"})
        {ui.Enter(value,bounds,text);N_CHECK(value==.75f && state.error);}
        ui.Enter(value,bounds,"0");N_CHECK(value==0 && !state.error);
        ui.Enter(value,bounds,"1");N_CHECK(value==1 && !state.error);
        ui.Enter(value,bounds,"0.2",false,true);N_CHECK(value==1 && !state.error);
        int size=160;ui.Enter(size,Closed(96,320),"320",true);N_CHECK(size==320);
        ui.Enter(size,Closed(96,320),"999",true);N_CHECK(size==320 && state.error);
        ui.Enter(size,Closed(96,320),"96.5",true);N_CHECK(size==320 && state.error);
        ui.Enter(size,Closed(96,320),"96",true);N_CHECK(size==96 && !state.error);
        float positive=1;ui.Enter(positive,Positive<float>(),"0");N_CHECK(positive==1 && state.error);
        ui.Enter(positive,Positive<float>(),"1e-8");N_CHECK(positive==1e-8f && !state.error);
        // A focus-loss commit must validate once without stealing the newly clicked item.
        for(int i=0;i<25;++i)ui.Frame(value,bounds);
        io.AddKeyEvent(ImGuiKey_ModCtrl,true);ui.Mouse(ui.center,true);ui.Frame(value,bounds);
        io.AddKeyEvent(ImGuiKey_ModCtrl,false);ui.Mouse(ui.center,false);ui.Frame(value,bounds);ui.Frame(value,bounds);
        io.AddInputCharactersUTF8("0.4");ui.Frame(value,bounds);
        ui.Mouse({ui.center.x,ui.center.y+80*scale},true);ui.Frame(value,bounds);
        ui.Mouse({ui.center.x,ui.center.y+80*scale},false);ui.Frame(value,bounds);N_CHECK(value==.4f && !state.editing);
        // A stale external readback retires the old text; it cannot overwrite the new value.
        for(int i=0;i<25;++i)ui.Frame(value,bounds);
        io.AddKeyEvent(ImGuiKey_ModCtrl,true);ui.Mouse(ui.center,true);ui.Frame(value,bounds);
        io.AddKeyEvent(ImGuiKey_ModCtrl,false);ui.Mouse(ui.center,false);ui.Frame(value,bounds);
        value=.6f;ui.Frame(value,bounds);N_CHECK(value==.6f && !state.editing && state.error);
        float position=0;ui.Enter(position,Bounds<float>{},"-1e20");N_CHECK(position==-1e20f);
        // Rejected semantic proposals remain unchanged even though they are numerically in range.
        float mass=1;auto finiteInverse=[](float v)->const char*{return std::isfinite(1.f/v)?nullptr:"Inverse mass overflow";};
        for(int i=0;i<25;++i)ui.Frame(mass,Positive<float>(),false,finiteInverse);
        ui.Mouse(ui.center,true);ui.Frame(mass,Positive<float>(),false,finiteInverse);
        ui.Mouse({-10000.f,ui.center.y},true);ui.Frame(mass,Positive<float>(),false,finiteInverse);N_CHECK(mass==1 && state.error);
        ui.Mouse(ui.center,false);ui.Frame(mass,Positive<float>(),false,finiteInverse);
        std::printf("NUMERIC_IMGUI_PASS scale=%.2f fast_drag shift outside_window strict_text slider_integer finite_unbounded semantic_rejection escape\n",scale);
        return true;
    }
    inline bool BackendBounds(::GEngine::PhysicsBackend backend)
    {
        using namespace ::GEngine;using namespace ::GEngine::Component;
        _Scene scene;N_CHECK(scene.ConfigurePhysicsBackend(backend));auto entity=scene.CreateEntity("numeric-boundary");N_CHECK(entity);
        RigidBody3DComponent body;body.Type=BodyType::Dynamic;entity->AddComponent<RigidBody3DComponent>(body);entity->AddComponent<BoxFixture3DComponent>();
        N_CHECK(scene.OnRuntimeStart());scene.SetPaused(true);auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();
        const auto handle=*scene.GetBodyBinding(*entity);auto p=*world.ReadBodyProperties(handle);
        for(float endpoint:{0.f,1.f})
        {
            p.material.friction=endpoint;p.material.restitution=endpoint;
            if(backend==PhysicsBackend::Jolt){p.linearDamping=endpoint;p.angularDamping=endpoint;p.gravityFactor=endpoint*10;}
            else {p.sleep.linearSpeedThreshold=endpoint;p.sleep.angularSpeedThreshold=endpoint;}
            N_CHECK(world.UpdateBodyProperties(handle,p));N_CHECK(*world.ReadBodyProperties(handle)==p);
        }
        for(int test=0;test<6;++test)
        {
            auto bad=p;
            if(test==0)bad.material.restitution=std::nextafter(1.f,2.f);
            if(test==1)bad.mass=std::numeric_limits<float>::denorm_min();
            if(test==2)bad.material.friction=-.01f;
            if(test==3)bad.mass=std::numeric_limits<float>::quiet_NaN();
            if(test==4)bad.gravityFactor=backend==PhysicsBackend::Jolt?10.01f:2.f;
            if(test==5){if(backend==PhysicsBackend::Jolt)bad.linearDamping=1.01f;else bad.sleep.inactivitySeconds=0;}
            N_CHECK(!world.UpdateBodyProperties(handle,bad));N_CHECK(*world.ReadBodyProperties(handle)==p);
        }
        if(backend==PhysicsBackend::Jolt)
        {
            const auto before=*world.ReadWorldSleepSettings();
            N_CHECK(!world.UpdateWorldSleepSettings({std::numeric_limits<float>::min(),std::numeric_limits<float>::min()}));
            N_CHECK(!world.UpdateWorldSleepSettings({std::numeric_limits<float>::max(),2}));
            N_CHECK(*world.ReadWorldSleepSettings()==before);
        }
        else N_CHECK(!world.ReadWorldSleepSettings());
        scene.OnRuntimeStop();std::printf("NUMERIC_BACKEND_PASS %s endpoints out_of_range nonfinite inverse_mass backend_support unchanged_readback\n",PhysicsBackendName(backend).data());return true;
    }
    inline int Run()
    {
        if(!Parsing())return 1;
        for(float scale:{1.f,1.25f,1.5f,2.f})if(!ComponentIdentity(scale) || !Interaction(scale))return 1;
        for(auto backend:{::GEngine::PhysicsBackend::Native,::GEngine::PhysicsBackend::Jolt})if(!BackendBounds(backend))return 1;
        std::puts("NUMERIC_INPUT_CHECKS_PASS");return 0;
    }
#undef N_CHECK
}
