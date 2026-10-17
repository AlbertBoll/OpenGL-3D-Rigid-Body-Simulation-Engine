#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Scene/_Scene.h"
#include "Physics/PhysicsSystem.h"
#include <new>
#include <format>

#include "Managers/AssetsManager.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <cmath>
#include <print>

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;
#ifdef GENGINE_LIFECYCLE_VALIDATION
namespace PreEditorLifecycle { bool GpuChecks(); }
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
#include <array>
namespace PreEditorLifecycle
{
    void PickTraceValues(const char*, std::int64_t = -1, const std::array<std::uint64_t, 12>& = {},
                         const std::array<double, 12>& = {}) noexcept;
    void FinishPickTrace(const char*) noexcept;
}
#endif
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
namespace PreEditorInput
{
    void RecordInputPicking(bool);
}
#endif

void RigidBodySimulationApp::Update(Timestep ts)
{
    if (!ValidateUpdate())
        return;

    if (m_Launch.AsyncPerformanceEnabled())
        return;
    if (!ReplayMaterialValidation())
        UpdateMaterialAnimation(ts);
    // ImGui reports subpixel extents; retain whole-pixel truncation for framebuffer resize.
    OnViewportResize(static_cast<int>(m_ViewportSize.x), static_cast<int>(m_ViewportSize.y));
    //auto entity = m_ActiveScene->FindEntityByName("sphere");

    //auto& relationship = entity.GetComponent<RelationshipComponent>();
    if (m_CameraView == CameraView::Editor)
        m_EditorCamera_.OnUpdate(ts);

    const auto authored = [this](const auto& result)
    {
        if (!result)
        {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Scene Transform",
                         std::to_string(static_cast<unsigned>(result.error().code)),
                         "authoritative Transform mutation", "",
                         "RBS authoring mutation rejected before commit"});
            return false;
        }
        if (!result->notification)
            ReportSubscriptionError(result->notification.error());
        return true;
    };
    auto skyRotation = m_SkyBoxEntity.Transform().EulerRotation;
    skyRotation.y += ts * 0.02f;
    if (!authored(m_ActiveScene->SetLocalRotation(m_SkyBoxEntity, skyRotation)))
        return;
    //std::cout << "time passes_ " << ts << std::endl;

    //static float angle = 0.f;
    //angle += ts * 0.4f;
    //glm::quat dq = glm::angleAxis(ts * 0.4f, Vec3f{ 0.f, 1.f, 0.f });

    //for (auto& it : m_ActiveScene->GetAllEntitiesWith<Transform3DComponent, RigidBody3DComponent>())
    //{
    //	_Entity ent = { it, m_ActiveScene.get() };
    //	auto& rigidBody = ent.GetComponent<RigidBody3DComponent>();
    //
    //	if (rigidBody.Type != BodyType::Static)
    //	{
    //		auto& transform = ent.GetComponent<Transform3DComponent>();
    //		transform.QuatRotation = dq * transform.QuatRotation;
    //		//transform.SetRotation(q);
    //	}
    //}

    // Scene accumulates elapsed time and owns the bounded fixed physics ticks.
    m_ActiveScene->SetPaused(m_IsPause);
    m_ActiveScene->Update(ts);

    //update entities
    /*auto view = m_ActiveScene->GetAllEntitiesWith<Transform3DComponent>();
	for (auto e : view)
	{

		_Entity entity{ e, m_ActiveScene.get()};
		auto& transform = entity.GetComponent<Transform3DComponent>();

		transform.EulerRotation.y += ts * 1;

		transform.SetRotation(transform.EulerRotation);

	}*/

    /*auto entity = m_ActiveScene->FindEntityByName("sphere");


	GENGINE_INFO("{}, {}", entity.GetName(), entity.GetUUID());*/
    //m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

    //m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);

    //switch (m_SceneState)
    //{
    //case SceneState::Edit:
    //{
    //	if (m_ViewportFocused)
    //		m_EditorCamera.OnUpdate(ts);
    //

    //	m_EditorCamera.OnUpdate(ts);

    //	m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);
    //	break;
    //}
    //case SceneState::Simulate:
    //{
    //	m_EditorCamera.OnUpdate(ts);

    //	//m_ActiveScene->OnUpdateSimulation(ts, m_EditorCamera);
    //	break;
    //}
    //case SceneState::Play:
    //{
    //	m_ActiveScene->OnUpdateRuntime(ts);
    //	break;
    //}
    //}
}

