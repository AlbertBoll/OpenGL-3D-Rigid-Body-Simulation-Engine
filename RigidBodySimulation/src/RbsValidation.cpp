#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Scene/_Scene.h"
#include <format>

#include <chrono>
#include <cmath>
#ifdef GENGINE_LIFECYCLE_VALIDATION
#include "../tests/CameraLifecycleChecks.h"
#endif
#ifdef GENGINE_CAMERA_VALIDATION
#include "../tests/CameraSnapshotChecks.h"
#endif
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

#if defined(GENGINE_LIFECYCLE_VALIDATION) && !defined(GENGINE_LIFECYCLE_CONTROL)
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
namespace PreEditorLifecycle
{
    void FinishFocusTrace(const char* reason)
    {
        const auto reset = probe.focusTrace.Reset();
        FocusMark(119, bool(reset));
        PreEditorCamera::FocusTrace::Finish(reset ? reason : "engine_observer_detach_failed");
    }
}
#endif
bool RigidBodySimulationApp::ValidateViewportGestures()
{
    using namespace PreEditorLifecycle;
    auto& p = gestureProbe; auto& input = *GetInputManager();
    using Movement = GestureProbe::Movement;
    const auto fail = [&](const char* reason) {
        p.rows.flush();
        Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_17_GESTURE_FAIL step={} case={} reason={}", p.step,p.name,reason);
        FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Viewport gestures",{},p.name,{},reason});
        return false;
    };
    if (++p.frames > 256) return fail("Bounded gesture frame allowance exhausted");
    if (!FocusGate(*this) || input.NativePollFailure()) return fail("Native/engine focus or transition failure");
    if (!p.started)
    {
        if (ImGui::GetCurrentContext()->InputEventsQueue.Size)
        {
            if (++p.settle > 32) return fail("Prior UI events did not retire before gesture matrix");
            return true;
        }
        if (!SetCameraView(CameraView::Editor) || !CancelCameraGesture()) return fail("Gesture setup");
        p.started = true;
    }
    if (p.tick && ++p.tick < 4) return true;
    if (p.tick)
    {
        const auto reads = m_GesturePickReadbacks - p.reads;
        const auto activations = m_ViewportActivations - p.activations;
        const bool capture = input.PointerCapture() == InputLayer::View;
        const bool moved = PreEditorCamera::Max(glm::dvec3(m_EditorCamera_.GetPosition()-p.position)) >
            PreEditorCamera::WorldLimit(glm::dvec3(p.position)) ||
            std::abs(glm::dot(m_EditorCamera_.GetOrientation(),p.orientation)) < 1.f-1e-6f;
        bool movement = false;
        switch (p.movement)
        {
        case Movement::None: movement = !moved; break;
        case Movement::Orbit: case Movement::Fly:
            movement = moved && std::abs(m_EditorCamera_.m_YawDelta) > 1e-6f &&
                std::abs(m_EditorCamera_.m_PitchDelta) > 1e-6f;
            if (p.allowNavTransfer) movement = moved; // Real UI cancellation may retire damping after movement.
            break;
        case Movement::Pan: movement = moved && m_EditorCamera_.m_FocalPoint != p.focal; break;
        case Movement::Dolly: movement = moved && std::abs(m_EditorCamera_.GetDistance()-p.distance)>1e-5f; break;
        }
        const bool pick = !p.expectedReads || (m_HoveredEntity &&
            m_HoveredEntity.GetName() == PreEditorCamera::Targets[0]);
        const auto highlightAge = ImGui::GetTime()-m_ViewportHighlightStart;
        const auto& ui = *ImGui::GetCurrentContext();
        const bool legitimateUITransfer = p.allowNavTransfer && !m_ViewportActive && ui.NavWindow &&
            ui.NavWindow != ImGui::FindWindowByName("Viewport");
        const bool highlight = !p.expectActive || legitimateUITransfer ||
            ((p.expectedActivations ? m_ViewportHighlightStart>=0 : m_ViewportHighlightStart==p.highlightStart) &&
             (m_ViewportHighlightStart<0 || highlightAge<.25 || m_ViewportHighlightOpacity==0));
        const bool passed = highlight && movement && reads==p.expectedReads && activations==p.expectedActivations &&
            (m_ViewportActive==p.expectActive || legitimateUITransfer) && capture==p.expectCapture &&
            (SDL_GetRelativeMouseMode()==SDL_TRUE)==p.expectCapture && pick;
        p.rows << p.step << ',' << p.name << ',' << passed << ',' << reads << ',' << activations << ','
            << m_ViewportActive << ',' << capture << ',' << moved << '\n';
        if (!passed) return fail("Movement/picking/activation/capture oracle");
        if (++p.step == 65)
        {
            if (!SetCameraView(CameraView::Editor) || !CancelCameraGesture()) return fail("Gesture teardown");
            p.complete=true; probe.ui=false; p.rows.flush();
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_GESTURES_PASS cases=65 held_alt_gestures=20 frames={}",p.frames);
            return true;
        }
        p.tick=0;
    }
    // Neutralize only old camera damping at the fixture boundary. Keep physical keys,
    // button ownership, capture and all routing intact (including Alt held for 20 gestures).
    m_EditorCamera_.m_Yaw += m_EditorCamera_.m_YawDelta;
    m_EditorCamera_.m_Pitch += m_EditorCamera_.m_PitchDelta;
    m_EditorCamera_.m_YawDelta = m_EditorCamera_.m_PitchDelta = 0;
    m_EditorCamera_.m_PositionDelta = {};
    m_EditorCamera_.m_FocalPoint = m_EditorCamera_.GetPosition() +
        m_EditorCamera_.GetForwardDirection()*m_EditorCamera_.GetDistance();
    p.position=m_EditorCamera_.GetPosition(); p.orientation=m_EditorCamera_.GetOrientation();
    p.focal=m_EditorCamera_.m_FocalPoint; p.distance=m_EditorCamera_.GetDistance();
    p.reads=m_GesturePickReadbacks; p.activations=m_ViewportActivations;
    p.highlightStart=m_ViewportHighlightStart;
    p.expectedReads=0; p.expectedActivations=0; p.expectActive=true; p.expectCapture=false;
    p.movement=Movement::None; p.allowNavTransfer=false;
    const auto r=input.Routing();
    const auto uiPoint=PreEditorInput::uiProbe.points[4];
    const glm::vec2 widgetPoint{uiPoint.x,uiPoint.y};
    const glm::vec2 center{(r.left+r.right)*.5f,(r.top+r.bottom)*.5f};
    const glm::vec2 outside[4]={{r.left-2,center.y},{center.x,r.top-2},{r.right+2,center.y},{center.x,r.bottom+2}};
    const auto button=[&](unsigned b,bool down,glm::vec2 pt) { return GestureButton(*this,b,down,pt); };
    const auto motion=[&](glm::vec2 pt,int dx=0,int dy=0) { return GestureMotion(*this,pt,dx,dy); };
    const auto alt=[&](bool down) { return GestureKey(*this,SDL_SCANCODE_LALT,down,down?KMOD_LALT:KMOD_NONE); };
    const auto click=[&](glm::vec2 pt) { return motion(pt) && button(SDL_BUTTON_LEFT,true,pt) && button(SDL_BUTTON_LEFT,false,pt); };
    const auto gesture=[&](unsigned b) { return motion(center) && button(b,true,center) &&
        motion(center,12,-9) && button(b,false,center); };
    const auto targetClick=[&]() {
        if (!m_CameraSnapshot) return false;
        const auto pixel=m_CameraSnapshot->WorldToPixel(PreEditorCamera::Points[0]);
        if (!pixel) return false;
        const auto& v=m_CameraSnapshot->Description().viewport;
        const glm::vec2 point(v.logicalOrigin+pixel->value/glm::dvec2(v.pixelSize)*v.logicalSize);
        return PreEditorCamera::NativePointVisible(glm::dvec2(point)) && click(point);
    };
    bool ok=true;
    if (p.step>=4 && p.step<24)
    {
        p.name="held_alt_consecutive_orbit"; p.movement=Movement::Orbit;
        ok=(input.GetPhysicalState().modifiers&LeftAlt) && gesture(SDL_BUTTON_LEFT);
    }
    else if (p.step>=25 && p.step<=28)
    {
        p.name="repeated_alt_press_release"; p.movement=Movement::Orbit;
        ok=alt(true)&&gesture(SDL_BUTTON_LEFT)&&alt(false);
    }
    else if (p.step>=40 && p.step<=47)
    {
        if (p.step%2==0)
        {
            p.name="new_outside_press_rejected"; p.expectActive=false;
            const auto point=outside[(p.step-40)/2];
            ok=alt(true)&&motion(point)&&button(SDL_BUTTON_LEFT,true,point)&&motion(point,12,-9)&&
                button(SDL_BUTTON_LEFT,false,point)&&alt(false);
        }
        else { p.name="fresh_activation_after_UI_transfer"; p.expectedActivations=1; ok=click(center); }
    }
    else switch(p.step)
    {
    case 0:
        p.name="hover_never_activates"; p.expectActive=false;
        ok=GestureFocus(*this,false)&&GestureFocus(*this,true)&&motion(center); break;
    case 1: p.name="activation_consumes_down_up"; p.expectedActivations=1; ok=click(center); break;
    case 2: p.name="fresh_plain_GPU_pick"; p.expectedReads=1; ok=targetClick(); break;
    case 3: p.name="alt_before_mouse"; ok=alt(true); break;
    case 24: p.name="alt_release_preserves_activation"; ok=alt(false); break;
    case 29:
        p.name="same_poll_alt_up_before_button_up"; p.movement=Movement::Orbit;
        ok=alt(true)&&motion(center)&&button(SDL_BUTTON_LEFT,true,center)&&motion(center,12,-9)&&
            alt(false)&&button(SDL_BUTTON_LEFT,false,center); break;
    case 30:
        p.name="mouse_first_alt_promotion"; p.movement=Movement::Orbit;
        ok=motion(center)&&button(SDL_BUTTON_LEFT,true,center)&&alt(true)&&motion(center,12,-9)&&
            button(SDL_BUTTON_LEFT,false,center)&&alt(false); break;
    case 31: p.name="left_alt_middle_pan"; p.movement=Movement::Pan; ok=alt(true)&&gesture(SDL_BUTTON_MIDDLE)&&alt(false); break;
    case 32: p.name="left_alt_right_dolly"; p.movement=Movement::Dolly; ok=alt(true)&&gesture(SDL_BUTTON_RIGHT)&&alt(false); break;
    case 33:
        p.name="right_alt_preserves_plain_picking"; p.expectedReads=1; p.allowNavTransfer=true;
        ok=GestureKey(*this,SDL_SCANCODE_RALT,true,KMOD_RALT)&&targetClick()&&
            GestureKey(*this,SDL_SCANCODE_RALT,false,KMOD_NONE); break;
    case 34:
        p.name="right_alt_preserves_right_fly"; p.movement=Movement::Fly; p.allowNavTransfer=true;
        if (!m_ViewportActive) { p.expectedActivations=1; ok=click(center); }
        ok=ok&&GestureKey(*this,SDL_SCANCODE_RALT,true,KMOD_RALT)&&gesture(SDL_BUTTON_RIGHT)&&
            GestureKey(*this,SDL_SCANCODE_RALT,false,KMOD_NONE); break;
    case 35:
        p.name="same_poll_orbit_crosses_left_boundary"; p.movement=Movement::Orbit; p.expectCapture=true;
        if (!m_ViewportActive) { p.expectedActivations=1; ok=click(center); }
        ok=ok&&alt(true)&&motion(center)&&button(SDL_BUTTON_LEFT,true,center)&&motion(outside[0],12,-9); break;
    case 36: case 37: case 38:
        p.name="owned_orbit_crosses_boundary"; p.movement=Movement::Orbit; p.expectCapture=true;
        ok=motion(outside[p.step-35],12,-9); break;
    case 39:
        p.name="outside_release_keeps_activation"; ok=button(SDL_BUTTON_LEFT,false,outside[3])&&alt(false); break;
    case 48:
        p.name="relative_capture_setup"; p.expectCapture=true;
        ok=alt(true)&&motion(center)&&button(SDL_BUTTON_LEFT,true,center); break;
    case 49:
        p.name="relative_release_then_fresh_same_poll"; p.movement=Movement::Orbit;
        ok=button(SDL_BUTTON_LEFT,false,center)&&gesture(SDL_BUTTON_LEFT)&&alt(false); break;
    case 50:
        p.name="focus_cancel_owned_orbit_setup"; p.movement=Movement::Orbit; p.expectCapture=true;
        ok=alt(true)&&motion(center)&&button(SDL_BUTTON_LEFT,true,center)&&motion(center,12,-9); break;
    case 51:
        p.name="cancel_before_regain_fresh_same_poll"; p.movement=Movement::Orbit; p.expectedActivations=1;
        ok=motion(center,30,20)&&GestureFocus(*this,false)&&GestureFocus(*this,true)&&alt(true)&&
            click(center)&&gesture(SDL_BUTTON_LEFT)&&alt(false); break;
    case 52:
        p.name="focus_regain_stays_inactive"; p.expectActive=false;
        ok=GestureFocus(*this,false)&&GestureFocus(*this,true)&&motion(center); break;
    case 53:
        p.name="UI_owned_alt_activation_barrier"; p.expectedActivations=1;
        ok=alt(true)&&click(center); break;
    case 54:
        p.name="orbit_with_alt_owned_before_activation"; p.movement=Movement::Orbit;
        ok=(input.GetPhysicalState().modifiers&LeftAlt)&&gesture(SDL_BUTTON_LEFT); break;
    case 55:
        p.name="plain_GPU_pick_after_alt_gestures"; p.expectedReads=1; ok=alt(false)&&targetClick(); break;
    case 56: p.name="real_UI_appearance_focus_transfer"; probe.ui=true; p.expectActive=false; break;
    case 57:
        p.name="UI_alt_press_owns_text_widget"; p.expectActive=false;
        ok=PreEditorInput::uiProbe.seen[4]&&alt(true)&&motion(widgetPoint)&&
            button(SDL_BUTTON_LEFT,true,widgetPoint); break;
    case 58:
        p.name="UI_alt_motion_release_never_orbits_or_picks"; p.expectActive=false;
        ok=motion(widgetPoint,12,-9)&&
            button(SDL_BUTTON_LEFT,false,widgetPoint)&&alt(false); break;
    case 59:
        p.name="active_text_has_priority_over_view_activation"; p.expectActive=false;
        ok=PreEditorInput::uiProbe.textActive&&click(center); break;
    case 60:
        p.name="UI_escape_retires_text"; p.expectActive=false;
        ok=GestureKey(*this,SDL_SCANCODE_ESCAPE,true,KMOD_NONE)&&GestureKey(*this,SDL_SCANCODE_ESCAPE,false,KMOD_NONE); break;
    case 61: p.name="fresh_activation_after_UI"; p.expectedActivations=1; ok=click(center); break;
    case 62: p.name="plain_GPU_pick_after_UI"; p.expectedReads=1; ok=targetClick(); break;
    case 63:
        p.name="scene_role_rejects_alt_camera_and_pick";
        ok=SetCameraView(CameraView::ActiveScene)&&alt(true)&&gesture(SDL_BUTTON_LEFT)&&alt(false); break;
    case 64:
        p.name="preview_role_rejects_alt_camera_and_pick";
        ok=SetCameraView(CameraView::Preview,m_SceneCameras[0])&&alt(true)&&gesture(SDL_BUTTON_LEFT)&&alt(false); break;
    default: return fail("Unknown gesture case");
    }
    if (!ok) return fail("Gesture input prerequisite/injection failed");
    p.tick=1;
    return true;
}

