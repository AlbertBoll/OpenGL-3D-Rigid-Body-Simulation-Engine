#include "RigidBodySimulation.h"
#include "RbsEditorIcons.h"
#include "RbsComponentEditing.h"
#include "RbsSpotGuide.h"
#include "Scene/_Scene.h"
#include <imguizmo/ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <format>
#ifdef GENGINE_PHASE28_QUALIFICATION
namespace Rbs { void Phase28AngularWidget(RigidBodySimulationApp&,const char*); }
#endif

using namespace ::GEngine;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;

namespace
{
    // A tool shortcut arms the next numeric gesture without taking input from a mouse drag.
    constexpr int AwaitKeyboardAxis = -2;
    constexpr int UniformScaleAxis = static_cast<int>(ImGuizmo::AxisHandle::Uniform);

    bool GizmoNumberPrefix(unsigned c)
    {
        return (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
    }

    std::optional<Mat4> GizmoWorldMatrix(_Scene& scene, _Entity entity)
    {
        auto local=scene.GetRenderTransform(entity);
        if(!local)return {};
        Mat4 matrix=local->matrix;
        // Scene-only hierarchy composes local presentation matrices. Live bodies
        // are existing world-space anchors. No full scene traversal is needed.
        while(!entity.HasAllComponents<RigidBody3DComponent>())
        {
            entity=entity.GetParent();if(!entity)break;
            auto parent=scene.GetRenderTransform(entity);if(!parent)return {};
            matrix=parent->matrix*matrix;
        }
        return matrix;
    }
}


std::optional<std::size_t> RigidBodySimulationApp::RagdollBoneFor(_Entity entity) const
{
    if(!m_Ragdoll.draft || !m_ActiveScene || entity.GetSceneContext()!=m_ActiveScene.get() || !entity)
        return {};
    const auto binding=m_ActiveScene->GetBodyBinding(entity);
    if(!binding)return {};
    for(std::size_t i=0;i<m_RagdollBindings.entities.size();++i)
        if(m_RagdollBindings.entities[i]==static_cast<entt::entity>(entity) &&
           m_RagdollBindings.mapping.bodies[i]==*binding)return i;
    return {};
}

bool RigidBodySimulationApp::IsAngularEndpoint(_Entity entity) const
{
    if(!m_ActiveSelection.IsAngularScene() || !m_ActiveScene ||
       entity.GetSceneContext()!=m_ActiveScene.get() || !entity)return false;
    const auto binding=m_ActiveScene->GetBodyBinding(entity);
    for(std::size_t i=0;i<2 && i<m_AngularBindings.bodyCount;++i)
        if(static_cast<entt::entity>(entity)==m_AngularBindings.entities[i] ||
           (binding && *binding==m_AngularBindings.bodies[i]))return true;
    return false;
}

bool RigidBodySimulationApp::ProtectsAngularTransform(_Entity entity) const
{
    if(RagdollBoneFor(entity))return true;
    if(!m_ActiveSelection.IsAngularScene() || !m_ActiveScene ||
       entity.GetSceneContext()!=m_ActiveScene.get() || !entity)return false;
    if(IsAngularEndpoint(entity))return true;
    for(std::size_t i=0;i<2 && i<m_AngularBindings.bodyCount;++i) {
        const _Entity endpoint{m_AngularBindings.entities[i],m_ActiveScene.get()};
        if(!endpoint)continue;
        for(auto parent=endpoint.GetParent();parent;parent=parent.GetParent())
            if(parent==entity)return true;
    }
    return false;
}

bool RigidBodySimulationApp::SelectionProtectsAngularTransform() const
{
    if(!m_ActiveScene || (!m_ActiveSelection.IsAngularScene() && !m_Ragdoll.draft))return false;
    for(std::size_t i=0;i<m_EditorSelection.count;++i)
        if(auto id=m_ActiveScene->RenderData().Resolve(m_EditorSelection.ids[i]);id &&
           ProtectsAngularTransform(_Entity{*id,m_ActiveScene.get()}))return true;
    return false;
}

bool RigidBodySimulationApp::TransformEditingAllowed() const
{
    using State=Rbs::SimulationState;
    return m_EditorMode==EditorMode::Edit && !m_CameraPilot && !m_SceneRequest && !m_BackendRequest &&
        (m_SimulationState==State::Ready || m_SimulationState==State::Paused);
}

void RigidBodySimulationApp::SetEditorMode(EditorMode mode)
{
    if(m_CameraPilot){m_CameraDiagnostic="Commit or Cancel Pilot before changing Editor mode.";return;}
    if(m_EditorMode==mode)return;
    if(mode==EditorMode::Simulation &&
       (m_SimulationState==Rbs::SimulationState::Switching || m_SimulationState==Rbs::SimulationState::Failed))return;
    if (mode==EditorMode::Simulation && (m_SceneRequest || m_BackendRequest || m_RenameTarget || m_NumericTransform || m_GroupTransform.number.editing ||
        m_GroupTransform.dirty || m_Angular.dirty || m_SceneDefaultsPending || m_PendingMotor ||
        (m_JoltSettingsEditor.generation==m_SceneGeneration && (m_JoltSettingsEditor.dirty || m_JoltSettingsEditor.queued)) ||
        (m_WorldSleepEditor.generation==m_SceneGeneration && (m_WorldSleepEditor.dirty || m_WorldSleepEditor.queued)) ||
        m_PendingSelection!=m_ActiveSelection || m_PendingBackend!=m_Backend ||
        (m_Angular.committed && m_PendingAngularVariant!=m_Angular.committed->variant) ||
        m_GroupTransform.released || m_GroupTransform.pending!=Rbs::Editor::TransformSession::Action::None ||
        HasPendingObjectProperties() || Rbs::Editor::Numeric::HasError())) {
        m_SceneDiagnostic=m_PropertyDiagnostic="Finish or revert pending edits before simulating.";
        return;
    }
    if (mode==EditorMode::Edit) {
        auto* physics=m_ActiveScene?m_ActiveScene->GetPhysicsSystem():nullptr;
        if (!physics || !physics->GetPhysicsWorld() || physics->GetBackendFailure() ||
            m_SceneRequest || m_BackendRequest || m_SimulationState==Rbs::SimulationState::Failed ||
            m_SimulationState==Rbs::SimulationState::Switching || m_Impulse.pending || m_ChainImpulse.Pending()) {
            m_SceneDiagnostic="Finish the pending operation or recover the world before entering Edit.";
            return;
        }
        // SetPaused collapses presentation history to the last completed live pose.
        if(m_SimulationState==Rbs::SimulationState::Running) SetSimulationState(Rbs::SimulationState::Paused);
        Rbs::Editor::Numeric::Reset();
        m_SceneDiagnostic="Edit mode. The current world is stopped.";
    } else {
        m_SceneDiagnostic.clear();
    }
    // Preserve a released gesture queued for Update; cancel only an active drag.
    CancelGroupTransform();m_EditorMode=mode;
    m_PropertyDiagnostic.clear();
}

void RigidBodySimulationApp::FinishTransformGesture()
{
    auto& s=m_GroupTransform;
    if(s.dirty){s.released=true;s.pending=Rbs::Editor::TransformSession::Action::Apply;}
    else CancelGroupTransform();
}

const char* RigidBodySimulationApp::TransformToolDisabled(TransformTool tool) const
{
    if(m_CameraPilot)return "Commit or Cancel Pilot before using Transform tools.";
    if(tool==TransformTool::Select)return nullptr;
    if(m_SceneRequest || m_BackendRequest || m_SimulationState==Rbs::SimulationState::Switching)
        return "Wait for the scene transaction to complete.";
    if(!m_EditorSelection.count)return "Select objects with Transforms.";
    if(!TransformEditingAllowed())return "Switch to Edit to transform objects.";
    if(m_EditorSelection.count>::GEngine::EditorTransformBudget::Roots)
        return "Transform exceeds the 4096-root staging budget. Full selection retained; no objects modified.";
    for(std::size_t i=0;i<m_EditorSelection.count;++i)
    {
        auto id=m_ActiveScene->RenderData().Resolve(m_EditorSelection.ids[i]);
        if(!id)return "Selection contains a stale entity.";
        const _Entity entity{*id,m_ActiveScene.get()};
        if(!entity.HasAllComponents<Transform3DComponent>())return "Every selected object needs a Transform.";
        if(RagdollBoneFor(entity)) {
            if(m_EditorSelection.count!=1)return "Select one ragdoll bone to stage a connected transform.";
            if(m_EditorMode!=EditorMode::Edit ||
               (m_SimulationState!=Rbs::SimulationState::Ready && m_SimulationState!=Rbs::SimulationState::Paused))
                return "Pause and enter Edit mode to stage a ragdoll transform.";
        }
        else if(ProtectsAngularTransform(entity))return "Select one joint endpoint and use Transform in Properties to move the connected pair.";
        if(entity==m_SkyBoxEntity)return "The sky follows the camera. Change its visibility in World Settings.";
        if(entity.HasAllComponents<RenderCameraComponent>() && tool==TransformTool::Scale)
            return "Camera supports Translate and Rotate; use projection properties for size.";
        if(entity.HasAllComponents<RenderLightComponent>()) {
            const auto kind=entity.GetComponent<RenderLightComponent>().kind;
            if(tool==TransformTool::Scale)return "Light scale is not an illumination parameter; edit range and cone properties.";
            if(kind==RenderLightKind::Directional && tool!=TransformTool::Rotate)return "Directional Light supports Rotate; position does not affect illumination.";
            if(kind==RenderLightKind::Point && tool!=TransformTool::Translate)return "Point Light supports Translate; its illumination has no orientation.";
        }
        if(entity.HasAllComponents<RigidBody3DComponent>())
        {
            if(!m_ActiveScene->GetBodyBinding(entity))return "A selected body's identity is unavailable.";
            if(entity.GetParent())return "Parented live-body transforms are unsupported.";
        }
        const auto scale=entity.GetComponent<Transform3DComponent>().Scale;
        if(scale.x<=0 || scale.y<=0 || scale.z<=0)return "Gizmos require positive, nonsingular scale.";
        for(auto parent=entity.GetParent();parent;parent=parent.GetParent())
        {
            const auto ps=parent.Transform().Scale;
            if(ps.x<=0 || ps.y<=0 || ps.z<=0)return "A parent has singular or mirrored scale.";
            if(tool!=TransformTool::Translate && (std::abs(ps.x-ps.y)>1e-5f || std::abs(ps.x-ps.z)>1e-5f))
                return "Rotation/scaling under nonuniform parent scale would introduce unsupported shear.";
            if(parent.HasAllComponents<RigidBody3DComponent>())break;
        }
    }
    return nullptr;
}

bool RigidBodySimulationApp::ActivateTransformTool(TransformTool tool)
{
    if(const auto* reason=TransformToolDisabled(tool)) {m_PropertyDiagnostic=reason;return false;}
    CancelGroupTransform();m_TransformTool=tool;m_ShowGizmo=true;
    if (tool != TransformTool::Select && m_ViewportActive)
        m_ViewportOwnsSelection = m_EditorSelection.count != 0;
    return true;
}

bool RigidBodySimulationApp::HandleTransformToolShortcut(const InputEvent& event,
    const InputRouting& route,std::uint32_t mouseButtons,bool pointerCaptured)
{
    // View routing owns admission. Never borrow a hover identity, a text/widget
    // gesture, another window's focus, or an RMB fly/Alt camera gesture.
    auto& session=m_GroupTransform;
    auto& number=session.number;
    if(number.editing && event.kind==InputKind::KeyDown && event.window==route.window)
        return true; // The ImGui text field owns Enter, Escape, navigation and text undo.
    const auto key=event.key;
    const bool uniformPrefix=number.axis==AwaitKeyboardAxis && m_TransformTool==TransformTool::Scale &&
        ((key>=GENGINE_KEY_1 && key<=GENGINE_KEY_0) || (key>=GENGINE_KEY_KP_1 && key<=GENGINE_KEY_KP_0) ||
         key==GENGINE_KEY_MINUS || key==GENGINE_KEY_PERIOD || key==GENGINE_KEY_EQUALS ||
         key==GENGINE_KEY_KP_PLUS || key==GENGINE_KEY_KP_MINUS || key==GENGINE_KEY_KP_PERIOD);
    constexpr auto chordModifiers=LeftControl|RightControl|LeftAlt|RightAlt|LeftSuper|RightSuper;
    const auto commandModifiers=uniformPrefix ? chordModifiers : chordModifiers|LeftShift|RightShift;
    if(!SandboxEnabled() || !m_ActiveScene ||
       event.kind!=InputKind::KeyDown || event.cleanup || event.repeated ||
       (event.modifiers&commandModifiers) || event.window!=route.window ||
       !route.visible || !route.focused || route.uiKeyboard || route.uiMouse ||
       !m_ViewportActive || !m_ViewportForcused || m_ViewportActivationButtons ||
       mouseButtons || pointerCaptured || m_GizmoUsing || m_ViewportPopupOpen || m_RenameTarget)return false;
    if(auto* ui=ImGui::GetCurrentContext();ui && (ui->IO.WantTextInput || ui->ActiveId || ui->DragDropActive ||
       ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)))return false;
    if(m_CameraPilot)return false; // WASD belongs to the existing navigation controller.
    if(m_EditorMode==EditorMode::Simulation) {
        if(event.key==GENGINE_KEY_S && m_SimulationState==Rbs::SimulationState::Ready)SetSimulationState(Rbs::SimulationState::Running);
        else if(event.key==GENGINE_KEY_P && m_SimulationState==Rbs::SimulationState::Running)SetSimulationState(Rbs::SimulationState::Paused);
        else if(event.key==GENGINE_KEY_R && m_SimulationState==Rbs::SimulationState::Paused)SetSimulationState(Rbs::SimulationState::Running);
        return event.key==GENGINE_KEY_S || event.key==GENGINE_KEY_P || event.key==GENGINE_KEY_R || event.key==GENGINE_KEY_T;
    }
    const auto tool=event.key==GENGINE_KEY_T?TransformTool::Translate:
        event.key==GENGINE_KEY_R?TransformTool::Rotate:event.key==GENGINE_KEY_S?TransformTool::Scale:TransformTool::Select;
    if(number.axis==AwaitKeyboardAxis) {
        const int axis=key==GENGINE_KEY_X?0:key==GENGINE_KEY_Y?1:key==GENGINE_KEY_Z?2:-1;
        const bool armed=ImGui::GetFrameCount()-number.lastDrawFrame<=1;
        number={};
        if(key==GENGINE_KEY_ESCAPE)return true;
        if(armed && (axis>=0 || uniformPrefix) && m_ViewportOwnsSelection && !m_NumericTransform &&
           !session.released && TransformEditingAllowed() && !TransformToolDisabled(m_TransformTool) &&
           !RagdollBoneFor(m_HoveredEntity) && session.preview.count && session.preview.generation==m_SceneGeneration) {
            number.axis=axis>=0?axis:UniformScaleAxis;
            number.editing=number.focus=true;
            number.lastDrawFrame=ImGui::GetFrameCount();
            session.dragOrigin=session.origin;
            m_ViewPickRequested=m_OutlineToggleRequested=false;
            return true;
        }
    }
    if(tool!=TransformTool::Select && m_EditorSelection.count && !session.released && !m_NumericTransform &&
       ActivateTransformTool(tool) && !RagdollBoneFor(m_HoveredEntity) && BeginGroupTransform()) {
        number.axis=AwaitKeyboardAxis;
        number.lastDrawFrame=ImGui::GetFrameCount();
    }
    // Edit keys never fall through to simulation, including with no selection.
    return tool!=TransformTool::Select || event.key==GENGINE_KEY_P;
}