void RigidBodySimulationApp::UpdateMaterialAnimation(Timestep ts)
{
    const auto authored = [this](const auto& result)
    {
        if (!result)
        {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Scene Transform",
                         std::to_string(static_cast<unsigned>(result.error().code)),
                         "authoritative Transform mutation", "",
                         "RBS authoring mutation rejected before commit"});
            return false;
        }
        if (!result->notification)
            ReportSubscriptionError(result->notification.error());
        return true;
    };
    if (m_MaterialGallery.parent && m_MaterialGallery.animate)
    {
        const bool validation =
            m_Launch.validation.Includes(Rbs::ValidationCheck::MaterialAuthoring);
        m_MaterialGallery.time += validation ? .05f : float(ts);
        const float t = m_MaterialGallery.time;
        if (!authored(m_ActiveScene->SetLocalTranslation(
                m_MaterialGallery.parent, {.5f * std::sin(t), 0, .5f * std::cos(t)})) ||
            !authored(m_ActiveScene->SetLocalRotation(m_MaterialGallery.parent,
                                                      {0, .04f * std::sin(t), 0})))
            return;
        for (std::size_t i = 0; i < m_MaterialGallery.uv.size(); ++i)
            for (auto entity : {m_MaterialGallery.uv[i], m_MaterialGallery.triplanar[i]})
            {
                if (!authored(m_ActiveScene->SetLocalRotation(
                        entity, {.3f * std::sin(t), t, .2f * std::cos(t)})) ||
                    !authored(m_ActiveScene->SetLocalScale(
                        entity,
                        validation && m_MaterialGallery.validationFrames < 30
                            ? Vec3f(1.f + .2f * std::sin(t))
                            : Vec3f{1.f + .2f * std::sin(t), 1.f + .15f * std::cos(t), 1.f})))
                    return;
            }
        if (validation && m_MaterialGallery.validationFrames == 59)
        {
            m_MaterialGallery.animate = false;
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_09_MOTION_COMPLETE frames=60 local_mapping=true parent_motion=true positive_nonuniform_scale=true visual_result=pending");
        }
    }
}

bool RigidBodySimulationApp::CancelCameraGesture()
{
    const auto position = m_EditorCamera_.GetPosition();
    const auto pitch = m_EditorCamera_.m_Pitch + m_EditorCamera_.m_PitchDelta;
    const auto yaw = m_EditorCamera_.m_Yaw + m_EditorCamera_.m_YawDelta;
    const bool pendingMovement = m_EditorCamera_.m_PositionDelta != Vec3f{} ||
        m_EditorCamera_.m_PitchDelta != 0 || m_EditorCamera_.m_YawDelta != 0;
    if (auto cancelled = GetInputManager()->CancelInput(InputCancelReason::OwnerRelease); !cancelled)
    {
        m_CameraDiagnostic = std::format("Camera switch rejected: input cancellation {}", int(cancelled.error()));
        return false;
    }
    // Angular damping participates in the controller's current orientation. Consume
    // cancellation, then retain that effective pose with no pending movement.
    m_EditorCamera_.OnUpdate(Timestep(0));
    m_EditorCamera_.m_Position = position;
    m_EditorCamera_.m_Pitch = pitch;
    m_EditorCamera_.m_Yaw = yaw;
    if (pendingMovement)
        m_EditorCamera_.m_FocalPoint = position +
            m_EditorCamera_.GetForwardDirection() * m_EditorCamera_.GetDistance();
    m_FramePickPixel.reset();
    return true;
}