bool RigidBodySimulationApp::ValidateLifecycleUpdate()
{
    using namespace PreEditorLifecycle;
    ++probe.updates;
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
    PickTraceValues("post_poll");
#endif
    FocusFrame();
    FocusMark(100);
    SetManualFrameRateLimit(0);
    const auto fail = [this](const char* message)
    {
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        PreEditorLifecycle::FinishFocusTrace(message);
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        PickTrace::Finish(message);
#endif
        FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Phase 17 camera lifecycle", {},
                     "bounded diagnostic", {}, message});
        return false;
    };
    auto& input = *GetInputManager();
    auto* native = SDL_GetWindowFromID(GetWindow()->GetWindowID());
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
    if (probe.updates == 1)
    {
        // Begin before the original fixture activation at update5. No extra pump.
        if (!PreEditorCamera::FocusTrace::Begin(native)) return fail("Trace hook initialization");
        FocusFrame();
        auto observer = input.Observe([this](const InputEvent& event)
        {
            const auto flags = std::uint64_t(event.kind) | (std::uint64_t(event.cancellation) << 8) |
                (std::uint64_t(event.cleanup) << 16) | (std::uint64_t(event.handled) << 17) |
                (std::uint64_t(event.destination) << 24);
            const auto state = std::uint64_t(GetInputManager()->Focused()) |
                (std::uint64_t(GetInputManager()->PointerCapture()) << 8) |
                (std::uint64_t(SDL_GetRelativeMouseMode()) << 16);
            FocusMark(110, event.sequence, flags, event.window, state);
            FocusMark(111, event.sequence, unsigned(event.key), unsigned(event.button), event.modifiers);
            // The state is sampled after Route/CancelInput's existing dispatch point;
            // nested cleanup can precede the physical focus assignment.
            if (event.kind == InputKind::FocusLost || event.kind == InputKind::FocusGained ||
                event.kind == InputKind::Cancel)
                FocusSample(121, *this);
        });
        if (!observer) return fail("Focus observation subscription");
        probe.focusTrace = std::move(*observer);
        FocusMark(122, 1);
        FocusSample(123, *this);
    }
