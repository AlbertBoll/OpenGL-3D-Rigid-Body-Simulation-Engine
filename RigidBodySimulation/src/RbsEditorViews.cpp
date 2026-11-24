#include "RigidBodySimulation.h"
#ifdef GENGINE_FRAME_SPIKE_AUDIT
#include "Core/FrameSpikeCapture.h"
#endif
#include "RbsEditorState.h"
#include "RbsEditorWidgets.h"
#include "RbsComponentEditing.h"
#include "UI/UICore.h"
#include "UI/FramebufferImage.h"
#include "Scene/_Scene.h"
#include <algorithm>
#include <format>
#include <bit>
#ifdef GENGINE_PHASE28_QUALIFICATION
namespace Rbs { void Phase28AngularWidget(RigidBodySimulationApp&,const char*); }
#endif

using namespace ::GEngine;
using namespace ::GEngine::Component;

unsigned RigidBodySimulationApp::EditorPresetKey() const
{return unsigned(m_ActiveSelection.scene)*32+m_ActiveSelection.chainLinks;}

std::expected<_Entity,SceneError> RigidBodySimulationApp::CreatePresetEntity(unsigned role,std::string_view name)
{
    auto entity=m_ActiveScene->CreateEntity(std::string(name));if(entity)RegisterPresetEntity(*entity,role);return entity;
}
void RigidBodySimulationApp::RegisterPresetEntity(_Entity entity,unsigned role)
{
    if(!m_BuildingPreset || role>=m_PresetOrdinals.size())return;
    entity.AddOrReplaceComponent<Rbs::Editor::PresetIdentity>(Rbs::Editor::PresetIdentity{(std::uint64_t(role)<<32)|++m_PresetOrdinals[role]});
}
void RigidBodySimulationApp::FreezePresetSignatures()
{
    for(auto e:m_ActiveScene->GetAllEntitiesWith<Rbs::Editor::PresetIdentity,TagComponent>()) {
        _Entity entity{e,m_ActiveScene.get()};auto& identity=entity.GetComponent<Rbs::Editor::PresetIdentity>();
        if(!identity.signature.empty())continue;
        identity.signature=entity.Name()+std::format("|v1|{}{}{}{}{}{}",entity.HasAllComponents<RenderCameraComponent>(),entity.HasAllComponents<RenderLightComponent>(),
            entity.HasAllComponents<RigidBody3DComponent>(),entity.HasAllComponents<SphereFixture3DComponent>(),entity.HasAllComponents<BoxFixture3DComponent>(),entity.HasAllComponents<ConvexFixture3DComponent>());
        if(entity.HasAllComponents<Transform3DComponent>()) {
            const auto& t=entity.Transform();
            for(float value:{t.Translation.x,t.Translation.y,t.Translation.z,t.QuatRotation.x,t.QuatRotation.y,t.QuatRotation.z,t.QuatRotation.w,t.Scale.x,t.Scale.y,t.Scale.z})
                identity.signature+=std::format("|{:08x}",std::bit_cast<unsigned>(value));
        }
    }
}

void RigidBodySimulationApp::SynchronizeEntityPresentation()
{
#ifdef GENGINE_FRAME_SPIKE_AUDIT
    FrameSpike::Label(Rbs::SceneName(m_ActiveSelection.scene),int(m_EditorMode),int(m_SimulationState));
#endif
    if(!SandboxEnabled() || !m_ActiveScene)return;
    const auto registry=m_EditorViewIdentity.registry;
    const bool replaced=m_PresentationRegistry!=registry;
    if(replaced) {
        m_CameraPanels.clear();m_EntityPresentation.clear();m_PresentationKnown.clear();m_EditorHidden.clear();m_RenameTarget={};
        m_PresentationRegistry=registry;
        if(m_FrameSubmission)m_FrameSubmission->RetireCameraCaches();
    }
    if(replaced)FreezePresetSignatures();
    for(auto e:m_ActiveScene->GetAllEntitiesWith<TagComponent,IDComponent>()) {
        _Entity entity{e,m_ActiveScene.get()};auto id=m_ActiveScene->RenderData().Identify(entity);if(!id)continue;
        if(m_PresentationKnown.contains(*id))continue;m_PresentationKnown.insert(*id);
        if(!entity.HasAllComponents<Rbs::Editor::PresetIdentity>()) {
            m_EntityPresentation.push_back({*id,0,false,{}});continue; // Created/duplicated entities are session-only.
        }
        const auto& authored=entity.GetComponent<Rbs::Editor::PresetIdentity>();const auto key=authored.key;
        auto& saved=Rbs::Editor::EntityPreferences[{EditorPresetKey(),key}];
        const bool hasOverrides=!saved.name.empty() || saved.hidden || saved.cameraOpen;
        const bool matches=(!hasOverrides && saved.signature.empty()) || saved.signature==authored.signature;
        m_EntityPresentation.push_back({*id,key,matches && saved.hidden,authored.signature});
        if(!matches){m_PropertyDiagnostic="Changed preset entity: saved editor overrides were not applied.";continue;}
        saved.signature=authored.signature;
        if(!saved.name.empty())if(auto renamed=m_ActiveScene->RenameEntity(entity,saved.name);!renamed)
            m_PropertyDiagnostic=std::string(renamed.error().message);
        if(saved.hidden)m_EditorHidden.push_back(*id);
    }
    std::sort(m_EditorHidden.begin(),m_EditorHidden.end());
    if(replaced)for(const auto& value:m_EntityPresentation)
        if(value.key && Rbs::Editor::EntityPreferences[{EditorPresetKey(),value.key}].signature==value.signature &&
            Rbs::Editor::EntityPreferences[{EditorPresetKey(),value.key}].cameraOpen)OpenCameraPanel(value.entity);
}

