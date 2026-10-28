#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Scene/_Scene.h"
#include "Physics/PhysicsSystem.h"
#include "Renderer/SceneRenderResources.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>

#include "UI/Dockspace.h"
#include "UI/FramebufferImage.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;

namespace Rbs
{
#ifdef GENGINE_PHASE24_COLLIDER_VALIDATION
    static std::uint64_t meshColliderFitCalls{};
    std::uint64_t MeshColliderFitCalls() { return meshColliderFitCalls; }
#endif

    // Command-only CPU preparation. Current scale becomes the replacement's
    // explicit unit-scale base; ordinary Physics creation/scaling is unchanged.
    std::expected<CollisionShapeDesc, GeometryAuthoringCode>
    FitMeshCollider(const MeshAsset& mesh, const Vec3f& scale, ShapeType type)
    {
#ifdef GENGINE_PHASE24_COLLIDER_VALIDATION
        ++meshColliderFitCalls;
#endif
        using Error = GeometryAuthoringCode;
        if (!Math::IsFinite(scale)) return std::unexpected(Error::InvalidScale);
        const auto& bounds = mesh.Bounds();
        if (bounds.empty || !mesh.VertexCount()) return std::unexpected(Error::EmptyBounds);
        const auto outward = [](double value) {
            float result = static_cast<float>(value);
            if (double(result) < value)
                result = std::nextafter(result, std::numeric_limits<float>::infinity());
            return result;
        };
        if (type == ShapeType::Box)
        {
            BoxShapeDesc box;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const double a = bounds.minimum[axis] * double(scale[axis]);
                const double b = bounds.maximum[axis] * double(scale[axis]);
                const double lower = (std::min)(a, b), upper = (std::max)(a, b);
                box.center[axis] = static_cast<float>((lower + upper) * .5);
                box.halfExtents[axis] = outward((std::max)(upper - double(box.center[axis]),
                                                                        double(box.center[axis]) - lower));
                if (!std::isfinite(box.center[axis]) || !std::isfinite(box.halfExtents[axis]) ||
                    box.halfExtents[axis] <= 0)
                    return std::unexpected(Error::DegenerateExtent);
            }
            return CollisionShapeDesc{box};
        }
        if (type != ShapeType::Sphere && type != ShapeType::Capsule)
            return std::unexpected(Error::InvalidDimensions);

        const Vec3f magnitude = glm::abs(scale);
        if (bounds.sphereCenter == std::array<double, 3>{} &&
            magnitude.x == magnitude.y && magnitude.x == magnitude.z)
        {
            const float radius = outward(bounds.sphereRadius * double(magnitude.x));
            if (!std::isfinite(radius) || radius <= 0)
                return std::unexpected(Error::InvalidDimensions);
            if (type == ShapeType::Sphere) return CollisionShapeDesc{SphereShapeDesc{radius}};
            const double radialExtent = (std::max)({std::abs(bounds.minimum[0]), std::abs(bounds.maximum[0]),
                                                    std::abs(bounds.minimum[2]), std::abs(bounds.maximum[2])}) * magnitude.x;
            // A spherical mesh needs no cylinder segment. The cached sphere is
            // already conservative; allow only its Float32 outward rounding.
            if (radius <= std::nextafter(static_cast<float>(radialExtent), std::numeric_limits<float>::infinity()))
                return CollisionShapeDesc{CapsuleShapeDesc{radius, 0.f}};
        }

        const auto layout = mesh.Layout();
        const VertexAttribute* position = nullptr;
        for (const auto& attribute : layout.attributes)
            if (attribute.semantic == VertexSemantic::Position) position = &attribute;
        if (!position || position->scalar != VertexScalarFormat::Float32 || position->components != 3 ||
            position->offsetBytes + sizeof(float) * 3 > layout.strideBytes)
            return std::unexpected(Error::UnsupportedLayout);
        const auto vertex = [&](std::size_t index) {
            std::array<float, 3> values;
            std::memcpy(values.data(), mesh.Vertices().data() + index * layout.strideBytes + position->offsetBytes,
                        sizeof(values));
            return glm::dvec3(values[0] * double(scale.x), values[1] * double(scale.y), values[2] * double(scale.z));
        };
        double squaredRadius = 0;
        for (std::size_t index = 0; index < mesh.VertexCount(); ++index)
        {
            const auto p = vertex(index);
            squaredRadius = (std::max)(squaredRadius, type == ShapeType::Sphere ? glm::dot(p, p) : p.x*p.x + p.z*p.z);
        }
        const float radius = outward(std::sqrt(squaredRadius));
        if (!std::isfinite(radius) || radius <= 0) return std::unexpected(Error::InvalidDimensions);
        if (type == ShapeType::Sphere) return CollisionShapeDesc{SphereShapeDesc{radius}};