#endif
    if (probe.updates < 5) return true;
    if (!probe.initialized)
    {
        if (!native || !ActivateNative(native, *this)) return fail("Native activation");
        FocusSample(102, *this);
        const auto prepared = PreEditorInput::PrepareNativeFixture(*this);
        FocusMark(103, bool(prepared), PreEditorInput::nativeForeground.held,
                  reinterpret_cast<std::uintptr_t>(PreEditorInput::nativeForeground.window));
        FocusSample(124, *this);
        if (!prepared) return fail(prepared.error().message.c_str());
        if (auto checked = Cpu(*m_SceneResources); !checked) return fail(checked.error().message.c_str());
        if (auto checked = PreEditorInput::CheckState(*this); !checked) return fail(checked.error().message.c_str());
        if (auto checked = NativePollChecks(*this); !checked) return fail(checked.error().message.c_str());
        // The new focus policy requires a consumed activation click before controls/costs.
        if (!Routing(*this) || !Button(*this, SDL_BUTTON_LEFT, true) ||
            !Button(*this, SDL_BUTTON_LEFT, false) || !Pump(*this) || !m_ViewportActive ||
            m_ViewPickRequested || input.PointerCapture() != InputLayer::None)
            return fail("Explicit Viewport activation prerequisite");
        PreEditorInput::Describe(*this);
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_RENDERER vendor={} renderer={} version={} swap_interval={} manual_cap={}",
            reinterpret_cast<const char*>(glGetString(GL_VENDOR)), reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION)), GetWindow()->GetSwapInterval(), GetManualFrameRateLimit());
        const auto originalEditor = m_EditorCamera_;
        const bool paused = m_IsPause, debug = m_IsShowDebugBoundingBox;