bool RigidBodySimulationApp::EditorEntityVisible(EntityRenderId id) const
{return !std::binary_search(m_EditorHidden.begin(),m_EditorHidden.end(),id);}

void RigidBodySimulationApp::QueueEntityVisibility(EntityRenderId id,bool visible)
{
    if(!TransformEditingAllowed())return;
    if(m_CameraPilot){m_PropertyDiagnostic="Commit or Cancel Pilot before other authoring.";return;}
    if(!m_ActiveScene->RenderData().Resolve(id))return;
    if(m_GroupTransform.released){m_PropertyDiagnostic="Transform commit pending; retry visibility after this frame.";return;}
    CancelGroupTransform();auto& batch=m_GroupTransform.preview;batch={};
    batch.kind=Rbs::Editor::TransformBatch::Kind::Visibility;batch.target=id;batch.generation=m_SceneGeneration;
    batch.beforeVisible=EditorEntityVisible(id);batch.afterVisible=visible;
    m_GroupTransform.dirty=true;m_GroupTransform.pending=Rbs::Editor::TransformSession::Action::Apply;
}

void RigidBodySimulationApp::BeginEntityRename(EntityRenderId id)
{
    if(!TransformEditingAllowed())return;
    if(m_CameraPilot){m_PropertyDiagnostic="Commit or Cancel Pilot before renaming.";return;}
    auto resolved=m_ActiveScene->RenderData().Resolve(id);if(!resolved)return;
    _Entity entity{*resolved,m_ActiveScene.get()};if(!entity.HasAllComponents<TagComponent>())return;
    // A Viewport-selected entity may be outside the current Hierarchy search.
    // Reveal that real row before handing keyboard focus to its inline editor.
    if(m_HierarchyFilter[0] && !std::any_of(m_Hierarchy.nodes.begin(),m_Hierarchy.nodes.end(),
        [&](const auto& row){return row.identity==id && row.visible;}))m_HierarchyFilter[0]=0;
    m_RenameTarget=id;m_RenameText=entity.Name();m_RenameFocus=true;m_RenameError.clear();m_HierarchyRevealed={};
    if(auto* hierarchy=ImGui::FindWindowByName("Hierarchy"))ImGui::FocusWindow(hierarchy);
}

bool RigidBodySimulationApp::DrawEntityRename(EntityRenderId id,float width)
{
    if(m_RenameTarget!=id)return false;
    ImGui::SetNextItemWidth(width);if(std::exchange(m_RenameFocus,false))ImGui::SetKeyboardFocusHere();
    // Use the linked ImGui core's resize callback; the legacy STL wrapper is not
    // part of the current ImGui runtime build. No fixed name-length truncation.
    const bool commit=ImGui::InputText("##Rename",m_RenameText.data(),m_RenameText.capacity()+1,
        ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_CallbackResize,
        [](ImGuiInputTextCallbackData* data) {
            auto& text=*static_cast<std::string*>(data->UserData);
            text.resize(data->BufTextLen);data->Buf=text.data();return 0;
        },&m_RenameText);
    if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)){
        m_RenameTarget={};if(m_PropertyDiagnostic==m_RenameError)m_PropertyDiagnostic.clear();m_RenameError.clear();return true;
    }
    if(commit) {
        bool valid=false;for(unsigned char c:m_RenameText){if(c<32||c==127){valid=false;break;}valid|=c!=' ';}
        if(!valid){m_RenameError="Enter a nonempty name without control characters.";m_PropertyDiagnostic=m_RenameError;m_RenameFocus=true;}
        else if(auto e=m_ActiveScene->RenderData().Resolve(id)) {
            if(m_GroupTransform.released){m_RenameFocus=true;return true;}
            CancelGroupTransform();auto& batch=m_GroupTransform.preview;batch={};
            batch.kind=Rbs::Editor::TransformBatch::Kind::Rename;batch.target=id;batch.generation=m_SceneGeneration;
            batch.beforeName=_Entity{*e,m_ActiveScene.get()}.Name();batch.afterName=m_RenameText;
            m_GroupTransform.dirty=true;m_GroupTransform.pending=Rbs::Editor::TransformSession::Action::Apply;
            m_RenameTarget={};
        } else {m_RenameTarget={};m_PropertyDiagnostic="Rename rejected: entity retired.";}
    }
    if(!m_RenameError.empty() && ImGui::IsItemHovered())ImGui::SetTooltip("%s",m_RenameError.c_str());
    return true;
}

