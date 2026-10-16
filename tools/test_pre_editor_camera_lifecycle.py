"""Phase 17: isolated, create-only RBS camera lifecycle evidence and matched input cost."""
import argparse
import csv
from collections import Counter
from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import test_pre_editor_rbs_modularity as closure
import test_pre_editor_camera_snapshot as camera_fixture

ROOT = closure.ROOT
BASE = ROOT / "logs/pre_editor/phase-17"
ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_17_OD05_SCOPE_ACCEPTANCE.json"
LEGACY_LIMIT_AMENDMENT = ROOT / "docs/pre_editor/active/PHASE_17_FINAL_OWNER_DIRECTIVE_ACCEPTANCE.json"
LIMIT_AMENDMENT = ROOT / "docs/pre_editor/active/PHASE_17_REVISION_01_AUTHORIZATION.json"
REVISION_ENTRY = BASE / "revision-01/entry.json"
NATIVE_POLL_AUTHORITY = ROOT / "docs/pre_editor/active/PHASE_17_NATIVE_POLL_SCOPE_ACCEPTANCE.json"
FOCUS_AUTHORITY = ROOT / "docs/pre_editor/active/PHASE_17_REVISION_02_ACCEPTANCE.json"
CASE55_AUTHORITY = ROOT / "docs/pre_editor/active/PHASE_17_CASE55_FIXTURE_CORRECTION_ACCEPTANCE.json"
MECHANICAL_AUTHORITY = ROOT / "docs/pre_editor/active/PHASE_17_BOUNDED_CORRECTION_AUTHORITY.json"
INTEGRATED_AUTHORITY = ROOT / "docs/pre_editor/active/PHASE_17_FINAL_INTEGRATED_AUTHORITY.json"
POST_HUMAN_AUTHORITY = ROOT / "docs/pre_editor/active/PHASE_17_POST_HUMAN_QUALIFICATION_AUTHORITY.json"
FINAL_CLOSURE_AUTHORITY = ROOT / "docs/pre_editor/active/PHASE_17_FINAL_CLOSURE_AUTHORITY.json"
PRELAUNCH_AUTHORITY = ROOT / "docs/pre_editor/active/PHASE_17_PRELAUNCH_OBSERVER_AUTHORITY.json"
RISK_AUTHORITY = ROOT / "docs/pre_editor/active/PHASE_17_RISK_BASED_CLOSURE_AUTHORITY.json"
PRELAUNCH_CORRECTION = BASE / "corrections/19/admission.json"
CASE59_KIND = "debug-case59"
CASE59_ADMISSION = BASE / "revision-06/final-closure-01/case59-diagnostic-admission.json"
PRELAUNCH_OBSERVER = BASE / "revision-06/final-closure-01/focused-check-01/start.json"
CASE59_CORRECTION = BASE / "corrections/18/analysis-and-admission.json"
CORRECTED_PROVIDERS = ("GEngine/include/GEngine/Camera/EditorCamera.h",
    "GEngine/src/Camera/EditorCamera.cpp", "GEngine/include/GEngine/Managers/InputManager.h",
    "GEngine/src/Managers/InputManager.cpp", "GEngine/src/Managers/EventManager.cpp")
ISOLATION_AMENDMENT = ROOT / "docs/pre_editor/active/PHASE_17_EXTERNAL_DESKTOP_ISOLATION_AMENDMENT_04_ACCEPTANCE.json"
PRIOR = ROOT / "logs/pre_editor/phase-16/candidate-20/scenario-manifest.json"
local, digest, identity, save, utc = closure.local, closure.digest, closure.identity, closure.save, closure.utc
PROJECTS, MSBUILD, environment = closure.PROJECTS, closure.MSBUILD, closure.environment
CONTROL_BASELINE = BASE / "control-03"
CONTROL_ADMISSION = BASE / "revision-01/control-correction-02/admission.json"
CONTROL_COMPARABILITY = BASE / "revision-01/control-comparability-audit.json"
FIRST_PICK_KIND = "release-first-pick"
FIRST_PICK_AUTHORITY = ROOT / "docs/pre_editor/active/PHASE_17_CANDIDATE_14_RELEASE_DIAGNOSTIC_ACCEPTANCE.json"
FIRST_PICK_ADMISSION = BASE / "revision-01/candidate-14-observer/admission.json"
FIRST_PICK_RUNNER_ADMISSION = BASE / "revision-01/candidate-14-observer/runner-admission-addendum.json"
FIRST_PICK_TEARDOWN_ADMISSION = BASE / "revision-01/candidate-14-observer/teardown-admission-addendum.json"
MEASURED_SHARED_INPUTS = (
    "RigidBodySimulation/tests/InputRoutingChecks.h",
    "GEngine/src/Managers/InputManager.cpp", "GEngine/src/Managers/EventManager.cpp",
    "GEngine/include/GEngine/Managers/InputManager.h", "GEngine/include/GEngine/Managers/EventManager.h",
    "GEngine/include/GEngine/Events/Signal.h", "GEngine/src/Core/BaseApp.cpp",
    "GEngine/include/GEngine/Core/BaseApp.h", "GEngine/src/Core/GEngine.cpp",
    "GEngine/include/GEngine/Core/GEngine.h", "GEngine/src/Core/Window.cpp",
    "GEngine/include/GEngine/Core/Window.h", "GEngine/src/Windows/SDLWindow.cpp",
    "GEngine/include/GEngine/Windows/SDLWindow.h", "GEngine/src/Windows/ImGuiWindow.cpp",
    "GEngine/include/GEngine/Windows/ImGuiWindow.h", "GEngine/src/Camera/EditorCamera.cpp",
    "GEngine/include/GEngine/Camera/EditorCamera.h", "RigidBodySimulation/include/RbsState.h",
    "RigidBodySimulation/RigidBodySimulation.vcxproj", "GEngine/GEngine.vcxproj",
    "external/glad/glad.vcxproj", "premake5.lua")


def git(*args):
    return subprocess.check_output(["git", "--no-optional-locks", *args], cwd=ROOT)


def runtime_amendments():
    paths = [LEGACY_LIMIT_AMENDMENT,
        ROOT / "docs/pre_editor/active/PHASE_17_FINAL_SEGMENT_8_QUALIFICATION_AMENDMENT_ACCEPTANCE.json",
        ISOLATION_AMENDMENT,
        ROOT / "docs/pre_editor/active/PHASE_17_RUNTIME_DIAGNOSTIC_AMENDMENT_03_ACCEPTANCE.json",
        ROOT / "docs/pre_editor/active/PHASE_17_RUNTIME_DIAGNOSTIC_AMENDMENT_02_ACCEPTANCE.json",
        ROOT / "docs/pre_editor/active/PHASE_17_RUNTIME_LIMIT_AMENDMENT_01_ACCEPTANCE.json"]
    chain = []
    for index, path in enumerate(paths):
        decision = json.loads(path.read_text())
        assert decision["phase"] == 17 and decision["status"] == "ACCEPTED"
        proposal = decision["proposal"]
        assert identity(ROOT / proposal["path"]) == {k: proposal[k] for k in ("sha256", "size")}
        if index + 1 < len(paths):
            previous = decision["previous_amendment"]
            assert ROOT / previous["path"] == paths[index + 1]
            assert identity(paths[index + 1]) == {k: previous[k] for k in ("sha256", "size")}
        else:
            assert "previous_amendment" not in decision
        chain.append((path, decision))
    current = chain[0][1]
    assert current["total_native_launches"] == 8 and current["diagnostic_only_Debug_launches"] == 2
    assert current["diagnostic_success_is_acceptance"] is False
    assert current["additional_check_files"] == [] and current["qualifying_candidate"] == "11"
    assert current["empty_inventory_authorized"] is True and current["focused_non_RBS_checks_authorized"] is True
    assert current["qualifying_processes"] == ["Debug", "Release only after Debug PASS"]
    assert current["historical_root_cause_exception"] is True
    assert current["additional_diagnostic_launches"] == 0
    assert current["retained_limits"] == {
        "process_seconds": 180, "process_aggregate_seconds": 540, "close_seconds": 30,
        "build_group_seconds": 1800, "build_aggregate_seconds": 5400}
    return chain



def native_poll_authorization():
    assert digest(NATIVE_POLL_AUTHORITY) == "b78e0417f878a81dbb1326fb96f27465b1d225d5b99901f1538fbe3878dac78f"
    authority = json.loads(NATIVE_POLL_AUTHORITY.read_text())
    focus = json.loads(FOCUS_AUTHORITY.read_text())
    assert authority["status"] == focus["status"] == "ACCEPTED"
    assert authority["previous_authority"] == {"path": FOCUS_AUTHORITY.relative_to(ROOT).as_posix(), **identity(FOCUS_AUTHORITY)}
    assert focus["previous_authority"] == {"path": LIMIT_AMENDMENT.relative_to(ROOT).as_posix(), **identity(LIMIT_AMENDMENT)}
    for decision in (authority, focus):
        pin = decision["accepted_proposal"]
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}
    assert len(authority["production_files"]) == 13 and len(authority["check_files"]) == 4
    assert authority["remaining_qualifying_launches"] == 2 and authority["remaining_diagnostics"] == 0
    assert authority["new_execution_budget"] == focus["new_execution_budget"]
    return authority


def case55_authorization():
    assert digest(CASE55_AUTHORITY) == "9ec8e1a6ba49b57dc7cabbaff5795f9fcdde0d6d37941b54616f8b6044a2cbcd"
    current = json.loads(CASE55_AUTHORITY.read_text())
    prior = native_poll_authorization()
    assert current["status"] == "ACCEPTED" and current["phase"] == 17
    assert current["previous_authority"] == {"path": NATIVE_POLL_AUTHORITY.relative_to(ROOT).as_posix(), **identity(NATIVE_POLL_AUTHORITY)}
    assert current["production_files"] == prior["production_files"] and current["check_files"] == prior["check_files"]
    for key in prior["new_execution_budget"].keys() - {"native_additional_launches", "native_cumulative_launch_cap"}:
        assert current["new_execution_budget"][key] == prior["new_execution_budget"][key], key
    assert current["new_execution_budget"]["native_additional_launches"] == 8
    assert current["new_execution_budget"]["native_cumulative_launch_cap"] == 14
    assert current["diagnostics_added"] == 0 and current["qualifying_launches"] == 2
    for path, value in current["frozen_production"].items():
        # The latest owner explicitly reopens the in-scope focus visual behavior.
        # Keep the old frozen source as provenance, not a false current-byte claim.
        if path == "RigidBodySimulation/src/RbsUi.cpp" and INTEGRATED_AUTHORITY.exists():
            assert digest(INTEGRATED_AUTHORITY) == "ed58fb90c33fc35cbf2949a1bf528a1495323b57703f933807febf393ad0ecf7"
            assert identity(BASE / "candidate-19/source-snapshot" / path) == value
        else:
            assert identity(ROOT / path) == value, "Candidate16 production drift: " + path
    return current


def mechanical_authorization():
    assert digest(MECHANICAL_AUTHORITY) == "2948935a7cfa7ace98811738bbd029e0bfe6e2e8639d657e4eb1be2ea8efce3d"
    current = json.loads(MECHANICAL_AUTHORITY.read_text())
    prior = case55_authorization()
    assert current["phase"] == 17 and current["status"] == "ACCEPTED"
    assert current["production_files"] == prior["production_files"] and current["check_files"] == prior["check_files"]
    assert current["frozen_production"] == prior["frozen_production"]
    assert current["new_execution_budget"] == {**prior["new_execution_budget"], "non_RBS_focused_invocations": 16}
    assert current["remaining_native_launches"] == 2 and current["diagnostics_added"] == 0
    for key in ("previous_authority", "previous_stop", "entry_accounting", "prior_execution_budget_authority"):
        pin = current[key]
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}, key
    return current


def prequalification(pin):
    path = local(pin["path"])
    assert identity(path) == {k: pin[k] for k in ("sha256", "size")}
    report = json.loads(path.read_text())
    assert report["result"] == "PASS" and report["kind"] == "CASE55_TRANSLATION_UNIT_PRECOMPILE"
    assert report["authority"] in (identity(MECHANICAL_AUTHORITY), identity(INTEGRATED_AUTHORITY), identity(POST_HUMAN_AUTHORITY), identity(FINAL_CLOSURE_AUTHORITY), identity(PRELAUNCH_AUTHORITY), identity(RISK_AUTHORITY))
    assert report["actual_compile_item_count"] == 1 and report["exits"] == [0, 0]
    for name, expected in report["source_identities"].items():
        if name == "tools/test_pre_editor_camera_lifecycle.py" and identity(ROOT/name) != expected:
            # Python runner is not a compiled TU input. Bind its single mechanical
            # Release prerequisite correction instead of inventing a new compile.
            reuse = json.loads(local("logs/pre_editor/phase-17/revision-09/risk-closure-01/runner-compile-reuse.json").read_text())
            assert reuse["authority"] == identity(RISK_AUTHORITY) and reuse["result"] == "PASS"
            assert reuse["compile_report"] == {"path":path.relative_to(ROOT).as_posix(), **identity(path)}
            assert expected == reuse["before_runner"] and identity(ROOT/name) == reuse["after_runner"]
            assert identity(local(report["fixture"]["path"])) == {k:report["fixture"][k] for k in ("sha256","size")}
        else:
            assert identity(ROOT / name) == expected, name
    assert identity(local(report["compiled_object"]["path"])) == {k: report["compiled_object"][k] for k in ("sha256", "size")}
    return report


def integrated_authorization():
    assert digest(INTEGRATED_AUTHORITY) == "ed58fb90c33fc35cbf2949a1bf528a1495323b57703f933807febf393ad0ecf7"
    current = json.loads(INTEGRATED_AUTHORITY.read_text())
    prior = mechanical_authorization()
    assert current["phase"] == 17 and current["status"] == "ACCEPTED"
    assert current["production_files"] == prior["production_files"] and current["check_files"] == prior["check_files"]
    assert current["production_delta"] == ["RigidBodySimulation/src/RbsUi.cpp"]
    assert current["new_execution_budget"] == {**prior["new_execution_budget"],
        "native_cumulative_launch_cap": 15, "native_additional_launches": 9, "non_RBS_focused_invocations": 20}
    for key in ("previous_authority", "accepted_decision", "entry_accounting", "previous_handoff"):
        pin = current[key]
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}
    assert current["remaining_native_launches"] == 2 and current["diagnostics_added"] == 0
    return current


def post_human_authorization():
    assert digest(POST_HUMAN_AUTHORITY) == "e3eec1b663d2207ca869502511d51c3695ce5226e3f96480aa02ddc9ec4fa395"
    current = json.loads(POST_HUMAN_AUTHORITY.read_text())
    prior = integrated_authorization()
    assert current["phase"] == 17 and current["status"] == "ACCEPTED" and current["candidate"] == "22"
    assert current["production_files"] == prior["production_files"] and current["check_files"] == prior["check_files"]
    assert current["new_execution_budget"] == {**prior["new_execution_budget"],
        "native_cumulative_launch_cap": 16, "native_additional_launches": 10, "non_RBS_focused_invocations": 24}
    assert current["remaining_native_launches"] == 2 and current["diagnostics_added"] == 0
    for key in ("previous_authority", "entry_accounting", "previous_handoff", "human_acceptance"):
        pin = current[key]
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}, key
    for path, expected in current["frozen_production"].items():
        assert identity(ROOT / path) == expected, "Post-human production drift: " + path
    return current


def final_closure_authorization():
    assert digest(FINAL_CLOSURE_AUTHORITY) == "27744ce59287e4268d647f25530548166526e71aa5f4815b8ec49bf0a85696b7"
    current = json.loads(FINAL_CLOSURE_AUTHORITY.read_text())
    prior = post_human_authorization()
    assert current["phase"] == 17 and current["status"] == "ACCEPTED"
    for key in ("production_files", "check_files", "frozen_production"):
        assert current[key] == prior[key]
    assert current["new_execution_budget"] == {**prior["new_execution_budget"],
        "native_cumulative_launch_cap": 18, "native_additional_launches": 12,
        "diagnostic_cumulative_launch_cap": 5, "diagnostic_additional_launches": 3,
        "non_RBS_focused_invocations": 30}
    for key in ("previous_authority", "accepted_proposal", "previous_handoff", "entry_accounting", "human_acceptance"):
        pin = current[key]
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}
    assert current["remaining_native_launches"] == 3 and current["diagnostics_added"] == 1
    return current


def prelaunch_authorization():
    assert digest(PRELAUNCH_AUTHORITY) == "c93b4972b71d3129896ee741b5cc5d414fec490b6ad35cd07105dee24ff5c6e8"
    current = json.loads(PRELAUNCH_AUTHORITY.read_text())
    prior = final_closure_authorization()
    assert current["phase"] == 17 and current["status"] == "ACCEPTED"
    for key in ("production_files", "check_files", "frozen_production", "human_acceptance"):
        assert current[key] == prior[key]
    assert current["new_execution_budget"] == {**prior["new_execution_budget"], "non_RBS_focused_invocations": 32}
    for key in ("previous_authority", "accepted_proposal", "previous_handoff", "entry_accounting"):
        pin = current[key]
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}
    assert current["remaining_native_launches"] == 2 and current["diagnostics_added"] == 0
    assert digest(PRELAUNCH_CORRECTION) == "4b69cd681cf008e8912bdc02b5df8b8c9d31fc46f15696545c2ac1d7768ca959"
    correction = json.loads(PRELAUNCH_CORRECTION.read_text())
    assert correction["authority"] == {"path": PRELAUNCH_AUTHORITY.relative_to(ROOT).as_posix(), **identity(PRELAUNCH_AUTHORITY)}
    for path, expected in correction["frozen_other_sources"].items():
        if path == "RigidBodySimulation/tests/CameraLifecycleChecks.h":
            assert digest(RISK_AUTHORITY) == "a499667fb25f32ba489913b231c6851b89dd20293140db82f847705631e0a96c"
            risk = json.loads(RISK_AUTHORITY.read_text())
            assert risk["source_at_entry"][path] == expected
            assert identity(BASE / "candidate-26/source-snapshot" / path) == expected
        else:
            assert identity(ROOT / path) == expected, "Candidate25 preserved source drift: " + path
    return current


def case59_mode(data, cfg=None):
    if data.get("diagnostic_kind") != CASE59_KIND:
        return False
    assert data["candidate"] == "24" and not data["control"]
    assert data["diagnostic_only"] and not data["acceptance_eligible"]
    assert data["case59_observer"] == {"path": CASE59_ADMISSION.relative_to(ROOT).as_posix(), **identity(CASE59_ADMISSION)}
    if cfg is not None:
        assert cfg == "Debug", "Case59 diagnostic is Debug only"
    return True


def case59_fixture_source(source):
    """Passive calls around the original operations; retain short-circuit evaluation."""
    substitutions = {
        "    using Movement = GestureProbe::Movement;": "    using Movement = GestureProbe::Movement;\n    Case59Trace::Update(*this);",
        "        p.rows.flush();": "        Case59Trace::Finish();\n        p.rows.flush();",
        "return motion(pt) && button(SDL_BUTTON_LEFT,true,pt) && button(SDL_BUTTON_LEFT,false,pt);":
            'return Case59Trace::Operand("click_motion",[&]{return motion(pt);})&&'
            'Case59Trace::Operand("click_down",[&]{return button(SDL_BUTTON_LEFT,true,pt);})&&'
            'Case59Trace::Operand("click_up",[&]{return button(SDL_BUTTON_LEFT,false,pt);});',
        "ok=PreEditorInput::uiProbe.textActive&&click(center);":
            'ok=Case59Trace::Operand("case59_text",[&]{return PreEditorInput::uiProbe.textActive;})&&'
            'Case59Trace::Operand("case59_click",[&]{return click(center);});',
    }
    begin = source.index("bool RigidBodySimulationApp::ValidateViewportGestures()")
    end = source.index("bool RigidBodySimulationApp::ValidateLifecycleUpdate()", begin)
    body = source[begin:end]
    for before, after in substitutions.items():
        assert body.count(before) == 1, before
        body = body.replace(before, after)
    return source[:begin] + body + source[end:]


def final_qualification_fixture_source(source):
    """Establish active text after legitimate Alt navigation; retain the test itself."""
    begin = source.index("bool RigidBodySimulationApp::ValidateViewportGestures()")
    end = source.index("bool RigidBodySimulationApp::ValidateLifecycleUpdate()", begin)
    body = source[begin:end]
    tick = "    if (p.tick && ++p.tick < 4) return true;"
    original = "ok=PreEditorInput::uiProbe.textActive&&click(center);"
    hook = '''    if (p.step==59 && case59Premise.preparing)
    {
        const int ready=ReadyCase59(*this);
        if (ready<0) return fail("Case59 text prerequisite did not publish within its bound");
        if (!ready) return true; // Ordinary frames consume the single setup click.
        const auto descriptor=input.Routing();
        const glm::vec2 target{(descriptor.left+descriptor.right)*.5f,(descriptor.top+descriptor.bottom)*.5f};
        if (!ClickCase59(*this,target)) return fail("Case59 original text/click predicate failed");
        p.tick=1; // Preserve the original complete observation interval after injection.
        return true;
    }
'''
    assert body.count(tick) == body.count(original) == 1
    body = body.replace(tick, hook+tick).replace(original, "ok=BeginCase59(*this,widgetPoint);")
    source = source[:begin]+body+source[end:]
    anchor = "        if (!gestureProbe.complete) return ValidateViewportGestures();"
    lifetime = '''
        const int lifetime=ObserveFocusLifetime(*this,m_ViewportActive,m_ViewportActivations,
                                               m_ViewportHighlightStart,m_GesturePickReadbacks);
        if (lifetime<0) return fail("Continuous five-second focus/DrawList lifetime");
        if (!lifetime) return true; // Render normally; no event, wait or pacing change.
'''
    assert source.count(anchor) == 1
    return source.replace(anchor, anchor+lifetime)


def case55_fixture_source(source):
    """Bounded test-only timing and visible-target setup; production bytes stay frozen."""
    path = "RigidBodySimulation/src/RbsValidation.cpp"
    assert hashlib.sha256(source.encode()).hexdigest() == hashlib.sha256((ROOT / path).read_text().encode()).hexdigest()
    assert digest(ROOT / path) == "7c044e7017e0bbc24772e820725f7db957df423e07ddf57751b81e662c6c6cdd"
    begin = source.index("bool RigidBodySimulationApp::ValidateViewportGestures()")
    end = source.index("bool RigidBodySimulationApp::ValidateLifecycleUpdate()", begin)
    body = source[begin:end]
    tick = "    if (p.tick && ++p.tick < 4) return true;"
    injection = "        p.name=\"plain_GPU_pick_after_alt_gestures\"; p.expectedReads=1; ok=alt(false)&&targetClick(); break;"
    assert body.count(tick) == body.count(injection) == 1
    hook = """    if (p.step==55 && p.tick)
    {
        const auto logical=GetEditorViewportLogicalSize();
        CameraViewport actual;
        actual.logicalOrigin=m_ViewportBounds[0];
        actual.logicalSize={logical.Width,logical.Height};
        actual.pixelSize={m_RenderTarget->GetWidth(),m_RenderTarget->GetHeight()};
        if (!m_CameraSnapshot || !Case55Tick(*this,p.tick,*m_CameraSnapshot,m_EditorCamera_,actual))
            return fail("Case55 stable snapshot/viewport premise or injection failed");
    }
"""
    deferred = "        p.name=\"plain_GPU_pick_after_alt_gestures\"; p.expectedReads=1; break; // Inject next existing tick."
    baseline = "    p.position=m_EditorCamera_.GetPosition(); p.orientation=m_EditorCamera_.GetOrientation();"
    assert body.count(baseline) == 1
    # Case54 has already passed. Set up the next case before capturing its baseline;
    # the existing ordinary frame publishes this pose before the next-tick click.
    visible_pose = "    if (p.step==55) FocusMaterialCamera(m_EditorCamera_, {0,7,6}, .12f, 0, 28.f);\n"
    corrected = body.replace(tick, hook + tick).replace(injection, deferred).replace(baseline, visible_pose + baseline)
    assert corrected.replace(hook + tick, tick).replace(deferred, injection).replace(visible_pose + baseline, baseline) == body
    return source[:begin] + corrected + source[end:]


