#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Scene/_Scene.h"
#include <format>

#include <chrono>
#include <cmath>
#ifdef GENGINE_INPUT_VALIDATION
#include "../tests/InputRoutingChecks.h"
#endif
#ifdef GENGINE_RBS_SCENE_VALIDATION
#include "Physics/PhysicsBody.h"
#include <algorithm>
#include <map>
#include <iterator>
#endif
#include "../tests/SceneAuthoringChecks.h"
#include "../tests/ResourceOwnershipChecks.h"
#include "../tests/GeometryTemplateChecks.h"
#include "../tests/ParametricGeometryChecks.h"
#include "../tests/GeometryAuthoringChecks.h"
#include "../tests/MaterialAuthoringChecks.h"
#include "../tests/ShaderDescriptionChecks.h"
#include "../tests/ShaderReloadChecks.h"
#include "../tests/AsyncResourceChecks.h"
#include "../tests/TypedSubscriptionChecks.h"
#ifdef GENGINE_RBS_TRANSFORM_VALIDATION
#include "../tests/TransformMutationChecks.h"
#endif
#if defined(GENGINE_RBS_MODULARITY_VALIDATION) || defined(GENGINE_RBS_SCENE_VALIDATION)
#include "../tests/LaunchConfigChecks.h"
#endif
#ifdef GENGINE_RBS_MODULARITY_VALIDATION
#include <imgui/imgui_internal.h>
#include <fstream>
#endif

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;

ApplicationInitializationResult RigidBodySimulationApp::InitializeValidationPlatform(
    const std::initializer_list<WindowProperties>& WindowsPropertyList)
{
    if (m_Launch.scene != Rbs::RbsScenePreset::GeometryGallery)
        for (const auto check :
             {Rbs::ValidationCheck::GeometryAuthoring, Rbs::ValidationCheck::MaterialAuthoring,
              Rbs::ValidationCheck::ShaderDescriptions, Rbs::ValidationCheck::ShaderReload,
              Rbs::ValidationCheck::AsyncResources, Rbs::ValidationCheck::Subscriptions})
            if (m_Launch.validation.Includes(check))
                return std::unexpected(PlatformError{
                    PlatformErrorCode::Initialization, "RBS validation target",
                    "Selected authoring validation requires the GeometryGallery preset"});
#if defined(GENGINE_RBS_MODULARITY_VALIDATION) || defined(GENGINE_RBS_SCENE_VALIDATION)
    const auto launchChecks = PreEditorValidation::CheckLaunchConfig();
    if (!launchChecks)
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                                             "Packet A launch configuration",
                                             launchChecks.error()});
#endif
    if (m_Launch.AsyncPerformanceEnabled())
    {
        WindowProperties properties;
        properties.m_Title = "Phase 12 async diagnostic";
        properties.flag = {WindowFlags::INVISIBLE, WindowFlags::BORDERLESS};
        properties.m_IsVsync = false;
        properties.m_Width = properties.m_Height = properties.m_MinWidth = properties.m_MinHeight =
            64;
        if (auto initialized = BaseApp::Initialize({properties}); !initialized)
            return initialized;
        if (auto checked = PreEditorValidation::RunAsyncPerformance(
                GetEngineContext(),
                m_Launch.validation.asyncPerformance->workload == Rbs::AsyncWorkload::AdmissionOnly,
                m_Launch.validation.asyncPerformance->observation == Rbs::Observation::Observed);
            !checked)
            return std::unexpected(checked.error());
        return {};
    }
    std::size_t cpuMetadataChecks = 0;
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::ResourceOwnership))
    {
        auto checked = PreEditorValidation::CheckCpuMeshMetadata();
        if (!checked)
            return std::unexpected(PlatformError{
                PlatformErrorCode::Initialization,
                "Phase 05 CPU metadata before platform initialization", checked.error()});
        cpuMetadataChecks = *checked;
    }
    std::size_t cpuGeometryChecks = 0;
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::GeometryTemplates))
    {
        auto checked = PreEditorValidation::CheckCpuGeometryTemplates();
        if (!checked)
            return std::unexpected(PlatformError{
                PlatformErrorCode::Initialization,
                "Phase 06 CPU geometry before platform initialization", checked.error()});
        cpuGeometryChecks = *checked;
    }
    std::size_t cpuParametricChecks = 0;
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::ParametricGeometry))
    {
        auto checked = PreEditorValidation::CheckCpuParametricGeometry();
        if (!checked)
            return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                                                 "Phase 07 CPU parametric geometry",
                                                 checked.error()});
        cpuParametricChecks = *checked;
    }
    if (auto initialized = BaseApp::Initialize(WindowsPropertyList); !initialized)
        return initialized;
#if defined(GENGINE_RBS_MODULARITY_VALIDATION) || defined(GENGINE_RBS_SCENE_VALIDATION)
    Log::GetCoreLogger()->info("PRE_EDITOR_PACKET_A_CONFIG_PASS checks={}", *launchChecks);