void RigidBodySimulationApp::CancelGroupTransform()
{
    if(m_GroupTransform.released)return; // A completed gesture belongs to the next safe Update.
    if(m_GizmoUsing)m_GizmoCancelled=true;
    auto& number=m_GroupTransform.number;
    if(auto* context=ImGui::GetCurrentContext();context && number.fieldId && context->ActiveId==number.fieldId)
        ImGui::ClearActiveID();
    number={};
    if(m_ActiveScene)(void)m_ActiveScene->PreviewEditorTransforms({});
    m_GroupTransform.preview.Clear();m_GroupTransform.proposal.Clear();m_GroupTransform.queryPending=false;
    m_GroupTransform.invalid=false;m_GroupTransform.resumeAfterApply=false;m_GroupTransform.live=false;m_GroupTransform.dirty=false;
    m_GroupTransform.pending=Rbs::Editor::TransformSession::Action::None;
}

bool RigidBodySimulationApp::BeginGroupTransform()
{
    auto& session=m_GroupTransform;
    if(session.preview.count)return true;
    if(TransformToolDisabled(m_TransformTool))return false;
    try {
        session.preview.Resize(m_EditorSelection.count);session.proposal.Resize(m_EditorSelection.count);
        session.world.resize(m_EditorSelection.count);session.parentWorld.resize(m_EditorSelection.count);
        session.poses.reserve(m_EditorSelection.count);
    } catch(const std::bad_alloc&) {CancelGroupTransform();m_PropertyDiagnostic="Transform preview allocation failed; selection retained.";return false;}
    session.preview.count=0;session.preview.generation=m_SceneGeneration;
    Vec3f center{};Mat4 activeWorld(1);
    for(std::size_t i=0;i<m_EditorSelection.count;++i)
    {
        auto resolved=m_ActiveScene->RenderData().Resolve(m_EditorSelection.ids[i]);if(!resolved)return false;
        const _Entity entity{*resolved,m_ActiveScene.get()};bool inherited=false;
        for(auto parent=entity.GetParent();parent;parent=parent.GetParent())
            if(m_EditorSelection.Contains(m_ActiveScene->RenderData().Identify(parent).value_or(EntityRenderId{})))
                {inherited=true;break;}
        if(i+1==m_EditorSelection.count)activeWorld=GizmoWorldMatrix(*m_ActiveScene,entity).value_or(Mat4(1));
        if(inherited)continue; // Real descendants inherit their selected ancestor exactly once.
        auto value=m_ActiveScene->ReadEditorTransform(m_EditorSelection.ids[i]);
        const auto world=GizmoWorldMatrix(*m_ActiveScene,entity);if(!value || !world)return false;
        if(const auto bone=RagdollBoneFor(entity)) {
            const auto& draft=m_Ragdoll.draft->bodies[*bone];
            value->translation=draft.center+Vec3f{0,Rbs::SandboxEnvironment::FloorTop+m_Ragdoll.draft->dropOffset,0};
            value->rotation=draft.orientation;
            value->scale=Rbs::RagdollRenderScale(draft);
            activeWorld=glm::translate(Mat4(1),value->translation)*glm::mat4_cast(value->rotation);
        }

        const auto parent=entity.GetParent();const auto index=session.preview.count++;
        session.preview.edits[index]={m_EditorSelection.ids[i],parent?m_ActiveScene->RenderData().Identify(parent).value_or(EntityRenderId{}):EntityRenderId{},
            m_ActiveScene->GetBodyBinding(entity).value_or(PhysicsBodyHandle{}),*value,*value};
        session.world[index]=entity.HasAllComponents<RigidBody3DComponent>()?
            glm::translate(Mat4(1),value->translation)*glm::mat4_cast(value->rotation)*glm::scale(Mat4(1),value->scale):*world;
        session.parentWorld[index]=parent?GizmoWorldMatrix(*m_ActiveScene,parent).value_or(Mat4(1)):Mat4(1);
        session.preview.parentWorld[index]=session.parentWorld[index];
        center+=Vec3f(session.world[index][3]);
    }
    if(!session.preview.count)return false;
    const auto position=Rbs::Editor::TransformPivot==1?Vec3f(activeWorld[3]):center/float(session.preview.count);
    Mat4 pivot=glm::translate(Mat4(1),position);
    if(Rbs::Editor::TransformOrientation==1)for(int i=0;i<3;++i)pivot[i]=glm::vec4(glm::normalize(Vec3f(activeWorld[i])),0);
    session.preview.Resize(session.preview.count);session.proposal.Resize(session.preview.count);
    session.previewTick=m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps();
    session.origin=session.pivot=session.gizmo=session.dragOrigin=pivot;return true;
}

bool RigidBodySimulationApp::ValidateTransformPlacement(Rbs::Editor::TransformBatch& batch,
    const PhysicsPlacementSettings& options,Mat4* pivot)
{
    auto& poses=m_GroupTransform.poses;std::size_t count=0;
    try {poses.resize(batch.count);}catch(const std::bad_alloc&){m_PropertyDiagnostic="Placement staging allocation failed; no objects modified.";return false;}
    for(auto& edit:std::span(batch.edits.data(),batch.count)) {
        const auto resolved=m_ActiveScene->RenderData().Resolve(edit.entity);
        if(!resolved){m_PropertyDiagnostic="The transform target is no longer in this scene.";return false;}
        const _Entity target{*resolved,m_ActiveScene.get()};
        if(ProtectsAngularTransform(target) || target==m_SkyBoxEntity)
        {m_PropertyDiagnostic="This target requires its own Properties controls.";return false;}
        const auto current=m_ActiveScene->ReadEditorTransform(edit.entity);
        if(current && m_GroupTransform.live && edit.body && current->scale==edit.before.scale)
            edit.before=*current; // Only a live gesture rebases the precommit pose, never Undo/Redo.
        if(!current || *current!=edit.before){m_PropertyDiagnostic="Transform source changed; edit rejected. Pause history replay requires the expected pose; it cannot rewind physics.";return false;}
        if(edit.body) {
            auto& p=poses[count++];p={edit.body,edit.after.translation,edit.after.rotation};
            if(edit.after.scale!=edit.before.scale){p.scale=edit.after.scale;p.referenceScale=edit.before.scale;}
        }
    }
    if((options.surface || options.resolve) && count!=batch.count) {
        m_PropertyDiagnostic="Surface snapping/correction requires collider bodies for every selected transform root.";return false;
    }
    if(!count)return true;
    auto queryOptions=options;queryOptions.preserveExistingContacts=true;
    auto result=m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->QueryEditorPlacement({poses.data(),count},queryOptions);
    if(!result){m_PropertyDiagnostic=std::string(result.error().operation);return false;}
    std::size_t i=0;for(auto& edit:std::span(batch.edits.data(),batch.count))if(edit.body) {
        edit.after.translation=result->poses[i].position;edit.after.rotation=result->poses[i++].rotation;
    }
    if(pivot) {
        const auto correction=glm::translate(Mat4(1),options.pivot+result->translation)*glm::mat4_cast(result->rotation)*
            glm::translate(Mat4(1),-options.pivot);*pivot=correction**pivot;
    }
    return true;
}

void RigidBodySimulationApp::ProcessTransformPreview()
{
    auto& s=m_GroupTransform;if(!std::exchange(s.queryPending,false))return;
    s.dirty=true;
    if(s.proposal.generation!=m_SceneGeneration || !TransformEditingAllowed()) {
        CancelGroupTransform();m_PropertyDiagnostic="Scene/simulation changed; candidate cancelled.";return;
    }
    auto& proposal=s.proposal;auto pivot=s.proposedPivot;
    if(proposal.count==1 && RagdollBoneFor(m_HoveredEntity) &&
       m_ActiveScene->RenderData().Identify(m_HoveredEntity)==proposal.edits[0].entity) {
        s.preview=proposal;s.pivot=pivot;s.invalid=false;
        m_PropertyDiagnostic="Release to stage the ragdoll pose. Apply the assembly in World Settings.";
        return;
    }
    if(!ValidateTransformPlacement(proposal,s.placement,&pivot)){s.invalid=true;return;}
    if(!m_ActiveScene->PreviewEditorTransforms(proposal.Span())) {
        s.invalid=true;m_PropertyDiagnostic="Preview identity changed; candidate rejected.";return;
    }
    s.preview=proposal;s.pivot=pivot;s.invalid=false;
    m_PropertyDiagnostic.clear();
}