def risk_closure_authorization():
    assert digest(RISK_AUTHORITY) == "a499667fb25f32ba489913b231c6851b89dd20293140db82f847705631e0a96c"
    current = json.loads(RISK_AUTHORITY.read_text())
    prior = prelaunch_authorization()
    assert current["phase"] == 17 and current["status"] == "ACCEPTED" and current["candidate"] == "27"
    for key in ("production_files", "check_files", "frozen_production", "human_acceptance"):
        assert current[key] == prior[key], key
    assert current["new_execution_budget"] == {**prior["new_execution_budget"],
        "native_cumulative_launch_cap": 20, "native_additional_launches": 14, "non_RBS_focused_invocations": 40}
    for key in ("previous_authority", "previous_handoff", "entry_accounting", "human_acceptance"):
        pin = current[key]
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}
    for path, expected in current["source_at_entry"].items():
        if path not in current["changed_check_files"]:
            assert identity(ROOT / path) == expected, path
    for path, expected in current["documentation_inputs"].items():
        assert identity(local(path)) == expected, path
    return current


def require_debug_qualification(out, data):
    if "debug_reuse" not in data:
        assert json.loads((out / "run-Debug.json").read_text())["result"] == "PASS"
        assert json.loads((out / "audit-Debug.json").read_text())["result"] == "PASS"
        return
    assert data["candidate"] in {"27", "28"} and not data["diagnostic_only"]
    current = risk_closure_authorization()
    pin = data["debug_reuse"]
    assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}
    proof = json.loads(local(pin["path"]).read_text())
    assert proof["result"] == "PASS" and proof["kind"] == "CANDIDATE26_DEBUG_IMPACT_EQUIVALENCE"
    assert proof["authority"] == identity(RISK_AUTHORITY)
    assert proof["source_identities"] == {p: identity(ROOT/p) for p in current["production_files"]+current["check_files"]}
    for pin in proof["evidence"].values():
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}
    for key in ("build", "runtime", "audit", "storage", "compile"):
        assert json.loads(local(proof["evidence"][key]["path"]).read_text())["result"] == "PASS", key
    assert proof["assertions_stimulus_and_measurement_unchanged"] is True
    assert proof["all_nonobserver_behavior_inputs_match"] is True
    assert proof["original_debug_frames"] == 1065 and proof["native_retest_claimed"] is False


def revision_authorization():
    entry = json.loads(REVISION_ENTRY.read_text())
    authority = json.loads(LIMIT_AMENDMENT.read_text())
    assert identity(LIMIT_AMENDMENT) == {k: entry["authority"][k] for k in ("sha256", "size")}
    assert authority["phase"] == 17 and authority["revision"] == "01" and authority["status"] == "AUTHORIZED"
    previous = authority["previous_authority"]
    assert ROOT / previous["path"] == LEGACY_LIMIT_AMENDMENT
    assert identity(LEGACY_LIMIT_AMENDMENT) == {k: previous[k] for k in ("sha256", "size")}
    # Historical accounting is immutable; every new attempt adds to it.
    for group in ("historical_starts", "historical_runs", "historical_builds"):
        for name, expected in entry[group].items():
            assert identity(local(BASE / name)) == expected, name
    current = risk_closure_authorization()
    # Preserve the revision01 accounting boundary. Only the accepted launch cap and
    # one corrected-candidate performance batch per role and focused cap supersede its budget.
    prior_limits = authority["new_execution_budget"]
    new_limits = current["new_execution_budget"]
    for key in prior_limits.keys() - {"native_additional_launches", "native_cumulative_launch_cap", "performance", "non_RBS_focused_invocations", "diagnostic_additional_launches", "diagnostic_cumulative_launch_cap"}:
        assert new_limits[key] == prior_limits[key], key
    assert new_limits["native_additional_launches"] == 14 and new_limits["native_cumulative_launch_cap"] == 20
    assert new_limits["non_RBS_focused_invocations"] == 40
    assert new_limits["diagnostic_additional_launches"] == 3 and new_limits["diagnostic_cumulative_launch_cap"] == 5
    return {**authority, "new_execution_budget": new_limits}, entry


def revision_usage(pattern, historical):
    return [(p, json.loads(p.read_text())) for p in BASE.glob(pattern)
            if str(p.relative_to(BASE)) not in historical]


def first_pick_authorization():
    """One owner-pinned nonaccepting Release diagnostic, without a budget reset."""
    for path, expected in (
        (FIRST_PICK_AUTHORITY, "c713b10e28705d4efbe762c9f4e6f90b7f809dde49319cc1fa93f31e388f1d30"),
        (FIRST_PICK_ADMISSION, "30b181e9b29c35b60aae60c494f210a0f4e648fae028ff7d2693bf6c5a8a7520"),
        (FIRST_PICK_RUNNER_ADMISSION, "ee8b8196bd65a7d34eb0ffd162b793bcad1634d3f8d99a9e05d65ae278c6a91c"),
        (FIRST_PICK_TEARDOWN_ADMISSION, "4d532e6b98b35608e1b729b3e0c2e47a712f1a0252e65c8490e3bc913c472fe0")):
        assert digest(local(path)) == expected, "First-pick diagnostic authority drift"
    authority = json.loads(FIRST_PICK_AUTHORITY.read_text())
    admission = json.loads(FIRST_PICK_ADMISSION.read_text())
    runner = json.loads(FIRST_PICK_RUNNER_ADMISSION.read_text())
    teardown = json.loads(FIRST_PICK_TEARDOWN_ADMISSION.read_text())
    assert authority["phase"] == 17 and authority["candidate"] == "14"
    assert authority["status"] == "AUTHORIZED_DIAGNOSTIC_ONLY"
    assert authority["configuration"] == "Release" and authority["native_launches_authorized"] == 1
    assert authority["diagnostic_only"] is True and authority["acceptance_eligible"] is False
    assert authority["trace_guard"] == "GENGINE_LIFECYCLE_PICK_TRACE"
    assert authority["trace_file"] == "lifecycle-first-pick-trace.json"
    assert authority["trace_capacity"] == 2048 and authority["trace_buffer_bytes_max"] == 1048576
    assert authority["prior_authority"] == {
        "path": LIMIT_AMENDMENT.relative_to(ROOT).as_posix(), **identity(LIMIT_AMENDMENT)}
    authority_pin = {"path": FIRST_PICK_AUTHORITY.relative_to(ROOT).as_posix(), **identity(FIRST_PICK_AUTHORITY)}
    assert admission["authority"] == runner["authority"] == authority_pin
    assert teardown["authority"] == authority_pin and teardown["parent"] == {
        "path": FIRST_PICK_ADMISSION.relative_to(ROOT).as_posix(), **identity(FIRST_PICK_ADMISSION)}
    assert teardown["source"]["path"] == "RigidBodySimulation/src/RigidBodySimulation.cpp"
    assert teardown["before"]["sha256"] == teardown["source"]["sha256"] == "908f174aa687082bbe4f3d10fa4a12858dadfc017c6e455c2026a4a819dcd166"
    proposal = authority["accepted_proposal"]
    assert identity(local(proposal["path"])) == {k: proposal[k] for k in ("sha256", "size")}
    assert admission["proposal"] == proposal
    evidence = {pin["path"]: pin for pin in admission["historical_evidence_verified"]}
    for pin in evidence.values():
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}
    for pin in admission["read_only_providers"]:
        if pin["path"] == "RigidBodySimulation/src/RigidBodySimulation.cpp":
            assert pin == teardown["source"]
            assert identity(local(teardown["before"]["path"])) == {k: pin[k] for k in ("sha256", "size")}
        else:
            assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}
    predecessor = {}
    for name in ("scenario-manifest.json", "build-Debug/result.json", "run-Debug.json", "audit-Debug.json"):
        path = BASE / "candidate-13" / name
        pin = evidence[path.relative_to(ROOT).as_posix()]
        predecessor[name] = pin
        value = json.loads(path.read_text())
        if name != "scenario-manifest.json":
            assert value["result"] == "PASS", "Candidate13 Debug predecessor must remain PASS"
        if name == "run-Debug.json":
            assert not value["diagnostic_only"] and value["acceptance_eligible"]
        if name == "build-Debug/result.json":
            assert value["fresh_full_closure"] and value["exits"] == [0, 0, 0, 0]
    before = admission["before"]["tools/test_pre_editor_camera_lifecycle.py"]
    assert runner["before_runner"] == before
    assert before["sha256"] == "a7af4c2add2d2bafde5b9034a73bd1783c37e00e31fc8ff767dd74807017a20d"
    assert identity(local(before["path"])) == {k: before[k] for k in ("sha256", "size")}
    control = runner["control_manifest"]
    assert local(control["path"]) == local(BASE / "control-05/scenario-manifest.json")
    assert identity(local(control["path"])) == {k: control[k] for k in ("sha256", "size")}
    assert json.loads(local(control["path"]).read_text())["input_hashes"]["tools/test_pre_editor_camera_lifecycle.py"] == before["sha256"]
    return {"kind": FIRST_PICK_KIND, "authority": authority_pin, "proposal": proposal,
        "observer_admission": {"path": FIRST_PICK_ADMISSION.relative_to(ROOT).as_posix(), **identity(FIRST_PICK_ADMISSION)},
        "runner_admission": {"path": FIRST_PICK_RUNNER_ADMISSION.relative_to(ROOT).as_posix(), **identity(FIRST_PICK_RUNNER_ADMISSION)},
        "teardown_admission": {"path": FIRST_PICK_TEARDOWN_ADMISSION.relative_to(ROOT).as_posix(), **identity(FIRST_PICK_TEARDOWN_ADMISSION)},
        "canonical_before": teardown["before"],
        "predecessor_Debug": predecessor, "control_manifest": control, "historical_control_runner": before,
        "original_cost_evidence": {role: evidence[f"logs/pre_editor/phase-17/candidate-13/runtime-Release/input-cost-{role}.csv"]
                                   for role in ("editor", "scene")},
        "trace_file": authority["trace_file"], "trace_capacity": authority["trace_capacity"],
        "trace_buffer_bytes_max": authority["trace_buffer_bytes_max"], "acceptance_credit": False}


def first_pick_mode(data, cfg=None):
    if data.get("diagnostic_kind") != FIRST_PICK_KIND:
        assert not data.get("first_pick_diagnostic"), "Unrecognized first-pick diagnostic mode"
        return False
    assert data["candidate"] == "14" and not data["control"]
    assert data["diagnostic_only"] is True and data["acceptance_eligible"] is False
    assert data["first_pick_diagnostic"] == first_pick_authorization()
    assert not data.get("reuse_control_build") and not data.get("reuse_candidate_build")
    if cfg is not None:
        assert cfg == "Release", "Owner authorized only the Candidate14 Release first-pick diagnostic"
    return True


def additional_check_files(chain):
    files = list(dict.fromkeys(name for _, decision in reversed(chain)
                              for name in decision.get("additional_check_files", [])))
    assert files == ["RigidBodySimulation/tests/CameraSnapshotChecks.h",
                     "tools/test_pre_editor_camera_snapshot.py"]
    return files


def normalized_reuse_props(text):
    # Only these diagnostic-RBS observer definitions may differ; all library/ordinary
    # options and every other overlay byte still have to match.
    ns = "{http://schemas.microsoft.com/developer/msbuild/2003}"
    condition = "'$(MSBuildProjectName)'=='RigidBodySimulation' and '$(LifecycleLane)'=='diagnostic'"
    for definition in ("GENGINE_LIFECYCLE_FOCUS_TRACE;", "GENGINE_LIFECYCLE_ORACLE_EVIDENCE;", "GENGINE_LIFECYCLE_CASE59_TRACE;", "GENGINE_LIFECYCLE_CASE59_FIXTURE;"):
        if definition not in text:
            continue
        sites = [node for group in ET.fromstring(text).findall(ns + "ItemDefinitionGroup")
                 if group.get("Condition") == condition
                 for node in group.findall(ns + "ClCompile/" + ns + "PreprocessorDefinitions")
                 if definition in (node.text or "")]
        assert text.count(definition) == 1 and len(sites) == 1, "Observer definition escaped diagnostic RBS lane"
        text = text.replace(definition, "", 1)
    return text


def guards():
    entry = json.loads((BASE / "entry-01/entry.json").read_text())
    accepted = json.loads(ACCEPTANCE.read_text())
    assert accepted["status"] == "ACCEPTED_WITH_CONTROLLING_CLARIFICATIONS"
    assert digest(ROOT / accepted["proposal"]["path"]) == accepted["proposal"]["sha256"]
    assert git("rev-parse", "HEAD").decode().strip() == entry["baseline"]
    assert git("branch", "--show-current").decode().strip() == "pre-editor/refactor"
    index = Path(git("rev-parse", "--git-path", "index").decode().strip())
    assert digest(index) == entry["index"]["raw_sha256"]
    for command, key in ((["ls-files", "--stage", "-z"], "stage_sha256"),
                         (["ls-files", "-v", "-z"], "flags_sha256")):
        assert hashlib.sha256(git(*command)).hexdigest() == entry["index"][key]
    current = native_poll_authorization()
    original_owned = accepted["production_files"] + accepted["check_files"] + additional_check_files(runtime_amendments())
    owned = current["production_files"] + current["check_files"]
    assert set(owned) - set(original_owned) == set(CORRECTED_PROVIDERS)
    changed = set(git("diff", "--name-only", "HEAD").decode().splitlines())
    untracked = set(git("ls-files", "--others", "--exclude-standard").decode().splitlines())
    derived = set()
    for manifest_path in BASE.glob("*/scenario-manifest.json"):
        manifest = json.loads(manifest_path.read_text())
        product = manifest["product_name"]
        assert re.fullmatch(r"Phase17(Control|Candidate)[0-9]{2}", product)
        for cfg in (["Release"] if manifest["control"] else ["Debug", "Release"]):
            prefix = f"bin/{cfg}/{product}"
            derived.update(f"{prefix}/{kind}/{kind}.{extension}" for kind, extension in
                (("GEngine", "lib"), ("glad", "lib"), ("RigidBodySimulation", "exe")))
            derived.update(f"{prefix}/assets/{name}" for name in manifest["asset_members"])
            package = json.loads((ROOT / "tools/runtime_assets.json").read_text())
            derived.update(f"{prefix}/assets/startup/{name}.txt" for name in package["startup"])
            derived.update(f"{prefix}/RigidBodySimulation/{Path(value['path']).name}" for value in manifest["expected_modules"][cfg].values())
    changed |= untracked - set(entry["preexisting_untracked"]["paths"]) - derived
    documentation = risk_closure_authorization()
    admitted_docs = set(documentation["documentation_inputs"]) | set(documentation["final_documentation_outputs"])
    assert changed <= set(owned) | admitted_docs, sorted(changed - set(owned) - admitted_docs)
    for path in entry["protected_paths"]:
        if path != "docs/pre_editor/active/REFACTOR_STATE.md":
            assert digest(ROOT / path) == entry["input_fingerprints"][path]["sha256"], path
    assert not (ROOT / "pre-editor-checkpoints/phase-17.json").exists()
    assert not Path(git("rev-parse", "--git-path", "pre-editor-approval/PHASE_17.json").decode().strip()).exists()
    return entry, accepted, owned


def write(path, value):
    path = local(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(value)


def measurement_wrapper(path):
    matched = re.search(rb"    inline PlatformResult Measure\(BaseApp& app, const char\* role\).*?\n    }",
                        Path(path).read_bytes(), re.S)
    assert matched, "Missing unchanged lifecycle measurement wrapper"
    raw = matched.group()
    return {"sha256": hashlib.sha256(raw).hexdigest(), "size": len(raw)}


def consumed_external_inputs(prior):
    """Pin fresh inputs, including actual compiler/link dependencies missing in old manifests.

    Retained tlogs are discovery inputs, never evidence of historical content equality.
    Only the selected Phase17 build trees are read. Workspace products/intermediates
    are excluded; repository dependencies and external toolchain inputs are retained.
    """
    selected = [ROOT / "bin-int/Release/Phase17Control01",
                ROOT / "bin-int/Release/Phase17Control02",
                ROOT / "bin-int/Debug/Phase17Candidate10",
                ROOT / "bin-int/Release/Phase17Candidate09"]
    files = {str(Path(p).resolve()).casefold(): Path(p).resolve() for p in prior["external_inputs"]}
    tlogs = {}
    for folder in selected:
        for path in sorted(folder.rglob("*.read.*.tlog")):
            tlogs[path.relative_to(ROOT).as_posix()] = identity(path)
            for row in path.read_text(encoding="utf-16").splitlines():
                if not row or row.startswith("^"):
                    continue
                dependency = Path(row).resolve()
                if not dependency.is_file():
                    continue
                if dependency.is_relative_to(ROOT):
                    relative = dependency.relative_to(ROOT).as_posix().lower()
                    if not relative.startswith("external/") or dependency.suffix.lower() not in (".lib", ".dll"):
                        continue  # Sources have their own closure; outputs are not behavior inputs.
                files[str(dependency).casefold()] = dependency
    compiler = next(Path(p) for p in prior["external_inputs"] if Path(p).name.lower() == "cl.exe")
    bitset = compiler.parents[3] / "include/bitset"
    assert bitset.is_file() and tlogs, "Consumed external dependency discovery unavailable"
    files[str(bitset.resolve()).casefold()] = bitset.resolve()
    return ({str(p): identity(p) for _, p in sorted(files.items())}, tlogs)


def current_machine():
    raw = subprocess.check_output(["powershell", "-NoProfile", "-Command",
        "@{cpu=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors);memory=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;os=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber);gpu=(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion);power=(powercfg /getactivescheme)} | ConvertTo-Json -Depth 4"])
    return json.loads(raw)


def staged_asset_reuse(manifest, candidate):
    assert re.fullmatch(r"[0-9]{2}", candidate)
    previous = local(BASE / ("candidate-" + candidate))
    original = json.loads((previous / "scenario-manifest.json").read_text())
    assert original["asset_members"] == manifest["asset_members"]
    assert all(original["input_hashes"][p] == manifest["input_hashes"][p]
               for p in manifest["asset_members"].values())
    record = previous / "stage-Debug.json"
    staged = json.loads(record.read_text())
    assert staged["result"] == "PASS"
    asset_root = local(f"bin/Debug/{original['product_name']}/assets")
    for name, source in manifest["asset_members"].items():
        assert digest(local(asset_root / name)) == manifest["input_hashes"][source]
    for path, expected in staged["generated_startup_descriptors"].items():
        assert identity(local(path)) == expected
    return {"root": str(asset_root), "record": str(record), **identity(record),
            "reason": "Read-only frozen package; exact asset membership/content and startup provenance"}


def prepare_control(out, name, reuse_control_build, reuse_candidate_build, diagnostic_only,
                    reuse_staged_assets):
    entry, accepted, owned = guards()
    authority, _ = revision_authorization()
    limits = authority["new_execution_budget"]
    assert name == "05" and not diagnostic_only and not reuse_control_build and not reuse_candidate_build
    admission = json.loads(CONTROL_ADMISSION.read_text())
    assert admission["status"] == "ADMITTED"
    assert admission["authority"] == {"path": LIMIT_AMENDMENT.relative_to(ROOT).as_posix(), **identity(LIMIT_AMENDMENT)}
    assert admission["causal_evidence"] == {"path": CONTROL_COMPARABILITY.relative_to(ROOT).as_posix(), **identity(CONTROL_COMPARABILITY)}
    assert admission["new_control"] == name and admission["new_product_name"] == "Phase17Control05"
    predecessor_keys = ("predecessor", "failed_control_manifest", "failed_control_build",
                        "failed_compile_log", "failed_compile_binlog", "frozen_baseline_manifest")
    for key in predecessor_keys:
        pin = admission[key]
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}, key
    previous = json.loads(local(admission["predecessor"]["path"]).read_text())
    assert previous["authority"] == admission["authority"] and previous["causal_evidence"] == admission["causal_evidence"]
    failed_manifest = json.loads(local(admission["failed_control_manifest"]["path"]).read_text())
    assert failed_manifest["control"] and failed_manifest["candidate"] == "04"
    assert failed_manifest["candidate_admission"] == admission["predecessor"]
    assert json.loads(local(admission["failed_control_build"]["path"]).read_text())["result"] == "FAIL"
    assert not (BASE / "control-04/run-Release-start.json").exists(), "Fresh control runtime slot already consumed"
    audit = json.loads(CONTROL_COMPARABILITY.read_text())
    baseline_manifest = CONTROL_BASELINE / "scenario-manifest.json"
    assert identity(baseline_manifest) == audit["historical_evidence"][baseline_manifest.relative_to(ROOT).as_posix()]
    baseline = json.loads(baseline_manifest.read_text())
    assert baseline["control"] and baseline["baseline"] == entry["baseline"]
    assert admission["frozen_baseline_manifest"] == {"path": baseline_manifest.relative_to(ROOT).as_posix(), **identity(baseline_manifest)}
    generated_headers = admission["consumed_generated_headers"]
    assert set(generated_headers) == {
        "runtime/tmp/gpu-capture-tools/RenderDoc_1.46_64/RenderDoc_1.46_64/renderdoc_app.h"}
    for path, pin in generated_headers.items():
        assert baseline["input_hashes"][path] == pin["sha256"]
        assert path not in baseline["include_preceding_absent"]
    for path in MEASURED_SHARED_INPUTS:
        assert digest(ROOT / path) == baseline["input_hashes"][path], path
    old_header = CONTROL_BASELINE / "source-snapshot/RigidBodySimulation/tests/CameraLifecycleChecks.h"
    assert measurement_wrapper(old_header) == measurement_wrapper(ROOT / "RigidBodySimulation/tests/CameraLifecycleChecks.h")
    platform = camera_fixture._phase17_platform()
    isolation_authorization = camera_fixture._phase17_authorization(ISOLATION_AMENDMENT)
    proof = camera_fixture._phase17_isolation_proof(isolation_authorization, platform)
    assert proof["result"] == "PASS" and proof["helper"] == identity(ROOT / "tools/test_pre_editor_camera_snapshot.py")
    assert proof["platform"] == platform and proof["authorization"] == isolation_authorization
    assert proof["final_authorization"] == {"path": str(LIMIT_AMENDMENT), **identity(LIMIT_AMENDMENT)}
    out.mkdir(parents=True, exist_ok=False)
    product_name = "Phase17Control05"
    for parent in ("bin", "bin-int"):
        assert not local(f"{parent}/Release/{product_name}").exists()
    shadow = local(out / "source-root")
    shadow.mkdir()
    external_inputs, tlogs = consumed_external_inputs(baseline)
    shadow_hashes, origins = {}, {}
    source_extensions = {".cpp", ".c", ".h", ".hpp", ".inl"}
    for path, expected in baseline["input_hashes"].items():
        if Path(path).suffix.lower() not in source_extensions or (
                not path.startswith(("GEngine/", "RigidBodySimulation/", "external/")) and path not in generated_headers):
            continue
        source = local(CONTROL_BASELINE / "source-snapshot" / path)
        assert digest(source) == expected, path
        if path in generated_headers:
            assert identity(source) == generated_headers[path], path
        target = local(shadow / path)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        shadow_hashes[path] = digest(target)
        origins[path] = {"path": source.relative_to(ROOT).as_posix(), **identity(source)}
    assert set(generated_headers) <= shadow_hashes.keys(), "Consumed generated header missing from frozen shadow"
    for path in PROJECTS:
        source = ROOT / path
        assert digest(source) == baseline["input_hashes"][path]
        # Project-relative includes/sources now resolve below shadow. The generated
        # project's absolute external include roots are translated equivalently.
        raw = source.read_bytes().replace(ROOT.as_posix().encode(), shadow.as_posix().encode())
        assert ROOT.as_posix().encode() not in raw.replace(shadow.as_posix().encode(), b"SHADOW")
        target = local(shadow / path)
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(raw)
        shadow_hashes[path] = digest(target)
        origins[path] = {"path": path, **identity(source), "transformation": "Only absolute workspace roots translated to frozen shadow root"}
    # The copied projects use project-relative library directories. Link only the
    # pinned prebuilt dependencies, never baseline GEngine/glad or executable output.
    for path, expected in external_inputs.items():
        source = Path(path)
        if source.is_relative_to(ROOT) and source.suffix.lower() in (".lib", ".dll"):
            relative = source.relative_to(ROOT).as_posix()
            assert relative.lower().startswith("external/") and identity(source) == expected
            target = local(shadow / relative)
            target.parent.mkdir(parents=True, exist_ok=True)
            os.link(source, target)
            shadow_hashes[relative] = digest(target)
            origins[relative] = {"path": relative, **expected, "role": "Current common prebuilt dependency; fresh control only"}
    generated = []
    for name_ in ("main-lifecycle-fixture.cpp", "RbsValidation-control.cpp"):
        source = CONTROL_BASELINE / name_
        assert digest(source) == baseline["input_hashes"][source.relative_to(ROOT).as_posix()]
        target = local(out / name_)
        with target.open("xb") as stream:
            stream.write(source.read_bytes())
        generated.append(target)
    main, adapter = generated
    props = out / "isolated.props"
    write(props, '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        '<PropertyGroup>'
        f'<OutDir>{ROOT.as_posix()}/bin/$(Configuration)/{product_name}/$(MSBuildProjectName)/</OutDir>'
        f'<IntDir>{ROOT.as_posix()}/bin-int/$(Configuration)/{product_name}/$(MSBuildProjectName)/$(LifecycleLane)/</IntDir>'
        '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>\n'
        '<ItemDefinitionGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\' and \'$(LifecycleLane)\'==\'diagnostic\'">'
        '<ClCompile><PreprocessorDefinitions>GENGINE_LIFECYCLE_VALIDATION;GENGINE_LIFECYCLE_CONTROL;%(PreprocessorDefinitions)</PreprocessorDefinitions>'
        f'<AdditionalIncludeDirectories>{shadow.as_posix()}/RigidBodySimulation/tests;{shadow.as_posix()}/RigidBodySimulation/src;'
        f'{shadow.as_posix()}/external/sdl2/include;{shadow.as_posix()}/external/glad/include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>'
        '</ClCompile></ItemDefinitionGroup>\n'
        '<ItemGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\' and \'$(LifecycleLane)\'==\'diagnostic\'">'
        f'<ClCompile Remove="src\\main.cpp"/><ClCompile Include="{main.as_posix()}"/>'
        f'<ClCompile Remove="src\\RbsValidation.cpp"/><ClCompile Include="{adapter.as_posix()}"/>'
        '</ItemGroup>\n</Project>\n')
    generated.append(props)
    names = set(MEASURED_SHARED_INPUTS) | set(baseline["asset_members"].values())
    names.update(["tools/test_pre_editor_camera_lifecycle.py", "tools/test_pre_editor_camera_snapshot.py",
                  "tools/test_pre_editor_rbs_modularity.py", "tools/postbuild.py", "tools/runtime_assets.json",
                  "docs/pre_editor/active/OUTPUT_PATHS.json"])
    names.update(p.relative_to(ROOT).as_posix() for p in (LIMIT_AMENDMENT, REVISION_ENTRY,
        CONTROL_ADMISSION, CONTROL_COMPARABILITY, baseline_manifest, ISOLATION_AMENDMENT))
    names.update(admission[key]["path"] for key in predecessor_keys)
    names.update(p.relative_to(ROOT).as_posix() for p in generated)
    names.update((shadow / path).relative_to(ROOT).as_posix() for path in shadow_hashes)
    for path in baseline["asset_members"].values():
        assert digest(ROOT / path) == baseline["input_hashes"][path]
    protocol = json.loads(json.dumps(baseline["protocol"]))
    protocol["limits"].update(build_seconds=limits["build_group_seconds"],
        build_aggregate_seconds=limits["build_cumulative_seconds_cap"], process_seconds=limits["process_seconds"],
        process_aggregate_seconds=limits["native_cumulative_seconds_cap"], total_launches=limits["native_cumulative_launch_cap"],
        close_seconds=limits["close_seconds"], revision=limits)
    protocol["runtime_limit_amendment"] = {"path": LIMIT_AMENDMENT.relative_to(ROOT).as_posix(), **identity(LIMIT_AMENDMENT)}
    manifest = {"phase": 17, "candidate": name, "control": True, "product_name": product_name,
        "diagnostic_only": False, "acceptance_eligible": False, "recorded_utc": utc(),
        "baseline": entry["baseline"], "owned_files": owned,
        "candidate_admission": {"path": CONTROL_ADMISSION.relative_to(ROOT).as_posix(), **identity(CONTROL_ADMISSION)},
        "isolation_proof": {"path": camera_fixture.PHASE17_EMPTY_INVENTORY_PROOF.relative_to(ROOT).as_posix(),
                            **identity(camera_fixture.PHASE17_EMPTY_INVENTORY_PROOF)},
        "isolation_authorization": {"path": ISOLATION_AMENDMENT.relative_to(ROOT).as_posix(), **identity(ISOLATION_AMENDMENT)},
        "input_hashes": {path: digest(ROOT / path) for path in sorted(names)},
        "external_inputs": external_inputs, "dependency_discovery_records": tlogs,
        "measured_shared_inputs": {path: digest(ROOT / path) for path in MEASURED_SHARED_INPUTS},
        "measurement_wrapper": measurement_wrapper(old_header),
        "compile_members": closure.compile_members(), "compile_discovery": closure.discovery(),
        "include_preceding_absent": [], "shadow_include_preceding_absent": baseline["include_preceding_absent"],
        "asset_members": baseline["asset_members"], "expected_modules": baseline["expected_modules"],
        "layout": baseline["layout"], "toolchain": baseline["toolchain"], "machine": current_machine(),
        "protocol": protocol, "source_projects": {path: str(shadow / path) for path in PROJECTS},
        "frozen_control": {"root": str(shadow), "files": shadow_hashes, "origins": origins,
            "baseline_manifest": {"path": baseline_manifest.relative_to(ROOT).as_posix(), **identity(baseline_manifest)},
            "purpose": "Fresh approved Phase16 baseline; no current production source restoration or historical identity backfill"},
        "writers": [str(out), f"bin/Release/{product_name}", f"bin-int/Release/{product_name}",
                    f"runtime/RigidBodySimulation/Release/phase-17-{product_name}", "runtime/tmp"],
        "product_roles": "Fresh control build only; cannot supply candidate acceptance or replace historical observations"}
    assert manifest["compile_members"] == baseline["compile_members"]
    if reuse_staged_assets:
        manifest["reuse_staged_assets"] = staged_asset_reuse(manifest, reuse_staged_assets)
    save(out / "scenario-manifest.json", manifest)
    print(f"Prepared fresh Control05 with {len(shadow_hashes)} frozen source/dependency members", flush=True)