bool RigidBodySimulationApp::SetCameraView(CameraView role, EntityRenderId preview)
{
    if (role == CameraView::Editor)
    {
        if (auto valid = m_ActiveScene->ValidateEditorViewIdentity(m_EditorViewIdentity); !valid)
        {
            m_CameraDiagnostic = "Editor view identity is unavailable";
            return false;
        }
    }
    else
    {
        auto active = m_ActiveScene->ActiveSceneCamera();
        if (role == CameraView::ActiveScene && (!active || !*active))
        {
            m_CameraDiagnostic = "No valid active Scene Camera";
            return false;
        }
        const auto id = role == CameraView::Preview ? preview : **active;
        auto access = m_SceneResources->Publication().BeginFrame();
        if (auto valid = m_ActiveScene->SnapshotSceneCamera(id, m_SceneResources->ForFrame(access), {}); !valid)
        {
            m_CameraDiagnostic = std::format("Camera binding rejected: {}", int(valid.error().code));
            return false;
        }
    }
    if (!CancelCameraGesture()) return false;
    if (role == CameraView::Preview && m_CameraView != CameraView::Preview)
        m_PreviewReturnView = m_CameraView;
    m_CameraView = role;
    m_PreviewCamera = role == CameraView::Preview ? preview : EntityRenderId{};
    m_CameraDiagnostic.clear();
    return true;
}

bool RigidBodySimulationApp::EndCameraPreview()
{
    if (m_CameraView != CameraView::Preview) return true;
    CameraView restore = CameraView::Editor;
    if (m_PreviewReturnView == CameraView::ActiveScene)
    {
        auto active = m_ActiveScene->ActiveSceneCamera();
        if (active && *active)
        {
            auto access = m_SceneResources->Publication().BeginFrame();
            if (m_ActiveScene->SnapshotSceneCamera(**active, m_SceneResources->ForFrame(access), {}))
                restore = CameraView::ActiveScene;
        }
    }
    // Choose the valid fallback before cancelling. A cancellation failure is
    // reported once and must not be retried as a different role transition.
    return SetCameraView(restore);
}

bool RigidBodySimulationApp::SelectSceneCamera(EntityRenderId id)
{
    auto access = m_SceneResources->Publication().BeginFrame();
    auto resources = m_SceneResources->ForFrame(access);
    if (auto valid = m_ActiveScene->SnapshotSceneCamera(id, resources, {}); !valid)
    {
        m_CameraDiagnostic = std::format("Scene selection rejected: {}", int(valid.error().code));
        return false;
    }
    if (m_CameraView == CameraView::ActiveScene && !CancelCameraGesture()) return false;
    if (auto selected = m_ActiveScene->SetActiveSceneCamera(id, resources); !selected)
    {
        m_CameraDiagnostic = std::format("Scene selection rejected: {}", int(selected.error().code));
        return false;
    }
    m_CameraDiagnostic.clear();
    return true;
}

bool RigidBodySimulationApp::RetireSceneCamera(std::size_t slot, bool replace)
{
    const auto old = m_SceneCameras[slot];
    auto source = m_ActiveScene->RenderData().Resolve(old);
    if (!source) { m_CameraDiagnostic = "Scene Camera has already retired"; return false; }
    const auto active = m_ActiveScene->ActiveSceneCamera();
    const bool bound = (m_CameraView == CameraView::Preview && m_PreviewCamera == old) ||
        (m_CameraView == CameraView::ActiveScene && active && *active && **active == old);
    if (bound && !CancelCameraGesture()) return false;
    std::expected<void, SceneCameraError> result;
    EntityRenderId replacement;
    if (replace)
    {
        auto copied = m_ActiveScene->DuplicateEntity(_Entity{*source, m_ActiveScene.get()});
        if (!copied) { m_CameraDiagnostic = "Camera replacement creation failed"; return false; }
        auto identity = m_ActiveScene->RenderData().Identify(*copied);
        if (identity)
        {
            replacement = *identity;
            auto access = m_SceneResources->Publication().BeginFrame();
            result = m_ActiveScene->ReplaceSceneCamera(old, replacement, m_SceneResources->ForFrame(access));
        }
        if (!identity || !result)
        {
            (void)m_ActiveScene->DestroyEntity(*copied);
            m_CameraDiagnostic = "Camera replacement rejected; original retained";
            return false;
        }
    }
    else result = m_ActiveScene->DeleteSceneCamera(old);
    if (!result)
    {
        m_CameraDiagnostic = std::format("Camera retirement rejected: {}", int(result.error().code));
        return false;
    }
    m_SceneCameras[slot] = replacement;
    // The next frame resolves exact references, including preview and active fallback.
    return true;
}