#endif
    if (cpuMetadataChecks)
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_05_CPU_PASS checks={} before_platform=true",
                                   cpuMetadataChecks);
    if (cpuGeometryChecks)
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_06_CPU_PASS checks={} before_platform=true",
                                   cpuGeometryChecks);
    if (cpuParametricChecks)
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_07_CPU_PASS checks={} before_platform=true",
                                   cpuParametricChecks);
    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::ValidateRuntimeScene(const Rbs::SceneAssets& assets)
{
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::SceneAuthoring))
    {
        auto checked = PreEditorValidation::CheckSceneAuthoring(
            *m_ActiveScene, *m_SceneResources, assets.boxMesh, assets.smoothSphereGeo,
            assets.boxMaterial, assets.sphereMaterial);
        if (!checked)
            return std::unexpected(checked.error());
    }

    if (m_Launch.validation.Includes(Rbs::ValidationCheck::ResourceOwnership))
    {
        auto checked = PreEditorValidation::CheckResourceOwnership(
            *m_SceneResources, assets.boxMesh, assets.smoothSphereGeo, assets.boxMaterial,
            assets.sphereMaterial);
        if (!checked)
            return std::unexpected(checked.error());
    }

    if (m_Launch.validation.Includes(Rbs::ValidationCheck::GeometryTemplates))
    {
        auto checked = PreEditorValidation::CheckGeometryPublication(
            GetEngineContext(), *m_SceneResources, assets.boxMesh, assets.floorMaterial);
        if (!checked)
            return std::unexpected(checked.error());
    }

    if (m_Launch.validation.Includes(Rbs::ValidationCheck::ParametricGeometry))
    {
        auto checked = PreEditorValidation::CheckParametricPublication(
            GetEngineContext(), *m_SceneResources, assets.boxMesh, assets.sphereMaterial,
            assets.pointMaterial);
        if (!checked)
            return std::unexpected(checked.error());
    }

    if (m_Launch.validation.Includes(Rbs::ValidationCheck::GeometryAuthoring))
    {
        auto checked = PreEditorValidation::CheckGeometryAuthoring(
            GetEngineContext(), *m_ActiveScene, *m_SceneResources, assets.authoringMesh,
            assets.floorMaterial);
        if (!checked)
            return std::unexpected(checked.error());
    }

    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::ValidateScaleBridge(const Rbs::SceneAssets& assets)
{
#ifdef GENGINE_RBS_TRANSFORM_VALIDATION
    if (auto checked = PreEditorValidation::CheckAuthoritativeTransformMutations(
            *m_SceneResources, assets.authoringMesh, assets.floorMaterial);
        !checked)
        return checked;
    // Re-run only the affected scale/lifetime functional checks, never provider cost measurements.
    if (auto checked = PreEditorValidation::CheckSubscriptionScaleBridge(
            *m_SceneResources, assets.authoringMesh, assets.floorMaterial);
        !checked)
        return checked;
    Log::GetCoreLogger()->info("PRE_EDITOR_PACKET_B_SCALE_LIFETIME_PASS");
#endif
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::Subscriptions))
    {
        auto checked = PreEditorValidation::CheckSubscriptionScaleBridge(
            *m_SceneResources, assets.authoringMesh, assets.floorMaterial);
        if (!checked)
            return std::unexpected(checked.error());
    }

    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::ValidateMaterialGallery(Asset::MeshHandle boxMesh)
{
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::MaterialAuthoring))
    {
        auto checked = PreEditorValidation::CheckMaterialAuthoring(
            *m_SceneResources, *m_ActiveScene, boxMesh, m_MaterialGallery.uvMaterial,
            m_MaterialGallery.uv[0].GetComponent<MeshRendererComponent>().mesh,
            m_Launch.validation.materialAlphaFixture
                ? m_Launch.validation.materialAlphaFixture->c_str()
                : nullptr,
            m_Launch.validation.materialComparison == Rbs::MaterialComparison::Floor);
        if (!checked)
            return checked;
        FocusMaterialCamera(m_EditorCamera_, {0, 2, -22}, .35f, 0, 38.f);
    }

    return {};
}

ApplicationInitializationResult RigidBodySimulationApp::ValidateShaderDescription(
    Asset::MeshHandle boxMesh, const MaterialShaderDescription& customDescription,
    MaterialHandle customMaterial, std::span<const ShaderSource> customStages,
    std::span<const MaterialShaderParameter> customParameters,
    std::span<const ShaderVariant> customVariants)
{
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::ShaderDescriptions))
    {
        auto checked = PreEditorValidation::CheckShaderDescriptions(
            GetEngineContext(), *m_SceneResources, boxMesh, customDescription, customMaterial,
            customStages, customParameters, customVariants);
        if (!checked)
            return checked;
        FocusMaterialCamera(m_EditorCamera_, {0, 3, -22}, .35f, 0, 38.f);
    }

    return {};
}

bool RigidBodySimulationApp::ValidateUpdate()
{
#ifdef GENGINE_INPUT_VALIDATION
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::InputRouting))
    {
        SetManualFrameRateLimit(0);
        static unsigned inputFrames = 0;
        static Vec3f uiCameraPosition{};
        static bool uiPaused = false, uiDebug = false;
        static std::uint32_t uiPicked = 0;
        ++inputFrames;
        const auto fail = [this](const char* message)
        {
            FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Input validation", "",
                         "Phase 15 control/candidate", "", message});
            return false;
        };
        if (inputFrames == 8)
        {
            PreEditorInput::Describe(*this);
            const auto camera = m_EditorCamera_;
            const bool paused = m_IsPause, debug = m_IsShowDebugBoundingBox;
#ifndef GENGINE_INPUT_CONTROL
            if (auto prepared = PreEditorInput::PrepareNativeFixture(*this); !prepared)
                return fail(prepared.error().message.c_str());
            if (auto checked = PreEditorInput::CheckState(*this); !checked)
                return fail(checked.error().message.c_str());
            if (auto prepared = PreEditorInput::PrepareNativeFixture(*this); !prepared)
                return fail(prepared.error().message.c_str());
            auto& input = *GetInputManager();
            input.BeginUIRouting();
            if (!input.PublishRouting(
                    {GetWindow()->GetWindowID(), 0, 0, 1280, 720, true, true, false, false}))
                return fail("native action routing fixture");
            const bool pauseTap = PreEditorInput::NativeTap(*this, SDL_SCANCODE_P);
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_NATIVE_ACTION pause injected={} actual={}",
                                       pauseTap, m_IsPause);
            if (!pauseTap || !m_IsPause)
                return fail("maintained pause/resume/debug native controls: pause");
            const bool resumeTap = PreEditorInput::NativeTap(*this, SDL_SCANCODE_R);
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_NATIVE_ACTION resume injected={} actualPause={}",
                                       resumeTap, m_IsPause);
            if (!resumeTap || m_IsPause)
                return fail("maintained pause/resume/debug native controls: resume");
            const bool debugTap = PreEditorInput::NativeTap(*this, SDL_SCANCODE_SPACE);
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_NATIVE_ACTION debug injected={} before={} after={}",
                                       debugTap, debug, m_IsShowDebugBoundingBox);
            if (!debugTap || m_IsShowDebugBoundingBox == debug)
                return fail("maintained pause/resume/debug native controls: debug");
            if (!PreEditorInput::NativeTap(*this, SDL_SCANCODE_I))
                return fail("native viewpoint shortcut");
            const auto beforeWheel = m_EditorCamera_.GetDistance();
            auto motion = PreEditorInput::Native(2, GetWindow()->GetWindowID());
            auto wheel = PreEditorInput::Native(3, GetWindow()->GetWindowID());
            if (SDL_PushEvent(&motion) != 1 || SDL_PushEvent(&wheel) != 1)
                return fail("native pointer injection");
            input.PrepareForUpdate();
            GetEventManager()->PollEvents();
            input.Update();
            if (m_EditorCamera_.GetDistance() == beforeWheel)
                return fail("maintained wheel consumer");
            m_EditorCamera_.OnUpdate(
                Timestep(.016f)); // Retire any earlier cancellation before a new gesture.
            SDL_Event button{};
            button.type = SDL_MOUSEBUTTONDOWN;
            button.button.windowID = GetWindow()->GetWindowID();
            button.button.button = SDL_BUTTON_RIGHT;
            button.button.state = SDL_PRESSED;
            button.button.x = 640;
            button.button.y = 360;
            if (SDL_PushEvent(&button) != 1)
                return fail("camera gesture injection");
            input.PrepareForUpdate();
            GetEventManager()->PollEvents();
            input.Update();
            m_EditorCamera_.OnUpdate(Timestep(.016f));
            if (input.PointerCapture() != InputLayer::View ||
                !input.CancelInput(InputCancelReason::Explicit))
                return fail("maintained camera capture/cancel");
            m_EditorCamera_.OnUpdate(Timestep(.016f));
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_15_ACTIONS_PASS pause=true resume=true debug=true viewpoint=true wheel=true camera=true");
            for (const auto lost : {SDL_WINDOWEVENT_FOCUS_LOST, SDL_WINDOWEVENT_HIDDEN,
                                    SDL_WINDOWEVENT_MINIMIZED})
            {
                if (!PreEditorInput::NativeTap(*this, SDL_SCANCODE_W))
                    return fail("window cancellation fixture");
                SDL_Event state{};
                state.type = SDL_WINDOWEVENT;
                state.window.windowID = GetWindow()->GetWindowID();
                state.window.event = lost;
                if (SDL_PushEvent(&state) != 1)
                    return fail("window cancellation injection");
                GetEventManager()->PollEvents();
                if (input.Focused() || input.PointerCapture() != InputLayer::None)
                    return fail("native focus/hide/minimize cancellation");
                state.window.event = SDL_WINDOWEVENT_RESTORED;
                if (SDL_PushEvent(&state) != 1)
                    return fail("window restore injection");
                state.window.event = SDL_WINDOWEVENT_SHOWN;
                if (SDL_PushEvent(&state) != 1)
                    return fail("window shown injection");
                state.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
                if (SDL_PushEvent(&state) != 1)
                    return fail("window focus injection");
                GetEventManager()->PollEvents();
                input.BeginUIRouting();
                if (!input.PublishRouting({GetWindow()->GetWindowID(), 0, 0, 1280, 720,
                                           true, true, false, false}))
                    return fail("restored view routing");
            }
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_WINDOW_PASS focus=true hidden=true minimize=true regain=true");
            // Retire the synthetic native button after checking semantic cancellation.
            // ImGui receives native events independently of the semantic state fixture.
            button.type = SDL_MOUSEBUTTONUP;
            button.button.state = SDL_RELEASED;
            if (SDL_PushEvent(&button) != 1)
                return fail("native camera fixture release");
            GetEventManager()->PollEvents();
