#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Scene/_Scene.h"
#include <format>

#include "UI/Dockspace.h"
#include "UI/FramebufferImage.h"
#include <imgui/imgui.h>

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;

void RigidBodySimulationApp::ImGuiRender()
{
    if (auto host = UI::BeginDockspaceHost({"DockSpace Demo", "MyDockSpace"}); !host)
    {
        ReportPlatformError(host.error());
        m_Running = false;
        return;
    }
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if (ImGui::GetFrameCount() == 4)
        ImGui::LogToFile(-1, "scene-ui-text.log");
#endif
    DrawShaderPanel();
#ifdef GENGINE_RBS_SCENE_VALIDATION
    // ImGui::End stops logging for each non-child window; resume the same capture.
    if (ImGui::GetFrameCount() == 4)
        ImGui::LogToFile(-1, "scene-ui-text.log");
#endif
    DrawMaterialPanel();
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if (ImGui::GetFrameCount() == 4)
        ImGui::LogToFile(-1, "scene-ui-text.log");
#endif
    DrawMenus();
    DrawViewport();
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if (ImGui::GetFrameCount() == 4)
        ImGui::LogFinish();
#endif
    UI::EndDockspaceHost();
#ifdef GENGINE_RBS_MODULARITY_VALIDATION
    ValidatePacketAUi();
#endif
}

void RigidBodySimulationApp::DrawShaderPanel()
{
    ImGui::Begin("Custom shader reload");
    if (m_ShaderReloadGallery.material)
    {
        ImGui::TextUnformatted("Both cubes share a shader; the clone keeps its lower brightness.");
        if (ImGui::Button("Focus custom cubes"))
            FocusMaterialCamera(m_EditorCamera_, {1.5f, 5, -22}, .2f, 0, 12.f);
        if (ImGui::Button("Reload color edit"))
            m_ShaderReloadGallery.request = 1;
        ImGui::SameLine();
        if (ImGui::Button("Restore original"))
            m_ShaderReloadGallery.request = 2;
        if (ImGui::Button("Try invalid shader"))
            m_ShaderReloadGallery.request = 3;
        ImGui::SameLine();
        if (ImGui::Button("Try incompatible schema"))
            m_ShaderReloadGallery.request = 4;
        if (ImGui::Button("Prepare edit"))
            m_ShaderReloadGallery.request = 5;
        ImGui::SameLine();
        if (ImGui::Button("Commit prepared"))
            m_ShaderReloadGallery.request = 6;
        ImGui::SameLine();
        if (ImGui::Button("Cancel prepared"))
            m_ShaderReloadGallery.request = 7;
        ImGui::TextWrapped("%s", m_ShaderReloadGallery.message.c_str());
    }
    else
        ImGui::TextDisabled("No shader target in this scene. Select GeometryGallery at launch.");
    ImGui::End();
}

void RigidBodySimulationApp::DrawMaterialPanel()
{
    ImGui::Begin("Material authoring");
    if (m_MaterialGallery.uvMaterial)
    {
        ImGui::TextUnformatted("Plane / Cube / Sphere / Capsule / Torus / Diamond");
        ImGui::TextUnformatted("Same floor: UV front row, Triplanar second row");
        if (ImGui::Button("Focus material comparison"))
        {
            FocusMaterialCamera(m_EditorCamera_, {0, 2, -22}, .35f, 0, 38.f);
        }
        ImGui::Checkbox("Animate object transforms", &m_MaterialGallery.animate);
        constexpr const char* geometryNames[]{"Plane",   "Cube",  "Sphere",
                                              "Capsule", "Torus", "Diamond"};
        ImGui::Combo("Inspect geometry", &m_MaterialGallery.inspectedGeometry, geometryNames, 6);
        if (ImGui::Button("Focus selected geometry"))
        {
            FocusMaterialCamera(m_EditorCamera_,
                                {-10.f + 4.f * m_MaterialGallery.inspectedGeometry, 2.f, -20.f},
                                .35f, .65f, 7.f);
        }
        const char* comparisons[]{"Paired rows", "UV at reference positions",
                                  "Triplanar at reference positions"};
        if (ImGui::Combo("Comparison", &m_MaterialGallery.comparison, comparisons, 3))
            SetMaterialComparison(m_MaterialGallery.comparison);
        if (auto desc = m_SceneResources->DescribeMaterial(m_MaterialGallery.triplanarMaterial);
            desc)
        {
            bool changed = ImGui::SliderFloat("Projection repeats / local unit",
                                              &desc->projectionScale.value, .001f, 8.f);
            changed |= ImGui::SliderFloat("Blend sharpness", &desc->blendSharpness.value, 1, 8);
            changed |= ImGui::SliderFloat("Normal strength", &desc->normalStrength, 0, 2);
            if (changed)
                if (auto edited =
                        m_SceneResources->EditMaterial(m_MaterialGallery.triplanarMaterial, *desc);
                    !edited)
                    Log::GetCoreLogger()->error("{}", DescribeSceneResourceError(edited.error()));
        }
    }
    else
        ImGui::TextDisabled("No material target in this scene. Select GeometryGallery at launch.");
    ImGui::End();
}