void RigidBodySimulationApp::ProcessGroupTransform()
{
    if(m_CameraPilot)return;
    using Action=Rbs::Editor::TransformSession::Action;
    auto& session=m_GroupTransform;
    if(session.generation!=m_SceneGeneration){CancelGroupTransform();session.Reset(m_SceneGeneration);return;}
    if (!TransformEditingAllowed()) {
        if (session.dirty || session.released || session.pending!=Action::None) {
            session.released=false;
            CancelGroupTransform();
        }
        return;
    }
    const auto followup=session.released && (session.pending==Action::Undo || session.pending==Action::Redo)?session.pending:Action::None;
    if(followup!=Action::None)session.pending=Action::Apply;
    struct Followup {Rbs::Editor::TransformSession& session;Action action;~Followup(){if(action!=Action::None)session.pending=action;}} follow{session,followup};
    ProcessTransformPreview();
    const bool released=std::exchange(session.released,false);
    const auto action=std::exchange(session.pending,Action::None);if(action==Action::None)return;
    if(action==Action::Cancel){CancelGroupTransform();m_PropertyDiagnostic="Group preview cancelled; Scene and bodies unchanged.";return;}
    if(action==Action::Apply && session.preview.count==1 &&
       session.preview.kind==Rbs::Editor::TransformBatch::Kind::Transform) {
        const auto& edit=session.preview.edits[0];
        if(const auto bone=RagdollBoneFor(m_HoveredEntity);bone && !session.invalid && session.dirty &&
           !session.queryPending && edit.body==m_RagdollBindings.mapping.bodies[*bone] &&
           m_ActiveScene->RenderData().Identify(m_HoveredEntity)==edit.entity) {
            const auto& after=edit.after;
            const auto value=m_TransformTool==TransformTool::Translate?
                after.translation-Vec3f{0,Rbs::SandboxEnvironment::FloorTop+m_Ragdoll.draft->dropOffset,0}:
                m_TransformTool==TransformTool::Rotate?glm::eulerAngles(after.rotation):after.scale;
            StageRagdollTransform(*bone,m_TransformTool,value);
            CancelGroupTransform();return;
        }
    }
    if(!session.history)
    {
        session.history.reset(new(std::nothrow) std::array<std::unique_ptr<Rbs::Editor::TransformBatch>,Rbs::Editor::TransformSession::HistoryCapacity>);
        if(!session.history){m_PropertyDiagnostic="History allocation failed; no transform committed.";return;}
    }
    std::unique_ptr<Rbs::Editor::TransformBatch> command;
    try {
    command=std::make_unique<Rbs::Editor::TransformBatch>();auto& batch=*command;
    if(action==Action::Apply)
    {if(!session.dirty || session.invalid || session.queryPending){if(released)CancelGroupTransform();return;}batch=session.preview;}
    else
    {
        CancelGroupTransform();
        if(action==Action::Undo && session.cursor)batch=*(*session.history)[session.cursor-1];
        else if(action==Action::Redo && session.cursor<session.size)batch=*(*session.history)[session.cursor];
        else {m_PropertyDiagnostic="No transform batch to undo/redo.";return;}
        if(action==Action::Undo)for(std::size_t i=0;i<batch.count;++i)std::swap(batch.edits[i].before,batch.edits[i].after);
    }
    } catch(const std::bad_alloc&){m_PropertyDiagnostic="History preparation allocation failed; no edit committed.";if(released)CancelGroupTransform();return;}
    auto& batch=*command;
    if(batch.Bytes()>EditorTransformBudget::HistoryBytes){m_PropertyDiagnostic="Gesture exceeds the 8 MiB history budget; no edit committed.";if(released)CancelGroupTransform();return;}
    if(batch.generation!=m_SceneGeneration){session.Reset(m_SceneGeneration);m_PropertyDiagnostic="Stale transform history retired.";return;}
    if(batch.kind!=Rbs::Editor::TransformBatch::Kind::Transform) {
        if(m_SceneRequest || m_BackendRequest){m_PropertyDiagnostic="Wait for the scene transaction.";return;}
        try {
            if(!ApplyEntityPresentation(batch,action==Action::Undo))return;
        } catch(const std::bad_alloc&) {m_PropertyDiagnostic="Editor allocation failed; change rejected.";return;}
        if(action==Action::Apply) {
            session.Store(std::move(command));
        } else if(action==Action::Undo)--session.cursor;else ++session.cursor;
        CancelGroupTransform();return;
    }
    for(std::size_t i=0;i<batch.count;++i)
    {
        const auto& edit=batch.edits[i];
        if(!edit.parent)continue;
        const auto parent=m_ActiveScene->RenderData().Resolve(edit.parent);
        const auto world=parent?GizmoWorldMatrix(*m_ActiveScene,_Entity{*parent,m_ActiveScene.get()}):std::optional<Mat4>{};
        if(!world || *world!=batch.parentWorld[i])
        {m_PropertyDiagnostic="Parent transform changed; atomic group command rejected.";return;}
    }
    if(!ValidateTransformPlacement(batch,{})){if(released)CancelGroupTransform();return;} // Exact final poses; never re-snap Undo/Redo.
    if(!ApplyWorkingTransformBatch(batch.Span())) {if(released)CancelGroupTransform();return;}
    for(std::size_t i=0;i<batch.count;++i)
    {
        const auto actual=m_ActiveScene->ReadEditorTransform(batch.edits[i].entity);
        if(!actual)
        {CancelGroupTransform();session.Reset(m_SceneGeneration);m_PropertyDiagnostic="Batch committed; an observer removed a target, so history was retired.";return;}
        batch.edits[i].after=*actual;
    }
    if(action==Action::Apply)
    {
        session.Store(std::move(command));
    }
    else if(action==Action::Undo)--session.cursor;
    else ++session.cursor;
    CancelGroupTransform();
    m_PropertyDiagnostic.clear();
}

void RigidBodySimulationApp::DrawGroupTransformActions()
{
    namespace E=Rbs::Editor;using Action=E::TransformSession::Action;
    auto& session=m_GroupTransform;
    if(m_CameraPilot) {
        const auto saved=ImGui::GetCursorScreenPos();const auto origin=ImGui::GetMainViewport()->Pos;
        const float margin=E::Px(10);
        const ImVec2 padding{std::max(margin,ImGui::GetStyle().WindowPadding.x),std::max(E::Px(8),ImGui::GetStyle().WindowPadding.y)};
        const auto layout=E::MeasurePilotControls(m_ViewportBounds[1].x-m_ViewportBounds[0].x-2.f*(margin+padding.x));
        const ImVec2 size{layout.size.x+2.f*padding.x,layout.size.y+2.f*padding.y};
        if(layout.size.x<layout.minimumWidth || size.y+2.f*margin>m_ViewportBounds[1].y-m_ViewportBounds[0].y)
            return; // Too small for usable actions; the shared Properties panel retains them.
        const ImVec2 p{m_ViewportBounds[0].x+margin,m_ViewportBounds[1].y-margin-size.y};
        const ImVec2 end{p.x+size.x,p.y+size.y};
        ImGui::GetWindowDrawList()->AddRectFilled(p,end,IM_COL32(24,29,37,245),E::Px(6));
        ImGui::SetCursorScreenPos({p.x+padding.x,p.y+padding.y});DrawCameraPilotControls(layout.size.x);
#ifdef GENGINE_PHASE28_QUALIFICATION
        Rbs::Phase28AngularWidget(*this,"pilot-overlay-bounds");
#endif
        (void)GetInputManager()->AddUIRegion({p.x-origin.x,p.y-origin.y,end.x-origin.x,end.y-origin.y});
        if(ImGui::IsMouseHoveringRect(p,end)){m_ViewportOverlayInteracting=true;m_ViewPickRequested=false;}
        ImGui::SetCursorScreenPos(saved);return;
    }
    if(session.number.editing){DrawGizmoNumber();return;}
    if(!session.dirty || session.preview.kind!=E::TransformBatch::Kind::Transform)return;
    const auto saved=ImGui::GetCursorScreenPos();const float width=std::min(E::Px(480),m_ViewportBounds[1].x-m_ViewportBounds[0].x-E::Px(28));
    if(width<E::Px(80))return;
    const float height=E::Px(100);
    const ImVec2 p{(m_ViewportBounds[0].x+m_ViewportBounds[1].x-width)*.5f,m_ViewportBounds[1].y-height-E::Px(14)};
    ImGui::GetWindowDrawList()->AddRectFilled(p,{p.x+width,p.y+height},IM_COL32(24,29,37,245),E::Px(6));
    ImGui::SetCursorScreenPos({p.x+E::Px(10),p.y+E::Px(7)});
    ImGui::Text("%zu roots | %s | Esc cancels",session.preview.count,
        session.number.axis>=0 && !RagdollBoneFor(m_HoveredEntity) ? "Type a value or release to commit" : "Release to commit");
    ImGui::SetCursorScreenPos({p.x+E::Px(10),p.y+E::Px(32)});ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width-E::Px(20));
    if(session.invalid)ImGui::PushStyleColor(ImGuiCol_Text,E::Color(0xF08A8A));
    ImGui::TextWrapped("%s",m_PropertyDiagnostic.c_str());if(session.invalid)ImGui::PopStyleColor();ImGui::PopTextWrapPos();
    const auto origin=ImGui::GetMainViewport()->Pos;
    (void)GetInputManager()->AddUIRegion({p.x-origin.x,p.y-origin.y,p.x+width-origin.x,p.y+height-origin.y});
    if(ImGui::IsMouseHoveringRect(p,{p.x+width,p.y+height}))
    {m_ViewportOverlayInteracting=true;m_ViewPickRequested=false;}
    ImGui::SetCursorScreenPos(saved);
}

void RigidBodySimulationApp::QueueTransformEdit(TransformTool tool, const Math::Vec3f& value)
{
    if(!m_HoveredEntity || m_SceneRequest || m_BackendRequest)return;
    if(ProtectsAngularTransform(m_HoveredEntity) || m_HoveredEntity==m_SkyBoxEntity)
    {m_PropertyDiagnostic="Use the selected object's supported Properties controls.";return;}
    if(!TransformEditingAllowed() || m_SimulationState==Rbs::SimulationState::Running) {
        m_PropertyDiagnostic="Return to Edit to author transforms.";return;
    }
    const auto binding=m_ActiveScene->GetBodyBinding(m_HoveredEntity);
    m_TransformEdit=TransformEdit{tool,m_HoveredEntity,binding?*binding:PhysicsBodyHandle{},value,m_SceneGeneration};
    m_PropertyDiagnostic="Transform edit queued for Update.";
}

bool RigidBodySimulationApp::StageRagdollTransform(std::size_t selected,TransformTool tool,const Vec3f& value)
{
    if(!m_Ragdoll.draft || m_SceneRequest || m_BackendRequest ||
       m_EditorMode!=EditorMode::Edit || m_Ragdoll.draftGeneration!=m_SceneGeneration ||
       m_Ragdoll.draftBaseRevision!=m_Ragdoll.revision || RagdollBoneFor(m_HoveredEntity)!=selected ||
       (m_SimulationState!=Rbs::SimulationState::Ready && m_SimulationState!=Rbs::SimulationState::Paused))
        return false;
    auto& packet=*m_Ragdoll.draft;
    const auto& body=packet.bodies[selected];
    if(!Math::IsFinite(value))return false;
    if(tool==TransformTool::Translate)
        Rbs::SetRagdollBodyPose(packet,body.key,value,body.orientation);
    else if(tool==TransformTool::Rotate)
        Rbs::SetRagdollBodyPose(packet,body.key,body.center,glm::quat(value));
    else if(tool==TransformTool::Scale) {
        if(glm::any(glm::lessThanEqual(value,Vec3f{0})))return false;
        if(body.shape==ShapeType::Box)
            Rbs::SetRagdollHalfExtents(packet,body.key,.5f*value);
        else {
            if(std::abs(value.x-value.y)>1.e-5f || std::abs(value.x-value.z)>1.e-5f)return false;
            Rbs::SetRagdollShapeDimensions(packet,body.key,body.shape,body.halfExtents,
                value.x,body.halfLength*(value.x/body.radius));
        }
    }
    else return false;
    m_Ragdoll.dirty=true;
    m_Ragdoll.diagnostic="Transform staged. Review the connected pivots, then Apply or Revert.";
    return true;
}