#if defined(GENGINE_CONFIG_RELEASE)
        if (auto measured = Measure(*this, "editor"); !measured) return fail(measured.error().message.c_str());
        if (!SetCameraView(CameraView::ActiveScene)) return fail("Scene input measurement binding");
        if (auto measured = Measure(*this, "scene"); !measured) return fail(measured.error().message.c_str());
        probe.costDone = true;
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        PickTrace::Arm(*this);
#endif
        m_EditorCamera_ = originalEditor;
        if (!SetCameraView(CameraView::Editor) || !Routing(*this)) return fail("Input fixture routing");
        if (!PreEditorLifecycle::NativeTap(*this, SDL_SCANCODE_P) || !m_IsPause ||
            !PreEditorLifecycle::NativeTap(*this, SDL_SCANCODE_R) || m_IsPause ||
            !PreEditorLifecycle::NativeTap(*this, SDL_SCANCODE_SPACE) || m_IsShowDebugBoundingBox == debug)
            return fail("Preserved Game actions");
        const auto beforeShortcutPosition = m_EditorCamera_.GetPosition();
        const auto beforeShortcutOrientation = m_EditorCamera_.GetOrientation();
        if (!PreEditorLifecycle::NativeTap(*this, SDL_SCANCODE_I)) return fail("Editor viewpoint input");
        const glm::dvec3 frontPosition(0, 20, std::sqrt(5000.0));
        if (!Check((m_EditorCamera_.GetPosition() != beforeShortcutPosition ||
                    m_EditorCamera_.GetOrientation() != beforeShortcutOrientation) &&
                   PreEditorCamera::Max(glm::dvec3(m_EditorCamera_.GetPosition()) - frontPosition) <=
                       PreEditorCamera::WorldLimit(frontPosition) &&
                   PreEditorCamera::Max(PreEditorCamera::Rotate(glm::dquat(m_EditorCamera_.GetOrientation()), {0,0,-1}) -
                                       glm::dvec3(0,0,-1)) <= 2e-5 &&
                   PreEditorCamera::Max(PreEditorCamera::Rotate(glm::dquat(m_EditorCamera_.GetOrientation()), {0,1,0}) -
                                       glm::dvec3(0,1,0)) <= 2e-5,
                   "editor_viewpoint_shortcut_fixed_front_pose")) return fail("Editor viewpoint pose");
        const float distance = m_EditorCamera_.GetDistance();
        if (!Wheel(*this) || !Pump(*this) || !Check(m_EditorCamera_.GetDistance() != distance, "editor_wheel"))
            return fail("Editor wheel consumer");
        FocusMaterialCamera(m_EditorCamera_, {0,7,6}, .12f, 0, 28.f);
        if (!CancelCameraGesture() || !Routing(*this)) return fail("Editor capture preparation");
        const auto beforeFly = m_EditorCamera_.GetPosition();
        if (!Button(*this, SDL_BUTTON_RIGHT, true) || !Key(*this, SDL_SCANCODE_W, true) || !Motion(*this) || !Pump(*this))
            return fail("Fly injection");
        m_EditorCamera_.OnUpdate(Timestep(.016f));
        if (!Check(input.PointerCapture() == InputLayer::View && m_EditorCamera_.GetPosition() != beforeFly,
                   "editor_real_fly_capture")) return fail("Editor fly");
        const auto movingPosition = m_EditorCamera_.GetPosition();
        const auto movingOrientation = m_EditorCamera_.GetOrientation();
        const auto movingDistance = m_EditorCamera_.GetDistance();
        if (!Check(m_EditorCamera_.m_YawDelta != 0 && m_EditorCamera_.m_PitchDelta != 0,
                   "switch_fixture_has_angular_damping")) return fail("Moving switch fixture");
        if (!SetCameraView(CameraView::ActiveScene)) return fail("Switch during capture");
        if (!Check(m_EditorCamera_.GetPosition() == movingPosition &&
                   m_EditorCamera_.GetOrientation() == movingOrientation &&
                   m_EditorCamera_.GetDistance() == movingDistance,
                   "switch_preserves_effective_editor_pose")) return fail("Switch moved Editor pose");
        const auto savedPosition = m_EditorCamera_.GetPosition();
        const auto savedOrientation = m_EditorCamera_.GetOrientation();
        const auto savedDistance = m_EditorCamera_.GetDistance();
        if (!Check(input.PointerCapture() == InputLayer::None && SDL_GetRelativeMouseMode() == SDL_FALSE &&
                   m_EditorCamera_.m_PositionDelta == Vec3f{} && m_EditorCamera_.m_YawDelta == 0 &&
                   m_EditorCamera_.m_PitchDelta == 0, "switch_cancels_capture_and_damping")) return fail("switch cancellation");
        const auto aEntity = *m_ActiveScene->RenderData().Resolve(m_SceneCameras[0]);
        const auto authoredPose = m_ActiveScene->ReadRegistry().get<Transform3DComponent>(aEntity).Translation;
        for (const auto role : {CameraView::ActiveScene, CameraView::Preview})
        {
            if (!SetCameraView(role, m_SceneCameras[1]) || !Routing(*this) ||
                !PreEditorLifecycle::NativeTap(*this, SDL_SCANCODE_I) || !Wheel(*this) ||
                !Button(*this, SDL_BUTTON_RIGHT, true) || !Key(*this, SDL_SCANCODE_W, true) || !Motion(*this) || !Pump(*this))
                return fail("Scene/Preview control injection");
            if (!Check(m_EditorCamera_.GetPosition() == savedPosition && m_EditorCamera_.GetOrientation() == savedOrientation &&
                    m_EditorCamera_.GetDistance() == savedDistance &&
                    m_ActiveScene->ReadRegistry().get<Transform3DComponent>(aEntity).Translation == authoredPose &&
                    input.PointerCapture() == InputLayer::None, "scene_preview_controls_preserve_authored_and_editor"))
                return fail("Scene/Preview suppression");
        }
        if (!EndCameraPreview() || m_CameraView != CameraView::ActiveScene || !SetCameraView(CameraView::Editor))
            return fail("Preview restores prior role");
        m_EditorCamera_.OnUpdate(Timestep(.016f));
        if (!Check(input.PointerCapture() == InputLayer::None && m_EditorCamera_.GetPosition() == savedPosition &&
                   m_EditorCamera_.GetOrientation() == savedOrientation,
                   "return_requires_fresh_press")) return fail("held input resumed");
        if (!Button(*this, SDL_BUTTON_RIGHT, false) || !Key(*this, SDL_SCANCODE_W, false) || !Pump(*this) ||
            !Routing(*this) || !Button(*this, SDL_BUTTON_RIGHT, true) || !Motion(*this) || !Pump(*this)) return fail("fresh press");
        m_EditorCamera_.OnUpdate(Timestep(.016f));
        if (!Check(input.PointerCapture() == InputLayer::View, "fresh_press_captures")) return fail("fresh capture");
        if (!Button(*this, SDL_BUTTON_RIGHT, false) || !Pump(*this) || !CancelCameraGesture()) return fail("fly release");
        for (const auto button : {SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE})
        {
            if (!Routing(*this) || !Key(*this, SDL_SCANCODE_LALT, true) || !Button(*this, button, true) ||
                !Motion(*this) || !Pump(*this)) return fail("orbit/pan injection");
            const auto q = m_EditorCamera_.GetOrientation();
            const auto focal = m_EditorCamera_.m_FocalPoint;
            m_EditorCamera_.OnUpdate(Timestep(.016f));
            if (!Check(input.PointerCapture() == InputLayer::View &&
                    (button == SDL_BUTTON_LEFT ? m_EditorCamera_.GetOrientation() != q : m_EditorCamera_.m_FocalPoint != focal),
                    "editor_real_orbit_pan")) return fail("orbit/pan control");
            const auto orbitPosition = m_EditorCamera_.GetPosition();
            const auto orbitOrientation = m_EditorCamera_.GetOrientation();
            const auto orbitDistance = m_EditorCamera_.GetDistance();
            if (button == SDL_BUTTON_LEFT &&
                !Check(m_EditorCamera_.m_YawDelta != 0 && m_EditorCamera_.m_PitchDelta != 0,
                       "orbit_switch_fixture_has_angular_damping")) return fail("Moving orbit switch fixture");
            if (!SetCameraView(CameraView::ActiveScene)) return fail("Orbit/pan Scene switch");
            if (!Check(m_EditorCamera_.GetPosition() == orbitPosition &&
                       m_EditorCamera_.GetOrientation() == orbitOrientation &&
                       m_EditorCamera_.GetDistance() == orbitDistance,
                       "orbit_pan_switch_preserves_effective_pose")) return fail("Orbit/pan switch moved Editor pose");
            if (!SetCameraView(CameraView::Editor)) return fail("Orbit/pan Editor return");
            m_EditorCamera_.OnUpdate(Timestep(0));
            if (!Check(input.PointerCapture() == InputLayer::None &&
                       PreEditorCamera::Max(glm::dvec3(m_EditorCamera_.GetPosition()) - glm::dvec3(orbitPosition)) <=
                           PreEditorCamera::WorldLimit(glm::dvec3(orbitPosition)) &&
                       m_EditorCamera_.GetOrientation() == orbitOrientation &&
                       m_EditorCamera_.GetDistance() == orbitDistance &&
                       m_EditorCamera_.m_PositionDelta == Vec3f{} &&
                       m_EditorCamera_.m_YawDelta == 0 && m_EditorCamera_.m_PitchDelta == 0,
                       "orbit_pan_idle_return_preserves_effective_pose")) return fail("Orbit/pan idle return moved Editor pose");
            if (!Button(*this, button, false) || !Key(*this, SDL_SCANCODE_LALT, false) || !Pump(*this) ||
                !CancelCameraGesture()) return fail("orbit/pan release");
        }
        for (const auto lost : {SDL_WINDOWEVENT_FOCUS_LOST, SDL_WINDOWEVENT_HIDDEN, SDL_WINDOWEVENT_MINIMIZED})
        {
            if (!Routing(*this)) return fail("focus fixture routing");
            if (!m_ViewportActive && (!Button(*this, SDL_BUTTON_LEFT, true) ||
                !Button(*this, SDL_BUTTON_LEFT, false) || !Pump(*this))) return fail("focus fixture activation");
            if (!Button(*this, SDL_BUTTON_RIGHT, true) || !Pump(*this)) return fail("focus capture setup");
            m_EditorCamera_.OnUpdate(Timestep(.016f));
            SDL_Event event{}; event.type = SDL_WINDOWEVENT;
            event.window.windowID = GetWindow()->GetWindowID(); event.window.event = lost;
            if (Push(event, 25) != 1) return fail("focus loss injection");
            GetEventManager()->PollEvents();
            if (!Check(!input.Focused() && input.PointerCapture() == InputLayer::None &&
                       SDL_GetRelativeMouseMode() == SDL_FALSE, "focus_hide_minimize_cancel")) return fail("focus cancellation");
            for (const auto restored : {SDL_WINDOWEVENT_RESTORED, SDL_WINDOWEVENT_SHOWN, SDL_WINDOWEVENT_FOCUS_GAINED})
            { event.window.event = restored; if (Push(event, 25) != 1) return fail("focus restore injection"); }
            if (!Button(*this, SDL_BUTTON_RIGHT, false) || !Pump(*this) || !CancelCameraGesture()) return fail("focus restore");
        }
        const auto savedScene = m_ActiveScene;
        const auto cameras = m_SceneCameras;
        const auto oldView = m_EditorViewIdentity;
        auto emptyScene = CreateRefPtr<_Scene>();
        if (!BindCameraScene(emptyScene)) return fail("Scene rebind");
        const auto temporaryView = m_EditorViewIdentity;
        if (!Check(!savedScene->RenderData().Resolve(oldView) && m_CameraView == CameraView::Editor &&
                   !m_PreviewCamera && !m_SceneCameras[0] && input.PointerCapture() == InputLayer::None,
                   "rebind_retires_old_view_clears_references")) return fail("rebind lifecycle");
        if (!BindCameraScene(savedScene)) return fail("Scene restore binding");
        m_SceneCameras = cameras;
        if (!Check(!emptyScene->RenderData().Resolve(temporaryView) && m_EditorViewIdentity != oldView,
                   "rebind_back_fresh_identity")) return fail("rebind stale");
        m_IsPause = paused; m_IsShowDebugBoundingBox = debug;
        FocusMaterialCamera(m_EditorCamera_, {0,7,6}, .12f, 0, 28.f);
        for (unsigned slot = 0; slot != 2; ++slot)
        {
            auto entity = m_ActiveScene->RenderData().Resolve(m_SceneCameras[slot]);
            if (!entity || !m_ActiveScene->SetLocalPose(_Entity{*entity, m_ActiveScene.get()},
                    glm::vec3(Position(slot == 1)), glm::quat(Orientation(slot == 1)))) return fail("authored diagnostic pose");
        }
        probe.editorPosition = m_EditorCamera_.GetPosition();
        probe.editorOrientation = m_EditorCamera_.GetOrientation();
        probe.editorDistance = m_EditorCamera_.GetDistance();
        // Transfer the process foreground lease from the input fixture to the frame fixture.
        if (!ForegroundHandoff(*this))
            return fail("Input-to-camera foreground lease handoff");
        probe.initialized = true;
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_INPUT_PASS fly=true orbit=true pan=true wheel=true shortcut=true suppression=true capture=true rebind=true");
    }
    if (probe.waitingRestore)
    {
        if (!std::ifstream("native-restored.json") || IsRenderingSuspended() || input.PointerCapture() != InputLayer::None)
            return fail("Native restore observation/capture");
        probe.waitingRestore = false;
    }
    if (probe.segment >= 12)
    {
        // A live camera with invalid projection must make this frame unavailable,
        // then produce a fresh snapshot after restoration; never reuse the old one.
        if (probe.invalidStage < 3)
        {
            auto entity = m_ActiveScene->RenderData().Resolve(m_SceneCameras[0]);
            if (!entity) return fail("Invalid-view fixture identity");
            auto& intent = m_ActiveScene->Reg().get<RenderCameraComponent>(*entity);
            if (probe.invalidStage == 0) intent.verticalFovRadians = 0;
            else if (probe.invalidStage == 1)
            {
                if (!Check(!m_CameraAvailable && !m_CameraSnapshot && !m_FramePickPixel && !m_CameraUnavailableDiagnostic.empty(),
                           "invalid_projection_unavailable_no_stale_snapshot")) return fail("invalid view reused camera");
                intent.verticalFovRadians = glm::radians(45.f);
            }
            else if (!Check(m_CameraAvailable && m_CameraSnapshot && m_CameraView == CameraView::ActiveScene &&
                            m_CameraUnavailableDiagnostic.empty(),
                            "restored_projection_fresh_snapshot")) return fail("restored snapshot unavailable");
            ++probe.invalidStage;
            return true;
        }
        if (!gestureProbe.complete) return ValidateViewportGestures();
        if (!probe.uiStarted)
        {
            if (!SetCameraView(CameraView::Editor)) return fail("UI acceptance view");
            probe.ui = probe.uiStarted = true;
            probe.uiPosition = m_EditorCamera_.GetPosition();
            probe.uiPaused = m_IsPause; probe.uiDebug = m_IsShowDebugBoundingBox;
            probe.uiPicked = static_cast<std::uint32_t>(m_HoveredEntity);
        }
        if (m_EditorCamera_.GetPosition() != probe.uiPosition || m_IsPause != probe.uiPaused ||
            m_IsShowDebugBoundingBox != probe.uiDebug || static_cast<std::uint32_t>(m_HoveredEntity) != probe.uiPicked)
            return fail("UI leaked camera/pick/Game action");
        if (auto checked = PreEditorInput::StepUI(*this); !checked) return fail(checked.error().message.c_str());
        if (PreEditorInput::uiProbe.step == 42)
        {
            if (!CancelCameraGesture() || !PreEditorInput::nativeForeground.Release() ||
                !PreEditorCamera::nativeFixture.Release() || !PreEditorInput::RequestNativeClose()) return fail("Diagnostic close cleanup");
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
            PreEditorLifecycle::FinishFocusTrace("diagnostic_complete");
#endif
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_PASS segments=12 frames=96 readbacks={} checks={} cost={}",
                                       probe.readbacks, probe.checks, probe.costDone);
            return false;
        }
        return true;
    }
    if (probe.applied != probe.segment)
    {
        if (!ActivateNative(native, *this)) return fail("Segment native activation");
        m_HoveredEntity = {};
        switch (probe.segment)
        {
        case 0: if (!SetCameraView(CameraView::Editor)) return fail("Editor segment"); break;
        case 1: if (!SetCameraView(CameraView::ActiveScene)) return fail("Scene A segment"); break;
        case 2: if (!SetCameraView(CameraView::Preview, m_SceneCameras[1])) return fail("Preview B segment"); break;
        case 3: if (!EndCameraPreview()) return fail("Preview return A"); break;
        case 4: if (!SelectSceneCamera(m_SceneCameras[1])) return fail("Activate B"); break;
        case 5: if (!SetCameraView(CameraView::Preview, m_SceneCameras[0])) return fail("Preview A segment"); break;
        case 6:
            probe.replaced = m_SceneCameras[0];
            if (!RetireSceneCamera(0, true)) return fail("Replace preview A");
            break;
        case 7:
            if (!Check(m_CameraView == CameraView::ActiveScene && !m_ActiveScene->RenderData().Resolve(probe.replaced) &&
                       !m_CameraDiagnostic.empty(),
                       "preview_replacement_restores_previous_exact_role")) return fail("preview replacement");
            probe.replaced = m_SceneCameras[1];
            if (!RetireSceneCamera(1, true)) return fail("Replace active B");
            break;
        case 8:
            if (!Check(!m_ActiveScene->RenderData().Resolve(probe.replaced), "active_replacement_old_stale") ||
                !RetireSceneCamera(0, false)) return fail("Inactive A deletion");
            SDL_SetWindowSize(native, 960, 640);
            if (!RestoreNativeExposureAfterResize(native)) return fail("Native resize exposure");
            ImGui::SetWindowSize("Viewport", {560,580});
            break;
        case 9:
            if (!SetCameraView(CameraView::Editor) || !SetCameraView(CameraView::Preview, m_SceneCameras[1]) ||
                !RetireSceneCamera(1, false)) return fail("Delete active and preview B");
            break;
        case 10:
            SDL_SetWindowSize(native, 1280, 720);
            if (!RestoreNativeExposureAfterResize(native)) return fail("Native restore exposure");
            ImGui::SetWindowSize("Viewport", {880,660});
            if (!Check(m_CameraView == CameraView::Editor && m_EditorCamera_.GetPosition() == probe.editorPosition &&
                       m_EditorCamera_.GetOrientation() == probe.editorOrientation && !m_CameraDiagnostic.empty(),
                       "retired_preview_restores_saved_editor")) return fail("Editor preservation");
            break;
        case 11:
            if (!CreateSceneCamera(0) || !CreateSceneCamera(1) || !SelectSceneCamera(m_SceneCameras[0]) ||
                !SetCameraView(CameraView::ActiveScene)) return fail("Fresh authored camera recreation");
            break;
        }
        probe.expected = probe.segment == 0 || probe.segment == 9 || probe.segment == 10 ? m_EditorViewIdentity :
            probe.segment == 1 || probe.segment == 3 || probe.segment == 5 || probe.segment == 11 ? m_SceneCameras[0] : m_SceneCameras[1];
        probe.applied = probe.segment; probe.stable = probe.observed = 0; probe.previousSize = {};
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_SEGMENT segment={} scale={}", probe.segment, Scale());
    }
    return true;
}