void RigidBodySimulationApp::DrawAsyncMenu()
{
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if (ImGui::GetFrameCount() == 3 || ImGui::GetFrameCount() == 4)
        ImGui::OpenPopup("Async box texture");
#endif
    if (ImGui::BeginMenu("Async box texture", !m_AsyncTextureGallery.clone || m_WoodSettled))
    {
        if (!m_AsyncTextureGallery.clone)
            ImGui::TextDisabled(
                "No async demo target in this scene. Select GeometryGallery at launch.");
        else
        {
            for (const auto* path :
                 {"Sphere/wood_diffuse", "Plane/wood_diffuse", "missing-phase12-image"})
                if (ImGui::MenuItem(path))
                {
                    m_AsyncTextureGallery.source = path;
                    m_AsyncTextureGallery.request = true;
                }
            if (ImGui::MenuItem("Cancel pending request", nullptr, false,
                                m_AsyncTextureGallery.pending.has_value()))
                m_AsyncTextureGallery.cancel = true;
            ImGui::TextWrapped("%s", m_AsyncTextureGallery.message.c_str());
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Load barrel mesh", nullptr, false,
                        m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery && m_WoodSettled &&
                            !m_LoadBarrel))
        m_LoadBarrel = true;
}

void RigidBodySimulationApp::DrawMenus()
{
    if (ImGui::BeginMenuBar())
    {
#ifdef GENGINE_RBS_SCENE_VALIDATION
        if (ImGui::GetFrameCount() == 3 || ImGui::GetFrameCount() == 4)
            ImGui::OpenPopup("File");
#endif
        if (ImGui::BeginMenu("File"))
        {
            DrawAsyncMenu();
            // Disabling fullscreen would allow the window to be moved to the front of other windows,
            // which we can't undo at the moment without finer window depth/z control.
            if (ImGui::MenuItem("Exit"))
            {
                ShutDown();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Shadows"))
        {
            auto request = GetShadowQuality().requested;
            int tier = static_cast<int>(request.quality);
            bool changed =
                ImGui::Combo("Quality", &tier, "Low (1024)\0Medium (2048)\0High (4096)\0Custom\0");
            request.quality = static_cast<ShadowQuality>(tier);
            if (request.quality == ShadowQuality::Custom)
            {
                int resolution = static_cast<int>(request.customResolution);
                if (ImGui::InputInt("Resolution", &resolution, 64, 256,
                                    ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    request.customResolution =
                        resolution > 0 ? static_cast<unsigned>(resolution) : 0;
                    changed = true;
                }
            }
            int budget = static_cast<int>(request.byteBudget / (1024 * 1024));
            if (ImGui::InputInt("Depth budget (MiB)", &budget, 48, 192,
                                ImGuiInputTextFlags_EnterReturnsTrue))
            {
                request.byteBudget = budget > 0 ? std::uint64_t(budget) * 1024 * 1024 : 0;
                changed = true;
            }
            bool fallback = request.fallback == ShadowFallback::LowerTiers;
            if (ImGui::Checkbox("Allow lower quality on allocation failure", &fallback))
            {
                request.fallback = fallback ? ShadowFallback::LowerTiers : ShadowFallback::None;
                changed = true;
            }
            if (changed)
                if (auto queued = RequestShadowQuality(request); !queued)
                    ReportFramebufferError("shadow quality request", queued.error());
            const auto& current = GetShadowQuality();
            ImGui::Text("Active: %s (%u x %u)", ShadowQualityLabel(current.effective).data(),
                        current.resolution, current.resolution);
            ImGui::Text("Estimated / allocated depth: %.1f / %.1f MiB",
                        double(current.memory.estimatedBytes) / (1024 * 1024),
                        double(current.memory.allocatedDepthBytes) / (1024 * 1024));
            ImGui::TextDisabled("Driver overhead is not included.");
            if (ShadowQualityPending())
                ImGui::TextUnformatted("Quality change pending");
            if (current.fallbackReason)
                ImGui::TextWrapped("Lower tier selected: %s", current.fallbackReason->message);
            if (GetShadowQualityError())
                ImGui::TextWrapped("Previous quality retained: %s",
                                   GetShadowQualityError()->message);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void RigidBodySimulationApp::DrawViewport()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
    //ImGui::ShowDemoWindow(&p_open);
    const bool viewportVisible = ImGui::Begin("Viewport");
    auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
    auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
    auto viewportOffset = ImGui::GetWindowPos();
    m_ViewportBounds[0] = {viewportMinRegion.x + viewportOffset.x,
                           viewportMinRegion.y + viewportOffset.y};
    m_ViewportBounds[1] = {viewportMaxRegion.x + viewportOffset.x,
                           viewportMaxRegion.y + viewportOffset.y};
    //GENGINE_CORE_INFO("{0}, {1}", viewportOffset.x, viewportOffset.y);

    m_ViewportForcused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();

    //GENGINE_INFO("Focused: {}", ImGui::IsWindowFocused());
    //GENGINE_INFO("Hovered: {}", ImGui::IsWindowHovered());

    auto viewportPanelSize = ImGui::GetContentRegionAvail();
    const bool hasArea = viewportVisible && viewportPanelSize.x > 0 && viewportPanelSize.y > 0;
    auto scale = hasArea ? UI::CurrentViewportFramebufferScale()
                         : std::expected<FramebufferScale, PlatformError>(FramebufferScale{});
    if (scale)
    {
        if (auto sized = SetEditorViewport(
                hasArea ? EditorViewportLogicalSize{viewportPanelSize.x, viewportPanelSize.y}
                        : EditorViewportLogicalSize{},
                *scale);
            !sized)
            ReportPlatformError(sized.error());
    }
    else
        ReportPlatformError(scale.error());

    ObserveViewport(scale);

    //m_ViewportSize = {1280, 720};
    //m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

    //if (m_RenderTarget && m_RenderTarget->IsMultiSampled())
    //m_RenderTarget->BindAndBlitToScreen(0);

    //ImGui::Image(reinterpret_cast<void*>(m_FinalFrameBuffer->GetColorMap()), { m_ViewportSize.x, m_ViewportSize.y }, { 0,1 }, { 1, 0 });
    if (HasVisibleViewport())
        if (auto image = UI::FramebufferImage(*m_RenderTarget, m_ViewportSize.x, m_ViewportSize.y);
            !image)
            ReportFramebufferError("viewport image", image.error());

    /*m_MousePickFrameBuffer->Bind();
	RenderSystem::OnMouseClicked(m_ActiveScene.get(), *m_MousePickFrameBuffer, m_ViewportBounds[0], m_ViewportBounds[1]);
	m_MousePickFrameBuffer->UnBind();*/

    /*auto windowSize = ImGui::GetWindowSize();
	ImVec2 minBound = ImGui::GetWindowPos();
	minBound.x += viewportOffset.x;
	minBound.y += viewportOffset.y;

	ImVec2 maxBound = { minBound.x + windowSize.x, minBound.y + windowSize.y };

	m_ViewportBounds[0] = { minBound.x, minBound.y };
	m_ViewportBounds[1] = { maxBound.x, maxBound.y };*/

    ImGui::End();
    ImGui::PopStyleVar();
}

namespace
{
    // Note: Switch this to true to enable dockspace
    //static bool show = true;
    //ImGui::ShowDemoWindow(&show);

    //static bool opt_fullscreen = true;
    //static bool opt_padding = false;
    //static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    //////// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
    //////// because it would be confusing to have two docking targets within each others.
    //ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    //if (opt_fullscreen)
    //{
    //	ImGuiViewport* viewport = ImGui::GetMainViewport();
    //	ImGui::SetNextWindowPos(viewport->Pos);
    //	ImGui::SetNextWindowSize(viewport->Size);
    //	ImGui::SetNextWindowViewport(viewport->ID);
    //	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    //	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    //	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    //	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    //}
    //else
    //{
    //	dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
    //}

    ////// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
    ////// and handle the pass-thru hole, so we ask Begin() to not render a background.
    //if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
    //	window_flags |= ImGuiWindowFlags_NoBackground;

    ////// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
    ////// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
    ////// all active windows docked into it will lose their parent and become undocked.
    ////// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
    ////// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
    //if (!opt_padding)
    //	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    //ImGui::Begin("DockSpace Demo", &show, window_flags);
    //if (!opt_padding)
    //	ImGui::PopStyleVar();

    //if (opt_fullscreen)
    //	ImGui::PopStyleVar(2);

    ////// Submit the DockSpace
    //ImGuiIO& io = ImGui::GetIO();

    //auto& style = ImGui::GetStyle();
    //float winMinSize = style.WindowMinSize.x;
    //style.WindowMinSize.x = 370.f;
    //if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    //{
    //	ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    //	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    //}

    //style.WindowMinSize.x = winMinSize;

    //if (ImGui::BeginMenuBar())
    //{
    //	if (ImGui::BeginMenu("File"))
    //	{
    //		// Disabling fullscreen would allow the window to be moved to the front of other windows,
    //		// which we can't undo at the moment without finer window depth/z control.
    //		//ImGui::MenuItem("Fullscreen", nullptr, &opt_fullscreen);
    //		//ImGui::MenuItem("Padding", nullptr, &opt_padding);
    //		//ImGui::Separator();

    //		if (ImGui::MenuItem("Exit"))
    //		{
    //			ShutDown();
    //		}

    //		ImGui::EndMenu();
    //	}

    //	ImGui::EndMenuBar();
    //}

    //ImGui::End();
}

void RigidBodySimulationApp::UI_Toolbar()
{
    /*ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	auto& colors = ImGui::GetStyle().Colors;
	const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
	const auto& buttonActive = colors[ImGuiCol_ButtonActive];
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActive.x, buttonActive.y, buttonActive.z, 0.5f));*/
}