void RigidBodySimulationApp::ProcessTransformEdit()
{
    ProcessGroupTransform();
    if(m_GridVisibilityEdit)
    {
        Rbs::Editor::Snap.visible=*m_GridVisibilityEdit;
        m_GridVisibilityEdit.reset();
    }
    if(m_GridEntity && m_GridEntity.GetComponent<VisibilityComponent>().enabled!=Rbs::Editor::Snap.visible)
    {
        auto visibility=m_GridEntity.GetComponent<VisibilityComponent>();
        visibility.enabled=Rbs::Editor::Snap.visible;
        m_GridEntity.AddOrReplaceComponent<VisibilityComponent>(visibility);
    }
    if(m_VelocityEdit)
    {
        const auto edit=*m_VelocityEdit;m_VelocityEdit.reset();
        if(edit.generation!=m_SceneGeneration || edit.entity!=m_HoveredEntity || !edit.entity)
            m_PropertyDiagnostic="Selection/world changed; velocity edit cancelled.";
        else if(RagdollBoneFor(edit.entity) || IsAngularEndpoint(edit.entity))
            m_PropertyDiagnostic="Use the connected body's authoring controls for its initial velocity.";
        else if(const auto binding=m_ActiveScene->GetBodyBinding(edit.entity); !binding || *binding!=edit.body)
            m_PropertyDiagnostic="Body identity changed; velocity edit cancelled.";
        else
        {
            auto* world=m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            const auto result=world->SetBodyVelocity(edit.body,edit.linear,edit.angular);
            if(!result)m_PropertyDiagnostic="Velocity rejected; previous state retained.";
            else if(auto actual=world->ReadEffectiveBody(edit.body);!actual)
                m_PropertyDiagnostic="Velocity accepted. Effective backend readback is unavailable.";
            else if(actual->linearVelocity==edit.linear && actual->angularVelocity==edit.angular)
                m_PropertyDiagnostic="Velocity applied and confirmed by the backend.";
            else m_PropertyDiagnostic="Velocity staged for the next Physics step.";
        }
    }
    if(!m_TransformEdit)return;
    CancelGroupTransform();
    const auto command=*m_TransformEdit;m_TransformEdit.reset();
    // Check the generation before dereferencing the borrowed entity/scene.
    if(command.generation!=m_SceneGeneration || command.entity!=m_HoveredEntity || !command.entity)
    {m_PropertyDiagnostic="Selection/world changed; Transform edit cancelled.";return;}
    if(ProtectsAngularTransform(command.entity) || command.entity==m_SkyBoxEntity)
    {m_PropertyDiagnostic="This object requires its own Properties controls.";return;}
    const auto binding=m_ActiveScene->GetBodyBinding(command.entity);
    if(command.body && (!binding || *binding!=command.body))
    {m_PropertyDiagnostic="Body identity changed; Transform edit cancelled.";return;}
    if(command.body) {
        auto* world=m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();const auto state=world->ReadBody(command.body);
        if(!state){m_PropertyDiagnostic="Transform body unavailable.";return;}
        PhysicsPlacementPose candidate{command.body,state->position,state->orientation};
        if(command.tool==TransformTool::Translate)candidate.position=command.value;
        else if(command.tool==TransformTool::Rotate)candidate.rotation=glm::quat(command.value);
        else {candidate.scale=command.value;candidate.referenceScale=command.entity.GetComponent<Transform3DComponent>().Scale;}
        PhysicsPlacementSettings options;options.preserveExistingContacts=true;
        if(auto check=world->QueryEditorPlacement({&candidate,1},options);!check) {
            m_PropertyDiagnostic=std::string(check.error().operation)+" Use a Viewport gizmo for a correction preview.";return;
        }
    }
    const auto result=command.tool==TransformTool::Translate ? m_ActiveScene->SetLocalTranslation(command.entity,command.value) :
        command.tool==TransformTool::Rotate ? m_ActiveScene->SetLocalRotation(command.entity,command.value) :
        m_ActiveScene->SetLocalScale(command.entity,command.value);
    if(!result)
    {m_PropertyDiagnostic="Transform rejected (code "+std::to_string(int(result.error().code))+"); previous state retained.";return;}
    if(command.body && !m_ActiveScene->ReadBodyTransform(command.entity))
    {m_PropertyDiagnostic="Transform body readback failed; simulation paused.";SetSimulationState(Rbs::SimulationState::Paused);return;}
    m_PropertyDiagnostic="Transform applied; authoritative Scene/body readback refreshed.";
}