bool RigidBodySimulationApp::ApplyEntityPresentation(const Rbs::Editor::TransformBatch& batch,bool undo)
{
    auto resolved=m_ActiveScene->RenderData().Resolve(batch.target);
    if(!resolved || batch.generation!=m_SceneGeneration){m_PropertyDiagnostic="Stale editor command rejected.";return false;}
    auto row=std::find_if(m_EntityPresentation.begin(),m_EntityPresentation.end(),[&](const auto& v){return v.entity==batch.target;});
    if(row==m_EntityPresentation.end()){m_PropertyDiagnostic="Entity has no current editor binding.";return false;}
    _Entity entity{*resolved,m_ActiveScene.get()};Rbs::Editor::SavedEntity temporary;
    auto& saved=row->key?Rbs::Editor::EntityPreferences[{EditorPresetKey(),row->key}]:temporary;
    saved.signature=row->signature;
    if(batch.kind==Rbs::Editor::TransformBatch::Kind::Rename) {
        const auto& expected=undo?batch.afterName:batch.beforeName;const auto& next=undo?batch.beforeName:batch.afterName;
        if(entity.Name()!=expected){m_PropertyDiagnostic="Name changed since this command; Undo/Redo rejected.";return false;}
        std::string retained(next); // All allocating preparation precedes authoritative mutation.
        auto result=m_ActiveScene->RenameEntity(entity,next);
        if(!result){m_PropertyDiagnostic=std::string(result.error().message);return false;}
        saved.name.swap(retained);
    } else {
        const bool expected=undo?batch.afterVisible:batch.beforeVisible,next=undo?batch.beforeVisible:batch.afterVisible;
        if(EditorEntityVisible(batch.target)!=expected){m_PropertyDiagnostic="Visibility changed; Undo/Redo rejected.";return false;}
        if(!next){m_EditorHidden.reserve(m_EditorHidden.size()+1);m_EditorHidden.push_back(batch.target);std::sort(m_EditorHidden.begin(),m_EditorHidden.end());}
        else std::erase(m_EditorHidden,batch.target);
        row->hidden=saved.hidden=!next;
    }
    ImGui::MarkIniSettingsDirty();m_PropertyDiagnostic="Editor change applied. Ctrl+Z Undo / Ctrl+Y Redo; runtime state unchanged.";
    return true;
}

void RigidBodySimulationApp::OpenCameraPanel(EntityRenderId id)
{
    auto e=m_ActiveScene->RenderData().Resolve(id);if(!e || !_Entity{*e,m_ActiveScene.get()}.HasAllComponents<RenderCameraComponent>())return;
    auto row=std::find_if(m_EntityPresentation.begin(),m_EntityPresentation.end(),[&](const auto& v){return v.entity==id;});
    if(row==m_EntityPresentation.end())return;
    for(auto& panel:m_CameraPanels)if(panel.entity==id){panel.open=true;return;}
    CameraPanel panel;panel.entity=id;panel.key=row->key;m_CameraPanels.push_back(std::move(panel));
    if(row->key) {auto& saved=Rbs::Editor::EntityPreferences[{EditorPresetKey(),row->key}];saved.cameraOpen=true;saved.signature=row->signature;ImGui::MarkIniSettingsDirty();}
}

