#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Scene/_Scene.h"
#include <format>

#include "Managers/AssetsManager.h"
#include <imgui/imgui.h>
#include <cmath>
#include <print>

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;
#if defined(GENGINE_INPUT_VALIDATION) && !defined(GENGINE_INPUT_CONTROL)
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
    if (!authored(m_ActiveScene->SetLocalPose(m_FrameCameraEntity, m_EditorCamera_.GetPosition(),
                                              m_EditorCamera_.GetOrientation())))
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

void RigidBodySimulationApp::Render()
{
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
        context.updateResources = {
            this, [](void* user)
            {
                return static_cast<RigidBodySimulationApp*>(user)->UpdateFrameResources();
            }};
        if (!context.visible)
            return FrameScheduler::Render(context);
        m_EditorCamera_.UpdateView();
        auto cameraId = m_ActiveScene->RenderData().Identify(m_FrameCameraEntity);
        if (!cameraId)
            return std::unexpected(ScheduleError{FrameStage::FreezeFrameInputs,
                                                 RenderExtractionError{{}, cameraId.error()}});
        const FrameCamera camera{*cameraId,
                                 m_EditorCamera_.GetViewMatrix(),
                                 m_EditorCamera_.GetProjection(),
                                 m_EditorCamera_.GetPosition(),
                                 0,
                                 0,
                                 static_cast<unsigned>(m_RenderTarget->GetWidth()),
                                 static_cast<unsigned>(m_RenderTarget->GetHeight())};
        FrameSubmissionDesc targets{*m_RenderTarget,
                                    *m_MousePickFrameBuffer,
                                    *m_PointShadowFrameBuffer,
                                    *m_CascadeShadowFrameBuffer,
                                    {}, // FrameScheduler supplies current renderer pipeline roles.
                                    m_ShadowCascadeLevels,
                                    m_EditorCamera_.GetFOV(),
                                    m_EditorCamera_.GetAspectRatio(),
                                    m_EditorCamera_.GetNearClip(),
                                    m_EditorCamera_.GetFarClip(),
                                    m_NearPlane,
                                    m_FarPlane};
        const auto mouse = GetInputManager()->GetViewState().m_Mouse.GetPosition();
        const auto& pickStorage = m_MousePickFrameBuffer->Buffer().Description();
        targets.pickingEnabled =
            GetInputManager()->GetViewState().m_Mouse.isButtonPressed(
                GEngineMouseCode::GENGINE_BUTTON_LEFT) &&
            ViewportPixelAt(mouse.x - m_ViewportBounds[0].x, mouse.y - m_ViewportBounds[0].y,
                            GetEditorViewportLogicalSize(), {pickStorage.Width, pickStorage.Height})
                .has_value();
#if defined(GENGINE_INPUT_VALIDATION) && !defined(GENGINE_INPUT_CONTROL)
        PreEditorInput::RecordInputPicking(targets.pickingEnabled);
#endif
        FrameSceneInput input{*m_ActiveScene, *m_SceneResources, *m_FrameSubmission,
                              targets,        m_PickTable,       {&camera, 1}};
        // Preserve Phase 45's input/viewport snapshot: read this frame's IDs
        // before BeginUI refreshes ImGui mouse state or authors new panel bounds.
        input.pickingReadback = {this, [](void* user) -> ScheduleResult
                                 {
                                     static_cast<RigidBodySimulationApp*>(user)->OnMouseClicked();
                                     return {};
                                 }};
        if (auto checked = ValidateBeforeSubmission(targets, camera); !checked)
            return std::unexpected(checked.error());
        auto submitted = FrameScheduler::Render(context, &input);
        if (auto checked = ValidateSubmittedFrame(bool(submitted), targets, camera); !checked)
            return std::unexpected(checked.error());
        return submitted;
    };
    if (auto result = render(); !result)
    {
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
    if (GetInputManager()->GetViewState().m_Mouse.isButtonPressed(
            GEngineMouseCode::GENGINE_BUTTON_LEFT))
    {
        const auto mouse = GetInputManager()->GetViewState().m_Mouse.GetPosition();
        const auto& storage = m_MousePickFrameBuffer->Buffer().Description();
        auto position =
            ViewportPixelAt(mouse.x - m_ViewportBounds[0].x, mouse.y - m_ViewportBounds[0].y,
                            GetEditorViewportLogicalSize(), {storage.Width, storage.Height});
        if (!position || !HasVisibleViewport())
            return;
        const int x = position->X, y = position->Y;
        auto pixel = m_MousePickFrameBuffer->ReadPixel(x, y);
        if (!pixel)
        {
            ReportFramebufferError("picking read", pixel.error());
            return;
        }
        m_HoveredEntity = {};
        if (*pixel != EntityPickTable::InvalidPixel)
        {
            auto id = m_ActiveScene->RenderData().ResolvePick(m_PickTable, *pixel);
            if (!id)
            {
                Log::GetCoreLogger()->warn("Stale/invalid picking result: {}", int(id.error()));
                return;
            }
            auto entity = m_ActiveScene->RenderData().Resolve(*id);
            if (!entity)
            {
                Log::GetCoreLogger()->warn("Picking entity: {}", int(entity.error()));
                return;
            }
            m_HoveredEntity = _Entity{*entity, m_ActiveScene.get()};
        }
        //GENGINE_CORE_INFO("Mouse Position: {}, {}; Entity {} has been clicked",x,y,m_HoveredEntity?m_HoveredEntity.GetName():"None");
        std::println("Mouse Position: {}, {}; Entity {} has been clicked", x, y,
                     m_HoveredEntity ? m_HoveredEntity.GetName() : "None");
        //std::cout << "Pixel Data: " << pixel_data << std::endl;
        //m_MousePickFrameBuffer->UnBind();
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