#endif
#if defined(GENGINE_CONFIG_RELEASE)
            if (auto measured = PreEditorInput::Measure(*this); !measured)
                return fail(measured.error().message.c_str());
#endif
            m_EditorCamera_ = camera;
            m_IsPause = paused;
            m_IsShowDebugBoundingBox = debug;
#ifdef GENGINE_INPUT_CONTROL
            if (!PreEditorInput::RequestNativeClose())
                return fail("native quit injection failed");
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_PASS");
            return false;
#else
            if (!GetInputManager()->CancelInput(InputCancelReason::Explicit))
                return fail("retire input fixtures");
            m_EditorCamera_.OnUpdate(Timestep(.016f));
            uiCameraPosition = m_EditorCamera_.GetPosition();
            uiPaused = m_IsPause;
            uiDebug = m_IsShowDebugBoundingBox;
            uiPicked = static_cast<std::uint32_t>(m_HoveredEntity);
#endif
        }
#ifndef GENGINE_INPUT_CONTROL
        if (inputFrames > 8)
        {
            if (m_IsPause != uiPaused || m_IsShowDebugBoundingBox != uiDebug ||
                m_EditorCamera_.GetPosition() != uiCameraPosition ||
                static_cast<std::uint32_t>(m_HoveredEntity) != uiPicked)
                return fail("UI changed scene selection/camera/Game state");
            if (auto checked = PreEditorInput::StepUI(*this); !checked)
                return fail(checked.error().message.c_str());
            if (PreEditorInput::uiProbe.step == 42)
            {
                if (!PreEditorInput::RequestNativeClose())
                    return fail("native quit injection failed");
                Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_PASS");
                return false;
            }
        }
#endif
    }
#endif
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::Subscriptions))
    {
        if (auto* connections = m_Subscriptions.get(); connections && !connections->checked)
        {
            auto checked = PreEditorValidation::CheckTypedSubscriptionLifetime(
                GetEventManager()->Completions());
            if (!checked)
            {
                ReportPlatformError(checked.error());
                m_Running = false;
                return false;
            }
            auto& input = *GetInputManager();
            const auto routing = input.Routing();
            input.BeginUIRouting();
            if (!input.PublishRouting(
                    {GetWindow()->GetWindowID(), 0, 0, 1280, 720, true, true, false, false}))
                return false;
            const auto tap = [&](GEngineKeyCode code)
            {
                InputEvent event;
                event.window = GetWindow()->GetWindowID();
                event.kind = InputKind::KeyDown;
                event.key = code;
                if (!input.Route(event))
                    return false;
                event.kind = InputKind::KeyUp;
                return bool(input.Route(event));
            };
            const bool paused = m_IsPause, debug = m_IsShowDebugBoundingBox;
            const auto distance = m_EditorCamera_.GetDistance();
            auto camera = m_EditorCamera_;
            bool valid = tap(GENGINE_KEY_P) && m_IsPause;
            valid = tap(GENGINE_KEY_R) && !m_IsPause && valid;
            valid = tap(GENGINE_KEY_SPACE) && m_IsShowDebugBoundingBox != debug && valid;
            valid = tap(GENGINE_KEY_I) && valid;
            InputEvent pointer;
            pointer.window = GetWindow()->GetWindowID();
            pointer.kind = InputKind::Motion;
            pointer.x = 640;
            pointer.y = 360;
            valid = bool(input.Route(pointer)) && valid;
            pointer.kind = InputKind::Wheel;
            pointer.wheelY = 1;
            valid =
                bool(input.Route(pointer)) && m_EditorCamera_.GetDistance() != distance && valid;
            m_IsPause = paused;
            m_IsShowDebugBoundingBox = debug;
            m_EditorCamera_ = camera;
            valid = bool(input.CancelInput(InputCancelReason::Explicit)) && valid;
            valid = bool(input.PublishRouting(routing)) && valid;
            if (!valid)
            {
                Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_13_FAIL maintained actions");
                m_Running = false;
                return false;
            }
            connections->checked = true;
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_13_ACTIONS_PASS");
        }
        if (auto measured = PreEditorValidation::ObserveSubscriptionFrame(
                m_Launch.validation.subscriptionOutput
                    ? m_Launch.validation.subscriptionOutput->c_str()
                    : nullptr);
            !measured)
        {
            ReportPlatformError(measured.error());
            m_Running = false;
            return false;
        }
    }

    return true;
}