bool RigidBodySimulationApp::BindCameraScene(RefPtr<_Scene> scene)
{
    // Camera part of the application's scene-owner transition. The caller must
    // finish its other authoring bindings before the next application Update.
    if (!scene) { m_CameraDiagnostic = "Camera binding requires a Scene"; return false; }
    if (scene == m_ActiveScene) return true;
    auto view = scene->CreateEditorViewIdentity();
    if (!view) { m_CameraDiagnostic = "New Scene view identity unavailable"; return false; }
    if (!CancelCameraGesture()) { (void)scene->RetireEditorViewIdentity(*view); return false; }
    if (m_ActiveScene && m_EditorViewIdentity)
        if (auto retired = m_ActiveScene->RetireEditorViewIdentity(m_EditorViewIdentity); !retired)
        {
            (void)scene->RetireEditorViewIdentity(*view);
            m_CameraDiagnostic = "Previous Scene view retirement failed";
            return false;
        }
    m_ActiveScene = std::move(scene);
    m_EditorViewIdentity = *view;
    m_SceneCameras = {};
    m_PreviewCamera = {};
    m_CameraView = m_PreviewReturnView = CameraView::Editor;
    m_HoveredEntity = {};
    m_CameraDiagnostic.clear();
    return true;
}

ScheduleResult RigidBodySimulationApp::PrepareCameraFrame(RenderContext& context,
                                                          FrameSceneInput& input, FrameCamera& camera)
{
    const bool requestedPick = std::exchange(m_ViewPickRequested, false);
    m_CameraSnapshot.reset();
    m_FramePickPixel.reset();
    m_CameraAvailable = false;
    m_CameraUnavailableDiagnostic.clear();
    if (!context.visible) return {};
    EntityRenderId id = m_EditorViewIdentity;
    if (m_CameraView == CameraView::Preview)
    {
        if (!m_ActiveScene->RenderData().Resolve(m_PreviewCamera))
        {
            if (!EndCameraPreview())
                return std::unexpected(ScheduleError{FrameStage::FreezeFrameInputs,
                    SubmissionError{m_CameraDiagnostic, SubmissionCode::InvalidCamera}});
            m_CameraDiagnostic = "Preview camera retired; previous valid view restored";
        }
        else id = m_PreviewCamera;
    }
    if (m_CameraView == CameraView::ActiveScene)
    {
        auto active = m_ActiveScene->ActiveSceneCamera();
        if (!active || !*active)
        {
            if (!SetCameraView(CameraView::Editor))
                return std::unexpected(ScheduleError{FrameStage::FreezeFrameInputs,
                    SubmissionError{m_CameraDiagnostic, SubmissionCode::InvalidCamera}});
            m_CameraDiagnostic = "Active Scene Camera unavailable; Editor view restored";
        }
        else id = **active;
    }
    const auto logical = GetEditorViewportLogicalSize();
    CameraViewport viewport;
    viewport.logicalOrigin = m_ViewportBounds[0];
    viewport.logicalSize = {logical.Width, logical.Height};
    viewport.pixelSize = {m_RenderTarget->GetWidth(), m_RenderTarget->GetHeight()};
    if (m_CameraView == CameraView::Editor)
    {
        id = m_EditorViewIdentity;
        if (!m_ActiveScene->ValidateEditorViewIdentity(id))
        {
            context.visible = false;
            m_CameraUnavailableDiagnostic = "Editor view unavailable: retired identity";
            return {};
        }
        CameraSnapshotDesc desc;
        desc.position = m_EditorCamera_.GetPosition();
        desc.orientation = m_EditorCamera_.GetOrientation();
        desc.verticalFovRadians = m_EditorCamera_.GetFOV();
        desc.nearPlane = m_EditorCamera_.GetNearClip();
        desc.farPlane = m_EditorCamera_.GetFarClip();
        desc.viewport = viewport;
#ifdef GENGINE_CAMERA_VALIDATION
        desc.projection = m_CameraValidationIntent.projection;
        desc.verticalFovRadians = m_CameraValidationIntent.verticalFovRadians;
        desc.orthographicHeight = m_CameraValidationIntent.orthographicHeight;
#endif
        m_EditorCamera_.UpdateView();
        auto snapshot = CameraSnapshot::Create(desc);
        if (snapshot) m_CameraSnapshot = *snapshot;
        else m_CameraUnavailableDiagnostic = std::format("Editor view unavailable: {}", int(snapshot.error().code));
    }
    else
    {
        // This callback runs after upload/publication and before extraction. Its
        // temporary access ends here; no authoring or Physics update intervenes.
        auto access = m_SceneResources->Publication().BeginFrame();
        auto snapshot = m_ActiveScene->SnapshotSceneCamera(id, m_SceneResources->ForFrame(access), viewport);
        if (snapshot) m_CameraSnapshot = *snapshot;
        else m_CameraUnavailableDiagnostic = std::format("Scene view unavailable: camera error {}", int(snapshot.error().code));
    }
    if (!m_CameraSnapshot) { context.visible = false; return {}; }
    m_CameraAvailable = true;
    camera = FrameCamera::FromSnapshot(id, *m_CameraSnapshot);
    const auto& desc = m_CameraSnapshot->Description();
    input.targets.cameraFov = static_cast<float>(desc.verticalFovRadians);
    input.targets.cameraAspect = float(camera.viewportWidth) / camera.viewportHeight;
    input.targets.cameraNear = static_cast<float>(desc.nearPlane);
    input.targets.cameraFar = static_cast<float>(desc.farPlane);
    const auto& mouse = GetInputManager()->GetViewState().m_Mouse;
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PreEditorLifecycle::PickTraceValues("pick_prepare", -1,
        {m_FramePickPixel.has_value(), m_FramePickPixel ? m_FramePickPixel->x : 0,
         m_FramePickPixel ? m_FramePickPixel->y : 0},
        {desc.viewport.logicalOrigin.x, desc.viewport.logicalOrigin.y,
         desc.viewport.logicalSize.x, desc.viewport.logicalSize.y,
         double(desc.viewport.pixelSize.x), double(desc.viewport.pixelSize.y)});
#endif
    if (requestedPick && m_ViewportActive && !m_ViewportActivationButtons &&
        mouse.isButtonPressed(GEngineMouseCode::GENGINE_BUTTON_LEFT))
    {
        const auto point = mouse.GetPosition();
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        auto pixel = m_CameraSnapshot->ReadbackPixelAt({point.x, point.y});
        PreEditorLifecycle::PickTraceValues("pick_conversion", bool(pixel),
            {pixel ? pixel->x : 0, pixel ? pixel->y : 0, pixel ? 0 : std::uint64_t(pixel.error().code) + 1});
        if (pixel)
#else
        if (auto pixel = m_CameraSnapshot->ReadbackPixelAt({point.x, point.y}); pixel)
#endif
            m_FramePickPixel = *pixel;
    }
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PreEditorLifecycle::PickTraceValues("pick_ready", m_FramePickPixel.has_value(),
        {m_FramePickPixel ? m_FramePickPixel->x : 0, m_FramePickPixel ? m_FramePickPixel->y : 0});
#endif
    input.targets.pickingEnabled = m_FramePickPixel.has_value();
#ifdef GENGINE_CAMERA_VALIDATION
    input.targets.pickingEnabled = true;
#endif
#ifdef GENGINE_LIFECYCLE_VALIDATION
    if (PreEditorLifecycle::GpuChecks()) input.targets.pickingEnabled = true;
#endif
#if (defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION)) && !defined(GENGINE_INPUT_CONTROL)
    PreEditorInput::RecordInputPicking(input.targets.pickingEnabled);
#endif
    return ValidateBeforeSubmission(input.targets, camera);
}