        // Fixed local-Y axis and existing pivot. For the chosen enclosing radial
        // radius, this is the shortest symmetric segment containing all vertices.
        double halfLength = 0;
        for (std::size_t index = 0; index < mesh.VertexCount(); ++index)
        {
            const auto p = vertex(index);
            const double cap = std::sqrt((std::max)(0.0, double(radius)*radius - p.x*p.x - p.z*p.z));
            halfLength = (std::max)(halfLength, std::abs(p.y) - cap);
        }
        const float length = outward(halfLength);
        if (!std::isfinite(length) || !std::isfinite(radius + length))
            return std::unexpected(Error::InvalidDimensions);
        return CollisionShapeDesc{CapsuleShapeDesc{radius, length}};
    }

    std::expected<void, std::string> ReplaceMeshCollider(_Scene& scene, SceneRenderResources& resources,
                                                       _Entity entity, ShapeType type)
    {
        if (!entity || entity.GetSceneContext() != &scene ||
            !entity.HasAllComponents<MeshRendererComponent, Transform3DComponent>() || !scene.GetBodyBinding(entity))
            return std::unexpected(std::string("Mesh fitting requires a live rendered body."));
        const auto source = resources.GeometrySource(entity.GetComponent<MeshRendererComponent>().mesh);
        if (!source) return std::unexpected(DescribeSceneResourceError(source.error()));
        if (!source->source) return std::unexpected(std::string("Mesh CPU geometry is unavailable."));
        auto description = FitMeshCollider(*source->source, entity.GetComponent<Transform3DComponent>().Scale, type);
        if (!description) return std::unexpected(std::format("Mesh collider fit rejected ({})", int(description.error())));
        if (auto replaced = scene.ReplaceBodyCollider(entity, *description); !replaced)
            return std::unexpected(std::format("Runtime collider replacement rejected ({})", int(replaced.error().code)));
        return {};
    }
}
#ifdef GENGINE_LIFECYCLE_VALIDATION
namespace PreEditorLifecycle
{
    bool UIActive();
    bool ObserveViewportFocus(bool, bool, std::uint64_t, double, float, float,
                              ImDrawList*, int, int, const ImVec4&, const ImVec4&);
}
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
#include <array>
namespace PreEditorLifecycle
{
    bool PickTraceActive() noexcept;
    void PickTraceValues(const char*, std::int64_t = -1, const std::array<std::uint64_t, 12>& = {},
                         const std::array<double, 12>& = {}) noexcept;
    void PickTraceUI(const char*, const InputRouting&, int, unsigned, unsigned) noexcept;
    void PickTraceUIBegin() noexcept;
}
#endif
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
namespace PreEditorInput
{
    void NoteUIItem(unsigned);
    void NoteUIMenu(unsigned, bool, float, float, float, float);
    void DrawInputFixture();
}
#endif

namespace
{
    void PublishPanelInputRegion()
    {
        const auto position = ImGui::GetWindowPos();
        const auto size = ImGui::GetWindowSize();
        const auto origin = ImGui::GetMainViewport()->Pos;
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        const InputRect region{position.x - origin.x, position.y - origin.y,
                               position.x + size.x - origin.x, position.y + size.y - origin.y};
        auto result = BaseApp::GetInputManager()->AddUIRegion(region);
        PreEditorLifecycle::PickTraceValues("ui_region", bool(result), {},
                                            {region.left, region.top, region.right, region.bottom});
        if (!result)
#else
        if (auto result = BaseApp::GetInputManager()->AddUIRegion(
                {position.x - origin.x, position.y - origin.y, position.x + size.x - origin.x,
                 position.y + size.y - origin.y});
            !result)
#endif
            Log::GetCoreLogger()->error("UI input region rejected: {}", int(result.error()));
    }
}