bool RigidBodySimulationApp::ReplayMaterialValidation()
{
    if (m_MaterialGallery.parent &&
        m_Launch.validation.Includes(Rbs::ValidationCheck::MaterialAuthoring))
    {
        // Two seconds per static view. Each four-second motion replay advances
        // the same 60 poses, holding poses 15/30/45/60 for matched captures.
        const auto now = std::chrono::steady_clock::now();
        if (m_MaterialGallery.validationStart == std::chrono::steady_clock::time_point{})
            m_MaterialGallery.validationStart = now;
        const double elapsed =
            std::chrono::duration<double>(now - m_MaterialGallery.validationStart).count();
        const int stage = elapsed < 28   ? int(elapsed / 2)
                          : elapsed < 36 ? 14 + int((elapsed - 28) / 4)
                                         : 16;
        const double within = elapsed - (stage < 14 ? stage * 2 : 28 + (stage - 14) * 4);
        if (stage != m_MaterialGallery.validationStage)
        {
            m_MaterialGallery.validationStage = stage;
            m_MaterialGallery.recordedSample = -1;
            SetMaterialComparison(stage == 16 ? 0 : 1 + stage % 2);
            const int primitive = stage >= 2 && stage < 14 ? (stage - 2) / 2 : -1;
            FocusMaterialCamera(m_EditorCamera_,
                                primitive < 0 ? Vec3f{0, 2, -22}
                                              : Vec3f{-10.f + 4.f * primitive, 2, -20},
                                .35f, primitive < 0 ? 0 : .65f, primitive < 0 ? 38.f : 7.f);
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_VIEW_BEGIN stage={}", stage);
        }
        int motionStep = 0;
        m_MaterialGallery.validationSample = 0;
        m_MaterialGallery.captureReady = stage < 14 && within >= .3;
        if (stage == 14 || stage == 15)
        {
            const int block = int(within);
            const double fraction = within - block;
            motionStep = block * 15 + (std::min)(15, 1 + int(fraction * 30));
            m_MaterialGallery.validationSample = block + 1;
            m_MaterialGallery.captureReady = fraction >= .65;
        }
        m_MaterialGallery.validationFrames = motionStep;
        const float t = motionStep * .05f;
        m_MaterialGallery.parent.Transform().SetTranslation(
            motionStep ? Vec3f{.5f * std::sin(t), 0, .5f * std::cos(t)} : Vec3f{});
        m_MaterialGallery.parent.Transform().SetRotation({0, .04f * std::sin(t), 0});
        for (std::size_t i = 0; i < m_MaterialGallery.uv.size(); ++i)
            for (auto entity : {m_MaterialGallery.uv[i], m_MaterialGallery.triplanar[i]})
            {
                entity.Transform().SetRotation({.3f * std::sin(t), t, .2f * std::cos(t)});
                entity.Transform().SetScale(
                    motionStep == 0 ? Vec3f{1}
                    : motionStep <= 30
                        ? Vec3f(1.f + .2f * std::sin(t))
                        : Vec3f{1.f + .2f * std::sin(t), 1.f + .15f * std::cos(t), 1});
            }
        return true;
    }
    return false;
}

ScheduleResult RigidBodySimulationApp::ValidateBeforeSubmission(const FrameSubmissionDesc& targets,
                                                                const FrameCamera& camera)
{
    if (m_Launch.validation.Includes(Rbs::ValidationCheck::ShaderReload) &&
        ++m_ShaderReloadGallery.frames == 3)
    {
        auto checked = PreEditorValidation::CheckShaderReload(
            GetEngineContext(), *m_SceneResources, *m_ActiveScene, m_ShaderReloadGallery.material,
            m_ShaderReloadGallery.clone, *m_ShaderReloadGallery.original,
            *m_ShaderReloadGallery.edited, *m_ShaderReloadGallery.invalid,
            *m_ShaderReloadGallery.incompatible, *m_FrameSubmission, targets, m_PickTable, camera);
        if (!checked)
            return std::unexpected(
                ScheduleError{FrameStage::UpdateFrameResources, checked.error()});
    }
    return {};
}

ScheduleResult RigidBodySimulationApp::ValidateSubmittedFrame(bool submitted,
                                                              const FrameSubmissionDesc& targets,
                                                              const FrameCamera& camera)
{
    if (submitted && m_Launch.validation.Includes(Rbs::ValidationCheck::AsyncResources) &&
        m_WoodSettled && (!m_LoadBarrel || m_BarrelSettled) && !m_AsyncTextureGallery.checked)
    {
        m_AsyncTextureGallery.checked = true;
        auto checked = PreEditorValidation::CheckAsyncAdoption(
            GetEngineContext(), *m_SceneResources, *m_ActiveScene, m_AsyncBoxMaterial,
            m_AsyncTextureGallery.clone, *m_TextureLoads, *m_FrameSubmission, targets, m_PickTable,
            camera,
            [&](const std::string& path, bool cancel)
            {
                m_AsyncTextureGallery.source = path;
                m_AsyncTextureGallery.request = true;
                m_AsyncTextureGallery.cancel = cancel;
            },
            [&]
            {
                return ApplyAsyncTextureEdit(*m_SceneResources, *m_TextureLoads,
                                             m_AsyncBoxMaterial);
            },
            [&]
            {
                return m_AsyncTextureGallery.state;
            });
        if (!checked)
            return std::unexpected(
                ScheduleError{FrameStage::UpdateFrameResources, checked.error()});
    }
    if (submitted && m_AsyncTextureGallery.checked && ++m_AsyncTextureGallery.frames == 5)
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_12_FUNCTIONAL_COMPLETE");
    if (submitted && m_Launch.validation.Includes(Rbs::ValidationCheck::Subscriptions))
        if (auto checked = PreEditorValidation::CheckSubscriptionPresentation(
                camera.viewportWidth, camera.viewportHeight);
            !checked)
            return std::unexpected(ScheduleError{FrameStage::Pass, checked.error()});
    if (submitted &&
        (m_Launch.validation.Includes(Rbs::ValidationCheck::ShaderDescriptions) ||
         m_Launch.validation.Includes(Rbs::ValidationCheck::ShaderReload)) &&
        !PreEditorValidation::CheckShaderSteadyFrame(
            *m_SceneResources, {static_cast<unsigned>(camera.viewportWidth),
                                static_cast<unsigned>(camera.viewportHeight)}))
        return std::unexpected(ScheduleError{
            FrameStage::Pass,
            PlatformError{PlatformErrorCode::Initialization, "Phase 10 shader hot path",
                          "Shader work appeared during warmed rendering"}});
    if (submitted && m_Launch.validation.Includes(Rbs::ValidationCheck::MaterialAuthoring))
    {
        if (m_MaterialGallery.captureReady &&
            m_MaterialGallery.recordedSample != m_MaterialGallery.validationSample)
        {
            const auto p = camera.worldPosition;
            const auto& q = camera.projection;
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_09_VIEW stage={} sample={} mode={} position={},{},{} pitch={} yaw={} fov={} near={} far={} viewport={},{} projection={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{} motion_step={}",
                m_MaterialGallery.validationStage, m_MaterialGallery.validationSample,
                m_MaterialGallery.comparison, p.x, p.y, p.z, m_EditorCamera_.m_Pitch,
                m_EditorCamera_.m_Yaw, m_EditorCamera_.GetFOV(), m_EditorCamera_.GetNearClip(),
                m_EditorCamera_.GetFarClip(), camera.viewportWidth, camera.viewportHeight, q[0][0],
                q[0][1], q[0][2], q[0][3], q[1][0], q[1][1], q[1][2], q[1][3], q[2][0], q[2][1],
                q[2][2], q[2][3], q[3][0], q[3][1], q[3][2], q[3][3],
                m_MaterialGallery.validationFrames);
            m_MaterialGallery.recordedSample = m_MaterialGallery.validationSample;
        }
        if (m_MaterialGallery.validationStage == 16 && m_MaterialGallery.recordedSample != 0)
        {
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_09_MOTION_COMPLETE poses=60 replays=2 matched_holds=15,30,45,60 visual_result=pending");
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_09_VIEWS_COMPLETE static_views=14 motion_views=8");
            m_MaterialGallery.recordedSample = 0;
        }
    }
    return {};
}

