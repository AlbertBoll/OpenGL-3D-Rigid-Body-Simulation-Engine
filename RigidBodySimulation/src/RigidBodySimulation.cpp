#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Core/RuntimeAssets.h"
#include "Scene/_Scene.h"
#include <format>

#include <new>

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;

RigidBodySimulationApp::RigidBodySimulationApp(const Rbs::LaunchConfig& launch)
    : BaseApp(launch.engine), m_Launch(launch)
{
}

#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
namespace PreEditorLifecycle { void FinishFocusTrace(const char* reason); }
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
namespace PreEditorLifecycle { void FinishPickTrace(const char* reason) noexcept; }
#endif

RigidBodySimulationApp::~RigidBodySimulationApp()
{
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
    PreEditorLifecycle::FinishFocusTrace("application_teardown_before_trace_decision");
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PreEditorLifecycle::FinishPickTrace("application_teardown_before_first_pick");
#endif
    if (auto* input = GetInputManager())
        if (auto cancelled = input->CancelInput(InputCancelReason::OwnerRelease); !cancelled)
            Log::GetCoreLogger()->error("RBS input cancellation failed: {}",
                                        int(cancelled.error()));
    m_Subscriptions.reset();
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::Subscriptions))
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_13_APP_RELEASED");
    m_MaterialGallery = {};
    m_ShaderReloadGallery = {};
    m_AsyncTextureGallery = {};
    if (m_AudioSystem)
        m_AudioSystem->Shutdown();
    if (m_SceneResources)
    {
        if (auto current = GetEngineContext().MakeCurrent(); !current)
        {
            ReportPlatformError(current.error());
            std::terminate();
        }
        m_MeshLoads.reset(); // Cancel/join imports before registry or scene retirement.
        if (m_ActiveScene && m_EditorViewIdentity)
            if (auto retired = m_ActiveScene->RetireEditorViewIdentity(m_EditorViewIdentity); !retired)
                Log::GetCoreLogger()->error("Editor view retirement failed: {}", int(retired.error().code));
#ifdef GENGINE_LIFECYCLE_VALIDATION
        if (m_ActiveScene && !m_ActiveScene->RenderData().Resolve(m_EditorViewIdentity) &&
            GetInputManager()->PointerCapture() == InputLayer::None)
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_TEARDOWN_PASS transient_retired=true capture=none");
#endif
        m_EditorViewIdentity = {};
        m_ActiveScene.reset();
        m_EditorScene.reset();
        m_FrameSubmission.reset();
        m_SceneResources.reset();
    }
#ifdef GENGINE_RBS_MODULARITY_VALIDATION
    Log::GetCoreLogger()->info(
        "PRE_EDITOR_PACKET_A_SHUTDOWN_PASS subscriptions_before_state=true resources_before_context=true");
#endif
}

ApplicationInitializationResult
RigidBodySimulationApp::Initialize(const std::initializer_list<WindowProperties>& properties)
{
    if (auto initialized = InitializeValidationPlatform(properties); !initialized)
        return initialized;
    if (m_Launch.AsyncPerformanceEnabled())
        return {};
    if (auto resources = InitializeResources(); !resources)
        return resources;
    return InitializeScene();
}

ApplicationInitializationResult RigidBodySimulationApp::Initialize(const WindowProperties& prop)
{
    return Initialize(std::initializer_list<WindowProperties>{prop});
}

ApplicationInitializationResult RigidBodySimulationApp::InitializeResources()
{
    m_AudioSystem = CreateScopedPtr<Audio::AudioSystem>();
    if (auto audio = m_AudioSystem->Initialize(); !audio)
        return std::unexpected(audio.error());
    auto resources = SceneRenderResources::Create(GetEngineContext());
    if (!resources)
        return SceneFailure(resources.error());
    m_SceneResources = std::move(*resources);
    auto meshRoot = RuntimeAssets::TryFile("Models");
    if (!meshRoot)
        return std::unexpected(meshRoot.error());
    m_MeshRoot = *meshRoot;
    // Opt-in validation exercises the same action as the File menu.
    m_LoadBarrel = m_Launch.startupMesh == Rbs::StartupMesh::Barrel;
    auto submission = FrameSubmission::Create();
    if (!submission)
    {
        Log::GetCoreLogger()->error("{}", DescribeSubmissionError(submission.error()));
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "frame submission",
                                             DescribeSubmissionError(submission.error())});
    }
    m_FrameSubmission.emplace(std::move(*submission));
    return {};
}

