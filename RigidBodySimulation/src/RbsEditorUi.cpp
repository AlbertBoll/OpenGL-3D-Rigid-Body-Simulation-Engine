#include "RigidBodySimulation.h"
#include "RbsEditorWidgets.h"
#include "RbsEditorIcons.h"
#include "RbsEditorLayout.h"
#include "RbsScenePresetPayload.h"
#include "RbsComponentEditing.h"
#include "Managers/AssetsManager.h"
#include "UI/UICore.h"
#include "Scene/_Scene.h"
#include "Renderer/SceneRenderResources.h"
#include "../tests/BackendSwitchChecks.h"
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
#include "../tests/SandboxChecks.h"
#endif
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <fstream>
#include <string_view>
#include <sdl2/SDL_stdinc.h>

using namespace ::GEngine;
using namespace ::GEngine::Component;

namespace
{
    const char* SimulationLabel(Rbs::SimulationState state)
    {
        switch(state)
        {
        case Rbs::SimulationState::Ready:return "Ready";
        case Rbs::SimulationState::Running:return "Running";
        case Rbs::SimulationState::Paused:return "Paused";
        case Rbs::SimulationState::Switching:return "Switching";
        default:return "Failed";
        }
    }

    // Copy compatible saved docking preferences once. Never rewrite the old record.
    bool MigrateDock(const char* from,const char* to)
    {
        if(ImGui::FindWindowSettings(ImHashStr(to)))return true;
        const auto* old=ImGui::FindWindowSettings(ImHashStr(from));
        if(!old)return false;
        const auto saved=*old; // Creating a settings entry may relocate the buffer.
        auto* next=ImGui::CreateNewWindowSettings(to);
        const auto id=next->ID;
        *next=saved;next->ID=id;next->WantApply=true;
        return true;
    }
}

void RigidBodySimulationApp::PrepareEditorLayout()
{
    if(m_EditorLayoutPrepared && !m_ResetEditorLayout)return;
    // Run before the existing host submits its DockSpace, never during its traversal.
    const auto dock=ImHashStr("MyDockSpace",0,ImHashStr("DockSpace Demo"));
    if(Rbs::Editor::LayoutVersion<Rbs::Editor::CurrentLayoutVersion)
    {
        if(Rbs::Editor::IsRecognizedLegacyDefault(dock))
        {
            m_ResetEditorLayout=true;
            Log::GetCoreLogger()->info("RBS_LAYOUT_MIGRATED recognized_default=legacy-18-22-23 version=4");
        }
        Rbs::Editor::LayoutVersion=Rbs::Editor::CurrentLayoutVersion;
        ImGui::MarkIniSettingsDirty();
    }
    if(!m_ResetEditorLayout)
    {
        const bool hierarchy = MigrateDock("Scene Control", "Hierarchy");
        const bool properties = MigrateDock("Inspector", "Properties") ||
            MigrateDock("Body Transform", "Properties");
        const bool content = MigrateDock("Console / Diagnostics", "Content Browser / Diagnostics") ||
            MigrateDock("Custom shader reload", "Content Browser / Diagnostics");
        if (hierarchy || properties || content || ImGui::FindWindowSettings(ImHashStr("Viewport")))
        {
            // Existing layouts (including deliberately floating panes) remain authoritative.
            if (!hierarchy) ImGui::DockBuilderDockWindow("Hierarchy", dock);
            if (!properties) ImGui::DockBuilderDockWindow("Properties", dock);
            if (!content) ImGui::DockBuilderDockWindow("Content Browser / Diagnostics", dock);
            if (!ImGui::FindWindowSettings(ImHashStr("World Settings")))
            {
                const auto* saved = ImGui::FindWindowSettings(ImHashStr("Properties"));
                ImGui::DockBuilderDockWindow("World Settings", saved && saved->DockId ? saved->DockId : dock);
            }
            m_EditorLayoutPrepared = true;
            return;
        }
    }
    const auto* viewport=ImGui::GetMainViewport();
    const float bar=ImGui::GetFrameHeight();
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dock,{viewport->WorkPos.x,viewport->WorkPos.y+bar});
    ImGui::DockBuilderSetNodeSize(dock,{viewport->WorkSize.x,std::max(100.f,viewport->WorkSize.y-bar)});
    auto center=dock;
    // The left column reaches the bottom edge: Properties remains bottom-left.
    // Bottom diagnostics belong to the remaining center/right area.
    const float workWidth=viewport->WorkSize.x;
    const float panelMin=std::min(Rbs::Editor::Px(180),std::max(1.f,(workWidth-4.f)/3.f));
    const float leftWidth=std::clamp(workWidth*.24f,panelMin,workWidth-2*panelMin-4.f);
    auto left=ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,leftWidth/workWidth,nullptr,&center);
    const float bottomRatio = std::clamp(Rbs::Editor::Px(260) / viewport->WorkSize.y, .25f, .45f);
    const auto bottom=ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,bottomRatio,nullptr,&center);
    const float remainingWidth=ImGui::DockBuilderGetNode(center)->Size.x;
    const float rightWidth=std::clamp(workWidth*.20f,panelMin,std::max(panelMin,remainingWidth-panelMin-2.f));
    const auto right=ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,rightWidth/remainingWidth,nullptr,&center);
    // Keep entity rows visible below the search field at larger UI scales.
    // This runs only for a new layout or the owner's explicit Reset Layout.
    const float leftHeight = ImGui::DockBuilderGetNode(left)->Size.y;
    const float hierarchyRatio = std::clamp(Rbs::Editor::Px(200) / std::max(1.f, leftHeight), .34f, .55f);
    const auto properties=ImGui::DockBuilderSplitNode(left,ImGuiDir_Down,1.f-hierarchyRatio,nullptr,&left);
    ImGui::DockBuilderDockWindow("Hierarchy",left);
    ImGui::DockBuilderDockWindow("Viewport",center);
    ImGui::DockBuilderDockWindow("Properties",properties);
    ImGui::DockBuilderDockWindow("World Settings",right);
    ImGui::DockBuilderDockWindow("Content Browser / Diagnostics",bottom);
    ImGui::DockBuilderFinish(dock);
    m_ResetEditorLayout=false;m_EditorLayoutPrepared=true;
}