void RigidBodySimulationApp::ObserveViewport(
    const std::expected<FramebufferScale, PlatformError>& scale)
{
    static bool phase08PresentationRecorded = false;
    if (!phase08PresentationRecorded &&
        m_Launch.validation.Includes(Rbs::ValidationCheck::GeometryAuthoring) &&
        HasVisibleViewport() && scale)
    {
        const auto logical = GetEditorViewportLogicalSize();
        const auto pixels = GetEditorViewportPixelSize();
        const auto& target = m_MousePickFrameBuffer->Buffer().Description();
        if (target.Width == pixels.Width && target.Height == pixels.Height)
        {
            const auto position = m_EditorCamera_.GetPosition();
            const auto& shadows = GetShadowQuality();
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_08_PRESENTATION logical={}x{} pixels={}x{} scale={},{} camera={},{},{} fov={} near={} far={} shadow={} resolution={} swap_interval={} manual_cap={}",
                logical.Width, logical.Height, pixels.Width, pixels.Height, scale->X, scale->Y,
                position.x, position.y, position.z, m_EditorCamera_.GetFOV(),
                m_EditorCamera_.GetNearClip(), m_EditorCamera_.GetFarClip(),
                ShadowQualityLabel(shadows.effective), shadows.resolution,
                GetWindow()->GetSwapInterval(), GetManualFrameRateLimit());
            phase08PresentationRecorded = true;
        }
    }

    if (!m_MaterialGallery.presentationRecorded &&
        m_Launch.validation.Includes(Rbs::ValidationCheck::MaterialAuthoring) &&
        HasVisibleViewport() && scale)
    {
        const auto logical = GetEditorViewportLogicalSize();
        const auto pixels = GetEditorViewportPixelSize();
        const auto position = m_EditorCamera_.GetPosition();
        const auto& shadows = GetShadowQuality();
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_09_PRESENTATION logical={}x{} pixels={}x{} scale={},{} camera={},{},{} fov={} near={} far={} shadow={} resolution={} swap_interval={} manual_cap={}",
            logical.Width, logical.Height, pixels.Width, pixels.Height, scale->X, scale->Y,
            position.x, position.y, position.z, m_EditorCamera_.GetFOV(),
            m_EditorCamera_.GetNearClip(), m_EditorCamera_.GetFarClip(),
            ShadowQualityLabel(shadows.effective), shadows.resolution,
            GetWindow()->GetSwapInterval(), GetManualFrameRateLimit());
        m_MaterialGallery.presentationRecorded = true;
    }
}

#ifdef GENGINE_RBS_MODULARITY_VALIDATION
namespace
{
    // Test-only state, on the owning UI thread. The normal application neither
    // reads these fixtures nor builds/resets a dock layout programmatically.
    constexpr const char* PacketAPanels[]{"Viewport", "Material authoring", "Custom shader reload"};
    struct DockProbe
    {
        unsigned frame = 0;
        ImGuiID root = 0, left = 0, right = 0, lower = 0;
        float widthBefore = 0;
    };

    bool PacketAWindowContract(ImGuiWindow* host, ImGuiWindow* panel)
    {
        const auto forbidden = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;
        // BeginDocked sets NoResize on its child; the dock split owns that resize.
        const bool resizable =
            panel &&
            (panel->DockIsActive ? panel->DockNode && !(panel->DockNode->MergedFlags &
                                                        ImGuiDockNodeFlags_NoResizeFlagsMask_)
                                 : !(panel->Flags & ImGuiWindowFlags_NoResize));
        return panel && panel->Active && !(panel->Flags & forbidden) && resizable &&
               host->BeginOrderWithinContext < panel->BeginOrderWithinContext;
    }

    void BuildPacketALayout(DockProbe& probe)
    {
        ImGui::DockBuilderRemoveNode(probe.root);
        ImGui::DockBuilderAddNode(probe.root, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(probe.root, ImGui::GetMainViewport()->WorkSize);
        probe.left =
            ImGui::DockBuilderSplitNode(probe.root, ImGuiDir_Left, .34f, nullptr, &probe.right);
        probe.lower =
            ImGui::DockBuilderSplitNode(probe.left, ImGuiDir_Down, .45f, nullptr, &probe.left);
        ImGui::DockBuilderDockWindow(PacketAPanels[0], probe.right);
        ImGui::DockBuilderDockWindow(PacketAPanels[1], probe.left);
        ImGui::DockBuilderDockWindow(PacketAPanels[2], probe.lower);
        ImGui::DockBuilderFinish(probe.root);
    }

    bool PacketARestoreMatches()
    {
        std::ifstream expected("packet-a-restore.expected");
        if (!expected)
            return false;
        for (const auto* name : PacketAPanels)
        {
            unsigned dock = 0;
            float x = 0, y = 0, width = 0, height = 0;
            const auto* window = ImGui::FindWindowByName(name);
            if (!(expected >> dock >> x >> y >> width >> height) || !window ||
                window->DockId != dock || std::abs(window->Pos.x - x) > 1 ||
                std::abs(window->Pos.y - y) > 1 || std::abs(window->Size.x - width) > 1 ||
                std::abs(window->Size.y - height) > 1)
                return false;
        }
        return true;
    }
}

void RigidBodySimulationApp::ValidatePacketAUi()
{
    static DockProbe probe;
    ++probe.frame;
    const auto fail = [&](const char* message)
    {
        Log::GetCoreLogger()->error("PRE_EDITOR_PACKET_A_FAIL frame={} {}", probe.frame, message);
        FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
                     "Packet A validation",
                     "UI",
                     "docking acceptance",
                     {},
                     message});
    };
    auto* host = ImGui::FindWindowByName("DockSpace Demo");
    if (!host)
        return fail("Missing dockspace host");
    probe.root = ImHashStr("MyDockSpace", 0, host->ID);
    const auto* root = ImGui::DockBuilderGetNode(probe.root);
    if (!root || root->LastFrameAlive != ImGui::GetFrameCount())
        return fail("Dockspace must be alive before its hosted panels");
    for (const auto* name : PacketAPanels)
        if (!PacketAWindowContract(host, ImGui::FindWindowByName(name)))
            return fail("Panel ordering, resize, move or docking flags violate the contract");
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        return fail("Platform viewports must remain disabled");
#ifdef GENGINE_RBS_SCENE_VALIDATION
    if (!m_IsPause)
        return fail("Scene composition diagnostic must not step Physics");
    if (auto membership = ValidateSceneMembership(); !membership)
        return fail("Scene entity identities/categories changed during UI/resource frames");
    if (probe.frame == 4 && m_Launch.scene != Rbs::RbsScenePreset::GeometryGallery)
    {
        std::ifstream captured("scene-ui-text.log");
        const std::string text{std::istreambuf_iterator<char>{captured}, {}};
        for (const auto* message :
             {"No shader target", "No material target", "No async demo target"})
            if (text.find(message) == std::string::npos)
                return fail("Missing explicit no-target state in submitted panel/menu UI");
        Log::GetCoreLogger()->info("PRE_EDITOR_PACKET_A_NO_TARGET_PASS panels=3");
    }