void RigidBodySimulationApp::SetViewportActive(bool active, bool cancelInput)
{
    if (m_ViewportActive == active) return;
    m_ViewportActive = active;
    if (active)
    {
        m_ViewportRequestFocus = m_ViewportHighlightPending = true;
#ifdef GENGINE_LIFECYCLE_VALIDATION
        ++m_ViewportActivations;
#endif
        return;
    }
    m_ViewportRequestFocus = m_ViewportHighlightPending = false;
    m_ViewportHighlightStart = -1;
    m_ViewportActivationButtons = 0;
    m_ViewPickRequested = false;
    if (cancelInput)
        if (auto cancelled = GetInputManager()->CancelInput(InputCancelReason::Transfer, InputLayer::View);
            !cancelled)
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Viewport input", "",
                "focus transfer cancellation", "", "View owner cancellation failed"});
}

ApplicationInitializationResult RigidBodySimulationApp::ConnectApplicationEventHandlers()
{
    auto connections = std::unique_ptr<Rbs::Subscriptions>(new (std::nothrow) Rbs::Subscriptions);
    if (!connections)
        return std::unexpected(PlatformError{PlatformErrorCode::Allocation, "RBS input",
                                             "Could not allocate connection ownership"});
    auto& input = *GetInputManager();
    auto view = input.Subscribe(InputLayer::View,
                                [this](InputDelivery& delivery)
                                {
                                    const auto& event = delivery.event;
                                    const auto& route = GetInputManager()->Routing();
                                    const auto point = event.kind == InputKind::ButtonDown || event.kind == InputKind::Motion
                                        ? Math::Vector2{event.x, event.y} : GetInputManager()->GetPhysicalState().m_Mouse.GetPosition();
                                    const bool inside = route.visible && point.x >= route.left && point.x < route.right &&
                                        point.y >= route.top && point.y < route.bottom;
                                    const auto cameraInput = [this, &event](bool eligible)
                                    {
                                        if (auto result = m_EditorCamera_.OnViewInput(event,
                                                eligible && m_CameraView == CameraView::Editor); !result)
                                        {
                                            ReportPlatformError(result.error());
                                            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
                                                "Editor Camera input", "", "owned View gesture", "", result.error().message});
                                        }
                                    };
                                    if (event.cleanup)
                                    {
                                        const bool activation = m_ViewportActivationButtons != 0;
                                        if (event.kind == InputKind::Cancel)
                                        {
                                            if (const auto* error = GetInputManager()->NativePollFailure())
                                                FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
                                                    "Native input transition", "", error->operation, "", error->message});
                                            m_ViewPickRequested = false;
                                            m_ViewportActivationButtons = 0;
                                            if (event.cancellation == InputCancelReason::FocusLoss ||
                                                event.cancellation == InputCancelReason::Hidden ||
                                                event.cancellation == InputCancelReason::Transfer)
                                                SetViewportActive(false, false);
                                        }
                                        else if (event.kind == InputKind::ButtonUp)
                                            m_ViewportActivationButtons &= ~GENGINE_BUTTON(event.button);
                                        cameraInput(m_ViewportActive && !activation);
                                        return;
                                    }
                                    if (event.kind == InputKind::Motion ||
                                        event.kind == InputKind::ButtonDown ||
                                        event.kind == InputKind::Wheel)
                                    {
                                        const bool fresh = event.kind == InputKind::ButtonDown &&
                                            !GetInputManager()->GetViewState().m_Mouse.isButtonHeld(event.button);
                                        delivery.Handle();
                                        if (fresh && inside && !m_ViewportActive)
                                        {
                                            SetViewportActive(true);
                                            m_ViewportActivationButtons |= GENGINE_BUTTON(event.button);
                                        }
                                        else if (fresh && m_ViewportActivationButtons)
                                            m_ViewportActivationButtons |= GENGINE_BUTTON(event.button);
                                        const bool eligible = m_ViewportActive && !m_ViewportActivationButtons &&
                                            (inside || (!fresh && GetInputManager()->PointerCapture() == InputLayer::View));
                                        if (eligible && fresh && event.button == GENGINE_BUTTON_LEFT)
                                            m_ViewPickRequested = (event.modifiers & LeftAlt) == 0;
                                        if ((event.modifiers & LeftAlt) &&
                                            GetInputManager()->GetViewState().m_Mouse.m_CurrentButtons)
                                        {
                                            m_ViewportAltGestureConsumed = true;
                                            m_ViewPickRequested = false;
                                        }
                                        cameraInput(eligible);
                                        if (eligible && event.kind == InputKind::Wheel && m_CameraView == CameraView::Editor)
                                            m_EditorCamera_.OnMouseScroll(event.wheelY);
                                        return;
                                    }
                                    if (event.kind != InputKind::KeyDown)
                                        return;
                                    std::optional<ViewportMode> direction;
                                    switch (event.key)
                                    {
                                    case GENGINE_KEY_I:
                                        direction = ViewportMode::FRONT;
                                        break;
                                    case GENGINE_KEY_K:
                                        direction = ViewportMode::BACK;
                                        break;
                                    case GENGINE_KEY_J:
                                        direction = ViewportMode::LEFT;
                                        break;
                                    case GENGINE_KEY_L:
                                        direction = ViewportMode::RIGHT;
                                        break;
                                    case GENGINE_KEY_U:
                                        direction = ViewportMode::TOP;
                                        break;
                                    case GENGINE_KEY_O:
                                        direction = ViewportMode::BOTTOM;
                                        break;
                                    case GENGINE_KEY_TAB:
                                        direction = ViewportMode::DEFAULT;
                                        break;
                                    case GENGINE_KEY_W:
                                    case GENGINE_KEY_A:
                                    case GENGINE_KEY_S:
                                    case GENGINE_KEY_D:
                                    case GENGINE_KEY_Q:
                                    case GENGINE_KEY_E:
                                    case GENGINE_KEY_LALT:
                                    case GENGINE_KEY_RALT:
                                    case GENGINE_KEY_LCTRL:
                                    case GENGINE_KEY_RCTRL:
                                    case GENGINE_KEY_LSHIFT:
                                    case GENGINE_KEY_RSHIFT:
                                        delivery.Handle();
                                        if (m_ViewportActive && !m_ViewportActivationButtons)
                                        {
                                            if ((event.modifiers & LeftAlt) &&
                                                GetInputManager()->GetViewState().m_Mouse.m_CurrentButtons)
                                            {
                                                m_ViewportAltGestureConsumed = true;
                                                m_ViewPickRequested = false;
                                            }
                                            cameraInput(inside || GetInputManager()->PointerCapture() == InputLayer::View);
                                        }
                                        return;
                                    default:
                                        return;
                                    }
                                    delivery.Handle();
                                    if (m_ViewportActive && !m_ViewportActivationButtons &&
                                        !event.repeated && m_CameraView == CameraView::Editor)
                                        m_EditorCamera_.Initialize(*direction);
                                });
    auto game = input.Subscribe(InputLayer::Game,
                                [this](InputDelivery& delivery)
                                {
                                    const auto& event = delivery.event;
                                    if (event.cleanup || event.kind != InputKind::KeyDown)
                                        return;
                                    if (event.key != GENGINE_KEY_P && event.key != GENGINE_KEY_R &&
                                        event.key != GENGINE_KEY_SPACE)
                                        return;
                                    delivery.Handle();
                                    if (event.repeated)
                                        return;
                                    if (event.key == GENGINE_KEY_P)
                                        m_IsPause = true;
                                    else if (event.key == GENGINE_KEY_R)
                                        m_IsPause = false;
                                    else
                                        m_IsShowDebugBoundingBox = !m_IsShowDebugBoundingBox;
                                });
    auto focus = input.Observe([this](const InputEvent& event)
    {
        // Hover and capture release do not transfer activation. A routed UI press does.
        if (!event.cleanup && event.kind == InputKind::ButtonDown &&
            event.destination != InputLayer::View)
            SetViewportActive(false);
    });
    if (!view || !game || !focus)
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "RBS input",
                                             "Typed input subscription rejected"});
    connections->tokens[0] = std::move(*view);
    connections->tokens[1] = std::move(*game);
    connections->tokens[2] = std::move(*focus);
    m_Subscriptions = std::move(connections);
    return {};
}