void RigidBodySimulationApp::Render()
{
    if (m_ViewportAltGestureConsumed)
        ImGui::GetCurrentContext()->NavWindowingToggleLayer = false;
    if (m_Launch.AsyncPerformanceEnabled())
        return;
    RenderContext context{*GetWindow(), *GetWindowManager(), m_RenderTarget.get(),
                          HasVisibleViewport()};
    context.editorUI = {this, [](void* user) -> ScheduleResult
                        {
                            auto& app = *static_cast<RigidBodySimulationApp*>(user);
                            app.ImGuiRender();
                            return {};
                        }};
    auto render = [&]() -> std::expected<ScheduledFrameStats, ScheduleError>
    {
        if (auto prepared = PrepareFrameResources(context); !prepared)
            return std::unexpected(prepared.error());
        FrameCamera camera;
        FrameSubmissionDesc targets{*m_RenderTarget,
                                    *m_MousePickFrameBuffer,
                                    *m_PointShadowFrameBuffer,
                                    *m_CascadeShadowFrameBuffer,
                                    {}, // FrameScheduler supplies current renderer pipeline roles.
                                    m_ShadowCascadeLevels,
                                    glm::radians(45.f), 1.f, .1f, 1000.f,
                                    m_NearPlane,
                                    m_FarPlane};
        FrameSceneInput input{*m_ActiveScene, *m_SceneResources, *m_FrameSubmission,
                              targets,        m_PickTable,       {&camera, 1}};
        // Preserve Phase 45's input/viewport snapshot: read this frame's IDs
        // before BeginUI refreshes ImGui mouse state or authors new panel bounds.
        input.pickingReadback = {this, [](void* user) -> ScheduleResult
                                 {
                                     static_cast<RigidBodySimulationApp*>(user)->OnMouseClicked();
                                     return {};
                                 }};
        struct Preparation { RigidBodySimulationApp& app; RenderContext& context;
                             FrameSceneInput& input; FrameCamera& camera;
                             std::unique_ptr<FrameDebugLine[]> debug; } preparation{*this, context, input, camera};
        context.updateResources = {&preparation, [](void* user) -> ScheduleResult
        {
            auto& p = *static_cast<Preparation*>(user);
            if (auto updated = p.app.UpdateFrameResources(); !updated) return updated;
            if (auto camera=p.app.PrepareCameraFrame(p.context,p.input,p.camera);!camera) return camera;
            if (p.context.visible && p.app.m_IsShowDebugBoundingBox) {
                constexpr std::size_t capacity=32768;
                p.debug.reset(new(std::nothrow) FrameDebugLine[capacity]);
                if (!p.debug) return std::unexpected(ScheduleError{FrameStage::BuildRenderFrame,
                    SubmissionError{"physics debug storage",SubmissionCode::Allocation}});
                DebugGeometry geometry({p.debug.get(),capacity});
                if (auto result=p.app.m_ActiveScene->GetPhysicsSystem()->AppendDebugGeometry(geometry);!result)
                    return std::unexpected(ScheduleError{FrameStage::BuildRenderFrame,
                        SubmissionError{std::format("physics debug geometry code={}",int(result.error())),SubmissionCode::InvalidDraw}});
                p.input.debugLines=geometry.Lines();
            }
            return {};
        }};
        auto submitted = FrameScheduler::Render(context, &input);
        if (context.visible && m_CameraSnapshot)
            if (auto checked = ValidateSubmittedFrame(bool(submitted), input.targets, camera); !checked)
                return std::unexpected(checked.error());
        return submitted;
    };
    if (auto result = render(); !result)
    {
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        PreEditorLifecycle::FinishPickTrace("submission_failure_before_first_pick");
#endif
        if (const auto* sceneError = std::get_if<SceneError>(&result.error().cause))
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Scene",
                         std::to_string(static_cast<unsigned>(sceneError->code)),
                         std::string(sceneError->operation),
                         std::format("entity={}", sceneError->entity),
                         DescribeScheduleError(result.error())});
        else
        {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
                         "Rendering",
                         std::to_string(static_cast<unsigned>(result.error().stage)),
                         "render frame",
                         {},
                         DescribeScheduleError(result.error())});
        }
    }
}

