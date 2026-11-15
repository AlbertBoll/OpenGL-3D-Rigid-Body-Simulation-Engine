#pragma once
#include "RigidBodySimulation.h"
#include <imgui/imgui_internal.h>
#include <cstdio>

namespace Rbs
{
    struct TransformShortcutChecks
    {
        static int Run()
        {
            using namespace ::GEngine;using namespace ::GEngine::Component;using namespace ::GEngine::Manager;
            using Tool=RigidBodySimulationApp::TransformTool;
            ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;
#define TS_CHECK(x) do{if(!(x)){std::fprintf(stderr,"TRANSFORM_SHORTCUT_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
            {
                LaunchConfig launch;launch.scene=RbsScenePreset::BoxStack;RigidBodySimulationApp app(launch);
                for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
                    app.m_ActiveScene=CreateRefPtr<_Scene>();auto& scene=*app.m_ActiveScene;
                    app.m_Backend=backend;TS_CHECK(scene.ConfigurePhysicsBackend(backend));
                    ++app.m_SceneGeneration;app.m_GroupTransform.Reset(app.m_SceneGeneration);
                    app.m_SimulationState=SimulationState::Paused;app.m_ViewportActive=app.m_ViewportForcused=true;
                    auto mesh=scene.CreateEntity("Mesh"),camera=scene.CreateEntity("Camera"),light=scene.CreateEntity("Light"),body=scene.CreateEntity("Body");
                    TS_CHECK(mesh&&camera&&light&&body);camera->AddComponent<RenderCameraComponent>();
                    light->AddComponent<RenderLightComponent>();body->AddComponent<RigidBody3DComponent>();body->AddComponent<BoxFixture3DComponent>();
                    TS_CHECK(scene.OnRuntimeStart());scene.SetPaused(true);
                    auto select=[&](_Entity entity) {app.m_EditorSelection={};app.m_HoveredEntity=entity;
                        return app.m_EditorSelection.Select(scene.RenderData(),*scene.RenderData().Identify(entity),false);};
                    InputRouting route{1,0,0,100,100,true,true,false,false};
                    InputEvent key;key.kind=InputKind::KeyDown;key.window=1;
                    auto press=[&](auto code,std::uint32_t buttons=0,bool captured=false) {
                        key.key=code;return app.HandleTransformToolShortcut(key,route,buttons,captured);};
                    TS_CHECK(select(*mesh));const auto original=mesh->Transform().GetTransform();
                    TS_CHECK(press(GENGINE_KEY_T)&&app.m_TransformTool==Tool::Translate&&app.m_ShowGizmo);
                    TS_CHECK(press(GENGINE_KEY_R)&&app.m_TransformTool==Tool::Rotate&&app.m_SimulationState==SimulationState::Paused);
                    TS_CHECK(press(GENGINE_KEY_S)&&app.m_TransformTool==Tool::Scale);
                    TS_CHECK(mesh->Transform().GetTransform()==original && app.m_GroupTransform.size==0);
                    // Actual selection is mandatory; hover identity alone does not qualify.
                    app.m_EditorSelection={};TS_CHECK(press(GENGINE_KEY_T)&&press(GENGINE_KEY_R)&&app.m_SimulationState==SimulationState::Paused);TS_CHECK(select(*mesh));
                    route.focused=false;TS_CHECK(!press(GENGINE_KEY_T));route.focused=true;
                    route.visible=false;TS_CHECK(!press(GENGINE_KEY_T));route.visible=true;
                    route.uiKeyboard=true;TS_CHECK(!press(GENGINE_KEY_T));route.uiKeyboard=false;
                    route.uiMouse=true;TS_CHECK(!press(GENGINE_KEY_T));route.uiMouse=false;
                    app.m_ViewportForcused=false;TS_CHECK(!press(GENGINE_KEY_T));app.m_ViewportForcused=true;
                    key.window=2;TS_CHECK(!press(GENGINE_KEY_T));key.window=1;
                    ImGui::GetIO().WantTextInput=true;TS_CHECK(!press(GENGINE_KEY_T));ImGui::GetIO().WantTextInput=false;
                    auto& context=*ImGui::GetCurrentContext();context.ActiveId=123;TS_CHECK(!press(GENGINE_KEY_T));context.ActiveId=0;
                    context.DragDropActive=true;TS_CHECK(!press(GENGINE_KEY_T));context.DragDropActive=false;
                    context.OpenPopupStack.push_back(ImGuiPopupData{});TS_CHECK(!press(GENGINE_KEY_T));context.OpenPopupStack.clear();
                    app.m_RenameTarget=*scene.RenderData().Identify(*mesh);TS_CHECK(!press(GENGINE_KEY_T));app.m_RenameTarget={};
                    TS_CHECK(!press(GENGINE_KEY_S,GENGINE_BUTTON(GENGINE_BUTTON_RIGHT))&&!press(GENGINE_KEY_T,0,true));
                    for(auto modifier:{LeftShift,RightShift,LeftAlt,RightAlt,LeftControl,RightControl,LeftSuper,RightSuper}) {
                        key.modifiers=modifier;TS_CHECK(!press(GENGINE_KEY_T)&&!press(GENGINE_KEY_R));
                    }
                    key.modifiers=CapsLock|NumLock;TS_CHECK(press(GENGINE_KEY_T));key.modifiers=0;
                    key.repeated=true;TS_CHECK(!press(GENGINE_KEY_R));key.repeated=false;
                    key.cleanup=true;TS_CHECK(!press(GENGINE_KEY_R));key.cleanup=false;
                    TS_CHECK(select(*camera));TS_CHECK(press(GENGINE_KEY_T)&&app.m_TransformTool==Tool::Translate);
                    TS_CHECK(press(GENGINE_KEY_R)&&app.m_TransformTool==Tool::Rotate);
                    TS_CHECK(press(GENGINE_KEY_S)&&app.m_TransformTool==Tool::Rotate&&!app.m_PropertyDiagnostic.empty());
                    TS_CHECK(select(*light));auto& component=light->GetComponent<RenderLightComponent>();
                    component.kind=RenderLightKind::Directional;
                    TS_CHECK(press(GENGINE_KEY_R)&&app.m_TransformTool==Tool::Rotate);
                    TS_CHECK(press(GENGINE_KEY_T)&&app.m_TransformTool==Tool::Rotate);
                    component.kind=RenderLightKind::Point;
                    TS_CHECK(press(GENGINE_KEY_T)&&app.m_TransformTool==Tool::Translate);
                    TS_CHECK(press(GENGINE_KEY_R)&&app.m_TransformTool==Tool::Translate&&app.m_SimulationState==SimulationState::Paused);
                    component.kind=RenderLightKind::Spot;
                    TS_CHECK(press(GENGINE_KEY_R)&&app.m_TransformTool==Tool::Rotate);
                    TS_CHECK(press(GENGINE_KEY_T)&&app.m_TransformTool==Tool::Translate);
                    TS_CHECK(press(GENGINE_KEY_S)&&app.m_TransformTool==Tool::Translate);
                    TS_CHECK(app.m_EditorSelection.Select(scene.RenderData(),*scene.RenderData().Identify(*camera),true));
                    TS_CHECK(press(GENGINE_KEY_R)&&app.m_TransformTool==Tool::Rotate);
                    TS_CHECK(press(GENGINE_KEY_S)&&app.m_TransformTool==Tool::Rotate);
                    TS_CHECK(select(*body));TS_CHECK(press(GENGINE_KEY_T)&&press(GENGINE_KEY_R));
                    const auto handle=scene.GetBodyBinding(*body);TS_CHECK(handle);
                    TS_CHECK(press(GENGINE_KEY_S)&&app.m_TransformTool==Tool::Scale);
                    const auto retained=scene.GetBodyBinding(*body);
                    TS_CHECK(retained && *retained==*handle && app.m_GroupTransform.size==0 && app.m_GroupTransform.cursor==0);
                    // Rejected requests must retain a complete pending preview, not cancel it.
                    TS_CHECK(select(*camera));app.m_GroupTransform.dirty=true;
                    auto& preview=app.m_GroupTransform.preview;preview.Resize(1);
                    preview.generation=app.m_SceneGeneration;auto& edit=preview.edits[0];
                    edit.entity=*scene.RenderData().Identify(*camera);edit.before=*scene.ReadEditorTransform(edit.entity);
                    edit.after=edit.before;edit.after.translation+=Vec3f(3,2,1);
                    TS_CHECK(scene.PreviewEditorTransforms(preview.Span()));
                    TS_CHECK(press(GENGINE_KEY_S)&&app.m_GroupTransform.dirty&&app.m_GroupTransform.preview.count==1);
                    TS_CHECK(Vec3f(scene.GetRenderTransform(*camera)->matrix[3])==edit.after.translation);
                    const auto saved=edit.before;
                    TS_CHECK(press(GENGINE_KEY_T)&&!app.m_GroupTransform.dirty&&app.m_GroupTransform.size==0);
                    TS_CHECK(Vec3f(scene.GetRenderTransform(*camera)->matrix[3])==saved.translation);
                    TS_CHECK(*scene.ReadEditorTransform(*scene.RenderData().Identify(*camera))==saved);
                    app.SetEditorMode(RigidBodySimulationApp::EditorMode::Simulation);
                    app.m_SimulationState=SimulationState::Ready;app.m_EditorSelection={};
                    TS_CHECK(press(GENGINE_KEY_S)&&app.m_SimulationState==SimulationState::Running);
                    TS_CHECK(press(GENGINE_KEY_R)&&app.m_SimulationState==SimulationState::Running&&app.m_TransformTool==Tool::Translate);
                    TS_CHECK(press(GENGINE_KEY_P)&&app.m_SimulationState==SimulationState::Paused);
                    route.focused=false;TS_CHECK(!press(GENGINE_KEY_R)&&app.m_SimulationState==SimulationState::Paused);route.focused=true;
                    TS_CHECK(press(GENGINE_KEY_R)&&app.m_SimulationState==SimulationState::Running);
                    app.SetEditorMode(RigidBodySimulationApp::EditorMode::Edit);TS_CHECK(app.m_SimulationState==SimulationState::Paused);
                    TS_CHECK(select(*mesh));TS_CHECK(scene.DestroyEntity(*mesh));
                    TS_CHECK(press(GENGINE_KEY_R)&&app.m_TransformTool==Tool::Translate&&app.m_GroupTransform.size==0);
                    app.m_EditorSelection={};app.m_HoveredEntity={};scene.OnRuntimeStop();app.m_ActiveScene.reset();
                }
            }
            ImGui::DestroyContext();std::puts("TRANSFORM_SHORTCUT_PASS Native Jolt selection focus text numeric modal rename foreign-window camera modifiers capabilities stale no-history");
            return 0;
#undef TS_CHECK
        }
    };
}