ScheduleResult RigidBodySimulationApp::ValidateLifecycleFrame(const FrameCamera& camera)
{
    using namespace PreEditorLifecycle;
    if (!GpuChecks()) return {};
    FocusFrame();
    FocusMark(101);
    const auto fail = [](const char* message) -> ScheduleResult
    {
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        PreEditorLifecycle::FinishFocusTrace(message);
#endif
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        PickTrace::Finish(message);
#endif
        return std::unexpected(ScheduleError{FrameStage::Pass,
            PlatformError{PlatformErrorCode::Initialization, "Phase 17 camera frame", message}});
    };
    const auto* snapshot = camera.Snapshot();
    if (!snapshot || !camera.MatchesSnapshot()) return fail("Frame camera/snapshot disagreement");
    const auto& desc = snapshot->Description();
    const bool rotated = probe.segment == 2 || probe.segment == 4 || (probe.segment >= 6 && probe.segment <= 8);
    const auto expectedProjection = rotated ? CameraProjection::Orthographic : CameraProjection::Perspective;
    auto pixels = ToFramebufferPixels({float(desc.viewport.logicalSize.x), float(desc.viewport.logicalSize.y)}, {Scale(),Scale()});
    const auto window = GetWindow()->GetLogicalSize();
    const bool small = probe.segment == 8 || probe.segment == 9;
    if (!pixels || desc.viewport.pixelSize != glm::uvec2(pixels->Width,pixels->Height) ||
        window.Width != (small ? 960 : 1280) || window.Height != (small ? 640 : 720))
    { probe.stable = 0; return {}; }
    if (probe.previousSize != desc.viewport.pixelSize) probe.stable = 0;
    probe.previousSize = desc.viewport.pixelSize;
    if (++probe.stable <= 2) return {};
    ++probe.observed;
    FocusFrame();
    if (!FocusGate(*this)) return fail("Native foreground lost");
    if (camera.entity != probe.expected || desc.projection != expectedProjection ||
        PreEditorCamera::Max(desc.position - Position(rotated)) > PreEditorCamera::WorldLimit(Position(rotated)) ||
        PreEditorCamera::Max(PreEditorCamera::Rotate(desc.orientation, {0,0,-1}) -
                            PreEditorCamera::Rotate(Orientation(rotated), {0,0,-1})) > 2e-5)
        return fail("Displayed identity/projection/world pose differs from fixed oracle");
    auto oracle = desc;
    oracle.position = Position(rotated); oracle.orientation = Orientation(rotated);
    oracle.verticalFovRadians = glm::radians(45.f); oracle.nearPlane = .1f;
    oracle.farPlane = 1000.f; oracle.orthographicHeight = 20;
    if (!MatrixAndCoordinates(*snapshot, oracle)) return fail("Frozen matrix/inverse/world/NDC tolerances");
    if (probe.pendingPick)
    {
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        const bool present = bool(m_HoveredEntity);
        const bool nameMismatch = present && m_HoveredEntity.GetName() != PreEditorCamera::Targets[0];
        const bool failed = !present || nameMismatch;
        if (!PickTrace::finished)
        {
            PickTrace::firstPick = true; PickTrace::firstPickPassed = !failed;
            PickTraceValues("pending_pick", !failed,
                            {present, static_cast<std::uint32_t>(m_HoveredEntity), present, nameMismatch});
        }
        if (failed) return fail("Same-frame routed GPU pick");
        PickTrace::Finish("first_pick_passed");
#else
        if (!m_HoveredEntity || m_HoveredEntity.GetName() != PreEditorCamera::Targets[0]) return fail("Same-frame routed GPU pick");
#endif
        probe.pendingPick = false;
    }
    const auto& target = m_MousePickFrameBuffer->Buffer().Description();
    if (target.Width != desc.viewport.pixelSize.x || target.Height != desc.viewport.pixelSize.y) return fail("Picking extent mismatch");
    std::array<int,3> ids{};
    for (unsigned i = 0; i != 2; ++i)
    {
        auto reference = desc; reference.position = Position(rotated); reference.orientation = Orientation(rotated);
        const auto expected = PreEditorCamera::Pixel(reference, PreEditorCamera::Points[i]);
        auto projected = snapshot->WorldToPixel(PreEditorCamera::Points[i]);
        const auto logical = desc.viewport.logicalOrigin + expected / glm::dvec2(desc.viewport.pixelSize) * desc.viewport.logicalSize;
        auto ray = snapshot->PixelToRay(expected);
#ifdef GENGINE_LIFECYCLE_ORACLE_EVIDENCE
        PreEditorCamera::OracleEvidence::Evaluation evidence;
        if (evidence.conditions.Record(0, !projected) || evidence.conditions.Record(1, !ray) ||
            (evidence.pixelDelta = projected->value - expected,
             evidence.pixelError = PreEditorCamera::Max(evidence.pixelDelta),
             evidence.conditions.Record(2, evidence.pixelError > .05)) ||
            evidence.conditions.Record(3, !PreEditorCamera::Interior(reference, i, expected, evidence.interior)) ||
            evidence.conditions.Record(4, !PreEditorCamera::NativePointVisible(logical, evidence.native)))
        {
            PreEditorCamera::OracleEvidence::Frame frame;
            frame.updates = probe.updates; frame.segment = probe.segment;
            frame.stable = probe.stable; frame.observed = probe.observed; frame.targetIndex = i;
            frame.entityIndex = camera.entity.index; frame.entityGeneration = camera.entity.generation;
            frame.entityRegistry = camera.entity.registry; frame.scale = Scale();
            frame.requestedNative = small ? glm::uvec2(960, 640) : glm::uvec2(1280, 720);
            frame.sampledNative = {window.Width, window.Height};
            frame.requestedViewportWindow = small ? glm::dvec2(560, 580) : glm::dvec2(880, 660);
            frame.pickingExtent = {target.Width, target.Height};
            frame.convertedExtent = {pixels->Width, pixels->Height};
            PreEditorCamera::OracleEvidence::Dump(frame, *snapshot, reference, PreEditorCamera::Points[i],
                                                  expected, logical, projected, ray, evidence);
            return fail("Projection/ray/interior/visibility oracle");
        }
#else
        if (!projected || !ray || PreEditorCamera::Max(projected->value - expected) > .05 ||
            !PreEditorCamera::Interior(reference, i, expected) || !PreEditorCamera::NativePointVisible(logical))
            return fail("Projection/ray/interior/visibility oracle");
#endif
        const auto distance = glm::dot(PreEditorCamera::Points[i] - ray->origin, ray->direction);
        auto rayPixel = snapshot->WorldToPixel(ray->origin + distance * ray->direction);
        if (!rayPixel || PreEditorCamera::Max(rayPixel->value - expected) > .05 || std::abs(glm::length(ray->direction)-1.) > 2e-5)
            return fail("Ray/render agreement");
        auto readback = snapshot->ReadbackPixelAt(logical);
        const int x = int(std::floor(expected.x)), y = int(target.Height) - 1 - int(std::floor(expected.y));
        if (!readback || *readback != glm::uvec2(x,y)) return fail("Exact floor/Y-flip");
        auto pixel = m_MousePickFrameBuffer->ReadPixel(x,y); ++probe.readbacks;
        if (!pixel) return fail("GPU readback");
        ids[i] = *pixel;
        auto id = m_ActiveScene->RenderData().ResolvePick(m_PickTable, *pixel);
        auto expectedId = m_ActiveScene->RenderData().Identify(m_ActiveScene->FindEntityByName(PreEditorCamera::Targets[i]));
        if (!id || !expectedId || *id != *expectedId) return fail("GPU exact runtime identity");
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
        if (i == 0 && probe.observed == 3)
            PickTraceValues("target", 1, {expectedId->index, expectedId->generation, expectedId->registry});
#endif
        if (i == 0 && probe.observed == 2)
        {
            SDL_WarpMouseInWindow(SDL_GetWindowFromID(GetWindow()->GetWindowID()), int(logical.x), int(logical.y));
#ifdef GENGINE_LIFECYCLE_PICK_TRACE
            PickTraceValues("warp", -1, {}, {logical.x, logical.y});
#endif
            if (!m_ViewportActive)
            {
                SDL_Event activation{}; activation.type = SDL_MOUSEBUTTONDOWN;
                activation.button.windowID = GetWindow()->GetWindowID();
                activation.button.button = SDL_BUTTON_LEFT; activation.button.state = SDL_PRESSED;
                activation.button.x = int(logical.x); activation.button.y = int(logical.y);
                if (Push(activation, 27) != 1) return fail("Viewport activation press");
                activation.type = SDL_MOUSEBUTTONUP; activation.button.state = SDL_RELEASED;
                if (Push(activation, 27) != 1) return fail("Viewport activation release");
            }
        }
        if (i == 0 && probe.observed == 3)
        {
            SDL_Event event{}; event.type = SDL_MOUSEBUTTONDOWN; event.button.windowID = GetWindow()->GetWindowID();
            event.button.button = SDL_BUTTON_LEFT; event.button.state = SDL_PRESSED;
            event.button.x = int(logical.x); event.button.y = int(logical.y);
            if (Push(event, 26) != 1) return fail("Viewport pick injection");
            event.type = SDL_MOUSEBUTTONUP; event.button.state = SDL_RELEASED;
            if (Push(event, 26) != 1) return fail("Viewport pick release");
            probe.pendingPick = true;
        }
    }
    auto background = m_MousePickFrameBuffer->ReadPixel(int(target.Width/4), int(target.Height)-9); ++probe.readbacks;
    if (!background || *background != EntityPickTable::InvalidPixel) return fail("Fixed background pixel");
    ids[2] = *background;
    if (probe.retained && (!probe.retained->MatchesSnapshot() ||
        PreEditorCamera::Max(probe.retained->Snapshot()->Description().position - Position(false)) >
            PreEditorCamera::WorldLimit(Position(false)))) return fail("Retained frame changed after lifecycle operation");
    if (probe.segment == 1 && probe.observed == 1) probe.retained = camera;
    probe.rows << probe.segment << ',' << probe.observed << ',' << target.Width << ',' << target.Height << ',' << Scale()
               << ',' << camera.entity.index << ',' << camera.entity.generation << ',' << camera.entity.registry
               << ',' << ids[0] << ',' << ids[1] << ',' << ids[2] << '\n';
    if (probe.observed == 8)
    {
        if (!PreEditorCamera::Capture(*m_RenderTarget, probe.segment)) return fail("Framebuffer evidence capture");
        probe.rows.flush(); if (!probe.rows) return fail("Frame evidence write");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_17_SEGMENT_PASS segment={} frames=8", probe.segment);
        if (probe.segment == 9)
        {
            if (!PreEditorInput::nativeForeground.Release() || !PreEditorCamera::nativeFixture.Release()) return fail("Native release before minimize");
            std::ofstream request("native-minimize.request"); request << "Observe minimized state before native restore.\n"; request.close();
            if (!request) return fail("Minimize request evidence");
            probe.waitingRestore = true;
            SDL_MinimizeWindow(SDL_GetWindowFromID(GetWindow()->GetWindowID()));
        }
#ifdef GENGINE_LIFECYCLE_FOCUS_TRACE
        if (probe.segment == 0) PreEditorLifecycle::FinishFocusTrace("segment0_complete_no_focus_failure");
#endif
        ++probe.segment;
    }
    return {};
}
#endif