def prepare(out, name, control, reuse_control_build=None, reuse_candidate_build=None, diagnostic_only=False,
            reuse_staged_assets=None, diagnostic_kind=None):
    assert diagnostic_kind in (None, FIRST_PICK_KIND, CASE59_KIND)
    case59 = diagnostic_kind == CASE59_KIND
    if case59:
        assert name == "24" and diagnostic_only and not control
        final_closure_authorization()
    first_pick = diagnostic_kind == FIRST_PICK_KIND
    assert name != "14" or control or first_pick, "Candidate14 is reserved for the owner-pinned first-pick diagnostic"
    assert not diagnostic_only or first_pick or case59 or not FIRST_PICK_AUTHORITY.exists(), "Unused Debug diagnostic slot was reallocated to Candidate14 Release"
    if first_pick:
        assert name == "14" and diagnostic_only and not control
        assert reuse_control_build is None and reuse_candidate_build is None, "Fresh full Release closure is required"
        diagnostic_authority = first_pick_authorization()
    if control:
        return prepare_control(out, name, reuse_control_build, reuse_candidate_build, diagnostic_only,
                               reuse_staged_assets)
    entry, accepted, owned = guards()
    amendment_chain = runtime_amendments()
    amendment, revision_entry = revision_authorization()
    limits = amendment["new_execution_budget"]
    amendment_inputs = {name for path, decision in amendment_chain
                        for name in (path.relative_to(ROOT).as_posix(), decision["proposal"]["path"])}
    amendment_inputs.update([LIMIT_AMENDMENT.relative_to(ROOT).as_posix(), REVISION_ENTRY.relative_to(ROOT).as_posix()])
    check_files = accepted["check_files"] + additional_check_files(amendment_chain)
    assert not control and reuse_control_build is None, "A fresh baseline control needs its separately frozen Phase16 inputs"
    assert name in {"11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "22", "23", "24", "25", "26", "27", "28"}, "Revision candidate destination not admitted"
    assert reuse_candidate_build is None or (reuse_candidate_build in {"11", "12", "13", "14", "15", "16", "20"} and int(reuse_candidate_build) < int(name)), "Fresh revision library closure required"
    candidate_admission_path = BASE / "revision-01" / ("candidate-" + name + "-admission.json")
    candidate_admission = json.loads(candidate_admission_path.read_text())
    assert candidate_admission["status"] == "ADMITTED" and candidate_admission["candidate"] == name
    assert candidate_admission["diagnostic_only"] == diagnostic_only
    assert candidate_admission.get("diagnostic_kind") == diagnostic_kind
    assert candidate_admission["authority"] == identity(LIMIT_AMENDMENT)
    if name in {"15", "16", "17", "18", "19", "20", "22", "23", "24", "25", "26", "27", "28"}:
        assert (not diagnostic_only or case59) and (not reuse_candidate_build or (name in {"22", "23", "24", "25", "26", "27", "28"} and reuse_candidate_build == "20"))
        assert candidate_admission["native_poll_authority"] == identity(NATIVE_POLL_AUTHORITY)
        assert set(candidate_admission["source_identities"]) == set(owned)
    if name in {"17", "18", "19", "20", "22", "23", "24", "25", "26", "27", "28"}:
        assert candidate_admission["case55_authority"] == identity(CASE55_AUTHORITY)
        amendment_inputs.add(CASE55_AUTHORITY.relative_to(ROOT).as_posix())
    if name in {"18", "19", "20", "22", "23", "24", "25", "26", "27", "28"}:
        assert candidate_admission["mechanical_authority"] == identity(MECHANICAL_AUTHORITY)
        prequalification(candidate_admission["prequalification"])
        amendment_inputs.add(MECHANICAL_AUTHORITY.relative_to(ROOT).as_posix())
    if name in {"20", "22", "23", "24", "25", "26", "27", "28"}:
        integrated_authorization()
        assert candidate_admission["integrated_authority"] == identity(INTEGRATED_AUTHORITY)
        assert prequalification(candidate_admission["prequalification"])["authority"] == identity(RISK_AUTHORITY if name in {"27", "28"} else PRELAUNCH_AUTHORITY if name == "26" else FINAL_CLOSURE_AUTHORITY if name in {"24", "25", "26", "27", "28"} else POST_HUMAN_AUTHORITY if name in {"22", "23"} else INTEGRATED_AUTHORITY)
        amendment_inputs.add(INTEGRATED_AUTHORITY.relative_to(ROOT).as_posix())
    if name in {"22", "23", "24", "25", "26", "27", "28"}:
        post_human = post_human_authorization()
        assert candidate_admission["post_human_authority"] == identity(POST_HUMAN_AUTHORITY)
        amendment_inputs.add(POST_HUMAN_AUTHORITY.relative_to(ROOT).as_posix())
        amendment_inputs.add(post_human["previous_authority"]["path"])
    if name in {"24", "25", "26", "27", "28"}:
        final_closure_authorization()
        assert candidate_admission["final_closure_authority"] == identity(FINAL_CLOSURE_AUTHORITY)
        compiled = prequalification(candidate_admission["prequalification"])
        assert compiled["case59_trace"] == case59
        if name in {"25", "26", "27", "28"}:
            assert compiled["case59_correction"] is True
        amendment_inputs.add(FINAL_CLOSURE_AUTHORITY.relative_to(ROOT).as_posix())
        amendment_inputs.add(PRELAUNCH_OBSERVER.relative_to(ROOT).as_posix())
        if case59:
            amendment_inputs.add(CASE59_ADMISSION.relative_to(ROOT).as_posix())
        if name in {"25", "26", "27", "28"}:
            assert candidate_admission["case59_correction"] == identity(CASE59_CORRECTION)
            amendment_inputs.add(CASE59_CORRECTION.relative_to(ROOT).as_posix())
    if name in {"26", "27", "28"}:
        prelaunch_authorization()
        assert candidate_admission["prelaunch_authority"] == identity(PRELAUNCH_AUTHORITY)
        assert candidate_admission["prelaunch_correction"] == identity(PRELAUNCH_CORRECTION)
        assert compiled["prelaunch_observer_proof"]["result"] == "PASS"
        amendment_inputs.update(p.relative_to(ROOT).as_posix() for p in (PRELAUNCH_AUTHORITY, PRELAUNCH_CORRECTION))
    if name in {"27", "28"}:
        risk_closure_authorization()
        assert candidate_admission["risk_closure_authority"] == identity(RISK_AUTHORITY)
        amendment_inputs.add(RISK_AUTHORITY.relative_to(ROOT).as_posix())
        amendment_inputs.add(candidate_admission["debug_reuse"]["path"])
    for pin in candidate_admission["causal_or_qualification_basis"]:
        assert identity(local(pin["path"])) == {k: pin[k] for k in ("sha256", "size")}
    for path, expected in candidate_admission["source_identities"].items():
        assert identity(ROOT / path) == expected, path
    amendment_inputs.add(candidate_admission_path.relative_to(ROOT).as_posix())
    amendment_inputs.update(p.relative_to(ROOT).as_posix() for p in (NATIVE_POLL_AUTHORITY, FOCUS_AUTHORITY))
    if first_pick:
        assert candidate_admission["diagnostic_authority"] == diagnostic_authority["authority"]
        amendment_inputs.update(pin["path"] for pin in diagnostic_authority.values()
                                if isinstance(pin, dict) and "path" in pin)
        amendment_inputs.update(pin["path"] for pin in diagnostic_authority["predecessor_Debug"].values())
        amendment_inputs.update(pin["path"] for pin in diagnostic_authority["original_cost_evidence"].values())
    isolation_platform = camera_fixture._phase17_platform()
    isolation_authorization = camera_fixture._phase17_authorization(ISOLATION_AMENDMENT)
    isolation_proof = camera_fixture._phase17_isolation_proof(isolation_authorization, isolation_platform)
    assert isolation_proof["result"] == "PASS"
    assert isolation_proof["helper"] == identity(ROOT / "tools/test_pre_editor_camera_snapshot.py")
    assert isolation_proof["platform"] == isolation_platform
    assert isolation_proof["authorization"] == isolation_authorization
    assert isolation_proof["final_authorization"] == {"path": str(LIMIT_AMENDMENT), **identity(LIMIT_AMENDMENT)}
    prior = json.loads(PRIOR.read_text())
    if control:
        for path in accepted["production_files"]:
            assert digest(ROOT / path) == entry["input_fingerprints"][path]["sha256"], path
    out.mkdir(parents=True, exist_ok=False)
    product_name = "Phase17" + ("Control" if control else "Candidate") + name
    for cfg in (["Release"] if control or first_pick else ["Debug", "Release"]):
        for parent in ("bin", "bin-int"):
            assert not local(f"{parent}/{cfg}/{product_name}").exists()
    source = (ROOT / "RigidBodySimulation/src/main.cpp").read_text()
    needle = "constexpr auto scenePreset = Rbs::RbsScenePreset::SphereBoxStack;"
    assert source.count(needle) == 1
    source = source.replace(needle, "constexpr auto scenePreset = Rbs::RbsScenePreset::GeometryGallery;")
    source = source.replace("Rbs::LaunchConfig settings;", "Rbs::LaunchConfig settings;\n    winProp.m_IsVsync = false;")
    anchor = "    // All consumers borrow this immutable value;"
    assert source.count(anchor) == 1
    source = source.replace(anchor,
        '    settings.engine.assetRootUtf8 = (std::filesystem::path(args[0]).parent_path().parent_path() / "assets").generic_string();\n' + anchor)
    main = out / "main-lifecycle-fixture.cpp"
    write(main, source)
    generated = [main]
    adapter = ""
    if control:
        source = (ROOT / "RigidBodySimulation/src/RbsValidation.cpp").read_text()
        source = '#include "CameraLifecycleChecks.h"\n' + source
        anchor = "bool RigidBodySimulationApp::ValidateUpdate()\n{"
        assert source.count(anchor) == 1
        source = source.replace(anchor, anchor + '\n    auto result = PreEditorLifecycle::ControlUpdate(*this);\n'
            '    if (!result) { FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Phase 17 control", {}, "diagnostic", {}, result.error().message}); return false; }\n'
            '    return *result;\n')
        validation = out / "RbsValidation-control.cpp"
        write(validation, source)
        generated.append(validation)
        adapter = f'<ClCompile Remove="src\\RbsValidation.cpp"/><ClCompile Include="{validation.as_posix()}"/>'
    if name in {"17", "18", "19", "20", "22", "23", "24", "25", "26", "27", "28"}:
        assert not control and (not diagnostic_only or case59)
        case55_authorization()
        validation = out / "RbsValidation-case55-fixture.cpp"
        fixture_source = case55_fixture_source((ROOT / "RigidBodySimulation/src/RbsValidation.cpp").read_text())
        if name in {"25", "26", "27", "28"}:
            fixture_source = final_qualification_fixture_source(fixture_source)
        write(validation, case59_fixture_source(fixture_source) if case59 else fixture_source)
        generated.append(validation)
        adapter = f'<ClCompile Remove="src\\RbsValidation.cpp"/><ClCompile Include="{validation.as_posix()}"/>'
    definitions = "GENGINE_LIFECYCLE_VALIDATION;" + ("GENGINE_LIFECYCLE_CONTROL;" if control else "")
    if not control:
        definitions += "GENGINE_LIFECYCLE_ORACLE_EVIDENCE;"
    if name in {"25", "26", "27", "28"}:
        definitions += "GENGINE_LIFECYCLE_CASE59_FIXTURE;"
    if first_pick:
        definitions += "GENGINE_LIFECYCLE_PICK_TRACE;"
    elif case59:
        definitions += "GENGINE_LIFECYCLE_CASE59_TRACE;"
    elif diagnostic_only:
        definitions += "GENGINE_LIFECYCLE_FOCUS_TRACE;"
    props = out / "isolated.props"
    write(props, '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        '<PropertyGroup>'
        f'<OutDir>{ROOT.as_posix()}/bin/$(Configuration)/{product_name}/$(MSBuildProjectName)/</OutDir>'
        f'<IntDir>{ROOT.as_posix()}/bin-int/$(Configuration)/{product_name}/$(MSBuildProjectName)/$(LifecycleLane)/</IntDir>'
        '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>\n'
        '<ItemDefinitionGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\' and \'$(LifecycleLane)\'==\'diagnostic\'">'
        f'<ClCompile><PreprocessorDefinitions>{definitions}%(PreprocessorDefinitions)</PreprocessorDefinitions>'
        f'<AdditionalIncludeDirectories>{ROOT.as_posix()}/RigidBodySimulation/tests;{ROOT.as_posix()}/RigidBodySimulation/src;'
        f'{ROOT.as_posix()}/external/sdl2/include;{ROOT.as_posix()}/external/glad/include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>'
        '</ClCompile></ItemDefinitionGroup>\n'
        '<ItemGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\' and \'$(LifecycleLane)\'==\'diagnostic\'">'
        f'<ClCompile Remove="src\\main.cpp"/><ClCompile Include="{main.as_posix()}"/>{adapter}'
        '</ItemGroup>\n</Project>\n')
    generated.append(props)
    names = set(PROJECTS + owned + ["premake5.lua", "external/glad/premake5.lua", "tools/postbuild.py",
        "tools/runtime_assets.json", "tools/test_pre_editor_rbs_modularity.py", "tools/test_pre_editor_camera_snapshot.py",
        "docs/pre_editor/phases/PHASE_17.md", "docs/pre_editor/VALIDATION_CONTRACTS.md",
        "docs/pre_editor/active/OUTPUT_PATHS.json", accepted["proposal"]["path"], ACCEPTANCE.relative_to(ROOT).as_posix()])
    names.update(amendment_inputs)
    names.update(p.relative_to(ROOT).as_posix() for p in (BASE / "revision-01").glob("*-admission*.json"))
    names.update(["logs/pre_editor/phase-17/corrections/14/implementation-admission-01.json",
                  "logs/pre_editor/phase-17/corrections/14/isolation-implementation-revision-01.md",
                  "logs/pre_editor/phase-17/corrections/15/admission.json",
                  "logs/pre_editor/phase-17/corrections/16/admission.json"])
    names.update(p.relative_to(ROOT).as_posix() for p in generated)
    names.update(prior["asset_members"].values())
    names.update(p for p in prior["input_hashes"] if p.startswith("external/") and p.endswith((".lib", ".dll", ".inl")))
    absent = set()
    closure.include_closure(names, absent)
    # Explicit diagnostic includes are not in the production project's search path.
    names.update(["RigidBodySimulation/tests/InputRoutingChecks.h", "RigidBodySimulation/tests/CameraSnapshotChecks.h"])
    for folder in ("external/sdl2/include", "external/glad/include"):
        names.update(p.relative_to(ROOT).as_posix() for p in (ROOT / folder).rglob("*.h"))
    names = {p for p in names if (ROOT / p).is_file()}
    absent = {p for p in absent if not (ROOT / p).exists()}
    for name_ in sorted(names):
        # Archive sources and validation inputs; prebuilt dependencies have identities below.
        if (ROOT / name_).suffix.lower() in (".cpp", ".c", ".h", ".hpp", ".inl", ".py", ".lua", ".props"):
            target = local(out / "source-snapshot" / name_)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name_, target)
    external_inputs, dependency_records = consumed_external_inputs(prior)
    manifest = {"phase": 17, "candidate": name, "control": control, "product_name": product_name,
        "diagnostic_only": diagnostic_only, "acceptance_eligible": not diagnostic_only,
        "candidate_admission": {"path": candidate_admission_path.relative_to(ROOT).as_posix(), **identity(candidate_admission_path)},
        "isolation_proof": {"path": camera_fixture.PHASE17_EMPTY_INVENTORY_PROOF.relative_to(ROOT).as_posix(),
                            **identity(camera_fixture.PHASE17_EMPTY_INVENTORY_PROOF)},
        "isolation_authorization": {"path": ISOLATION_AMENDMENT.relative_to(ROOT).as_posix(),
                                    **identity(ISOLATION_AMENDMENT)},
        "recorded_utc": utc(), "baseline": entry["baseline"], "owned_files": owned,
        "input_hashes": {p: digest(ROOT / p) for p in sorted(names)},
        "external_inputs": external_inputs, "dependency_discovery_records": dependency_records,
        "measured_shared_inputs": {path: digest(ROOT / path) for path in MEASURED_SHARED_INPUTS},
        "measurement_wrapper": measurement_wrapper(ROOT / "RigidBodySimulation/tests/CameraLifecycleChecks.h"),
        "compile_members": closure.compile_members(), "compile_discovery": closure.discovery(),
        "include_preceding_absent": sorted(absent), "asset_members": prior["asset_members"],
        "expected_modules": prior["expected_modules"], "layout": prior["layout"],
        "toolchain": prior["toolchain"], "machine": current_machine(),
        "protocol": {"acceptance": identity(ACCEPTANCE), "workload": "GeometryGallery exact baseline object construction and settings",
            "camera_pose": {"focal": [0, 7, 6], "distance": 28, "pitch": .12, "yaw": [0, .12],
                "orientation": "glm::quat(vec3(-pitch,-yaw,0)); position=focal-rotate(q,(0,0,-1))*28",
                "near": .1, "far": 1000, "vertical_fov_radians": math.pi/4, "orthographic_height": 20},
            "picking": prior["protocol"]["picking"], "background": prior["protocol"]["background"],
            "limits": {"build_seconds": limits["build_group_seconds"], "build_aggregate_seconds": limits["build_cumulative_seconds_cap"], "process_seconds": limits["process_seconds"],
                "process_aggregate_seconds": limits["native_cumulative_seconds_cap"], "initial_launches": 3, "total_launches": limits["native_cumulative_launch_cap"], "close_seconds": limits["close_seconds"],
                "diagnostic_only_Debug_launches": limits["diagnostic_cumulative_launch_cap"],
                "revision": limits,
                "segments_max": 16, "stable_frames": 2, "observed_frames": 8},
            "input_cost": {"pairs": 3, "lanes": ["OFF", "ON"], "warmup": 120, "samples": 240,
                "events": 64, "latency_max_ms": 16.667, "median_delta_max_ms": .100, "p95_delta_max_ms": .250,
                "function": "unchanged PreEditorInput::Measure", "control": "Phase16 modern input, all 64 deliveries",
                "candidate_roles": ["Editor", "Scene"], "observer": "report ON-minus-OFF; never subtract to pass"},
            "pacing": "diagnostic VSync off, no manual cap; production pacing unchanged",
            "runtime_limit_amendment": {"path": LIMIT_AMENDMENT.relative_to(ROOT).as_posix(), **identity(LIMIT_AMENDMENT)},
            "failure_observer": {"definition": "GENGINE_LIFECYCLE_ORACLE_EVIDENCE",
                "output": "lifecycle-oracle-failure.json", "boundary": "Failure path only, outside input-cost measurement; gates and short-circuit operands unchanged"},
            "stop": "Revision01: bounded diagnostics supply no acceptance; qualify Debug before Release; stop dependent work on failure, preserve evidence, correct only demonstrated authorized defects; no favorable-result retry or threshold change"},
        "writers": [str(out), f"bin/{{Debug,Release}}/{product_name}", f"bin-int/{{Debug,Release}}/{product_name}",
            f"runtime/RigidBodySimulation/{{Debug,Release}}/phase-17-{product_name}", "runtime/tmp"],
        "product_roles": "Original normal/diagnostic products archived and identified separately; no prior test claim for owner rebuilds"}
    if name in {"15", "16", "17", "18", "19", "20", "22", "23", "24", "25", "26", "27", "28"}:
        manifest["native_poll_authority"] = {"path": NATIVE_POLL_AUTHORITY.relative_to(ROOT).as_posix(), **identity(NATIVE_POLL_AUTHORITY)}
        manifest["control_reuse"] = corrected_control_binding(manifest)
        manifest["protocol"]["gesture_matrix"] = {"cases": 65, "held_alt_gestures": 20, "max_frames": 256,
            "output": "viewport-gestures.csv", "requires": "actual movement, zero unintended GPU reads, activation/capture, direct and polled provider regression"}
    if name in {"17", "18", "19", "20", "22", "23", "24", "25", "26", "27", "28"}:
        manifest["case55_authority"] = {"path": CASE55_AUTHORITY.relative_to(ROOT).as_posix(), **identity(CASE55_AUTHORITY)}
        manifest["protocol"]["case55_fixture"] = {"input": "Pinned Candidate16 production source", "overlay": validation.relative_to(ROOT).as_posix(),
            "correction": "Case55 visible canonical pose before baseline, ordinary snapshot publication, then original plain-LMB tuple next tick", "output": "case55-premise.csv"}
    if name in {"18", "19", "20", "22", "23", "24", "25", "26", "27", "28"}:
        manifest["mechanical_authority"] = {"path": MECHANICAL_AUTHORITY.relative_to(ROOT).as_posix(), **identity(MECHANICAL_AUTHORITY)}
        manifest["prequalification"] = candidate_admission["prequalification"]
    if name in {"20", "22", "23", "24", "25", "26", "27", "28"}:
        manifest["integrated_authority"] = {"path": INTEGRATED_AUTHORITY.relative_to(ROOT).as_posix(), **identity(INTEGRATED_AUTHORITY)}
        manifest["protocol"]["focus_visual"] = {"persistent": True, "flash_seconds": .25,
            "base_accent_alpha": .45, "flash_accent_alpha": .35, "fade": "smoothstep",
            "output": "viewport-focus-visual.csv", "capacity": 2048,
            "observer": "Existing frames after cost batches, preallocated memory; shutdown-only write"}
    if name in {"22", "23", "24", "25", "26", "27", "28"}:
        manifest["post_human_authority"] = {"path": POST_HUMAN_AUTHORITY.relative_to(ROOT).as_posix(), **identity(POST_HUMAN_AUTHORITY)}
        manifest["protocol"]["focus_visual"] = {"version": 2, **post_human["visual_contract"],
            "output": "viewport-focus-visual.csv", "capacity": 2048,
            "observer": "Existing frames after cost batches; exact opaque state colors, widths, raw geometry and clipping; bounded shutdown-only evidence"}
    if name in {"24", "25", "26", "27", "28"}:
        manifest["final_closure_authority"] = {"path": FINAL_CLOSURE_AUTHORITY.relative_to(ROOT).as_posix(), **identity(FINAL_CLOSURE_AUTHORITY)}
    if case59:
        manifest.update(diagnostic_kind=CASE59_KIND, case59_observer={"path": CASE59_ADMISSION.relative_to(ROOT).as_posix(), **identity(CASE59_ADMISSION)})
    if name in {"26", "27", "28"}:
        manifest["prelaunch_authority"] = {"path": PRELAUNCH_AUTHORITY.relative_to(ROOT).as_posix(), **identity(PRELAUNCH_AUTHORITY)}
        manifest["prelaunch_correction"] = {"path": PRELAUNCH_CORRECTION.relative_to(ROOT).as_posix(), **identity(PRELAUNCH_CORRECTION)}
    if name in {"25", "26", "27", "28"}:
        manifest["case59_correction"] = {"path": CASE59_CORRECTION.relative_to(ROOT).as_posix(), **identity(CASE59_CORRECTION)}
        manifest["protocol"]["case59_fixture"] = "One ordinary text setup click after original UI Alt gesture; bounded publication, then original center click and full observation interval; same65cases/256frame cap"
        manifest["protocol"]["focus_visual"]["lifetime_seconds"] = [0, .25, 1, 5]
    if name in {"27", "28"}:
        manifest["risk_closure_authority"] = {"path": RISK_AUTHORITY.relative_to(ROOT).as_posix(), **identity(RISK_AUTHORITY)}
        manifest["debug_reuse"] = candidate_admission["debug_reuse"]
        manifest["protocol"]["focus_visual"]["storage"] = "prefix2047_latest_complete_counters_first_failure_v1"
        require_debug_qualification(out, manifest)
    if first_pick:
        manifest.update(diagnostic_kind=FIRST_PICK_KIND, first_pick_diagnostic=diagnostic_authority)
        manifest["writers"] = [str(out), f"bin/Release/{product_name}", f"bin-int/Release/{product_name}",
            f"runtime/RigidBodySimulation/Release/phase-17-{product_name}", "runtime/tmp"]
        manifest["protocol"]["diagnostic_cost"] = {
            "purpose": "Unchanged Release startup stimulus only; no replacement accepted performance sample",
            "acceptance_credit": False, "original_evidence": diagnostic_authority["original_cost_evidence"]}
        manifest["protocol"]["diagnostic_prerequisite"] = {
            "configuration": "Release", "predecessor": diagnostic_authority["predecessor_Debug"],
            "meaning": "Pinned Candidate13 Debug predecessor only; no Candidate14 Debug PASS or qualification claimed"}
        manifest["protocol"]["stop"] = "Owner-pinned Candidate14 Release diagnostic only; preserve all original gates and startup stimulus; no acceptance, retry, replacement cost sample or budget reset"
    if reuse_control_build:
        assert control, "This reuse is confined to the unchanged Phase 16 control"
        previous = local(BASE / ("control-" + reuse_control_build))
        original = json.loads((previous / "scenario-manifest.json").read_text())
        build_record = json.loads((previous / "build-Release/result.json").read_text())
        assert original["control"] and build_record["exits"][:3] == [0, 0, 0]
        excluded = set(check_files) | amendment_inputs
        def ordinary_inputs(data):
            return {p: h for p, h in data["input_hashes"].items()
                    if p not in excluded and not p.startswith("logs/pre_editor/phase-17/")
                    and not (p.endswith((".lib", ".dll")) and not p.startswith("external/"))}
        assert ordinary_inputs(original) == ordinary_inputs(manifest), "Unchanged ordinary build inputs required"
        for key in ("external_inputs", "compile_members", "compile_discovery", "expected_modules", "asset_members", "toolchain"):
            assert original[key] == manifest[key], key
        assert (previous / "isolated.props").read_text().replace("Control" + reuse_control_build, "Control" + name).replace("control-" + reuse_control_build, "control-" + name) == props.read_text()
        reused = {kind: {"path": str(ROOT / f"bin/Release/{original['product_name']}/{kind}/{kind}.lib"),
            **identity(ROOT / f"bin/Release/{original['product_name']}/{kind}/{kind}.lib")} for kind in ("glad", "GEngine")}
        ordinary = build_record["ordinary_product"]
        assert identity(ordinary["path"]) == {key: ordinary[key] for key in ("sha256", "size")}
        manifest["reuse_control_build"] = {"record": str(previous / "build-Release/result.json"),
            **identity(previous / "build-Release/result.json"), "libraries": reused, "ordinary_product": ordinary}
        if build_record["result"] == "PASS":
            for header in ("RigidBodySimulation/tests/CameraLifecycleChecks.h", "RigidBodySimulation/tests/CameraSnapshotChecks.h"):
                assert original["input_hashes"][header] == manifest["input_hashes"][header]
            for generated_source in ("main-lifecycle-fixture.cpp", "RbsValidation-control.cpp"):
                assert (previous / generated_source).read_bytes() == (out / generated_source).read_bytes()
            binary = ROOT / f"bin/Release/{original['product_name']}/RigidBodySimulation/RigidBodySimulation.exe"
            assert identity(binary) == build_record["products"][str(binary)]
            manifest["reuse_control_build"]["diagnostic_product"] = {"path": str(binary), **identity(binary)}
    if reuse_candidate_build:
        previous = local(BASE / ("candidate-" + reuse_candidate_build))
        source_manifest = previous / "scenario-manifest.json"
        original = json.loads(source_manifest.read_text())
        assert not original["control"]
        library_names, library_absent = set(PROJECTS[:2]), set()
        original_projects = closure.PROJECTS
        try:
            closure.PROJECTS = PROJECTS[:2]
            closure.include_closure(library_names, library_absent)
        finally:
            closure.PROJECTS = original_projects
        library_inputs = {p: manifest["input_hashes"][p] for p in sorted(library_names)}
        assert all(original["input_hashes"].get(p) == h for p, h in library_inputs.items()), "Library source/dependency drift"
        for key in ("external_inputs", "expected_modules", "toolchain"):
            assert original[key] == manifest[key], key
        assert {k: v for k, v in original["compile_members"].items() if k in PROJECTS[:2]} == {k: v for k, v in manifest["compile_members"].items() if k in PROJECTS[:2]}
        assert original["compile_discovery"] == manifest["compile_discovery"]
        previous_props = (previous / "isolated.props").read_text().replace(original["product_name"], product_name).replace(
            "candidate-" + reuse_candidate_build, "candidate-" + name)
        assert normalized_reuse_props(previous_props) == normalized_reuse_props(props.read_text()), "Library build options differ"
        assert library_absent <= set(original["include_preceding_absent"]), "Preceding include-resolution proof differs"
        assert all(not (ROOT / p).exists() for p in library_absent)
        manifest["reuse_candidate_build"] = {}
        for cfg in ("Debug", "Release"):
            record = previous / f"build-{cfg}/result.json"
            if not record.exists():
                continue  # This configuration receives a fresh full build.
            built = json.loads(record.read_text())
            assert built["result"] == "PASS" and built["exits"] == [0, 0, 0, 0]
            assert built["revision_full_library_closure"], "No successful fresh revision library closure"
            reused = {kind: {"path": str(ROOT / f"bin/{cfg}/{original['product_name']}/{kind}/{kind}.lib"),
                **identity(ROOT / f"bin/{cfg}/{original['product_name']}/{kind}/{kind}.lib")} for kind in ("glad", "GEngine")}
            for source in reused.values():
                assert {k: source[k] for k in ("sha256", "size")} == built["products"][source["path"]]
            # A /Zi static library retains the original compiler-PDB path.
            # Pin these consumed prebuilt link inputs before the new build.
            link_inputs = {}
            for source in reused.values():
                pdb = local(Path(source["path"]).with_suffix(".pdb"))
                if cfg == "Debug":
                    assert pdb.is_file(), "Reused Debug library PDB missing: " + str(pdb)
                if pdb.is_file():
                    link_inputs[str(pdb)] = identity(pdb)
            manifest["reuse_candidate_build"][cfg] = {"record": str(record), **identity(record), "libraries": reused,
                "link_inputs": link_inputs,
                "source_manifest": {"path": str(source_manifest), **identity(source_manifest)},
                "library_input_hashes": library_inputs, "library_absent": sorted(library_absent),
                "options": "Exact library compile options; only isolated destination and diagnostic-RBS macros differ",
                "rbs_rebuild": "Fresh ordinary and qualifying RBS"}
    if reuse_staged_assets:
        assert re.fullmatch(r"[0-9]{2}", reuse_staged_assets)
        previous = local(BASE / ("candidate-" + reuse_staged_assets))
        original = json.loads((previous / "scenario-manifest.json").read_text())
        assert original["asset_members"] == manifest["asset_members"]
        assert all(original["input_hashes"][p] == manifest["input_hashes"][p]
                   for p in manifest["asset_members"].values())
        record = previous / "stage-Debug.json"
        staged = json.loads(record.read_text())
        assert staged["result"] == "PASS"
        asset_root = local(f"bin/Debug/{original['product_name']}/assets")
        for name_, source in manifest["asset_members"].items():
            assert digest(local(asset_root / name_)) == manifest["input_hashes"][source]
        for path, expected in staged["generated_startup_descriptors"].items():
            assert identity(local(path)) == expected
        manifest["reuse_staged_assets"] = {"root": str(asset_root), "record": str(record), **identity(record),
            "reason": "Read-only frozen package; exact asset membership/content and startup provenance"}
    save(out / "scenario-manifest.json", manifest)
    print(f"Prepared {out.relative_to(ROOT)}; {len(names)} consumed/project inputs", flush=True)