#endif

    if (probe.frame == 5)
    {
        auto& dispatcher = GetEventManager()->GetEventDispatcher();
        const bool paused = m_IsPause, debug = m_IsShowDebugBoundingBox;
        const auto camera = m_EditorCamera_;
        bool valid = bool(dispatcher.Dispatch(Manager::Event::AppPause)) && m_IsPause;
        valid = bool(dispatcher.Dispatch(Manager::Event::AppResume)) && !m_IsPause && valid;
        valid = bool(dispatcher.Dispatch(Manager::Event::DebugShow)) &&
                m_IsShowDebugBoundingBox != debug && valid;
        valid = bool(dispatcher.Dispatch(Manager::Event::ViewportChange)) && valid;
        valid = bool(dispatcher.Dispatch(Manager::Event::MouseScrollWheel,
                                         MouseScrollWheelParam{0, 0, 1})) &&
                m_EditorCamera_.GetDistance() != camera.GetDistance() && valid;
        m_IsPause = paused;
        m_IsShowDebugBoundingBox = debug;
        m_EditorCamera_ = camera;
        if (!valid)
            return fail("Maintained application actions changed after extraction");
        Log::GetCoreLogger()->info("PRE_EDITOR_PACKET_A_ACTIONS_PASS actions=5");
    }

    if (probe.frame == 10)
    {
        if (!PacketARestoreMatches())
            return fail("Persisted mixed layout did not restore");
        Log::GetCoreLogger()->info("PRE_EDITOR_PACKET_A_LAYOUT_RESTORE_PASS");
        BuildPacketALayout(probe);
    }
    // Each operation is followed by several full application frames. This checks
    // retention through normal Begin/DockSpace submission, not just builder state.
    if (probe.frame >= 20 && probe.frame < 200)
    {
        const unsigned panelIndex = (probe.frame - 20) / 60;
        const unsigned step = (probe.frame - 20) % 60;
        const auto* name = PacketAPanels[panelIndex];
        auto* panel = ImGui::FindWindowByName(name);
        if (step == 0)
        {
            if (!panel->DockIsActive)
                return fail("Split docking did not survive subsequent frames");
            // Keep the third panel in the other split. Emptying that split can
            // merge its sibling into the root and invalidate a cached child ID.
            ImGui::DockBuilderDockWindow(panelIndex == 0 ? PacketAPanels[1] : name, probe.right);
        }
        if (step == 5)
        {
            if (!panel->DockIsActive || panel->DockId != probe.right || !panel->DockNode ||
                panel->DockNode->Windows.Size < 2)
                return fail("Tab docking did not persist");
            ImGui::FocusWindow(panel);
        }
        if (step == 10)
            ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), panel);
        if (step == 15)
        {
            if (panel->DockIsActive || panel->DockId != 0)
                return fail("Queued undock did not become a floating window");
            ImGui::SetWindowPos(panel, {80.f + panelIndex * 30.f, 90.f + panelIndex * 20.f});
            ImGui::SetWindowSize(panel, {560, 400});
        }
        if (step == 20)
        {
            if (panel->DockIsActive || panel->Size.x != 560 || panel->Size.y != 400)
                return fail("Floating resize did not persist");
            BuildPacketALayout(probe);
        }
        if (step == 30)
        {
            if (!panel->DockIsActive || !panel->DockNode)
                return fail("Redocking failed");
            probe.widthBefore = panel->Size.x;
            // Resize the left branch, exactly as a split changes retained SizeRef.
            auto* left = ImGui::DockBuilderGetNode(probe.left)->ParentNode;
            left->SizeRef.x += 48;
            left->Size.x += 48;
            left->WantLockSizeOnce = true;
        }
        if (step == 40)
        {
            if (!panel->DockIsActive || std::abs(panel->Size.x - probe.widthBefore) < 8)
                return fail("Docked split resize did not change the panel extent");
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PACKET_A_DOCK_PASS panel={} split=true tab=true undock=true redock=true resize=true",
                name);
        }
    }
    // Steady visible frames after layout changes exercise the unchanged logical /
    // framebuffer conversion and camera aspect; no new DPI tolerance is introduced.
    if (probe.frame == 210)
    {
        const auto pixels = GetEditorViewportPixelSize();
        const auto logical = GetEditorViewportLogicalSize();
        const auto& storage = m_MousePickFrameBuffer->Buffer().Description();
        const auto center = ViewportPixelAt(logical.Width / 2, logical.Height / 2, logical,
                                            {storage.Width, storage.Height});
        if (!pixels.Width || !pixels.Height || storage.Width != pixels.Width ||
            storage.Height != pixels.Height || !center ||
            std::abs(m_EditorCamera_.GetAspectRatio() - float(pixels.Width) / pixels.Height) >
                1e-5f)
            return fail("Viewport storage, camera aspect or picking conversion diverged");
        Log::GetCoreLogger()->info("PRE_EDITOR_PACKET_A_VIEWPORT_PASS logical={}x{} pixels={}x{}",
                                   logical.Width, logical.Height, pixels.Width, pixels.Height);
    }
    if (probe.frame == 220)
        ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(),
                                            ImGui::FindWindowByName(PacketAPanels[2]));
    if (probe.frame == 225)
    {
        auto* panel = ImGui::FindWindowByName(PacketAPanels[2]);
        ImGui::SetWindowPos(panel, {680, 240});
        ImGui::SetWindowSize(panel, {560, 400});
    }
    if (probe.frame == 240)
    {
        if (!m_WoodSettled || !m_Subscriptions)
            return fail("Extracted scene, subscriptions or asynchronous publication not ready");
        if (m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery)
        {
            if (!m_MaterialGallery.uvMaterial || !m_ShaderReloadGallery.material ||
                !m_AsyncTextureGallery.clone)
                return fail("Gallery authoring targets not ready");
        }
        else if (m_MaterialGallery.parent || m_MaterialGallery.uvMaterial ||
                 m_ShaderReloadGallery.material || m_AsyncTextureGallery.clone || m_MeshLoads ||
                 m_BarrelRequest)
            return fail("Physics preset acquired a gallery/demo target or import");
#ifdef GENGINE_RBS_SCENE_VALIDATION
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PACKET_A_MEMBERSHIP_PASS preset={} identities=true categories=true retained_frames=240 physics_paused=true",
            static_cast<unsigned>(m_Launch.scene));