bool RigidBodySimulationApp::EditSceneCamera(EntityRenderId id,bool matchEditorPose)
{
    if(!TransformEditingAllowed()){m_CameraDiagnostic="Enter Edit to change a scene camera.";return false;}
    auto resolved=m_ActiveScene->RenderData().Resolve(id);
    if(!resolved || m_SceneRequest || m_BackendRequest || HasPendingObjectProperties()) {
        m_PropertyDiagnostic="Apply or Revert pending changes before editing a Camera.";return false;
    }
    _Entity entity{*resolved,m_ActiveScene.get()};
    if(!entity.HasAllComponents<RenderCameraComponent,Transform3DComponent>())return false;
    if(matchEditorPose && entity.GetParent()) {
        m_PropertyDiagnostic="Match Editor View supports root Cameras. Use local Transform controls for this parented Camera.";return false;
    }
    if(!SetCameraView(CameraView::Editor))return false;
    SetEditorMode(EditorMode::Edit); // Explicit Edit action pauses; Preview never authors a pose.
    SelectColliderEntity(entity,false);
    if(m_HoveredEntity!=entity)return false;
    m_InspectorTabRequest=1;
    if(auto* properties=ImGui::FindWindowByName("Properties"))ImGui::FocusWindow(properties);
    if(!ActivateTransformTool(TransformTool::Translate))return false;
    if(!matchEditorPose) {
        m_PropertyDiagnostic="Camera selected for Transform editing. T/R and the gizmo move this Camera; Editor navigation remains independent.";
        return true;
    }
    // Reuse the existing atomic Scene Transform transaction and Undo/Redo history.
    // The immutable projection component and active-camera identity are untouched.
    if(!BeginGroupTransform() || m_GroupTransform.preview.count!=1)return false;
    auto& pose=m_GroupTransform.preview.edits[0].after;
    pose.translation=m_EditorCamera_.GetPosition();pose.rotation=m_EditorCamera_.GetOrientation();
    m_GroupTransform.dirty=true;FinishTransformGesture();
    m_PropertyDiagnostic="Camera pose queued from Editor View. Apply occurs at the next safe Scene boundary; Ctrl+Z restores the previous pose.";
    return true;
}

namespace
{
    std::optional<Mat4> CameraParentWorld(_Scene& scene,_Entity entity)
    {
        Mat4 world(1);
        for(auto parent=entity.GetParent();parent;parent=parent.GetParent()) {
            auto pose=scene.GetRenderTransform(parent);if(!pose)return {};
            world=pose->matrix*world;
            if(parent.HasAllComponents<RigidBody3DComponent>())break;
        }
        return world;
    }
}

bool RigidBodySimulationApp::BeginCameraPilot(EntityRenderId id)
{
    if(!TransformEditingAllowed()){m_CameraDiagnostic="Enter Edit to pilot a scene camera.";return false;}
    if(m_CameraPilot || !m_ActiveScene || m_SceneRequest || m_BackendRequest ||
       HasPendingObjectProperties() || Rbs::Editor::Numeric::HasError() || m_Angular.dirty || m_SceneDefaultsPending || m_GroupTransform.dirty) {
        m_CameraDiagnostic="Commit/Cancel Pilot or Apply/Revert pending edits first.";return false;
    }
    auto resolved=m_ActiveScene->RenderData().Resolve(id);if(!resolved)return false;
    _Entity entity{*resolved,m_ActiveScene.get()};
    if(!entity.HasAllComponents<RenderCameraComponent,Transform3DComponent>() || entity.HasAllComponents<RigidBody3DComponent>()) {
        m_CameraDiagnostic="Pilot requires a Scene Camera with a non-physics Transform.";return false;
    }
    auto before=m_ActiveScene->ReadEditorTransform(id);
    auto parent=CameraParentWorld(*m_ActiveScene,entity);
    if(!before || !parent)return false;
    const Vec3f lengths{glm::length(Vec3f((*parent)[0])),glm::length(Vec3f((*parent)[1])),glm::length(Vec3f((*parent)[2]))};
    if(!Math::IsFinite(lengths) || lengths.x<=0 || lengths.y<=0 || lengths.z<=0) {
        m_CameraDiagnostic="Pilot requires a nonsingular parent Transform.";return false;
    }
    const glm::mat3 basis{Vec3f((*parent)[0])/lengths.x,Vec3f((*parent)[1])/lengths.y,Vec3f((*parent)[2])/lengths.z};
    if(glm::determinant(basis)<0 || std::abs(lengths.x-lengths.y)>1e-5f*lengths.x ||
       std::abs(lengths.x-lengths.z)>1e-5f*lengths.x ||
       std::abs(glm::dot(basis[0],basis[1]))>1e-5f || std::abs(glm::dot(basis[0],basis[2]))>1e-5f ||
       std::abs(glm::dot(basis[1],basis[2]))>1e-5f) {
        m_CameraDiagnostic="Pilot supports rigid or uniformly scaled parent frames. Use local Transform editing for mirrored, sheared or nonuniform parents.";return false;
    }
    auto access=m_SceneResources->Publication().BeginFrame();
    auto snapshot=m_ActiveScene->SnapshotSceneCamera(id,m_SceneResources->ForFrame(access),{});
    if(!snapshot){m_CameraDiagnostic="Pilot requires a valid Scene Camera pose and projection.";return false;}
    const auto desc=snapshot->Description();
    CameraPilot pilot;
    try {
        pilot.command=std::make_unique<Rbs::Editor::TransformBatch>();pilot.command->Resize(1);
        if(!m_GroupTransform.history)m_GroupTransform.history=std::make_unique<std::array<std::unique_ptr<Rbs::Editor::TransformBatch>,Rbs::Editor::TransformSession::HistoryCapacity>>();
    } catch(const std::bad_alloc&) {m_CameraDiagnostic="Pilot history preparation failed; Camera unchanged.";return false;}
    if(!CancelCameraGesture())return false;
    SetEditorMode(EditorMode::Edit);CancelGroupTransform();
    if(m_SimulationState==Rbs::SimulationState::Running)SetSimulationState(Rbs::SimulationState::Paused);
    SelectColliderEntity(entity,false);if(m_HoveredEntity!=entity)return false;
    pilot.command->generation=m_SceneGeneration;
    pilot.command->edits[0]={id,entity.GetParent()?m_ActiveScene->RenderData().Identify(entity.GetParent()).value_or(EntityRenderId{}):EntityRenderId{}, {},*before,*before};
    pilot.command->parentWorld[0]=*parent;pilot.current=*before;
    pilot.parentRotation=glm::normalize(glm::quat_cast(basis));
    pilot.navigation=Camera::_EditorCamera(glm::degrees(float(desc.verticalFovRadians)),1280.f,720.f,float(desc.nearPlane),float(desc.farPlane));
    auto& nav=pilot.navigation;
    const Quat worldRotation(desc.orientation);
    const Vec3f forward=worldRotation*Vec3f{0,0,-1};
    nav.m_Position=Vec3f(desc.position);nav.m_Pitch=-std::asin(std::clamp(forward.y,-1.f,1.f));nav.m_Yaw=std::atan2(forward.x,-forward.z);
    nav.m_PositionDelta={};nav.m_PitchDelta=nav.m_YawDelta=0;
    nav.m_Distance=std::max(1.f,m_EditorCamera_.GetDistance());nav.m_FocalPoint=nav.m_Position+nav.GetForwardDirection()*nav.m_Distance;
    nav.m_CameraMode=Camera::CameraMode::FLYCAM;nav.m_Panning=nav.m_Rotating=false;nav.UpdateView();
    pilot.navigationPosition=nav.GetPosition();pilot.navigationRotation=nav.GetOrientation();
    pilot.orientationOffset=glm::inverse(pilot.navigationRotation)*worldRotation; // Retain existing Camera roll.
    Manager::InputEvent cancel{};cancel.kind=Manager::InputKind::Cancel;cancel.cleanup=true;
    if(auto accepted=nav.OnViewInput(cancel,false);!accepted)return false;
    m_CameraPilot.emplace(std::move(pilot));m_CameraView=CameraView::Pilot;
    m_CameraPilot->navigation.SetViewportSize(std::max(1.f,m_ViewportSize.x),std::max(1.f,m_ViewportSize.y));
    m_InspectorTabRequest=1;m_ViewPickRequested=false;CancelViewportMarquee();ClearOutlineHover();
    SetViewportActive(true);m_ViewportRequestFocus=true;
    m_CameraDiagnostic="Piloting selected Camera. Navigate in the main Viewport; Commit keeps one undoable pose, Cancel restores the starting pose.";
    return true;
}