void RigidBodySimulationApp::DrawViewportOverlay()
{
    namespace E=Rbs::Editor;
    ImGui::BeginDisabled(m_CameraPilot.has_value());
    struct PilotToolbarScope {~PilotToolbarScope(){ImGui::EndDisabled();}} pilotToolbarScope;
    using State=Rbs::SimulationState;
    const auto restore=ImGui::GetCursorScreenPos();
    const auto min=m_ViewportBounds[0],max=m_ViewportBounds[1];
    const float pad=E::Px(4),gap=E::Px(2),hit=E::Px(E::IconHitPixels),margin=E::Px(10);
    const float fullLeft=pad*2+9.3f*hit+6*gap,fullCenter=pad*2+3*hit+3*gap+E::Px(92),rightWidth=pad*2+hit;
    const float height=pad*2+hit;
    const float availableWidth=max.x-min.x;
    const bool compact=availableWidth<fullLeft+fullCenter+rightWidth+4*margin;
    const bool toolMenu=compact && availableWidth<E::Px(430);
    const bool modeInMenu=compact && availableWidth<E::Px(240);
    const float modeWidth=compact?E::Px(44):E::Px(92);
    const float leftWidth=compact?pad*2+(toolMenu?hit:4*hit+3*gap):fullLeft;
    const float centerWidth=compact?pad*2+2*hit+gap+(modeInMenu?0:modeWidth+gap):fullCenter;
    const float centerX=min.x+margin+leftWidth+gap;
    const float rightX=compact?centerX+centerWidth+gap:max.x-margin-rightWidth;
    const float rowY=min.y+margin;
    const auto origin=ImGui::GetMainViewport()->Pos;
    m_ViewportOverlayInteracting=false;
    const auto group=[&](float x,float y,float width)
    {
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled({x,y},{x+width,y+height},ImGui::GetColorU32(E::Color(0x1A1E24,.94f)),E::Px(6));
        draw->AddRect({x,y},{x+width,y+height},ImGui::GetColorU32(E::Color(0x414A56,.65f)),E::Px(6));
        m_ViewportOverlayInteracting|=ImGui::IsMouseHoveringRect({x,y},{x+width,y+height});
        if(auto result=GetInputManager()->AddUIRegion({x-origin.x,y-origin.y,x+width-origin.x,y+height-origin.y});!result)
            Log::GetCoreLogger()->error("Viewport toolbar input region rejected: {}",int(result.error()));
        ImGui::SetCursorScreenPos({x+pad,y+pad});
    };
    int atlasIndex=0;
    for(int i=0;i<4;++i)if(E::Scale>=E::Scales[i])atlasIndex=i;
    const auto button=[&](const char* id,E::Icon icon,bool selected,bool enabled,const char* tip)
    {
        const bool clicked=E::IconButton(id,icon,m_ViewportIconAtlases[atlasIndex],E::AtlasPixels[atlasIndex],selected,enabled,tip);
        const bool pressed=ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::IsMouseClicked(0);
        m_ViewportOverlayInteracting|=ImGui::IsItemActive() || clicked || pressed;
        if(clicked || pressed){SetViewportActive(true,false);m_ViewportRequestFocus=false;}
        return clicked;
    };
    // The image has zero WindowPadding; tooltips/popups need normal panel padding.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{E::Px(10),E::Px(10)});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{gap,gap});
    group(min.x+margin,rowY,leftWidth);
    constexpr const char* names[]{"Select","Translate","Rotate","Scale"};
    constexpr const char* tips[]{"Select objects | Ctrl+click toggles selection",
        "Translate: T, then X/Y/Z and metres. You can also click or drag an arrow.",
        "Rotate: R, then X/Y/Z and degrees. You can also click or drag a ring.",
        "Scale: S and a factor for uniform scale, or S, X/Y/Z and a factor for one axis. Drag for uniform scale."};
    for(int i=0;!toolMenu && i<4;++i)
    {
        if(i)ImGui::SameLine();
        const auto tool=TransformTool(i);const char* reason=TransformToolDisabled(tool);
        const bool clicked=button(names[i],E::Icon(i),m_TransformTool==tool,!reason,reason?reason:tips[i]);
#ifdef GENGINE_PHASE28_QUALIFICATION
        if(i==0)Rbs::Phase28AngularWidget(*this,"outline.select-tool");
#endif
        if(clicked)
            (void)ActivateTransformTool(tool);
    }
    if(toolMenu) {
        if(button("TransformTools",E::Icon(int(m_TransformTool)),true,true,"Select / Translate / Rotate / Scale"))ImGui::OpenPopup("ViewportTransformTools");
        if(ImGui::BeginPopup("ViewportTransformTools")) {
            for(int i=0;i<4;++i){const auto tool=TransformTool(i);const auto* reason=TransformToolDisabled(tool);
                if(ImGui::MenuItem(names[i],nullptr,m_TransformTool==tool,!reason))(void)ActivateTransformTool(tool);
                if(reason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",reason);}
            ImGui::EndPopup();
        }
    }
    const bool editing=TransformEditingAllowed();
    const bool connectedSelection=editing && SelectionProtectsAngularTransform();
    const char* pivotNames[]{"Center","Pivot","Individual Origins"};
    if(!compact) {
    ImGui::SameLine();
    ImGui::BeginDisabled(!editing || connectedSelection);
    if(E::Snap.grid || E::Snap.surface || E::Snap.resolve)ImGui::PushStyleColor(ImGuiCol_Button,E::Color(0x284B70));
    if(ImGui::Button("Snap",{hit*1.5f,hit})) {ImGui::OpenPopup("ViewportSnapOptions");SetViewportActive(true,false);m_ViewportOverlayInteracting=true;}
    if(E::Snap.grid || E::Snap.surface || E::Snap.resolve)ImGui::PopStyleColor();
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Grid / surface snapping and collision-safe placement");
    ImGui::SameLine();
    if(ImGui::Button(E::TransformPivot==2?"Origins":pivotNames[E::TransformPivot],{2*hit,hit})) {ImGui::OpenPopup("TransformPivotOptions");m_ViewportOverlayInteracting=true;}
    E::Tooltip("Center: mean of transform roots. Pivot: active entity origin. Individual: each root rotates/scales about itself.");
    ImGui::SameLine();
    if(ImGui::Button(E::TransformOrientation?"Local":"World",{1.8f*hit,hit})) {CancelGroupTransform();E::TransformOrientation=1-E::TransformOrientation;ImGui::MarkIniSettingsDirty();m_ViewportOverlayInteracting=true;}
    E::Tooltip("World axes / active entity Local axes. Individual Local rotation uses each root's own axes.");
    ImGui::EndDisabled();
    }
    group(centerX,rowY,centerWidth);
    const bool available=!m_CameraPilot && !m_SceneRequest && !m_BackendRequest && m_SimulationState!=State::Switching;
    const bool editMode=m_EditorMode==EditorMode::Edit;
    const auto switchMode=[&] {
        SetEditorMode(editMode?EditorMode::Simulation:EditorMode::Edit);
    };
    if(!modeInMenu) {
        ImGui::BeginDisabled(!available);
        const bool modeClicked=ImGui::Button(editMode?"Edit":compact?"Sim":"Simulation",{modeWidth,hit});
#ifdef GENGINE_PHASE28_QUALIFICATION
        Rbs::Phase28AngularWidget(*this,"mode-toggle");
#endif
        if(modeClicked) {
            switchMode();SetViewportActive(true,false);m_ViewportRequestFocus=false;m_ViewportOverlayInteracting=true;
        }
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",editMode?"Inspect in Simulation mode. Playback stays stopped.":"Return to Edit at the current pose.");
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    const bool running=m_SimulationState==State::Running;
    const bool paused=m_SimulationState==State::Paused;
    const bool canPlay=available && !editMode && (running || paused || m_SimulationState==State::Ready);
    const bool canStop=available && !editMode && (running || paused);
    if(button("StartPauseResume",running?E::Icon::Pause:E::Icon::Play,running,canPlay,
        editMode?"Switch to Simulation mode to play":running?"Pause simulation (P)":paused?"Resume from the current pose (R)":"Start simulation (S)"))
        SetSimulationState(running?State::Paused:State::Running);
    if(!compact) {
        ImGui::SameLine();
        if(button("Stop",E::Icon::Stop,false,canStop,"Stop playback at the current pose"))SetSimulationState(State::Ready);
    }
    ImGui::SameLine();
    if(button("Reset",E::Icon::Reset,false,available,"Reset to the authored starting state"))RequestScene(m_ActiveSelection);
#ifdef GENGINE_PHASE28_QUALIFICATION
    Rbs::Phase28AngularWidget(*this,"general-reset");
#endif
    group(rightX,rowY,rightWidth);
    if(button("DisplayOptions",E::Icon::Options,ImGui::IsPopupOpen("ViewportDisplayOptions"),true,"Viewport options"))ImGui::OpenPopup("ViewportDisplayOptions");
    ImGui::PopStyleVar(); // Icon gaps must not compress menu labels/shortcuts.
    const bool wasPopup=m_ViewportPopupOpen;
    bool openSnap=false;
    m_ViewportPopupOpen=ImGui::BeginPopup("ViewportDisplayOptions");
    if(m_ViewportPopupOpen)
    {
        if(compact) {
            if(modeInMenu && ImGui::MenuItem(editMode?"Switch to Simulation mode":"Return to Edit",nullptr,false,available))switchMode();
            if(ImGui::MenuItem("Stop playback",nullptr,false,canStop))SetSimulationState(State::Ready);
            ImGui::BeginDisabled(!editing || connectedSelection);
            if(ImGui::MenuItem("Snap settings...")){openSnap=true;ImGui::CloseCurrentPopup();}
            if(ImGui::BeginMenu("Transform pivot")){for(int i=0;i<3;++i)if(ImGui::MenuItem(pivotNames[i],nullptr,E::TransformPivot==i)){CancelGroupTransform();E::TransformPivot=i;ImGui::MarkIniSettingsDirty();}ImGui::EndMenu();}
            if(ImGui::MenuItem(E::TransformOrientation?"Use World axes":"Use Local axes")){CancelGroupTransform();E::TransformOrientation=1-E::TransformOrientation;ImGui::MarkIniSettingsDirty();}
            ImGui::EndDisabled();ImGui::Separator();
        }
        if(ImGui::MenuItem("Select Through",nullptr,&E::SelectThrough))ImGui::MarkIniSettingsDirty();
        E::Tooltip("Marquee: select occluded geometry too. Default OFF uses visible raster coverage. Drag over objects or empty space; Shift adds, Ctrl toggles, Esc cancels.");
        bool grid=m_GridVisibilityEdit.value_or(m_GridEntity && m_GridEntity.GetComponent<VisibilityComponent>().enabled);
        if(ImGui::MenuItem("Grid",nullptr,&grid,editing)){m_GridVisibilityEdit=grid;E::Snap.visible=grid;ImGui::MarkIniSettingsDirty();}
        ImGui::MenuItem("Selected collider outlines",nullptr,&m_ShowSelectedColliderOutlines);
        ImGui::MenuItem("Physics debug (all bodies)","Space",&m_IsShowDebugBoundingBox);
        ImGui::MenuItem("Gizmo",nullptr,&m_ShowGizmo,editing && !connectedSelection);
        if(connectedSelection)E::Tooltip("Use Properties to transform the joint endpoints together.");
        if(!editMode)ImGui::MenuItem("Impulse direction arrows",nullptr,&m_Impulse.showArrows);
        bool changed=ImGui::MenuItem("Scene icons",nullptr,&E::SceneIcons);
        ImGui::SetNextItemWidth(E::Px(220));
        changed|=E::Numeric::Scalar("Scene Icon Size",E::SceneIconPercent,1.f,E::Numeric::Bounds<int>{75,200},E::Numeric::Accept{},true,"%d%%");
        if(ImGui::BeginMenu("Spot icon artwork")) {
            const char* names[]{"Studio barrel (B)","Angled fixture (A)","Round housing (C)"};
            for(int i=0;i<3;++i)if(ImGui::MenuItem(names[i],nullptr,E::SpotArtwork==i)){E::SpotArtwork=i;changed=true;}
            ImGui::EndMenu();
        }
        const auto visibility=[&](const char* label,int& value){
            if(ImGui::BeginMenu(label)){const char* names[]{"Off","Selected","All"};
                for(int i=0;i<3;++i)if(ImGui::MenuItem(names[i],nullptr,value==i)){value=i;changed=true;}
                ImGui::EndMenu();}
        };
        visibility("Camera Frustum",E::CameraFrustums);visibility("Light Gizmos",E::LightGizmos);
        if(ImGui::BeginMenu("Spot guides")) {
            changed|=ImGui::MenuItem("Inner Cone",nullptr,&E::SpotInner);
            changed|=ImGui::MenuItem("Outer Cone",nullptr,&E::SpotOuter);
            changed|=ImGui::MenuItem("Range",nullptr,&E::SpotRange);ImGui::EndMenu();
        }
        if(changed)ImGui::MarkIniSettingsDirty();
        ImGui::Separator();
        DrawCameraMenu();
        if(ImGui::BeginMenu("Physics debug layers")){DrawPhysicsDebugSection();ImGui::EndMenu();}
        ImGui::EndPopup();
    }
    if(openSnap)ImGui::OpenPopup("ViewportSnapOptions");
    const bool snapPopup=ImGui::BeginPopup("ViewportSnapOptions");
    if(snapPopup) {
        ImGui::BeginDisabled(!editing);
        bool grid=m_GridVisibilityEdit.value_or(m_GridEntity && m_GridEntity.GetComponent<VisibilityComponent>().enabled);
        if(ImGui::MenuItem("Grid visible",nullptr,&grid)){m_GridVisibilityEdit=grid;E::Snap.visible=grid;ImGui::MarkIniSettingsDirty();}
        bool changed=ImGui::Checkbox("Grid Snap",&E::Snap.grid);
        const char* steps[]{"0.1 m","0.25 m","0.5 m","1 m"};const float values[]{.1f,.25f,.5f,1.f};
        for(int i=0;i<4;++i){if(i)ImGui::SameLine();if(ImGui::SmallButton(steps[i])){E::Snap.increment=values[i];changed=true;}}
        ImGui::SetNextItemWidth(E::Px(160));changed|=E::Numeric::Scalar("Translation step (m)",E::Snap.increment,.01f,E::Numeric::Positive<float>());
        ImGui::Separator();const bool jolt=m_Backend==PhysicsBackend::Jolt;
        ImGui::BeginDisabled(!jolt && !E::Snap.surface);changed|=ImGui::Checkbox("Surface Snap",&E::Snap.surface);ImGui::EndDisabled();
        ImGui::BeginDisabled(!jolt && !E::Snap.align);changed|=ImGui::Checkbox("Align to surface normal",&E::Snap.align);ImGui::EndDisabled();
        ImGui::SetNextItemWidth(E::Px(160));changed|=E::Numeric::Scalar("Surface offset (m)",E::Snap.offset,.01f);
        ImGui::BeginDisabled(!jolt && !E::Snap.resolve);changed|=ImGui::Checkbox("Auto Resolve Penetration",&E::Snap.resolve);ImGui::EndDisabled();
        ImGui::SetNextItemWidth(E::Px(160));changed|=E::Numeric::Scalar("Maximum correction (m)",E::Snap.distance,.1f,E::Numeric::Positive<float>());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+E::Px(300));
        E::Tooltip("Surface contact overrides grid snapping along the normal. Changing a setting cancels the preview.");
        if(!jolt)ImGui::TextWrapped("Native supports grid snapping and clearance checks. Surface Snap and Auto Resolve are unavailable.");
        ImGui::PopTextWrapPos();
        if(changed){CancelGroupTransform();ImGui::MarkIniSettingsDirty();}
        ImGui::EndDisabled();ImGui::EndPopup();
    }
    const bool pivotPopup=ImGui::BeginPopup("TransformPivotOptions");
    if(pivotPopup) {for(int i=0;i<3;++i)if(ImGui::MenuItem(pivotNames[i],nullptr,E::TransformPivot==i)) {
        CancelGroupTransform();E::TransformPivot=i;ImGui::MarkIniSettingsDirty();}ImGui::EndPopup();}
    m_ViewportPopupOpen|=snapPopup||pivotPopup||ImGui::IsPopupOpen("ViewportTransformTools");
    m_ViewportOverlayInteracting|=wasPopup || m_ViewportPopupOpen;
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(restore);
}

bool RigidBodySimulationApp::StageGroupTransform(const Mat4& matrix,float uniformFactor)
{
    auto& session=m_GroupTransform;
    const auto delta=matrix*glm::inverse(session.origin);
    const int axis=session.number.axis;
    const bool axisScale=m_TransformTool==TransformTool::Scale && session.number.editing && axis>=0 && axis<3;
    const bool individualLocal=Rbs::Editor::TransformPivot==2 && Rbs::Editor::TransformOrientation==1;
    auto& proposal=session.proposal;proposal=session.preview;
    for(std::size_t i=0;i<proposal.count;++i)
    {
        Mat4 world=delta*session.world[i];
        if(Rbs::Editor::TransformPivot==2 && m_TransformTool!=TransformTool::Translate) {
            if(axisScale && individualLocal) {
                world=session.world[i];
                world[axis]*=uniformFactor;
            } else if(m_TransformTool==TransformTool::Rotate && Rbs::Editor::TransformOrientation) {
                // Individual Local axes rotate each root about its own orientation.
                const glm::mat3 localRotation=glm::mat3(glm::inverse(session.origin)*matrix);
                const auto& w=session.world[i];const Vec3f lengths{glm::length(Vec3f(w[0])),glm::length(Vec3f(w[1])),glm::length(Vec3f(w[2]))};
                glm::mat3 orientation{Vec3f(w[0])/lengths.x,Vec3f(w[1])/lengths.y,Vec3f(w[2])/lengths.z};
                orientation*=localRotation;for(int a=0;a<3;++a)world[a]=glm::vec4(orientation[a]*lengths[a],0);
            }
            world[3]=session.world[i][3];
        }
        const auto local=glm::inverse(session.parentWorld[i])*world;
        auto& v=proposal.edits[i].after;v.translation=Vec3f(local[3]);
        v.scale={glm::length(Vec3f(local[0])),glm::length(Vec3f(local[1])),glm::length(Vec3f(local[2]))};
        if(!Math::IsFinite(v.translation) || !Math::IsFinite(v.scale) || glm::any(glm::lessThanEqual(v.scale,Vec3f(0)))) {
            session.invalid=true;m_PropertyDiagnostic="The Transform exceeds the supported range or has singular scale.";return false;
        }
        const glm::mat3 basis{Vec3f(local[0])/v.scale.x,Vec3f(local[1])/v.scale.y,Vec3f(local[2])/v.scale.z};
        if(std::abs(glm::dot(basis[0],basis[1]))>1e-4f || std::abs(glm::dot(basis[0],basis[2]))>1e-4f || std::abs(glm::dot(basis[1],basis[2]))>1e-4f)
        {m_PropertyDiagnostic="Transform would introduce shear; previous preview retained.";session.invalid=true;return false;}
        v.rotation=glm::normalize(glm::quat_cast(basis));
        // A world-axis resize can change a different local dimension on a rotated root.
        // The decomposed scale above is usable only after the shear check.
        const auto& before=proposal.edits[i].before;
        if(!axisScale)
            v.scale=before.scale*(m_TransformTool==TransformTool::Scale?uniformFactor:1.f);
        else if(individualLocal || (proposal.count==1 && Rbs::Editor::TransformOrientation==1 &&
                                     m_EditorSelection.count==1)) {
            v.scale=before.scale;
            v.scale[axis]*=uniformFactor;
        }
        if(m_TransformTool==TransformTool::Translate || axisScale)v.rotation=before.rotation;
        if(!Math::IsFinite(v.rotation) || !Math::IsFinite(v.scale) || glm::any(glm::lessThanEqual(v.scale,Vec3f(0)))) {
            session.invalid=true;m_PropertyDiagnostic="The resulting rotation or scale cannot be represented.";return false;
        }
    }
    session.proposedPivot=matrix;session.queryPending=true;session.dirty=true;return true;
}

bool RigidBodySimulationApp::StageGizmoNumber(float value)
{
    auto& session=m_GroupTransform;
    auto matrix=session.dragOrigin;
    const int axis=session.number.axis;
    const bool scale=m_TransformTool==TransformTool::Scale;
    if(scale) {
        if(axis==UniformScaleAxis)for(int i=0;i<3;++i)matrix[i]*=value;
        else if(axis>=0 && axis<3)matrix[axis]*=value;
        else return false;
    } else {
        if(axis<0 || axis>2)return false;
        const auto direction=glm::normalize(Vec3f(session.dragOrigin[axis]));
        if(m_TransformTool==TransformTool::Translate)matrix[3]+=glm::vec4(direction*value,0);
        else {
            const auto rotation=glm::angleAxis(glm::radians(std::remainder(value,360.f)),direction);
            for(int i=0;i<3;++i)matrix[i]=glm::vec4(rotation*Vec3f(session.dragOrigin[i]),0);
        }
    }
    for(int i=0;i<4;++i)if(!Math::IsFinite(matrix[i])) {
        m_PropertyDiagnostic="The requested Transform is outside the representable range.";
        return false;
    }
    // StageGroupTransform applies the original root/parent matrices and the existing pivot policy.
    // Exact typed deltas bypass grid quantization. Explicit collision placement uses the usual query.
    return StageGroupTransform(matrix,scale?value:1.f) && SetNumericPlacement(m_TransformTool,Vec3f(matrix[3]));
}

void RigidBodySimulationApp::DrawGizmoNumber()
{
    namespace E=Rbs::Editor;
    auto& session=m_GroupTransform;
    auto& number=session.number;
    const bool scale=m_TransformTool==TransformTool::Scale;
    const auto& io=ImGui::GetIO();
    if(!number.focus && ImGui::GetCurrentContext()->ActiveId==number.fieldId &&
       !io.KeyCtrl && !io.KeyAlt && !io.KeySuper && !io.KeyShift) {
        const int axis=ImGui::IsKeyPressed(ImGuiKey_X,false)?0:ImGui::IsKeyPressed(ImGuiKey_Y,false)?1:
                       ImGui::IsKeyPressed(ImGuiKey_Z,false)?2:-1;
        if(axis>=0 && axis!=number.axis) {
            number.axis=axis;
            number.staged.reset(); // Re-evaluate the same text against the original pose.
        }
    }
    const float padding=E::Px(7);
    const float width=std::min(E::Px(260),m_ViewportBounds[1].x-m_ViewportBounds[0].x-4*padding);
    if(width<ImGui::GetFrameHeight()){CancelGroupTransform();return;}

    const char* detail=number.error ? number.error : session.invalid ? m_PropertyDiagnostic.c_str() : nullptr;
    const float innerWidth=width-2*padding;
    const float detailHeight=detail ? ImGui::CalcTextSize(detail,nullptr,false,innerWidth).y+ImGui::GetStyle().ItemSpacing.y : 0.f;
    const float height=2*padding+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeight()+detailHeight;
    const ImVec2 pos{m_ViewportBounds[0].x+padding,m_ViewportBounds[1].y-height-padding};
    const ImVec2 end{pos.x+width,pos.y+height};
    const auto origin=ImGui::GetMainViewport()->Pos;
    (void)GetInputManager()->AddUIRegion({pos.x-origin.x,pos.y-origin.y,end.x-origin.x,end.y-origin.y});
    const auto restore=ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(pos,end,ImGui::GetColorU32(E::Color(0x181D25,.96f)),E::Px(5));
    ImGui::SetCursorScreenPos({pos.x+padding,pos.y+padding});
    if(scale && number.axis==UniformScaleAxis)ImGui::TextUnformatted("Uniform scale factor");
    else if(scale)ImGui::Text("Scale %c (factor)",'X'+number.axis);
    else ImGui::Text("%s %c (%s)",m_TransformTool==TransformTool::Translate?"Move":"Rotate",'X'+number.axis,
                     m_TransformTool==TransformTool::Translate?"m":"deg");
    ImGui::SetCursorScreenPos({pos.x+padding,pos.y+padding+ImGui::GetTextLineHeightWithSpacing()});
    ImGui::SetNextItemWidth(innerWidth);
    const bool focusing=std::exchange(number.focus,false);
    // SDL starts text input at input-manager initialization. ImGui discards queued text on
    // programmatic focus, so transfer this frame once, skipping tool/axis letters queued before it.
    if(focusing && !number.text[0] && !number.error && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper) {
        const auto& chars=io.InputQueueCharacters;
        int first=0;
        while(first<chars.Size && !GizmoNumberPrefix(chars[first]))++first;
        const auto count=static_cast<std::size_t>(chars.Size-first);
        if(count>=number.text.size())number.error="The number is too long.";
        else {
            for(std::size_t i=0;i<count;++i) {
                const auto c=chars[first+static_cast<int>(i)];
                number.text[i]=c<128?static_cast<char>(c):'?';
            }
            number.text[count]=0;
        }
    }
    number.fieldId=ImGui::GetID("##GizmoRelativeValue");
    if(focusing)ImGui::GetCurrentContext()->NavActivateInputId=number.fieldId;
    const bool escape=ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    const bool submitted=ImGui::InputTextWithHint("##GizmoRelativeValue",scale?"1.00":"0.00",number.text.data(),number.text.size(),
        ImGuiInputTextFlags_CharsScientific|ImGuiInputTextFlags_EnterReturnsTrue);
    // ImGui defers Enter on the activation frame. A quick complete key sequence also owns that Enter.
    const bool enter=submitted || (focusing && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter,false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter,false)));
    if(focusing && !number.staged && number.text[0] && !number.error) {
        // When typing takes over a drag, keep its first characters and append the next keystroke.
        // A stationary click begins with an empty field, so no default text needs replacing.
        if(auto* state=ImGui::GetInputTextState(number.fieldId)) {
            state->Stb.cursor=state->CurLenW;state->ClearSelection();
        }
    }
    const bool active=ImGui::IsItemActive();
    E::Tooltip("Enter commits one Transform. Escape cancels. Surface Snap and Auto Resolve apply when enabled.");
    if(detail) {
        ImGui::SetCursorScreenPos({pos.x+padding,pos.y+padding+ImGui::GetTextLineHeightWithSpacing()+
            ImGui::GetFrameHeight()+ImGui::GetStyle().ItemSpacing.y});
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+innerWidth);
        ImGui::TextColored(E::Color(0xF08A8A),"%s",detail);ImGui::PopTextWrapPos();
    }
    ImGui::SetCursorScreenPos(restore);
    number.lastDrawFrame=ImGui::GetFrameCount();
    if(escape || (!active && !enter)) {CancelGroupTransform();return;}

    float value{};
    const char* error=E::Numeric::Parse(std::string_view(number.text.data()),value,
        scale ? E::Numeric::Positive<float>() : E::Numeric::Bounds<float>{});
    if(error) {
        if(enter){number.error=error;number.focus=true;}
        return;
    }
    number.error=nullptr;
    const bool neutral=value==(scale?1.f:0.f);
    if(number.staged!=value) {
        number.staged=value;
        if((!neutral || session.dirty) && !StageGizmoNumber(value)) {
            session.queryPending=false;session.invalid=true;
        }
    }
    if(!enter)return;
    if(neutral){CancelGroupTransform();return;}
    if(session.invalid && !session.queryPending){number.focus=true;return;}
    if(ImGui::GetCurrentContext()->ActiveId==number.fieldId)ImGui::ClearActiveID();
    number={};
    m_GizmoCancelled=ImGui::IsMouseDown(0);
    m_DirectTransformReleased=true;
    FinishTransformGesture();
}