#endif
        std::ofstream expected("packet-a-layout.expected");
        for (const auto* name : PacketAPanels)
        {
            const auto* window = ImGui::FindWindowByName(name);
            expected << window->DockId << ' ' << window->Pos.x << ' ' << window->Pos.y << ' '
                     << window->Size.x << ' ' << window->Size.y << '\n';
        }
        expected.flush();
        if (!expected)
            return fail("Cannot save layout expectation");
        ImGui::SaveIniSettingsToDisk("imgui.ini");
        Log::GetCoreLogger()->info("PRE_EDITOR_PACKET_A_UI_PASS frames={} mixed_layout_saved=true",
                                   probe.frame);
        ShutDown();
    }
}
#endif

#ifdef GENGINE_RBS_SCENE_VALIDATION
namespace
{
    struct PhysicsFixtureExpectation
    {
        std::string name;
        ShapeType shape;
        Vec3f position;
        Vec3f scale{1.f};
        Vec3f linear{};
        Vec3f angular{};
    };

    // Independent initial-state oracle from the approved Phase 13 activation blocks.
    // This checks construction only; it does not step or measure the Physics solver.
    std::vector<PhysicsFixtureExpectation> ExpectedPhysicsFixtures(Rbs::RbsScenePreset preset)
    {
        std::vector<PhysicsFixtureExpectation> fixtures;
        switch (preset)
        {
        case Rbs::RbsScenePreset::GeometryGallery:
            break;
        case Rbs::RbsScenePreset::SphereDiamond:
            fixtures.push_back(
                {"wood_sphere_0", ShapeType::Sphere, {30, 5, 0}, Vec3f{1}, {-80, 0, 0}});
            fixtures.push_back(
                {"Diamond", ShapeType::Convex, {-30, 5, 0}, Vec3f{1}, {100, 0, 0}, {5, 0, 5}});
            break;
        case Rbs::RbsScenePreset::SphereLattice:
            for (int index = 0; index < 180; ++index)
                fixtures.push_back({std::format("wood_sphere{}", index),
                                    ShapeType::Sphere,
                                    {float((index / 6) % 6 - 1) * 2.f,
                                     10.f + float(index / 36) * 2.f, float(index % 6 - 1) * 2.f}});
            break;
        case Rbs::RbsScenePreset::BoxStack:
        case Rbs::RbsScenePreset::SphereBoxStack:
            for (int index = 0; index < 16; ++index)
                fixtures.push_back({"wood_box",
                                    ShapeType::Box,
                                    {float(index % 4) * 2.01f, 1.5f + float(index / 4) * 2.f, 0},
                                    Vec3f{2}});
            if (preset == Rbs::RbsScenePreset::SphereBoxStack)
                fixtures.push_back(
                    {"wood_sphere_0", ShapeType::Sphere, {3.5f, 5, -20}, Vec3f{1}, {0, 0, 40}});
            break;
        }
        return fixtures;
    }

    bool MatchesFixtureProperty(const Fixture3DProperty& property,
                                const PhysicsFixtureExpectation& expected)
    {
        return property.m_Position == expected.position &&
               property.m_Orientation == Quat(1, 0, 0, 0) &&
               property.m_LinearVelocity == expected.linear &&
               property.m_AngularVelocity == expected.angular && property.m_InvMass == 1.f &&
               property.m_Friction == .5f && property.m_Elasticity == .5f;
    }

    enum class SceneCategory
    {
        Camera,
        DirectionalLight,
        PointLight,
        Environment,
        Ground,
        Sphere,
        Box,
        Convex,
        AuthoringParent,
        Demo
    };

    std::multimap<std::string, SceneCategory> ExpectedSceneEntities(Rbs::RbsScenePreset preset)
    {
        // Closed inventory: an unexpected name/category fails, even if totals match.
        std::multimap<std::string, SceneCategory> expected{
            {"Editor frame camera", SceneCategory::Camera},
            {"ambient_light", SceneCategory::DirectionalLight},
            {"point_light", SceneCategory::PointLight},
            {"grid", SceneCategory::Environment},
            {"axis", SceneCategory::Environment},
            {"Environment_SkyBox", SceneCategory::Environment},
            {"wood_plane", SceneCategory::Ground},
            {"wall_entity_1", SceneCategory::Ground},
            {"wall_entity_2", SceneCategory::Ground},
            {"wall_entity_3", SceneCategory::Ground},
            {"wall_entity_4", SceneCategory::Ground}};
        for (const auto& fixture : ExpectedPhysicsFixtures(preset))
            expected.emplace(fixture.name,
                             fixture.shape == ShapeType::Sphere ? SceneCategory::Sphere
                             : fixture.shape == ShapeType::Box  ? SceneCategory::Box
                                                                : SceneCategory::Convex);
        if (preset != Rbs::RbsScenePreset::GeometryGallery)
            return expected;
        expected.emplace("Material mapping transform", SceneCategory::AuthoringParent);
        for (const auto* name : {"Template Plane", "Template Quad", "Template Grid",
                                 "Phase08 Source", "Phase08 Unique Bake", "Phase08 Regenerated",
                                 "Phase10 Custom Shader", "Phase11 Custom Shader Clone"})
            expected.emplace(name, SceneCategory::Demo);
        for (const auto* family : {"Sphere", "Cylinder", "Cone", "Capsule", "Torus", "Diamond"})
            for (const auto* shading : {"Smooth", "Flat"})
                expected.emplace(std::format("Template {} {}", family, shading),
                                 SceneCategory::Demo);
        for (const auto* family : {"Plane", "Cube", "Sphere", "Capsule", "Torus", "Diamond"})
            for (const auto* mapping : {"UV", "Triplanar"})
                expected.emplace(std::format("{} {}", family, mapping), SceneCategory::Demo);
        for (unsigned i = 0; i < 5; ++i)
            expected.emplace(std::format("Typed material kind {}", i), SceneCategory::Demo);
        for (unsigned i = 0; i < 3; ++i)
            expected.emplace(std::format("Async texture {}", i), SceneCategory::Demo);
        return expected;
    }