bool RigidBodySimulationApp::FinishCameraPilot(bool commit)
{
    if(!m_CameraPilot)return true;
    if(!CancelCameraGesture())return false;
    auto& p=*m_CameraPilot;auto& edit=p.command->edits[0];
    auto resolved=m_ActiveScene->RenderData().Resolve(edit.entity);
    auto actual=m_ActiveScene->ReadEditorTransform(edit.entity);
    const auto parent=resolved?_Entity{*resolved,m_ActiveScene.get()}.GetParent():_Entity{};
    const auto parentId=parent?m_ActiveScene->RenderData().Identify(parent).value_or(EntityRenderId{}):EntityRenderId{};
    const bool owned=p.command->generation==m_SceneGeneration && resolved && actual && *actual==p.current && parentId==edit.parent;
    bool restoredExactly=true,notified=true;
    if(owned && !commit && p.current!=edit.before) {
        auto restored=m_ActiveScene->SetLocalPose(_Entity{*resolved,m_ActiveScene.get()},edit.before.translation,edit.before.rotation);
        if(!restored){m_CameraDiagnostic="Pilot Cancel could not restore the Camera; session retained.";p.finish=0;return false;}
        auto readback=m_ActiveScene->ReadEditorTransform(edit.entity);
        restoredExactly=readback && *readback==edit.before;notified=bool(restored->notification);
    }
    if(owned && commit && p.current!=edit.before) {
        edit.after=p.current;m_GroupTransform.Store(std::move(p.command));
    }
    m_CameraPilot.reset();m_CameraView=CameraView::Editor;m_PreviewCamera={};
    m_FramePickPixel.reset();m_ViewPickRequested=false;ClearOutlineHover();
    m_CameraDiagnostic=!owned || !restoredExactly?"Pilot target changed or retired; foreign changes preserved, Editor view restored.":
        !notified?"Pilot pose restored, but a Transform observer reported failure. Editor view restored.":
        commit?"Pilot committed as one Camera Transform. Ctrl+Z Undo / Ctrl+Y Redo. Editor view restored.":
               "Pilot cancelled; starting Camera Transform and Editor view restored.";
    m_PropertyDiagnostic=m_CameraDiagnostic;return true;
}