void RigidBodySimulationApp::DrawEditorToolbar()
{
    if(ImGui::BeginMenu("Layout"))
    {
        if(ImGui::MenuItem("Reset Layout"))m_ResetEditorLayout=true;
        if (ImGui::BeginMenu("UI scale"))
        {
            constexpr const char* labels[]{"Auto", "100%", "125%", "150%", "200%"};
            for (int i = 0; i < 5; ++i)
                if (ImGui::MenuItem(labels[i], nullptr, Rbs::Editor::ScaleChoice == i))
                {
                    Rbs::Editor::ScaleChoice = i;
                    ImGui::MarkIniSettingsDirty();
                }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    DrawSceneSelectionControls();
    ImGui::Separator();
    ImGui::Text("%s | Tick %llu",SimulationLabel(m_SimulationState),
        static_cast<unsigned long long>(m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()));
    if(m_Launch.m_BackendSwitchChecks)Rbs::BackendSwitchChecks::PanelDrawn();
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
    Rbs::SandboxChecks::PanelDrawn();
#endif
}

void RigidBodySimulationApp::DrawSceneSelectionControls()
{
    ImGui::BeginDisabled(m_SceneRequest.has_value() || m_SimulationState==Rbs::SimulationState::Switching);
    ImGui::SetNextItemWidth(Rbs::Editor::Px(175));
    if(ImGui::BeginCombo("##ScenePreset",Rbs::SceneName(m_PendingSelection.scene)))
    {
        for(auto preset:{Rbs::RbsScenePreset::GeometryGallery,Rbs::RbsScenePreset::SphereDiamond,
            Rbs::RbsScenePreset::SphereLattice,Rbs::RbsScenePreset::BoxStack,Rbs::RbsScenePreset::SphereBoxStack,
            Rbs::RbsScenePreset::DistancePendulum,Rbs::RbsScenePreset::DistanceChain,Rbs::RbsScenePreset::DistanceChainContact})
        {
            const bool selected=preset==m_PendingSelection.scene;
            if(ImGui::Selectable(Rbs::SceneName(preset),selected))
            {
                m_PendingSelection.scene=preset;
                if(preset==Rbs::RbsScenePreset::DistanceChainContact)m_PendingSelection.chainLinks=8;
            }
            if(selected)ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Scene preset; Load applies the pending choice");
    if(m_PendingSelection.IsDistanceScene() && m_PendingSelection.scene!=Rbs::RbsScenePreset::DistancePendulum)
    {
        ImGui::SameLine();ImGui::SetNextItemWidth(Rbs::Editor::Px(48));
        ImGui::BeginDisabled(m_PendingSelection.scene==Rbs::RbsScenePreset::DistanceChainContact);
        int index=m_PendingSelection.chainLinks==4?0:m_PendingSelection.chainLinks==8?1:2;
        if(ImGui::Combo("Links",&index,"4\0" "8\0" "16\0"))m_PendingSelection.chainLinks=std::array{4u,8u,16u}[index];
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if(ImGui::Button("Load"))RequestScene(m_PendingSelection);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Load selected scene at the next Update boundary");
    ImGui::EndDisabled();
}

bool RigidBodySimulationApp::IsSphereLatticeEntity(EntityRenderId id) const
{
    if(!m_ActiveScene || m_ActiveSelection.scene!=Rbs::RbsScenePreset::SphereLattice)return false;
    const auto resolved=m_ActiveScene->RenderData().Resolve(id);if(!resolved)return false;
    const _Entity e{*resolved,m_ActiveScene.get()};
    return e.HasAllComponents<Rbs::Editor::PresetIdentity,RigidBody3DComponent>() &&
        (e.GetComponent<Rbs::Editor::PresetIdentity>().key>>32)==Rbs::Editor::SphereLatticeSphereRole;
}

bool RigidBodySimulationApp::ExecuteSelectionCommand(Rbs::Editor::SelectionCommand command,
    Rbs::Editor::SelectionType type,EntityRenderId root)
{
    namespace E=Rbs::Editor;using Command=E::SelectionCommand;
    if(!m_ActiveScene || m_SceneRequest || m_BackendRequest)return false;
    if(command==Command::Clear){m_EditorSelection.Clear();SynchronizeEditorSelection();m_HierarchyRevealed={};return true;}
    try {
        m_Hierarchy.Build(*m_ActiveScene,m_HierarchyFilter.data());
        if(command==Command::Children && !m_ActiveScene->RenderData().Resolve(root))return false;
        std::vector<EntityRenderId> targets;targets.reserve(m_Hierarchy.nodes.size());
        for(const auto& n:m_Hierarchy.nodes) {
            const _Entity entity{n.entity,m_ActiveScene.get()};bool include=false;
            switch(command) {
            case Command::Scene:include=true;break;
            case Command::Filtered:include=E::NameContains(n.name,m_HierarchyFilter.data());break;
            case Command::SceneVisible:include=EditorEntityVisible(n.identity);break;
            case Command::Focused:include=n.visible && int(n.group)==m_HierarchyFocusedGroup && (!m_HierarchySphereCohort || IsSphereLatticeEntity(n.identity));break;
            case Command::SphereCohort:include=IsSphereLatticeEntity(n.identity);break;
            case Command::Invert:include=!m_EditorSelection.Contains(n.identity);break;
            case Command::Children:
                for(int parent=n.parent;parent!=E::HierarchyModel::None;parent=m_Hierarchy.nodes[parent].parent)
                    if(m_Hierarchy.nodes[parent].identity==root){include=true;break;}
                break;
            case Command::Type:
                if(type==E::SelectionType::Camera)include=entity.HasAnyComponents<RenderCameraComponent,CameraComponent>();
                else if(type==E::SelectionType::Light)include=entity.HasAnyComponents<RenderLightComponent,LightComponent,LightComponents,DirectionalLightComponent,PointLightComponent,SpotLightComponent>();
                else if(type==E::SelectionType::Mesh)include=entity.HasAllComponents<MeshRendererComponent>();
                else if(type==E::SelectionType::Rigidbody)include=entity.HasAllComponents<RigidBody3DComponent>();
                else if(const auto binding=m_ActiveScene->GetBodyBinding(entity)) {
                    if(const auto body=m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->ReadBody(*binding)) {
                        const ShapeType kinds[]{ShapeType::Sphere,ShapeType::Box,ShapeType::Capsule,ShapeType::Convex};include=body->shapeType==kinds[int(type)];
                    } else {m_PropertyDiagnostic="Selection body identity changed; previous selection retained.";return false;}
                }
                break;
            default:break;
            }
            if(include)targets.push_back(n.identity);
        }
        if(!m_EditorSelection.SelectSet(m_ActiveScene->RenderData(),targets,E::EntitySelection::SetOperation::Replace)) {
            m_PropertyDiagnostic="Selection rejected: stale identity, allocation failure or 4096-entity renderer budget. Previous selection retained.";return false;
        }
        SynchronizeEditorSelection();m_HierarchyRevealed=m_ActiveScene->RenderData().Identify(m_HoveredEntity).value_or(EntityRenderId{});
        m_HierarchySelectionHandoff=true;return true;
    } catch(const std::bad_alloc&) {m_PropertyDiagnostic="Selection allocation failed; previous selection retained.";return false;}
}

void RigidBodySimulationApp::DrawHierarchyRegion()
{
    namespace E=Rbs::Editor;
    if(ImGui::Begin("Hierarchy",nullptr,ImGuiWindowFlags_NoScrollWithMouse))
    {
        PublishPanelInputRegion();
        const auto active=m_ActiveScene->RenderData().Identify(m_HoveredEntity).value_or(EntityRenderId{});
        EditorHistoryShortcuts();
        if(active && !ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2,false))BeginEntityRename(active);
        const auto previousRegistry=m_Hierarchy.nodes.empty()?0:m_Hierarchy.nodes.front().identity.registry;
        m_Hierarchy.Build(*m_ActiveScene,m_HierarchyFilter.data(),active);
        const bool sceneChanged=!m_Hierarchy.nodes.empty() && previousRegistry!=m_Hierarchy.nodes.front().identity.registry;
        ImGui::TextWrapped("%s | %zu entities",Rbs::SceneName(m_ActiveSelection.scene),m_Hierarchy.nodes.size());
        ImGui::TextDisabled("%zu selected",m_EditorSelection.count);
        ImGui::SameLine();if(ImGui::SmallButton("Select..."))ImGui::OpenPopup("HierarchySelection");
        E::SelectionCommand selectionCommand{};E::SelectionType selectionType{};EntityRenderId childrenRoot=active;bool requested=false;
        const auto selectionMenu=[&](EntityRenderId target) {
            const auto item=[&](const char* label,E::SelectionCommand command){if(ImGui::MenuItem(label)){selectionCommand=command;requested=true;}};
            item("Select All in Scene (including hidden)",E::SelectionCommand::Scene);
            item("Select Filter Matches (all groups)",E::SelectionCommand::Filtered);
            item("Select Scene-Visible Entities",E::SelectionCommand::SceneVisible);
            if(ImGui::BeginMenu("Select All of Type (full scene)")) {
                for(int i=0;i<8;++i)if(ImGui::MenuItem(E::SelectionTypeNames[i])){selectionCommand=E::SelectionCommand::Type;selectionType=E::SelectionType(i);requested=true;}
                ImGui::EndMenu();
            }
            if(ImGui::MenuItem("Select All Children (descendants)",nullptr,false,bool(target))) {selectionCommand=E::SelectionCommand::Children;childrenRoot=target;requested=true;}
            item("Invert Selection (full scene)",E::SelectionCommand::Invert);item("Deselect All",E::SelectionCommand::Clear);
        };
        if(ImGui::BeginPopup("HierarchySelection")){selectionMenu(active);ImGui::EndPopup();}
        ImGui::TextDisabled("Ctrl+A: filtered %s",m_HierarchySphereCohort?"SphereLattice spheres":E::GroupNames[m_HierarchyFocusedGroup]);
        ImGui::SetNextItemWidth(-1.f);
        const bool searchChanged=ImGui::InputTextWithHint("##EntityFilter","Find entity",m_HierarchyFilter.data(),m_HierarchyFilter.size());
        if(searchChanged)m_Hierarchy.Build(*m_ActiveScene,m_HierarchyFilter.data(),active);
        if(m_RenameTarget && !m_RenameError.empty())ImGui::TextWrapped("%s",m_RenameError.c_str());
        ImGui::Separator();
        const bool searching=m_HierarchyFilter[0]!=0;
        const bool reveal=active!=m_HierarchyRevealed || (searchChanged && !searching);
        const int atlas=E::IconAtlasIndex();
        constexpr E::Icon icons[]{E::Icon::Camera,E::Icon::Light,E::Icon::Environment,E::Icon::Rigidbody,E::Icon::Entity};
        ImGui::BeginChild("EntityTree",{0,0},false,ImGuiWindowFlags_HorizontalScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        // Keep physical wheel axes in Hierarchy: bundled ImGui otherwise remaps
        // Shift+wheel to horizontal scrolling before this panel is submitted.
        if(ImGui::IsWindowHovered() && !ImGui::GetIO().WantTextInput && !ImGui::GetCurrentContext()->DragDropActive) {
            auto* w=ImGui::GetCurrentWindow();const auto& io=ImGui::GetIO();
            if(io.MouseWheel)ImGui::SetScrollY(std::clamp(w->Scroll.y-io.MouseWheel*std::floor(std::min(5*ImGui::GetFontSize(),w->InnerRect.GetHeight()*.67f)),0.f,w->ScrollMax.y));
            if(io.MouseWheelH)ImGui::SetScrollX(std::clamp(w->Scroll.x-io.MouseWheelH*2*ImGui::GetFontSize(),0.f,w->ScrollMax.x));
        }
        // A replacement world must start with its group headings visible. Ordinary
        // frames and selection reveals preserve user scrolling and tree expansion.
        if(sceneChanged){ImGui::SetScrollX(0);ImGui::SetScrollY(0);}
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{E::Px(4),E::Px(2)});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{E::Px(4),std::max(E::Px(2),(E::Px(22)-ImGui::GetFontSize())*.5f)});
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing,E::Px(16));
        // Search has separate open-state storage: clearing it restores the user's tree.
        ImGui::PushID(searching?"Filtered":"Tree");
        m_VisibleHierarchyRows.clear();
        EntityRenderId clicked{};bool selectSpheres=false;
        const auto sphere=[&](const _Entity& e) {return IsSphereLatticeEntity(m_ActiveScene->RenderData().Identify(e).value_or(EntityRenderId{}));};
        const auto row=[&](const char* id,const char* label,E::Icon icon,ImGuiTreeNodeFlags flags,bool activeRow,bool editing=false)
        {
            const auto start=ImGui::GetCursorScreenPos();
            const float height=std::max(ImGui::GetFrameHeight(),E::Px(22));
            ImGui::SetNextItemWidth(-1.f);
            const bool open=ImGui::TreeNodeEx(id,flags|ImGuiTreeNodeFlags_FramePadding,"%s","");
            const auto end=ImGui::GetItemRectMax();
            const float x=start.x+ImGui::GetTreeNodeToLabelSpacing();
            const float glyph=E::Px(20);
            E::DrawIcon(icon,m_ViewportIconAtlases[atlas],E::AtlasPixels[atlas],
                {std::round(x),std::round(start.y+(height-glyph)*.5f)},glyph,
                activeRow?E::Color(0xA9D1FF):E::Color(0x9DAAB9));
            const ImVec2 text{x+glyph+E::Px(5),start.y+(height-ImGui::GetFontSize())*.5f};
            if(!editing)ImGui::GetWindowDrawList()->AddText(text,ImGui::GetColorU32(ImGuiCol_Text),label);
            // Preserve label width for horizontal scrolling without changing LastItem.
            ImGui::GetCurrentWindow()->DC.CursorMaxPos.x=std::max(ImGui::GetCurrentWindow()->DC.CursorMaxPos.x,
                text.x+ImGui::CalcTextSize(label).x+E::Px(8));
            if(activeRow)ImGui::GetWindowDrawList()->AddRectFilled({start.x,start.y},{start.x+E::Px(2),end.y},IM_COL32(93,161,224,255));
            return open;
        };
        const auto draw=[&](auto&& self,int index,std::size_t depth)->void
        {
            if(depth>m_Hierarchy.nodes.size())return;
            const auto& n=m_Hierarchy.nodes[index];
            if(!n.visible)return;
            const auto resolved=m_ActiveScene->RenderData().Resolve(n.identity);
            if(!resolved)return;
            const _Entity entity{*resolved,m_ActiveScene.get()};
            m_VisibleHierarchyRows.push_back(n.identity);
            const bool leaf=n.firstChild==E::HierarchyModel::None;
            const bool selected=entity==m_HoveredEntity || m_EditorSelection.Contains(n.identity);
            auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_AllowItemOverlap;
            if(leaf)flags|=ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if(selected)flags|=ImGuiTreeNodeFlags_Selected;
            ImGui::PushID(static_cast<int>(n.entity));
            if(!leaf && (searching || (reveal && n.activePath)))ImGui::SetNextItemOpen(true);
            auto icon=icons[int(E::Classify(entity))];
            if(entity.HasAllComponents<RenderLightComponent>()) {
                const auto kind=entity.GetComponent<RenderLightComponent>().kind;
                const auto spot=E::SpotArtwork==1?E::Icon::SpotA:E::SpotArtwork==2?E::Icon::SpotC:E::Icon::Spot;
                icon=kind==RenderLightKind::Directional?E::Icon::Sun:kind==RenderLightKind::Spot?spot:E::Icon::Light;
            }
            const auto rowStart=ImGui::GetCursorScreenPos();
            const float eyeX=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x-E::Px(26);
            const bool open=row("##Entity",n.name.data(),icon,flags,entity==m_HoveredEntity,m_RenameTarget==n.identity);
            const bool onName=ImGui::GetMousePos().x<eyeX;
            if(ImGui::IsItemHovered() && onName && ImGui::IsMouseDoubleClicked(0) && !ImGui::IsItemToggledOpen())BeginEntityRename(n.identity);
            if(ImGui::BeginPopupContextItem("EntityMenu")) {
                if(ImGui::MenuItem("Rename","F2"))BeginEntityRename(n.identity);
                selectionMenu(n.identity);
                if(m_ActiveSelection.scene==Rbs::RbsScenePreset::SphereLattice && ImGui::MenuItem("Select All Spheres"))selectSpheres=true;
                if(entity.HasAllComponents<RenderCameraComponent>() && ImGui::MenuItem("Open Camera View"))OpenCameraPanel(n.identity);
                const bool visible=EditorEntityVisible(n.identity);
                if(ImGui::MenuItem("Scene Visible",nullptr,visible))QueueEntityVisibility(n.identity,!visible);
                ImGui::EndPopup();
            }
            if(ImGui::IsItemClicked() && onName && m_RenameTarget!=n.identity && !ImGui::IsItemToggledOpen())
            {
                clicked=n.identity;m_HierarchyFocusedGroup=int(n.group);m_HierarchySphereCohort=sphere(entity);
                // Keep keyboard focus in Hierarchy; the next routed View click can
                // activate AND select. No UI click is forwarded to the camera.
                m_HierarchySelectionHandoff=true;
            }
            if(ImGui::IsItemHovered() && ImGui::GetMousePos().x>ImGui::GetItemRectMax().x-E::Px(20))
                ImGui::SetTooltip("%s",n.name.data());
            const auto nextRow=ImGui::GetCursorScreenPos();
            if(m_RenameTarget==n.identity) {
                ImGui::SetCursorScreenPos({rowStart.x+ImGui::GetTreeNodeToLabelSpacing()+E::Px(25),rowStart.y});
                DrawEntityRename(n.identity,std::max(E::Px(60),eyeX-ImGui::GetCursorScreenPos().x-E::Px(4)));
            }
            ImGui::SetCursorScreenPos({eyeX,rowStart.y});
            const bool visible=EditorEntityVisible(n.identity);
            ImGui::PushStyleColor(ImGuiCol_Button,E::Color(0x202329));
            if(ImGui::Button("##SceneVisible",{E::Px(24),E::Px(22)}))QueueEntityVisibility(n.identity,!visible);
            E::DrawIcon(visible?E::Icon::Eye:E::Icon::EyeOff,m_ViewportIconAtlases[atlas],E::AtlasPixels[atlas],{eyeX+E::Px(2),rowStart.y+E::Px(1)},E::Px(20),visible?E::Color(0xA6B3C3):E::Color(0x718093));
            E::Tooltip("Scene Visible: editor mesh/icon/picking only. Camera output, illumination, shadows and Physics are unchanged.");
            ImGui::PopStyleColor();ImGui::SetCursorScreenPos(nextRow);
            if(reveal && entity==m_HoveredEntity)ImGui::SetScrollHereY(.5f);
            if(open && !leaf)
            {
                for(int child=n.firstChild;child!=E::HierarchyModel::None;child=m_Hierarchy.nodes[child].next)self(self,child,depth+1);
                ImGui::TreePop();
            }
            ImGui::PopID();
        };
        bool any=false;
        for(int g=0;g<int(E::EntityGroup::Count);++g)
        {
            const auto& group=m_Hierarchy.groups[g];
            if(!group.visible)continue;
            any=true;ImGui::PushID(g);
            if(searching || (reveal && group.activePath))ImGui::SetNextItemOpen(true);
            char label[96];std::snprintf(label,sizeof(label),"%s  (%u)",E::GroupNames[g],searching?group.visible:group.total);
            ImGuiTreeNodeFlags flags=ImGuiTreeNodeFlags_SpanAvailWidth;
            if(g==int(E::EntityGroup::PhysicsBodies))flags|=ImGuiTreeNodeFlags_DefaultOpen;
            const bool open=row("##Group",label,icons[g],flags,false);
            if(ImGui::IsItemClicked()){m_HierarchyFocusedGroup=g;m_HierarchySphereCohort=false;}
            if(ImGui::BeginPopupContextItem("GroupMenu")) {
                selectionMenu({});
                if(m_ActiveSelection.scene==Rbs::RbsScenePreset::SphereLattice && ImGui::MenuItem("Select All Spheres"))selectSpheres=true;
                ImGui::EndPopup();
            }
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Editor-only category; no Scene parenting is changed.");
            if(open)
            {
                for(int root=group.first;root!=E::HierarchyModel::None;root=m_Hierarchy.nodes[root].next)draw(draw,root,0);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if(!any)ImGui::TextDisabled("No matching entities");
        if(clicked)
        {
            if(ImGui::GetIO().KeyShift)
            {
                if(m_EditorSelection.SelectRange(m_ActiveScene->RenderData(),m_VisibleHierarchyRows,clicked,ImGui::GetIO().KeyCtrl))
                    SynchronizeEditorSelection();
                else if(m_EditorSelection.capacityReached)m_PropertyDiagnostic="Range exceeds 4096 selections; previous selection retained.";
            }
            else if(auto e=m_ActiveScene->RenderData().Resolve(clicked))SelectColliderEntity({_Entity{*e,m_ActiveScene.get()}},ImGui::GetIO().KeyCtrl);
        }
        const bool all=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
            !m_RenameTarget && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel) &&
            ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_A,false);
        if(all || selectSpheres || requested) {
            (void)ExecuteSelectionCommand(selectSpheres?E::SelectionCommand::SphereCohort:all?E::SelectionCommand::Focused:selectionCommand,selectionType,childrenRoot);
        }
        // Consume local changes now. Only an external single-object selection may
        // reveal next frame; range endpoints/anchors never request scrolling.
        m_HierarchyRevealed=(clicked || all || selectSpheres || requested)?m_ActiveScene->RenderData().Identify(m_HoveredEntity).value_or(EntityRenderId{}):active;
        static const bool trace=SDL_getenv("GENGINE_RBS_SELECTION_TRACE")!=nullptr;static int traceFrames{};
        if(trace) {
            if(clicked || all || selectSpheres || (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel))traceFrames=4;
            if(traceFrames>0){--traceFrames;Log::GetCoreLogger()->info("HIERARCHY_SELECTION scroll={} target={} selected={} anchor={} active={} wheel={} shift={} nav={} active_widget={} drag_drop={}",
                ImGui::GetScrollY(),ImGui::GetCurrentWindow()->ScrollTarget.y,m_EditorSelection.count,m_EditorSelection.anchor.index,m_HoveredEntity?static_cast<unsigned>(m_HoveredEntity):0,
                ImGui::GetIO().MouseWheel,ImGui::GetIO().KeyShift,ImGui::GetCurrentContext()->NavId,ImGui::GetCurrentContext()->ActiveId,ImGui::GetCurrentContext()->DragDropActive);}
        }
        ImGui::PopID();ImGui::PopStyleVar(3);ImGui::EndChild();
    }
    ImGui::End();
}

void RigidBodySimulationApp::DrawInspectorRegion()
{
    if (m_InspectorTabRequest == 1) ImGui::SetNextWindowFocus();
    if (ImGui::Begin("Properties"))
    {
        PublishPanelInputRegion();
        EditorHistoryShortcuts();
        DrawEditorProperties();
    }
    ImGui::End();
    if (m_InspectorTabRequest == 0) ImGui::SetNextWindowFocus();
    if (ImGui::Begin("World Settings"))
    {
        PublishPanelInputRegion();
        DrawWorldProperties();
    }
    ImGui::End();
    m_InspectorTabRequest = -1;
}

void RigidBodySimulationApp::DrawWorldProperties()
{
    ImGui::TextWrapped("Scene: %s",Rbs::SceneName(m_ActiveSelection.scene));
    ImGui::TextUnformatted("Interactive Sandbox");
    if(Rbs::Editor::Component("Physics"))DrawPhysicsSettings();
    if(Rbs::Editor::Component("Environment",false))DrawEnvironmentProperties();
    if(Rbs::Editor::Component("Materials", false))DrawMaterialSection();
    if(Rbs::Editor::Component("Custom shaders", false))DrawShaderSection();
}

void RigidBodySimulationApp::DrawDiagnosticsRegion()
{
    if(ImGui::Begin("Content Browser / Diagnostics"))
    {
        PublishPanelInputRegion();
        if(ImGui::BeginTabBar("DiagnosticsTabs"))
        {
            if (ImGui::BeginTabItem("Content Browser"))
            {
                DrawContentBrowser();
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Console",nullptr,m_DiagnosticsTabRequest==0?ImGuiTabItemFlags_SetSelected:0))
            { DrawConsoleSection();ImGui::EndTabItem(); }
            if(ImGui::BeginTabItem("Profiler",nullptr,m_DiagnosticsTabRequest==1?ImGuiTabItemFlags_SetSelected:0))
            { DrawPerformanceSection();ImGui::EndTabItem(); }
            if(ImGui::BeginTabItem("Physics Debug",nullptr,m_DiagnosticsTabRequest==2?ImGuiTabItemFlags_SetSelected:0))
            {
                DrawPhysicsDebugSection();DrawPresentationSettings();
                ImGui::TextWrapped("%s",m_SceneDiagnostic.c_str());
                ImGui::EndTabItem();
            }
            m_DiagnosticsTabRequest=-1;
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void RigidBodySimulationApp::DrawContentBrowser()
{
    namespace E = Rbs::Editor;
    constexpr std::array presets{Rbs::RbsScenePreset::GeometryGallery, Rbs::RbsScenePreset::SphereDiamond,
        Rbs::RbsScenePreset::SphereLattice, Rbs::RbsScenePreset::BoxStack, Rbs::RbsScenePreset::SphereBoxStack,
        Rbs::RbsScenePreset::DistancePendulum, Rbs::RbsScenePreset::DistanceChain, Rbs::RbsScenePreset::DistanceChainContact};
    constexpr const char* titles[]{"Geometry Gallery", "Sphere Diamond", "Sphere Lattice", "Box Stack",
        "Sphere + Box Stack", "Distance Pendulum", "Distance Chain", "Chain + Contact"};
    const float browserWidth=ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted("Scenes / Presets");
    if(browserWidth>E::Px(340))ImGui::SameLine();
    ImGui::SetNextItemWidth(std::min(E::Px(200),ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##PresetFilter", "Search scenes", m_PresetFilter.data(), m_PresetFilter.size());
    if(browserWidth>E::Px(720))ImGui::SameLine();
    if(browserWidth<E::Px(360))ImGui::TextUnformatted("Thumbnail size");
    ImGui::SetNextItemWidth(std::min(E::Px(160),ImGui::GetContentRegionAvail().x));
    if(E::Numeric::Scalar("##ThumbnailWidth",E::ThumbnailWidth,1.f,E::Numeric::Closed(96,320),E::Numeric::Accept{},true,"%d px"))ImGui::MarkIniSettingsDirty();
    if(browserWidth>=E::Px(360)){ImGui::SameLine();ImGui::TextUnformatted("Thumbnail size");}
    if(browserWidth>E::Px(1150))ImGui::SameLine();
    ImGui::TextWrapped("Drag a preset into Viewport | Double-click or select and Load");
    ImGui::Separator();
    if (!m_PresetThumbnailsLoaded)
    {
        m_PresetThumbnailsLoaded = true;
        // Eight optional preview resources, owned by the existing asset registry.
        for (std::size_t i = 0; i < presets.size(); ++i)
        {
            const std::string path = std::string("Editor/Scenes/") + Rbs::SceneName(presets[i]);
            if (!RuntimeAssets::TryFile("Images/" + path + ".png")) continue;
            auto texture = LoadSceneTexture(path);
            if (texture) m_PresetThumbnails[i] = *texture;
        }
    }
    ImGui::BeginChild("PresetGrid",{0,std::max(E::Px(80),ImGui::GetContentRegionAvail().y)});
    const float available=ImGui::GetContentRegionAvail().x;
    const int columns=std::clamp(int(available/(E::Px(float(E::ThumbnailWidth))+E::Px(16))),1,8);
    const float cell=std::max(1.f,std::min(E::Px(float(E::ThumbnailWidth)+16),available/columns));
    const float imageWidth=std::max(1.f,cell-E::Px(16)), imageHeight=imageWidth*9.f/16.f;
    float labelHeight=0;
    for(const auto* title:titles)labelHeight=std::max(labelHeight,ImGui::CalcTextSize(title,nullptr,false,imageWidth).y);
    const float cardHeight=imageHeight+labelHeight+ImGui::GetTextLineHeight()+E::Px(22);
    ImGui::BeginDisabled(m_SceneRequest.has_value() || m_SimulationState == Rbs::SimulationState::Switching);
    if (ImGui::BeginTable("Presets", columns, ImGuiTableFlags_SizingFixedFit))
    {
        for (std::size_t i = 0; i < presets.size(); ++i)
        {
            if (m_PresetFilter[0] && !ImStristr(titles[i], nullptr, m_PresetFilter.data(), nullptr)) continue;
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            const auto start = ImGui::GetCursorPos();
            const bool selected = m_PendingSelection.scene == presets[i];
            if (ImGui::Selectable("##Preset", selected, ImGuiSelectableFlags_AllowDoubleClick,
                {cell - E::Px(8), cardHeight}))
            {
                m_PendingSelection.scene = presets[i];
                if (presets[i] == Rbs::RbsScenePreset::DistanceChainContact) m_PendingSelection.chainLinks = 8;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) RequestScene(m_PendingSelection);
            }
            if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                const Rbs::ScenePresetPayload payload{1,{presets[i],presets[i]==Rbs::RbsScenePreset::DistanceChainContact?8u:m_PendingSelection.chainLinks}};
                ImGui::SetDragDropPayload(Rbs::ScenePresetPayloadType,&payload,sizeof(payload));
                ImGui::Text("Load %s",titles[i]);ImGui::EndDragDropSource();
            }
            E::Tooltip(Rbs::SceneName(presets[i]));
            const auto end = ImGui::GetCursorPos();
            ImGui::SetCursorPos({start.x + E::Px(4), start.y + E::Px(4)});
            if (m_PresetThumbnails[i]) UI::Image(m_PresetThumbnails[i], {imageWidth, imageHeight}, {0, 1}, {1, 0});
            else ImGui::Dummy({imageWidth, imageHeight});
            ImGui::SetCursorPosX(start.x + E::Px(4));
            ImGui::PushTextWrapPos(start.x + cell - E::Px(12));
            ImGui::TextUnformatted(titles[i]);
            ImGui::TextDisabled("%s", m_ActiveSelection.scene == presets[i] ? "ACTIVE SCENE" : "SCENE PRESET");
            ImGui::PopTextWrapPos();
            ImGui::SetCursorPos(end);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
}

void RigidBodySimulationApp::DrawConsoleSection()
{
    if(ImGui::Button("Refresh log"))m_ConsoleRefreshTime=0;
    ImGui::SameLine();ImGui::TextUnformatted("GEngine.log | latest 64 KiB");
    if(!m_SceneDiagnostic.empty())ImGui::TextWrapped("%s",m_SceneDiagnostic.c_str());
    if(ImGui::GetTime()>=m_ConsoleRefreshTime)
    {
        m_ConsoleRefreshTime=ImGui::GetTime()+1.;
        std::ifstream log("GEngine.log",std::ios::binary|std::ios::ate);
        if(log)
        {
            const auto end=log.tellg();
            if(end>=0)
            {
                const auto count=std::min<std::streamoff>(end,64*1024);
                log.seekg(end-count);m_ConsoleText.resize(static_cast<std::size_t>(count));
                log.read(m_ConsoleText.data(),count);m_ConsoleText.resize(static_cast<std::size_t>(log.gcount()));
            }
        }
        else m_ConsoleText="Log file is not yet available.";
    }
    ImGui::BeginChild("LogContents",{0,0},false,ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(m_ConsoleText.c_str());
    ImGui::EndChild();
}

void RigidBodySimulationApp::DrawPerformanceSection()
{
    auto* physics=m_ActiveScene->GetPhysicsSystem();auto* world=physics->GetPhysicsWorld();
    const float fps=ImGui::GetIO().Framerate;
    ImGui::Text("Frame average: %.2f ms | %.1f FPS",fps>0?1000.f/fps:0.f,fps);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Rolling 120-frame wall-time average, including present pacing.\nCamera changes affect visible work; this is not GPU busy time or Physics Step time.");
    ImGui::Text("Bodies: %zu | Joints: %zu | Tick: %llu",world->GetBodyCount(),world->GetJointCount(),
        static_cast<unsigned long long>(physics->GetCompletedSteps()));
    const auto& timing=m_ActiveScene->GetPhysicsTiming();
    ImGui::Text("Last frame: %u physics steps | pending %.4f s",timing.stepsLastUpdate,timing.pendingSeconds);
    ImGui::TextWrapped("Live diagnostics only. Timing qualification is a separate, pending gate.");
    if(m_Backend==PhysicsBackend::Jolt)ImGui::TextWrapped("Native contact/solver timing counters are unavailable for Jolt.");
}

void RigidBodySimulationApp::ProcessBodyPropertyCommand()
{
    auto& edit=m_BodyPropertyEditor;
    if(!edit.queued)return;
    edit.queued=false;
    const auto binding=m_ActiveScene->GetBodyBinding(edit.entity);
    if(edit.generation!=m_SceneGeneration || !binding || *binding!=edit.target || edit.entity!=m_HoveredEntity)
    {m_PropertyDiagnostic="Body/selection changed; pending Apply cancelled.";edit={};return;}
    auto* world=m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    const auto result=world->UpdateBodyProperties(edit.target,edit.draft);
    if(!result)
    {m_PropertyDiagnostic="Apply rejected: "+std::string(result.error().operation)+" (code "+std::to_string(int(result.error().code))+"). Previous body retained.";return;}
    const auto actual=world->ReadBodyProperties(edit.target);
    if(!actual){m_PropertyDiagnostic="Body readback failed; simulation paused.";SetSimulationState(Rbs::SimulationState::Paused);return;}
    edit.draft=*actual;edit.dirty=false;
    m_PropertyDiagnostic="Applied; authoritative body readback refreshed.";
}

void RigidBodySimulationApp::ProcessWorldSleepCommand()
{
    auto& edit=m_WorldSleepEditor;
    if(!edit.queued)return;
    edit.queued=false;
    if(!m_ActiveScene || edit.generation!=m_SceneGeneration)
    {edit={};edit.diagnostic="World changed; pending sleep Apply cancelled.";return;}
    auto& world=*m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
    if(auto result=world.UpdateWorldSleepSettings(edit.draft);!result)
    {edit.diagnostic="Apply rejected: "+std::string(result.error().operation);return;}
    const auto actual=world.ReadWorldSleepSettings();
    if(!actual){edit.diagnostic="World sleep readback failed; simulation paused.";SetSimulationState(Rbs::SimulationState::Paused);return;}
    edit.draft=*actual;edit.dirty=false;
    edit.diagnostic="Applied; active world readback refreshed. Sleep tests restarted.";
}

void RigidBodySimulationApp::DrawWorldSleepSettings()
{
    if(!m_ActiveScene || !ImGui::TreeNode("Sleeping"))return;
    namespace E=Rbs::Editor;
    auto& edit=m_WorldSleepEditor;
    const auto actual=m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->ReadWorldSleepSettings();
    if(edit.generation!=m_SceneGeneration){edit={};edit.generation=m_SceneGeneration;}
    if(actual && !edit.dirty && !edit.queued)edit.draft=*actual;
    ImGui::BeginDisabled(!actual || edit.queued || !SandboxEnabled() || m_SceneRequest.has_value());
    edit.dirty|=E::Number("Point speed (m/s)",edit.draft.pointVelocityThreshold,.001f,
        "Jolt world three-point sleep test. Finite, positive velocity; not a per-body linear threshold.",E::Numeric::Positive<float>(),[&](float v)->const char*{const float product=v*edit.draft.timeBeforeSleep;return std::isfinite(product)&&product>0?nullptr:"Sleep speed/time product must remain finite and positive.";});
    edit.dirty|=E::Number("Time before sleep (s)",edit.draft.timeBeforeSleep,.01f,"Finite positive dwell time. Apply wakes bodies and restarts their sleep tests.",E::Numeric::Positive<float>(),[&](float v)->const char*{const float product=v*edit.draft.pointVelocityThreshold;return std::isfinite(product)&&product>0?nullptr:"Sleep speed/time product must remain finite and positive.";});
    ImGui::BeginDisabled(!edit.dirty || E::Numeric::HasError());
    if(ImGui::Button("Apply Sleeping"))edit.queued=true;
    ImGui::EndDisabled();
    if(ImGui::GetContentRegionAvail().x>E::Px(260))ImGui::SameLine();
    if(ImGui::Button("Revert Sleeping") && actual){edit.draft=*actual;edit.dirty=false;edit.diagnostic.clear();E::Numeric::Reset();}
    if(ImGui::Button("Defaults##Sleeping")){edit.draft={};edit.dirty=true;E::Numeric::Reset();}
    ImGui::EndDisabled();
    if(!actual)ImGui::TextWrapped("Native has per-body sleep thresholds in Properties > Rigidbody. Jolt world settings are unavailable here.");
    else
    {
        ImGui::TextWrapped("Active: %.4g m/s / %.4g s",actual->pointVelocityThreshold,actual->timeBeforeSleep);
        ImGui::TextWrapped("Interactive world override. Reset/scene load restores defaults. Apply Defaults before switching to Native. Per-body Allow sleeping remains separate.");
    }
    if(!edit.diagnostic.empty())ImGui::TextWrapped("%s",edit.diagnostic.c_str());
    if(edit.dirty || edit.queued)ImGui::TextUnformatted(edit.queued?"Queued for simulation boundary":"Unapplied sleep draft");
    ImGui::TreePop();
}

void RigidBodySimulationApp::ProcessContextProperties()
{
    auto& edit=m_ContextEditor;
    if(edit.generation!=m_SceneGeneration){edit={};m_SkyVisibilityEdit.reset();return;}
    if(m_SkyVisibilityEdit)
    {
        if(m_SkyBoxEntity && m_SkyBoxEntity.GetSceneContext()==m_ActiveScene.get())
        {
            const bool hasVisibility=m_SkyBoxEntity.HasAllComponents<VisibilityComponent>();
            auto value=hasVisibility?m_SkyBoxEntity.GetComponent<VisibilityComponent>():VisibilityComponent{};
            value.enabled=*m_SkyVisibilityEdit;
            const auto id=m_ActiveScene->RenderData().Identify(m_SkyBoxEntity);
            if(id)
            {
                const auto result=hasVisibility?m_ActiveScene->RenderData().Replace(*id,value):m_ActiveScene->RenderData().Add(*id,value);
                edit.diagnostic=result?"Sky visibility applied; authoritative component refreshed.":"Sky visibility rejected; previous state retained.";
            }
        }
        m_SkyVisibilityEdit.reset();
    }
    const int command=std::exchange(edit.queued,0);if(!command)return;
    const auto resolved=m_ActiveScene->RenderData().Resolve(edit.target);
    if(!resolved || m_SceneRequest || m_BackendRequest){edit.diagnostic="World/entity changed; edit rejected.";return;}
    const _Entity entity{*resolved,m_ActiveScene.get()};
    bool applied=false;
    if(command==1 && edit.camera && entity.HasAllComponents<RenderCameraComponent>())
    {
        if(const auto* reason=Rbs::Editor::ValidateCamera(*edit.camera)){edit.diagnostic=reason;return;}
        if(entity.GetComponent<RenderCameraComponent>()!=*edit.cameraBefore){edit.diagnostic="Camera changed since drafting; Revert to refresh.";return;}
        applied=bool(m_ActiveScene->RenderData().Replace(edit.target,*edit.camera));
        if(applied){edit.camera=edit.cameraBefore=entity.GetComponent<RenderCameraComponent>();edit.cameraDirty=false;}
    }
    if(command==2 && edit.light && entity.HasAllComponents<RenderLightComponent>())
    {
        if(const auto* reason=Rbs::Editor::ValidateLight(*edit.light,m_NearPlane)){edit.diagnostic=reason;return;}
        if(entity.GetComponent<RenderLightComponent>()!=*edit.lightBefore){edit.diagnostic="Light changed since drafting; Revert to refresh.";return;}
        for(auto e:m_ActiveScene->GetAllEntitiesWith<RenderLightComponent>())
            if(e!=*resolved && _Entity{e,m_ActiveScene.get()}.GetComponent<RenderLightComponent>().kind==edit.light->kind)
            {edit.diagnostic="Renderer capacity: one directional, one point and one spot light. Requested type already exists.";return;}
        if(edit.light->kind==RenderLightKind::Spot && edit.light->castShadows) {
            if(!m_FrameSubmission){edit.diagnostic="Renderer unavailable; light unchanged.";return;}
            auto ready=m_FrameSubmission->PrepareSpotShadowTarget(edit.light->spotShadow);
            if(!ready){edit.diagnostic=DescribeSubmissionError(ready.error());return;}
        }
        applied=bool(m_ActiveScene->RenderData().Replace(edit.target,*edit.light));
        if(applied){edit.light=edit.lightBefore=entity.GetComponent<RenderLightComponent>();edit.lightDirty=false;}
    }
    if(command==3 && edit.material && entity.HasAllComponents<MeshRendererComponent>() &&
        entity.GetComponent<MeshRendererComponent>().material==edit.materialTarget)
    {
        const auto current=m_SceneResources->DescribeMaterial(edit.materialTarget);
        if(!current || *current!=*edit.materialBefore){edit.diagnostic="Material changed since drafting; Revert to refresh.";return;}
        const auto result=m_SceneResources->EditMaterial(edit.materialTarget,*edit.material);
        if(!result){edit.diagnostic=DescribeSceneResourceError(result.error());return;}
        const auto actual=m_SceneResources->DescribeMaterial(edit.materialTarget);
        applied=bool(actual);if(actual){edit.material=edit.materialBefore=*actual;edit.materialDirty=false;}
    }
    edit.diagnostic=applied?"Applied at Update; authoritative readback refreshed.":"Edit rejected; previous component retained.";
}

void RigidBodySimulationApp::DrawContextProperties()
{
    namespace E=Rbs::Editor;
    auto& edit=m_ContextEditor;const auto id=m_ActiveScene->RenderData().Identify(m_HoveredEntity);
    if(!id)return;
    if(edit.target!=*id || edit.generation!=m_SceneGeneration){edit={};edit.target=*id;edit.generation=m_SceneGeneration;}
    if(m_HoveredEntity.HasAllComponents<RenderCameraComponent>() && !edit.cameraDirty && edit.queued!=1)
        edit.camera=edit.cameraBefore=m_HoveredEntity.GetComponent<RenderCameraComponent>();
    if(m_HoveredEntity.HasAllComponents<RenderLightComponent>() && !edit.lightDirty && edit.queued!=2)
        edit.light=edit.lightBefore=m_HoveredEntity.GetComponent<RenderLightComponent>();
    if(m_HoveredEntity.HasAllComponents<MeshRendererComponent>() && !edit.materialDirty && edit.queued!=3)
    {
        edit.materialTarget=m_HoveredEntity.GetComponent<MeshRendererComponent>().material;
        if(auto actual=m_SceneResources->DescribeMaterial(edit.materialTarget))edit.material=edit.materialBefore=*actual;
        else edit.material.reset();
    }
    const auto actions=[&](int command,bool& dirty,const char* invalid)
    {
        if(invalid)ImGui::TextWrapped("%s",invalid);
        ImGui::BeginDisabled(!dirty || invalid || edit.queued || E::Numeric::HasError());
        if(ImGui::Button("Apply"))edit.queued=command;
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("Revert")){E::Numeric::Reset();dirty=false;edit.queued=0;edit.diagnostic="Draft discarded; current component will be reloaded.";}
    };
    if(m_HoveredEntity.HasAllComponents<MeshRendererComponent>() && E::Component("Surface Material",true,"Surface material"))
    {
        ImGui::PushID("SurfaceMaterial");
        E::ReadOnly("Material instance",std::format("{}",edit.materialTarget.index).c_str());
        if(!edit.material)ImGui::TextWrapped("This helper/sky pipeline has no typed material authoring contract. Surface edits are unavailable.");
        else
        {
            auto& material=*edit.material;
            ImGui::TextWrapped("Shared material: edits affect all objects using this material. Physics friction/restitution remain in Rigidbody.");
            edit.materialDirty|=E::Color3("Base color (linear)",material.baseColor.data());
            edit.materialDirty|=E::Number("Metallic",material.metallic,.01f,"0 to 1; multiplier when a metallic texture is bound.",E::Numeric::Closed(0.f,1.f));
            edit.materialDirty|=E::Number("Roughness",material.roughness,.01f,"0 to 1; multiplier when a roughness texture is bound.",E::Numeric::Closed(0.f,1.f));
            edit.materialDirty|=E::Number("Normal strength",material.normalStrength,.01f,"Range 0 to 2.",E::Numeric::Closed(0.f,2.f));
            edit.materialDirty|=E::Number("AO strength",material.aoStrength,.01f,"Range 0 to 1.",E::Numeric::Closed(0.f,1.f));
            edit.materialDirty|=E::Number("UV repeat X",material.uvTiling[0],.05f,"Range 0.001 to 1024.",E::Numeric::Closed(.001f,1024.f));
            edit.materialDirty|=E::Number("UV repeat Y",material.uvTiling[1],.05f,"Range 0.001 to 1024.",E::Numeric::Closed(.001f,1024.f));
            actions(3,edit.materialDirty,nullptr);
        }
        ImGui::PopID();
    }
    if(edit.camera && E::Component("Camera"))
    {
        ImGui::PushID("SceneCamera");auto& camera=*edit.camera;
        const auto cameraCheck=[&](auto member){return [&,member](float v){auto candidate=camera;candidate.*member=v;return E::ValidateCamera(candidate);};};
        ImGui::TextWrapped("Scene Camera. Editing does not change the Editor Camera or the active view.");
        const auto active=m_ActiveScene->ActiveSceneCamera();
        ImGui::TextUnformatted(active && *active && **active==*id?"Active Scene Camera":"Scene Camera (not active)");
        if(m_CameraView==CameraView::Preview && m_PreviewCamera==*id) {
            if(ImGui::Button("End camera preview"))(void)EndCameraPreview();
        } else if(ImGui::Button("Preview this camera"))(void)SetCameraView(CameraView::Preview,*id);
        if(ImGui::Button("Open Camera View"))OpenCameraPanel(*id);
        ImGui::SameLine();if(ImGui::Button("Make active"))(void)SelectSceneCamera(*id);
        {E::PropertyRow row("Projection");int projection=int(camera.projection);
            if(ImGui::Combo("##projection",&projection,"Perspective\0Orthographic\0")){camera.projection=Component::CameraProjection(projection);edit.cameraDirty=true;}}
        if(camera.projection==Component::CameraProjection::Perspective)
        {float fov=glm::degrees(camera.verticalFovRadians);if(E::Number("Vertical FOV (deg)",fov,.1f,"Open range: 1 to 179 degrees; finite, invertible projection required.",E::Numeric::Bounds<float>{glm::degrees(.0174533f),glm::degrees(3.124139f),true,true},[&](float v){auto candidate=camera;candidate.verticalFovRadians=glm::radians(v);return E::ValidateCamera(candidate);})){camera.verticalFovRadians=glm::radians(fov);edit.cameraDirty=true;}}
        else {
            float size=camera.orthographicHeight*.5f;
            if(E::Number("Orthographic size (m)",size,.1f,"Positive half-height; full view height is twice this size.",E::Numeric::Positive<float>(),[&](float v){auto c=camera;c.orthographicHeight=2.f*v;return E::ValidateCamera(c);})){camera.orthographicHeight=size*2.f;edit.cameraDirty=true;}
        }
        edit.cameraDirty|=E::Number("Near clip (m)",camera.nearPlane,.01f,"0 < near < far.",E::Numeric::Bounds<float>{0.f,camera.farPlane,true,true},cameraCheck(&RenderCameraComponent::nearPlane));
        edit.cameraDirty|=E::Number("Far clip (m)",camera.farPlane,1.f,"Far must exceed near; finite, invertible projection required.",E::Numeric::Bounds<float>{camera.nearPlane,{},true},cameraCheck(&RenderCameraComponent::farPlane));
        actions(1,edit.cameraDirty,E::ValidateCamera(camera));ImGui::PopID();
    }
    if(edit.light && E::Component("Light"))
    {
        ImGui::PushID("SceneLight");auto& light=*edit.light;
        {E::PropertyRow row("Type","Renderer supports one Directional, one Point and one Spot Light.");int kind=int(light.kind);
            if(ImGui::Combo("##kind",&kind,"Directional\0Point\0Spot\0")){light.kind=RenderLightKind(kind);edit.lightDirty=true;}}
        edit.lightDirty|=E::Color3("Color (linear)",light.color.data());
        edit.lightDirty|=E::Number("Intensity",light.intensity,.05f,"Linear multiplier; finite and nonnegative.",E::Numeric::Nonnegative<float>());
        ImGui::BeginDisabled(light.kind==RenderLightKind::Directional);
        const bool point=light.kind==RenderLightKind::Point;
        edit.lightDirty|=E::Number(point?"Shadow range (m)":"Range (m)",light.range,.1f,
            point?"Cube-shadow far clip and spherical guide radius. Point illumination has no finite cutoff.":"Spot range is the radial illumination cutoff; Directional lights have no finite range.",
            E::Numeric::Bounds<float>{point?m_NearPlane:0.f,{},true},
            [&](float value){auto candidate=light;candidate.range=value;return E::ValidateLight(candidate,m_NearPlane);});ImGui::EndDisabled();
        if(point) {
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Finite illumination cutoff is unsupported; the sphere shows shadow coverage.");
            ImGui::PopStyleColor();
        }
        if(light.kind==RenderLightKind::Spot)
        {
            float inner=glm::degrees(light.innerConeRadians),outer=glm::degrees(light.outerConeRadians);
            if(E::Number("Inner cone (deg)",inner,.1f,"0 <= inner <= outer.",E::Numeric::Closed(0.f,outer),[&](float v){auto candidate=light;candidate.innerConeRadians=glm::radians(v);return E::ValidateLight(candidate);})){light.innerConeRadians=glm::radians(inner);edit.lightDirty=true;}
            if(E::Number("Outer cone (deg)",outer,.1f,"inner <= outer < 90; outer must be positive.",E::Numeric::Bounds<float>{inner,glm::degrees(1.5707963f),inner==0,true},[&](float v){auto candidate=light;candidate.outerConeRadians=glm::radians(v);return E::ValidateLight(candidate);})){light.outerConeRadians=glm::radians(outer);edit.lightDirty=true;}
        }
        edit.lightDirty|=ImGui::Checkbox("Cast shadows",&light.castShadows);
        if(light.kind==RenderLightKind::Spot) {
            ImGui::BeginDisabled(!light.castShadows);
            edit.lightDirty|=E::Number("Shadow resolution",light.spotShadow.resolution,64.f,"1 to 8192 pixels; GPU allocation is validated before Apply.",E::Numeric::Closed(1,8192));
            edit.lightDirty|=E::Number("Shadow bias",light.spotShadow.bias,.0001f,"Normalized projected depth, 0 to 1.",E::Numeric::Closed(0.f,1.f));
            {E::PropertyRow row("Filtering");edit.lightDirty|=ImGui::Combo("##PCF",&light.spotShadow.pcfRadius,"Hard\0PCF 3 x 3\0PCF 5 x 5\0");}
            ImGui::EndDisabled();
        }
        if(light.kind==RenderLightKind::Directional)ImGui::TextWrapped("Direction comes from Rotation. Position is an editor anchor and does not affect illumination or shadows.");
        actions(2,edit.lightDirty,E::ValidateLight(light,m_NearPlane));ImGui::PopID();
    }
    if(!edit.diagnostic.empty())ImGui::TextWrapped("%s",edit.diagnostic.c_str());
}

void RigidBodySimulationApp::DrawEnvironmentProperties()
{
    if(m_SkyBoxEntity && m_SkyBoxEntity.GetSceneContext()==m_ActiveScene.get())
    {
        bool visible=m_SkyVisibilityEdit.value_or(!m_SkyBoxEntity.HasAllComponents<VisibilityComponent>() || m_SkyBoxEntity.GetComponent<VisibilityComponent>().enabled);
        if(ImGui::Checkbox("Sky visibility",&visible)){m_ContextEditor.generation=m_SceneGeneration;m_SkyVisibilityEdit=visible;}
    }
    else ImGui::TextDisabled("No sky entity in this scene");
    ImGui::BeginDisabled();float ambient=.05f;Rbs::Editor::Number("Ambient factor",ambient);ImGui::EndDisabled();
    ImGui::TextWrapped("Ambient factor is fixed by the current PBR shader. Sky tint, exposure and rotation have no Scene authoring API.");
    ImGui::TextWrapped("Select Ground or a wall in Hierarchy for Transform, Surface material and Rigidbody settings. Lighting is edited on real Light entities.");
}

#include "RbsJoltParameterUi.inl"

#include "RbsBulkProperties.inl"

#include "RbsImpulseEditor.inl"