def corrected_control_binding(data):
    """Exact frozen Control05 baseline, with five explicitly authorized measured deltas."""
    control = BASE / "control-05/scenario-manifest.json"
    original = json.loads(control.read_text())
    prior = json.loads(FIRST_PICK_RUNNER_ADMISSION.read_text())
    assert identity(control) == {k: prior["control_manifest"][k] for k in ("sha256", "size")}
    old_runner = prior["before_runner"]
    assert identity(local(old_runner["path"])) == {k: old_runner[k] for k in ("sha256", "size")}
    assert old_runner["sha256"] == original["input_hashes"]["tools/test_pre_editor_camera_lifecycle.py"]
    shadow = local(original["frozen_control"]["root"])
    delta = {}
    for name in CORRECTED_PROVIDERS:
        before = original["measured_shared_inputs"][name]
        assert digest(local(shadow / name)) == original["frozen_control"]["files"][name] == before
        after = data["measured_shared_inputs"][name]
        assert digest(ROOT / name) == after and after != before
        delta[name] = {"baseline": before, "candidate": after}
    return {"manifest": {"path": control.relative_to(ROOT).as_posix(), **identity(control)},
            "historical_runner": old_runner, "measured_delta": delta,
            "meaning": "Control05 baseline only; new provider bytes receive fresh qualification and cost evidence"}


def verify(out, *, first_pick_control=None, corrected_control=None):
    guards()
    data = json.loads((out / "scenario-manifest.json").read_text())
    assert data.get("role") != "ordinary-owner-inspection-only", "Inspection artifacts cannot enter qualification"
    historical_runner = None
    corrected = None
    if corrected_control is not None:
        assert corrected_control["candidate"] in {"15", "16", "17", "18", "19", "20", "22", "23", "24", "25", "26", "27", "28"} and not corrected_control["diagnostic_only"]
        corrected = corrected_control_binding(corrected_control)
        assert corrected == corrected_control["control_reuse"]
        assert local(out) == local(BASE / "control-05") and data["control"]
        historical_runner = corrected["historical_runner"]
    if first_pick_control is not None:
        assert first_pick_mode(first_pick_control, "Release")
        assert local(out) == local(BASE / "control-05") and data["control"] and data["candidate"] == "05"
        binding = first_pick_control["first_pick_diagnostic"]
        assert identity(out / "scenario-manifest.json") == {k: binding["control_manifest"][k] for k in ("sha256", "size")}
        historical_runner = binding["historical_control_runner"]
        assert digest(ROOT / "tools/test_pre_editor_camera_lifecycle.py") == first_pick_control["input_hashes"]["tools/test_pre_editor_camera_lifecycle.py"]
    proof = data.get("isolation_proof")
    if proof:
        assert identity(local(proof["path"])) == {k: proof[k] for k in ("sha256", "size")}
    assert data["isolation_authorization"] == {
        "path": ISOLATION_AMENDMENT.relative_to(ROOT).as_posix(), **identity(ISOLATION_AMENDMENT)}
    for p, value in data["input_hashes"].items():
        if historical_runner is not None and p == "tools/test_pre_editor_camera_lifecycle.py":
            assert value == historical_runner["sha256"]
            assert identity(local(historical_runner["path"])) == {k: historical_runner[k] for k in ("sha256", "size")}
        elif corrected is not None and p in corrected["measured_delta"]:
            delta = corrected["measured_delta"][p]
            assert value == delta["baseline"] and digest(ROOT / p) == delta["candidate"], p
        else:
            assert digest(ROOT / p) == value, p
    if "mechanical_authority" in data:
        mechanical_authorization()
        assert data["candidate"] in {"18", "19", "20", "22", "23", "24", "25", "26", "27", "28"} and (not data.get("diagnostic_only") or case59_mode(data))
        assert data["mechanical_authority"] == {"path": MECHANICAL_AUTHORITY.relative_to(ROOT).as_posix(), **identity(MECHANICAL_AUTHORITY)}
        prequalification(data["prequalification"])
    if "integrated_authority" in data:
        integrated_authorization()
        assert data["candidate"] in {"20", "22", "23", "24", "25", "26", "27", "28"} and (not data["diagnostic_only"] or case59_mode(data))
        assert data["integrated_authority"] == {"path": INTEGRATED_AUTHORITY.relative_to(ROOT).as_posix(), **identity(INTEGRATED_AUTHORITY)}
        assert prequalification(data["prequalification"])["authority"] == identity(RISK_AUTHORITY if data["candidate"] in {"27", "28"} else PRELAUNCH_AUTHORITY if data["candidate"] == "26" else FINAL_CLOSURE_AUTHORITY if data["candidate"] in {"24", "25", "26", "27", "28"} else POST_HUMAN_AUTHORITY if data["candidate"] in {"22", "23"} else INTEGRATED_AUTHORITY)
    if "post_human_authority" in data:
        post_human = post_human_authorization()
        assert data["candidate"] in {"22", "23", "24", "25", "26", "27", "28"} and (not data["diagnostic_only"] or case59_mode(data))
        assert data["post_human_authority"] == {"path": POST_HUMAN_AUTHORITY.relative_to(ROOT).as_posix(), **identity(POST_HUMAN_AUTHORITY)}
        assert data["protocol"]["focus_visual"]["version"] == 2
        for key, expected in post_human["visual_contract"].items():
            assert data["protocol"]["focus_visual"][key] == expected
    if "case55_authority" in data:
        case55_authorization()
        assert data["candidate"] in {"17", "18", "19", "20", "22", "23", "24", "25", "26", "27", "28"} and (not data.get("diagnostic_only") or case59_mode(data))
        assert data["case55_authority"] == {"path": CASE55_AUTHORITY.relative_to(ROOT).as_posix(), **identity(CASE55_AUTHORITY)}
        overlay = local(data["protocol"]["case55_fixture"]["overlay"])
        expected = case55_fixture_source((ROOT / "RigidBodySimulation/src/RbsValidation.cpp").read_text())
        if data["candidate"] in {"25", "26", "27", "28"}:
            assert data["case59_correction"] == {"path": CASE59_CORRECTION.relative_to(ROOT).as_posix(), **identity(CASE59_CORRECTION)}
            expected = final_qualification_fixture_source(expected)
        assert overlay.read_text() == (case59_fixture_source(expected) if case59_mode(data) else expected)
    if "native_poll_authority" in data:
        assert data["native_poll_authority"] == {"path": NATIVE_POLL_AUTHORITY.relative_to(ROOT).as_posix(), **identity(NATIVE_POLL_AUTHORITY)}
        assert data["control_reuse"] == corrected_control_binding(data)
    for p, value in data["external_inputs"].items():
        assert identity(p) == value, p
    for reuse in data.get("reuse_candidate_build", {}).values():
        for p, value in reuse.get("link_inputs", {}).items():
            assert identity(local(p)) == value, "Reused link input drift: " + p
    assert closure.compile_members() == data["compile_members"]
    assert closure.discovery() == data["compile_discovery"]
    assert all(not (ROOT / p).exists() for p in data["include_preceding_absent"])
    if frozen := data.get("frozen_control"):
        shadow = local(frozen["root"])
        actual = {p.relative_to(shadow).as_posix().casefold() for p in shadow.rglob("*") if p.is_file()}
        assert actual == {p.casefold() for p in frozen["files"]}, "Frozen baseline membership drift"
        for path, expected in frozen["files"].items():
            assert digest(local(shadow / path)) == expected, path
        assert all(not (shadow / p).exists() for p in data["shadow_include_preceding_absent"])
    first_pick_mode(data)
    case59_mode(data)
    if data["candidate"] in {"24", "25", "26", "27", "28"} and not data["control"]:
        final_closure_authorization()
        assert data["final_closure_authority"] == {"path": FINAL_CLOSURE_AUTHORITY.relative_to(ROOT).as_posix(), **identity(FINAL_CLOSURE_AUTHORITY)}
    if data["candidate"] in {"26", "27", "28"} and not data["control"]:
        prelaunch_authorization()
        for key, path in (("prelaunch_authority", PRELAUNCH_AUTHORITY), ("prelaunch_correction", PRELAUNCH_CORRECTION)):
            assert data[key] == {"path": path.relative_to(ROOT).as_posix(), **identity(path)}
        assert prequalification(data["prequalification"])["prelaunch_observer_proof"]["result"] == "PASS"
    if "risk_closure_authority" in data:
        assert data["risk_closure_authority"] == {"path": RISK_AUTHORITY.relative_to(ROOT).as_posix(), **identity(RISK_AUTHORITY)}
        assert data["protocol"]["focus_visual"]["storage"] == "prefix2047_latest_complete_counters_first_failure_v1"
        require_debug_qualification(out, data)
    return data