void RigidBodySimulationApp::UpdateCameraPilot(Timestep ts)
{
    if(!m_CameraPilot)return;
    auto& p=*m_CameraPilot;
    const auto& edit=p.command->edits[0];auto resolved=m_ActiveScene->RenderData().Resolve(edit.entity);
    auto actual=m_ActiveScene->ReadEditorTransform(edit.entity);
    const auto parent=resolved?CameraParentWorld(*m_ActiveScene,_Entity{*resolved,m_ActiveScene.get()}):std::optional<Mat4>{};
    const auto parentEntity=resolved?_Entity{*resolved,m_ActiveScene.get()}.GetParent():_Entity{};
    const auto parentId=parentEntity?m_ActiveScene->RenderData().Identify(parentEntity).value_or(EntityRenderId{}):EntityRenderId{};
    if(!resolved || !actual || *actual!=p.current || p.command->generation!=m_SceneGeneration ||
       parentId!=edit.parent || !parent || *parent!=p.command->parentWorld[0]) {
        (void)FinishCameraPilot(false);m_CameraDiagnostic="Pilot stopped: Camera identity, Transform or parent frame changed.";return;
    }
    if(p.finish){(void)FinishCameraPilot(p.finish==1);return;}
    auto& nav=p.navigation;nav.OnUpdate(ts);
    const auto position=nav.GetPosition();const auto rotation=nav.GetOrientation();nav.UpdateView();
    if(position==p.navigationPosition && rotation==p.navigationRotation)return;
    const auto localPosition=Vec3f(glm::inverse(*parent)*glm::vec4(position,1));
    const auto localRotation=glm::normalize(glm::inverse(p.parentRotation)*rotation*p.orientationOffset);
    auto applied=m_ActiveScene->SetLocalPose(_Entity{*resolved,m_ActiveScene.get()},localPosition,localRotation);
    if(!applied){(void)FinishCameraPilot(false);m_CameraDiagnostic="Pilot pose rejected; starting Camera restored.";return;}
    auto readback=m_ActiveScene->ReadEditorTransform(edit.entity);
    if(!readback){(void)FinishCameraPilot(false);return;}
    p.current=*readback;p.navigationPosition=position;p.navigationRotation=rotation;
    if(!applied->notification){(void)FinishCameraPilot(false);m_CameraDiagnostic="Pilot stopped after a Transform notification failure.";}
}

void RigidBodySimulationApp::DrawCameraPilotControls(float availableWidth)
{
    if(!m_CameraPilot)return;
    auto layout=Rbs::Editor::MeasurePilotControls(availableWidth>0.f?availableWidth:ImGui::GetContentRegionAvail().x);
    // Auto-sizing Camera menus may not have established their content width yet.
    if(availableWidth<=0.f && layout.size.x<layout.minimumWidth)
        layout=Rbs::Editor::MeasurePilotControls(layout.minimumWidth);
    ImGui::BeginGroup(); // Preserve this anchor when ItemSize advances to the next line.
    ImGui::TextUnformatted(Rbs::Editor::PilotControlsLayout::Title);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+layout.size.x);
    ImGui::TextDisabled("%s",Rbs::Editor::PilotControlsLayout::Hint);
    ImGui::PopTextWrapPos();
    if(ImGui::Button("Commit Pilot",layout.button))m_CameraPilot->finish=1;
#ifdef GENGINE_PHASE28_QUALIFICATION
    if(availableWidth>0.f)Rbs::Phase28AngularWidget(*this,"pilot-overlay-commit");
#endif
    if(!layout.stacked)ImGui::SameLine();
    if(ImGui::Button("Cancel Pilot",layout.button))m_CameraPilot->finish=2;
#ifdef GENGINE_PHASE28_QUALIFICATION
    if(availableWidth>0.f)Rbs::Phase28AngularWidget(*this,"pilot-overlay-cancel");
#endif
    Rbs::Editor::Tooltip("Cancel or Esc restores the starting pose. Commit creates one undoable Transform. Both restore the independent Editor view.");
    ImGui::EndGroup();
}

void RigidBodySimulationApp::EditorHistoryShortcuts()
{
    if(!TransformEditingAllowed() || m_GroupTransform.number.editing)return;
    if(!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())return;
    if(ImGui::GetIO().KeyCtrl) {
        if(ImGui::IsKeyPressed(ImGuiKey_Z,false))m_GroupTransform.pending=Rbs::Editor::TransformSession::Action::Undo;
        if(ImGui::IsKeyPressed(ImGuiKey_Y,false))m_GroupTransform.pending=Rbs::Editor::TransformSession::Action::Redo;
    }
}

