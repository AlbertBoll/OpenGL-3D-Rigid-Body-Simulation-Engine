#include "RigidBodySimulation.h"
#include "RbsEditorState.h"
#include "RbsEditorWidgets.h"
#include "RbsComponentEditing.h"
#include "UI/UICore.h"
#include "UI/FramebufferImage.h"
#include "Scene/_Scene.h"
#include <algorithm>
#include <format>
#include <bit>

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
    if(!m_ActiveScene->RenderData().Resolve(id))return;
    if(m_GroupTransform.released){m_PropertyDiagnostic="Transform commit pending; retry visibility after this frame.";return;}
    CancelGroupTransform();auto& batch=m_GroupTransform.preview;batch={};
    batch.kind=Rbs::Editor::TransformBatch::Kind::Visibility;batch.target=id;batch.generation=m_SceneGeneration;
    batch.beforeVisible=EditorEntityVisible(id);batch.afterVisible=visible;
    m_GroupTransform.dirty=true;m_GroupTransform.pending=Rbs::Editor::TransformSession::Action::Apply;
}

void RigidBodySimulationApp::BeginEntityRename(EntityRenderId id)
{
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
            m_RenameTarget={};m_HierarchySelectionHandoff=true;
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

void RigidBodySimulationApp::EditorHistoryShortcuts()
{
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
            PublishPanelInputRegion();auto size=ImGui::GetContentRegionAvail();
            auto scale=UI::CurrentViewportFramebufferScale();
            auto pixels=scale?ToFramebufferPixels(EditorViewportLogicalSize{size.x,size.y},*scale):std::expected<EditorViewportPixelSize,PlatformError>(std::unexpected(scale.error()));
            panel.visible=pixels && pixels->Width>0 && pixels->Height>0;
            if(panel.visible){panel.width=pixels->Width;panel.height=pixels->Height;}
            if(panel.color && panel.ready) {
                if(auto image=UI::FramebufferImage(*panel.color,size.x,size.y);!image)panel.diagnostic=image.error().message;
            } else ImGui::TextWrapped("%s",panel.diagnostic.empty()?"Preparing Camera View...":panel.diagnostic.c_str());
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
        auto result=m_FrameSubmission->Submit(*frame,targets,panel.picks);
        if(!result){panel.diagnostic=DescribeSubmissionError(result.error());continue;}
        panel.stats=*result;
        if(panel.color->IsMultiSampled())if(auto resolved=panel.color->BindAndBlitToScreen();!resolved){panel.diagnostic=resolved.error().message;continue;}
        panel.color->UnBind();panel.ready=true;++panel.renderedFrames;
    }
    if(retired)m_FrameSubmission->RetireCameraCaches();
    std::erase_if(m_CameraPanels,[](const auto& panel){return !panel.open;});
}
