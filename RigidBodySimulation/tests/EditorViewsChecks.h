#pragma once
#include "RigidBodySimulation.h"
#include "RbsEditorState.h"
#include <cstdio>

namespace Rbs
{
    struct EditorViewsChecks
    {
        static int Run()
        {
            using namespace ::GEngine;using namespace ::GEngine::Component;
            ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;Editor::InstallEntityPreferences();
#define EV_CHECK(x) do{if(!(x)){std::fprintf(stderr,"EDITOR_VIEWS_CHECK_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
            {
                LaunchConfig launch;launch.scene=RbsScenePreset::BoxStack;RigidBodySimulationApp app(launch);
                for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
                    app.m_ActiveScene=CreateRefPtr<_Scene>();auto& scene=*app.m_ActiveScene;
                    EV_CHECK(scene.ConfigurePhysicsBackend(backend));
                    auto view=scene.CreateEditorViewIdentity();EV_CHECK(view);app.m_EditorViewIdentity=*view;
                    ++app.m_SceneGeneration;app.m_GroupTransform.Reset(app.m_SceneGeneration);
                    app.m_BuildingPreset=true;app.m_PresetOrdinals={};
                    auto entity=app.CreatePresetEntity(1,"Body"),camera=app.CreatePresetEntity(2,"Camera");EV_CHECK(entity&&camera);
                    entity->AddComponent<RigidBody3DComponent>();entity->AddComponent<BoxFixture3DComponent>();
                    camera->AddComponent<RenderCameraComponent>();
                    app.FreezePresetSignatures();app.m_BuildingPreset=false;
                    EV_CHECK(scene.OnRuntimeStart());scene.SetPaused(true);
                    app.SynchronizeEntityPresentation();
                    const auto id=*scene.RenderData().Identify(*entity),cameraId=*scene.RenderData().Identify(*camera);
                    std::snprintf(app.m_HierarchyFilter.data(),app.m_HierarchyFilter.size(),"unmatched search");
                    app.m_Hierarchy.Build(scene,app.m_HierarchyFilter.data(),id);app.BeginEntityRename(id);
                    EV_CHECK(app.m_HierarchyFilter[0]==0 && app.m_RenameTarget==id && app.m_RenameText==entity->Name());
                    app.m_RenameTarget={};
                    const auto handle=scene.GetBodyBinding(*entity);EV_CHECK(handle);
                    const auto body=scene.GetPhysicsSystem()->GetPhysicsWorld()->ReadBody(*handle);EV_CHECK(body);
                    const auto tagBefore=entity->Name();
                    EV_CHECK(!scene.RenameEntity(*entity,"")&&!scene.RenameEntity(*entity,"   ")&&!scene.RenameEntity(*entity,"bad\nname"));
                    EV_CHECK(entity->Name()==tagBefore && scene.RenderData().Identify(*entity)==id);
                    Editor::TransformBatch rename;rename.kind=Editor::TransformBatch::Kind::Rename;rename.target=id;
                    rename.generation=app.m_SceneGeneration;rename.beforeName=tagBefore;rename.afterName="Studio object";
                    app.m_GroupTransform.preview=rename;app.m_GroupTransform.dirty=true;
                    app.m_GroupTransform.pending=Editor::TransformSession::Action::Apply;app.ProcessGroupTransform();
                    EV_CHECK(entity->Name()=="Studio object" && app.m_GroupTransform.cursor==1);
                    app.QueueEntityVisibility(id,false);app.ProcessGroupTransform();
                    EV_CHECK(!app.EditorEntityVisible(id) && app.m_GroupTransform.cursor==2);
                    EV_CHECK(scene.GetBodyBinding(*entity).value_or(PhysicsBodyHandle{})==*handle);
                    const auto read=scene.GetPhysicsSystem()->GetPhysicsWorld()->ReadBody(*handle);
                    EV_CHECK(read && read->position==body->position && read->inverseMass==body->inverseMass);
                    app.m_GroupTransform.pending=Editor::TransformSession::Action::Undo;app.ProcessGroupTransform();
                    EV_CHECK(app.EditorEntityVisible(id) && entity->Name()=="Studio object");
                    app.m_GroupTransform.pending=Editor::TransformSession::Action::Undo;app.ProcessGroupTransform();EV_CHECK(entity->Name()==tagBefore);
                    app.m_GroupTransform.pending=Editor::TransformSession::Action::Redo;app.ProcessGroupTransform();EV_CHECK(entity->Name()=="Studio object");
                    app.m_GroupTransform.pending=Editor::TransformSession::Action::Redo;app.ProcessGroupTransform();EV_CHECK(!app.EditorEntityVisible(id));
                    auto captured=RigidBodySimulationApp::BackendAuthoring::Capture(scene);EV_CHECK(captured);
                    EV_CHECK(captured->m_Bodies.size()==1 && captured->m_Bodies[0].m_Name=="Body");
                    auto duplicate=scene.DuplicateEntity(*entity);EV_CHECK(duplicate&&duplicate->Name()==entity->Name());
                    EV_CHECK(!duplicate->HasAllComponents<Editor::PresetIdentity>() && scene.RenderData().Identify(*duplicate)!=id);
                    app.SynchronizeEntityPresentation();const auto copyId=*scene.RenderData().Identify(*duplicate);
                    EV_CHECK(app.EditorEntityVisible(copyId));
                    app.OpenCameraPanel(cameraId);EV_CHECK(app.m_CameraPanels.size()==1);
                    const auto stable=app.m_CameraPanels[0].key;
                    EV_CHECK(scene.RenameEntity(*camera,"Camera renamed") && app.m_CameraPanels[0].key==stable && app.m_CameraPanels[0].entity==cameraId);
                    std::size_t length{};const char* ini=ImGui::SaveIniSettingsToMemory(&length);std::string saved(ini,length);
                    Editor::EntityPreferences.clear();ImGui::LoadIniSettingsFromMemory(saved.c_str(),saved.size());
                    EV_CHECK((Editor::EntityPreferences[{app.EditorPresetKey(),stable}].cameraOpen));
                    // Reload rebinds numeric preset roles, never old handles or renamed labels.
                    app.m_CameraPanels.clear();scene.OnRuntimeStop();app.m_ActiveScene.reset();
                    app.m_ActiveScene=CreateRefPtr<_Scene>();auto& replacement=*app.m_ActiveScene;
                    app.m_EditorViewIdentity=*replacement.CreateEditorViewIdentity();app.m_BuildingPreset=true;app.m_PresetOrdinals={};
                    auto restored=app.CreatePresetEntity(1,"Body"),restoredCamera=app.CreatePresetEntity(2,"Camera");
                    restored->AddComponent<RigidBody3DComponent>();restored->AddComponent<BoxFixture3DComponent>();restoredCamera->AddComponent<RenderCameraComponent>();
                    app.FreezePresetSignatures();app.m_BuildingPreset=false;++app.m_SceneGeneration;app.SynchronizeEntityPresentation();
                    const auto restoredId=*replacement.RenderData().Identify(*restored);
                    EV_CHECK(restored->Name()=="Studio object" && !app.EditorEntityVisible(restoredId) && restoredId!=id && app.m_CameraPanels.size()==1);
                    EV_CHECK(!app.ApplyEntityPresentation(rename,false));
                    app.m_GroupTransform.pending=Editor::TransformSession::Action::Undo;app.ProcessGroupTransform();EV_CHECK(app.m_GroupTransform.size==0);
                    // An authored signature mismatch leaves the replacement untouched.
                    auto& preference=Editor::EntityPreferences[{app.EditorPresetKey(),restored->GetComponent<Editor::PresetIdentity>().key}];preference.signature="old incompatible preset";
                    EV_CHECK(replacement.RenameEntity(*restored,"Body"));app.m_PresentationRegistry=0;app.SynchronizeEntityPresentation();
                    EV_CHECK(restored->Name()=="Body" && app.EditorEntityVisible(restoredId));
                    // A partial/malformed saved entry must not bypass the compatibility guard.
                    preference.signature.clear();app.m_PresentationRegistry=0;app.SynchronizeEntityPresentation();
                    EV_CHECK(restored->Name()=="Body" && app.EditorEntityVisible(restoredId));
                    app.m_CameraPanels.clear();app.m_ActiveScene.reset();app.m_EditorViewIdentity={};Editor::EntityPreferences.clear();
                    std::printf("EDITOR_VIEWS_BACKEND_PASS backend=%s rename history visibility identity duplicate preset_reload stale signature_guard\n",PhysicsBackendName(backend).data());
                }
            }
            ImGui::DestroyContext();std::puts("EDITOR_VIEWS_CHECK_PASS");return 0;
#undef EV_CHECK
        }
    };
}