void RigidBodySimulationApp::DrawViewportGizmo()
{
    if(m_GroupTransform.preview.kind!=Rbs::Editor::TransformBatch::Kind::Transform)return;
    m_GizmoHovered=m_GizmoUsing=false;
    if(m_GizmoCancelled) {
        ImGuizmo::Enable(false);
        if(ImGui::IsMouseDown(0))return;
        m_GizmoCancelled=false;
    }
    auto& number=m_GroupTransform.number;
    if(number.axis==AwaitKeyboardAxis) {
        const auto& io=ImGui::GetIO();
        if(!m_ViewportOwnsSelection || !m_ViewportActive || !m_ViewportForcused || m_ViewportPopupOpen ||
           io.AppFocusLost || io.WantTextInput || ImGui::GetCurrentContext()->ActiveId || ImGui::IsAnyMouseDown() ||
           GetInputManager()->PointerCapture()!=InputLayer::None || ImGui::GetFrameCount()-number.lastDrawFrame>1)
            number={};
        else number.lastDrawFrame=ImGui::GetFrameCount();
    }
    if(!m_ViewportOwnsSelection || m_ViewportActivationButtons || m_Marquee.armed || m_Marquee.waiting || !m_ShowGizmo || m_TransformTool==TransformTool::Select || !m_CameraSnapshot || TransformToolDisabled(m_TransformTool))
    {
        if(m_GroupTransform.number.editing)CancelGroupTransform();
        ImGuizmo::Enable(false);return;
    }
    if(m_GroupTransform.released)return;
    const auto ragdollBone=RagdollBoneFor(m_HoveredEntity);
    if(ragdollBone && !m_GroupTransform.dirty && !ImGuizmo::IsUsing() && m_GroupTransform.preview.count) {
        const auto& body=m_Ragdoll.draft->bodies[*ragdollBone];
        const auto& previous=m_GroupTransform.preview.edits[0].before;
        if(previous.translation!=body.center+Vec3f{0,Rbs::SandboxEnvironment::FloorTop+m_Ragdoll.draft->dropOffset,0} ||
           previous.rotation!=body.orientation || previous.scale!=Rbs::RagdollRenderScale(body))CancelGroupTransform();
    }
    if(!m_GroupTransform.dirty && !ImGuizmo::IsUsing() && m_GroupTransform.previewTick!=m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps())CancelGroupTransform();
    if(!BeginGroupTransform())return;
    auto& session=m_GroupTransform;
    auto matrix=number.editing?session.pivot:session.gizmo;
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList()); // Existing Viewport; no helper window.
    ImGuizmo::SetID(int(static_cast<entt::entity>(m_HoveredEntity)));
    ImGuizmo::SetRect(m_ViewportBounds[0].x,m_ViewportBounds[0].y,
        m_ViewportBounds[1].x-m_ViewportBounds[0].x,m_ViewportBounds[1].y-m_ViewportBounds[0].y);
    ImGuizmo::SetOrthographic(m_CameraSnapshot->Description().projection==CameraProjection::Orthographic);
    // Scale the screen-space gizmo to the selected UI scale, not the camera distance.
    ImGuizmo::SetGizmoSizeClipSpace(.12f*Rbs::Editor::Scale);
    const auto& io=ImGui::GetIO();
    if(number.editing && (!m_ViewportForcused || io.AppFocusLost || m_ViewportPopupOpen ||
        GetInputManager()->PointerCapture()==InputLayer::View)) {
        CancelGroupTransform();ImGuizmo::Enable(false);return;
    }
    const bool enabled=!number.editing && !m_NumericTransform && !io.KeyAlt && !ImGui::GetCurrentContext()->DragDropActive && !m_ViewportPopupOpen &&
        GetInputManager()->PointerCapture()!=InputLayer::View && (ImGuizmo::IsUsing() || !m_ViewportOverlayInteracting);
    ImGuizmo::Enable(enabled);
    const auto operation=m_TransformTool==TransformTool::Translate?ImGuizmo::TRANSLATE:
        m_TransformTool==TransformTool::Rotate?ImGuizmo::ROTATE:ImGuizmo::SCALE;
    const bool wasUsing=ImGuizmo::IsUsing();
    const bool changed=ImGuizmo::Manipulate(glm::value_ptr(m_CameraSnapshot->View()),
        glm::value_ptr(m_CameraSnapshot->Projection()),operation,Rbs::Editor::TransformOrientation?ImGuizmo::LOCAL:ImGuizmo::WORLD,glm::value_ptr(matrix));
    m_GizmoHovered=enabled&&ImGuizmo::IsOver();m_GizmoUsing=enabled&&ImGuizmo::IsUsing();
    if(number.editing) {
        m_GizmoUsing=true;m_ViewPickRequested=false;
        return; // The passive gizmo is drawn at the last validated preview.
    }
    if(m_GizmoUsing && !wasUsing) {
        session.dragOrigin=session.pivot;
        number.axis=static_cast<int>(ImGuizmo::GetActiveAxis());
    }
    if(m_GizmoUsing){SetViewportActive(true,false);m_ViewportRequestFocus=false;}
    if(m_GizmoHovered || m_GizmoUsing)
    {
        // Precise ImGuizmo hit testing reserves only the current pointer region;
        // it never reserves the object's projected rectangle or the whole image.
        const auto origin=ImGui::GetMainViewport()->Pos;
        const float x=io.MousePos.x-origin.x,y=io.MousePos.y-origin.y;
        if(auto result=GetInputManager()->AddUIRegion({x-4,y-4,x+4,y+4});!result)
            Log::GetCoreLogger()->error("Gizmo input region rejected: {}",int(result.error()));
        m_ViewPickRequested=false;
    }
    if(enabled && !ragdollBone && number.axis>=0 && (m_GizmoUsing || wasUsing)) {
        const bool click=wasUsing && !m_GizmoUsing && io.MouseReleased[0] && io.MouseDragMaxDistanceSqr[0]==0.f &&
            io.MousePos.x==io.MouseClickedPos[0].x && io.MousePos.y==io.MouseClickedPos[0].y;
        const auto& chars=io.InputQueueCharacters;
        const auto first=chars.empty()?0:chars[0];
        const bool typing=!io.KeyCtrl && !io.KeyAlt && !io.KeySuper && GizmoNumberPrefix(first);
        if(click || typing) {
            number.editing=number.focus=true;
            number.lastDrawFrame=ImGui::GetFrameCount();
            ImGuizmo::Enable(false);
            m_GizmoUsing=true;m_ViewPickRequested=m_OutlineToggleRequested=false;
            return; // The mouse release can no longer submit this gesture.
        }
    }
    if(wasUsing && !m_GizmoUsing)FinishTransformGesture();
    if(!changed){if(!m_GizmoUsing)session.gizmo=session.pivot;return;}
    session.gizmo=matrix;
    float uniformFactor=1.f;
    if(m_TransformTool==TransformTool::Scale)
    {
        // This ImGuizmo version has no uniform-only operation. Any scale handle
        // controls one uniform factor, avoiding shear for differently rotated roots.
        // Keep ImGuizmo's raw axis matrix separate from the uniform presentation.
        // Ratios are relative to this gesture's starting matrix, not the previous
        // frame's already-uniform result (which otherwise oscillates back to 1).
        Vec3f old{glm::length(Vec3f(session.dragOrigin[0])),glm::length(Vec3f(session.dragOrigin[1])),glm::length(Vec3f(session.dragOrigin[2]))};
        Vec3f ratio{glm::length(Vec3f(matrix[0]))/old.x,glm::length(Vec3f(matrix[1]))/old.y,glm::length(Vec3f(matrix[2]))/old.z};
        int axis=0;for(int i=1;i<3;++i)if(std::abs(ratio[i]-1)>std::abs(ratio[axis]-1))axis=i;
        uniformFactor=ratio[axis];matrix=session.dragOrigin;for(int i=0;i<3;++i)matrix[i]*=uniformFactor;
    }
    if(!ragdollBone && m_TransformTool==TransformTool::Translate && Rbs::Editor::Snap.grid) {
        const auto snapped=Rbs::Editor::SnapGrid(Vec3f(matrix[3]),Rbs::Editor::Snap.increment);
        if(!snapped){m_PropertyDiagnostic="Grid result is not representable; candidate rejected.";session.invalid=true;return;}
        matrix[3]=glm::vec4(*snapped,1);
    }
    if(!StageGroupTransform(matrix,uniformFactor))return;
    if(ragdollBone)return;
    session.placement={};session.placement.pivot=Vec3f(matrix[3]);
    const auto& snap=Rbs::Editor::Snap;
    session.placement.surface=snap.surface && m_Backend==PhysicsBackend::Jolt;session.placement.alignNormal=session.placement.surface&&snap.align;
    session.placement.resolve=snap.resolve && m_Backend==PhysicsBackend::Jolt;session.placement.offset=snap.offset;session.placement.maxCorrection=snap.distance;
    if(session.placement.surface) {
        const auto& viewport=m_CameraSnapshot->Description().viewport;
        const auto pixel=glm::dvec2{(io.MousePos.x-m_ViewportBounds[0].x)*viewport.pixelSize.x/(m_ViewportBounds[1].x-m_ViewportBounds[0].x),
            (io.MousePos.y-m_ViewportBounds[0].y)*viewport.pixelSize.y/(m_ViewportBounds[1].y-m_ViewportBounds[0].y)};
        const auto ray=m_CameraSnapshot->PixelToRay(pixel);
        if(!ray){m_PropertyDiagnostic="Surface ray outside the Viewport; candidate rejected.";session.invalid=true;return;}
        session.placement.rayOrigin=Vec3f(ray->origin+ray->direction*ray->tNear);
        session.placement.rayDelta=Vec3f(ray->direction*(ray->tFar-ray->tNear));
    }

}