#ifdef GENGINE_CAMERA_VALIDATION
bool RigidBodySimulationApp::ValidateCameraUpdate()
{
    using namespace PreEditorCamera;
    auto& probe = runtime;
    ++probe.updates;
    const auto fail = [this](const char* reason)
    {
        FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,
                     "Phase 16 camera",
                     {},
                     "bounded diagnostic",
                     {},
                     reason});
        return false;
    };
    if (!probe.cpu)
    {
        if (!m_ActiveScene->ValidateEditorViewIdentity(m_EditorViewIdentity) || !Cpu(m_EditorViewIdentity))
            return fail("Independent CPU camera checks failed; see camera-math.csv");
        probe.cpu = true;
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_16_RENDERER vendor={} renderer={} version={} swap_interval={} manual_cap={}",
            reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION)), GetWindow()->GetSwapInterval(),
            GetManualFrameRateLimit());
    }
    if (probe.waitingRestore)
    {
        if (!std::ifstream("native-restored.json"))
            return fail("Application resumed without external native minimize/restore observation");
        probe.waitingRestore = false;
        if (GetInputManager()->PointerCapture() != InputLayer::None || IsRenderingSuspended())
            return fail("Stale capture or suspended state after native restore");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_16_RESTORE resumed=true capture=none");
    }
    if (probe.segment >= 7)
    {
        if (!nativeFixture.Release())
            return fail("Native fixture cleanup before close failed");
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_16_FOREGROUND_RELEASE before=close success=true");
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        if (SDL_PushEvent(&quit) != 1)
            return fail("Native clean-close request failed");
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_16_PASS segments=7 observed_frames=56 readbacks={}", probe.readbacks);
        return false;
    }
    if (probe.applied != probe.segment)
    {
        m_HoveredEntity = {};
        auto* native = SDL_GetWindowFromID(GetWindow()->GetWindowID());
        if (!native)
            return fail("Missing native diagnostic window");
        if (!ActivateNativeWindow(native))
            return fail("Native diagnostic activation failed");
        FocusMaterialCamera(m_EditorCamera_, {0, 7, 6}, .12f, Rotated() ? .12f : 0.f, 28.f);
        auto& intent = m_CameraValidationIntent;
        intent.projection = Ortho() ? Projection::Orthographic : Projection::Perspective;
        intent.orthographicHeight = 20;
        intent.nearPlane = .1f;
        intent.farPlane = 1000;
        if (probe.segment == 5)
        {
            SDL_SetWindowSize(native, 960, 640);
            ImGui::SetWindowSize("Viewport", {560, 580});
        }
        if (probe.segment == 6)
        {
            SDL_SetWindowSize(native, 1280, 720);
            ImGui::SetWindowSize("Viewport", {880, 660});
        }
        probe.applied = probe.segment;
        probe.stable = probe.observed = 0;
        probe.previousSize = {};
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_16_SEGMENT segment={} ortho={} yaw={} controlled_scale={}",
            probe.segment, Ortho(), Rotated() ? .12 : 0, Scale());
    }
    return true;
}