def bounded_build_command(command, *, cwd, env, timeout, capture_output=False,
                          stdout=None, stderr=None, creationflags=0):
    """Contain newly created MSBuild and its descendants within one time budget."""
    assert os.name == "nt" and Path(command[0]).resolve() == MSBUILD.resolve()
    assert timeout > 0
    if capture_output:
        assert stdout is None and stderr is None
        stdout = stderr = subprocess.PIPE
    deadline = time.monotonic() + timeout
    # This is part of the caller's allowance, never an extension after timeout.
    cleanup_reserve = min(1.0, timeout / 4)

    class BasicLimits(ctypes.Structure):
        _fields_ = [("process_time", ctypes.c_longlong), ("job_time", ctypes.c_longlong),
            ("flags", wintypes.DWORD), ("working_min", ctypes.c_size_t),
            ("working_max", ctypes.c_size_t), ("process_limit", wintypes.DWORD),
            ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
            ("scheduling", wintypes.DWORD)]

    class IoCounters(ctypes.Structure):
        _fields_ = [(name, ctypes.c_ulonglong) for name in
            ("read_ops", "write_ops", "other_ops", "read_bytes", "write_bytes", "other_bytes")]

    class ExtendedLimits(ctypes.Structure):
        _fields_ = [("basic", BasicLimits), ("io", IoCounters),
            ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
            ("peak_process_memory", ctypes.c_size_t), ("peak_job_memory", ctypes.c_size_t)]

    class Accounting(ctypes.Structure):
        _fields_ = [("user_time", ctypes.c_longlong), ("kernel_time", ctypes.c_longlong),
            ("period_user_time", ctypes.c_longlong), ("period_kernel_time", ctypes.c_longlong),
            ("page_faults", wintypes.DWORD), ("total", wintypes.DWORD),
            ("active", wintypes.DWORD), ("terminated", wintypes.DWORD)]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel.CreateJobObjectW.restype = wintypes.HANDLE
    kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    kernel.SetInformationJobObject.restype = wintypes.BOOL
    kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    kernel.AssignProcessToJobObject.restype = wintypes.BOOL
    kernel.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                               wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    kernel.QueryInformationJobObject.restype = wintypes.BOOL
    kernel.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
    kernel.TerminateJobObject.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    native = ctypes.WinDLL("ntdll")
    native.NtResumeProcess.argtypes = [wintypes.HANDLE]
    native.NtResumeProcess.restype = wintypes.LONG
    job = kernel.CreateJobObjectW(None, None)  # Unnamed, non-inheritable private job.
    if not job:
        raise ctypes.WinError(ctypes.get_last_error())
    process = None
    assigned = False
    try:
        limits = ExtendedLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE; no breakaway.
        if not kernel.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            raise ctypes.WinError(ctypes.get_last_error())
        if time.monotonic() + cleanup_reserve >= deadline:
            raise subprocess.TimeoutExpired(command, timeout)
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=stdout, stderr=stderr,
            creationflags=creationflags | subprocess.CREATE_NO_WINDOW | 0x4)  # CREATE_SUSPENDED
        if not kernel.AssignProcessToJobObject(job, process._handle):
            raise ctypes.WinError(ctypes.get_last_error())
        assigned = True
        work_seconds = deadline - time.monotonic() - cleanup_reserve
        if work_seconds <= 0:
            raise subprocess.TimeoutExpired(command, timeout)
        # NtResumeProcess is used only on the handle returned by our suspended
        # MSBuild Popen above, after job assignment. It never touches externals.
        status = native.NtResumeProcess(process._handle)
        if status < 0:
            raise OSError(f"Owned MSBuild resume failed: NTSTATUS 0x{status & 0xffffffff:08x}")
        output, errors = process.communicate(timeout=work_seconds)
        result = subprocess.CompletedProcess(command, process.returncode, output, errors)
    finally:
        try:
            if process is not None:
                if assigned:
                    if not kernel.TerminateJobObject(job, 1):
                        raise ctypes.WinError(ctypes.get_last_error())
                elif process.poll() is None:
                    process.kill()  # Assignment failed; this owned process is still suspended.
                process.wait(timeout=max(0, deadline - time.monotonic()))
                if assigned:
                    accounting = Accounting()
                    while True:
                        if not kernel.QueryInformationJobObject(job, 1, ctypes.byref(accounting),
                                                                ctypes.sizeof(accounting), None):
                            raise ctypes.WinError(ctypes.get_last_error())
                        if accounting.active == 0:
                            break
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            raise TimeoutError("Owned MSBuild tree did not exit within its accepted allowance")
                        time.sleep(min(.01, remaining))
            if time.monotonic() > deadline:
                raise TimeoutError("Owned MSBuild command and cleanup exceeded the accepted allowance")
        finally:
            # Kernel kill-on-close also contains cleanup errors and abrupt runner exit.
            if not kernel.CloseHandle(job):
                raise ctypes.WinError(ctypes.get_last_error())
    return result


def verify_built_dependencies(data, cfg):
    """Every actually read non-output dependency must have a pre-build identity."""
    pinned = {str((ROOT / path).resolve()).casefold(): expected for path, expected in data["input_hashes"].items()}
    pinned.update({str(Path(path).resolve()).casefold(): expected["sha256"]
                   for path, expected in data["external_inputs"].items()})
    pinned.update({str(local(path)).casefold(): expected["sha256"]
                   for path, expected in data.get("reuse_candidate_build", {}).get(cfg, {}).get("link_inputs", {}).items()})
    outputs = [local(f"bin/{cfg}/{data['product_name']}"), local(f"bin-int/{cfg}/{data['product_name']}")]
    records, consumed = {}, set()
    for log in sorted(outputs[1].rglob("*.read.*.tlog")):
        records[log.relative_to(ROOT).as_posix()] = identity(log)
        rows = [part for row in log.read_text(encoding="utf-16").splitlines() if row
                for part in (row[1:].split("|") if row.startswith("^") else [row])]
        for row in rows:
            path = Path(row).resolve()
            if any(path.is_relative_to(folder) for folder in outputs):
                continue  # PCH, objects, library and executable outputs are separately identified.
            assert path.is_file(), "Consumed dependency disappeared: " + str(path)
            key = str(path).casefold()
            assert key in pinned, "Consumed dependency lacked pre-build identity: " + str(path)
            if key not in consumed:
                assert digest(path) == pinned[key], "Consumed dependency changed: " + str(path)
                consumed.add(key)
    assert records and consumed, "Fresh compiler dependency records missing"
    return {"result": "PASS", "consumed_nonoutput_files": len(consumed), "records": records,
            "policy": "Pre-build input identities only; no post-build backfill"}


def build(out, cfg):
    data = verify(out)
    first_pick = first_pick_mode(data, cfg)
    case59 = case59_mode(data, cfg)
    authority, revision_entry = revision_authorization()
    limits = authority["new_execution_budget"]
    assert not data["control"] or (data["candidate"] == "05" and cfg == "Release" and "frozen_control" in data)
    if cfg == "Release" and not data["control"] and not first_pick:
        require_debug_qualification(out, data)
    assert not data.get("diagnostic_only") or first_pick or case59 or (cfg == "Debug" and not FIRST_PICK_AUTHORITY.exists()), "Only the owner-pinned Release diagnostic allocation remains"
    if first_pick:
        assert not (out / "build-Debug").exists() and not (out / "run-Debug.json").exists()
    prior_seconds = sum(json.loads(p.read_text())["seconds"] for p in BASE.glob("*/build-*/result.json"))
    new_builds = revision_usage("*/build-*/result.json", revision_entry["historical_builds"])
    revision_seconds = sum(row["seconds"] for _, row in new_builds)
    configuration_seconds = sum(row["seconds"] for p, row in new_builds if p.parent.name == "build-" + cfg)
    allowance = min(limits["build_group_seconds"], limits["build_cumulative_seconds_cap"] - prior_seconds,
                    limits["build_revision_seconds"] - revision_seconds,
                    limits["build_revision_configuration_seconds"][cfg] - configuration_seconds)
    assert allowance > 0, "Aggregate build limit reached"
    assert all((p / "result.json").exists() for p in BASE.glob("*/build-*") if p.is_dir()), "Unfinished build attempt requires explicit accounting"
    folder = local(out / ("build-" + cfg))
    folder.mkdir()
    save(folder / "start.json", {"recorded_utc": utc(), "allowance_seconds": allowance, "configuration": cfg,
        "authority": identity(LIMIT_AMENDMENT), "native_poll_authority": identity(NATIVE_POLL_AUTHORITY), "case55_authority": identity(CASE55_AUTHORITY), "historical_seconds": prior_seconds, "revision_seconds": revision_seconds})
    report = {"result": "FAIL", "recorded_utc": utc(), "commands": [], "exits": []}
    start = time.monotonic()
    def remaining():
        value = allowance - (time.monotonic() - start)
        assert value > 0, "Accepted build time limit"
        return value
    common = [f"/p:Configuration={cfg}", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
        "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:ForceImportBeforeCppTargets=" + str(out / "isolated.props")]
    try:
        projects = [(PROJECTS[1], "ordinary"), (PROJECTS[0], "ordinary"),
                    (PROJECTS[2], "ordinary"), (PROJECTS[2], "diagnostic")]
        if reuse := data.get("reuse_control_build") or data.get("reuse_candidate_build", {}).get(cfg):
            if data["control"]: assert cfg == "Release"
            assert identity(reuse["record"]) == {key: reuse[key] for key in ("sha256", "size")}
            frozen = reuse["source_manifest"]
            assert identity(local(frozen["path"])) == {key: frozen[key] for key in ("sha256", "size")}
            for kind, source in reuse["libraries"].items():
                assert identity(source["path"]) == {key: source[key] for key in ("sha256", "size")}
                target = local(f"bin/{cfg}/{data['product_name']}/{kind}/{kind}.lib")
                target.parent.mkdir(parents=True, exist_ok=False)
                os.link(local(source["path"]), target)
            report.update(reused_build=reuse, exits=[0, 0])
            projects = projects[2:]
            if ordinary := reuse.get("ordinary_product"):
                assert identity(ordinary["path"]) == {key: ordinary[key] for key in ("sha256", "size")}
                archive = local(folder / "ordinary/RigidBodySimulation.exe")
                archive.parent.mkdir()
                os.link(local(ordinary["path"]), archive)
                report.update(ordinary_product={"path": str(archive), **identity(archive)}, exits=[0, 0, 0])
                projects = projects[1:]
            if diagnostic := reuse.get("diagnostic_product"):
                assert identity(diagnostic["path"]) == {key: diagnostic[key] for key in ("sha256", "size")}
                target = local(f"bin/{cfg}/{data['product_name']}/RigidBodySimulation/RigidBodySimulation.exe")
                target.parent.mkdir(parents=True, exist_ok=False)
                os.link(local(diagnostic["path"]), target)
                report["exits"].append(0)
                projects = []
        for project, lane in projects:
            stem = Path(project).stem + "-" + lane
            source_project = data.get("source_projects", {}).get(project, project)
            args = [str(MSBUILD), source_project, *common, "/p:LifecycleLane=" + lane]
            evaluated = bounded_build_command(args + ["/getProperty:OutDir,IntDir,TargetPath,PostBuildEventUseInBuild,VCToolsVersion,WindowsTargetPlatformVersion", "/nologo"],
                cwd=ROOT, env=environment(), capture_output=True, timeout=min(30, remaining()))
            save(folder / (stem + "-evaluation.json"), {"exit": evaluated.returncode,
                "stdout": evaluated.stdout.decode(errors="replace"), "stderr": evaluated.stderr.decode(errors="replace")})
            assert evaluated.returncode == 0
            props = json.loads(evaluated.stdout)["Properties"]
            for key in ("OutDir", "IntDir", "TargetPath"):
                assert data["product_name"] in str(local(props[key]))
            assert props["PostBuildEventUseInBuild"] == "false"
            assert props["VCToolsVersion"] == "14.44.35207" and props["WindowsTargetPlatformVersion"] == "10.0.26100.0"
            command = args + ["/t:Build", "/p:BuildProjectReferences=false", "/m:2", "/nr:false", "/nologo", "/v:normal", "/bl:" + str(folder / (stem + ".binlog"))]
            report["commands"].append(command)
            print(f"Building {data['product_name']} {cfg} {stem}", flush=True)
            with (folder / (stem + ".log")).open("xb") as stream:
                result = bounded_build_command(command, cwd=ROOT, env=environment(), stdout=stream, stderr=subprocess.STDOUT,
                    timeout=remaining(), creationflags=subprocess.CREATE_NO_WINDOW)
            report["exits"].append(result.returncode)
            assert result.returncode == 0, stem + " compilation failed"
            if project == PROJECTS[2] and lane == "ordinary":
                target = local(props["TargetPath"])
                archive = local(out / f"build-{cfg}/ordinary/RigidBodySimulation.exe")
                archive.parent.mkdir()
                shutil.copyfile(target, archive)
                report["ordinary_product"] = {"path": str(archive), **identity(archive)}
        verify(out)
        report["consumed_dependency_check"] = verify_built_dependencies(data, cfg)
        report.update(result="PASS", fresh_full_closure=not bool(data.get("reuse_candidate_build", {}).get(cfg)),
            revision_full_library_closure=True, products={str(p): identity(p) for p in [
            local(f"bin/{cfg}/{data['product_name']}/{name}/{name}.{extension}")
            for name, extension in [("glad", "lib"), ("GEngine", "lib"), ("RigidBodySimulation", "exe")]]})
        remaining()
    except Exception as error:
        report["result"] = "FAIL"
        report["reason"] = str(error)
    report["seconds"] = time.monotonic() - start
    save(folder / "result.json", report)
    print(json.dumps({k: report[k] for k in ("result", "seconds", "exits")} | {"reason": report.get("reason")}), flush=True)
    if report["result"] != "PASS":
        raise SystemExit(1)


def prepare_owner_border_release(out):
    """Freeze an ordinary inspection build; never supplies qualification credit."""
    entry, accepted, owned = guards()
    assert out == local(BASE / "owner-release-21") and not out.exists()
    authority_path = local("docs/pre_editor/active/PHASE_17_BORDER_VISUAL_REVISION_AUTHORITY.json")
    authority = json.loads(authority_path.read_text())
    assert authority["status"] == "ACCEPTED" and authority["native_launch_permission"] is False
    assert authority["new_execution_budget"] == revision_authorization()[0]["new_execution_budget"]
    admission_path = local(authority["evidence_root"]) / "admission.json"
    admission = json.loads(admission_path.read_text())
    allowed = {"RigidBodySimulation/src/RbsUi.cpp", "RigidBodySimulation/tests/CameraLifecycleChecks.h",
               "tools/test_pre_editor_camera_lifecycle.py"}
    for path, before in admission["preexisting_sources"].items():
        if path not in allowed:
            assert identity(ROOT / path) == before, "Border revision changed unrelated source: " + path
    prior = json.loads((BASE / "candidate-20/scenario-manifest.json").read_text())
    out.mkdir()
    props = out / "isolated.props"
    product = "Phase17Candidate21"
    write(props, '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        '<PropertyGroup>'
        f'<OutDir>{ROOT.as_posix()}/bin/$(Configuration)/{product}/$(MSBuildProjectName)/</OutDir>'
        f'<IntDir>{ROOT.as_posix()}/bin-int/$(Configuration)/{product}/$(MSBuildProjectName)/ordinary/</IntDir>'
        '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>\n</Project>\n')
    # Start with the already declared dependency closure, excluding historical
    # evidence/generated qualification TUs; discover the current ordinary TUs.
    names = {p for p in prior["input_hashes"] if not p.startswith("logs/")}
    names.update(owned + PROJECTS + [authority_path.relative_to(ROOT).as_posix(), props.relative_to(ROOT).as_posix()])
    absent = set()
    closure.include_closure(names, absent)
    names = {p for p in names if (ROOT / p).is_file()}
    external, discovery_records = consumed_external_inputs(prior)
    for name in sorted(names):
        if (ROOT / name).suffix.lower() in (".cpp", ".c", ".h", ".hpp", ".inl", ".py", ".lua", ".props"):
            target = local(out / "source-snapshot" / name)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
    manifest = {"phase": 17, "candidate": "21", "control": False, "product_name": product,
        "role": "ordinary-owner-inspection-only", "acceptance_eligible": False, "native_launch_authorized": False,
        "recorded_utc": utc(), "baseline": entry["baseline"], "owned_files": owned,
        "authority": {"path": authority_path.relative_to(ROOT).as_posix(), **identity(authority_path)},
        "admission": {"path": admission_path.relative_to(ROOT).as_posix(), **identity(admission_path)},
        "input_hashes": {p: digest(ROOT / p) for p in sorted(names)},
        "absent_inputs": sorted(p for p in absent if not (ROOT / p).exists()),
        "external_inputs": external, "dependency_discovery_records": discovery_records,
        "compile_members": closure.compile_members(), "source_discovery": closure.discovery(),
        "asset_members": prior["asset_members"], "expected_modules": prior["expected_modules"],
        "protocol": {"focus_visual": {"version": 2, **authority["visual_contract"]},
                     "qualification": "NOT RUN; full Debug PASS still required before a Release qualifier",
                     "scene": "Unmodified ordinary main.cpp; SphereBoxStack; no validation macros or fixture overlay"}}
    save(out / "scenario-manifest.json", manifest)
    return manifest


def build_owner_border_release(out):
    """One fresh ordinary Release closure under existing aggregate/time limits."""
    data = json.loads((out / "scenario-manifest.json").read_text())
    assert out == local(BASE / "owner-release-21") and data["role"] == "ordinary-owner-inspection-only"
    assert not data["acceptance_eligible"] and not data["native_launch_authorized"]
    def frozen():
        guards()
        for path, expected in data["input_hashes"].items():
            assert digest(ROOT / path) == expected, "Inspection build input drift: " + path
        for path, expected in data["external_inputs"].items():
            assert identity(path) == expected, "Inspection build dependency drift: " + path
        assert all(not (ROOT / p).exists() for p in data["absent_inputs"])
        assert closure.compile_members() == data["compile_members"]
        assert closure.discovery() == data["source_discovery"]
    frozen()
    authority, revision_entry = revision_authorization()
    limits = authority["new_execution_budget"]
    builds = [(p, json.loads(p.read_text())) for p in BASE.glob("*/build-*/result.json")]
    recent = [(p, row) for p, row in builds if str(p.relative_to(BASE)) not in revision_entry["historical_builds"]]
    total = sum(row["seconds"] for _, row in builds)
    revised = sum(row["seconds"] for _, row in recent)
    release = sum(row["seconds"] for p, row in recent if p.parent.name == "build-Release")
    allowance = min(limits["build_group_seconds"], limits["build_cumulative_seconds_cap"]-total,
                    limits["build_revision_seconds"]-revised,
                    limits["build_revision_configuration_seconds"]["Release"]-release)
    assert allowance > 0
    assert all((p / "result.json").exists() for p in BASE.glob("*/build-*") if p.is_dir())
    folder = local(out / "build-Release"); folder.mkdir()
    save(folder / "start.json", {"recorded_utc": utc(), "role": data["role"], "allowance_seconds": allowance,
        "configuration": "Release", "authority": data["authority"], "historical_seconds": total,
        "revision_seconds": revised, "configuration_seconds": release,
        "qualification_guard": "Separate ordinary owner-build authorization; no failed Debug gate bypass or acceptance"})
    report = {"result": "FAIL", "role": data["role"], "recorded_utc": utc(), "commands": [], "exits": [],
              "acceptance_eligible": False, "native_launches": 0}
    started = time.monotonic()
    def remaining():
        value = allowance-(time.monotonic()-started)
        assert value > 0, "Existing build time limit exhausted"
        return value
    try:
        for project in (PROJECTS[1], PROJECTS[0], PROJECTS[2]):
            stem = Path(project).stem
            args = [str(MSBUILD), project, "/p:Configuration=Release", "/p:Platform=x64",
                "/p:VCToolsVersion=14.44.35207", "/p:WindowsTargetPlatformVersion=10.0.26100.0",
                "/p:ForceImportBeforeCppTargets=" + str(out / "isolated.props")]
            evaluation = args + ["/getProperty:OutDir,IntDir,TargetPath,PostBuildEventUseInBuild,VCToolsVersion,WindowsTargetPlatformVersion", "/nologo"]
            evaluated = bounded_build_command(evaluation, cwd=ROOT, env=environment(), capture_output=True, timeout=min(30, remaining()))
            save(folder / (stem + "-evaluation.json"), {"command": evaluation, "exit": evaluated.returncode,
                "stdout": evaluated.stdout.decode(errors="replace"), "stderr": evaluated.stderr.decode(errors="replace")})
            assert evaluated.returncode == 0
            props = json.loads(evaluated.stdout)["Properties"]
            for key in ("OutDir", "IntDir", "TargetPath"):
                assert data["product_name"] in str(local(props[key]))
            assert props["PostBuildEventUseInBuild"] == "false"
            assert props["VCToolsVersion"] == "14.44.35207" and props["WindowsTargetPlatformVersion"] == "10.0.26100.0"
            command = args + ["/t:Build", "/p:BuildProjectReferences=false", "/m:2", "/nr:false", "/nologo", "/v:normal",
                              "/bl:" + str(folder / (stem + ".binlog"))]
            report["commands"].append(command)
            print("Building ordinary owner Release: " + stem, flush=True)
            with (folder / (stem + ".log")).open("xb") as stream:
                result = bounded_build_command(command, cwd=ROOT, env=environment(), stdout=stream,
                    stderr=subprocess.STDOUT, timeout=remaining(), creationflags=subprocess.CREATE_NO_WINDOW)
            report["exits"].append(result.returncode)
            assert result.returncode == 0, stem + " compilation failed"
        frozen()
        report["consumed_dependency_check"] = verify_built_dependencies(data, "Release")
        report["products"] = {str(p): identity(p) for p in [
            local(f"bin/Release/{data['product_name']}/{name}/{name}.{extension}")
            for name, extension in (("glad", "lib"), ("GEngine", "lib"), ("RigidBodySimulation", "exe"))]}
        remaining()
        report["result"] = "PASS"
    except Exception as error:
        report["reason"] = str(error)
    report["seconds"] = time.monotonic()-started
    save(folder / "result.json", report)
    print(json.dumps({k: report.get(k) for k in ("result", "seconds", "exits", "reason")}), flush=True)
    if report["result"] != "PASS":
        raise SystemExit(1)