void RigidBodySimulationApp::DrawComponentOverlays()
{
    namespace E=Rbs::Editor;
    m_VisibleSceneIconCount=0;
    m_OutlineHoverIcon={};
    if(!m_CameraSnapshot)return;
    const auto lo=m_ViewportBounds[0],hi=m_ViewportBounds[1];
    const auto vp=m_CameraSnapshot->Projection()*m_CameraSnapshot->View();
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect({lo.x,lo.y},{hi.x,hi.y},true);
    const auto project=[&](const Vec3f& p)->std::optional<ImVec2>
    {
        const auto clip=vp*glm::vec4(p,1);if(clip.w<=.001f || clip.z < -clip.w || clip.z > clip.w)return {};
        const auto ndc=Vec3f(clip)/clip.w;
        return ImVec2{lo.x+(ndc.x*.5f+.5f)*(hi.x-lo.x),lo.y+(.5f-ndc.y*.5f)*(hi.y-lo.y)};
    };
    const auto line=[&](const Vec3f& a,const Vec3f& b,ImU32 color)
    {
        auto p=vp*glm::vec4(a,1),q=vp*glm::vec4(b,1);
        // Clip guides in homogeneous space, including segments crossing the eye
        // or near plane. Avoid dropping a visible frustum edge with one hidden end.
        for(int axis=0;axis<3;++axis)for(float sign:{-1.f,1.f})
        {
            const float x=p.w+sign*p[axis],y=q.w+sign*q[axis];
            if(x<0 && y<0)return;
            if((x<0)!=(y<0)){const auto cut=p+(q-p)*(x/(x-y));if(x<0)p=cut;else q=cut;}
        }
        if(p.w<=.001f || q.w<=.001f)return;
        const auto pixel=[&](const glm::vec4& v){return ImVec2{lo.x+(v.x/v.w*.5f+.5f)*(hi.x-lo.x),lo.y+(.5f-v.y/v.w*.5f)*(hi.y-lo.y)};};
        draw->AddLine(pixel(p),pixel(q),color,E::Px(1));
    };
    if(m_Ragdoll.draft && m_Ragdoll.debugEnabled) {
        auto& world=*m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
        for(std::size_t i=0;i<m_RagdollBindings.mapping.joints.size();++i) {
            const auto joint=world.ReadJoint(m_RagdollBindings.mapping.joints[i]);
            if(!joint)continue;
            const auto pixel=project(joint->worldAnchorA);
            const auto& d=joint->descriptor;
            if(m_Ragdoll.showAnchors) {
                if(pixel)draw->AddCircle(*pixel,E::Px(4.f),IM_COL32(30,214,245,255));
                if(const auto b=project(joint->worldAnchorB))draw->AddCircle(*b,E::Px(2.f),IM_COL32(254,170,46,255));
            }
            if(m_Ragdoll.showAxes) {
                for(bool first:{true,false}) {
                    const auto body=world.ReadEffectiveBody(first?d.bodyA:d.bodyB);
                    if(!body)continue;
                    const auto frame=body->orientation*(first?d.angular.localFrameA:d.angular.localFrameB);
                    const auto start=first?joint->worldAnchorA:joint->worldAnchorB;
                    const auto length=first?.4f:.25f;
                    line(start,start+frame*Vec3f{length,0,0},IM_COL32(240,78,72,240));
                    line(start,start+frame*Vec3f{0,length,0},IM_COL32(70,218,112,240));
                    line(start,start+frame*Vec3f{0,0,length},IM_COL32(89,157,250,240));
                }
            }
            if(m_Ragdoll.showErrors) {
                line(joint->worldAnchorA,joint->worldAnchorB,IM_COL32(255,86,179,255));
                if(pixel)draw->AddText({pixel->x+E::Px(8),pixel->y},IM_COL32(255,169,216,255),
                    std::format("{}: {:.3g} m",RagdollName(static_cast<RagdollJoint>(i)),joint->error).c_str());
            }
            if(m_Ragdoll.showLimits && pixel) {
                const bool hinge=d.kind==JointKind::Hinge;
                const auto label=!d.angular.limitsEnabled?std::string("Free"):
                    E::DecimalText("{} {:.2f} / {:.2f} deg",hinge?"Hinge":"CV U/V",
                        glm::degrees(hinge?d.angular.lower:d.angular.halfAngleU),
                        glm::degrees(hinge?d.angular.upper:d.angular.halfAngleV));
                draw->AddText({pixel->x+E::Px(8),pixel->y+ImGui::GetTextLineHeight()},IM_COL32(255,199,100,255),label.c_str());
            }
        }
        if(m_Ragdoll.showContacts)
            draw->AddText({lo.x+E::Px(8),hi.y-ImGui::GetTextLineHeight()*2},IM_COL32(245,231,177,255),
                m_Ragdoll.contactDiagnostic.empty()?"Contact geometry: awaiting measurement":m_Ragdoll.contactDiagnostic.c_str());
    }
    const auto saved=ImGui::GetCursorScreenPos();
    struct Hit {EntityRenderId id;ImVec2 lo,hi,center;float radius;};std::array<Hit,64> hits{};std::size_t hitCount{};
    const auto outline=[&](ImVec2 center,float radius,bool selected)
    {
        // ImGui tessellates these screen-space strokes; no implementation-defined
        // GL wide lines. Keep the same gold as the existing mesh selection pass.
        // Draw-data framebuffer scaling applies once; UI/font scale changes the
        // icon radius, not the logical selection stroke's apparent thickness.
        const float width=selected?2.2f:1.f;
        draw->AddCircle(center,radius,IM_COL32(12,15,20,selected?180:90),48,width+1.5f);
        draw->AddCircle(center,radius,ImGui::GetColorU32(ImVec4{1.f,.72f,.08f,selected?1.f:.5f}),48,width);
    };
    const bool allowHover = m_ViewportActive && !m_ViewportAwaitPointer && !m_ViewportActivationButtons &&
        GetInputManager()->PointerCapture() != InputLayer::View && !m_Marquee.armed && !m_Marquee.waiting &&
        !m_GizmoHovered && !m_GizmoUsing && !ImGui::GetIO().KeyAlt && !ImGui::GetCurrentContext()->DragDropActive;
    const auto render=[&](auto tag)
    {
        using T=decltype(tag);
        for(auto e:m_ActiveScene->GetAllEntitiesWith<T,Transform3DComponent>())
        {
            const _Entity entity{e,m_ActiveScene.get()};const auto identity=m_ActiveScene->RenderData().Identify(entity);
            if(!identity || !EditorEntityVisible(*identity))continue;
            const auto world=GizmoWorldMatrix(*m_ActiveScene,entity);if(!world)continue;
            const auto pos=Vec3f((*world)[3]);const auto screen=project(pos);
            const bool selected=m_ViewportOwnsSelection && m_EditorSelection.Contains(*identity);
            const auto color=selected?IM_COL32(116,183,244,230):IM_COL32(173,187,204,200);
            const int mode=std::same_as<T,RenderCameraComponent>?E::CameraFrustums:E::LightGizmos;
            if(mode==2 || (mode==1 && selected))
            {
                if constexpr(std::same_as<T,RenderCameraComponent>)
                {
                    const auto& c=entity.GetComponent<T>();
                    if(!Rbs::Editor::ValidateCamera(c))
                    {
                        // Same rigid world basis and projection contract as SnapshotSceneCamera;
                        // no repeated Scene traversal/publication from the overlay pass.
                        const glm::dmat3 basis(*world);const auto residual=glm::transpose(basis)*basis-glm::dmat3(1);
                        double error=std::abs(glm::determinant(basis)-1.);
                        for(int x=0;x<3;++x)for(int y=0;y<3;++y)error=std::max(error,std::abs(residual[x][y]));
                        CameraSnapshotDesc desc;desc.position=pos;desc.orientation=glm::normalize(glm::quat_cast(basis));
                        desc.projection=c.projection;desc.verticalFovRadians=c.verticalFovRadians;desc.orthographicHeight=c.orthographicHeight;
                        desc.nearPlane=c.nearPlane;desc.farPlane=c.farPlane;desc.viewport=m_CameraSnapshot->Description().viewport;
                        auto snapshot=CameraSnapshot::Create(desc);
                        if(snapshot && error<=2e-4) {
                            std::array<Vec3f,8> corners;
                            bool valid=true;
                            for(int i=0;i<8;++i){const auto point=snapshot->NdcToWorld({(i&1)?1.:-1.,(i&2)?1.:-1.,(i&4)?1.:-1.});
                                if(point)corners[i]=Vec3f(*point);else valid=false;}
                            if(valid)for(int i=0;i<8;++i)for(int bit:{1,2,4})if(!(i&bit))line(corners[i],corners[i|bit],color);
                        }
                    }
                }
                else
                {
                    const auto& l=entity.GetComponent<T>();
                    const auto direction=glm::normalize(Vec3f(*world*glm::vec4(0,0,-1,0)));
                    if(l.kind==RenderLightKind::Directional) {
                        const auto right=glm::normalize(glm::cross(direction,std::abs(direction.y)>.99f?Vec3f(1,0,0):Vec3f(0,1,0)));
                        for(int i=-1;i<=1;++i){const auto start=pos+right*float(i),tip=start+direction*6.f;
                            line(start,tip,color);line(tip,tip-direction+right*.4f,color);line(tip,tip-direction-right*.4f,color);}
                    }
                    else if(l.kind==RenderLightKind::Point)
                    {
                        for(int axis=0;axis<3;++axis)for(int i=0;i<48;++i)
                        {
                            Vec3f a{},b{};const float x=float(i)*glm::two_pi<float>()/48,y=float(i+1)*glm::two_pi<float>()/48;
                            a[(axis+1)%3]=std::cos(x)*l.range;a[(axis+2)%3]=std::sin(x)*l.range;
                            b[(axis+1)%3]=std::cos(y)*l.range;b[(axis+2)%3]=std::sin(y)*l.range;line(pos+a,pos+b,color);
                        }
                    }
                    else
                    {
                        if(const auto frame=E::MakeSpotGuideFrame(*world))
                            E::EmitSpotGuide(*frame,l.range,l.innerConeRadians,l.outerConeRadians,
                                {E::SpotInner,E::SpotOuter,E::SpotRange},
                                [&](const Vec3f& a,const Vec3f& b,E::SpotGuidePart part) {
                                    line(a,b,part==E::SpotGuidePart::Inner?IM_COL32(226,191,107,160):color);
                                });
                    }
                }
            }
            if(!screen || !E::SceneIcons)continue;
            const bool spotIcon=entity.HasAllComponents<RenderLightComponent>() && entity.GetComponent<RenderLightComponent>().kind==RenderLightKind::Spot;
            const float hit=E::Px(spotIcon?44.f:40.f)*E::SceneIconPercent*.01f;const ImVec2 p{std::round(screen->x-hit*.5f),std::round(screen->y-hit*.5f)};
            if(p.x+hit<=lo.x||p.x>=hi.x||p.y<lo.y+E::Px(60)||p.y>=hi.y-E::Px(m_GroupTransform.dirty?90.f:0.f))continue;
            auto icon=E::Icon::Camera;
            if constexpr(std::same_as<T,RenderLightComponent>) {
                const auto kind=entity.GetComponent<T>().kind;
                icon=kind==RenderLightKind::Directional?E::Icon::Sun:kind==RenderLightKind::Spot?E::Icon::Spot:E::Icon::Light;
            }
            if(icon==E::Icon::Spot && E::SpotArtwork)icon=E::SpotArtwork==1?E::Icon::SpotA:E::Icon::SpotC;
            if(selected)outline(*screen,hit*.56f,true);
            int atlas=0;while(atlas+1<int(std::size(E::BillboardPixels)) && E::BillboardPixels[atlas]<hit)++atlas;
            E::DrawIcon(icon,m_ComponentIconAtlases[atlas],E::BillboardPixels[atlas],{p.x+E::Px(1),p.y+E::Px(1)},hit,E::Color(0x111820,.8f));
            E::DrawIcon(icon,m_ComponentIconAtlases[atlas],E::BillboardPixels[atlas],p,hit,
                std::same_as<T,RenderCameraComponent>?E::Color(0xD7E2EF):E::Color(0xE6C477));
            if(m_VisibleSceneIconCount<m_VisibleSceneIcons.size())m_VisibleSceneIcons[m_VisibleSceneIconCount++]={*identity,{std::max(p.x,lo.x),std::max(p.y,lo.y)},{std::min(p.x+hit,hi.x),std::min(p.y+hit,hi.y)}};
            if(!allowHover)continue;
            const ImVec2 hitLo{std::max(p.x,lo.x),std::max(p.y,lo.y)},hitHi{std::min(p.x+hit,hi.x),std::min(p.y+hit,hi.y)};
            if(ImGui::IsMouseHoveringRect(hitLo,hitHi) && hitCount<hits.size())hits[hitCount++]={*identity,hitLo,hitHi,*screen,hit*.56f};
        }
    };
    render(RenderCameraComponent{});render(RenderLightComponent{});
    if(hitCount) {
        std::size_t pick=hitCount-1;
        if(hitCount>1 && m_ViewportOwnsSelection)for(std::size_t i=0;i<hitCount;++i)if(m_EditorSelection.count && m_EditorSelection.ids[m_EditorSelection.count-1]==hits[i].id){pick=(i+1)%hitCount;break;}
        if(auto e=m_ActiveScene->RenderData().Resolve(hits[pick].id)) {
            _Entity entity{*e,m_ActiveScene.get()};ImGui::SetTooltip("%s | Scene icon%s",entity.Name().c_str(),hitCount>1?" | click to cycle overlapping icons":"");
            if(!m_ViewportOverlayInteracting && ImGui::IsWindowHovered() && !ImGui::GetCurrentContext()->OpenPopupStack.Size) {
                m_OutlineHoverIcon=hits[pick].id;
                if(!m_ViewportOwnsSelection || !m_EditorSelection.Contains(hits[pick].id))outline(hits[pick].center,hits[pick].radius,false);
            }
        }
    }
    ImGui::SetCursorScreenPos(saved);draw->PopClipRect();
}