ScheduleResult RigidBodySimulationApp::ValidateCameraFrame(const FrameCamera& camera)
{
    using namespace PreEditorCamera;
    auto& probe = runtime;
    const auto fail = [](std::string reason) -> ScheduleResult
    {
        Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_16_FAIL {}", reason);
        return std::unexpected(
            ScheduleError{FrameStage::Pass, PlatformError{PlatformErrorCode::Initialization,
                                                          "Phase 16 camera", std::move(reason)}});
    };
    if (probe.segment >= 7)
        return {};
    const auto* snapshot = camera.Snapshot();
    if (!snapshot || !camera.MatchesSnapshot())
        return fail("Missing or contradictory frame camera");
    const auto& d = snapshot->Description();
    const float controlled = static_cast<float>(Scale());
    const auto requested = ToFramebufferPixels({static_cast<float>(d.viewport.logicalSize.x),
                                                static_cast<float>(d.viewport.logicalSize.y)},
                                               {controlled, controlled});
    const auto windowSize = GetWindow()->GetLogicalSize();
    const bool expectedWindow = probe.segment == 5
                                    ? windowSize.Width == 960 && windowSize.Height == 640
                                    : windowSize.Width == 1280 && windowSize.Height == 720;
    const bool expectedPanel =
        probe.segment == 5 ? d.viewport.logicalSize != probe.initialLogical
                           : probe.segment != 6 || d.viewport.logicalSize == probe.initialLogical;
    if (!requested || d.viewport.pixelSize != glm::uvec2(requested->Width, requested->Height) ||
        d.projection != (Ortho() ? Projection::Orthographic : Projection::Perspective) ||
        !expectedWindow || !expectedPanel)
    {
        probe.stable = 0;
        return {};
    }
    if (probe.previousSize != d.viewport.pixelSize)
        probe.stable = 0;
    probe.previousSize = d.viewport.pixelSize;
    if (++probe.stable <= 2)
        return {};
    // Validate actual focus after normal event processing of native activation.
    auto* native = SDL_GetWindowFromID(GetWindow()->GetWindowID());
    if (!AcquireNativeForeground() || !native ||
        !(SDL_GetWindowFlags(native) & SDL_WINDOW_INPUT_FOCUS) || !GetInputManager()->Focused())
        return fail(std::format("Native diagnostic focus was not retained: flags={} engine={}",
                                native ? SDL_GetWindowFlags(native) : 0,
                                GetInputManager()->Focused()));
    ++probe.observed;
    if (probe.segment == 0 && probe.observed == 1)
        probe.initialLogical = d.viewport.logicalSize;
    const auto& picking = m_MousePickFrameBuffer->Buffer().Description();
    if (picking.Width != d.viewport.pixelSize.x || picking.Height != d.viewport.pixelSize.y)
        return fail("Picking target and snapshot extent differ");
    const glm::dquat expectedQ(glm::vec3(-.12f, Rotated() ? -.12f : 0, 0));
    const auto expectedPosition = glm::dvec3(0, 7, 6) - Rotate(expectedQ, {0, 0, -1}) * 28.0;
    if (Max(d.position - expectedPosition) > WorldLimit(expectedPosition) ||
        Max(Rotate(d.orientation, {0, 0, -1}) - Rotate(expectedQ, {0, 0, -1})) > 2e-5)
        return fail("Observed camera differs from frozen diagnostic pose");
    if (probe.pendingPick)
    {
        if (!m_HoveredEntity || m_HoveredEntity.GetName() != Targets[0])
            return fail("Routed viewport click did not select the predeclared source");
        probe.pendingPick = false;
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_16_ROUTED_PICK segment={} entity={}",
                                   probe.segment, m_HoveredEntity.GetName());
    }
    std::array<int, 3> ids{};
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto expected = Pixel(d, Points[i]);
        const auto logical = d.viewport.logicalOrigin +
                             expected / glm::dvec2(d.viewport.pixelSize) * d.viewport.logicalSize;
        if (!NativePointVisible(logical))
            return fail("Declared viewport point is occluded by another native window");
        const auto projected = snapshot->WorldToPixel(Points[i]);
        if (!projected || Max(projected->value - expected) > .05 || !Interior(d, i, expected))
            return fail("Predeclared pick point lacks projection agreement or four-pixel margin");
        const int x = static_cast<int>(std::floor(expected.x));
        const int y =
            static_cast<int>(picking.Height) - 1 - static_cast<int>(std::floor(expected.y));
        const auto pixel = m_MousePickFrameBuffer->ReadPixel(x, y);
        ++probe.readbacks;
        if (!pixel)
            return fail("GPU picking readback failed");
        ids[i] = *pixel;
        auto resolved = m_ActiveScene->RenderData().ResolvePick(m_PickTable, *pixel);
        auto entity = m_ActiveScene->FindEntityByName(Targets[i]);
        const auto expectedId = m_ActiveScene->RenderData().Identify(entity);
        if (!resolved || !expectedId || *resolved != *expectedId)
            return fail(std::format("Exact entity readback mismatch: {} pixel={} x={} y={}",
                                    Targets[i], *pixel, x, y));
        if (i == 0 && (probe.observed == 2 || probe.observed == 3))
        {
            auto* native = SDL_GetWindowFromID(GetWindow()->GetWindowID());
            if (probe.observed == 2)
            {
                SDL_WarpMouseInWindow(native, static_cast<int>(logical.x),
                                      static_cast<int>(logical.y));
                ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));
            }
            else
            {
                SDL_Event event{};
                event.type = SDL_MOUSEBUTTONDOWN;
                event.button.windowID = GetWindow()->GetWindowID();
                event.button.button = SDL_BUTTON_LEFT;
                event.button.state = SDL_PRESSED;
                event.button.x = static_cast<int>(logical.x);
                event.button.y = static_cast<int>(logical.y);
                if (SDL_PushEvent(&event) != 1)
                    return fail("Native picking injection failed");
                event.type = SDL_MOUSEBUTTONUP;
                event.button.state = SDL_RELEASED;
                if (SDL_PushEvent(&event) != 1)
                    return fail("Native picking release failed");
                probe.pendingPick = true;
            }
        }
    }
    const auto background = m_MousePickFrameBuffer->ReadPixel(static_cast<int>(picking.Width / 4),
                                                              static_cast<int>(picking.Height) - 9);
    ++probe.readbacks;
    if (!background || *background != EntityPickTable::InvalidPixel)
        return fail("Background readback must be invalid");
    ids[2] = *background;
    probe.rows << probe.segment << ',' << probe.observed << ',' << d.viewport.logicalSize.x << ','
               << d.viewport.logicalSize.y << ',' << picking.Width << ',' << picking.Height << ','
               << Scale() << ',' << ids[0] << ',' << ids[1] << ',' << ids[2] << '\n';
    if (probe.observed == 1)
    {
        const auto real = GetWindow()->GetFramebufferPixelSize();
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_16_VIEW segment={} logical={},{} pixels={},{} actual_os_scale={},{} position={},{},{} quaternion={},{},{},{}",
            probe.segment, d.viewport.logicalSize.x, d.viewport.logicalSize.y, picking.Width,
            picking.Height, double(real.Width) / windowSize.Width,
            double(real.Height) / windowSize.Height, d.position.x, d.position.y, d.position.z,
            d.orientation.w, d.orientation.x, d.orientation.y, d.orientation.z);
    }
    if (probe.observed == 8)
    {
        if (!Capture(*m_RenderTarget, probe.segment))
            return fail("Representative framebuffer capture failed");
        probe.rows.flush();
        if (!probe.rows)
            return fail("Frame evidence write failed");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_16_SEGMENT_PASS segment={} frames=8",
                                   probe.segment);
        if (probe.segment == 5)
        {
            if (!nativeFixture.Release())
                return fail("Native fixture cleanup before minimize failed");
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_16_FOREGROUND_RELEASE before=minimize success=true");
            std::ofstream request("native-minimize.request");
            request
                << "Native restore required after observed minimized state; no rendering while suspended.\n";
            request.close();
            if (!request)
                return fail("Native minimize request record failed");
            probe.waitingRestore = true;
            SDL_MinimizeWindow(SDL_GetWindowFromID(GetWindow()->GetWindowID()));
        }
        ++probe.segment;
    }
    return {};
}
#endif

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
#ifdef GENGINE_LIFECYCLE_VALIDATION
    if (!ValidateLifecycleUpdate()) return false;
#endif
#ifdef GENGINE_CAMERA_VALIDATION
    if (!ValidateCameraUpdate())
        return false;
#endif
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
#ifdef GENGINE_LIFECYCLE_VALIDATION
    if (submitted)
        if (auto checked = ValidateLifecycleFrame(camera); !checked) return checked;
#endif
#ifdef GENGINE_CAMERA_VALIDATION
    if (submitted)
        if (auto checked = ValidateCameraFrame(camera); !checked)
            return checked;
#endif
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
#ifdef GENGINE_LIFECYCLE_VALIDATION
    if (scale && HasVisibleViewport())
    {
        const float controlled = PreEditorLifecycle::Scale();
        if (auto sized = SetEditorViewport(GetEditorViewportLogicalSize(), {controlled, controlled}); !sized)
            ReportPlatformError(sized.error());
    }
#endif
#ifdef GENGINE_CAMERA_VALIDATION
    if (scale && HasVisibleViewport())
    {
        const float controlled = static_cast<float>(PreEditorCamera::Scale());
        if (auto sized =
                SetEditorViewport(GetEditorViewportLogicalSize(), {controlled, controlled});
            !sized)
            ReportPlatformError(sized.error());
    }
#endif
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