def focus_storage_preflight(out):
    """Compile the actual bounded collector and exercise the former 2048 boundary; no RBS."""
    guards()
    current = risk_closure_authorization()
    evidence = local(current["evidence_root"])
    focused = sorted(evidence.glob("focused-check-*"))
    assert all((p/"result.json").is_file() for p in focused)
    used = current["usage_at_entry"]["focused_invocations"]+len(focused)
    seconds = current["usage_at_entry"]["focused_seconds"]+sum(json.loads((p/"result.json").read_text())["seconds"] for p in focused)
    limits = current["new_execution_budget"]
    _, entry = revision_authorization()
    builds = [(p,json.loads(p.read_text())) for p in BASE.glob("*/build-*/result.json")]
    recent = [(p,row) for p,row in builds if str(p.relative_to(BASE)) not in entry["historical_builds"]]
    allowance = min(60,limits["non_RBS_aggregate_seconds"]-seconds,
        limits["build_cumulative_seconds_cap"]-sum(row["seconds"] for _,row in builds),
        limits["build_revision_seconds"]-sum(row["seconds"] for _,row in recent),
        limits["build_revision_configuration_seconds"]["Debug"]-sum(row["seconds"] for p,row in recent if p.parent.name=="build-Debug"))
    assert used<limits["non_RBS_focused_invocations"] and allowance>0
    out=local(out); folder=local(out/"build-Debug"); folder.mkdir(parents=True,exist_ok=False)
    focused_out=local(evidence/f"focused-check-{len(focused)+1:02d}"); focused_out.mkdir()
    header=(ROOT/"RigidBodySimulation/tests/CameraLifecycleChecks.h").read_text()
    block=header[header.index("struct FocusVisualProbe"):header.index("        void WriteRow(")]+"};\n"
    source=local(out/"focus-storage.cpp")
    write(source,'#include <array>\n#include <cstdint>\n#include <imgui/imgui.h>\n'+block+r'''
extern "C" __declspec(dllexport) int Check(unsigned n, std::uint64_t* out)
{
    static FocusVisualProbe p; p=FocusVisualProbe{};
    for(unsigned i=1;i<=n;++i)
    {
        if(i>1 && p.rows[p.count-1].update!=i-1) return 1;
        FocusVisualProbe::Row r{};
        r.update=i; r.segment=11; r.step=4+i%20; r.activations=i<5000 ? 1 : 2;
        r.active=r.drawable=true; r.pass=i!=9000; r.failures=r.pass ? 0 : 32;
        r.time=i*.001; r.flash=i<250 ? .5f : 0.f; r.border=1; r.alpha=255;
        r.scale={1,1}; p.Record(r);
        if(p.rows[p.count-1].update!=i || p.total!=i || p.count!=(i<2048 ? i : 2048)) return 2;
        for(unsigned j=0;j<p.count-1;++j) if(p.rows[j].update!=j+1) return 3;
    }
    const std::uint64_t values[]={p.total,p.count,p.passed,p.failed,p.firstFailureOrdinal,p.firstFailure.update,
        p.failureBits,p.gestureMask,p.digest,p.multipleActivations,p.persistent,p.afterFiveSeconds,p.opaqueFrames};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i) out[i]=values[i];
    return 0;
}
''')
    props=local(out/"storage.props")
    write(props,'<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">'
        '<PropertyGroup><ConfigurationType>DynamicLibrary</ConfigurationType><TargetExt>.dll</TargetExt>'
        f'<OutDir>{out.as_posix()}/product/</OutDir><IntDir>{out.as_posix()}/intermediate/</IntDir>'
        '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>'
        '<ItemGroup><ProjectReference Remove="@(ProjectReference)"/><ClCompile Remove="@(ClCompile)"/>'
        f'<ClCompile Include="{source.as_posix()}"/></ItemGroup>'
        '<ItemDefinitionGroup><Link><AdditionalDependencies>kernel32.lib</AdditionalDependencies><AdditionalOptions>/DLL %(AdditionalOptions)</AdditionalOptions></Link></ItemDefinitionGroup></Project>')
    common=[str(MSBUILD),"RigidBodySimulation/RigidBodySimulation.vcxproj","/p:Configuration=Debug","/p:Platform=x64",
        "/p:VCToolsVersion=14.44.35207","/p:WindowsTargetPlatformVersion=10.0.26100.0",
        "/p:ForceImportBeforeCppTargets="+str(props),"/p:BuildProjectReferences=false","/nr:false","/nologo"]
    report={"result":"FAIL","kind":"BOUNDED_FOCUS_STORAGE_PREFLIGHT","recorded_utc":utc(),"invocation":used+1,
        "authority":identity(RISK_AUTHORITY),"source":identity(source),"header":identity(ROOT/"RigidBodySimulation/tests/CameraLifecycleChecks.h"),
        "allowance_seconds":allowance,"native_launches":0,"application_launches":0,"commands":[],"exits":[]}
    save(folder/"start.json",report); save(focused_out/"start.json",report)
    started=time.monotonic()
    try:
        command=common+["/getItem:ClCompile","/getProperty:TargetPath,ConfigurationType"]
        report["commands"].append(command)
        result=bounded_build_command(command,cwd=ROOT,env=environment(),capture_output=True,timeout=min(15,allowance))
        report["exits"].append(result.returncode)
        save(folder/"evaluation.json",{"exit":result.returncode,"stdout":result.stdout.decode(errors="replace"),"stderr":result.stderr.decode(errors="replace")})
        assert result.returncode==0
        evaluated=json.loads(result.stdout); items=evaluated["Items"]["ClCompile"]
        assert len(items)==1 and local(items[0]["Identity"])==source
        target=local(evaluated["Properties"]["TargetPath"])
        assert target.is_relative_to(out) and target.suffix==".dll" and evaluated["Properties"]["ConfigurationType"]=="DynamicLibrary"
        command=common+["/t:Build","/m:1","/v:normal"]
        report["commands"].append(command)
        with (folder/"compile.log").open("xb") as stream:
            result=bounded_build_command(command,cwd=ROOT,env=environment(),stdout=stream,stderr=subprocess.STDOUT,
                timeout=allowance-(time.monotonic()-started),creationflags=subprocess.CREATE_NO_WINDOW)
        report["exits"].append(result.returncode); assert result.returncode==0,"Storage compile"
        module=ctypes.CDLL(str(target)); check=module.Check
        check.argtypes=[ctypes.c_uint,ctypes.POINTER(ctypes.c_uint64)]; check.restype=ctypes.c_int
        import struct
        report["cases"]=[]
        for n in (1065,2048,2049,10000):
            values=(ctypes.c_uint64*13)(); assert check(n,values)==0
            h=14695981039346656037
            for i in range(1,n+1):
                scalar=struct.pack('<QIIIQ???ddffiiII10f',i,i,11,4+i%20,1 if i<5000 else 2,
                    True,True,i!=9000,i*.001,0.,.5 if i<250 else 0.,1.,0,0,255,0 if i!=9000 else 32,
                    0.,0.,0.,0.,0.,0.,0.,0.,1.,1.)
                for byte in scalar: h=((h^byte)*1099511628211)&((1<<64)-1)
            fail=int(n>=9000)
            expected=[n,min(n,2048),n-fail,fail,9000 if fail else 0,9000 if fail else 0,32 if fail else 0,
                (1<<20)-1,h,int(n>=5000),n-249,max(0,n-4999),n-fail]
            assert list(values)==expected,(n,list(values),expected)
            report["cases"].append({"frames":n,"values":list(values),"result":"PASS"})
        assert time.monotonic()-started<allowance
        report.update(result="PASS",binary={"path":str(target),**identity(target)},
            meaning="Actual collector, exact scalar-byte digest, all previous/latest rows, bounded prefix, failure after former capacity; no ImGui/SDL/GPU calls")
    except Exception as error:
        report["reason"]=repr(error)
    report["seconds"]=time.monotonic()-started
    save(folder/"result.json",report);save(focused_out/"result.json",report)
    print(json.dumps({k:report.get(k) for k in ("result","invocation","seconds","exits","reason")}),flush=True)
    if report["result"]!="PASS": raise SystemExit(1)


def precompile_fixture(out, *, ui=False, case59_trace=False, case59_correction=False):
    """Compile the actual Case55 TU after proving its evaluated item selection."""
    guards()
    current = risk_closure_authorization()
    authority, entry = revision_authorization()
    limits = authority["new_execution_budget"]
    evidence = local(current["evidence_root"])
    focused = sorted(evidence.glob("focused-check-*"))
    assert all((p / "result.json").is_file() for p in focused), "Unfinished focused check"
    used = current["usage_at_entry"]["focused_invocations"] + len(focused)
    seconds = current["usage_at_entry"]["focused_seconds"] + sum(json.loads((p / "result.json").read_text())["seconds"] for p in focused)
    assert used < limits["non_RBS_focused_invocations"]
    builds = [(p, json.loads(p.read_text())) for p in BASE.glob("*/build-*/result.json")]
    recent = [(p, row) for p, row in builds if str(p.relative_to(BASE)) not in entry["historical_builds"]]
    allowance = min(limits["non_RBS_per_invocation_seconds"], limits["non_RBS_aggregate_seconds"] - seconds,
        limits["build_group_seconds"], limits["build_cumulative_seconds_cap"] - sum(row["seconds"] for _, row in builds),
        limits["build_revision_seconds"] - sum(row["seconds"] for _, row in recent),
        limits["build_revision_configuration_seconds"]["Debug"] - sum(row["seconds"] for p, row in recent if p.parent.name == "build-Debug"))
    assert allowance > 0
    assert all((p / "result.json").is_file() for p in BASE.glob("*/build-*") if p.is_dir())
    out = local(out)
    assert re.fullmatch(r"compile-check-[0-9]{2}", out.name) and out.parent == BASE
    folder = local(out / "build-Debug"); folder.mkdir(parents=True, exist_ok=False)
    focused_out = local(evidence / f"focused-check-{len(focused)+1:02d}"); focused_out.mkdir()
    source = local(ROOT / "RigidBodySimulation/src/RbsUi.cpp") if ui else local(out / "RbsValidation-case55-fixture.cpp")
    if not ui:
        fixture_source = case55_fixture_source((ROOT / "RigidBodySimulation/src/RbsValidation.cpp").read_text())
        if case59_correction:
            assert not case59_trace
            fixture_source = final_qualification_fixture_source(fixture_source)
        write(source, case59_fixture_source(fixture_source) if case59_trace else fixture_source)
    template = (BASE / "candidate-17/isolated.props").read_text()
    original = json.loads((BASE / "candidate-17/scenario-manifest.json").read_text())
    assert digest(BASE / "candidate-17/isolated.props") == original["input_hashes"]["logs/pre_editor/phase-17/candidate-17/isolated.props"]
    if case59_trace:
        assert not ui
        template = template.replace("GENGINE_LIFECYCLE_VALIDATION;", "GENGINE_LIFECYCLE_VALIDATION;GENGINE_LIFECYCLE_CASE59_TRACE;")
    if case59_correction:
        assert not ui and not case59_trace
        template = template.replace("GENGINE_LIFECYCLE_VALIDATION;", "GENGINE_LIFECYCLE_VALIDATION;GENGINE_LIFECYCLE_CASE59_FIXTURE;")
    product = "Phase17CompileCheck" + out.name[-2:]
    begin = template.index('<ItemGroup Condition=')
    end = template.index('</ItemGroup>', begin) + len('</ItemGroup>')
    # Retain every compiler definition/include/option; select one TU in the evaluated item graph.
    selected = '<ItemGroup><ClCompile Remove="@(ClCompile)"/><ClCompile Include="' + source.as_posix() + '"/></ItemGroup>'
    props = local(out / "single-file.props")
    write(props, (template[:begin] + selected + template[end:]).replace("Phase17Candidate17", product))
    common = [str(MSBUILD), "RigidBodySimulation/RigidBodySimulation.vcxproj", "/p:Configuration=Debug", "/p:Platform=x64",
        "/p:VCToolsVersion=14.44.35207", "/p:WindowsTargetPlatformVersion=10.0.26100.0",
        "/p:ForceImportBeforeCppTargets=" + str(props), "/p:LifecycleLane=diagnostic", "/p:BuildProjectReferences=false", "/nr:false", "/nologo"]
    evaluation = common + ["/getItem:ClCompile", "/getProperty:OutDir,IntDir,TargetPath,PostBuildEventUseInBuild,VCToolsVersion,WindowsTargetPlatformVersion"]
    command = common + ["/t:ClCompile", "/m:1", "/v:normal", "/bl:" + str(folder / "compile.binlog")]
    report = {"kind": "UI_TRANSLATION_UNIT_PRECOMPILE" if ui else "CASE55_TRANSLATION_UNIT_PRECOMPILE", "result": "FAIL", "recorded_utc": utc(), "configuration": "Debug",
        "authority": identity(RISK_AUTHORITY), "case59_trace": case59_trace, "case59_correction": case59_correction, "invocation": used + 1, "allowance_seconds": allowance,
        "commands": [evaluation, command], "exits": [], "acceptance_credit": False, "native_launches": 0,
        "source_identities": {name: identity(ROOT / name) for name in current["production_files"] + current["check_files"]}}
    save(folder / "start.json", report); save(focused_out / "start.json", report)
    start = time.monotonic()
    def remaining():
        value = allowance - (time.monotonic() - start)
        assert value > 0, "Focused compile time limit"
        return value
    try:
        report["prelaunch_observer_proof"] = prelaunch_observer_proof(focused_out)
        remaining()
        evaluated = bounded_build_command(evaluation, cwd=ROOT, env=environment(), capture_output=True, timeout=min(15, remaining()))
        report["exits"].append(evaluated.returncode)
        save(folder / "evaluation.json", {"exit": evaluated.returncode, "stdout": evaluated.stdout.decode(errors="replace"), "stderr": evaluated.stderr.decode(errors="replace")})
        assert evaluated.returncode == 0
        data = json.loads(evaluated.stdout)
        items = data["Items"]["ClCompile"]
        assert len(items) == 1 and Path(items[0]["Identity"]).resolve() == source.resolve(), "Unexpected compile item selection"
        report["actual_compile_item_count"] = len(items)
        for key in ("OutDir", "IntDir", "TargetPath"):
            assert product in str(local(data["Properties"][key]))
        assert data["Properties"]["PostBuildEventUseInBuild"] == "false"
        assert data["Properties"]["VCToolsVersion"] == "14.44.35207" and data["Properties"]["WindowsTargetPlatformVersion"] == "10.0.26100.0"
        assert measurement_wrapper(ROOT / "RigidBodySimulation/tests/CameraLifecycleChecks.h") == original["measurement_wrapper"]
        with (folder / "compile.log").open("xb") as stream:
            compiled = bounded_build_command(command, cwd=ROOT, env=environment(), timeout=remaining(), stdout=stream, stderr=subprocess.STDOUT)
        report["exits"].append(compiled.returncode)
        assert compiled.returncode == 0, "Case55 translation-unit compile failed"
        obj = local(data["Properties"]["IntDir"]) / (source.stem + ".obj")
        assert obj.is_file(), "No object produced"
        log = (folder / "compile.log").read_text(errors="replace")
        compiler_lines = [line for line in log.splitlines() if "CL.exe /c" in line]
        assert len(compiler_lines) == 1 and source.as_posix() in compiler_lines[0]
        assert not any(name in compiler_lines[0] for name in ("src\\RbsFrame.cpp", "src\\main.cpp", "main-lifecycle-fixture.cpp"))
        for name, expected in report["source_identities"].items():
            assert identity(ROOT / name) == expected, name
        remaining()
        report.update(result="PASS", compiled_object={"path": str(obj), **identity(obj)}, fixture={"path": source.relative_to(ROOT).as_posix(), **identity(source)},
            selection="Exactly one evaluated and compiled guarded Case55 TU; no SelectedFiles argument; original flags retained")
    except Exception as error:
        report["reason"] = repr(error)
    report["seconds"] = time.monotonic() - start
    save(folder / "result.json", report); save(focused_out / "result.json", report)
    print(json.dumps({k: report.get(k) for k in ("result", "invocation", "seconds", "exits", "reason")}), flush=True)
    if report["result"] != "PASS": raise SystemExit(1)


def focus_drawlist_cpu_probe(out):
    """Reproduce the failed visual predicate in Python's process; no application/window/GPU."""
    guards()
    current = integrated_authorization()
    evidence = local(current["evidence_root"])
    focused = sorted(evidence.glob("focused-check-*"))
    used = current["usage_at_entry"]["focused_invocations"] + len(focused)
    seconds = current["usage_at_entry"]["focused_seconds"] + sum(json.loads((p / "result.json").read_text())["seconds"] for p in focused)
    limits = current["new_execution_budget"]
    assert used < limits["non_RBS_focused_invocations"]
    authority, entry = revision_authorization()
    builds = [(p, json.loads(p.read_text())) for p in BASE.glob("*/build-*/result.json")]
    recent = [(p, row) for p, row in builds if str(p.relative_to(BASE)) not in entry["historical_builds"]]
    allowance = min(60, limits["non_RBS_aggregate_seconds"] - seconds,
        limits["build_cumulative_seconds_cap"] - sum(row["seconds"] for _, row in builds),
        limits["build_revision_seconds"] - sum(row["seconds"] for _, row in recent),
        limits["build_revision_configuration_seconds"]["Debug"] - sum(row["seconds"] for p, row in recent if p.parent.name == "build-Debug"))
    assert allowance > 0
    out = local(out); folder = local(out / "build-Debug"); folder.mkdir(parents=True, exist_ok=False)
    focused_out = local(evidence / f"focused-check-{len(focused)+1:02d}"); focused_out.mkdir()
    library = local("bin/Debug/Phase17Candidate20/GEngine/GEngine.lib")
    built = json.loads((BASE / "candidate-20/build-Debug/result.json").read_text())
    assert identity(library) == built["products"][str(library)]
    source = local(out / "drawlist-cpu.cpp")
    write(source, '''#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
extern "C" __declspec(dllexport) int Observe(double* out)
{
    auto* context = ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
    if (!io.Fonts->Build()) { ImGui::DestroyContext(context); return -1; }
    auto* shared = ImGui::GetDrawListSharedData();
    shared->Font = io.Fonts->Fonts[0]; shared->TexUvWhitePixel = io.Fonts->TexUvWhitePixel;
    shared->TexUvLines = io.Fonts->TexUvLines;
    {
        ImDrawList draw(shared); draw._ResetForNewFrame();
        draw.Flags = ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex;
        draw.PushTextureID(io.Fonts->TexID);
        draw.PushClipRect({0,0}, {1280,720}, false);
        draw.PushClipRect({381,54}, {1259,689}, false);
        draw.AddRect({381.5f,55.5f}, {1258.5f,688.5f},
            ImGui::GetColorU32(ImVec4(.26f,.59f,.98f,.45f+.35f)), 0.f, 0, 2.f);
        draw.PopClipRect();
        out[0]=draw.VtxBuffer.Size; out[1]=draw.IdxBuffer.Size;
        out[2]=draw.CmdBuffer[0].ClipRect.x; out[3]=draw.CmdBuffer[0].ClipRect.y;
        out[4]=draw.CmdBuffer[0].ClipRect.z; out[5]=draw.CmdBuffer[0].ClipRect.w;
        for (int i=0; i<draw.VtxBuffer.Size && i<16; ++i)
        {
            out[6+i*3]=draw.VtxBuffer[i].pos.x; out[7+i*3]=draw.VtxBuffer[i].pos.y;
            out[8+i*3]=(draw.VtxBuffer[i].col>>IM_COL32_A_SHIFT)&255u;
        }
    }
    ImGui::DestroyContext(context); return 0;
}
''')
    props = local(out / "cpu-probe.props")
    write(props, '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">'
        '<PropertyGroup><ConfigurationType>DynamicLibrary</ConfigurationType><TargetExt>.dll</TargetExt>'
        f'<OutDir>{out.as_posix()}/product/</OutDir><IntDir>{out.as_posix()}/intermediate/</IntDir>'
        '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>'
        '<ItemGroup><ClCompile Remove="@(ClCompile)"/>'
        f'<ClCompile Include="{source.as_posix()}"/></ItemGroup>'
        '<ItemDefinitionGroup><Link>'
        f'<AdditionalDependencies>{library.as_posix()};kernel32.lib;user32.lib;gdi32.lib;imm32.lib</AdditionalDependencies>'
        '</Link></ItemDefinitionGroup></Project>')
    command = [str(MSBUILD), "RigidBodySimulation/RigidBodySimulation.vcxproj", "/p:Configuration=Debug", "/p:Platform=x64",
        "/p:VCToolsVersion=14.44.35207", "/p:WindowsTargetPlatformVersion=10.0.26100.0",
        "/p:ForceImportBeforeCppTargets=" + str(props), "/p:BuildProjectReferences=false", "/nr:false", "/nologo"]
    report = {"result": "FAIL", "kind": "CPU_ONLY_IMGUI_DRAWLIST_REPRODUCTION", "recorded_utc": utc(),
        "invocation": used+1, "authority": identity(INTEGRATED_AUTHORITY), "native_launches": 0,
        "application_launches": 0, "acceptance_credit": False, "source": identity(source),
        "frozen_library": {"path": str(library), **identity(library)}, "commands": [], "exits": []}
    save(folder / "start.json", report); save(focused_out / "start.json", report)
    start = time.monotonic()
    try:
        evaluation = command + ["/getItem:ClCompile", "/getProperty:TargetPath,OutDir,IntDir,ConfigurationType"]
        report["commands"].append(evaluation)
        result = bounded_build_command(evaluation, cwd=ROOT, env=environment(), capture_output=True, timeout=min(15, allowance))
        report["exits"].append(result.returncode)
        save(folder / "evaluation.json", {"exit": result.returncode, "stdout": result.stdout.decode(errors="replace"), "stderr": result.stderr.decode(errors="replace")})
        assert result.returncode == 0
        data = json.loads(result.stdout); items = data["Items"]["ClCompile"]
        assert len(items) == 1 and local(items[0]["Identity"]) == source
        target = local(data["Properties"]["TargetPath"])
        assert target.is_relative_to(out) and target.suffix == ".dll" and data["Properties"]["ConfigurationType"] == "DynamicLibrary"
        args = command + ["/t:Build", "/m:1", "/v:normal"]
        report["commands"].append(args)
        with (folder / "compile.log").open("xb") as stream:
            result = bounded_build_command(args, cwd=ROOT, env=environment(), stdout=stream, stderr=subprocess.STDOUT,
                timeout=allowance-(time.monotonic()-start), creationflags=subprocess.CREATE_NO_WINDOW)
        report["exits"].append(result.returncode); assert result.returncode == 0
        module = ctypes.CDLL(str(target)); observe = module.Observe
        observe.argtypes = [ctypes.POINTER(ctypes.c_double)]; observe.restype = ctypes.c_int
        values = (ctypes.c_double*64)(); assert observe(values) == 0
        report["vertices"] = [list(values[6+i*3:9+i*3]) for i in range(int(values[0]))]
        report["counts"] = {"vertices": int(values[0]), "indices": int(values[1])}
        report["clip"] = list(values[2:6])
        report["raw_containment"] = all(380 <= x <= 1260 and 54 <= y <= 690 for x,y,a in report["vertices"])
        report["clip_containment"] = values[2] >= 380 and values[3] >= 54 and values[4] <= 1260 and values[5] <= 690
        report["binary"] = {"path": str(target), **identity(target)}
        assert report["counts"] == {"vertices": 8, "indices": 24}
        assert not report["raw_containment"] and report["clip_containment"], "Hypothesized observer premise not reproduced"
        assert time.monotonic()-start <= allowance
        report["result"] = "PASS"
    except Exception as error:
        report["reason"] = repr(error)
    report["seconds"] = time.monotonic()-start
    save(folder / "result.json", report); save(focused_out / "result.json", report)
    print(json.dumps(report), flush=True)
    if report["result"] != "PASS": raise SystemExit(1)


def stage(out, cfg):
    import postbuild
    data = verify(out)
    first_pick_mode(data, cfg)
    assert json.loads((out / f"build-{cfg}/result.json").read_text())["result"] == "PASS"
    target = local(f"bin/{cfg}/{data['product_name']}")
    assert not (target / "assets").exists()
    if reused := data.get("reuse_staged_assets"):
        assert identity(local(reused["record"])) == {k: reused[k] for k in ("sha256", "size")}
        for name, source in data["asset_members"].items():
            frozen = local(Path(reused["root"]) / name)
            assert digest(frozen) == data["input_hashes"][source]
            assert frozen.stat().st_mtime_ns == (ROOT / source).stat().st_mtime_ns
            destination = local(target / "assets" / name)
            destination.parent.mkdir(parents=True, exist_ok=True)
            os.link(frozen, destination)
    postbuild.stage_runtime_assets(ROOT, target / "assets")
    products = {}
    for item in data["expected_modules"][cfg].values():
        source = Path(item["path"])
        assert digest(source) == item["sha256"]
        destination = local(target / "RigidBodySimulation" / source.name)
        assert not destination.exists()
        shutil.copyfile(source, destination)
        products[str(destination)] = identity(destination)
    for asset, source in data["asset_members"].items():
        assert digest(target / "assets" / asset) == digest(ROOT / source)
    package = json.loads((ROOT / "tools/runtime_assets.json").read_text())
    startup = {str(target / "assets/startup" / (name + ".txt")):
        identity(target / "assets/startup" / (name + ".txt")) for name in package["startup"]}
    save(out / f"stage-{cfg}.json", {"result": "PASS", "recorded_utc": utc(), "modules": products,
        "generated_startup_descriptors": startup})
    print(f"Staged {data['product_name']} {cfg}", flush=True)