bool RigidBodySimulationApp::PickViewportIcon(float x, float y, bool toggle)
{
    if (!m_VisibleSceneIconCount || !m_ActiveScene) return false;
    const auto origin = ImGui::GetMainViewport()->Pos;
    x += origin.x;
    y += origin.y;
    const auto primary = m_ViewportOwnsSelection && m_EditorSelection.count ?
        m_EditorSelection.ids.back() : EntityRenderId{};
    EntityRenderId first{}, last{}, next{};
    bool foundPrimary = false;
    for (std::size_t i = 0; i < m_VisibleSceneIconCount; ++i) {
        const auto& icon = m_VisibleSceneIcons[i];
        if (x < icon.lo.x || x >= icon.hi.x || y < icon.lo.y || y >= icon.hi.y) continue;
        if (!first) first = icon.id;
        if (foundPrimary && !next) next = icon.id;
        if (icon.id == primary) foundPrimary = true;
        last = icon.id;
    }
    if (!first) return false;
    const auto hit = foundPrimary ? (next ? next : first) : last;
    if (auto entity = m_ActiveScene->RenderData().Resolve(hit)) {
        if (!HasPendingObjectProperties()) {
            SelectColliderEntity(_Entity{*entity,m_ActiveScene.get()}, toggle);
            m_ViewportOwnsSelection = m_EditorSelection.count != 0;
        }
    }
    return true;
}

// UI owns the gesture; immutable frame submission owns raster queries. Neither
// path touches Transform or Physics. A cancelled gesture never publishes selection.
void RigidBodySimulationApp::CancelViewportMarquee()
{
    m_Marquee.armed=m_Marquee.dragging=m_Marquee.queued=m_Marquee.waiting=false;
    m_Marquee.clicked={};
    m_Marquee.icons.clear();
    if(m_FrameSubmission)m_FrameSubmission->CancelRectangleSelection();
}
void RigidBodySimulationApp::ArmViewportMarquee(unsigned x,unsigned y,EntityRenderId clicked)
{
    CancelViewportMarquee();if(!m_CameraSnapshot)return;
    const auto size=m_CameraSnapshot->Description().viewport.pixelSize;
    auto& m=m_Marquee;m.armed=true;m.generation=m_SceneGeneration;m.serial=++m_MarqueeSerial;
    m.clicked=clicked; // Defer the click until release; Esc/drag retain the prior set.
    m.viewportLo=m_ViewportBounds[0];m.viewportHi=m_ViewportBounds[1];
    const auto extent=m.viewportHi-m.viewportLo;
    m.start=m.end=m.viewportLo+Vec2f{(float(x)+.5f)/size.x,1.f-(float(y)+.5f)/size.y}*extent;
    m.add=m_ViewSelectionAdd;m.toggle=m_FrameOutlineToggle;m.through=Rbs::Editor::SelectThrough;
}
void RigidBodySimulationApp::CommitViewportClick()
{
    const auto clicked=m_Marquee.clicked;
    const auto generation=m_Marquee.generation;
    const bool add=m_Marquee.add,toggle=m_Marquee.toggle;
    CancelViewportMarquee();
    if(!m_ViewportActive || generation!=m_SceneGeneration || !m_ActiveScene)return;
    if(HasPendingObjectProperties()) {
        m_PropertyDiagnostic="Apply or Revert the selected object's pending changes before changing selection.";
        return;
    }
    if(clicked) {
        auto& render=m_ActiveScene->RenderData();
        if(!render.Resolve(clicked) || !EditorEntityVisible(clicked))return;
        using Operation=Rbs::Editor::EntitySelection::SetOperation;
        if(m_EditorSelection.SelectSet(render,std::span(&clicked,1),toggle?Operation::Toggle:add?Operation::Add:Operation::Replace)) {
            m_EditorSelection.anchor=clicked;
            SynchronizeEditorSelection();
            m_ViewportOwnsSelection = m_EditorSelection.count != 0;
        }
    } else if(!add && !toggle) {
        m_EditorSelection.Clear();m_EditorSelection.anchor={};SynchronizeEditorSelection();m_HierarchyRevealed={};
        m_ViewportOwnsSelection = false;
    }
}
void RigidBodySimulationApp::DrawViewportMarquee()
{
    namespace E=Rbs::Editor;auto& m=m_Marquee;if(!m.armed && !m.waiting)return;
    auto& io=ImGui::GetIO();
    if(m.generation!=m_SceneGeneration || m.viewportLo!=m_ViewportBounds[0] || m.viewportHi!=m_ViewportBounds[1] ||
       !m_ViewportForcused || m_GizmoUsing || m_GizmoHovered || m_ViewportOverlayInteracting || io.KeyAlt || io.MouseDown[1] ||
       io.WantTextInput || m_RenameTarget || ImGui::GetTopMostPopupModal() || ImGui::GetCurrentContext()->DragDropActive ||
       ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {CancelViewportMarquee();return;}
    if(m.waiting) {
        if(HasPendingObjectProperties()) {
            m_PropertyDiagnostic="Apply or Revert the selected object's pending changes before changing selection.";
            CancelViewportMarquee();return;
        }
        const auto result=m_FrameSubmission->PollRectangleSelection();
        if(!result){m_PropertyDiagnostic="Marquee GPU query failed; selection retained.";CancelViewportMarquee();return;}
        if(*result && (*result)->serial==m.serial) {
            auto ids=std::move((*result)->entities);
            for(auto id:m.icons)if(std::find(ids.begin(),ids.end(),id)==ids.end())ids.push_back(id);
            const auto operation=m.toggle?E::EntitySelection::SetOperation::Toggle:m.add?E::EntitySelection::SetOperation::Add:E::EntitySelection::SetOperation::Replace;
            // A replacement scene, deleted entity or visibility change invalidates
            // the complete result; never select a partial/stale asynchronous batch.
            for(auto id:ids)if(!m_ActiveScene->RenderData().Resolve(id) || !EditorEntityVisible(id)) {
                m_PropertyDiagnostic="Marquee result became stale; selection retained.";CancelViewportMarquee();return;
            }
            const auto draws=(*result)->draws;
            if(m_EditorSelection.SelectSet(m_ActiveScene->RenderData(),ids,operation)) {
                SynchronizeEditorSelection();m_HierarchyRevealed=m_ActiveScene->RenderData().Identify(m_HoveredEntity).value_or(EntityRenderId{});
                m_ViewportOwnsSelection = m_EditorSelection.count != 0;
                m_PropertyDiagnostic="Marquee: "+std::to_string(m_EditorSelection.count)+" selected; "+std::to_string(ids.size())+" intersecting entities.";
                Log::GetCoreLogger()->info("MARQUEE_RESULT selected={} hits={} query_draws={} physics_tick={}",m_EditorSelection.count,ids.size(),draws,m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps());
            } else m_PropertyDiagnostic="Marquee exceeds 4096 selections or contains stale entities; selection retained.";
            CancelViewportMarquee();return;
        }
    } else {
        m.end=glm::clamp(Vec2f{io.MousePos.x,io.MousePos.y},m.viewportLo,m.viewportHi);
        m.dragging|=glm::length(m.end-m.start)>=E::Px(5);
        if(!io.MouseDown[0]) {
            if(!m.dragging) {
                CommitViewportClick();return;
            }
            const auto lo=glm::min(m.start,m.end),hi=glm::max(m.start,m.end);
            const auto extent=m.viewportHi-m.viewportLo;const auto size=m_CameraSnapshot->Description().viewport.pixelSize;
            const auto a=(lo-m.viewportLo)/extent,b=(hi-m.viewportLo)/extent;
            const auto x=unsigned(std::floor(a.x*size.x)),y=unsigned(std::floor((1-b.y)*size.y));
            const auto right=std::min(size.x,unsigned(std::ceil(b.x*size.x))),top=std::min(size.y,unsigned(std::ceil((1-a.y)*size.y)));
            if(right<=x || top<=y){CancelViewportMarquee();return;}
            m.request={m.serial,{x,y,right-x,top-y},m.through};
            m.icons.clear();for(std::size_t i=0;i<m_VisibleSceneIconCount;++i) {
                const auto& icon=m_VisibleSceneIcons[i];if(icon.hi.x>lo.x && icon.lo.x<hi.x && icon.hi.y>lo.y && icon.lo.y<hi.y)m.icons.push_back(icon.id);
            }
            m.armed=false;m.queued=m.waiting=true;
        }
    }
    if(m.dragging) {
        const auto lo=glm::min(m.start,m.end),hi=glm::max(m.start,m.end);
        auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect({m.viewportLo.x,m.viewportLo.y},{m.viewportHi.x,m.viewportHi.y},true);
        draw->AddRectFilled({lo.x,lo.y},{hi.x,hi.y},IM_COL32(69,143,222,35));
        draw->AddRect({lo.x,lo.y},{hi.x,hi.y},IM_COL32(100,174,250,230),0,0,E::Px(1));
        draw->AddText({lo.x+E::Px(5),lo.y+E::Px(5)},IM_COL32(225,235,255,255),m.waiting?"Selecting...":m.through?"Select Through":"Visible only");
        draw->PopClipRect();
    }
}