    bool MatchesSceneCategory(_Entity entity, SceneCategory category)
    {
        const bool physics = category == SceneCategory::Ground ||
                             category == SceneCategory::Sphere || category == SceneCategory::Box ||
                             category == SceneCategory::Convex;
        const bool light =
            category == SceneCategory::DirectionalLight || category == SceneCategory::PointLight;
        const bool renderable = category != SceneCategory::Camera &&
                                category != SceneCategory::DirectionalLight &&
                                category != SceneCategory::AuthoringParent;
        if (!entity.HasAllComponents<TagComponent, Transform3DComponent>() ||
            entity.HasAllComponents<MeshRendererComponent>() != renderable ||
            entity.HasAllComponents<RenderCameraComponent>() !=
                (category == SceneCategory::Camera) ||
            entity.HasAllComponents<RenderLightComponent>() != light ||
            entity.HasAllComponents<RigidBody3DComponent>() != physics ||
            entity.HasAllComponents<SphereFixture3DComponent>() !=
                (category == SceneCategory::Sphere) ||
            entity.HasAllComponents<BoxFixture3DComponent>() !=
                (category == SceneCategory::Box || category == SceneCategory::Ground) ||
            entity.HasAllComponents<ConvexFixture3DComponent>() !=
                (category == SceneCategory::Convex))
            return false;
        if (light && entity.GetComponent<RenderLightComponent>().kind !=
                         (category == SceneCategory::PointLight ? RenderLightKind::Point
                                                                : RenderLightKind::Directional))
            return false;
        return !physics ||
               entity.GetComponent<RigidBody3DComponent>().Type ==
                   (category == SceneCategory::Ground ? BodyType::Static : BodyType::Dynamic);
    }
}

ApplicationInitializationResult RigidBodySimulationApp::ValidateSceneMembership()
{
    auto expected = ExpectedSceneEntities(m_Launch.scene);
    const auto failure = [](std::string message) -> ApplicationInitializationResult
    {
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                                             "Packet A scene membership", std::move(message)});
    };
    // Diagnostic-only identity retention, on the app/context thread. Names and
    // component categories are checked independently of these ECS identities.
    static std::map<entt::entity, std::string> initial;
    std::map<entt::entity, std::string> actual;
    for (auto id : m_ActiveScene->GetAllEntitiesWith<IDComponent>())
    {
        _Entity entity{id, m_ActiveScene.get()};
        if (!entity.HasAllComponents<TagComponent>())
            return failure("An entity has no category/name identity");
        const auto found = expected.find(entity.Name());
        if (found == expected.end() || !MatchesSceneCategory(entity, found->second))
            return failure("Unexpected entity or category: " + entity.Name());
        actual.emplace(id, entity.Name());
        expected.erase(found);
    }
    if (!expected.empty())
        return failure("Missing required entity: " + expected.begin()->first);
    if (initial.empty())
    {
        initial = actual;
        for (const auto& [id, name] : actual)
            Log::GetCoreLogger()->info("PRE_EDITOR_PACKET_A_ENTITY id={} name={}",
                                       static_cast<unsigned>(id), name);
    }
    else if (initial != actual)
        return failure("Scene identities changed after initialization");
    return {};
}

ApplicationInitializationResult
RigidBodySimulationApp::ValidatePhysicsScene(const Rbs::SceneAssets& assets)
{
    auto expected = ExpectedPhysicsFixtures(m_Launch.scene);
    const auto dynamicCount = expected.size();
    const auto failure = [](std::string message) -> ApplicationInitializationResult
    {
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                                             "Packet A physics scene construction",
                                             std::move(message)});
    };
    const unsigned galleryCount = m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery ? 15 : 0;
    unsigned actualGallery = 0;
    for (auto id : m_ActiveScene->GetAllEntitiesWith<TagComponent>())
    {
        _Entity entity{id, m_ActiveScene.get()};
        if (entity.Name().starts_with("Template "))
        {
            ++actualGallery;
            if (!entity.HasAllComponents<MeshRendererComponent>() ||
                entity.HasAllComponents<RigidBody3DComponent>())
                return failure("Template gallery must contain only render fixtures");
        }
    }
    if (actualGallery != galleryCount || assets.galleryFixtures != galleryCount ||
        assets.normalPhysicsBodies != dynamicCount + 5 ||
        m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies().size() !=
            dynamicCount + 5)
        return failure("Gallery/default or total runtime body count changed");
    unsigned staticCount = 0;
    for (auto id : m_ActiveScene->GetAllEntitiesWith<RigidBody3DComponent>())
    {
        _Entity entity{id, m_ActiveScene.get()};
        const auto& component = entity.GetComponent<RigidBody3DComponent>();
        const auto* body = component.RuntimeBody;
        if (!body || !body->m_Shape || body->Type != component.Type ||
            !entity.HasAllComponents<MeshRendererComponent, Transform3DComponent>())
            return failure("Missing fixture runtime body, shape or renderable");
        if (component.Type == BodyType::Static)
        {
            ++staticCount;
            if (!entity.HasAllComponents<BoxFixture3DComponent>() || body->GetInverseMass() != 0 ||
                body->m_Shape->GetShapeType() != ShapeType::Box)
                return failure("Common ground/wall body family changed");
            continue;
        }
        const auto& transform = entity.GetComponent<Transform3DComponent>();
        auto found = std::find_if(expected.begin(), expected.end(),
                                  [&](const auto& fixture)
                                  {
                                      return fixture.name == entity.Name() &&
                                             fixture.position == transform.Translation &&
                                             fixture.shape == body->m_Shape->GetShapeType();
                                  });
        if (component.Type != BodyType::Dynamic || found == expected.end())
            return failure("Unexpected dynamic fixture family or initial position");
        if (transform.Scale != found->scale || transform.QuatRotation != Quat(1, 0, 0, 0) ||
            body->m_Position != found->position || body->m_Orientation != transform.QuatRotation ||
            body->GetLinearVelocity() != found->linear ||
            body->GetAngularVelocity() != found->angular || body->GetInverseMass() != 1.f ||
            body->m_Friction != .5f || body->m_Elasticity != .5f)
            return failure("Initial transform, motion or material differs from Phase 13");
        const Fixture3DProperty* property = nullptr;
        switch (found->shape)
        {
        case ShapeType::Sphere:
            if (entity.HasAllComponents<SphereFixture3DComponent>())
            {
                const auto& fixture = entity.GetComponent<SphereFixture3DComponent>();
                if (fixture.Radius != 1.f)
                    return failure("Sphere radius changed");
                property = &fixture.Property;
            }
            break;
        case ShapeType::Box:
            if (entity.HasAllComponents<BoxFixture3DComponent>())
                property = &entity.GetComponent<BoxFixture3DComponent>().Property;
            break;
        case ShapeType::Convex:
            if (entity.HasAllComponents<ConvexFixture3DComponent>())
                property = &entity.GetComponent<ConvexFixture3DComponent>().Property;
            break;
        default:
            break;
        }
        if (!property || !MatchesFixtureProperty(*property, *found))
            return failure("Authored fixture component differs from Phase 13");
        expected.erase(found);
    }
    if (staticCount != 5 || !expected.empty())
        return failure("Missing expected static or dynamic fixture");
    Log::GetCoreLogger()->info(
        "PRE_EDITOR_PACKET_A_SCENE_PASS preset={} dynamic={} static=5 gallery={} before_step=true",
        static_cast<unsigned>(m_Launch.scene), dynamicCount, galleryCount);
    return {};
}
#endif