void RigidBodySimulationApp::ImGuiRender()
{
    GetInputManager()->BeginUIRouting();
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PreEditorLifecycle::PickTraceUIBegin();
#endif
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
    if (ImGui::Begin("Body Transform"))
    {
        PublishPanelInputRegion();
        ImGui::Checkbox("Pause simulation", &m_IsPause);
        bool interpolate = m_ActiveScene->IsRenderInterpolationEnabled();
        if (ImGui::Checkbox("Interpolate presentation", &interpolate))
            m_ActiveScene->SetRenderInterpolationEnabled(interpolate);
        ImGui::Checkbox("Physics and selected presentation outlines", &m_IsShowDebugBoundingBox);
        ImGui::TextWrapped("Gold: selected visible mesh. Cyan: current Physics collider. Magenta: selected presentation collider. Interpolation can separate the collider diagnostics.");
        if (auto pose = m_ActiveScene->ReadBodyTransform(m_HoveredEntity); pose)
        {
            ImGui::Text("Selected: %s", m_HoveredEntity.GetName().c_str());
            ImGui::Text("Mode: %s", pose->type == BodyType::Static ? "Static" :
                pose->type == BodyType::Kinematic ? "Kinematic" : "Dynamic");
            static std::string diagnostic;
            const auto report = [&](const auto& result) {
                diagnostic = result ? "Edit accepted" : std::format("Edit rejected (code {}). Previous state preserved.", int(result.error().code));
            };
            auto position = m_HoveredEntity.Transform().Translation;
            auto rotation = m_HoveredEntity.Transform().EulerRotation;
            auto scale = m_HoveredEntity.Transform().Scale;
            if (ImGui::DragFloat3("Teleport position", &position.x, .05f))
                report(m_ActiveScene->SetLocalTranslation(m_HoveredEntity, position));
            if (ImGui::DragFloat3("Teleport rotation (radians)", &rotation.x, .01f))
                report(m_ActiveScene->SetLocalRotation(m_HoveredEntity, rotation));
            if (ImGui::DragFloat3("Transform scale", &scale.x, .02f))
                report(m_ActiveScene->SetLocalScale(m_HoveredEntity, scale));
            auto* world = m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            if (auto body = world->ReadBody(pose->body); body && pose->type != BodyType::Static)
            {
                auto linear = body->linearVelocity, angular = body->angularVelocity;
                bool changed = ImGui::DragFloat3("Linear velocity", &linear.x, .05f);
                changed |= ImGui::DragFloat3("Angular velocity", &angular.x, .02f);
                if (changed) report(world->SetBodyVelocity(pose->body, linear, angular));
            }
            static int replacement = 0;
            static bool fitMesh = true;
            static float radius = 1, halfLength = .5f;
            static Vec3f halfExtents(.5f);
            ImGui::Combo("Collider type", &replacement, "Sphere\0Box\0Capsule\0");
            ImGui::Checkbox("Fit current mesh", &fitMesh);
            if (!fitMesh)
            {
                if (replacement == 1) ImGui::InputFloat3("Half extents", &halfExtents.x);
                else
                {
                    ImGui::InputFloat("Radius", &radius);
                    if (replacement == 2) ImGui::InputFloat("Segment half length", &halfLength);
                }
            }
            if (ImGui::Button("Replace runtime collider"))
            {
                if (fitMesh)
                {
                    const auto type = replacement == 1 ? ShapeType::Box : replacement == 2 ? ShapeType::Capsule : ShapeType::Sphere;
                    const auto changed = Rbs::ReplaceMeshCollider(*m_ActiveScene, *m_SceneResources, m_HoveredEntity, type);
                    diagnostic = changed ? "" : changed.error();
                }
                else
                {
                    CollisionShapeDesc desc = SphereShapeDesc{radius};
                    if (replacement == 1) desc = BoxShapeDesc{halfExtents};
                    if (replacement == 2) desc = CapsuleShapeDesc{radius, halfLength};
                    report(m_ActiveScene->ReplaceBodyCollider(m_HoveredEntity, desc));
                }
            }
            ImGui::TextWrapped("Mesh fitting uses the current mesh and scale only when replacing. Sphere and Capsule fit around the existing pivot. Mesh and startup fixtures stay unchanged.");
            if (!diagnostic.empty()) ImGui::TextWrapped("%s", diagnostic.c_str());
        }
        else ImGui::TextWrapped("Click a rendered body to inspect or edit it.");
    }
    ImGui::End();
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if (ImGui::GetFrameCount() == 4)
        ImGui::LogToFile(-1, "scene-ui-text.log");
#endif
    DrawMenus();
#if defined(GENGINE_INPUT_VALIDATION) && !defined(GENGINE_INPUT_CONTROL)
    PreEditorInput::DrawInputFixture();
#endif
#if defined(GENGINE_LIFECYCLE_VALIDATION) && !defined(GENGINE_INPUT_VALIDATION)
    if (PreEditorLifecycle::UIActive()) PreEditorInput::DrawInputFixture();
#endif
    DrawViewport();
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if (ImGui::GetFrameCount() == 4)
        ImGui::LogFinish();
#endif
    UI::EndDockspaceHost();
    // Publish one completed UI descriptor for the next input poll. The viewport
    // background is a View surface; every active widget/menu/popup has UI priority.
    const auto& io = ImGui::GetIO();
    const auto origin = ImGui::GetMainViewport()->Pos;
    const bool popup =
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool viewGesture = GetInputManager()->PointerCapture() == InputLayer::View;
    auto& uiContext = *ImGui::GetCurrentContext();
    // Consumption follows the owned pointer gesture, even if Alt belonged to a
    // different layer before activation. Let ImGui retire its own queued release.
    if (m_ViewportAltGestureConsumed)
    {
        uiContext.NavWindowingToggleLayer = false;
        bool queuedAlt = false;
        for (const auto& queued : uiContext.InputEventsQueue)
            queuedAlt |= queued.Type == ImGuiInputEventType_Key &&
                (queued.Key.Key == ImGuiKey_ModAlt || queued.Key.Key == ImGuiKey_LeftAlt ||
                 queued.Key.Key == ImGuiKey_RightAlt);
        if (!io.KeyAlt && !(GetInputManager()->GetPhysicalState().modifiers & LeftAlt) && !queuedAlt)
            m_ViewportAltGestureConsumed = false;
    }
    const auto* viewportWindow = ImGui::FindWindowByName("Viewport");
    // ImGui retains a MoveId for a background click even when title-bar-only
    // movement prevents a drag. It is not an active widget taking this gesture.
    const bool viewBackground =
        viewportWindow && uiContext.ActiveIdWindow == viewportWindow &&
        uiContext.ActiveId == viewportWindow->MoveId && !uiContext.MovingWindow;
    const bool active = ImGui::IsAnyItemActive() && !viewBackground;
    const bool text = io.WantTextInput;
    const bool visible = HasVisibleViewport() && m_ViewportBounds[1].x > m_ViewportBounds[0].x &&
                         m_ViewportBounds[1].y > m_ViewportBounds[0].y;
    const unsigned navWindow = uiContext.NavWindow ? uiContext.NavWindow->ID : 0;
    const bool navTransferred = navWindow != m_ViewportNavWindow && navWindow &&
        viewportWindow && uiContext.NavWindow != viewportWindow;
    if (!visible || active || popup || text || navTransferred)
        SetViewportActive(false);
    m_ViewportNavWindow = navWindow;
    const InputRouting routing{
        GetWindow()->GetWindowID(),
        m_ViewportBounds[0].x - origin.x,
        m_ViewportBounds[0].y - origin.y,
        m_ViewportBounds[1].x - origin.x,
        m_ViewportBounds[1].y - origin.y,
        visible,
        m_ViewportActive,
        active || popup || text,
        active || popup || text || (io.WantCaptureKeyboard && !m_ViewportActive)};
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    const bool tracing = PreEditorLifecycle::PickTraceActive();
    unsigned traceFlags{}, traceMoveId{};
    if (tracing)
    {
        traceFlags = unsigned(active) | (unsigned(popup) << 1) | (unsigned(text) << 2) |
            (unsigned(viewBackground) << 3) | (unsigned(viewGesture) << 4) | (unsigned(visible) << 5) |
            (unsigned(m_ViewportForcused) << 6) | (unsigned(m_ViewportHovered) << 7);
        traceMoveId = viewportWindow ? viewportWindow->MoveId : 0;
        PreEditorLifecycle::PickTraceUI("ui_before", routing, -1, traceFlags, traceMoveId);
    }
    auto published = GetInputManager()->PublishRouting(routing);
    if (tracing) PreEditorLifecycle::PickTraceUI("ui_after", routing, bool(published), traceFlags, traceMoveId);
    if (!published)
#else
    if (auto published = GetInputManager()->PublishRouting(routing); !published)
#endif
        Log::GetCoreLogger()->error("RBS input routing rejected: {}", int(published.error()));
    // Paint only after this frame's UI transfers/cancellation have resolved.
    // Activation is authoritative; hover, capture and animation never own focus.
    const ImVec2 imageMin{m_ViewportBounds[0].x, m_ViewportBounds[0].y};
    const ImVec2 imageMax{m_ViewportBounds[1].x, m_ViewportBounds[1].y};
    const ImVec2 clipMin = viewportWindow ? ImMax(imageMin, viewportWindow->InnerClipRect.Min) : imageMin;
    const ImVec2 clipMax = viewportWindow ? ImMin(imageMax, viewportWindow->InnerClipRect.Max) : imageMin;
    // ImGui's renderer applies DisplayFramebufferScale to every draw list. Use
    // that exact transform, then apply monitor DPI once to the physical widths.
    const ImVec2 pixelScale = io.DisplayFramebufferScale;
    const float dpi = viewportWindow ? viewportWindow->Viewport->DpiScale : 1.f;
    const bool drawable = visible && m_CameraAvailable && viewportWindow &&
        viewportWindow->Active && !viewportWindow->Hidden &&
        std::isfinite(dpi) && dpi > 0.f &&
        std::isfinite(pixelScale.x) && pixelScale.x > 0.f &&
        std::isfinite(pixelScale.y) && pixelScale.y > 0.f &&
        clipMax.x > clipMin.x && clipMax.y > clipMin.y &&
        imageMax.x - imageMin.x > 3.f && imageMax.y - imageMin.y > 3.f;
    auto* borderDraw = viewportWindow ? viewportWindow->DrawList : nullptr;
#ifdef GENGINE_LIFECYCLE_VALIDATION
    const int firstBorderVertex = borderDraw ? borderDraw->VtxBuffer.Size : 0;
    const int firstBorderIndex = borderDraw ? borderDraw->IdxBuffer.Size : 0;
#endif
    float flashOpacity = 0.f, borderOpacity = 0.f;
    if (drawable)
    {
        // Put the complete ring inside the visible image, including the docked
        // window's inner clip. Filled strips avoid a centered stroke's clipped
        // half-width/AA fringe. Snap inward in the final renderer's pixel space.
        const auto displayOrigin = viewportWindow->Viewport->Pos;
        const ImVec2 minimum{
            displayOrigin.x + std::ceil((clipMin.x - displayOrigin.x) * pixelScale.x) / pixelScale.x,
            displayOrigin.y + std::ceil((clipMin.y - displayOrigin.y) * pixelScale.y) / pixelScale.y};
        const ImVec2 maximum{
            displayOrigin.x + std::floor((clipMax.x - displayOrigin.x) * pixelScale.x) / pixelScale.x,
            displayOrigin.y + std::floor((clipMax.y - displayOrigin.y) * pixelScale.y) / pixelScale.y};
        const auto ring = [&](float pixelsAt1x, ImU32 color)
        {
            if (maximum.x <= minimum.x || maximum.y <= minimum.y) return;
            const float pixels = (std::max)(1.f, std::round(pixelsAt1x * dpi));
            const float x = (std::min)(pixels / pixelScale.x, (maximum.x - minimum.x) * .5f);
            const float y = (std::min)(pixels / pixelScale.y, (maximum.y - minimum.y) * .5f);
            borderDraw->AddRectFilled(minimum, {maximum.x, minimum.y + y}, color);
            borderDraw->AddRectFilled({minimum.x, maximum.y - y}, maximum, color);
            borderDraw->AddRectFilled({minimum.x, minimum.y + y}, {minimum.x + x, maximum.y - y}, color);
            borderDraw->AddRectFilled({maximum.x - x, minimum.y + y}, {maximum.x, maximum.y - y}, color);
        };
        borderDraw->PushClipRect(clipMin, clipMax, false);
        // Opaque state indication is independent of the clock and Style.Alpha.
        // These are the owner's explicit palette, not the dark HeaderActive.
        borderOpacity = 1.f;
        ring(m_ViewportActive ? 2.f : 1.f,
             m_ViewportActive ? IM_COL32(56, 189, 248, 255) : IM_COL32(71, 85, 105, 255));
        if (m_ViewportActive && m_ViewportHighlightStart >= 0)
        {
            const float remaining = std::clamp(float(1.0 -
                (ImGui::GetTime() - m_ViewportHighlightStart) / .25), 0.f, 1.f);
            flashOpacity = remaining * remaining * (3.f - 2.f * remaining);
            // Only the extra emphasis fades; the 2px focused ring stays intact.
            ring(4.f, IM_COL32(125, 211, 252, int(std::round(255.f * flashOpacity))));
        }
        borderDraw->PopClipRect();
    }
#ifdef GENGINE_LIFECYCLE_VALIDATION
    m_ViewportHighlightOpacity = flashOpacity;
    if (!PreEditorLifecycle::ObserveViewportFocus(m_ViewportActive, drawable, m_ViewportActivations,
            m_ViewportHighlightStart, flashOpacity, borderOpacity, borderDraw,
            firstBorderVertex, firstBorderIndex,
            {imageMin.x, imageMin.y, imageMax.x, imageMax.y},
            {clipMin.x, clipMin.y, clipMax.x, clipMax.y}))
        FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Viewport focus border", {},
            "DrawList observation", {}, "Persistent border/activation/clip oracle failed"});