void RigidBodySimulationApp::OnMouseClicked()
{
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PreEditorLifecycle::PickTraceValues("pick_readback_enter", -1,
        {m_FramePickPixel.has_value(), m_CameraSnapshot.has_value(),
         m_FramePickPixel ? m_FramePickPixel->x : 0, m_FramePickPixel ? m_FramePickPixel->y : 0});
#endif
    if (m_FramePickPixel && m_CameraSnapshot)
    {
        const int x = static_cast<int>(m_FramePickPixel->x);
        const int y = static_cast<int>(m_FramePickPixel->y);
        m_FramePickPixel.reset();
#ifdef GENGINE_LIFECYCLE_VALIDATION
        ++m_GesturePickReadbacks;
#endif
        auto pixel = m_MousePickFrameBuffer->ReadPixel(x, y);
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        PreEditorLifecycle::PickTraceValues("pick_readback_result", bool(pixel),
            {pixel ? 0 : std::uint64_t(pixel.error().code) + 1}, {pixel ? double(*pixel) : 0});
#endif
        if (!pixel)
        {
            ReportFramebufferError("picking read", pixel.error());
            return;
        }
        m_HoveredEntity = {};
        if (*pixel != EntityPickTable::InvalidPixel)
        {
            auto id = m_ActiveScene->RenderData().ResolvePick(m_PickTable, *pixel);
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
            PreEditorLifecycle::PickTraceValues("pick_resolve", bool(id),
                {id ? id->index : 0, id ? id->generation : 0, id ? id->registry : 0,
                 id ? 0 : std::uint64_t(id.error()) + 1});
#endif
            if (!id)
            {
                Log::GetCoreLogger()->warn("Stale/invalid picking result: {}", int(id.error()));
                return;
            }
            auto entity = m_ActiveScene->RenderData().Resolve(*id);
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
            PreEditorLifecycle::PickTraceValues("pick_entity", bool(entity),
                {entity ? std::uint64_t(*entity) : 0, entity ? 0 : std::uint64_t(entity.error()) + 1});
#endif
            if (!entity)
            {
                Log::GetCoreLogger()->warn("Picking entity: {}", int(entity.error()));
                return;
            }
            m_HoveredEntity = _Entity{*entity, m_ActiveScene.get()};
        }
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        PreEditorLifecycle::PickTraceValues("pick_hovered", bool(m_HoveredEntity),
            {static_cast<std::uint32_t>(m_HoveredEntity)});
#endif
        Log::GetCoreLogger()->info("Mouse Position: {}, {}; Entity {} has been clicked", x, y,
                                   m_HoveredEntity ? m_HoveredEntity.GetName() : "None");
    }
}

void RigidBodySimulationApp::FilledKDTreePoints()
{
    //auto& entities = m_ActiveScene->GetAllEntitiesWith<Transform3DComponent, RigidBody3DComponent>();
    /*for (auto& e : m_ActiveScene->GetAllEntitiesWith<Transform3DComponent, RigidBody3DComponent>())
	{
		_Entity entity{ e, m_ActiveScene.get() };
		auto& transform = entity.GetComponent<Transform3DComponent>();
		m_KDTreePoints.push_back(transform.Translation);

	}*/
}

void RigidBodySimulationApp::OnViewportResize(int, int)
{
    // Allocation is coalesced by BaseApp once per frame, before application work.
    // Use successful target storage, never native-window or fractional panel size.
    if (!HasVisibleViewport())
        return;
    const auto width = m_RenderTarget->GetWidth(), height = m_RenderTarget->GetHeight();
    if (!width || !height)
        return;
    m_EditorCamera_.SetViewportSize(static_cast<float>(width), static_cast<float>(height));
}