def restore_native(process, directory, report):
    # Same accepted PID-owned minimize/restore observation, reading this phase's
    # active frame stream. The imported Phase16 observer reads a different CSV.
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.IsIconic.argtypes = [wintypes.HWND]
    user.ShowWindowAsync.argtypes = [wintypes.HWND, ctypes.c_int]
    handles = []
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(handle, unused):
        pid = wintypes.DWORD()
        user.GetWindowThreadProcessId(handle, ctypes.byref(pid))
        if pid.value == process.pid and user.IsIconic(handle): handles.append(handle)
        return True
    assert user.EnumWindows(visit, 0)
    if handles:
        assert len(handles) == 1
        before = (directory / "lifecycle-frames.csv").read_bytes()
        time.sleep(.25)
        assert before == (directory / "lifecycle-frames.csv").read_bytes(), "Application rendered while minimized"
        record = {"pid": process.pid, "handle": handles[0], "minimized_observed": True,
                  "frame_rows_stable": True, "frame_stream": "lifecycle-frames.csv", "recorded_utc": utc()}
        save(directory / "native-restored.json", record)
        user.ShowWindowAsync(handles[0], 9)
        report["native_restore"] = record


def matched_control(data):
    control = BASE / "control-05"
    manifest = (verify(control, corrected_control=data) if "control_reuse" in data else
                verify(control, first_pick_control=data) if first_pick_mode(data) else verify(control))
    summary = json.loads((control / "control-summary.json").read_text())
    runtime = json.loads((control / "run-Release.json").read_text())
    assert summary["status"] == "VALID_CONTROL; CANDIDATE_COMPARISON_PENDING"
    assert summary["manifest"] == identity(control / "scenario-manifest.json")
    assert summary["build"] == identity(control / "build-Release/result.json")
    assert summary["run"] == identity(control / "run-Release.json")
    assert runtime["result"] == "PASS" and runtime["control"] and not runtime["acceptance_eligible"]
    raw = control / "runtime-Release/input-cost-editor.csv"
    assert identity(raw) == summary["raw"] == runtime["evidence"][raw.name]
    for key in ("machine", "toolchain", "asset_members", "expected_modules", "layout",
                "external_inputs", "measured_shared_inputs", "measurement_wrapper"):
        if key == "measured_shared_inputs" and "control_reuse" in data:
            delta = data["control_reuse"]["measured_delta"]
            assert set(manifest[key]) == set(data[key])
            for name, before in manifest[key].items():
                if name in delta:
                    assert delta[name] == {"baseline": before, "candidate": data[key][name]}
                else:
                    assert before == data[key][name], "Unchanged measured input drift: " + name
        else:
            assert manifest[key] == data[key], "Matched control mismatch: " + key
    assert manifest["protocol"]["input_cost"] == data["protocol"]["input_cost"]
    return control, manifest, summary, runtime


def first_pick_observation(data, out, directory, archive, report):
    """Validate passive diagnostic evidence separately from the application's gates."""
    binding = data["first_pick_diagnostic"]
    result = {"status": "INCONCLUSIVE", "acceptance_credit": False,
        "application_observed_result": report.get("observed_result", "UNAVAILABLE"),
        "application_exit": report.get("exit"), "errors": [],
        "manifest": {"path": (out / "scenario-manifest.json").relative_to(ROOT).as_posix(),
                     **identity(out / "scenario-manifest.json")},
        "binary": report["binary"], "authority": binding["authority"],
        "observer_admission": binding["observer_admission"],
        "candidate_admission": data["candidate_admission"],
        "diagnostic_cost": {"acceptance_credit": False, "purpose": "Unchanged startup stimulus only", "files": {}}}
    def require(condition, message):
        if not condition:
            raise ValueError(message)
    def integer(value):
        return isinstance(value, int) and not isinstance(value, bool)
    def invalid_constant(value):
        raise ValueError("Nonfinite diagnostic JSON constant: " + value)
    try:
        name = binding["trace_file"]
        pin = report["evidence"].get(name)
        require(pin is not None, "Required first-pick trace is missing")
        result["trace"] = {"path": (archive / name).relative_to(ROOT).as_posix(), **pin}
        require(identity(local(directory / name)) == identity(local(archive / name)) == pin,
                "First-pick trace archive identity mismatch")
        trace = json.loads((archive / name).read_text(), parse_constant=invalid_constant)
        require(isinstance(trace, dict) and trace.get("schema") == 1, "Unsupported first-pick trace schema")
        require(trace.get("guard") == "GENGINE_LIFECYCLE_PICK_TRACE", "Trace guard identity differs")
        require(trace.get("diagnostic_only") is True and trace.get("acceptance_eligible") is False,
                "Trace must explicitly exclude acceptance")
        for key in ("armed", "armed_after_cost_batches", "finished", "attach_success", "detach_success", "dump_complete"):
            result[key] = trace.get(key)
            require(trace.get(key) is True, "Incomplete trace lifecycle: " + key)
        result["finish_reason"] = trace.get("finish_reason")
        require(isinstance(trace.get("finish_reason"), str) and bool(trace["finish_reason"]), "Missing trace finish reason")
        require(trace.get("capacity") == binding["trace_capacity"], "Trace capacity differs from owner admission")
        require(integer(trace.get("record_buffer_bytes")) and 0 < trace["record_buffer_bytes"] <= binding["trace_buffer_bytes_max"],
                "Trace record storage exceeds admitted bound or lacks its size")
        require(integer(trace.get("overflow")) and trace["overflow"] == 0, "Trace overflow prevents causal observation")
        records = trace.get("records")
        require(isinstance(records, list), "Missing trace records")
        require(integer(trace.get("count")) and 0 < trace["count"] == len(records) <= trace["capacity"],
                "Trace record count/capacity mismatch")
        require(all(isinstance(row, dict) for row in records), "Malformed trace record")
        def state_complete(row):
            if not integer(row.get("result")) or not integer(row.get("capture")) or type(row.get("focused")) is not bool:
                return False
            for key in ("routing", "reported_routing"):
                value = row.get(key)
                if not isinstance(value, dict) or not integer(value.get("window")):
                    return False
                if any(type(value.get(flag)) is not bool for flag in ("finite", "visible", "focused", "uiMouse", "uiKeyboard")):
                    return False
                if any(part not in value or (value[part] is None and value["finite"]) or
                       (value[part] is not None and (type(value[part]) not in (int, float) or not math.isfinite(value[part])))
                       for part in ("left", "top", "right", "bottom")):
                    return False
            for key in ("physical", "view"):
                value = row.get(key)
                if not isinstance(value, dict) or not integer(value.get("modifiers")):
                    return False
                if any(type(value.get(flag)) is not bool for flag in ("finite", "pressed", "released", "held")):
                    return False
                if any(part not in value or (value[part] is None and value["finite"]) or
                       (value[part] is not None and (type(value[part]) not in (int, float) or not math.isfinite(value[part])))
                       for part in ("x", "y")):
                    return False
            if not all(isinstance(row.get(key), list) and len(row[key]) == 12 for key in ("u", "v", "v_finite")):
                return False
            if not all(integer(value) and value >= 0 for value in row["u"]):
                return False
            return all(type(finite) is bool and ((value is None and not finite) or
                       (type(value) in (int, float) and math.isfinite(value) and finite))
                       for value, finite in zip(row["v"], row["v_finite"]))
        require(all(state_complete(row) for row in records), "Passive routing/View/physical/payload state is incomplete")
        require([row.get("ordinal") for row in records] == list(range(1, len(records) + 1)),
                "Trace append ordinal is missing, repeated or reordered")
        require(trace.get("arm_ordinal") == 1 and trace.get("finish_ordinal") == len(records),
                "Trace arm/finish boundaries are incomplete")
        require(records[0].get("kind") == "armed" and records[-1].get("kind") == "finished",
                "Trace does not span its original arm and finish boundaries")
        require(all(all(integer(row.get(k)) for k in ("updates", "segment", "stable", "observed", "uiGeneration", "sequence"))
                    for row in records), "Trace frame/event counters are incomplete")
        log = (archive / "GEngine.log").read_text(errors="replace")
        require(log.count("PRE_EDITOR_PHASE_17_PICK_TRACE_DUMP_COMPLETE") == 1,
                "Post-close trace dump success marker is missing or repeated")
        require(log.count("PRE_EDITOR_PHASE_15_COST_COMPLETE") == 2, "Both original Release cost batches did not complete")
        expected = Counter((pair, on, sample) for pair in range(3) for on in range(2) for sample in range(240))
        for role in ("editor", "scene"):
            cost_name = f"input-cost-{role}.csv"
            cost_pin = report["evidence"].get(cost_name)
            require(cost_pin is not None, "Missing diagnostic startup cost archive: " + role)
            require(identity(local(directory / cost_name)) == identity(local(archive / cost_name)) == cost_pin,
                    "Diagnostic cost archive identity mismatch: " + role)
            with (archive / cost_name).open(newline="") as stream:
                rows = list(csv.DictReader(stream))
            require(Counter((int(row["pair"]), int(row["on"]), int(row["sample"])) for row in rows) == expected,
                    "Incomplete original startup cost sample identities: " + role)
            result["diagnostic_cost"]["files"][role] = {
                "path": (archive / cost_name).relative_to(ROOT).as_posix(), **cost_pin,
                "rows": len(rows), "acceptance_credit": False}
        kinds = Counter(row.get("kind") for row in records)
        result.update(count=len(records), capacity=trace["capacity"], overflow=trace["overflow"],
                      record_buffer_bytes=trace["record_buffer_bytes"], record_kinds=dict(kinds))
        require(trace.get("first_pick_observed") is True, "Original first pending-pick assertion was not observed")
        require(type(trace.get("first_pick_passed")) is bool, "First-pick predicate outcome is unavailable")
        for kind in ("armed", "warp", "focus", "pending_pick", "finished"):
            require(kinds[kind] == 1, "Missing or repeated first-pick hook: " + kind)
        for kind in ("post_poll", "ui_begin", "ui_region", "ui_before", "ui_after", "target", "pick_prepare", "pick_readback_enter"):
            require(kinds[kind] > 0, "Required passive observation hook is missing: " + kind)
        pending = next(row for row in records if row["kind"] == "pending_pick")
        require(pending["segment"] == 0 and pending["result"] in (0, 1), "First-pick predicate metadata differs")
        require(bool(pending["result"]) == trace["first_pick_passed"], "First-pick predicate outcome is inconsistent")
        for kind in ("post_poll", "pick_prepare", "pick_readback_enter", "ui_before", "ui_after"):
            require(any(row["kind"] == kind and row["updates"] == pending["updates"] and row["ordinal"] < pending["ordinal"]
                        for row in records), "Missing same-update first-pick observation: " + kind)
        injected = [row for row in records if row["kind"] == "injected"]
        require(len(injected) == 2 and [row.get("native_type") for row in injected] == [1025, 1026],
                "Original down/up injection markers are missing or ambiguous")
        require(all(row.get("result") == 1 and row["segment"] == 0 for row in injected),
                "Original first-pick native injection was not accepted")
        require(all(injected[0].get(key) == injected[1].get(key) for key in ("window", "button", "x", "y")),
                "Injected down/up target metadata differs")
        require(all(key in injected[0] for key in ("window", "button", "x", "y")) and injected[0]["button"] == 1,
                "Injected left-button identity is incomplete")
        layouts = trace.get("payload_layouts", {})
        require(layouts.get("routed_u") == ["kind", "window", "key", "button", "modifiers", "destination", "cancellation", "repeated", "handled", "cleanup"],
                "Routed-event scalar layout is unknown")
        require(layouts.get("routed_v") == ["x", "y", "dx", "dy", "wheelX", "wheelY"],
                "Routed-event position layout is unknown")
        routed = [row for row in records if row["kind"] == "routed"]
        require(all(isinstance(row.get("u"), list) and len(row["u"]) >= 10 and
                    isinstance(row.get("v"), list) and len(row["v"]) >= 6 for row in routed),
                "Routed-event payload is incomplete")
        matches = {}
        for event_kind, label in ((5, "down"), (6, "up")):
            matching = [row for row in routed if injected[-1]["ordinal"] < row["ordinal"] < pending["ordinal"]
                        and row["u"][0] == event_kind and row["u"][1] == injected[0]["window"]
                        and row["u"][3] == injected[0]["button"]
                        and row["v"][0] == injected[0]["x"] and row["v"][1] == injected[0]["y"]]
            result.setdefault("injected_event_match_counts", {})[label] = len(matching)
            require(len(matching) == 1, "Injected " + label + " event cannot be matched uniquely to its routed observation")
            matches[label] = {key: matching[0][key] for key in ("ordinal", "sequence", "updates", "u", "v")}
        result.update(status="COMPLETE", first_pick_passed=trace["first_pick_passed"],
            matched_routed_events=matches,
            matched_down_precedes_up=matches["down"]["ordinal"] < matches["up"]["ordinal"],
            diagnostic_outcome="NOT_REPRODUCED; STOP" if trace["first_pick_passed"] else "FIRST_PICK_FAILURE_REPRODUCED; CAUSAL_REVIEW_REQUIRED")
    except Exception as error:
        result["errors"].append(str(error))
        result["diagnostic_outcome"] = "INCONCLUSIVE; STOP"
    return result


class _PrelaunchNative(camera_fixture._Phase17Native):
    """Read-only observer rights; isolation/debug-attachment rights are unchanged."""
    def open(self, pid):
        # Identity APIs accept QUERY_LIMITED_INFORMATION; the unchanged zero-time
        # WaitForSingleObject liveness proof also requires SYNCHRONIZE.
        access = 0x100000 | 0x1000
        ctypes.set_last_error(0)
        handle = self.kernel.OpenProcess(access, False, pid)
        if not handle:
            raise camera_fixture._Phase17NativeError("OpenProcess", ctypes.get_last_error(),
                pid=int(pid), access_mask=access)
        self.handle_context[int(handle)] = {"pid": int(pid), "access_mask": access}
        return handle


def prelaunch_observer_source():
    assert digest(PRELAUNCH_OBSERVER) == "278e5194144421c10478e8d3bf0de390dea6b240ddd79c690348bf552e873b5b"
    source = json.loads(PRELAUNCH_OBSERVER.read_text())["observer_source"]
    old = "native=f._Phase17Native();k=native.kernel;u=native.user"
    new = "from test_pre_editor_camera_lifecycle import _PrelaunchNative\nnative=_PrelaunchNative();k=native.kernel;u=native.user"
    assert source.count(old) == 1
    source = source.replace(old, new)
    # Preserve observations completed before an API failure, without admitting it.
    return "try:\n" + "\n".join(" " + line for line in source.splitlines()) + '''
except Exception as error:
 print(json.dumps({'result':'FAIL','partial':{key:globals()[key] for key in ('processes','foreground') if key in globals()},'error':f._phase17_error_details(error)}))
 raise
'''


def prelaunch_observer_proof(directory):
    """Part of focused31, not a new runtime/diagnostic or a relaxed admission."""
    assert _PrelaunchNative.process is camera_fixture._Phase17Native.process
    assert _PrelaunchNative.alive is camera_fixture._Phase17Native.alive
    assert _PrelaunchNative.close is camera_fixture._Phase17Native.close
    class Kernel:
        denied = False
        waited = 258
        def OpenProcess(self, access, inherit, pid):
            assert access == 0x101000 and inherit is False and pid == 123
            ctypes.set_last_error(5 if self.denied else 0)
            return 0 if self.denied else 456
        def WaitForSingleObject(self, handle, timeout):
            assert handle == 456 and timeout == 0
            ctypes.set_last_error(6 if self.waited == 0xFFFFFFFF else 0)
            return self.waited
        def CloseHandle(self, handle):
            assert handle == 456
            return 1
    native = _PrelaunchNative.__new__(_PrelaunchNative)
    native.kernel = Kernel(); native.handle_context = {}
    handle = native.open(123)
    assert native.handle_context[456] == {"pid": 123, "access_mask": 0x101000}
    assert native.alive(handle)
    native.kernel.waited = 0
    assert not native.alive(handle)
    native.kernel.waited = 0xFFFFFFFF
    try:
        native.alive(handle)
    except camera_fixture._Phase17NativeError as error:
        assert error.record["api"] == "WaitForSingleObject" and error.record["last_error"] == 6
    else:
        raise AssertionError("Failed wait must reject liveness")
    native.close(handle); assert not native.handle_context
    native.kernel.denied = True
    try:
        native.open(123)
    except camera_fixture._Phase17NativeError as error:
        assert error.record == {"api": "OpenProcess", "last_error": 5, "pid": 123, "access_mask": 0x101000}
    else:
        raise AssertionError("Denied access must reject eligibility")
    assert not native.handle_context
    observed = mandatory_prelaunch(directory, "focused-proof")
    return {"result": "PASS", "minimum_access_mask": "0x101000", "failure_paths": ["OpenProcess denied", "wait failed", "process exited"],
        "identity_liveness_close_implementations_unchanged": True,
        "live_observation": {"path": str(directory / "prelaunch-focused-proof.json"), **identity(directory / "prelaunch-focused-proof.json")},
        "foreground": observed["state"]["foreground"], "historical_pid22720_identity": "Not recorded historically; no attribution"}