#endif
#ifdef GENGINE_RBS_MODULARITY_VALIDATION
    ValidatePacketAUi();
#endif
}

void RigidBodySimulationApp::DrawShaderPanel()
{
    ImGui::Begin("Custom shader reload");
    PublishPanelInputRegion();
    if (m_ShaderReloadGallery.material)
    {
        ImGui::TextUnformatted("Both cubes share a shader; the clone keeps its lower brightness.");
        if (ImGui::Button("Focus custom cubes") && m_CameraView == CameraView::Editor)
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
    PublishPanelInputRegion();
    if (m_MaterialGallery.uvMaterial)
    {
        ImGui::TextUnformatted("Plane / Cube / Sphere / Capsule / Torus / Diamond");
        ImGui::TextUnformatted("Same floor: UV front row, Triplanar second row");
        if (ImGui::Button("Focus material comparison") && m_CameraView == CameraView::Editor)
        {
            FocusMaterialCamera(m_EditorCamera_, {0, 2, -22}, .35f, 0, 38.f);
        }
        ImGui::Checkbox("Animate object transforms", &m_MaterialGallery.animate);
        constexpr const char* geometryNames[]{"Plane",   "Cube",  "Sphere",
                                              "Capsule", "Torus", "Diamond"};
        ImGui::Combo("Inspect geometry", &m_MaterialGallery.inspectedGeometry, geometryNames, 6);
        if (ImGui::Button("Focus selected geometry") && m_CameraView == CameraView::Editor)
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
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
        const auto filePoint = ImGui::GetCursorScreenPos();
#endif
        const bool fileOpen = ImGui::BeginMenu("File");
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
        PreEditorInput::NoteUIMenu(3, fileOpen, filePoint.x, filePoint.y,
                                   ImGui::CalcTextSize("File").x +
                                       2 * ImGui::GetStyle().ItemSpacing.x,
                                   ImGui::GetFrameHeight());
#endif
        if (fileOpen)
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

#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
        const auto shadowPoint = ImGui::GetCursorScreenPos();
#endif
        const bool shadowsOpen = ImGui::BeginMenu("Shadows");
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
        PreEditorInput::NoteUIMenu(0, shadowsOpen, shadowPoint.x, shadowPoint.y,
                                   ImGui::CalcTextSize("Shadows").x +
                                       2 * ImGui::GetStyle().ItemSpacing.x,
                                   ImGui::GetFrameHeight());
#endif
        if (shadowsOpen)
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
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
            PreEditorInput::NoteUIItem(2);
#endif
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
        DrawCameraMenu();
        ImGui::EndMenuBar();
    }
}

void RigidBodySimulationApp::DrawCameraMenu()
{
    if (!ImGui::BeginMenu("Cameras")) return;
    if (ImGui::MenuItem("Editor view", nullptr, m_CameraView == CameraView::Editor))
        (void)SetCameraView(CameraView::Editor);
    if (ImGui::MenuItem("Active Scene view", nullptr, m_CameraView == CameraView::ActiveScene))
        (void)SetCameraView(CameraView::ActiveScene);
    if (ImGui::MenuItem("End preview", nullptr, false, m_CameraView == CameraView::Preview))
        (void)EndCameraPreview();
    ImGui::Separator();
    auto active = m_ActiveScene->ActiveSceneCamera();
    for (std::size_t slot = 0; slot != m_SceneCameras.size(); ++slot)
    {
        if (!ImGui::BeginMenu(slot == 0 ? "Scene Camera A" : "Scene Camera B")) continue;
        const auto id = m_SceneCameras[slot];
        const bool live = bool(m_ActiveScene->RenderData().Resolve(id));
        if (ImGui::MenuItem("Select active", nullptr, active && *active && **active == id, live))
            (void)SelectSceneCamera(id);
        if (ImGui::MenuItem("Preview", nullptr,
                            m_CameraView == CameraView::Preview && m_PreviewCamera == id, live))
            (void)SetCameraView(CameraView::Preview, id);
        if (ImGui::MenuItem("Replace with fresh identity", nullptr, false, live))
            (void)RetireSceneCamera(slot, true);
        if (ImGui::MenuItem("Delete", nullptr, false, live))
            (void)RetireSceneCamera(slot, false);
        if (ImGui::MenuItem("Create from Editor pose", nullptr, false, !live))
            if (auto created = CreateSceneCamera(slot); !created)
                m_CameraDiagnostic = "Camera creation failed";
        ImGui::EndMenu();
    }
    if (!m_CameraDiagnostic.empty()) ImGui::TextWrapped("%s", m_CameraDiagnostic.c_str());
    if (!m_CameraUnavailableDiagnostic.empty())
        ImGui::TextWrapped("%s", m_CameraUnavailableDiagnostic.c_str());
    ImGui::EndMenu();
}

void RigidBodySimulationApp::DrawViewport()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
    //ImGui::ShowDemoWindow(&p_open);
    // A deliberate activation click requests focus once. Hover/animation never do.
    if (m_ViewportRequestFocus)
    {
        ImGui::SetNextWindowFocus();
        m_ViewportRequestFocus = false;
    }
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
    if (HasVisibleViewport() && m_CameraAvailable)
    {
        if (auto image = UI::FramebufferImage(*m_RenderTarget, m_ViewportSize.x, m_ViewportSize.y);
            !image)
            ReportFramebufferError("viewport image", image.error());
        else
        {
            const auto minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
            m_ViewportBounds[0] = {minimum.x, minimum.y};
            m_ViewportBounds[1] = {maximum.x, maximum.y};
        }
    }
    if (!m_CameraAvailable && !m_CameraUnavailableDiagnostic.empty())
        ImGui::TextWrapped("%s", m_CameraUnavailableDiagnostic.c_str());
    if (m_ViewportHighlightPending)
    {
        m_ViewportHighlightStart = ImGui::GetTime();
        m_ViewportHighlightPending = false;
    }
    if (m_CameraAvailable && !m_CameraDiagnostic.empty())
    {
        // Keep fallback/command feedback visible over the image without changing
        // the viewport extent or introducing an input-consuming widget.
        const ImVec2 position{m_ViewportBounds[0].x + 8.f, m_ViewportBounds[0].y + 8.f};
        const float wrap = (std::max)(1.f, m_ViewportBounds[1].x - position.x - 8.f);
        const auto size = ImGui::CalcTextSize(m_CameraDiagnostic.c_str(), nullptr, false, wrap);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled({position.x - 4.f, position.y - 4.f},
                            {position.x + size.x + 4.f, position.y + size.y + 4.f},
                            ImGui::GetColorU32(ImVec4(0, 0, 0, .8f)), 3.f);
        draw->AddText(nullptr, 0.f, position, ImGui::GetColorU32(ImGuiCol_Text),
                      m_CameraDiagnostic.c_str(), nullptr, wrap);
    }

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