void RigidBodySimulationApp::DrawEntityPresentationProperties()
{
    namespace E=Rbs::Editor;
    auto id=m_ActiveScene->RenderData().Identify(m_HoveredEntity);if(!id)return;
    bool visible=EditorEntityVisible(*id);
    if(ImGui::Checkbox("Scene Visible",&visible))QueueEntityVisibility(*id,visible);
    E::Tooltip("Editor mesh/icon/picking only. Runtime Camera output, lighting, shadows and Physics remain active. Gizmo display is separate.");
    if(ImGui::Button("Rename entity"))BeginEntityRename(*id);
    if(m_RenameTarget==*id)ImGui::TextDisabled("Rename in the Hierarchy. Enter applies; Esc cancels.");
    auto row=std::find_if(m_EntityPresentation.begin(),m_EntityPresentation.end(),[&](const auto& v){return v.entity==*id;});
    if(row!=m_EntityPresentation.end() && !row->key)ImGui::TextDisabled("Session-only entity; no scene serialization.");
}

void RigidBodySimulationApp::DrawCameraPanels()
{
    namespace E=Rbs::Editor;
    for(auto& panel:m_CameraPanels) {
        panel.visible=false;if(!panel.open)continue;
        auto e=m_ActiveScene->RenderData().Resolve(panel.entity);
        if(!e || !_Entity{*e,m_ActiveScene.get()}.HasAllComponents<RenderCameraComponent>()) {
            panel.open=false;if(panel.key)E::EntityPreferences[{EditorPresetKey(),panel.key}].cameraOpen=false;ImGui::MarkIniSettingsDirty();continue;
        }
        const auto& name=_Entity{*e,m_ActiveScene.get()}.Name();
        const auto title=panel.key?std::format("Camera: {}###RbsCamera_{}_{:016x}",name,EditorPresetKey(),panel.key):
            std::format("Camera: {}###RbsCamera_session_{}_{}_{}",name,panel.entity.registry,panel.entity.generation,panel.entity.index);
        ImGui::SetNextWindowSize({E::Px(480),E::Px(320)},ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
        if(ImGui::Begin(title.c_str(),&panel.open,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|(panel.key?0:ImGuiWindowFlags_NoSavedSettings))) {
            EditorHistoryShortcuts();
            PublishPanelInputRegion();auto size=ImGui::GetContentRegionAvail();const auto imageOrigin=ImGui::GetCursorScreenPos();
            auto scale=UI::CurrentViewportFramebufferScale();
            auto pixels=scale?ToFramebufferPixels(EditorViewportLogicalSize{size.x,size.y},*scale):std::expected<EditorViewportPixelSize,PlatformError>(std::unexpected(scale.error()));
            panel.visible=pixels && pixels->Width>0 && pixels->Height>0;
            if(panel.visible){panel.width=pixels->Width;panel.height=pixels->Height;}
            if(panel.color && panel.ready) {
                if(auto image=UI::FramebufferImage(*panel.color,size.x,size.y);!image)panel.diagnostic=image.error().message;
            } else ImGui::TextWrapped("%s",panel.diagnostic.empty()?"Preparing Camera View...":panel.diagnostic.c_str());
            // Overlay controls preserve the exact image/target extent when opened
            // or docked. No second image, target, render pass or navigation controller.
            const auto afterImage=ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos({imageOrigin.x+E::Px(6),imageOrigin.y+E::Px(6)});
            ImGui::PushID(int(panel.entity.index));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{E::Px(10),E::Px(10)});
            if(ImGui::Button("Camera..."))ImGui::OpenPopup("CameraViewActions");
            if(ImGui::BeginPopup("CameraViewActions")) {
                if(m_CameraPilot)DrawCameraPilotControls();
                else if(ImGui::MenuItem("Pilot Camera",nullptr,false,TransformEditingAllowed()))(void)BeginCameraPilot(panel.entity);
                if(ImGui::MenuItem("Edit Transform",nullptr,false,TransformEditingAllowed()))(void)EditSceneCamera(panel.entity);
                E::Tooltip("Select this Camera in the shared Properties panel, pause physics and return to Editor navigation. Use T/R or numeric Position/Rotation.");
                const bool root=!_Entity{*e,m_ActiveScene.get()}.GetParent();
                if(ImGui::MenuItem("Match Editor View",nullptr,false,root && TransformEditingAllowed()))(void)EditSceneCamera(panel.entity,true);
                E::Tooltip(root?"Copy Editor Camera position and rotation through the atomic Transform/Undo path. Projection is preserved.":"Parented Cameras use local Transform editing.");
                ImGui::Separator();
                if(ImGui::MenuItem("Preview in main Viewport"))(void)SetCameraView(CameraView::Preview,panel.entity);
                if(ImGui::MenuItem("Make Active",nullptr,false,TransformEditingAllowed()))(void)SelectSceneCamera(panel.entity);
                if(ImGui::MenuItem("Return main Viewport to Editor Camera"))(void)SetCameraView(CameraView::Editor);
                ImGui::Separator();ImGui::TextDisabled("Camera View monitors output. Pilot navigates this Camera in the main Viewport.");
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();ImGui::PopID();ImGui::SetCursorScreenPos(afterImage);
        }
        ImGui::End();ImGui::PopStyleVar();
        if(!panel.open){if(panel.key)E::EntityPreferences[{EditorPresetKey(),panel.key}].cameraOpen=false;ImGui::MarkIniSettingsDirty();}
    }
}

void RigidBodySimulationApp::RenderCameraPanels()
{
    bool retired=false;
    for(auto& panel:m_CameraPanels) {
        if(!panel.open || !m_ActiveScene->RenderData().Resolve(panel.entity)) {
            retired|=bool(panel.cascade);panel.color.reset();panel.cascade.reset();panel.ready=false;continue;
        }
        if(!panel.visible || !panel.width || !panel.height)continue;
        panel.ready=false;panel.diagnostic.clear();
        auto spec=m_RenderTarget->Description();spec.Storage.Width=panel.width;spec.Storage.Height=panel.height;spec.SizeSource=TargetSizeSource::Fixed;
        if(!panel.color) {
            auto target=RenderTarget::Create(spec);if(!target){panel.diagnostic=target.error().message;continue;}panel.color.emplace(std::move(*target));
        } else if(auto resized=panel.color->Reconfigure(spec);!resized){panel.diagnostic=resized.error().message;continue;}
        const auto shadowSpec=m_CascadeShadowFrameBuffer->Buffer().Description();
        if(!panel.cascade || panel.cascade->Buffer().Description().Width!=shadowSpec.Width) {
            auto target=CascadeShadowFrameBuffer::Create(shadowSpec.Width,shadowSpec.Height,shadowSpec.Layers-1);
            if(!target){panel.diagnostic=target.error().message;continue;}
            panel.cascade.emplace(std::move(*target));retired=true;
        }
        if(retired){m_FrameSubmission->RetireCameraCaches();retired=false;}
        auto access=m_SceneResources->Publication().BeginFrame();
        CameraViewport viewport;viewport.logicalSize={double(panel.width),double(panel.height)};viewport.pixelSize={panel.width,panel.height};
        auto snapshot=m_ActiveScene->SnapshotSceneCamera(panel.entity,m_SceneResources->ForFrame(access),viewport);
        if(!snapshot){panel.diagnostic="Camera unavailable: invalid/deleted Camera or transform.";continue;}
        const auto& cameraDesc=snapshot->Description();const auto camera=FrameCamera::FromSnapshot(panel.entity,*snapshot);
        const auto slices=Rbs::Editor::CameraCascadeSplits(m_ShadowCascadeLevels,m_EditorCamera_.GetNearClip(),m_EditorCamera_.GetFarClip(),cameraDesc.nearPlane,cameraDesc.farPlane);
        if(!slices){panel.diagnostic="Camera clipping interval cannot represent shadow slices.";continue;}
        RenderExtractionStats extraction;
        auto frame=ExtractRenderFrame(*m_ActiveScene,m_SceneResources->ForFrame(access),extraction,{&camera,1});
        if(!frame){panel.diagnostic="Camera frame extraction failed.";continue;}
        FrameSubmissionDesc targets{*panel.color,*m_MousePickFrameBuffer,*m_PointShadowFrameBuffer,*panel.cascade,
            m_SceneResources->Pipelines(),*slices,float(cameraDesc.verticalFovRadians),float(panel.width)/panel.height,
            float(cameraDesc.nearPlane),float(cameraDesc.farPlane),m_NearPlane,m_FarPlane};
        targets.pickingEnabled=false;
        targets.measureSubmission=GetWindow()->Timings().Enabled();
        auto result=m_FrameSubmission->Submit(*frame,targets,panel.picks);
        if(!result){panel.diagnostic=DescribeSubmissionError(result.error());continue;}
        panel.stats=*result;
        if(panel.color->IsMultiSampled())if(auto resolved=panel.color->BindAndBlitToScreen();!resolved){panel.diagnostic=resolved.error().message;continue;}
        panel.color->UnBind();panel.ready=true;++panel.renderedFrames;
    }
    if(retired)m_FrameSubmission->RetireCameraCaches();
    std::erase_if(m_CameraPanels,[](const auto& panel){return !panel.open;});
}