def mandatory_prelaunch(directory, label):
    """Owner-required read-only admission, inside the existing process allowance."""
    source = prelaunch_observer_source()
    observed = subprocess.run([sys.executable, "-B", "-c", source], cwd=ROOT,
        env=environment(), capture_output=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
    report = {"recorded_utc": utc(), "exit": observed.returncode,
        "observer": identity(PRELAUNCH_OBSERVER), "observer_correction": identity(PRELAUNCH_CORRECTION),
        "observer_source_sha256": hashlib.sha256(source.encode()).hexdigest(), "process_access_mask": "0x101000",
        "stdout": observed.stdout.decode(errors="replace"),
        "stderr": observed.stderr.decode(errors="replace"), "result": "FAIL"}
    try:
        assert observed.returncode == 0, "Native desktop inventory failed"
        state = json.loads(observed.stdout)
        report["state"] = state
        assert state["processes"] == [], "Huabao or another RBS is present; launch prohibited"
        assert state["input_desktop"].casefold() == "default" and state["foreground"]["hwnd"] and state["foreground"]["thread"], "Interactive foreground unavailable"
        report["result"] = "PASS"
    finally:
        save(directory / ("prelaunch-" + label + ".json"), report)
    return report


@contextmanager
def absence_only_lease(directory, seconds):
    # Reject the existing helper's nonempty branch before any process attachment.
    # Its tested empty-inventory/watchdog implementation remains byte-identical.
    original = camera_fixture._Phase17Native.windows
    def absent(native, *args, **kwargs):
        inventory = original(native, *args, **kwargs)
        assert not inventory, "Desktop target reappeared; no suspension fallback authorized"
        return inventory
    camera_fixture._Phase17Native.windows = absent
    try:
        with camera_fixture.phase17_desktop_isolation(directory, seconds, authorization=ISOLATION_AMENDMENT) as lease:
            assert lease.request["lease_mode"] == "ABSENCE_MONITORED" and lease.request["targets"] == []
            yield lease
    finally:
        camera_fixture._Phase17Native.windows = original


def run(out, cfg):
    data = verify(out)
    first_pick = first_pick_mode(data, cfg)
    case59 = case59_mode(data, cfg)
    authority, revision_entry = revision_authorization()
    limits = authority["new_execution_budget"]
    assert not data["control"] or (data["candidate"] == "05" and cfg == "Release" and "frozen_control" in data)
    assert not data.get("diagnostic_only") or first_pick or case59 or (cfg == "Debug" and not FIRST_PICK_AUTHORITY.exists())
    assert data["protocol"]["runtime_limit_amendment"] == {
        "path": LIMIT_AMENDMENT.relative_to(ROOT).as_posix(), **identity(LIMIT_AMENDMENT)}
    # Debug qualifies first; Release build/runtime remain conditional on that PASS.
    assert json.loads((out / f"build-{cfg}/result.json").read_text())["result"] == "PASS"
    assert json.loads((out / f"stage-{cfg}.json").read_text())["result"] == "PASS"
    starts = list(BASE.glob("*/run-*-start.json"))
    new_starts = revision_usage("*/run-*-start.json", revision_entry["historical_starts"])
    assert len(new_starts) < limits["native_additional_launches"], "Revision launch cap reached"
    if data["control"]:
        assert not any(p.parent.name.startswith("control-") for p, _ in new_starts), "One fresh matched control launch only"
    if data.get("diagnostic_only"):
        assert sum(bool(row.get("diagnostic_only")) for _, row in new_starts) < limits["diagnostic_additional_launches"]
        assert sum(bool(json.loads(p.read_text()).get("diagnostic_only")) for p in starts) < limits["diagnostic_cumulative_launch_cap"]
    if case59:
        assert not any(row.get("diagnostic_kind") == CASE59_KIND for _, row in new_starts), "Case59 diagnostic allocation already used"
    if first_pick:
        assert not any(row.get("diagnostic_kind") == FIRST_PICK_KIND for _, row in new_starts), "The single first-pick diagnostic attempt was already charged"
        built = json.loads((out / "build-Release/result.json").read_text())
        assert built["fresh_full_closure"] and built["exits"] == [0, 0, 0, 0]
        assert not built.get("reused_build") and len(built["commands"]) == 4
        assert built["consumed_dependency_check"]["result"] == "PASS"
        assert not (out / "run-Debug.json").exists() and not (out / "audit-Debug.json").exists()
    if cfg == "Release" and not data["control"]:
        if not first_pick:
            require_debug_qualification(out, data)
        matched_control(data)  # Fail before a timed candidate batch if the fresh control is not comparable.
    assert not (out / f"run-{cfg}.json").exists() and not (out / f"run-{cfg}-start.json").exists()
    prior_seconds = sum(json.loads(p.read_text())["seconds"] for p in BASE.glob("*/run-*.json") if not p.name.endswith("-start.json"))
    new_runs = [(p, row) for p, row in revision_usage("*/run-*.json", revision_entry["historical_runs"]) if not p.name.endswith("-start.json")]
    allowance = min(limits["process_seconds"], limits["native_cumulative_seconds_cap"] - prior_seconds,
                    limits["native_revision_seconds"] - sum(row["seconds"] for _, row in new_runs))
    assert len(starts) < data["protocol"]["limits"]["total_launches"] and allowance > 0, "Accepted launch/aggregate limit reached"
    assert all(p.with_name(p.name.replace("-start", "")).exists() for p in starts), "Unfinished runtime attempt"
    staging = json.loads((out / f"stage-{cfg}.json").read_text())
    for path, expected in {**staging["modules"], **staging["generated_startup_descriptors"]}.items():
        assert identity(path) == expected, path
    assets = local(f"bin/{cfg}/{data['product_name']}/assets")
    expected_assets = set(data["asset_members"]) | {
        Path(path).relative_to(assets).as_posix() for path in staging["generated_startup_descriptors"]}
    assert {p.relative_to(assets).as_posix() for p in assets.rglob("*") if p.is_file()} == expected_assets
    for name, source in data["asset_members"].items():
        assert digest(local(assets / name)) == data["input_hashes"][source], name
    directory = local(f"runtime/RigidBodySimulation/{cfg}/phase-17-{data['product_name']}")
    directory.mkdir(parents=True, exist_ok=False)
    write(directory / "imgui.ini", data["layout"])
    executable = local(f"bin/{cfg}/{data['product_name']}/RigidBodySimulation/RigidBodySimulation.exe")
    built = json.loads((out / f"build-{cfg}/result.json").read_text())
    assert identity(executable) == built["products"][str(executable)], "Qualifying product drift"
    report = {"result": "INCONCLUSIVE", "recorded_utc": utc(), "command": [str(executable)],
        "cwd": str(directory), "binary": identity(executable), "diagnostic_only": data["diagnostic_only"],
        "acceptance_eligible": not data["diagnostic_only"] and not data["control"],
        "control": data["control"], "amendment": identity(LIMIT_AMENDMENT), "native_poll_authority": identity(NATIVE_POLL_AUTHORITY), "case55_authority": identity(CASE55_AUTHORITY),
        "isolation_authorization": data["isolation_authorization"]}
    if first_pick:
        report.update(diagnostic_kind=FIRST_PICK_KIND, first_pick_diagnostic=data["first_pick_diagnostic"],
                      performance_acceptance_credit=False)
    if case59:
        report.update(diagnostic_kind=CASE59_KIND, case59_observer=data["case59_observer"], performance_acceptance_credit=False)
    start = time.monotonic()
    process = None
    lease = None
    close_at = start + allowance - min(30, allowance)
    try:
        mandatory_prelaunch(directory, "before-lease")
        with absence_only_lease(directory, allowance - (time.monotonic() - start)) as lease:
            lease.assert_healthy()
            with (directory / "process-output.log").open("xb") as stream:
                try:
                    assert time.monotonic() < close_at, "Isolation setup exhausted runtime allowance before reserved close"
                    mandatory_prelaunch(directory, "before-process")
                    lease.assert_healthy()
                    save(out / f"run-{cfg}-start.json", {"recorded_utc": utc(), "limit_seconds": allowance,
                        "command": [str(executable)], "cwd": str(directory), "diagnostic_only": data["diagnostic_only"],
                        "diagnostic_kind": data.get("diagnostic_kind"),
                        "control": data["control"],
                        "isolation_verified_before_launch": True, "amendment": identity(LIMIT_AMENDMENT), "native_poll_authority": identity(NATIVE_POLL_AUTHORITY)})
                    process = subprocess.Popen([str(executable)], cwd=directory, env=environment(), stdout=stream,
                        stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
                    report["pid"] = process.pid
                    lease.bind_runtime(process)
                    while process.poll() is None:
                        lease.assert_healthy()
                        if time.monotonic() >= close_at:
                            raise TimeoutError("Accepted runtime limit; reserved close interval")
                        if (directory / "native-minimize.request").exists() and "native_restore" not in report:
                            restore_native(process, directory, report)
                        time.sleep(.05)
                    report["exit"] = process.returncode
                    log = (directory / "GEngine.log").read_text(errors="replace")
                    marker = "PRE_EDITOR_PHASE_17_CONTROL_PASS" if data["control"] else "PRE_EDITOR_PHASE_17_PASS"
                    report["observed_result"] = "PASS" if process.returncode == 0 and marker in log else "FAIL"
                    lease.assert_healthy()
                finally:
                    # Only our own RBS may be stopped. External targets are never terminated.
                    if process is not None and process.poll() is None:
                        process.kill()
                        process.wait(timeout=min(30, max(0, allowance - (time.monotonic() - start))))
        report["isolation"] = lease.report
        assert report["isolation"]["result"] == "PASS", "Temporary isolation/restoration failed"
        verify(out)
        assert time.monotonic() - start <= allowance, "Accepted runtime limit including restoration"
        report["result"] = report.get("observed_result", "FAIL")
        if data["diagnostic_only"] and report["result"] == "PASS":
            report["result"] = "DIAGNOSTIC_COMPLETE"
    except Exception as error:
        report["result"] = "FAIL"
        report["reason"] = str(error)
        if lease is not None:
            report["isolation"] = lease.report
    report["seconds"] = time.monotonic() - start
    archive = local(out / ("runtime-" + cfg))
    archive.mkdir()
    for p in directory.rglob("*"):
        if p.is_file():
            target = local(archive / p.relative_to(directory))
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(local(p), target)
    report["evidence"] = {p.relative_to(archive).as_posix(): identity(p) for p in archive.rglob("*") if p.is_file()}
    oracle_name = "lifecycle-oracle-failure.json"
    archived_log = archive / "GEngine.log"
    oracle_failed = archived_log.exists() and "Projection/ray/interior/visibility oracle" in archived_log.read_text(errors="replace")
    oracle_identity = report["evidence"].get(oracle_name)
    observation = {"required": oracle_failed, "present": oracle_identity is not None,
                   "archived_identity_matches": False}
    if oracle_identity is not None:
        report["oracle_failure"] = {"path": (archive / oracle_name).relative_to(ROOT).as_posix(), **oracle_identity}
        try:
            observation["archived_identity_matches"] = identity(directory / oracle_name) == oracle_identity
        except Exception as error:
            observation["error"] = str(error)
        if not observation["archived_identity_matches"]:
            report["result"] = "FAIL"
    if oracle_failed:
        report["result"] = "FAIL"
        if not observation["present"]:
            observation["error"] = "Required failure-frame JSON is missing; original oracle failure remains FAIL"
    report["oracle_failure_observability"] = observation
    if first_pick:
        report["first_pick_observation"] = first_pick_observation(data, out, directory, archive, report)
        if report["first_pick_observation"]["status"] != "COMPLETE" and report["result"] == "DIAGNOSTIC_COMPLETE":
            report["result"] = "DIAGNOSTIC_INCONCLUSIVE"
    save(out / f"run-{cfg}.json", report)
    print(json.dumps({k: report.get(k) for k in ("result", "seconds", "exit", "reason")}), flush=True)
    if report["result"] not in ("PASS", "DIAGNOSTIC_COMPLETE"):
        raise SystemExit(1)


def audit_control(out, cfg, data):
    assert data["candidate"] == "05" and cfg == "Release" and not data["diagnostic_only"]
    built = json.loads((out / "build-Release/result.json").read_text())
    assert built["result"] == "PASS" and built["fresh_full_closure"] and built["exits"] == [0, 0, 0, 0]
    runtime = json.loads((out / "run-Release.json").read_text())
    assert runtime["result"] == "PASS" and runtime["control"] and not runtime["acceptance_eligible"]
    archive = out / "runtime-Release"
    for name, expected in runtime["evidence"].items():
        assert identity(archive / name) == expected
    assert "PRE_EDITOR_PHASE_17_CONTROL_PASS" in (archive / "GEngine.log").read_text()
    raw = archive / "input-cost-editor.csv"
    rows = list(csv.DictReader(raw.open()))
    assert Counter((int(r["pair"]), int(r["on"]), int(r["sample"])) for r in rows) == Counter(
        (pair, on, sample) for pair in range(3) for on in range(2) for sample in range(240))
    assert all(int(r["deliveries"]) == 64 and int(r["native_remaining"]) == 0 and int(r["engine_backlog"]) == 0 for r in rows)
    groups = []
    for pair in range(3):
        for on in range(2):
            lane = [r for r in rows if int(r["pair"]) == pair and int(r["on"]) == on]
            values = sorted(float(r["batch_ms"]) for r in lane)
            latencies = [float(r["max_latency_ms"]) for r in lane]
            assert all(math.isfinite(v) and v >= 0 for v in values + latencies)
            if on:
                assert all(len({r[k] for r in lane}) == 1 for k in
                    ("allocations", "requested_bytes", "logical_value_copies", "logical_copy_bytes"))
            assert max(latencies) <= 16.667
            groups.append({"pair": pair, "on": on, "median_ms": statistics.median(values),
                "p95_ms": values[math.ceil(.95 * len(values)) - 1], "max_latency_ms": max(latencies)})
    save(out / "control-summary.json", {"phase": 17, "recorded_utc": utc(),
        "status": "VALID_CONTROL; CANDIDATE_COMPARISON_PENDING", "run": identity(out / "run-Release.json"),
        "raw": identity(raw), "rows": len(rows), "groups": groups, "runtime_launches": 1,
        "source": "Fresh Control05; frozen approved Phase16 sources with current pinned common dependencies",
        "manifest": identity(out / "scenario-manifest.json"), "build": identity(out / "build-Release/result.json"),
        "observer": "OFF optional counters unmeasured; ON-minus-OFF reported without subtraction; no display latency claim"})
    save(out / "audit-Release.json", {"result": "PASS", "kind": "MATCHED_CONTROL_ONLY",
        "recorded_utc": utc(), "acceptance_eligible": False, "control_summary": identity(out / "control-summary.json")})
    print("Fresh Control05 input-cost evidence PASS; candidate comparison pending", flush=True)


REQUIRED_LIFECYCLE_CHECKS = {'transient_live': 1, 'transient_no_authored_state': 1, 'transient_retirement': 1, 'transient_slot_reuse_old_generation_rejected': 1, 'transient_storage_reuses_one_live_slot': 8, 'explicit_active_A': 1, 'copy_remaps_active_uuid_excludes_transient': 1, 'foreign_selection_preserves_active': 1, 'sampled_rigid_parent_no_physics_tick': 1, 'parent_snapshot_matrix_coordinate_tolerances': 1, 'scale_shear_reflection_singular_rejected': 3, 'projection_pose_edit_retains_old_snapshot_frame': 1, 'empty_viewport_typed': 1, 'invalid_replacement_preserves_old': 1, 'missing_camera_preserves_active': 1, 'missing_transform_preserves_active': 1, 'nonfinite_pose_preserves_active': 1, 'extraction_rejects_lifecycle_mutation': 1, 'replacement_new_identity_old_stale_retained_frame': 1, 'authored_slot_reuse_old_generation_rejected': 1, 'inactive_delete_preserves_active': 1, 'active_delete_clears_selection': 1, 'editor_wheel': 1, 'editor_real_fly_capture': 1, 'switch_cancels_capture_and_damping': 1, 'scene_preview_controls_preserve_authored_and_editor': 2, 'return_requires_fresh_press': 1, 'fresh_press_captures': 1, 'editor_real_orbit_pan': 2, 'focus_hide_minimize_cancel': 3, 'rebind_retires_old_view_clears_references': 1, 'rebind_back_fresh_identity': 1, 'preview_replacement_restores_previous_exact_role': 1, 'active_replacement_old_stale': 1, 'retired_preview_restores_saved_editor': 1, 'invalid_projection_unavailable_no_stale_snapshot': 1, 'restored_projection_fresh_snapshot': 1, 'editor_viewpoint_shortcut_fixed_front_pose': 1, 'switch_fixture_has_angular_damping': 1, 'switch_preserves_effective_editor_pose': 1, 'orbit_switch_fixture_has_angular_damping': 1, 'orbit_pan_switch_preserves_effective_pose': 2, 'orbit_pan_idle_return_preserves_effective_pose': 2, 'transient_render_component_copy_excluded': 1, 'transient_authored_components_do_not_promote_identity': 1, 'transient_authored_components_copy_excluded': 1}


REQUIRED_LIFECYCLE_CHECKS["transient_camera_value_invalid_but_retirable"] = 1


def audit(out, cfg):
    data = verify(out)
    if data["control"]:
        return audit_control(out, cfg, data)
    assert not data.get("diagnostic_only"), "Diagnostic success cannot supply acceptance evidence"
    build_record = json.loads((out / f"build-{cfg}/result.json").read_text())
    assert build_record["result"] == "PASS" and build_record["revision_full_library_closure"]
    runtime = json.loads((out / f"run-{cfg}.json").read_text())
    assert runtime["result"] == "PASS"
    archive = out / ("runtime-" + cfg)
    for name, expected in runtime["evidence"].items():
        assert identity(archive / name) == expected
    report = {"result": "FAIL", "recorded_utc": utc(), "runtime": identity(out / f"run-{cfg}.json"),
              "configuration": cfg, "coverage": [], "input_cost": []}
    try:
        frames = list(csv.DictReader((archive / "lifecycle-frames.csv").open()))
        checks = list(csv.DictReader((archive / "lifecycle-checks.csv").open()))
        assert len(frames) == 96 and len({(r["segment"], r["frame"]) for r in frames}) == 96
        assert all(r["pass"] == "1" for r in checks)
        assert Counter(r["case"] for r in checks) == Counter(REQUIRED_LIFECYCLE_CHECKS), "Missing, duplicate or unexpected lifecycle check coverage"
        log = (archive / "GEngine.log").read_text()
        assert "PRE_EDITOR_PHASE_17_TEARDOWN_PASS" in log
        if "native_poll_authority" in data:
            gestures = list(csv.DictReader((archive / "viewport-gestures.csv").open()))
            assert len(gestures) == 65 and [int(r["step"]) for r in gestures] == list(range(65))
            assert all(r["pass"] == "1" for r in gestures)
            assert sum(r["case"] == "held_alt_consecutive_orbit" for r in gestures) == 20
            assert all(int(r["reads"]) == (1 if int(r["step"]) in (2,33,55,62) else 0) for r in gestures)
            assert all(marker in log for marker in ("PRE_EDITOR_PHASE_15_STATE_PASS", "PRE_EDITOR_PHASE_17_NATIVE_POLL_PASS", "PRE_EDITOR_PHASE_17_GESTURES_PASS"))
            report["gesture_regressions"] = {"cases": 65, "held_alt_gestures": 20, "raw": identity(archive / "viewport-gestures.csv"), "native_and_direct_provider": "PASS"}
        if "case55_authority" in data:
            premise = list(csv.DictReader((archive / "case55-premise.csv").open()))
            assert len(premise) == 3 and [int(r["tick"]) for r in premise] == [1,2,3]
            assert all(r[k] == "1" for r in premise for k in ("pose","viewport","fixed","ordering","native_visible","pass"))
            assert [r["injected"] for r in premise] == ["1","0","0"]
            assert [int(r["update"]) for r in premise] == list(range(int(premise[0]["update"]),int(premise[0]["update"])+3))
            report["case55_premise"] = {"result":"PASS", "raw":identity(archive / "case55-premise.csv"),
                "meaning":"Original next-frame target premise verified through click consumption; no historical causal attribution"}
        if "case59_correction" in data:
            premise = list(csv.DictReader((archive / "case59-premise.csv").open()))
            assert 3 <= len(premise) <= 10 and all(r["overflow"] == "0" and r["setup_clicks"] == "1" for r in premise)
            assert premise[0]["kind"] == "0" and premise[0]["result"] == "1"
            assert all(r["kind"] == "1" and r["result"] == "0" for r in premise[1:-2])
            assert premise[-2]["kind"] == "1" and premise[-1]["kind"] == "2"
            assert all(r["result"] == r["text_active"] == "1" and r["active_id"] != "0" and
                       r["alt"] == r["down"] == r["queued"] == "0" for r in premise[-2:])
            lifetime = json.loads((archive / "focus-lifetime.json").read_text())
            assert lifetime["begun"] == lifetime["complete"] == 1 and lifetime["frames"] > 1
            assert lifetime["wall_seconds"] >= 5 and lifetime["activation_age"] >= 5
            report["case59_premise"] = {"result":"PASS", "raw":identity(archive / "case59-premise.csv"),
                "setup_clicks":1, "original_center_click_and_oracle":"PASS"}
            report["focus_lifetime"] = {"result":"PASS", "raw":identity(archive / "focus-lifetime.json"), **lifetime}
        if "integrated_authority" in data:
            visual = list(csv.DictReader((archive / "viewport-focus-visual.csv").open()))
            assert visual and len(visual) <= 2048
            assert all(r["pass"] == "1" and r["overflow"] == "0" for r in visual)
            if data["protocol"]["focus_visual"].get("storage"):
                coverage = json.loads((archive / "viewport-focus-coverage.json").read_text())
                n = coverage["total"]
                assert coverage["schema"] == 1 and coverage["capacity"] == 2048 and n > 0
                assert coverage["stored"] == len(visual) == min(n, 2048)
                assert coverage["compacted"] == n-len(visual)
                assert [int(r["ordinal"]) for r in visual] == list(range(1, min(n, 2047)+1)) + ([n] if n>2047 else [])
                assert coverage["passed"] == n and coverage["failed"] == coverage["failure_bits"] == 0
                assert coverage["first_failure_ordinal"] == coverage["overflow"] == 0
                assert not (archive / "viewport-focus-first-failure.csv").exists()
                assert all(r["failures"] == "0" for r in visual)
                assert all(float(r["border"]) == 1 and int(r["alpha"]) == 255 for r in visual if r["drawable"] == "1")
                assert 0 < coverage["opaque_frames"] == coverage["drawable_frames"] <= n
                assert 0 < coverage["active_frames"] <= coverage["drawable_frames"]
                assert all(0 < coverage[key] <= n for key in ("flashes", "persistent", "after_one_second", "after_five_seconds", "inactive_frames"))
                assert coverage["gesture_mask"] == (1<<20)-1 and coverage["multiple_activations"] == 1
                assert 0 <= int(coverage["field_digest_fnv1a64"]) < 1<<64
                report["focus_visual"] = {"result":"PASS", "raw":identity(archive / "viewport-focus-visual.csv"),
                    "coverage":identity(archive / "viewport-focus-coverage.json"), "frames":n,
                    "retained_rows":len(visual), "active_frames":coverage["active_frames"],
                    "persistent_after_250ms":True, "actual_vertices_alpha_clip_checked":True,
                    "held_alt_twenty_cases":True, "additional_visual_observation_frames":report["focus_lifetime"]["frames"],
                    "input_cost_observer_changed":False, "unchecked_frames":0}
            else:
                active_visual = [r for r in visual if r["active"] == r["drawable"] == "1"]
                assert active_visual and any(float(r["flash"]) > 0 for r in active_visual)
                assert any(float(r["time"])-float(r["start"]) >= .25 and float(r["border"]) > 0 and float(r["flash"]) == 0 for r in active_visual)
                if data["protocol"]["focus_visual"].get("version") == 2:
                    assert all(float(r["border"]) == 1 and int(r["alpha"]) == 255 and r["failures"] == "0"
                               for r in visual if r["drawable"] == "1")
                    assert any(r["active"] == "0" and r["drawable"] == "1" and float(r["flash"]) == 0 for r in visual)
                else:
                    assert any(r["active"] == "0" and float(r["border"]) == 0 for r in visual)
                assert {int(r["step"]) for r in active_visual} >= set(range(4, 24))
                assert len({int(r["activations"]) for r in active_visual}) > 1
                if "case59_correction" in data:
                    assert any(float(r["time"])-float(r["start"]) >= 5 and r["flash"] == "0" for r in active_visual)
                report["focus_visual"] = {"result": "PASS", "raw": identity(archive / "viewport-focus-visual.csv"),
                    "frames": len(visual), "active_frames": len(active_visual), "persistent_after_250ms": True,
                    "actual_vertices_alpha_clip_checked": True, "held_alt_twenty_cases": True,
                    "additional_visual_observation_frames": report.get("focus_lifetime", {}).get("frames", 0),
                    "input_cost_observer_changed": False}
        for segment in range(12):
            selected = [r for r in frames if int(r["segment"]) == segment]
            assert len(selected) == 8 and all(r["background"] == "-1" for r in selected)
            assert all(int(r["source"]) >= 0 and int(r["regenerated"]) >= 0 for r in selected)
            assert len({(r["width"], r["height"], r["scale"], r["identity_index"],
                         r["identity_generation"], r["identity_domain"]) for r in selected}) == 1
        report["coverage"] = {"segments": 12, "observed_frames": 96, "readbacks": 288,
            "lifecycle_checks": len(checks), "native_restore": runtime["native_restore"],
            "ui": "unchanged 42-step Shadows/menu/popup/widget/Unicode test; zero View/Game/picking leaks",
            "manual_remaining": "Owner RBS visual acceptance and physical OS DPI transition"}
        if cfg == "Release":
            control, control_manifest, control_summary, control_run = matched_control(data)
            raw_control = control / "runtime-Release/input-cost-editor.csv"
            unchanged = "RigidBodySimulation/tests/InputRoutingChecks.h"
            assert control_manifest["input_hashes"][unchanged] == data["input_hashes"][unchanged]
            report["matched_control"] = {"path": str(control), "manifest": identity(control / "scenario-manifest.json"),
                "summary": identity(control / "control-summary.json"), "run": identity(control / "run-Release.json")}
            baseline = list(csv.DictReader(raw_control.open()))
            for role in ("editor", "scene"):
                raw = archive / f"input-cost-{role}.csv"
                rows = list(csv.DictReader(raw.open()))
                assert Counter((int(r["pair"]), int(r["on"]), int(r["sample"])) for r in rows) == Counter(
                    (pair, on, sample) for pair in range(3) for on in range(2) for sample in range(240))
                assert all(int(r["deliveries"]) == 64 and int(r["native_remaining"]) == 0 and
                           int(r["engine_backlog"]) == 0 for r in rows)
                groups = []
                for pair in range(3):
                    for on in range(2):
                        lane = [r for r in rows if int(r["pair"]) == pair and int(r["on"]) == on]
                        original = [r for r in baseline if int(r["pair"]) == pair and int(r["on"]) == on]
                        assert len(lane) == len(original) == 240
                        values = sorted(float(r["batch_ms"]) for r in lane)
                        old_values = sorted(float(r["batch_ms"]) for r in original)
                        assert all(math.isfinite(v) and v >= 0 for v in values)
                        median = statistics.median(values); p95 = values[math.ceil(.95*len(values))-1]
                        delta_median = median-statistics.median(old_values)
                        delta_p95 = p95-old_values[math.ceil(.95*len(old_values))-1]
                        latencies = [float(r["max_latency_ms"]) for r in lane]
                        assert all(math.isfinite(v) and v >= 0 for v in latencies)
                        latency = max(latencies)
                        passed = delta_median <= .100 and delta_p95 <= .250 and latency <= 16.667
                        metrics = {key: {"min": min(int(r[key]) for r in lane), "max": max(int(r[key]) for r in lane)}
                            for key in lane[0] if key not in ("pair", "on", "sample", "batch_ms", "max_latency_ms")}
                        groups.append({"pair": pair, "on": on, "median_ms": median, "p95_ms": p95,
                            "delta_median_ms": delta_median, "delta_p95_ms": delta_p95,
                            "max_latency_ms": latency, "counters": metrics, "pass": passed})
                        # The existing measurement function bounds storage and verifies
                        # one complete empty-drain state every iteration. ON counters
                        # additionally must not grow across this identical workload.
                        if on:
                            assert all(metrics[k]["min"] == metrics[k]["max"] for k in
                                ("allocations", "requested_bytes", "logical_value_copies", "logical_copy_bytes"))
                report["input_cost"].append({"role": role, "raw": identity(raw), "groups": groups,
                    "observer_on_minus_off_median_ms": [groups[2*p+1]["median_ms"]-groups[2*p]["median_ms"] for p in range(3)],
                    "observer_note": "OFF optional counters unmeasured; no observer subtraction for PASS; no display latency claim"})
            assert all(g["pass"] for role in report["input_cost"] for g in role["groups"]), "Input cost/latency gate"
        report["result"] = "PASS"
    except Exception as error:
        report["reason"] = str(error)
    save(out / f"audit-{cfg}.json", report)
    print(json.dumps({key: report.get(key) for key in ("result", "configuration", "reason")}))
    if report["result"] != "PASS": raise SystemExit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("operation", choices=["prepare", "verify", "build", "stage", "run", "audit"])
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--control", action="store_true")
    parser.add_argument("--diagnostic-only", action="store_true")
    parser.add_argument("--diagnostic-kind", choices=[FIRST_PICK_KIND, CASE59_KIND])
    parser.add_argument("--reuse-control-build")
    parser.add_argument("--reuse-candidate-build")
    parser.add_argument("--reuse-staged-assets")
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Release")
    args = parser.parse_args()
    assert re.fullmatch(r"[0-9]{2}", args.candidate)
    out = local(BASE / (("control-" if args.control else "candidate-") + args.candidate))
    if args.operation == "prepare":
        prepare(out, args.candidate, args.control, args.reuse_control_build, args.reuse_candidate_build,
                args.diagnostic_only, args.reuse_staged_assets, args.diagnostic_kind)
    elif args.operation == "verify":
        verify(out)
        print("Inputs match", flush=True)
    else:
        globals()[args.operation](out, args.configuration)
