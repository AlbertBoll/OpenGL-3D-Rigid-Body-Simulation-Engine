"""Phase 28 single-step build, run and audit commands. No implicit campaign or retry."""
from __future__ import annotations

import argparse
import csv
from collections import Counter
import datetime as dt
import hashlib
import json
import math
import os
import shutil
import statistics
import struct
import subprocess
import time
from pathlib import Path
from xml.sax.saxutils import escape

import postbuild as postbuild_tools


ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "logs/pre_editor/phase-28"
MATRIX = BASE / "design-02/acceptance-matrix.json"
MATRIX_SHA256 = "bc91dde2019284820bd80c209fe1bae8c58ccdf83a4ed595ca28430dfaeae2c5"
BUDGET_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_28_RUN_BUDGET_ACCEPTANCE_01.json"
BUDGET_ACCEPTANCE_SHA256 = "d840a099c4503f6dd53f80f8365d6ea4919415c63c3952faaabbb819b338950e"
BUDGET_REVIEW = ROOT / "docs/pre_editor/active/PHASE_28_RUN_BUDGET_REVIEW_01.md"
BUDGET_REVIEW_SHA256 = "9cd9d4a0c48f9d8e9cedcaae1125604fac80ff7b5aadc2c24d726410c301e980"
BUDGET_PROGRESS = BASE / "budget-review-01/progress.json"
BUDGET_PROGRESS_SHA256 = "aaddcc4690cc56b803a98d41800bd6b32bbd7b3a2a43530f4f104728fc873eaf"
BUDGET_ACCEPTANCE_02 = ROOT / "docs/pre_editor/active/PHASE_28_RUN_BUDGET_ACCEPTANCE_02.json"
BUDGET_ACCEPTANCE_02_SHA256 = "a9d7baec821f5673dd97667a379bc5da327003b8e8b3cd51df35f65d585d39ba"
BUDGET_REVIEW_02 = ROOT / "docs/pre_editor/active/PHASE_28_RUN_BUDGET_REVIEW_02.md"
BUDGET_REVIEW_02_SHA256 = "d5cddc8fcce94db1216c6180ed7b02b9d5f1b704e611561a821c397e54b6a067"
BUDGET_PROGRESS_02 = BASE / "budget-review-02/progress.json"
BUDGET_PROGRESS_02_SHA256 = "261b1b1f4bead902225822ec1d42f493cb463d8e37926bfa74dc19e6afc214c9"
CANDIDATE_10_ARCHIVE = BASE / "candidate-10/source-archive.json"
CANDIDATE_10_ARCHIVE_SHA256 = "8d8dc2548dc3b5ca70b88f59f9190269455b4f9461c74b1da98da4da205654ae"
CANDIDATE_10_HEADER_SHA256 = "a40b1474e0cb2d40ea7364de84da5801df1f80d1592ac59066266d8652a85920"
CANDIDATE_10_DEBUG_EXE_SHA256 = "1552a9ad183a48a068f50821e52157ce33c326541770b860ad634a70a04bb52a"
CAMPAIGN = ROOT / "docs/pre_editor/active/PHASE_28_AUTONOMOUS_CAMPAIGN_01.json"
CAMPAIGN_SHA256 = "42be9d1ed33aa9ea6b20ee213c942e29cad9d5366df415f9ec0c25f88924943e"
EDITOR_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_28_JOLT_EDITOR_COMPLETION_ACCEPTANCE_01.json"
EDITOR_ACCEPTANCE_SHA256 = "bb09b19865762ac0428edde811bc8b157e8362ee5a27a9a9e98193952b1d0a3e"
EFFECTIVE_PATHS = BASE / "correction-25/effective-paths.json"
EFFECTIVE_PATHS_SHA256 = "2f2f6c66ee01b1b05cf6dceabf34b36fda4f5fc25101d94cdf7e553ad11da7c4"
MEMORY_SCOPE_REVIEW = ROOT / "docs/pre_editor/active/PHASE_28_BUILD_MEMORY_SCOPE_REVIEW_01.md"
MEMORY_SCOPE_REVIEW_SHA256 = "bec7f36def4846f38a6e140848c3ce0917da19c276a4d66c0a88810878e7d465"
MEMORY_SCOPE_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_28_BUILD_MEMORY_SCOPE_ACCEPTANCE_01.json"
MEMORY_SCOPE_ACCEPTANCE_SHA256 = "55af22efe2422c18e6490015b04ac1cc96ddd228fa1cf70cd479e337e0f0f342"
EFFECTIVE_PATHS_02 = BASE / "correction-38/effective-paths-02.json"
EFFECTIVE_PATHS_02_SHA256 = "fa5b0e378fd166a5ce9eb5ce102baca51759d34cae13702edc6d4fb84fefd3f8"
QUALIFICATION_PLAN = BASE / "qualification-plan-01.json"
QUALIFICATION_PLAN_SHA256 = "e4958f244a7c1f2126c2ffe720b1aa80f624e962d6ba4caef6975a3c050e5a07"
QUALIFICATION_PLAN_02 = BASE / "qualification-plan-02.json"
QUALIFICATION_PLAN_02_SHA256 = "6c1e776649ae27d5f396634bd4a430ac329511bf50ad0681e4e3016ce64b2333"
RUNTIME_INPUTS = BASE / "design-02/relevant-runtime-inputs.json"
RUNTIME_INPUTS_SHA256 = "b0fd1c7c4d40e20e19d8ed0a4b63397360b98218af2d3cce9294955aa4574d97"
MSBUILD = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe")
SCENE = {
    "H_FREE": ("hinge-basic", "air"),
    "H_LIMIT": ("hinge-limited", "air"),
    "CV_FREE": ("cv-free", "air"),
    "CV_LIMIT": ("cv-limited", "air"),
    "H_CONTACT": ("hinge-limited", "contact"),
    "CV_CONTACT": ("cv-limited", "contact"),
}
CONTACT = {"H_CONTACT", "CV_CONTACT"}
CONTACT_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_28_CONTACT_COMPLETION_ACCEPTANCE_02.json"
CONTACT_ACCEPTANCE_SHA256 = "fcba980d903288cf8b93b86f707e78407bf37f64558f6959e65e67b1d55b0c28"
CONTACT_HISTORY = BASE / "jolt-editor-completion-02/progress.json"
CONTACT_HISTORY_SHA256 = "3dc5056b05c6e06008fd5d85c77ca4979ffc904ac19162d02c43dd0b2fc511cb"
CONTACT_INPUT = BASE / "contact-v2-01/contact-input.json"
CONTACT_INPUT_SHA256 = "5854f90562e5a96ef8502d22f77ee34009ad594d77f1d59d39b277c486b5074e"
COMPONENT_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_28_COMPONENT_UI_RUN_ACCEPTANCE_01.json"
COMPONENT_ACCEPTANCE_SHA256 = "3e4afee4c8d0221347d94a4d5b48b37ce1c738f61d2ee2c49d2cb2c9dfb88064"
COMPONENT_HISTORY = BASE / "component-ui-01/contracts-budget-preservation.json"
COMPONENT_HISTORY_SHA256 = "0540a6d73732b7879b826204b2b0d25bfb2415d3288d4b1d3c018ce807a25d51"
OUTLINE_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_28_SELECTION_OUTLINE_ACCEPTANCE_01.json"
OUTLINE_ACCEPTANCE_SHA256 = "88a8a9a668a644b6443c8ae1c24ea3b04cf4260b37f4bbfe3dd05943c2737004"
OUTLINE_PATHS = BASE / "selection-highlight-02/effective-paths-03.json"
OUTLINE_PATHS_SHA256 = "8394491fbe4c56db1daa1598bdd59fc801ffa738616df6aeca9925d3956f26db"
OUTLINE_HISTORY = BASE / "selection-highlight-01/resource-accounting.json"
OUTLINE_HISTORY_SHA256 = "356f5a31a43167fc4dc1877c07e5e65bd9140bfbaa2023932f08380a36c63d77"
RESERVE_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_28_SELECTION_OUTLINE_COMPLETION_ACCEPTANCE_02.json"
RESERVE_ACCEPTANCE_SHA256 = "623000e942031821ec157bbe9446b5dbf5a4e2f14bedeb16a23bb4709fcee25b"
RESERVE_HISTORY = BASE / "selection-highlight-03/history-47.json"
RESERVE_HISTORY_SHA256 = "d9bbc10d766332dc886b783d15a145fc529d0b12de9ae3ad1fb2a335de406428"
ADMIN_BRIDGE = BASE / "selection-highlight-03/runner-administrative-bridge.json"


def contact_fixture_identity(version: int = 3) -> dict:
    path, digest = {
        2: (CONTACT_INPUT, CONTACT_INPUT_SHA256),
        3: (BASE / "contact-v3-01/contact-input.json",
            "824b02d6f06fd7ad549e543058c3f5e20fb961ff1371b18d6899a479aca5298b"),
        4: (BASE / "contact-cv-v4-01/contact-input.json",
            "2622c9753cb0003af57d501af9735077471703e02f24fa8e0803dc5b44ae51e9"),
    }[version]
    if sha(path) != digest:
        raise ValueError(f"Frozen version {version} contact input changed")
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": digest}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe(path: Path) -> Path:
    p = Path(os.path.abspath(path))
    if not p.is_relative_to(ROOT):
        raise ValueError(f"Output escapes PRE_EDITOR worktree: {p}")
    for parent in (p, *p.parents):
        if parent.exists():
            attrs = getattr(parent.stat(), "st_file_attributes", 0)
            if parent.is_symlink() or attrs & 0x400:
                raise ValueError(f"Reparse-point output ancestor: {parent}")
        if parent == ROOT:
            break
    return p


def write(path: Path, value: object) -> None:
    p = safe(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    payload = value if isinstance(value, str) else json.dumps(value, indent=2, sort_keys=True) + "\n"
    with p.open("x", encoding="utf-8", newline="\n") as out:
        out.write(payload)


def matrix() -> dict:
    if sha(MATRIX) != MATRIX_SHA256:
        raise ValueError("Frozen Phase 28 acceptance matrix identity changed")
    return json.loads(MATRIX.read_text(encoding="utf-8"))


def effective_paths() -> dict:
    if not EFFECTIVE_PATHS.is_file() or sha(EFFECTIVE_PATHS) != EFFECTIVE_PATHS_SHA256:
        raise ValueError("Prospective Phase 28 effective path record drifted")
    if not EFFECTIVE_PATHS_02.is_file() or sha(EFFECTIVE_PATHS_02) != EFFECTIVE_PATHS_02_SHA256:
        raise ValueError("Accepted Phase 28 protected-header path record drifted")
    prior = json.loads(EFFECTIVE_PATHS.read_text(encoding="utf-8"))
    record = json.loads(EFFECTIVE_PATHS_02.read_text(encoding="utf-8"))
    m = matrix()
    acceptance = json.loads(EDITOR_ACCEPTANCE.read_text(encoding="utf-8"))
    added = "RigidBodySimulation/tests/RestingContactDiagnosis.h"
    if (prior.get("source_paths") != m["source_allowlist"] + acceptance["additional_source_paths"] or
        prior.get("runtime_paths") != m["runtime_allowlist"] or
        prior.get("supporting_paths") != m["supporting_allowlist"] or
        prior.get("counts") != {"cpp_headers": 23, "runtime": 6, "supporting": 3,
                                "total": 32, "production_runtime": 29} or
        record.get("source_paths") != prior["source_paths"] or
        record.get("runtime_paths") != prior["runtime_paths"] or
        record.get("supporting_paths") != prior["supporting_paths"] + [added] or
        record.get("counts") != {"cpp_headers": 23, "runtime": 6, "supporting": 4,
                                 "total": 33, "production_runtime": 29} or
        record.get("prior_effective_scope") != {"path": EFFECTIVE_PATHS.relative_to(ROOT).as_posix(),
                                                 "sha256": EFFECTIVE_PATHS_SHA256} or
        record.get("owner_build_memory_acceptance") != {
            "path": MEMORY_SCOPE_ACCEPTANCE.relative_to(ROOT).as_posix(),
            "sha256": MEMORY_SCOPE_ACCEPTANCE_SHA256,
            "decision": "D28-BUILD-MEMORY-SCOPE-01"}):
        raise ValueError("Prospective Phase 28 path record does not match accepted amendment")
    authority = outline_authority()
    if sha(OUTLINE_PATHS) != OUTLINE_PATHS_SHA256:
        raise ValueError("Accepted Phase 28 selection outline path record drifted")
    outline = json.loads(OUTLINE_PATHS.read_text(encoding="utf-8"))
    if (outline.get("source_paths") != record["source_paths"] + authority["additional_source_paths"] or
        outline.get("runtime_paths") != record["runtime_paths"] or
        outline.get("supporting_paths") != record["supporting_paths"] or
        outline.get("counts") != {"cpp_headers": 25, "runtime": 6, "supporting": 4,
                                  "total": 35, "production_runtime": 31} or
        outline.get("prior_effective_scope") != {"path": EFFECTIVE_PATHS_02.relative_to(ROOT).as_posix(),
                                                   "sha256": EFFECTIVE_PATHS_02_SHA256} or
        outline.get("owner_selection_outline_acceptance") != {
            "path": OUTLINE_ACCEPTANCE.relative_to(ROOT).as_posix(),
            "sha256": OUTLINE_ACCEPTANCE_SHA256, "decision": "D28-SELECTION-OUTLINE-01"}):
        raise ValueError("Selection outline path record exceeds the exact Owner scope")
    return outline


def outline_authority() -> dict:
    for path, digest in ((OUTLINE_ACCEPTANCE, OUTLINE_ACCEPTANCE_SHA256),
                         (OUTLINE_HISTORY, OUTLINE_HISTORY_SHA256)):
        if not path.is_file() or sha(path) != digest:
            raise ValueError("Selection outline authority or 44-process history drifted")
    authority = json.loads(OUTLINE_ACCEPTANCE.read_text(encoding="utf-8"))
    if (authority.get("phase") != "28" or authority.get("decision") != "D28-SELECTION-OUTLINE-01" or
        authority.get("status") != "OWNER_APPROVED_WITH_CLARIFICATIONS" or
        authority.get("additional_source_paths") != [
            "GEngine/include/GEngine/Renderer/FrameSubmission.h", "GEngine/src/Renderer/FrameSubmission.cpp"] or
        authority.get("scope") != {"production_runtime": 31, "supporting": 4, "total": 35, "only_phase28": True} or
        authority.get("accepted_limits") != {"rbs_processes_max": 50, "ordinary_editor_processes_max": 18,
            "debug_processes_max": 12, "release_fixture_processes_max": 24, "fixed_physics_ticks_max": 20000,
            "seconds_per_process_max": 180, "seconds_per_affected_configuration_build_max": 1200} or
        authority.get("historical_usage") != {"total": 44, "Debug": 10, "Release": 22, "Editor": 12,
            "known_ticks": 15153, "unknown_tick_reservation": 840, "debited_ticks": 15993} or
        authority.get("historical_progress") != {"path": OUTLINE_HISTORY.relative_to(ROOT).as_posix(),
                                                  "sha256": OUTLINE_HISTORY_SHA256} or
        authority.get("correction_replacements_max") != 5 or
        authority.get("correction_replacements_already_used") != 2 or
        authority.get("per_editor_reserved_ticks_max") != 600 or authority.get("script_frames_max") != 1800 or
        authority.get("compiler_private_guard_bytes") != 4 * 2**30 or
        authority.get("thresholds_and_mandatory_gates") != "UNCHANGED"):
        raise ValueError("Selection outline authority is not the exact accepted disposition")
    binding = authority["controlling_review"]
    if sha(safe(ROOT / binding["path"])) != binding["sha256"]:
        raise ValueError("Selection outline accepted review drifted")
    return authority


def reserve_authority() -> dict:
    for path, digest in ((RESERVE_ACCEPTANCE, RESERVE_ACCEPTANCE_SHA256),
                         (RESERVE_HISTORY, RESERVE_HISTORY_SHA256)):
        if sha(path) != digest:
            raise ValueError("Accepted completion reserve or 47-launch history drifted")
    authority = json.loads(RESERVE_ACCEPTANCE.read_text(encoding="utf-8"))
    if (authority.get("decision") != "D28-SELECTION-OUTLINE-COMPLETION-02" or
        authority.get("status") != "OWNER_APPROVED_WITH_RESOURCE_CLARIFICATION" or
        authority.get("accepted_limits") != {"rbs_processes_max": 53, "ordinary_editor_processes_max": 21,
            "debug_processes_max": 12, "release_fixture_processes_max": 24, "fixed_physics_ticks_max": 20000,
            "seconds_per_process_max": 180, "seconds_per_affected_configuration_build_max": 1200} or
        authority.get("correction_replacements_max") != 9 or authority.get("source_paths_added") != [] or
        authority.get("script_frames_max") != 1800 or authority.get("thresholds_and_mandatory_gates") != "UNCHANGED"):
        raise ValueError("Completion reserve differs from the exact Owner disposition")
    review = authority["controlling_review"]
    if sha(safe(ROOT / review["path"])) != review["sha256"]:
        raise ValueError("Accepted Candidate47 completion review drifted")
    return authority


def administrative_runner_bridge(archive: dict, identity: dict) -> dict:
    """Keep the accepted Candidate47 script/build exact across the budget-only tool update."""
    reserve_authority()
    bridge = json.loads(ADMIN_BRIDGE.read_text(encoding="utf-8"))
    runner = "tools/pre_editor_phase28_qualification.py"
    old = "4e25474dee31ac7c4a6d854db602ba5b520582191448e0108dfd856ec445f920"
    if (archive.get("candidate") != "candidate-47" or archive["files"].get(runner) != old or
        bridge.get("before_sha256") != old or bridge.get("authority_sha256") != RESERVE_ACCEPTANCE_SHA256 or
        bridge.get("after_sha256") != sha(ROOT / runner) or
        identity["sources"]["sources"].get(runner) != bridge["after_sha256"] or
        bridge.get("status") != "VERIFIED_ADMINISTRATIVE_ONLY" or
        sha(safe(ROOT / bridge["exact_delta"]["path"])) != bridge["exact_delta"]["sha256"]):
        raise ValueError("Candidate47 administrative runner bridge is missing or drifted")
    return {"path": ADMIN_BRIDGE.relative_to(ROOT).as_posix(), "sha256": sha(ADMIN_BRIDGE)}


def accepted_run_budget() -> dict:
    for path, digest in ((BUDGET_ACCEPTANCE, BUDGET_ACCEPTANCE_SHA256),
                         (BUDGET_REVIEW, BUDGET_REVIEW_SHA256),
                         (BUDGET_PROGRESS, BUDGET_PROGRESS_SHA256),
                         (QUALIFICATION_PLAN, QUALIFICATION_PLAN_SHA256),
                         (BUDGET_ACCEPTANCE_02, BUDGET_ACCEPTANCE_02_SHA256),
                         (BUDGET_REVIEW_02, BUDGET_REVIEW_02_SHA256),
                         (BUDGET_PROGRESS_02, BUDGET_PROGRESS_02_SHA256),
                         (CANDIDATE_10_ARCHIVE, CANDIDATE_10_ARCHIVE_SHA256),
                         (QUALIFICATION_PLAN_02, QUALIFICATION_PLAN_02_SHA256),
                         (CAMPAIGN, CAMPAIGN_SHA256),
                         (EDITOR_ACCEPTANCE, EDITOR_ACCEPTANCE_SHA256),
                         (MEMORY_SCOPE_REVIEW, MEMORY_SCOPE_REVIEW_SHA256),
                         (MEMORY_SCOPE_ACCEPTANCE, MEMORY_SCOPE_ACCEPTANCE_SHA256)):
        if not path.is_file() or sha(path) != digest:
            raise ValueError(f"Phase 28 run-budget authority drift: {path}")
    prior = json.loads(BUDGET_ACCEPTANCE.read_text(encoding="utf-8"))
    prior_limits = {"automatic_unchanged_retries": False, "debug_processes_max": 3,
                "fixed_physics_ticks_max": 8880, "ordinary_editor_processes_max": 2,
                "rbs_processes_max": 17, "release_fixture_processes_max": 12,
                "seconds_per_affected_configuration_build_max": 1200,
                "seconds_per_process_max": 180}
    if prior.get("phase") != "28" or prior.get("decision") != "D28-RUN-BUDGET-01" or \
       prior.get("status") != "ACCEPTED" or prior.get("accepted_limits") != prior_limits or \
       prior.get("accounting_at_acceptance") != {"fixed_physics_ticks": 4, "rbs_processes": 1} or \
       prior.get("controlling_review") != {"path": BUDGET_REVIEW.relative_to(ROOT).as_posix(),
                                               "sha256": BUDGET_REVIEW_SHA256} or \
       prior.get("previous_progress") != {"path": BUDGET_PROGRESS.relative_to(ROOT).as_posix(),
                                              "sha256": BUDGET_PROGRESS_SHA256} or \
       not str(prior.get("replacement", "")).startswith("Exactly one changed Native Debug replacement"):
        raise ValueError("Previous Phase 28 run-budget decision drifted")
    decision = json.loads(BUDGET_ACCEPTANCE_02.read_text(encoding="utf-8"))
    limits = dict(prior_limits, debug_processes_max=4, rbs_processes_max=18)
    if decision.get("phase") != "28" or decision.get("decision") != "D28-RUN-BUDGET-02" or \
       decision.get("status") != "ACCEPTED" or decision.get("owner_disposition") != "APPROVED" or \
       decision.get("candidate") != "candidate-10" or decision.get("accepted_limits") != limits or \
       decision.get("accounting_at_acceptance") != {"fixed_physics_ticks": 8, "rbs_processes": 2} or \
       decision.get("prior_accepted_budget") != {"path": BUDGET_ACCEPTANCE.relative_to(ROOT).as_posix(),
                                                 "sha256": BUDGET_ACCEPTANCE_SHA256} or \
       decision.get("controlling_review") != {"path": BUDGET_REVIEW_02.relative_to(ROOT).as_posix(),
                                               "sha256": BUDGET_REVIEW_02_SHA256} or \
       decision.get("previous_progress") != {"path": BUDGET_PROGRESS_02.relative_to(ROOT).as_posix(),
                                              "sha256": BUDGET_PROGRESS_02_SHA256} or \
       decision.get("candidate_archive") != {"path": CANDIDATE_10_ARCHIVE.relative_to(ROOT).as_posix(),
                                               "sha256": CANDIDATE_10_ARCHIVE_SHA256} or \
       decision.get("replacement_harness") != {"path": "RigidBodySimulation/tests/AngularJointChecks.h",
                                               "sha256": CANDIDATE_10_HEADER_SHA256} or \
       decision.get("replacement_debug_binary") != {"path": "bin/Debug/RigidBodySimulation/RigidBodySimulation.exe",
                                                   "sha256": CANDIDATE_10_DEBUG_EXE_SHA256}:
        raise ValueError("Current Phase 28 run-budget decision is not the accepted exact disposition")
    campaign = json.loads(CAMPAIGN.read_text(encoding="utf-8"))
    campaign_limits = {"debug_processes_max": 8, "fixed_physics_ticks_max": 12000,
                       "ordinary_editor_processes_max": 4, "rbs_processes_max": 26,
                       "release_fixture_processes_max": 14,
                       "seconds_per_affected_configuration_build_max": 1200,
                       "seconds_per_process_max": 180, "unchanged_failing_retries": False}
    history = campaign.get("historical_accounting", {})
    if campaign.get("phase") != "28" or campaign.get("status") != "OWNER_AUTHORIZED_ACTIVE" or \
       campaign.get("campaign") != "PHASE_28_AUTONOMOUS_CAMPAIGN_01" or \
       campaign.get("accepted_limits") != campaign_limits or \
       any(history.get(k) != v for k, v in {
           "processes": 4, "debug_processes": 4, "release_fixture_processes": 0,
           "ordinary_editor_processes": 0, "known_captured_ticks": 248,
           "crashed_jolt_actual_ticks": "UNKNOWN", "crashed_jolt_reserved_ticks": 240,
           "conservative_debit_total": 488}.items()):
        raise ValueError("Phase 28 autonomous campaign authority is not the exact accepted disposition")
    acceptance = json.loads(EDITOR_ACCEPTANCE.read_text(encoding="utf-8"))
    expected_additions = ["GEngine/include/GEngine/Physics/PhysicsBackend.h",
                          "GEngine/include/GEngine/Scene/_Scene.h",
                          "GEngine/src/Scene/_Scene.cpp",
                          "RigidBodySimulation/include/RbsEditorWidgets.h",
                          "RigidBodySimulation/src/RbsViewportToolbar.cpp"]
    expected_limits = {"rbs_processes_max": 40, "debug_processes_max": 12,
                       "release_fixture_processes_max": 24,
                       "ordinary_editor_processes_max": 4,
                       "fixed_physics_ticks_max": 20000,
                       "seconds_per_process_max": 180,
                       "seconds_per_affected_configuration_build_max": 1200}
    if (acceptance.get("phase") != "28" or
        acceptance.get("decision") != "D28-JOLT-EDITOR-COMPLETION-01" or
        acceptance.get("status") != "ACCEPTED_WITH_OWNER_RESOURCE_AMENDMENT" or
        acceptance.get("additional_source_paths") != expected_additions or
        acceptance.get("limits") != expected_limits or
        acceptance.get("historical_debit") != {"processes": 18, "Debug": 7,
                                             "Release": 11, "Editor": 0,
                                             "known_ticks": 7568,
                                             "crash_unknown_debit": 240,
                                             "total_tick_debit": 7808,
                                             "crash_actual_ticks": "UNKNOWN"} or
        acceptance.get("thresholds_and_mandatory_gates") != "UNCHANGED"):
        raise ValueError("Phase 28 Editor completion authority is not the exact accepted decision")
    memory_scope = json.loads(MEMORY_SCOPE_ACCEPTANCE.read_text(encoding="utf-8"))
    if (memory_scope.get("phase") != "28" or
        memory_scope.get("decision") != "D28-BUILD-MEMORY-SCOPE-01" or
        memory_scope.get("status") != "OWNER_APPROVED_WITH_STRICT_EQUIVALENCE" or
        memory_scope.get("review") != {"path": MEMORY_SCOPE_REVIEW.relative_to(ROOT).as_posix(),
                                       "sha256": MEMORY_SCOPE_REVIEW_SHA256} or
        memory_scope.get("additional_supporting_path") != "RigidBodySimulation/tests/RestingContactDiagnosis.h" or
        memory_scope.get("before_sha256") != "16f290e9461f7c4f521d9b1db20863dfaebf5f5ac2c9ccb11e0e4c426b01c386" or
        memory_scope.get("scope") != {"production_runtime": 29, "supporting": 4,
                                      "only_phase28": True} or
        memory_scope.get("historical_usage") != {"total": 18, "Debug": 7,
                                                  "Release": 11, "Editor": 0,
                                                  "known_ticks": 7568,
                                                  "crash_unknown_reservation": 240,
                                                  "debited_ticks": 7808} or
        memory_scope.get("limits") != {"RBS": 40, "Debug": 12, "Release": 24,
                                       "Editor": 4, "fixed_ticks": 20000,
                                       "seconds_per_process": 180,
                                       "seconds_per_configuration_build": 1200,
                                       "compiler_private_guard_bytes": 4 * 2**30}):
        raise ValueError("Phase 28 protected diagnostic header decision drifted")
    effective_paths()
    if sha(CONTACT_ACCEPTANCE) != CONTACT_ACCEPTANCE_SHA256 or sha(CONTACT_HISTORY) != CONTACT_HISTORY_SHA256:
        raise ValueError("Versioned contact authority or historical debit changed")
    contact = json.loads(CONTACT_ACCEPTANCE.read_text(encoding="utf-8"))
    if contact["limits"] != dict(expected_limits, ordinary_editor_processes_max=8):
        raise ValueError("Only the ordinary Editor category allowance was amended")
    campaign["accepted_limits"] = contact["limits"]
    campaign["campaign"] = contact["decision"]
    for path, digest in ((COMPONENT_ACCEPTANCE, COMPONENT_ACCEPTANCE_SHA256),
                         (COMPONENT_HISTORY, COMPONENT_HISTORY_SHA256)):
        if not path.is_file() or sha(path) != digest:
            raise ValueError("Accepted component UI authority or 40-process history drifted")
    component = json.loads(COMPONENT_ACCEPTANCE.read_text(encoding="utf-8"))
    if (component.get("phase") != "28" or component.get("decision") != "D28-COMPONENT-UI-RUN-01" or
        component.get("status") != "OWNER_APPROVED" or
        component.get("accepted_limits") != dict(expected_limits, rbs_processes_max=46,
                                                  ordinary_editor_processes_max=14) or
        component.get("historical_usage") != {"total": 40, "Debug": 10, "Release": 22, "Editor": 8,
            "known_ticks": 15139, "unknown_tick_reservation": 240, "debited_ticks": 15379} or
        component.get("planned_sessions") != 4 or component.get("correction_replacements_max") != 2 or
        component.get("per_editor_reserved_ticks_max") != 600 or component.get("script_frames_max") != 1800 or
        component.get("source_paths_added") != []):
        raise ValueError("Component UI authority is not the exact accepted bounded campaign")
    for key in ("controlling_review", "candidate_review", "historical_progress"):
        binding = component[key]
        if sha(safe(ROOT / binding["path"])) != binding["sha256"]:
            raise ValueError("Component UI acceptance provenance drifted")
    outline = outline_authority()
    campaign["accepted_limits"] = outline["accepted_limits"]
    campaign["campaign"] = outline["decision"]
    campaign["component_replacements_max"] = outline["correction_replacements_max"]
    reserve = reserve_authority()
    campaign["accepted_limits"] = reserve["accepted_limits"]
    campaign["campaign"] = reserve["decision"]
    campaign["component_replacements_max"] = reserve["correction_replacements_max"]
    return campaign


def environment() -> dict[str, str]:
    e = os.environ.copy()
    for key in list(e):
        if key.startswith(("GENGINE_", "JPH_")) or key.upper() in ("CL", "_CL_"):
            del e[key]
    temp = safe(ROOT / "runtime/tmp/phase28")
    temp.mkdir(parents=True, exist_ok=True)
    e.update(TMP=str(temp), TEMP=str(temp))
    return e


def inputs(config: str) -> dict:
    m = matrix()
    paths = set(effective_paths()["source_paths"] + effective_paths()["supporting_paths"] +
                effective_paths()["runtime_paths"] + m["contract_files"])
    closure = BASE / "design-02/relevant-build-inputs-02.json"
    paths.update(json.loads(closure.read_text(encoding="utf-8"))["paths"])
    paths.add(closure.relative_to(ROOT).as_posix())
    accepted_run_budget()
    paths.update(p.relative_to(ROOT).as_posix() for p in
                 (BUDGET_ACCEPTANCE, BUDGET_REVIEW, BUDGET_PROGRESS, QUALIFICATION_PLAN,
                  BUDGET_ACCEPTANCE_02, BUDGET_REVIEW_02, BUDGET_PROGRESS_02,
                  CANDIDATE_10_ARCHIVE, QUALIFICATION_PLAN_02, CAMPAIGN,
                  EDITOR_ACCEPTANCE, EFFECTIVE_PATHS,
                  MEMORY_SCOPE_REVIEW, MEMORY_SCOPE_ACCEPTANCE, EFFECTIVE_PATHS_02,
                  CONTACT_ACCEPTANCE, CONTACT_HISTORY, COMPONENT_ACCEPTANCE, COMPONENT_HISTORY,
                  OUTLINE_ACCEPTANCE, OUTLINE_PATHS, OUTLINE_HISTORY,
                  RESERVE_ACCEPTANCE, RESERVE_HISTORY, ADMIN_BRIDGE))
    runtime = runtime_input_record()
    paths.add(RUNTIME_INPUTS.relative_to(ROOT).as_posix())
    paths.update(runtime["resource_sources"].values())
    paths.update([
        "premake5.lua", "GEngine/GEngine.vcxproj",
        "RigidBodySimulation/RigidBodySimulation.vcxproj",
        "external/jolt-build/Jolt.vcxproj", "external/glad/glad.vcxproj",
        "tools/postbuild.py", "RigidBodySimulation/postbuild.py",
        "RigidBodySimulation/include/RbsPch.h", "RigidBodySimulation/src/RbsPch.cpp",
    ])
    return {p: sha(ROOT / p) if (ROOT / p).is_file() else None for p in sorted(paths)}


def runtime_input_record() -> dict:
    if sha(RUNTIME_INPUTS) != RUNTIME_INPUTS_SHA256:
        raise ValueError("Declared Phase 28 runtime input closure identity changed")
    return json.loads(RUNTIME_INPUTS.read_text(encoding="utf-8"))


def qualified_bin(config: str, project: str) -> Path:
    if config not in ("Debug", "Release") or project not in ("RigidBodySimulation", "GEngine", "Jolt", "glad"):
        raise ValueError("Invalid isolated Phase 28 build target")
    return safe(ROOT / "bin/phase28-qualification" / config / project)


def qualified_runtime(config: str, session: str | None = None) -> Path:
    path = safe(ROOT / "runtime/RigidBodySimulation/phase28-qualification" / config)
    return safe(path / session) if session else path


def runtime_inputs(config: str, isolated: bool = False) -> dict:
    """Exact consumed runtime files and discovery membership; excludes mutable imgui.ini."""
    record = runtime_input_record()
    asset_root = (safe(ROOT / "bin/phase28-qualification" / config / "assets") if isolated else
                  safe(ROOT / record["staged_asset_root"].format(config=config)))
    resources = record["resource_sources"]
    absent = set(record["initially_absent_until_native_editor_capture"])
    present = {}
    for resource, source in sorted(resources.items()):
        source_path = safe(ROOT / source)
        source_exists = source_path.is_file()
        if resource not in absent and not source_exists:
            raise FileNotFoundError(f"Declared runtime source missing: {source}")
        staged = safe(asset_root / resource)
        staged_exists = staged.is_file()
        if resource not in absent and not staged_exists:
            raise FileNotFoundError(f"Declared staged runtime asset missing: {staged}")
        if source_exists != staged_exists:
            raise ValueError(f"Source/staged asset membership differs: {resource}")
        if source_exists and sha(source_path) != sha(staged):
            raise ValueError(f"Source/staged asset bytes differ: {resource}")
        present[staged.relative_to(ROOT).as_posix()] = (
            {"sha256": sha(staged), "mode": staged.stat().st_mode} if staged.is_file() else None)
    for name in record["consumed_generated"]:
        staged = safe(asset_root / name)
        if not staged.is_file():
            raise FileNotFoundError(f"Declared generated runtime input missing: {staged}")
        present[staged.relative_to(ROOT).as_posix()] = {
            "sha256": sha(staged), "mode": staged.stat().st_mode}
    directory_members = {}
    for name in record["required_directories"]:
        directory = safe(asset_root / name)
        if not directory.is_dir():
            raise FileNotFoundError(f"Required runtime asset directory missing: {directory}")
        directory_members[directory.relative_to(ROOT).as_posix()] = True
    actual_scenes = sorted(p for p in record["exact_discovery_scope"]
                           if (asset_root / p).is_file())
    return {"sources": inputs(config), "staged_files": present,
            "directories": directory_members, "scene_cards": actual_scenes}


def qualification_profile() -> Path:
    """One content-addressed, shared profile for every affected project/attempt."""
    target = safe(BASE / "qualification-profiles")
    bin_root = safe(ROOT / "bin/phase28-qualification")
    int_root = safe(ROOT / "bin-int/phase28-qualification")
    content = ("<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">"
               "<PropertyGroup><OutDir>" + escape(str(bin_root)) +
               "\\$(Configuration)\\$(MSBuildProjectName)\\</OutDir><IntDir>" +
               escape(str(int_root)) +
               "\\$(Configuration)\\$(MSBuildProjectName)\\</IntDir>"
               "<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>"
               "<ItemDefinitionGroup><ClCompile>"
               "<PreprocessorDefinitions>GENGINE_PHASE28_QUALIFICATION;GENGINE_JOLT_FINAL_AUDIT;"
               "GE_ENABLE_PHYSICS_PROFILING;JPH_EXTERNAL_PROFILE;%(PreprocessorDefinitions)"
               "</PreprocessorDefinitions><ShowIncludes>true</ShowIncludes>"
               "<ObjectFileName>$(IntDir)</ObjectFileName>"
               "<PrecompiledHeaderOutputFile>$(IntDir)$(MSBuildProjectName).pch</PrecompiledHeaderOutputFile>"
               "<ProgramDataBaseFileName>$(IntDir)$(MSBuildProjectName).compile.pdb</ProgramDataBaseFileName>"
               "<AssemblerListingLocation>$(IntDir)</AssemblerListingLocation>"
               "</ClCompile><Link><OutputFile>$(OutDir)$(TargetName)$(TargetExt)</OutputFile>"
               "<ProgramDatabaseFile>$(OutDir)$(TargetName).pdb</ProgramDatabaseFile>"
               "<GenerateMapFile>true</GenerateMapFile><MapFileName>$(OutDir)phase28.map</MapFileName>"
               "</Link><Lib><OutputFile>$(OutDir)$(TargetName)$(TargetExt)</OutputFile></Lib>"
               "<PostBuildEvent><Command></Command></PostBuildEvent></ItemDefinitionGroup></Project>\n")
    path = safe(target / (hashlib.sha256(content.encode("utf-8")).hexdigest() + ".props"))
    if path.exists():
        if path.read_text(encoding="utf-8") != content:
            raise ValueError("Content-addressed Phase 28 qualification profile drifted")
    else:
        write(path, content)
    return path


def stage_qualified_runtime(config: str) -> dict:
    """Reuse approved staging functions while keeping the ordinary postbuild off."""
    destination = qualified_bin(config, "RigidBodySimulation")
    postbuild_tools.stage_profile_runtime(ROOT, config, destination)
    for overlay in ("PostBuildCopy", "PostBuildCopy_windows"):
        source = safe(ROOT / "RigidBodySimulation" / overlay)
        if source.is_dir():
            if source.is_symlink() or getattr(source.stat(), "st_file_attributes", 0) & 0x400:
                raise ValueError("Qualification overlay is a reparse point")
            shutil.copytree(source, destination, dirs_exist_ok=True)
    postbuild_tools.stage_runtime_assets(ROOT, safe(destination.parent / "assets"))
    postbuild_tools.stage_assimp_runtime(ROOT, destination)
    missing = [name for name in postbuild_tools.runtime_dlls(config, "RigidBodySimulation")
               if not (destination / name).is_file()]
    if missing:
        raise ValueError(f"Isolated Phase 28 runtime DLLs missing: {missing}")
    return runtime_inputs(config, isolated=True)


def stage(config: str, attempt: str) -> None:
    """One explicit asset/runtime refresh after reviewed thumbnail source installation."""
    if config not in ("Debug", "Release"):
        raise ValueError("Invalid qualification staging configuration")
    directory = safe(BASE / f"stage-{config}-{attempt}")
    directory.mkdir(parents=True, exist_ok=False)
    before = inputs(config)
    write(directory / "inputs-before.json", before)
    staged = stage_qualified_runtime(config)
    unchanged = before == inputs(config)
    write(directory / "result.json", {"inputs_unchanged": unchanged, "staged_runtime": staged})
    if not unchanged:
        raise ValueError("Qualification staging changed its source inputs")


def archive_initial_imgui(directory: Path, cwd: Path) -> dict:
    source = safe(cwd / "imgui.ini")
    if source.is_file():
        payload = source.read_bytes()
        with safe(directory / "initial-imgui.ini").open("xb") as out:
            out.write(payload)
        identity = {"present": True, "sha256": hashlib.sha256(payload).hexdigest(),
                    "bytes": len(payload), "mode": source.stat().st_mode}
    else:
        identity = {"present": False}
    write(directory / "initial-imgui.json", identity)
    return identity


def invoke(command: list[object], directory: Path, timeout: int, e: dict[str, str], cwd: Path) -> dict:
    argv = [str(x) for x in command]
    write(directory / "command.json", {"argv": argv, "cwd": str(cwd), "timeout_s": timeout})
    start = time.monotonic()
    with (directory / "process.log").open("xb") as log:
        process = subprocess.Popen(argv, cwd=cwd, env=e, stdout=log, stderr=subprocess.STDOUT)
        timed_out = False
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            code = process.wait()
    return {"exit_code": code, "seconds": time.monotonic() - start, "timed_out": timed_out}


def correction_identity(value: str) -> dict:
    path = safe(ROOT / value)
    relative = path.relative_to(BASE) if path.is_relative_to(BASE) else None
    if not path.is_file() or relative is None or len(relative.parts) != 2 or \
       not relative.parts[0].startswith("correction-") or path.suffix != ".json":
        raise ValueError("Replacement correction must be an existing Phase 28 correction JSON")
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path)}


def require_correction(identity: dict, predecessor: tuple[Path, dict],
                       invalidated: bool) -> None:
    binding = identity.get("correction")
    if not isinstance(binding, dict) or not isinstance(binding.get("path"), str):
        raise ValueError("A replacement process requires explicit correction evidence")
    current = correction_identity(binding["path"])
    if binding != current:
        raise ValueError("Replacement correction evidence drifted")
    correction = json.loads((ROOT / binding["path"]).read_text(encoding="utf-8"))
    previous_path, previous = predecessor
    previous_name = previous_path.name
    previous_relative = previous_path.relative_to(ROOT).as_posix()
    if correction.get("phase") not in (28, "28") or \
       not isinstance(correction.get("reason"), str) or not correction["reason"].strip():
        raise ValueError("Replacement correction lacks Phase 28 reason")
    if invalidated:
        named = correction.get("invalidated_runs")
        if not isinstance(named, list) or not any(p in (previous_name, previous_relative) for p in named):
            raise ValueError("Revalidation does not bind its invalidated PASS run")
    elif correction.get("failed_run") not in (previous_name, previous_relative):
        raise ValueError("Replacement correction does not bind its failed predecessor")
    old_sources = previous["sources"]["sources"]
    new_sources = identity["sources"]["sources"]
    frozen = matrix()
    relevant = set(effective_paths()["source_paths"] + effective_paths()["runtime_paths"] +
                   ["RigidBodySimulation/tests/RestingContactDiagnosis.h"] +
                   ["RigidBodySimulation/tests/AngularJointChecks.h", "premake5.lua",
                    "GEngine/GEngine.vcxproj", "RigidBodySimulation/RigidBodySimulation.vcxproj",
                    "external/jolt-build/Jolt.vcxproj"])
    admitted_before = json.loads(EDITOR_ACCEPTANCE.read_text(encoding="utf-8"))["before_sha256"]
    source_changed = any(old_sources.get(p, admitted_before.get(p)) != new_sources.get(p)
                         for p in relevant)
    product_changed = (previous["executable"]["sha256"] != identity["executable"]["sha256"] or
                       previous["runtime_dlls"] != identity["runtime_dlls"] or
                       previous["sources"]["staged_files"] != identity["sources"]["staged_files"])
    if source_changed and product_changed:
        return
    # Component scripts are immutable external SDL/ImGui action inputs. A causal
    # script correction changes the executed stimulus even when the binary matches.
    if identity.get("editor_session") in COMPONENT_EDITOR_ROLES and previous.get("component_plan"):
        current_plan, current_binding = frozen_component_plan(identity["component_plan"]["path"], identity["editor_session"])
        old_plan_path = safe(ROOT / previous["component_plan"]["path"])
        if current_binding != identity["component_plan"] or sha(old_plan_path) != previous["component_plan"]["sha256"]:
            raise ValueError("Replacement session plan identity drifted")
        old_plan = json.loads(old_plan_path.read_text(encoding="utf-8"))
        impact = correction.get("execution_impact")
        if (isinstance(impact, str) and impact.strip() and
            identity.get("component_script", {}).get("sha256") != previous.get("component_script", {}).get("sha256") and
            current_plan["script"]["actions"] != old_plan["script"]["actions"]):
            return
    # A runner-only fix qualifies only when the executed invocation changes.
    # An audit/parser change uses existing raw evidence and consumes no new RBS process.
    runner = "tools/pre_editor_phase28_qualification.py"
    runner_changed = old_sources.get(runner) != new_sources.get(runner)
    current_args = planned_args(identity)
    previous_args = json.loads((previous_path / "command.json").read_text(encoding="utf-8"))["argv"][1:]
    impact = correction.get("execution_impact")
    if not (runner_changed and isinstance(impact, str) and impact.strip() and
            current_args != previous_args):
        raise ValueError("Unchanged failing or invalidated workload cannot consume a new RBS process")


EDITOR_ROLES = {"fresh-jolt": ("EDITOR_JOLT", "jolt", "hinge-limited", False),
                "saved-native": ("EDITOR_NATIVE", "native", "hinge-basic", False),
                "override-native": ("EDITOR_NATIVE_OVERRIDE", "native", "hinge-limited", True),
                "properties-jolt": ("EDITOR_PROPERTIES_JOLT", "jolt", "hinge-limited", True),
                "properties-native": ("EDITOR_PROPERTIES_NATIVE", "native", "hinge-limited", True),
                "properties-default-jolt": ("EDITOR_PROPERTIES_JOLT", "jolt", "hinge-limited", False),
                "direct-default-jolt": ("EDITOR_DIRECT_JOLT", "jolt", "hinge-limited", False),
                "general-default-jolt": ("EDITOR_GENERAL_JOLT", "jolt", "hinge-limited", False),
                "general-native": ("EDITOR_GENERAL_NATIVE", "native", "hinge-limited", True),
                "component-angular-jolt": ("EDITOR_COMPONENT_ANGULAR_JOLT", "jolt", "hinge-limited", False),
                "component-general-jolt": ("EDITOR_COMPONENT_GENERAL_JOLT", "jolt", "hinge-limited", False),
                "component-view-jolt": ("EDITOR_COMPONENT_VIEW_JOLT", "jolt", "hinge-limited", False),
                "component-native": ("EDITOR_COMPONENT_NATIVE", "native", "hinge-limited", True)}

GENERAL_EDITOR_ROLES = ("general-default-jolt", "general-native")
COMPONENT_EDITOR_ROLES = ("component-angular-jolt", "component-general-jolt", "component-view-jolt", "component-native")


def planned_args(identity: dict) -> list[str]:
    case = identity["case"]
    if case.startswith("EDITOR_"):
        role = identity.get("editor_session")
        if role not in EDITOR_ROLES:
            raise ValueError("Editor run has no admitted session role")
        expected_case, backend, scene, explicit = EDITOR_ROLES[role]
        if case != expected_case or identity["backend"] != backend:
            raise ValueError("Editor session identity disagrees with admitted role")
        return ["--scene", scene] + (["--backend", backend] if explicit else []) + ["--variant", "air"]
    scene, variant = SCENE[case]
    return ["--scene", scene, "--backend", identity["backend"], "--variant", variant]


def require_jolt_debug_prerequisite(identity: dict, debug_pass: tuple[Path, dict],
                                    release_failure: tuple[int, Path, dict]) -> None:
    """A corrected limited Jolt Debug must precede its pending Release retry."""
    require_correction(identity, debug_pass, True)
    failed_path = release_failure[1]
    correction = json.loads((ROOT / identity["correction"]["path"]).read_text(encoding="utf-8"))
    if correction.get("failed_run") not in (failed_path.name,
                                             failed_path.relative_to(ROOT).as_posix()):
        raise ValueError("Jolt Debug prerequisite correction does not bind pending Release failure")
    old = debug_pass[1]
    provider = "GEngine/src/Physics/JoltBackend.cpp"
    if old["sources"]["sources"].get(provider) == identity["sources"]["sources"].get(provider) or \
       old["executable"]["sha256"] == identity["executable"]["sha256"]:
        raise ValueError("Jolt Debug prerequisite needs changed compiled adapter source and product")


def qualified_audit(directory: Path, original: dict) -> dict:
    """Select a reviewed create-only supplement; retain the first audit unchanged."""
    pointer_path = directory / "qualified-audit.json"
    if not pointer_path.exists():
        return original
    pointer = json.loads(pointer_path.read_text(encoding="utf-8"))
    supplement = pointer.get("supplemental_audit", {})
    supplemental_path = safe(ROOT / supplement.get("path", ""))
    run_name = directory.relative_to(ROOT).as_posix()
    if pointer.get("phase") not in (28, "28") or pointer.get("run") != run_name or \
       pointer.get("original_audit") != {"path": (directory / "audit.json").relative_to(ROOT).as_posix(),
                                         "sha256": sha(directory / "audit.json")} or \
       supplemental_path.parent != directory or \
       not supplemental_path.name.startswith("audit-allocation-") or \
       supplemental_path.suffix != ".json" or not supplemental_path.is_file() or \
       supplement.get("sha256") != sha(supplemental_path):
        raise ValueError("Phase 28 qualified-audit pointer does not bind preserved evidence")
    selected = json.loads(supplemental_path.read_text(encoding="utf-8"))
    if original.get("status") != "INCOMPLETE" or selected.get("status") != "PASS" or \
       selected.get("run") != run_name or \
       selected.get("supersedes_audit_sha256") != sha(directory / "audit.json") or \
       selected.get("inputs_sha256") != sha(directory / "inputs.json") or \
       selected.get("result_sha256") != sha(directory / "result.json"):
        raise ValueError("Phase 28 supplemental audit identity or outcome does not match")
    raw = selected.get("raw_sha256", {})
    if raw != original.get("raw_sha256") or \
       any(sha(directory / "data" / name) != digest for name, digest in raw.items()):
        raise ValueError("Phase 28 supplemental audit raw input changed")
    evidence = selected.get("allocation_evidence", {})
    evidence_path = safe(ROOT / evidence.get("path", ""))
    if evidence_path.parent != directory or not evidence_path.is_file() or \
       evidence.get("sha256") != sha(evidence_path):
        raise ValueError("Phase 28 supplemental audit attribution evidence changed")
    old_checks = {c["check"]: c["result"] for c in original["checks"]}
    new_checks = {c["check"]: c["result"] for c in selected["checks"]}
    if len(old_checks) != len(original["checks"]) or len(new_checks) != len(selected["checks"]) or \
       old_checks.get("steady_heap_attribution") != "INCOMPLETE" or \
       new_checks.get("steady_heap_attribution") != "PASS" or \
       any(value != "PASS" for value in new_checks.values()) or \
       json.dumps({c["check"]: c for c in original["checks"] if c["check"] != "steady_heap_attribution"},
                  sort_keys=True) != \
       json.dumps({c["check"]: c for c in selected["checks"] if c["check"] != "steady_heap_attribution"},
                  sort_keys=True):
        raise ValueError("Supplemental audit changes a gate beyond heap source attribution")
    return selected


def check_run_budget(identity: dict) -> dict:
    """Admit one process, counting all attempts and full caps for crash-unknown ticks."""
    campaign = accepted_run_budget()
    plan = json.loads(QUALIFICATION_PLAN.read_text(encoding="utf-8"))["process_order"]
    if len(plan) != 16 or plan[0] != {"backend": "native", "case": "H_LIMIT", "config": "Debug",
                                     "max_ticks": 240, "ordinary_ticks": 237, "unit_ticks": 3}:
        raise ValueError("Original Phase 28 process order changed")
    plan.append({"backend": "native", "case": "EDITOR_NATIVE_OVERRIDE",
                 "config": "Release", "max_ticks": 600})
    for role in ("properties-jolt", "properties-native", "direct-default-jolt", *GENERAL_EDITOR_ROLES, *COMPONENT_EDITOR_ROLES):
        case, backend, _, _ = EDITOR_ROLES[role]
        plan.append({"backend": backend, "case": case, "config": "Release", "max_ticks": 600})
    by_signature = {(r["config"], r["case"], r["backend"]): i for i, r in enumerate(plan)}
    if len(by_signature) != len(plan):
        raise ValueError("Original Phase 28 process signatures are ambiguous")
    observed = []
    for path in BASE.glob("run-*"):
        if not path.is_dir():
            continue
        required = ("process.log", "command.json", "inputs.json", "result.json", "audit.json")
        missing = [name for name in required if not (path / name).is_file()]
        if missing:
            raise ValueError(f"Incomplete Phase 28 process accounting at {path}: {missing}")
        prior = json.loads((path / "inputs.json").read_text(encoding="utf-8"))
        result = json.loads((path / "result.json").read_text(encoding="utf-8"))
        original_audit = json.loads((path / "audit.json").read_text(encoding="utf-8"))
        audit = qualified_audit(path, original_audit)
        when = dt.datetime.fromisoformat(prior["utc"])
        if when.tzinfo is None or when.utcoffset() != dt.timedelta(0):
            raise ValueError(f"Unordered Phase 28 process identity: {path}")
        observed.append((when, path, prior, result, audit))
    observed.sort(key=lambda item: (item[0], item[1].name))
    if len(observed) < 18 or len(observed) >= campaign["accepted_limits"]["rbs_processes_max"] or \
       len({item[0] for item in observed}) != len(observed):
        raise ValueError("Historical Phase 28 processes missing, exhausted or ambiguous")
    known_ticks = 0
    unknown_debit = 0
    unknown_paths = []
    counts = {"Debug": 0, "Release": 0, "Editor": 0}
    next_original = 0
    latest_pass = {}
    last_failed = None
    pending_release = None
    prospective = []
    historical_debit = None
    history_names = ("run-Debug-H_LIMIT-native-01", "run-Debug-H_LIMIT-native-02",
                     "run-Debug-H_LIMIT-native-03", "run-Debug-H_LIMIT-jolt-01")
    history_rows = (0, 0, 0, 1)
    prior_native = None
    for index, (_, path, prior, result, audit) in enumerate(observed):
        signature = (prior.get("config"), prior.get("case"), prior.get("backend"))
        if signature not in by_signature:
            raise ValueError(f"Unadmitted Phase 28 process identity: {path}")
        row_index = by_signature[signature]
        row = plan[row_index]
        category = "Editor" if prior["case"].startswith("EDITOR_") else prior["config"]
        counts[category] += 1
        cap = int(row["max_ticks"])
        command = json.loads((path / "command.json").read_text(encoding="utf-8"))
        # Count an owned deadline failure without turning termination overhead into
        # a PASS allowance. Its absent final capture costs the entire reservation.
        owned_component_timeout = (
            prior.get("editor_session") in COMPONENT_EDITOR_ROLES and
            result.get("capture") is None and result.get("timed_out") is True and
            audit.get("status") == "FAIL" and result.get("exit_code") not in (None, 0) and
            isinstance(result.get("seconds"), (int, float)) and result["seconds"] >= 180 and
            command.get("timeout_s") == 180 and
            command.get("argv") == [prior["executable"]["path"], *planned_args(prior)] and
            audit.get("inputs_sha256") == sha(path / "inputs.json") and
            audit.get("result_sha256") == sha(path / "result.json"))
        if cap not in (240, 600) or result.get("inputs_unchanged") is not True or \
           not isinstance(result.get("seconds"), (int, float)) or \
           not math.isfinite(result["seconds"]) or (result["seconds"] > 180 and not owned_component_timeout) or \
           audit.get("status") not in ("PASS", "FAIL", "INCOMPLETE"):
            raise ValueError(f"Untrusted Phase 28 process result: {path}")
        for field in ("sources", "executable", "runtime_dlls", "case", "backend", "config"):
            if field not in prior:
                raise ValueError(f"Incomplete prior process identity: {path}")
        capture = result.get("capture")
        raw_path = path / "data/result.json"
        if isinstance(capture, dict):
            if not raw_path.is_file() or json.loads(raw_path.read_text(encoding="utf-8")) != capture:
                raise ValueError(f"Raw/result tick accounting differs: {path}")
            key = "physics_ticks" if category == "Editor" else "ticks"
            ticks = capture.get(key)
            if isinstance(ticks, bool) or not isinstance(ticks, int) or ticks < 0 or ticks > cap:
                raise ValueError(f"Unknown or over-cap captured ticks: {path}")
            if category != "Editor" and ticks != sum(capture.get(k, -cap) for k in
                                                       ("normal_ticks", "wrap_ticks", "unit_ticks")):
                raise ValueError(f"Physical segment ticks do not sum to capture: {path}")
            known_ticks += ticks
            successful = (capture.get("complete") is True and
                          (category == "Editor" or ticks == cap) and
                          audit["status"] == "PASS" and result.get("exit_code") == 0 and
                          result.get("timed_out") is False)
        elif capture is None:
            command = json.loads((path / "command.json").read_text(encoding="utf-8"))
            if raw_path.exists() or not (path / "process.log").stat().st_size or \
               audit["status"] != "FAIL" or (result.get("timed_out") is not False and not owned_component_timeout) or \
               result.get("exit_code") in (None, 0) or command.get("timeout_s") != 180 or \
               command.get("argv", [None])[0] != prior["executable"]["path"]:
                raise ValueError(f"Unproved launch or crash-unknown tick accounting: {path}")
            unknown_debit += cap
            unknown_paths.append(path.relative_to(ROOT).as_posix())
            successful = False
        else:
            raise ValueError(f"Malformed Phase 28 process capture: {path}")
        if audit["status"] == "PASS" and not successful:
            raise ValueError(f"False PASS on an incomplete Phase 28 process: {path}")
        if index == 17:
            historical_debit = (dict(counts), known_ticks, unknown_debit)
        if index < 4:
            if path.name != history_names[index] or row_index != history_rows[index]:
                raise ValueError("Accepted Phase 28 historical process order changed")
            if index < 2:
                expected_failure = ("book quaternion Jacobian finite difference",
                                    "normal pre-step world identity")[index]
                if not isinstance(capture, dict) or ticks != 4 or \
                   capture.get("unit_ticks") != 3 or capture.get("normal_ticks") != 1 or \
                   capture.get("failure") != expected_failure or \
                   capture.get("complete") is not False or audit["status"] != "FAIL" or \
                   result.get("exit_code") != 0 or result.get("timed_out") is not False:
                    raise ValueError("Preserved Native Debug failure evidence changed")
                if prior_native and (prior_native["executable"]["sha256"] == prior["executable"]["sha256"] or
                                     prior_native["sources"]["sources"]["RigidBodySimulation/tests/AngularJointChecks.h"] ==
                                     prior["sources"]["sources"]["RigidBodySimulation/tests/AngularJointChecks.h"]):
                    raise ValueError("Historical Native Debug correction was not a changed candidate")
                prior_native = prior
                last_failed = (row_index, path, prior)
            elif index == 2:
                if not successful or ticks != 240 or \
                   prior["executable"]["sha256"] != CANDIDATE_10_DEBUG_EXE_SHA256 or \
                   prior["sources"]["sources"]["RigidBodySimulation/tests/AngularJointChecks.h"] != CANDIDATE_10_HEADER_SHA256:
                    raise ValueError("Accepted Candidate10 Native Debug PASS evidence changed")
                next_original = 1
                latest_pass[0] = (path, prior)
                last_failed = None
            else:
                h = campaign["historical_accounting"]
                if capture is not None or result.get("exit_code") != 0x80000003 or \
                   sha(path / "inputs.json") != h["crashed_inputs"]["sha256"] or \
                   sha(path / "result.json") != h["crashed_result"]["sha256"] or \
                   sha(path / "GEngine.log") != h["crashed_assertion_log"]["sha256"]:
                    raise ValueError("Accepted Jolt crash evidence changed")
                last_failed = (row_index, path, prior)
            continue
        if index >= 18:
            prospective.append((path, prior, successful, row_index))
            continue
        release_retry = pending_release is not None and last_failed is None
        if last_failed:
            if last_failed[0] == 5 and pending_release is None:
                if row_index != 1 or 1 not in latest_pass:
                    raise ValueError("Corrected limited Jolt Debug must precede failed Release retry")
                require_jolt_debug_prerequisite(prior, latest_pass[1], last_failed)
                pending_release = last_failed
            elif row_index == last_failed[0]:
                require_correction(prior, (last_failed[1], last_failed[2]), False)
            else:
                raise ValueError("Failed mandatory row must be corrected before advancing")
        elif pending_release:
            if row_index != pending_release[0]:
                raise ValueError("Pending failed Release row must follow corrected Jolt Debug")
            require_correction(prior, (pending_release[1], pending_release[2]), False)
        elif row_index < next_original and row_index in latest_pass:
            require_correction(prior, latest_pass[row_index], True)
        elif row_index != next_original:
            raise ValueError(f"Process differs from original Phase 28 row order: {path}")
        if release_retry:
            pending_release = None
        if successful:
            latest_pass[row_index] = (path, prior)
            if row_index == next_original:
                next_original += 1
            last_failed = None
        else:
            last_failed = (row_index, path, prior)
    if historical_debit != ({"Debug": 7, "Release": 11, "Editor": 0}, 7568, 240) or \
       last_failed is None or last_failed[0] != 10 or \
       last_failed[1].name != "run-Release-H_CONTACT-native-01":
        raise ValueError("Accepted 18-process Phase 28 history or pending Native contact drifted")
    required = (15, 14, 16, 11, 13, 10, 12)
    required_position = 0
    pending = None

    def admit_future(row_index: int, future_identity: dict,
                     successful: bool | None = None, path: Path | None = None) -> None:
        nonlocal required_position, pending
        if pending is not None:
            if row_index != pending[0]:
                raise ValueError("Failed prospective Phase 28 row must be corrected before advancing")
            require_correction(future_identity, (pending[1], pending[2]), False)
        elif required_position < len(required) and row_index == required[required_position]:
            if row_index == 10:
                require_correction(future_identity, (last_failed[1], last_failed[2]), False)
        elif row_index in latest_pass:
            require_correction(future_identity, latest_pass[row_index], True)
        else:
            raise ValueError("Next process is outside accepted Jolt Editor/contact priority order")
        if successful is not None:
            if successful:
                latest_pass[row_index] = (path, future_identity)
                if required_position < len(required) and row_index == required[required_position]:
                    required_position += 1
                pending = None
            else:
                pending = (row_index, path, future_identity)

    for path, prior, successful, row_index in prospective[:6]:
        admit_future(row_index, prior, successful, path)
    history = json.loads(CONTACT_HISTORY.read_text(encoding="utf-8"))["ledger"]
    by_path = {item["path"]: item for item in history}
    if len(observed) < 24 or len(by_path) != 24:
        raise ValueError("The 24-launch historical debit must be present before continuation")
    for _, path, prior, result, _ in observed[:24]:
        pinned = by_path[path.relative_to(ROOT).as_posix()]
        capture = result.get("capture")
        actual = capture["physics_ticks" if pinned["category"] == "Editor" else "ticks"] if capture else "UNKNOWN"
        if (sha(path / "result.json") != pinned["result_sha256"] or
            prior["executable"]["sha256"] != pinned["tested_executable_sha256"] or
            actual != pinned["actual_ticks"]):
            raise ValueError(f"Historical run differs from accepted 24-process ledger: {path}")

    completed = {}
    for _, path, prior, result, audit in observed[:24]:
        sig = (prior["config"], prior["case"], prior["backend"])
        completed[sig] = (path, prior, audit["status"] == "PASS")

    # The new amendment adds only four named Editor sessions and two corrected
    # replacements. Pin all forty earlier attempts; they never consume new slots.
    component_history = json.loads(COMPONENT_HISTORY.read_text(encoding="utf-8"))
    historical_rows = component_history["verified_ledger_records"]
    if len(observed) < 40 or len(historical_rows) != 40:
        raise ValueError("Component UI continuation requires the immutable forty-launch history")
    historical_by_path = {r["path"]: r for r in historical_rows}
    for _, path, prior, result, audit in observed[:40]:
        pin = historical_by_path.get(path.relative_to(ROOT).as_posix())
        if pin is None or sha(path / "inputs.json") != pin["inputs"]["sha256"] or \
           sha(path / "result.json") != pin["process_result"]["sha256"]:
            raise ValueError("A preserved pre-component launch changed")

    # The outline amendment preserves all forty-four attempts, including the two
    # consumed component replacements and both unknown-tick reservations.
    outline_history = json.loads(OUTLINE_HISTORY.read_text(encoding="utf-8"))
    outline_rows = outline_history["rows"]
    if len(observed) < 44 or len(outline_rows) != 44:
        raise ValueError("Selection outline continuation requires the immutable 44-launch history")
    outline_by_path = {r["path"]: r for r in outline_rows}
    for _, path, prior, result, audit in observed[:44]:
        pin = outline_by_path.get(path.relative_to(ROOT).as_posix())
        if pin is None or sha(path / "inputs.json") != pin["inputs"]["sha256"] or \
           sha(path / "result.json") != pin["result"]["sha256"]:
            raise ValueError("A preserved pre-outline launch changed")

    component_attempts = Counter()
    component_replacements = 0
    reserve_rows = json.loads(RESERVE_HISTORY.read_text(encoding="utf-8"))["rows"]
    if len(reserve_rows) != 47 or len(observed) < 47:
        raise ValueError("Completion reserve requires all 47 historical launches")
    for (_, path, prior, result, audit), pin in zip(observed[:47], reserve_rows):
        if (path.relative_to(ROOT).as_posix() != pin["path"] or
            any(sha(path / (key + ".json")) != pin[key] for key in ("inputs", "result", "audit"))):
            raise ValueError("A preserved pre-reserve launch changed")

    def admit_completion(candidate: dict) -> None:
        nonlocal component_replacements
        sig = (candidate["config"], candidate["case"], candidate["backend"])
        config, case, backend = sig
        role = candidate.get("editor_session")
        if role in COMPONENT_EDITOR_ROLES:
            planned_args(candidate)
            previous = completed.get(sig)
            if previous:
                require_correction(candidate, previous[:2], invalidated=previous[2])
                component_replacements += 1
                if component_replacements > campaign["component_replacements_max"]:
                    raise ValueError("All accepted component correction-only replacement slots are consumed")
            else:
                for required_role in COMPONENT_EDITOR_ROLES[:COMPONENT_EDITOR_ROLES.index(role)]:
                    required_case, required_backend, _, _ = EDITOR_ROLES[required_role]
                    if not completed.get(("Release", required_case, required_backend), (None, None, False))[2]:
                        raise ValueError("Evaluate and pass each preceding focused component session before advancing")
            # A completed role cannot spend an unused slot: require_correction above
            # must prove a changed workload and explicitly invalidate its prior PASS.
            component_attempts[role] += 1
            return
        if case.startswith("EDITOR_"):
            role = candidate.get("editor_session")
            if role in GENERAL_EDITOR_ROLES:
                planned_args(candidate)
                if role == "general-native" and not completed.get(("Release", "EDITOR_GENERAL_JOLT", "jolt"), (None, None, False))[2]:
                    raise ValueError("Complete primary Jolt general-object coverage before Native")
            elif role != "properties-" + backend and not (
                    backend == "jolt" and role in ("properties-default-jolt", "direct-default-jolt")):
                raise ValueError("Only affected property authoring sessions were requested")
            if backend == "native" and ("Release", "EDITOR_PROPERTIES_JOLT", "jolt") not in completed:
                raise ValueError("Qualify the Jolt Properties panel first")
        elif case in CONTACT:
            frozen = candidate.get("contact_fixture", {})
            versions = (2, 3, 4) if case == "CV_CONTACT" else (2, 3)
            if config != "Release" or frozen not in [contact_fixture_identity(v) for v in versions]:
                raise ValueError("A Release contact run requires the frozen versioned physical input")
            if backend == "native":
                for c in CONTACT:
                    required_input = (contact_fixture_identity(3) if c == "H_CONTACT" and
                                      frozen == contact_fixture_identity(4) else frozen)
                    prior_jolt = completed.get(("Release", c, "jolt"), (None, {}, False))
                    if prior_jolt[1].get("contact_fixture") != required_input:
                        raise ValueError("Measure the corresponding Jolt inputs before the Native comparison")
        elif sig not in completed:
            raise ValueError("Unadmitted completion workload")
        previous = completed.get(sig)
        if previous:
            require_correction(candidate, previous[:2], invalidated=previous[2])
        elif case.startswith("EDITOR_"):
            prior_properties = candidate.get("editor_session") in ("direct-default-jolt", *GENERAL_EDITOR_ROLES)
            previous_case = ("EDITOR_PROPERTIES_" if prior_properties else "EDITOR_") + backend.upper()
            existing = completed[("Release", previous_case, backend)]
            require_correction(candidate, existing[:2], invalidated=True)

    for path, prior, successful, row_index in prospective[6:]:
        if path not in [item[1] for item in observed[:40]] and prior.get("editor_session") not in COMPONENT_EDITOR_ROLES:
            raise ValueError("The component campaign admits no additional unrelated workload")
        admit_completion(prior)
        completed[(prior["config"], prior["case"], prior["backend"])] = (path, prior, successful)
    limits = campaign["accepted_limits"]
    if counts["Debug"] > limits["debug_processes_max"] or \
       counts["Release"] > limits["release_fixture_processes_max"] or \
       counts["Editor"] > limits["ordinary_editor_processes_max"] or \
       known_ticks + unknown_debit > limits["fixed_physics_ticks_max"]:
        raise ValueError("Phase 28 historical process/category/tick budget exceeded")
    signature = (identity.get("config"), identity.get("case"), identity.get("backend"))
    if identity.get("editor_session") not in COMPONENT_EDITOR_ROLES:
        raise ValueError("Only the four Owner-approved component sessions may consume the new launch resources")
    if signature not in by_signature:
        raise ValueError("Next process has no original Phase 28 row")
    row_index = by_signature[signature]
    admit_completion(identity)
    next_category = "Editor" if identity["case"].startswith("EDITOR_") else identity["config"]
    counts[next_category] += 1
    if counts["Debug"] > limits["debug_processes_max"] or \
       counts["Release"] > limits["release_fixture_processes_max"] or \
       counts["Editor"] > limits["ordinary_editor_processes_max"]:
        raise ValueError("Next Phase 28 category process cap exhausted")
    reserved = int(plan[row_index]["max_ticks"])
    debit = known_ticks + unknown_debit
    if reserved not in (240, 600) or debit + reserved > limits["fixed_physics_ticks_max"]:
        raise ValueError("Insufficient accepted aggregate Phase 28 ticks for full next-process reservation")
    return {"decision": campaign["campaign"], "decision_sha256": RESERVE_ACCEPTANCE_SHA256,
            "completed_processes": len(observed), "known_captured_ticks": known_ticks,
            "unknown_tick_debit": unknown_debit, "unknown_tick_processes": unknown_paths,
            "conservative_prior_debit": debit, "next_reserved_ticks": reserved,
            "remaining_after_reservation": limits["fixed_physics_ticks_max"]-debit-reserved,
            "original_row_index": row_index, "component_attempts": dict(component_attempts),
            "component_replacements_used_including_reservation": component_replacements}


def build(config: str, attempt: str) -> None:
    if config not in ("Debug", "Release"):
        raise ValueError("Invalid configuration")
    directory = safe(BASE / f"build-{config}-{attempt}")
    directory.mkdir(parents=True, exist_ok=False)
    before = inputs(config)
    write(directory / "inputs-before.json", before)
    props = qualification_profile()
    link_map = safe(qualified_bin(config, "RigidBodySimulation") / "phase28.map")
    canonical = [ROOT / f"bin/{config}/{project}/{project}.{suffix}" for project, suffix in
                 (("RigidBodySimulation", "exe"), ("GEngine", "lib"), ("Jolt", "lib"))]
    canonical_before = {p.relative_to(ROOT).as_posix(): sha(p) if p.is_file() else None
                        for p in canonical}
    if not MSBUILD.is_file():
        raise FileNotFoundError(MSBUILD)
    command = [MSBUILD, ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj",
               f"/p:Configuration={config}", "/p:Platform=x64",
               "/p:VCToolsVersion=14.44.35207",
               "/p:WindowsTargetPlatformVersion=10.0.26100.0",
               "/p:PreferredToolArchitecture=x64",
               f"/p:ForceImportBeforeCppTargets={props}", "/m:2", "/nr:false",
               "/nologo", "/t:Build", "/v:minimal",
               f"/bl:{directory / 'build.binlog'};ProjectImports=None"]
    result = invoke(command, directory, 1200, environment(), ROOT)
    after = inputs(config)
    result["inputs_unchanged"] = before == after
    result["profile"] = {"path": props.relative_to(ROOT).as_posix(), "sha256": sha(props)}
    result["canonical_products_unchanged"] = canonical_before == {
        p.relative_to(ROOT).as_posix(): sha(p) if p.is_file() else None for p in canonical}
    result["ordinary_postbuild_skipped"] = "postbuild.py config=" not in (
        (directory / "process.log").read_text(encoding="utf-8", errors="replace"))
    result["product"] = {}
    for p in [qualified_bin(config, "RigidBodySimulation") / "RigidBodySimulation.exe",
              qualified_bin(config, "GEngine") / "GEngine.lib",
              qualified_bin(config, "Jolt") / "Jolt.lib",
              qualified_bin(config, "glad") / "glad.lib"]:
        if p.is_file():
            result["product"][p.relative_to(ROOT).as_posix()] = sha(p)
    if result["exit_code"] == 0 and not result["timed_out"] and result["inputs_unchanged"] and \
       result["canonical_products_unchanged"] and result["ordinary_postbuild_skipped"]:
        result["staged_runtime"] = stage_qualified_runtime(config)
    result["link_map"] = {"path": str(link_map), "sha256": sha(link_map)} if link_map.is_file() else None
    write(directory / "result.json", result)
    print(json.dumps(result, indent=2))
    if result["exit_code"] or result["timed_out"] or not result["inputs_unchanged"] or \
       not result["canonical_products_unchanged"] or not result["ordinary_postbuild_skipped"] or \
       len(result["product"]) != 4 or not result["link_map"] or "staged_runtime" not in result:
        raise SystemExit(1)


def run(config: str, case: str, backend: str, attempt: str,
        correction: str | None = None) -> None:
    if config not in ("Debug", "Release") or backend not in ("native", "jolt"):
        raise ValueError("Invalid configuration or backend")
    if case not in SCENE:
        raise ValueError("Invalid admitted case")
    directory = safe(BASE / f"run-{config}-{case}-{backend}-{attempt}")
    data = safe(directory / "data")
    if directory.exists():
        raise FileExistsError(directory)
    exe = qualified_bin(config, "RigidBodySimulation") / "RigidBodySimulation.exe"
    if not exe.is_file():
        raise FileNotFoundError(exe)
    e = environment()
    e["GENGINE_PHASE28_OUTPUT"] = str(data)
    e["GENGINE_PHASE28_CASE"] = case
    e["GENGINE_ASSET_ROOT"] = str(safe(ROOT / "bin/phase28-qualification" / config / "assets"))
    if config == "Debug":
        e["GENGINE_PHASE28_DEBUG"] = "1"
    before = runtime_inputs(config, isolated=True)
    runtime_dlls = {p.relative_to(ROOT).as_posix(): sha(p) for p in
                    qualified_bin(config, "RigidBodySimulation").glob("*.dll")}
    identity = {
        "sources": before, "executable": {"path": str(exe), "sha256": sha(exe)},
        "runtime_dlls": runtime_dlls,
        "matrix_sha256": MATRIX_SHA256, "case": case, "backend": backend,
        "config": config, "utc": dt.datetime.now(dt.timezone.utc).isoformat(),
    }
    if case in CONTACT:
        version = 4 if case == "CV_CONTACT" else 3
        e["GENGINE_PHASE28_CONTACT_FIXTURE"] = f"v{version}"
        identity["contact_fixture"] = contact_fixture_identity(version)
    if correction:
        identity["correction"] = correction_identity(correction)
    if config == "Release" and case in CONTACT and backend == "native":
        identity["native_contact_stages"] = "P28_NATIVE_CONTACT_STAGES_01"
        e["GENGINE_PHASE28_NATIVE_CONTACT_STAGES"] = "1"
    identity["budget_reservation"] = check_run_budget(identity)
    directory.mkdir(parents=True, exist_ok=False)
    data.mkdir(exist_ok=False)
    cwd = qualified_runtime(config)
    cwd.mkdir(parents=True, exist_ok=True)
    identity["initial_imgui"] = archive_initial_imgui(directory, cwd)
    write(directory / "inputs.json", identity)
    result = invoke([exe, *planned_args(identity)],
                    directory, 180, e, cwd)
    result["inputs_unchanged"] = before == runtime_inputs(config, isolated=True)
    result["capture"] = json.loads((data / "result.json").read_text()) if (data / "result.json").exists() else None
    for name in ("GEngine.log", "imgui.ini"):
        source = cwd / name
        if source.is_file():
            with (directory / name).open("xb") as target_file:
                target_file.write(source.read_bytes())
    write(directory / "result.json", result)
    print(json.dumps(result, indent=2))
    target = 240 if config == "Debug" else 600
    if result["exit_code"] or result["timed_out"] or not result["inputs_unchanged"] or \
            not result["capture"] or not result["capture"].get("complete") or \
            result["capture"].get("ticks") != target:
        raise SystemExit(1)


def frozen_component_plan(value: str, role: str, identity: dict | None = None) -> tuple[dict, dict]:
    """Validate one create-only, pre-launch script/coverage/expectation packet."""
    path = safe(ROOT / value)
    if (not any(path.is_relative_to(BASE / folder) for folder in ("component-ui-02", "selection-highlight-02")) or
        path.suffix != ".json" or not path.is_file()):
        raise ValueError("Component session plan must be a frozen admitted component/outline JSON")
    plan = json.loads(path.read_text(encoding="utf-8"))
    if (plan.get("schema") != 1 or plan.get("phase") not in (28, "28") or plan.get("role") != role or
        plan.get("limits") != {"max_frames": 1800, "physics_ticks": 600, "seconds": 180}):
        raise ValueError("Component session plan identity or unchanged limits mismatch")

    def bound(key: str) -> dict:
        binding = plan[key]
        source = safe(ROOT / binding["path"])
        if not source.is_file() or sha(source) != binding.get("sha256"):
            raise ValueError(f"Frozen component plan {key} binding drifted")
        return json.loads(source.read_text(encoding="utf-8"))

    archive = bound("source_archive")
    build = bound("build_result")
    assessment_binding = plan.get("coverage", {}).get("assessment", {})
    assessment_path = safe(ROOT / assessment_binding.get("path", ""))
    if not assessment_path.is_file() or sha(assessment_path) != assessment_binding.get("sha256"):
        raise ValueError("Component inventory binding is missing or drifted")
    assessment = json.loads(assessment_path.read_text(encoding="utf-8"))
    inventory = {row["row_id"] for row in assessment.get("inventory", [])}
    covered = plan.get("coverage", {}).get("row_ids")
    if len(inventory) != 600 or not isinstance(covered, list) or not covered or \
       len(covered) != len(set(covered)) or not set(covered) <= inventory:
        raise ValueError("Component coverage must name unique rows from the independently audited 600-row inventory")
    script = plan.get("script", {}).get("actions")
    ops = {"Click", "Open", "Type", "Choose", "Inspect", "Expect", "Image", "RoomImage", "UiImage", "Keyboard",
           "Viewport", "PickMesh", "DragMesh", "Negative", "InjectFailure", "End",
           "Pointer", "Wheel", "Resize", "Dpi", "NativeImage", "Wait", "Stimulus"}
    if not isinstance(script, list) or not script or script[-1].get("op") != "End" or \
       any(not isinstance(action, dict) or set(action) != {"op", "key", "value"} or
           action["op"] not in ops or any(action[k] is not None and (not isinstance(action[k], str) or not action[k] or
                  any(c in action[k] for c in "\t\r\n"))
                                        for k in ("key", "value")) for action in script):
        raise ValueError("Exact ordered SDL/ImGui op/key/value script is required")
    if any(action["op"] == "End" for action in script[:-1]):
        raise ValueError("Only the final action may end a component session")
    pacing = {"Click": 4, "Open": 1, "Type": 9, "Expect": 1, "Image": 8, "RoomImage": 8,
              "UiImage": 8, "Keyboard": 1, "Viewport": 5, "PickMesh": 5, "DragMesh": 8,
              "Negative": 1, "InjectFailure": 1, "Inspect": 1, "End": 1,
              "Pointer": 12, "Wheel": 12, "Resize": 16, "Dpi": 16,
              "NativeImage": 8, "Stimulus": 8}
    minimum_frames = 0
    for action in script:
        if action["op"] == "Choose":
            if action["value"] is None or not action["value"].isdigit() or not 0 <= int(action["value"]) <= 16:
                raise ValueError("Choose requires a bounded nonnegative option index")
            minimum_frames += 12 + 2 * int(action["value"])
        elif action["op"] == "Wait":
            if action["key"] not in ("stationary", "idle", "settle") or action["value"] is None or \
               not action["value"].isdigit() or not 8 <= int(action["value"]) <= 40:
                raise ValueError("Outline Wait requires a frozen bounded8..40 frame observation interval")
            minimum_frames += int(action["value"])
        else:
            if action["op"] == "Inspect" and action["value"] not in ("enabled", "disabled", "visible"):
                raise ValueError("Inspect must freeze the expected actual widget capability")
            if action["op"] == "Pointer" and (not action["key"] or action["value"] not in ("hover", "click", "ctrl", "clear", "outside")):
                raise ValueError("Pointer requires an exact target and bounded real input operation")
            if action["op"] == "Wheel" and (action["key"] != "viewport" or action["value"] not in ("-3", "-2", "-1", "1", "2", "3")):
                raise ValueError("Wheel requires a frozen nonzero bounded SDL camera gesture")
            if action["op"] == "Resize" and (action["key"] != "window" or action["value"] not in ("1280x800-offset", "restore")):
                raise ValueError("Resize requires the exact ordinary primary-window stimulus")
            if action["op"] == "Dpi" and (action["key"] != "viewport" or action["value"] not in ("1", "2")):
                raise ValueError("Dpi requires actual normal or2x framebuffer sizing")
            if action["op"] == "Stimulus" and (action["key"] != "isolated-render-only" or action["value"] not in ("opaque", "masked", "transparent", "occluded", "restore")):
                raise ValueError("Only the exact isolated render-only coverage stimuli are admitted")
            if action["op"] == "NativeImage" and (not action["key"] or action["value"] is not None):
                raise ValueError("NativeImage requires an explicit unscaled viewport capture name")
            minimum_frames += pacing[action["op"]]
    if minimum_frames > 1800 or plan.get("minimum_action_frames") != minimum_frames:
        raise ValueError("Exact component script exceeds its frame cap or has stale pacing accounting")
    assertions = plan.get("expected_assertions")
    expects = [a["key"] for a in script if a["op"] == "Expect"]
    if not isinstance(assertions, list) or not assertions or assertions != expects:
        raise ValueError("Frozen readback assertions must occur in the actual interaction script")
    values = plan.get("expected_values")
    if not isinstance(values, dict) or set(values) != set(assertions) or \
       any(not (isinstance(v, dict) and v) and not (isinstance(v, list) and
           len(v) == assertions.count(key) and all(isinstance(item, dict) and item for item in v))
           for key, v in values.items()):
        raise ValueError("Every expected assertion needs frozen property readback values")
    by_tick = plan.get("expected_values_by_tick", {})
    if by_tick:
        velocity_key = "component.readback.body-velocity"
        if (role != "component-general-jolt" or not isinstance(by_tick, dict) or
            set(by_tick) != {velocity_key} or assertions.count(velocity_key) != 1 or
            by_tick[velocity_key] != component_velocity_tick_packets() or
            plan.get("numeric_tolerance") != .0001):
            raise ValueError("Runtime velocity oracle requires the exact frozen Dynamic Jolt ticks 1..6")
        common = values[velocity_key]
        if isinstance(common, list):
            common = common[0]
        if (set(common) & {"tick", "provider.linear_velocity", "provider.angular_velocity"} or
            common.get("scene") != "sphere-box-stack" or common.get("backend") != "jolt" or
            common.get("selected") != "wood_sphere_0" or common.get("provider.type") != 1):
            raise ValueError("Tick-indexed runtime velocity requires the exact selected Dynamic sphere context")
    elif not isinstance(by_tick, dict):
        raise ValueError("Tick-indexed expected values must be an object")
    events = plan.get("required_events")
    if not isinstance(events, dict) or not {"edited", "clicked", "observed"} <= set(events) or \
       not set(events) <= {"edited", "clicked", "observed", "chosen", "enabled-observed", "disabled-observed", "visible-observed",
                          "outline-pointer", "outline-wheel", "outline-resized", "outline-dpi", "native-viewport-png", "outline-wait", "outline-stimulus"} or \
       any(not isinstance(v, dict) or any(not isinstance(k, str) or not isinstance(n, int) or
               isinstance(n, bool) or n < 1 for k, n in v.items()) for v in events.values()):
        raise ValueError("Required real interaction event counts are not frozen")
    screenshots = plan.get("screenshots")
    if not isinstance(screenshots, list) or not screenshots or len(screenshots) != len(set(screenshots)) or \
       any(not isinstance(name, str) or Path(name).name != name or name.endswith(".png") or
           name not in {a["key"] for a in script if a["op"] == "UiImage"} for name in screenshots):
        raise ValueError("Required screenshots must name actual full-Editor script captures")
    native = plan.get("native_screenshots", [])
    if (not isinstance(native, list) or len(native) != len(set(native)) or
        native != [a["key"] for a in script if a["op"] == "NativeImage"] or
        any(not isinstance(name, str) or Path(name).name != name or name.endswith(".png") or name in screenshots for name in native)):
        raise ValueError("Native screenshot names must exactly match unscaled viewport input operations")
    tolerance = plan.get("numeric_tolerance", 0)
    if isinstance(tolerance, bool) or not isinstance(tolerance, (int, float)) or \
       not math.isfinite(tolerance) or tolerance < 0 or tolerance > .001:
        raise ValueError("Authoring readback tolerance must be explicitly bounded; Physics gates are separate")
    if identity is not None:
        files = archive.get("files", {})
        outline = outline_authority()
        historical = dt.datetime.fromisoformat(identity["utc"]) < dt.datetime.fromisoformat(outline["recorded_utc"])
        declared = effective_paths()
        required_paths = set(declared["source_paths"] + declared["runtime_paths"] + declared["supporting_paths"])
        if historical:
            required_paths -= set(outline["additional_source_paths"])
        mismatches = [p for p, digest in files.items() if identity["sources"]["sources"].get(p) != digest]
        if mismatches == ["tools/pre_editor_phase28_qualification.py"]:
            identity["administrative_runner_bridge"] = administrative_runner_bridge(archive, identity)
            mismatches = []
        if set(files) != required_paths or mismatches:
            raise ValueError("Frozen component source archive does not match current runtime inputs")
        if (build.get("exit_code") != 0 or build.get("guard") is not None or
            not build.get("inputs_unchanged") or not build.get("canonical_products_unchanged")):
            raise ValueError("Component candidate lacks a successful affected guarded build")
        executable = safe(Path(identity["executable"]["path"]))
        if build.get("product", {}).get(executable.relative_to(ROOT).as_posix()) != identity["executable"]["sha256"]:
            raise ValueError("Component candidate executable differs from its frozen successful build")
        build_input_path = safe(ROOT / plan["build_result"]["path"]).parent / "inputs-before.json"
        built_sources = json.loads(build_input_path.read_text(encoding="utf-8"))
        if any(built_sources.get(p) != digest for p, digest in files.items()):
            raise ValueError("Component source archive differs from the successful build input snapshot")
    return plan, {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path)}


def component_script_input(plan: dict, binding: dict) -> dict:
    path = safe(BASE / "component-ui-02/frozen-scripts" / f"{plan['role']}-{binding['sha256']}.tsv")
    payload = "".join("\t".join(action[k] or "" for k in ("op", "key", "value")) + "\n"
                      for action in plan["script"]["actions"])
    if path.exists():
        if path.read_text(encoding="utf-8") != payload:
            raise ValueError("Frozen SDL/ImGui script input changed")
    else:
        write(path, payload)
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path)}


def editor(role: str, attempt: str, correction: str | None = None, session_plan: str | None = None) -> None:
    if role not in EDITOR_ROLES:
        raise ValueError("Invalid ordinary Editor session role")
    config = "Release"
    case, backend, scene, explicit_cli = EDITOR_ROLES[role]
    directory = safe(BASE / f"run-{config}-{case}-{backend}-{attempt}")
    data = safe(directory / "data")
    if directory.exists():
        raise FileExistsError(directory)
    exe = qualified_bin(config, "RigidBodySimulation") / "RigidBodySimulation.exe"
    if not exe.is_file():
        raise FileNotFoundError(exe)
    before = runtime_inputs(config, isolated=True)
    runtime_dlls = {p.relative_to(ROOT).as_posix(): sha(p) for p in
                    qualified_bin(config, "RigidBodySimulation").glob("*.dll")}
    identity = {"sources": before, "executable": {"path": str(exe), "sha256": sha(exe)},
                "runtime_dlls": runtime_dlls, "matrix_sha256": MATRIX_SHA256,
                "case": case, "backend": backend, "config": config,
                "editor_session": role, "cli_backend_explicit": explicit_cli,
                "utc": dt.datetime.now(dt.timezone.utc).isoformat()}
    if correction:
        identity["correction"] = correction_identity(correction)
    if role in COMPONENT_EDITOR_ROLES:
        if not session_plan:
            raise ValueError("Freeze and supply --session-plan before each component session")
        plan, identity["component_plan"] = frozen_component_plan(session_plan, role, identity)
        identity["component_script"] = component_script_input(plan, identity["component_plan"])
    elif session_plan:
        raise ValueError("A component session plan cannot be attached to a historical role")
    identity["budget_reservation"] = check_run_budget(identity)
    directory.mkdir(parents=True, exist_ok=False)
    data.mkdir(exist_ok=False)
    fresh_default = role in ("properties-default-jolt", "direct-default-jolt", *GENERAL_EDITOR_ROLES, *COMPONENT_EDITOR_ROLES)
    cwd = qualified_runtime(config, directory.name if fresh_default else None)
    cwd.mkdir(parents=True, exist_ok=not fresh_default)
    identity["initial_imgui"] = archive_initial_imgui(directory, cwd)
    write(directory / "inputs.json", identity)
    e = environment()
    e["GENGINE_PHASE28_OUTPUT"] = str(data)
    e["GENGINE_PHASE28_CASE"] = "EDITOR_JOLT" if backend == "jolt" else "EDITOR_NATIVE"
    e["GENGINE_PHASE28_EDITOR_SESSION"] = role
    e["GENGINE_PHASE28_MODE"] = "editor"
    if role in COMPONENT_EDITOR_ROLES:
        e["GENGINE_PHASE28_EDITOR_SCRIPT"] = str(safe(ROOT / identity["component_script"]["path"]))
    e["GENGINE_ASSET_ROOT"] = str(safe(ROOT / "bin/phase28-qualification/Release/assets"))
    result = invoke([exe, *planned_args(identity)],
                    directory, 180, e, cwd)
    result["inputs_unchanged"] = before == runtime_inputs(config, isolated=True)
    if role in COMPONENT_EDITOR_ROLES:
        result["component_inputs_unchanged"] = all(sha(safe(ROOT / identity[k]["path"])) == identity[k]["sha256"]
                                                  for k in ("component_plan", "component_script"))
        result["inputs_unchanged"] = result["inputs_unchanged"] and result["component_inputs_unchanged"]
    result["capture"] = json.loads((data / "result.json").read_text()) if (data / "result.json").exists() else None
    result["images"] = {p.name: {"sha256": sha(p), "dimensions": png_dimensions(p)}
                        for p in data.glob("*.png")}
    for name in ("GEngine.log", "imgui.ini"):
        source = cwd / name
        if source.is_file():
            with (directory / name).open("xb") as target_file:
                target_file.write(source.read_bytes())
    write(directory / "result.json", result)
    print(json.dumps(result, indent=2))
    capture = result["capture"] or {}
    if result["exit_code"] or result["timed_out"] or not result["inputs_unchanged"] or \
            not capture.get("complete") or capture.get("physics_ticks", 601) > 600 or \
            capture.get("images") != (5 if role in ("fresh-jolt", "saved-native") else 0) or \
            capture.get("room_images") != (12 if role == "override-native" else 0):
        raise SystemExit(1)


def rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as source:
        return list(csv.DictReader(source))


def png_dimensions(path: Path) -> tuple[int, int] | None:
    if not path.is_file():
        return None
    header = path.read_bytes()[:24]
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        return None
    return int.from_bytes(header[16:20], "big"), int.from_bytes(header[20:24], "big")


def stat(values: list[float]) -> dict[str, float]:
    a = sorted(values)
    if not a:
        raise ValueError("Empty measurement series")
    return {"n": len(a), "mean": statistics.mean(a), "median": statistics.median(a),
            "p95": a[math.ceil(.95 * len(a)) - 1],
            "p99": a[math.ceil(.99 * len(a)) - 1], "max": a[-1],
            "rolling60_mean_max": max(statistics.mean(values[i:i + 60]) for i in range(len(values) - 59))
            if len(values) >= 60 else None}


def vec(row: dict[str, str], keys: tuple[str, str, str]) -> tuple[float, float, float]:
    return tuple(float(row[k]) for k in keys)


def dot(a: tuple[float, ...], b: tuple[float, ...]) -> float:
    return sum(x*y for x, y in zip(a, b))


def norm(a: tuple[float, ...]) -> float:
    return math.sqrt(dot(a, a))


def quat(row: dict[str, str]) -> tuple[float, float, float, float]:
    return tuple(float(row[k]) for k in ("qw", "qx", "qy", "qz"))


def qangle(a: tuple[float, ...], b: tuple[float, ...]) -> float:
    return 2 * math.acos(min(1.0, abs(dot(a, b)/(norm(a)*norm(b)))))


def rotated_axes(q: tuple[float, float, float, float]) -> tuple[tuple[float, float, float], ...]:
    w, x, y, z = (v / norm(q) for v in q)
    return ((1-2*(y*y+z*z), 2*(x*y+w*z), 2*(x*z-w*y)),
            (2*(x*y-w*z), 1-2*(x*x+z*z), 2*(y*z+w*x)),
            (2*(x*z+w*y), 2*(y*z-w*x), 1-2*(x*x+y*y)))


def cross(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, float, float]:
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def box_floor_overlap(bar: dict[str, str], scale: float, *,
                      floor_center=(0., 0., 0.), floor_half=(5., .5, 5.),
                      floor_orientation=(1., 0., 0., 0.)) -> float:
    """Independent geometric OBB/finite-floor SAT; no manifold or slop input."""
    axes_a = rotated_axes(quat(bar))
    axes_b = rotated_axes(floor_orientation)
    half_a = (.15*scale, scale, .15*scale)
    half_b = floor_half
    delta = tuple(x-c for x,c in zip(vec(bar, ("x", "y", "z")), floor_center))
    candidates = [*axes_a, *axes_b, *(cross(a, b) for a in axes_a for b in axes_b)]
    smallest = math.inf
    for axis in candidates:
        size = norm(axis)
        if size < 1e-12:
            continue
        unit = tuple(v/size for v in axis)
        ra = sum(h*abs(dot(unit, a)) for h, a in zip(half_a, axes_a))
        rb = sum(h*abs(dot(unit, b)) for h, b in zip(half_b, axes_b))
        overlap = ra+rb-abs(dot(delta, unit))
        if overlap <= 0:
            return 0.
        smallest = min(smallest, overlap)
    return smallest


def slope_of_bins(values: list[float]) -> float:
    if len(values) % 60 or len(values) < 120:
        raise ValueError("Expected at least two exact 60-tick bins")
    means = [statistics.mean(values[i:i+60]) for i in range(0, len(values), 60)]
    center = (len(means)-1)/2
    return sum((i-center)*(v-statistics.mean(means)) for i, v in enumerate(means)) / \
        sum((i-center)**2 for i in range(len(means)))


def aligned(n: int, alignment: int) -> int:
    return 0 if n == 0 else alignment * math.ceil(n/alignment)


def source_arena_sum(sizes: list[int]) -> int:
    pointer, u32, alignment, step, ccd, contact, body_pair, body_id = sizes
    c, b, n, j, islands, t = 16384, 1, 1, 1, 1, 16384
    requests = [step, c*body_pair, n*pointer, c*contact, c*u32, c*u32,
                j*u32, b*body_id, (b+1)*u32, j*u32, (islands+1)*u32,
                t*u32, (islands+1)*u32, islands*u32, islands*u32,
                islands, b*ccd, b*u32, b*pointer]
    return sum(aligned(x, alignment) for x in requests)


def verify_allocation_evidence(directory: Path, record: dict, result: dict,
                               heap: list[dict], arena: list[dict], path_arg: str) -> dict:
    """Bind one reviewed source attribution to every event of this exact process."""
    path = safe(ROOT / path_arg)
    if path.parent != directory or not path.is_file():
        raise ValueError("Allocation evidence must be an existing run-local file")
    evidence = json.loads(path.read_text(encoding="utf-8"))
    run_name = directory.relative_to(ROOT).as_posix()
    expected_raw = {
        "heap_csv_sha256": sha(directory / "data/heap.csv"),
        "arena_csv_sha256": sha(directory / "data/arena.csv"),
        "result_json_sha256": sha(directory / "result.json"),
        "original_audit_json_sha256": sha(directory / "audit.json"),
    }
    if evidence.get("phase") not in (28, "28") or evidence.get("run") != run_name or \
       evidence.get("raw") != expected_raw or \
       evidence.get("executable", {}).get("sha256") != record["executable"]["sha256"] or \
       safe(ROOT / evidence.get("executable", {}).get("path", "")) != \
       safe(Path(record["executable"]["path"])):
        raise ValueError("Allocation evidence does not bind the original run and executable")
    mapping = evidence.get("map", {})
    map_path = safe(ROOT / mapping.get("path", ""))
    qualified_map = safe(Path(record["executable"]["path"]).parent / "phase28.map")
    if not map_path.is_file() or not (map_path.is_relative_to(BASE) or
                                     map_path == qualified_map) or \
       sha(map_path) != mapping.get("sha256"):
        raise ValueError("Allocation evidence does not bind the Release linker map")
    preferred = int(mapping["preferred_target_sizes_va"], 16)
    runtime = int(mapping["runtime_target_sizes_va"])
    delta = int(mapping["aslr_delta"], 16)
    map_lines = [line for line in map_path.read_text(encoding="utf-8", errors="replace").splitlines()
                 if "g_Phase27TargetSizes" in line]
    if len(map_lines) != 1 or f"{preferred:016X}" not in map_lines[0].upper() or \
       runtime != int(result["capture"]["target_sizes_runtime_address"]) or \
       runtime - preferred != delta:
        raise ValueError("Allocation evidence ASLR anchor does not match the process and map")
    bounds = evidence.get("source_bounds", {})
    for key, digest_key in (("path", "sha256"),
                            ("compiled_target_sizes_path", "compiled_target_sizes_sha256")):
        source = safe(ROOT / bounds.get(key, ""))
        if not source.is_file() or sha(source) != bounds.get(digest_key):
            raise ValueError("Allocation source bound or compiled sizes changed")
    if bounds.get("actual_target_sizes") != result["capture"].get("target_sizes"):
        raise ValueError("Allocation evidence compiled target sizes do not match capture")
    sources = bounds.get("source_sha256", {})
    if not sources or any(not safe(ROOT / name).is_file() or
                          sha(safe(ROOT / name)) != digest for name, digest in sources.items()):
        raise ValueError("Allocation evidence source identity changed")

    def check_site_source(site: dict) -> None:
        source_path, separator, line = site.get("source", "").rpartition(":")
        if not separator or source_path not in sources or not line.isdecimal() or \
           int(line) < 1 or int(line) > len((ROOT / source_path).read_text(encoding="utf-8").splitlines()):
            raise ValueError("Unknown allocation source site")

    heap_expected: Counter = Counter()
    heap_site_by_call = {}
    for site in evidence.get("heap_sites", []):
        check_site_source(site)
        if site.get("category") != "shared_nonjoint" or not site.get("compiled_symbol") or \
           int(site["runtime_caller"]) != int(site["preferred_va"], 16) + delta or \
           int(site["count"]) <= 0:
            raise ValueError("Heap site is not a bound shared source call")
        key = (int(site["runtime_caller"]), site["operation"], int(site["requested_bytes"]))
        heap_expected[key] += int(site["count"])
        heap_site_by_call[key] = site
    observed: Counter = Counter()
    outstanding = {}
    temporary_pairs = 0
    temporary_pairs_by_size: Counter = Counter()
    quad_source = "external/jolt/Jolt/Physics/Collision/BroadPhase/QuadTree.cpp"
    quad_temporary_sites = {
        4: (f"{quad_source}:300", f"{quad_source}:391", "QuadTree::UpdatePrepare"),
        8: (f"{quad_source}:300", f"{quad_source}:391", "QuadTree::UpdatePrepare"),
        32: (f"{quad_source}:544", f"{quad_source}:654", "QuadTree::BuildTree"),
    }
    contact_fixture = record["case"] in ("H_CONTACT", "CV_CONTACT") and \
        json.loads((directory / "data/fixture.json").read_text(encoding="utf-8")).get("floor") is True
    for event in heap:
        caller, pointer, requested = (int(event[k]) for k in ("caller", "pointer", "requested"))
        operation = "reserve" if int(event["reserve"]) else "free"
        key = (caller, operation, requested)
        if key not in heap_expected or pointer == 0:
            raise ValueError("Unattributed or null JPH heap event")
        observed[key] += 1
        if operation == "reserve":
            if requested <= 0 or pointer in outstanding:
                raise ValueError("Invalid or duplicate JPH heap reserve")
            outstanding[pointer] = (event["segment"], int(event["tick"]), caller, requested)
        else:
            prior = outstanding.pop(pointer, None)
            if prior is None or requested != 0 or \
               (event["segment"], int(event["tick"])) != prior[:2]:
                raise ValueError("JPH temporary heap pointer or lifetime mismatch")
            size = prior[3]
            reserve_site = heap_site_by_call.get((prior[2], "reserve", size), {})
            free_site = heap_site_by_call.get(key, {})
            rule = quad_temporary_sites.get(size)
            if rule is None or (size in (8, 32) and not contact_fixture) or \
               reserve_site.get("source") != rule[0] or \
               free_site.get("source") != rule[1] or \
               reserve_site.get("compiled_symbol") != rule[2] or \
               free_site.get("compiled_symbol") != rule[2]:
                raise ValueError("JPH temporary heap source site or size differs from proved QuadTree pair")
            temporary_pairs += 1
            temporary_pairs_by_size[size] += 1
    if observed != heap_expected:
        raise ValueError("JPH heap caller, size, or count differs from source attribution")
    heap_summary = evidence.get("heap_observation", {})
    remaining = heap_summary.get("remaining_within_Update_observer", {})
    expected_pair_sizes = heap_summary.get("temporary_pairs_by_requested_bytes")
    if expected_pair_sizes is None:
        expected_pair_sizes = {"4": heap_summary.get("matched_quad_temporary_allocate_free_pairs")}
    if len(heap) != heap_summary.get("total_rows") or \
       temporary_pairs != heap_summary.get("matched_quad_temporary_allocate_free_pairs") or \
        {str(size): count for size, count in temporary_pairs_by_size.items()} != expected_pair_sizes or \
       len(outstanding) != remaining.get("count") or \
       any(value[3] != remaining.get("each_bytes") or value[3] != 229376 for value in outstanding.values()) or \
       heap_summary.get("duplicate_or_unmatched_pointer_events") != 0 or \
       heap_summary.get("joint_induced_heap_calls") != 0 or \
       heap_summary.get("joint_induced_heap_bytes") != 0 or \
       len({value[0] for value in outstanding.values()}) != len(outstanding):
        raise ValueError("JPH heap lifecycle or joint attribution differs from source proof")

    arena_expected: Counter = Counter()
    for site in evidence.get("arena_sites", []):
        check_site_source(site)
        if not site.get("source_site") or \
           int(site["runtime_caller"]) != int(site["preferred_va"], 16) + delta or \
           int(site["reserve_count"]) <= 0:
            raise ValueError("Arena site is not bound to the linker map")
        key = (int(site["runtime_caller"]), int(site["requested_bytes"]), int(site["aligned_bytes"]))
        arena_expected[key] += int(site["reserve_count"])
    arena_observed = Counter((int(e["caller"]), int(e["requested"]), int(e["aligned"]))
                             for e in arena if int(e["reserve"]) and int(e["requested"]) > 0)
    arena_summary = evidence.get("arena_observation", {})
    if arena_observed != arena_expected or len(arena) != arena_summary.get("total_rows") or \
       sum(int(e["reserve"]) for e in arena) != arena_summary.get("reserve_events") or \
       sum(not int(e["reserve"]) for e in arena) != arena_summary.get("free_events") or \
       max((int(e["used"]) for e in arena), default=0) != arena_summary.get("actual_peak_used_bytes") or \
       arena_summary.get("duplicate_or_unmatched_pointer_events") != 0 or \
       arena_summary.get("outstanding_after_each_completed_Update") != 0:
        raise ValueError("Arena raw stream differs from source attribution")
    if not evidence.get("source_proof") or not evidence.get("reuse_rule"):
        raise ValueError("Allocation evidence lacks source proof and per-run reuse boundary")
    return {"path": run_name + "/" + path.name, "sha256": sha(path),
            "heap_events_attributed": len(heap), "shared_job_pages": len(outstanding),
            "temporary_pointer_pairs": temporary_pairs,
            "arena_reservations_attributed": sum(arena_observed.values())}


def native_contact_allocation_proof(data: Path, samples: list[dict[str, str]]) -> dict:
    stage_names = ("Begin", "Wake", "Gravity", "Broadphase", "Narrowphase", "WarmStart",
                   "Islands", "Solve", "Integration", "Correction", "Sleep", "End")
    trace = rows(data / "native-contact-stages.csv")
    physical = [r for r in samples if r["segment"] == "normal" and int(r["tick"]) > 0]
    if len(physical) != 600 or len(trace) != len(physical) * len(stage_names):
        return {"complete": False, "reason": "Missing Native tick or stage observations"}
    totals = {s: {"calls": 0, "bytes": 0} for s in stage_names}
    failures = []
    for index, sample in enumerate(physical):
        tick = index + 1
        prior = None
        for stage, name in enumerate(stage_names):
            row = trace[index * len(stage_names) + stage]
            label = f"tick {tick} {name}"
            if (int(sample["tick"]) != tick or int(row["tick"]) != tick or int(row["stage"]) != stage):
                return {"complete": False, "reason": f"Missing, duplicate or reordered boundary: {label}"}
            cur = {k: int(row[k]) for k in ("calls", "bytes", "epa", "queries", "storage")}
            if any(v < 0 for v in cur.values()):
                return {"complete": False, "reason": f"Negative cumulative value: {label}"}
            calls = cur["calls"] - (prior["calls"] if prior else 0)
            size = cur["bytes"] - (prior["bytes"] if prior else 0)
            if calls < 0 or size < 0 or (prior and any(cur[k] < prior[k] for k in ("epa", "queries"))):
                return {"complete": False, "reason": f"Counter regressed: {label}"}
            totals[name]["calls"] += calls
            totals[name]["bytes"] += size
            if name == "Broadphase":
                known = calls == int(sample["broadphase_cpp_calls"]) and size == int(sample["broadphase_cpp_bytes"])
            elif not calls and not size:
                known = True
            elif name in ("Narrowphase", "Integration"):
                # In this three-body topology only bar/floor can enter a geometry query.
                # A first retained contact/manifold buffer is included in the Narrowphase
                # stage; Integration has no angular-joint allocation or point-joint TOI path.
                epa = cur["epa"] > prior["epa"]
                first_buffer = (name == "Narrowphase" and calls == 1 and
                                0 < size == cur["storage"] - prior["storage"])
                known = calls > 0 and size > 0 and cur["queries"] > prior["queries"] and (epa or first_buffer)
            else:
                known = False
            if not known and len(failures) < 10:
                failures.append({"boundary": label, "calls": calls, "bytes": size})
            prior = cur
        if prior["calls"] != int(sample["cpp_calls"]) or prior["bytes"] != int(sample["cpp_bytes"]):
            return {"complete": False, "reason": f"Stage counters do not span whole Native Update at tick {tick}"}
    return {"complete": not failures, "stages": totals, "first_unknowns": failures,
            "sample_count": len(trace), "raw_sha256": sha(data / "native-contact-stages.csv")}


def audit(run_directory: str, output: str | None = None,
          allocation_evidence: str | None = None) -> None:
    directory = safe(ROOT / run_directory)
    if not directory.is_dir() or not directory.is_relative_to(BASE):
        raise ValueError("Expected an existing Phase 28 run directory")
    audit_path = safe(ROOT / output) if output else directory / "audit.json"
    if output:
        if audit_path.parent != directory or not audit_path.name.startswith(("audit-allocation-", "audit-attribution-")) or \
           audit_path.suffix != ".json" or allocation_evidence is None:
            raise ValueError("Supplemental allocation audit needs a create-only run-local output and evidence")
    elif allocation_evidence is not None:
        raise ValueError("Allocation evidence requires a separate supplemental audit output")
    if audit_path.exists():
        raise FileExistsError(audit_path)
    record = json.loads((directory / "inputs.json").read_text())
    data = directory / "data"
    result = json.loads((directory / "result.json").read_text())
    if record["case"].startswith("EDITOR_"):
        if output:
            raise ValueError("Heap source attribution supplement applies only to physical Jolt runs")
        audit_editor(directory, record, data, result)
        return
    if output and (record["backend"] != "jolt" or not (directory / "audit.json").is_file()):
        raise ValueError("Supplemental allocation evidence requires a preserved Jolt audit")
    checks: list[dict] = []

    def check(name: str, ok: bool, actual: object, criterion: str,
              unknown_on_false: bool = False) -> None:
        checks.append({"check": name, "result": "PASS" if ok else
                       "INCOMPLETE" if unknown_on_false else "FAIL",
                       "actual": actual, "criterion": criterion})

    if result["exit_code"] or result["timed_out"] or not result["capture"]:
        check("process", False, result, "successful bounded RBS process with complete raw capture")
        write(audit_path, {"status": "FAIL", "checks": checks})
        return
    capture = result["capture"]
    case = record["case"]
    config = record["config"]
    target = 240 if config == "Debug" else 600
    expected_normal = 237 if config == "Debug" else 360 if case == "H_FREE" else 600
    expected_wrap = 240 if config == "Release" and case == "H_FREE" else 0
    expected_unit = 3 if config == "Debug" else 0
    check("process", bool(capture.get("complete")) and capture.get("ticks") == target and
          capture.get("normal_ticks") == expected_normal and
          capture.get("wrap_ticks") == expected_wrap and capture.get("unit_ticks") == expected_unit,
          capture, f"complete; exact normal/wrap/unit={expected_normal}/{expected_wrap}/{expected_unit}, total={target}")
    required = ["samples.csv", "bodies.csv", "arena.csv", "heap.csv", "impulses.csv", "fixture.json"]
    native_stages = "native_contact_stages" in record
    if native_stages:
        check("native_stage_identity", record["native_contact_stages"] == "P28_NATIVE_CONTACT_STAGES_01" and
              case in CONTACT and record["backend"] == "native" and config == "Release" and "contact_fixture" in record,
              record["native_contact_stages"], "stages belong to the versioned Native contact fixture")
        required.append("native-contact-stages.csv")
    missing = [p for p in required if not (data / p).is_file()]
    if missing:
        check("raw_capture", False, missing, "all declared raw files present")
        write(audit_path, {"status": "INCOMPLETE", "checks": checks})
        return
    samples = rows(data / "samples.csv")
    bodies = rows(data / "bodies.csv")
    arena = rows(data / "arena.csv")
    heap = rows(data / "heap.csv")
    impulses = rows(data / "impulses.csv")
    fixture = json.loads((data / "fixture.json").read_text())
    contact_v2 = "contact_fixture" in record or "contact_version" in fixture
    contact_version = fixture.get("contact_version", 0)
    floor_shape = {}
    if contact_v2:
        expected_binding = contact_fixture_identity(contact_version) if contact_version in (2, 3, 4) else {}
        if not expected_binding:
            raise ValueError("Unrecognized effective contact fixture")
        packet = json.loads((ROOT / expected_binding["path"]).read_text())
        floor = packet["floor"]
        measured_center = fixture.get("floor_center", [])
        measured_half = fixture.get("floor_half", [])
        measured_orientation = fixture.get("floor_orientation", [])
        def near_vector(actual, frozen):
            return len(actual) == len(frozen) and all(math.isfinite(v) and abs(v-e) <= 1.e-5
                                                       for v,e in zip(actual, frozen))
        pose_matches = (near_vector(measured_center, floor["center"]) and
                        near_vector(measured_half, floor["half_extents"]) and
                        (near_vector(measured_orientation, floor["orientation"]) or
                         near_vector(measured_orientation, [-v for v in floor["orientation"]])))
        check("versioned_contact_fixture", record.get("contact_fixture") == expected_binding and
              contact_version == packet["version"] and case in packet["cases"] and config == "Release" and
              abs(float(fixture["pivot_y"]) - packet["pivot"][1]) <= 1.e-5 and pose_matches,
              {"input": record.get("contact_fixture"), "floor": fixture},
              "exact independent fixture input SHA256 and actual static finite floor OBB agree")
        floor_shape = {"floor_center": floor["center"], "floor_half": floor["half_extents"],
                       "floor_orientation": floor["orientation"]}
    for name, value in [("samples", samples), ("bodies", bodies)]:
        check(name + "_present", bool(value), len(value), "nonempty authoritative tick capture")
    expected_segments = {"normal": expected_normal}
    if expected_wrap:
        expected_segments["wrap"] = expected_wrap
    check("exact_sample_segments", {r.get("segment") for r in samples} == set(expected_segments),
          sorted({r.get("segment") for r in samples}), "only expected normal/wrap segments")
    check("exact_body_segments", {r.get("segment") for r in bodies} == set(expected_segments),
          sorted({r.get("segment") for r in bodies}), "only expected normal/wrap segments")
    for segment, count in expected_segments.items():
        sr = [r for r in samples if r.get("segment") == segment]
        br = [r for r in bodies if r.get("segment") == segment]
        ticks = [int(r["tick"]) for r in sr]
        body_ticks = [int(r["tick"]) for r in br]
        check(segment + "_sample_membership", ticks == list(range(count + 1)),
              {"count": len(sr), "first": ticks[:3], "last": ticks[-3:]},
              f"exact authoritative ticks 0..{count}")
        check(segment + "_body_membership", body_ticks == list(range(count + 1)),
              {"count": len(br)}, f"exact body ticks 0..{count}")
    completed = [r for r in samples if int(r["tick"]) > 0]
    if completed and config == "Release":
        q = float(capture.get("clock_quantum_ns", 0))
        check("clock", 0 < q <= 1000, q, "0<q<=1000ns on tested product")
        dt_ns = 1e9 / 60
        for scope in ("inclusive_ns", "joint_ns", "contact_ns", "observer_ns"):
            values = [float(r[scope]) for r in completed]
            check(scope + "_finite", all(math.isfinite(v) and v >= 0 for v in values),
                  {"min": min(values), "max": max(values)}, "every completed tick finite and nonnegative")
            measures = stat(values)
            if scope == "inclusive_ns":
                check("inclusive_cpu", measures["mean"] + q <= .5 * dt_ns and
                      measures["median"] + q <= .5 * dt_ns and
                      measures["p95"] + q <= .75 * dt_ns and
                      measures["rolling60_mean_max"] + q <= .75 * dt_ns,
                      measures, "all ticks raw+q: mean/median<=.5dt; p95/max rolling60<=.75dt")
            if scope == "observer_ns":
                pairs = [(float(r["minimal_ns"]), float(r["full_ns"])) for r in completed]
                check("same_step_observer_scopes", all(math.isfinite(a) and math.isfinite(b) and
                      0 < a <= b and a == float(r["inclusive_ns"]) and
                      abs((b-a)-float(r["capture_ns"])) <= 1 and
                      float(r["observer_ns"]) == float(r["capture_ns"]) and
                      float(r["observer_upper_ns"]) == b+float(r["boundary_observer_ns"]) and
                      abs(float(r["observer_refined_ns"])-(float(r["capture_ns"])+
                          float(r["boundary_observer_ns"])+float(r["wrapper_ns"])+
                          float(r["profile_hook_ns"])+
                          2*q*(float(r["wrapper_calls"])+float(r["profile_hook_calls"])))) <= 1
                      for (a, b), r in zip(pairs, completed)),
                      {"first_pairs": pairs[:5]},
                      "U=existing complete Update observer boundary, F=U+actual outside Capture; boundary callbacks separately bounded; no inclusive subtraction")
                check("outside_observer_overhead", measures["mean"] + 2*q <= dt_ns/100 and
                      measures["median"] + 2*q <= dt_ns/100 and
                      measures["p95"] + 2*q <= dt_ns/50 and
                      measures["rolling60_mean_max"] + 2*q <= dt_ns/50,
                      measures, "same-tick actual F-U outside Capture+2q within frozen thresholds")
                upper=stat([float(r["observer_upper_ns"]) for r in completed])
                check("all_observer_sufficient_upper_bound",
                      upper["mean"] + 2*q <= dt_ns/100 and
                      upper["median"] + 2*q <= dt_ns/100 and
                      upper["p95"] + 2*q <= dt_ns/50 and
                      upper["rolling60_mean_max"] + 2*q <= dt_ns/50,
                      {"upper": upper,
                       "whole_wrapper_refinement": stat([float(r["observer_refined_ns"]) for r in completed])},
                      "conservative U+Capture+begin/end callback cost contains all in-Update hooks and outside Capture; failure is incomplete proof, not observed cost FAIL",
                      unknown_on_false=True)
        if record["backend"] == "jolt":
            sdk_values = [float(r["sdk_update_ns"]) for r in completed]
            stage_counts = [sum(int(r[f"jolt_stage_{i}_calls"]) for r in completed)
                            for i in range(8)]
            stage_measures = [stat([float(r[f"jolt_stage_{i}_ns"]) for r in completed])
                              for i in range(8)]
            hook_calls = sum(int(r["profile_hook_calls"]) for r in completed)
            check("jolt_sdk_stage_cpu", all(v > 0 and math.isfinite(v) for v in sdk_values) and
                  sum(stage_counts[:4]) > 0 and hook_calls > 0 and
                  (case not in CONTACT or stage_counts[4] > 0),
                  {"shared_sdk_update": stat(sdk_values),
                   "stage_names": ["joint_setup", "joint_warm", "joint_velocity", "joint_position",
                                   "contact_creation", "contact_warm", "contact_velocity", "contact_position"],
                   "stage_calls": stage_counts, "stage_ns": stage_measures,
                   "stage_measurement_kind": "sum of measured worker-scope wall durations; concurrent scopes may overlap and totals are not additive with whole Update wall time",
                   "profile_hook_calls": hook_calls,
                   "profile_hook_ns": stat([float(r["profile_hook_ns"]) for r in completed]),
                   "native_joint_contact_columns": "unavailable for Jolt"},
                  "profiled Jolt product reports whole SDK Update wall duration plus measured separate joint/contact worker-scope wall durations; concurrent scopes may overlap; contact workload exercises contact; zero unavailable native stage columns grant no PASS",
                  unknown_on_false=True)
        else:
            check("native_stage_cpu", all(int(r["sdk_update_ns"]) == 0 and
                  all(int(r[f"jolt_stage_{i}_ns"]) == 0 and
                      int(r[f"jolt_stage_{i}_calls"]) == 0 for i in range(8))
                  for r in completed) and
                  any(int(r["joint_ns"]) > 0 for r in completed) and
                  (case not in CONTACT or any(int(r["contact_ns"]) > 0 for r in completed)),
                  {"native_joint": stat([float(r["joint_ns"]) for r in completed]),
                   "native_contact": stat([float(r["contact_ns"]) for r in completed]),
                   "jolt_stages": "unavailable for Native"},
                  "existing Native joint/contact scopes measured; contact case actually enters contact solve; Jolt SDK stages unavailable on Native",
                  unknown_on_false=True)
        if case in CONTACT:
            actual_contact_ticks = [int(r["tick"]) for r in completed if
                (int(r["jolt_stage_4_calls"]) > 0 if record["backend"] == "jolt"
                 else int(r["native_contact_pairs"]) > 0)]
            check("actual_contact_exercise", bool(actual_contact_ticks),
                  {"backend": record["backend"], "positive_ticks": len(actual_contact_ticks),
                   "first_positive_ticks": actual_contact_ticks[:10],
                   "native_max_pairs": max(int(r["native_contact_pairs"]) for r in completed),
                   "jolt_stage4_calls": sum(int(r["jolt_stage_4_calls"]) for r in completed)},
                  "the same admitted contact workload forms at least one real Native nonempty-manifold pair or Jolt new/cached contact constraint; floor presence or empty timing is insufficient",
                  unknown_on_false=True)
    if config == "Release":
        scale = float(fixture["scale"])
        mass = float(fixture["mass_kg"])
        check("reference_packet", scale == 1 and mass == 1 and
              float(fixture["gravity_y"]) == -10 and
              bool(fixture["floor"]) == (case in CONTACT), fixture,
              "frozen 1kg, s=1, g=(0,-10,0), floor only H/CV_CONTACT")
        sample_map = {(r["segment"], int(r["tick"])): r for r in samples}
        body_map = {(r["segment"], int(r["tick"])): r for r in bodies}
        if contact_version >= 3:
            initial = sample_map.get(("normal", 0), {})
            expected_start = math.radians(12.5) * (1 if case == "H_CONTACT" else -1)
            coordinate = "angle" if case == "H_CONTACT" else "angle_v"
            check("interior_angular_start",
                  abs(float(initial.get(coordinate, math.inf))-expected_start) < 1.e-5 and
                  (case == "H_CONTACT" or abs(float(initial.get("angle_u", math.inf))) < 1.e-5),
                  {key:initial.get(key) for key in ("angle","angle_u","angle_v")},
                  "provider starts at the frozen interior datum, separated from the nearest limit by 32.5 degrees")
        l_anchor = .25 + scale
        d_contact = .30*scale
        d_visual = 2*scale
        quiet: dict[tuple[str, int], bool] = {}
        overlap: dict[int, float] = {}
        violations: list[dict] = []
        energy_bad: list[dict] = []
        finite_bad: list[tuple[str, int, str]] = []
        consecutive = 0
        rest_energy = mass*(.00125+.0000681666666667*scale*scale)
        loaded_energy = .46125*mass
        wrap_energy = .2748708333333*mass*scale*scale
        for segment, count in expected_segments.items():
            baseline = sample_map.get((segment, 0))
            if baseline is None:
                continue
            e0_raw, e0_star = float(baseline["energy_raw"]), float(baseline["energy_intended"])
            consecutive = 0
            for tick in range(1, count+1):
                sample = sample_map.get((segment, tick))
                body = body_map.get((segment, tick))
                if sample is None or body is None:
                    finite_bad.append((segment, tick, "missing state"))
                    continue
                values = {k: float(v) for k, v in {**sample, **body}.items()
                          if k not in ("segment", "tick")}
                if not all(math.isfinite(v) for v in values.values()):
                    finite_bad.append((segment, tick, "nonfinite"))
                    continue
                linear = norm(vec(body, ("vx", "vy", "vz")))
                angular_speed = norm(vec(body, ("wx", "wy", "wz")))
                physically_quiet = int(body["sleeping"]) == 1 or (linear <= .05 and angular_speed <= .02)
                consecutive = consecutive+1 if physically_quiet else 0
                settled = segment == "normal" and (tick <= 60 or consecutive >= 30)
                quiet[(segment, tick)] = settled
                if segment == "normal" and tick <= 60 and not physically_quiet:
                    violations.append({"tick": tick, "metric": "initial quiet", "linear": linear,
                                       "angular": angular_speed})
                a = float(sample["anchor_error"])
                a_limit = (.005 if settled else .01)*l_anchor
                if a > a_limit:
                    violations.append({"tick": tick, "segment": segment,
                                       "metric": "anchor_m", "actual": a, "limit": a_limit})
                angular_names = (["axis_error"] if case.startswith("H_") else ["cv_error"])
                if case in ("H_LIMIT", "H_CONTACT", "CV_LIMIT", "CV_CONTACT"):
                    angular_names += ["violation_u"]
                if case in ("CV_LIMIT", "CV_CONTACT"):
                    angular_names += ["violation_v"]
                limit = math.asin(.005 if settled else .01)
                for name in angular_names:
                    value = float(sample[name])
                    if value > limit:
                        violations.append({"tick": tick, "segment": segment,
                                           "metric": name, "actual": value, "limit": limit})
                if segment == "normal" and case in CONTACT:
                    overlap[tick] = box_floor_overlap(body, scale, **floor_shape)
                    contact_limit = (.01 if settled else .02)*d_contact
                    if overlap[tick] > contact_limit:
                        violations.append({"tick": tick, "metric": "box_floor_overlap_m",
                                           "actual": overlap[tick], "limit": contact_limit})
                allowance = (wrap_energy if segment == "wrap" else
                             rest_energy if tick <= 60 else loaded_energy)
                for name, initial in (("energy_raw", e0_raw), ("energy_intended", e0_star)):
                    excess = float(sample[name])-initial-float(sample["work"])
                    if excess > allowance:
                        energy_bad.append({"tick": tick, "segment": segment,
                                           "metric": name, "excess_J": excess, "limit_J": allowance})
        check("all_tick_finite", not finite_bad, finite_bad[:20],
              "all exported authoritative state finite and present; no omitted peak")
        check("anchor_angular_quiet", not violations,
              {"first_failures": violations[:20], "count": len(violations)},
              "initial/sustained quiet anchor<=.005L, angular<=asin(.005); otherwise .01L/asin(.01); contact 1%/2% thickness")
        check("raw_and_intended_energy", not energy_bad,
              {"first_failures": energy_bad[:20], "count": len(energy_bad)},
              "every completed tick E-E0-actual central impulse work <= rest/loaded/wrap allowance")
        if case in CONTACT and overlap:
            settled_depth = .01*d_contact
            episodes: list[dict] = []
            cursor = 1
            while cursor <= expected_segments["normal"]:
                if overlap.get(cursor, 0) <= settled_depth:
                    cursor += 1
                    continue
                start = cursor
                recovered = next((t for t in range(start, min(start+4, expected_segments["normal"])+1)
                                  if overlap.get(t, math.inf) <= settled_depth), None)
                confirmed = recovered is not None and recovered+2 <= expected_segments["normal"] and \
                    all(overlap.get(t, math.inf) <= settled_depth for t in (recovered+1, recovered+2))
                episodes.append({"first": start, "recovered": recovered, "confirmed": confirmed})
                cursor = recovered+3 if confirmed else start+5
            check("contact_recovery", all(e["confirmed"] for e in episodes), episodes,
                  "first >1%D depth returns <=1%D within 4 ticks plus two confirmations; no pending episode")
            if contact_v2:
                # Contact detection may precede/follow end-of-step transform sampling by one step.
                positives = set(actual_contact_ticks)
                coupled = [e for e in episodes if e["confirmed"] and
                           any(t in positives for t in range(max(1, e["first"]-1), e["recovered"]+2))]
                check("nonvacuous_contact_recovery", bool(coupled),
                      {"maximum_depth_m": max(overlap.values()), "episode_count": len(episodes),
                       "coupled_recovered_episodes": coupled},
                      "one actual >3 mm SAT penetration returns within four ticks with two confirmations "
                      "and corresponding real provider bar/floor contact activity")
        if len(impulses) == 2:
            impulse_bad = []
            for action, after_tick, expected in zip(impulses, (60, 300),
                                                     ((3., 0., 0.), (0., 0., 3.) if case.startswith("CV") else (-3., 0., 0.))):
                j = vec(action, ("jx", "jy", "jz"))
                v = vec(action, ("vx_before", "vy_before", "vz_before"))
                m = float(action["mass"])
                actual_work = float(action["work"])
                formula = dot(j, v)+dot(j, j)/(2*m)
                if contact_version >= 3:
                    expected = packet["actions"][case][0 if after_tick==60 else 1]
                    body = body_map[("normal", after_tick)]
                    axes = rotated_axes(quat(body))
                    com = vec(action, ("com_x","com_y","com_z"))
                    point = vec(action, ("point_x","point_y","point_z"))
                    lever = tuple(p-c for p,c in zip(point,com))
                    offset = packet["actions"]["local_lever_from_bar_com_m"][case][0 if after_tick==60 else 1]
                    required_lever = tuple(sum(offset[i]*axes[i][k] for i in range(3)) for k in range(3))
                    spin = vec(action, ("wx_before","wy_before","wz_before"))
                    angular_momentum = cross(lever,j)
                    half = packet["bar"]["half_extents"]
                    inertia = [m*sum(half[k]**2 for k in range(3) if k!=i)/3 for i in range(3)]
                    local_spin_delta = [dot(axis,angular_momentum)/moment for axis,moment in zip(axes,inertia)]
                    spin_delta = tuple(sum(local_spin_delta[i]*axes[i][k] for i in range(3)) for k in range(3))
                    formula += dot(angular_momentum,spin) + .5*dot(angular_momentum,spin_delta)
                    before_ok = (near_vector(v,vec(body,("vx","vy","vz"))) and
                                 near_vector(spin,vec(body,("wx","wy","wz"))) and
                                 near_vector(com,vec(body,("x","y","z"))) and near_vector(lever,required_lever))
                    after_ok = (near_vector(vec(action,("vx_after","vy_after","vz_after")),
                                            [a+b/m for a,b in zip(v,j)]) and
                                near_vector(vec(action,("wx_after","wy_after","wz_after")),
                                            [a+b for a,b in zip(spin,spin_delta)]))
                    if not (before_ok and after_ok):
                        impulse_bad.append({"after_tick":after_tick,"before_ok":before_ok,"after_ok":after_ok})
                if int(action["after_tick"]) != after_tick or any(abs(a-b) > 1e-6 for a, b in zip(j, expected)) or \
                        abs(actual_work-formula) > 1e-5 or abs(m-mass) > 1e-6:
                    impulse_bad.append(action)
            check("action_work", not impulse_bad, impulse_bad,
                  "exact scheduled impulses, typed strike points, measured momentum changes and signed work"
                  if contact_version >= 3 else "exact after-tick60/300 COM impulses and J dot v_before + |J|^2/(2m)")
            if contact_v2:
                # The after-tick impulse changes the next completed sample's energy allowance.
                cumulative_bad = []
                for t in range(expected_normal+1):
                    expected_work = sum(float(r["work"]) for r in impulses if int(r["after_tick"]) < t)
                    sample = sample_map.get(("normal", t))
                    if sample is None or abs(float(sample["work"])-expected_work) > 1.e-5:
                        cumulative_bad.append(t)
                check("contact_cumulative_work", not cumulative_bad, cumulative_bad,
                      "each normal tick accounts for exactly the previously applied impulses")
        else:
            check("action_work", False, impulses, "exactly two scheduled normal-segment COM impulses")
        trend_bad: list[dict] = []
        for segment, lo, hi in [("normal", 61, 300), ("normal", 301, expected_segments["normal"]),
                                 ("wrap", 1, 240)]:
            if segment not in expected_segments or hi-lo+1 < 120:
                continue
            n = hi-lo+1
            if n % 60:
                continue
            for metric in ("energy_raw", "energy_intended"):
                e0 = float(sample_map[(segment, 0)][metric])
                residual = [float(sample_map[(segment, t)][metric])-e0-
                            float(sample_map[(segment, t)]["work"]) for t in range(lo, hi+1)]
                slope = slope_of_bins(residual)
                limit = rest_energy/(n/60-1)
                if slope > limit:
                    trend_bad.append({"segment": segment, "ticks": [lo, hi], "metric": metric,
                                      "slope_J_per_s": slope, "limit_J_per_s": limit})
        check("energy_growth", not trend_bad, trend_bad,
              "raw and intended exact 60-tick OLS bins, <=one quiet-energy allowance across bin-center span")
        jitter_bad: list[dict] = []
        quiet_windows = 0
        for segment, count in expected_segments.items():
            for lo in range(1, count-58):
                hi = lo+59
                if not all(quiet.get((segment, t), False) for t in range(lo, hi+1)):
                    continue
                quiet_windows += 1
                state = [body_map[(segment, t)] for t in range(lo, hi+1)]
                position = [vec(r, ("x", "y", "z")) for r in state]
                orientation = [quat(r) for r in state]
                position_peak = max(norm(tuple(a-b for a, b in zip(p, q)))
                                    for i, p in enumerate(position) for q in position[i+1:])
                orientation_peak = max(qangle(p, q)
                                       for i, p in enumerate(orientation) for q in orientation[i+1:])
                position_hf = max(norm(tuple(position[i][k]-(position[i-1][k]+position[i+1][k])/2
                                              for k in range(3))) for i in range(1, 59))
                orientation_hf = max(qangle(orientation[i], tuple(
                    (orientation[i-1][k]+orientation[i+1][k]) if dot(orientation[i-1], orientation[i+1]) >= 0
                    else (orientation[i-1][k]-orientation[i+1][k]) for k in range(4))) for i in range(1, 59))
                anchor = [float(sample_map[(segment, t)]["anchor_error"]) for t in range(lo, hi+1)]
                anchor_hf = max(abs(anchor[i]-(anchor[i-1]+anchor[i+1])/2) for i in range(1, 59))
                if (position_peak > .001*d_visual or orientation_peak > .002 or
                    position_hf > .0005*d_visual or orientation_hf > .001 or
                    anchor_hf > .001*l_anchor):
                    jitter_bad.append({"segment": segment, "ticks": [lo, hi],
                                       "position_peak_m": position_peak,
                                       "orientation_peak_rad": orientation_peak,
                                       "position_hf_m": position_hf,
                                       "orientation_hf_rad": orientation_hf,
                                       "anchor_hf_m": anchor_hf})
        check("quiet_jitter", quiet_windows > 0 and not jitter_bad,
              {"windows": quiet_windows, "first_failures": jitter_bad[:10]},
              "all quiet sliding60 windows: p-p<=.001D_visual/.002rad; 3-point HF<=.0005D_visual/.001rad/.001L_anchor")
        if case == "H_FREE" and ("wrap", 240) in sample_map:
            angle = [float(sample_map[("wrap", t)]["angle"]) for t in range(241)]
            increments = [math.remainder(angle[t]-angle[t-1], 2*math.pi) for t in range(1, 241)]
            crossings = sum(angle[t]-angle[t-1] < -math.pi for t in range(1, 241))
            check("free_hinge_wrap", all(d > 0 for d in increments) and
                  sum(increments) > 2*math.pi and crossings >= 1,
                  {"accumulated_rad": sum(increments), "minimum_increment": min(increments),
                   "principal_wrap_crossings": crossings},
                  "fresh zero-g free hinge makes positive admitted-axis progress through a principal wrap; no unfrozen 8rad drift threshold")
        if case in ("H_LIMIT", "H_CONTACT", "CV_LIMIT", "CV_CONTACT"):
            tolerance = math.asin(.01)
            limit = math.pi/4
            source = [sample_map[("normal", t)] for t in range(1, expected_segments["normal"]+1)]
            def reached_and_returned(values: list[float], side: int) -> dict:
                signed = [side*v for v in values]
                visits = [i for i, v in enumerate(signed) if v >= limit-tolerance]
                returned = any(any(v <= limit-tolerance for v in signed[i+1:]) for i in visits)
                return {"visited": bool(visits), "returned": returned,
                        "maximum_toward_bound_rad": max(signed), "first_visit_tick": visits[0]+1 if visits else None}
            if case.startswith("H_"):
                values = [float(r["angle"]) for r in source]
                coverage = {"upper": reached_and_returned(values, 1),
                            "lower": reached_and_returned(values, -1)}
            else:
                coverage = {name: {"positive": reached_and_returned([float(r[column]) for r in source], 1),
                                   "negative": reached_and_returned([float(r[column]) for r in source], -1)}
                            for name, column in (("U", "angle_u"), ("V", "angle_v"))}
                coverage = {name: next((side for side in sides.values()
                                        if side["visited"] and side["returned"]),
                                       {"visited": any(x["visited"] for x in sides.values()),
                                        "returned": False, "sides": sides})
                            for name, sides in coverage.items()}
            check("actual_limit_axis_coverage",
                  all(x["visited"] and x["returned"] for x in coverage.values()),
                  coverage,
                  "admitted actions visit each required hinge lower/upper or CV U/V bound within existing asin(.01) transient tolerance and later return inward; missing exercise is INCOMPLETE, not a physics FAIL",
                  unknown_on_false=True)
        sizes = [int(x) for x in capture.get("target_sizes", [])]
        current_size = len(sizes) == 8 and sizes[0] == 8 and sizes[1] == 4 and sizes[2] == 16
        bound = source_arena_sum(sizes) if current_size else None
        check("current_target_sizes", current_size and bound <= 33554432,
              {"sizes": sizes, "conservative_aligned_sum_bytes": bound},
              "eight actual current Release GEngine type sizes; source-derived 19-site sum within existing32MiB, not runtime PASS")
        arena_bad = []
        arena_peak = 0
        per_tick: dict[tuple[str, int], list[dict[str, str]]] = {}
        for event in arena:
            per_tick.setdefault((event["segment"], int(event["tick"])), []).append(event)
        expected_arena_keys = {(segment, tick) for segment, count in expected_segments.items()
                               for tick in range(1, count+1)}
        active_arena_keys = {(r["segment"], int(r["tick"])) for r in completed
                             if int(r["pre_step_active"])}
        for key, events in per_tick.items():
            stack: list[tuple[int, int]] = []
            positive_calls = 0
            for event in events:
                requested = int(event["requested"])
                if requested == 0:
                    continue
                pointer = int(event["pointer"])
                aligned_bytes = int(event["aligned"])
                usage = int(event["used"])
                arena_peak = max(arena_peak, usage)
                if aligned_bytes != aligned(requested, sizes[2] if current_size else 16):
                    arena_bad.append({"tick": key, "reason": "alignment", "event": event})
                if int(event["reserve"]):
                    positive_calls += 1
                    stack.append((pointer, requested))
                elif not stack or stack.pop() != (pointer, requested):
                    arena_bad.append({"tick": key, "reason": "release mismatch", "event": event})
            if stack or (events and int(events[-1]["used"]) != 0) or positive_calls > 19:
                arena_bad.append({"tick": key, "reason": "unbalanced/extra sites", "calls": positive_calls,
                                  "outstanding": stack[:10]})
        if record["backend"] == "jolt":
            extra=set(per_tick)-expected_arena_keys
            positive_keys={key for key, events in per_tick.items()
                           if any(int(e["reserve"]) and int(e["requested"]) > 0 for e in events)}
            missing_active=active_arena_keys-positive_keys
            if extra or missing_active or not positive_keys:
                arena_bad.append({"extra_ticks": sorted(extra)[:10],
                                  "active_ticks_without_positive_reservation": sorted(missing_active)[:10],
                                  "reason": "unmapped events, missing active tick callback, or empty instrumented Jolt stream"})
        elif arena:
            arena_bad.append({"reason": "Native unexpectedly used Jolt arena", "events": len(arena)})
        check("arena_lifetime", not arena_bad and arena_peak <= 33554432 and
              (bound is None or arena_peak <= bound),
              {"peak_bytes": arena_peak, "events": len(arena),
               "active_ticks": len(active_arena_keys),
               "zero_event_ticks": len(expected_arena_keys-set(per_tick)),
               "legitimate_inactive_zero_event_ticks": len((expected_arena_keys-set(per_tick))-active_arena_keys),
               "first_failures": arena_bad[:10]},
              "all observed Jolt events map to completed ticks and balance LIFO with <=19 sites/current source bound; every pre-step active dynamic-body tick has a positive reservation; sleeping early-return may have zero events; Native no Jolt arena")
        nonzero_heap = [r for r in heap]
        scoped = []
        broadphase_calls_by_segment: dict[str, int] = {}
        for r in completed:
            calls = int(r["cpp_calls"])
            byte_count = int(r["cpp_bytes"])
            joint_calls, graph_calls, broad_calls = (int(r[name]) for name in
                ("joint_cpp_calls", "graph_cpp_calls", "broadphase_cpp_calls"))
            joint_bytes, graph_bytes, broad_bytes = (int(r[name]) for name in
                ("joint_cpp_bytes", "graph_cpp_bytes", "broadphase_cpp_bytes"))
            broadphase_calls_by_segment[r["segment"]] = (
                broadphase_calls_by_segment.get(r["segment"], 0) + broad_calls)
            if calls or byte_count or joint_calls or graph_calls or broad_calls:
                scoped.append({"segment": r["segment"], "tick": int(r["tick"]),
                               "total_calls": calls, "total_bytes": byte_count,
                               "joint_calls": joint_calls, "joint_bytes": joint_bytes,
                               "graph_calls": graph_calls, "graph_bytes": graph_bytes,
                               "broadphase_calls": broad_calls, "broadphase_bytes": broad_bytes,
                               "unattributed_calls": calls-joint_calls-graph_calls-broad_calls,
                               "unattributed_bytes": byte_count-joint_bytes-graph_bytes-broad_bytes})
        joint_or_graph = [r for r in scoped if r["joint_calls"] or r["joint_bytes"] or
                          r["graph_calls"] or r["graph_bytes"]]
        unattributed = [r for r in scoped if r["unattributed_calls"] or r["unattributed_bytes"]]
        # Four Rebuild vectors and at most three geometric final-pair growth
        # steps for the admitted <=3-body world; no joint member is read here.
        broadphase_source_bound = all(0 <= count <= 7 for count in broadphase_calls_by_segment.values())
        check("incremental_joint_heap", not joint_or_graph,
              {"joint_or_graph_allocation_ticks": len(joint_or_graph), "first": joint_or_graph[:10]},
              "existing Update joint solve and both Update graph-build sites allocate zero C++ heap; any call is joint-induced and blocks")
        attribution = (verify_allocation_evidence(directory, record, result, heap, arena,
                                                   allocation_evidence) if allocation_evidence else
                       {"source_attribution": "absent", "jph_heap_events": len(nonzero_heap)})
        if native_stages:
            attribution = native_contact_allocation_proof(data, samples)
        source_complete = (attribution["complete"] and not joint_or_graph) if native_stages else not unattributed
        check("steady_heap_attribution", (bool(allocation_evidence) or not nonzero_heap) and
              source_complete and broadphase_source_bound,
              {"jph_heap_events": len(nonzero_heap), "cpp_nonzero_ticks": len(scoped),
               "broadphase_calls_by_segment": broadphase_calls_by_segment,
               "first_jph": nonzero_heap[:10], "first_unattributed": unattributed[:10],
               "first_scoped": scoped[:10], "source_attribution": attribution},
              "Zero incremental joint-induced heap. Complete Update C++ calls require exact existing broadphase attribution (<=7 per segment), or separately verified Native contact-stage provenance. Every JPH heap event requires source attribution; unknown allocation remains incomplete.",
              unknown_on_false=True)
    status = ("FAIL" if any(c["result"] == "FAIL" for c in checks) else
              "INCOMPLETE" if any(c["result"] == "INCOMPLETE" for c in checks) else "PASS")
    report = {"status": status, "checks": checks, "matrix_sha256": MATRIX_SHA256,
              "auditor_sha256": sha(Path(__file__)), "raw_sha256": {p: sha(data / p) for p in required}}
    if output:
        original = json.loads((directory / "audit.json").read_text(encoding="utf-8"))
        old_checks = {c["check"]: c["result"] for c in original["checks"]}
        new_checks = {c["check"]: c["result"] for c in checks}
        old_status = original.get("status")
        old_incomplete_to_pass = (audit_path.name.startswith("audit-allocation-") and
                                  old_status == "INCOMPLETE" and status == "PASS")
        old_fail_remains_fail = (audit_path.name.startswith("audit-attribution-") and
                                 old_status == "FAIL" and status == "FAIL")
        if not (old_incomplete_to_pass or old_fail_remains_fail) or \
           len(old_checks) != len(original["checks"]) or len(new_checks) != len(checks) or \
           old_checks.get("steady_heap_attribution") != "INCOMPLETE" or \
           new_checks.get("steady_heap_attribution") != "PASS" or \
           json.dumps({c["check"]: c for c in original["checks"] if c["check"] != "steady_heap_attribution"},
                      sort_keys=True) != \
           json.dumps({c["check"]: c for c in checks if c["check"] != "steady_heap_attribution"},
                      sort_keys=True):
            raise ValueError("Supplemental audit cannot change a gate beyond source attribution")
        report.update({"run": directory.relative_to(ROOT).as_posix(),
                       "inputs_sha256": sha(directory / "inputs.json"),
                       "result_sha256": sha(directory / "result.json"),
                       "supersedes_audit_sha256": sha(directory / "audit.json"),
                       "allocation_evidence": {"path": safe(ROOT / allocation_evidence).relative_to(ROOT).as_posix(),
                                               "sha256": sha(safe(ROOT / allocation_evidence))}})
    write(audit_path, report)
    print(json.dumps({"status": status, "checks": len(checks)}, indent=2))


def audit_property_editor(directory: Path, record: dict, data: Path, result: dict) -> None:
    role = record["editor_session"]
    direct = role == "direct-default-jolt" or role in GENERAL_EDITOR_ROLES
    case, backend, start_scene, explicit = EDITOR_ROLES[role]
    capture = result.get("capture") or {}
    events = rows(data / "editor-events.csv") if (data / "editor-events.csv").is_file() else []
    checks = []

    def check(name: str, ok: bool, actual: object) -> None:
        checks.append({"check": name, "result": "PASS" if ok else "FAIL", "actual": actual})

    startup_path = data / "editor-startup.json"
    startup = json.loads(startup_path.read_text()) if startup_path.exists() else {}
    check("provider_startup", record["case"] == case and record["backend"] == backend and
          record["cli_backend_explicit"] is explicit and startup.get("cli_backend_explicit") is explicit and
          (explicit or (startup.get("saved_backend") == "none" and not record["initial_imgui"]["present"])) and
          startup.get("session") == role and startup.get("scene") == start_scene and
          startup.get("world_backend") == startup.get("app_backend") == backend, startup)
    if role == "component-general-jolt" and dt.datetime.fromisoformat(record["utc"]) >= \
       dt.datetime.fromisoformat(outline_authority()["recorded_utc"]):
        default_path = data / "diagnostic-defaults.json"
        defaults = json.loads(default_path.read_text(encoding="utf-8")) if default_path.is_file() else {}
        check("diagnostic_default_initialization", defaults.get("complete") is True and
              defaults.get("allocated") is True and defaults.get("released_before_physics") is True and
              all(defaults.get(key) == value for key, value in {
                  "rows_checked": 32, "pairs_checked": 17020, "differences_checked": 17020,
                  "pen_stages_checked": 4, "pen_contacts_checked": 4096,
                  "pen_positions_checked": 1024, "row_hash_default": 14695981039346656037,
                  "pair_state_bytes": 64}.items()), defaults)
    check("completed_bounded_editor", result["exit_code"] == 0 and result["inputs_unchanged"] and
          not result["timed_out"] and result["seconds"] <= 180 and capture.get("complete") and
          0 <= capture.get("physics_ticks", 601) <= 600 and capture.get("editor_session") == role,
          {"capture": capture, "seconds": result["seconds"]})
    observed = [e["key"] for e in events if e["event"] == "observed"]
    check("reference_scene", "sphere-reference-applied" in observed and
          any(e["event"] == "edited" and e["key"] == "reference-position-x" for e in events),
          {"sphere_reference_observed": "sphere-reference-applied" in observed})
    check("five_loaded_cards", "thumbnails-loaded" in observed, "thumbnails-loaded" in observed)
    scenes = (["hinge-basic", "hinge-quaternion", "hinge-limited", "cv-free", "cv-limited"]
              if backend == "jolt" else ["hinge-limited"])
    for scene in scenes:
        start = next((i for i, e in enumerate(events) if e["key"] == scene and e["event"] == "observed"), len(events))
        end = next((i for i, e in enumerate(events[start + 1:], start + 1)
                    if e["event"] == "observed" and e["key"] in scenes), len(events))
        segment = events[start:end]
        edits = {e["key"] for e in segment if e["event"] == "edited"}
        clicks = {e["key"] for e in segment if e["event"] == "clicked"}
        observations = Counter(e["key"] for e in segment if e["event"] == "observed")
        required = {"position-x", "rotation-y", "scale-x", "size-x", "mass", "friction",
                    "restitution", "joint-frame", "radius"}
        if backend == "jolt": required.add("linear-damping")
        if scene == "hinge-limited": required.add("limit-upper")
        if scene == "cv-limited": required.add("limit-u")
        required_clicks = {"hierarchy.bar", "hierarchy.anchor", "create-enhanced", "shape.sphere",
                           "motion.kinematic", "apply", "revert", "reset"}
        required_states = {"book-protected", "enhanced", "selected-bar", "selected-anchor",
                           "scale-staged", "draft-isolated", "dirty-switch-protected"}
        if direct:
            required_clicks.remove("create-enhanced")
            required_clicks.update({"restore-defaults", "advanced"})
            required_states.difference_update({"book-protected", "enhanced"})
            required_states.update({"direct-editable", "defaults-staged", "defaults", "reset"})
            if scene == "hinge-limited":
                required_states.add("invalid-mass")
        check(scene, required <= edits and required_clicks <= clicks and
              (not direct or "create-enhanced" not in clicks) and
              required_states <= observations.keys() and observations["reset-retained"] >= 2 and
              observations["body-properties-applied"] >= 3 and (scene != "hinge-limited" or
              {"impulse", "paused", "running"} <= observations.keys()),
              {"missing_edits": sorted(required - edits), "missing_clicks": sorted(required_clicks - clicks),
               "observations": dict(observations)})
    check("backend_switch_and_return", {"jolt", "native", "portable-properties"} <= set(observed) and
          observed.count("portable-properties") >= 2, observed[-6:])
    if direct:
        check("saved_backend_preference", capture.get("saved_backend_at_end") == backend,
              capture.get("saved_backend_at_end"))
        check("invalid_and_stale_atomic_rejection",
              any(e["event"] == "stale-invalid-atomic-rejection" for e in events),
              "stale-invalid-atomic-rejection")
    if backend == "jolt":
        check("ordinary_properties_screenshot", png_dimensions(data / "editable-properties-jolt.png") is not None,
              png_dimensions(data / "editable-properties-jolt.png"))
    if role in GENERAL_EDITOR_ROLES:
        required = {"general-camera", "general-camera-preview", "general-directional", "general-point",
                    "general-room-reset", "general-room-defaults", "general-mesh-initial", "general-mesh",
                    "general-translate", "general-mesh-reset", "general-mesh-defaults"}
        check("general_object_applied_readback", required <= set(observed) and observed.count("general-room") >= 4,
              {"missing": sorted(required - set(observed)), "room_checks": observed.count("general-room")})
        edits = Counter(e["key"] for e in events if e["event"] == "edited")
        clicks = {e["key"] for e in events if e["event"] == "clicked"}
        required_clicks = {"hierarchy.floor", "hierarchy.wall", "hierarchy.wall-px", "hierarchy.wall-nz", "hierarchy.wall-pz",
                           "hierarchy.point", "hierarchy.directional", "hierarchy.camera", "hierarchy.mesh",
                           "light-apply", "light-revert", "camera-apply", "camera-revert", "camera-preview",
                           "material-apply", "material-revert", "generic-body-apply", "general-reset"}
        minimum_edits = {"light-intensity": 3, "camera-fov": 2, "material-roughness": 2, "generic-position-y": 6,
                         "generic-scale-x": 1, "generic-friction": 1}
        check("general_object_typed_controls", required_clicks <= clicks and
              all(edits[key] >= count for key, count in minimum_edits.items()),
              {"missing_clicks": sorted(required_clicks - clicks), "counts": {key: edits[key] for key in minimum_edits}})
        gesture = [(e["event"], e["key"]) for e in events if e["event"] in ("viewport-picked", "gizmo-drag-applied") or
                   (e["event"], e["key"]) == ("host-key", "T")]
        check("real_viewport_pick_and_transform_drag",
              gesture == [("viewport-picked", "Template Plane"), ("host-key", "T"), ("gizmo-drag-applied", "Template Plane")],
              gesture)
        for image in ("general-nonphysics", "general-light-camera"):
            dimensions = png_dimensions(data / (image + ".png"))
            check(image, bool(dimensions and min(dimensions) > 0 and any(e["event"] == "full-editor-png" and
                  e["key"] == image for e in events)), dimensions)
    status = "PASS" if all(c["result"] == "PASS" for c in checks) else "FAIL"
    write(directory / "audit.json", {"status": status, "checks": checks, "F_OWNER": "PENDING",
          "inputs_sha256": sha(directory / "inputs.json"), "result_sha256": sha(directory / "result.json"),
          "auditor_sha256": sha(Path(__file__)), "raw_sha256": {p.name: sha(p) for p in data.iterdir() if p.is_file()}})
    print(json.dumps({"status": status, "checks": len(checks)}))


def component_velocity_tick_packets() -> dict[str, dict]:
    """Frozen Dynamic free-flight oracle; Jolt applies gravity before drag."""
    f32 = lambda value: struct.unpack("<f", struct.pack("<f", value))[0]
    step = f32(1.0 / 60.0)
    linear_factor = f32(1.0 - f32(f32(.12) * step))
    angular_factor = f32(1.0 - f32(f32(.21) * step))
    gravity_delta = f32(step * f32(f32(.75) * f32(-12.0)))
    linear, angular = [.5, 0.0, 40.0], [.25, 0.0, 0.0]
    packets = {}
    for tick in range(1, 7):
        linear = [f32(linear[0] * linear_factor),
                  f32(f32(linear[1] + gravity_delta) * linear_factor),
                  f32(linear[2] * linear_factor)]
        angular = [f32(value * angular_factor) for value in angular]
        packets[str(tick)] = {"tick": tick, "provider.linear_velocity": linear,
                              "provider.angular_velocity": angular}
    return packets


def component_value_mismatches(actual: object, expected: object, tolerance: float,
                               prefix: str = "values") -> list[dict]:
    """Compare frozen authored expectations with actual observations, never authored substitutes."""
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return [{"property": prefix, "expected": expected, "actual": actual}]
        mismatch = []
        for key, value in expected.items():
            if key not in actual:
                mismatch.append({"property": prefix + "." + key, "expected": value, "actual": "MISSING"})
            else:
                mismatch.extend(component_value_mismatches(actual[key], value, tolerance, prefix + "." + key))
        return mismatch
    if isinstance(expected, list):
        if not isinstance(actual, list) or len(actual) != len(expected):
            return [{"property": prefix, "expected": expected, "actual": actual}]
        return [item for index, value in enumerate(expected) for item in
                component_value_mismatches(actual[index], value, tolerance, f"{prefix}[{index}]")]
    if isinstance(expected, (int, float)) and not isinstance(expected, bool):
        same = (isinstance(actual, (int, float)) and not isinstance(actual, bool) and
                math.isfinite(actual) and math.isfinite(expected) and abs(actual - expected) <= tolerance)
    else:
        same = type(actual) is type(expected) and actual == expected
    return [] if same else [{"property": prefix, "expected": expected, "actual": actual}]


def audit_component_editor(directory: Path, record: dict, data: Path, result: dict) -> None:
    role = record["editor_session"]
    plan, binding = frozen_component_plan(record["component_plan"]["path"], role, record)
    if binding != record["component_plan"]:
        raise ValueError("The prelaunch component plan identity changed")
    script_binding = record["component_script"]
    script_path = safe(ROOT / script_binding["path"])
    actions = plan["script"]["actions"]
    payload = "".join("\t".join(action[k] or "" for k in ("op", "key", "value")) + "\n"
                      for action in actions)
    if sha(script_path) != script_binding["sha256"] or script_path.read_text(encoding="utf-8") != payload:
        raise ValueError("Frozen exact SDL/ImGui TSV input changed")
    capture = result.get("capture") or {}
    events = rows(data / "editor-events.csv") if (data / "editor-events.csv").is_file() else []
    checks = []

    def check(name: str, ok: bool, actual: object) -> None:
        checks.append({"check": name, "result": "PASS" if ok else "FAIL", "actual": actual})

    case, backend, scene, explicit = EDITOR_ROLES[role]
    startup_path = data / "editor-startup.json"
    startup = json.loads(startup_path.read_text(encoding="utf-8")) if startup_path.is_file() else {}
    check("provider_startup", record["case"] == case and record["backend"] == backend and
          record["cli_backend_explicit"] is explicit and startup.get("cli_backend_explicit") is explicit and
          startup.get("saved_backend") == "none" and not record["initial_imgui"]["present"] and
          startup.get("session") == role and startup.get("scene") == scene and
          startup.get("world_backend") == startup.get("app_backend") == backend, startup)
    check("completed_bounded_editor", result["exit_code"] == 0 and result["inputs_unchanged"] and
          result.get("component_inputs_unchanged") is True and not result["timed_out"] and
          result["seconds"] <= 180 and capture.get("complete") is True and
          0 <= capture.get("physics_ticks", 601) <= 600 and capture.get("editor_session") == role and
          capture.get("actions") == len(actions) - 1 and
          bool(events) and all(0 <= int(event["frame"]) <= 1800 for event in events),
          {"capture": capture, "seconds": result["seconds"],
           "last_event_frame": max((int(event["frame"]) for event in events), default=None)})
    actual_script_path = data / "script.json"
    actual_script = json.loads(actual_script_path.read_text(encoding="utf-8")) if actual_script_path.is_file() else {}
    check("exact_executed_script", actual_script == {"actions": actions},
          {"frozen_actions": len(actions), "exported_actions": len(actual_script.get("actions", [])),
           "script_input": script_binding})
    indexed_events = {}
    for event in events:
        indexed_events.setdefault(int(event["action"]), []).append(event)
    action_failures = []
    for index, action in enumerate(actions):
        event_names = {"Click": {"clicked", "combo-options-visible"}, "Open": {"clicked", "already-open"},
                       "Type": {"edited"}, "Choose": {"chosen"}, "Expect": {"observed"},
                       "Inspect": {str(action["value"]) + "-observed"}, "UiImage": {"full-editor-png"},
                       "Image": {"viewport-png"}, "RoomImage": {"room-viewport-png"},
                       "Keyboard": {"host-key"}, "Viewport": {"viewport-click-observed"},
                       "PickMesh": {"viewport-picked"}, "DragMesh": {"gizmo-drag-applied"},
                       "Pointer": {"outline-pointer"}, "Wheel": {"outline-wheel"}, "Resize": {"outline-resized"},
                       "Dpi": {"outline-dpi"}, "Wait": {"outline-wait"}, "Stimulus": {"outline-stimulus"},
                       "NativeImage": {"native-viewport-png"}}.get(action["op"])
        if not event_names:
            continue
        matching = [event for event in indexed_events.get(index, []) if event["event"] in event_names and
                    (action["op"] in ("Viewport", "PickMesh", "DragMesh") or
                     event["key"] == (action["value"] if action["op"] == "Stimulus" else action["key"] or ""))]
        if not matching:
            action_failures.append({"action_index": index, "action": action,
                                    "actual": indexed_events.get(index, [])})
    check("every_scripted_ui_action_observed", not action_failures, action_failures)
    for event_name, expected_counts in plan["required_events"].items():
        actual_counts = Counter(event["key"] for event in events if event["event"] == event_name)
        check("required_" + event_name, all(actual_counts[key] >= count for key, count in expected_counts.items()),
              {"required": expected_counts, "actual": dict(actual_counts)})
    readback_path = data / "component-readback.jsonl"
    observations, parse_errors = [], []
    if readback_path.is_file():
        for line_number, line in enumerate(readback_path.read_text(encoding="utf-8").splitlines(), 1):
            try:
                value = json.loads(line)
                if not isinstance(value, dict):
                    raise ValueError("Readback record must be an object")
                observations.append(value)
            except (ValueError, TypeError) as error:
                parse_errors.append({"line": line_number, "error": str(error)})
    check("complete_ordered_readback", not parse_errors and
          [observation.get("key") for observation in observations] == plan["expected_assertions"] and
          all(observation.get("passed") is True and isinstance(observation.get("values"), dict)
              for observation in observations),
          {"expected": plan["expected_assertions"], "actual": [observation.get("key") for observation in observations],
           "parse_errors": parse_errors})
    occurrences = Counter()
    for index, observation in enumerate(observations):
        key = observation.get("key")
        expected = plan["expected_values"].get(key)
        occurrence = occurrences[key]
        occurrences[key] += 1
        if isinstance(expected, list):
            expected = expected[occurrence] if occurrence < len(expected) else None
        table = plan.get("expected_values_by_tick", {}).get(key)
        if table is not None:
            tick = observation.get("values", {}).get("tick")
            packet = table.get(str(tick)) if type(tick) is int else None
            expected = dict(expected, **packet) if isinstance(expected, dict) and packet is not None else None
        mismatch = component_value_mismatches(observation.get("values"), expected, plan.get("numeric_tolerance", 0))
        check(f"readback_{index:03}_{key}", expected is not None and observation.get("passed") is True and not mismatch,
              {"key": key, "occurrence": occurrence, "mismatches": mismatch,
               "observed_values": observation.get("values")})
    for name in plan["screenshots"]:
        dimensions = png_dimensions(data / (name + ".png"))
        check("actual_editor_screenshot_" + name,
              bool(dimensions and 0 < min(dimensions) and max(dimensions) <= 4096 and
                   any(event["event"] == "full-editor-png" and event["key"] == name for event in events)), dimensions)
    for name in plan.get("native_screenshots", []):
        dimensions = png_dimensions(data / (name + ".png"))
        check("native_viewport_screenshot_" + name,
              bool(dimensions and 0 < min(dimensions) and max(dimensions) <= 4096 and
                   any(event["event"] == "native-viewport-png" and event["key"] == name for event in events)), dimensions)
    status = "PASS" if all(item["result"] == "PASS" for item in checks) else "FAIL"
    write(directory / "audit.json", {"status": status, "checks": checks, "F_OWNER": "PENDING",
          "scope": "one focused actual SDL/ImGui session; property/context matrix closure needs separately justified evidence mapping",
          "component_plan": binding, "coverage_row_ids": plan["coverage"]["row_ids"],
          "inputs_sha256": sha(directory / "inputs.json"), "result_sha256": sha(directory / "result.json"),
          "auditor_sha256": sha(Path(__file__)), "raw_sha256": {p.name: sha(p) for p in data.iterdir() if p.is_file()}})
    print(json.dumps({"status": status, "checks": len(checks)}))


def audit_editor(directory: Path, record: dict, data: Path, result: dict) -> None:
    role = record.get("editor_session")
    if role not in EDITOR_ROLES:
        raise ValueError("Ordinary Editor audit needs an admitted session role")
    if role in COMPONENT_EDITOR_ROLES:
        audit_component_editor(directory, record, data, result)
        return
    if role.startswith("properties-") or role == "direct-default-jolt" or role in GENERAL_EDITOR_ROLES:
        audit_property_editor(directory, record, data, result)
        return
    case, backend, scene, explicit = EDITOR_ROLES[role]
    capture = result.get("capture") or {}
    checks: list[dict] = []
    def check(name: str, ok: bool, actual: object, criterion: str) -> None:
        checks.append({"check": name, "result": "PASS" if ok else "FAIL",
                       "actual": actual, "criterion": criterion})
    expected_saved = {"fresh-jolt": "none", "saved-native": "native",
                      "override-native": "jolt"}[role]
    expected_end = {"fresh-jolt": "native", "saved-native": "jolt",
                    "override-native": "jolt"}[role]
    startup_path = data / "editor-startup.json"
    startup = json.loads(startup_path.read_text(encoding="utf-8")) if startup_path.is_file() else {}
    check("startup_precedence", record.get("case") == case and record.get("backend") == backend and
          record.get("cli_backend_explicit") is explicit and
          startup == {"session": role, "cli_backend_explicit": explicit,
                      "saved_backend": expected_saved, "app_backend": backend,
                      "world_backend": backend, "scene": scene} and
          capture.get("editor_session") == role and
          capture.get("saved_backend_at_end") == expected_end,
          {"startup": startup, "capture_session": capture.get("editor_session"),
           "saved_at_end": capture.get("saved_backend_at_end")},
          "actual startup CLI > saved > Jolt precedence and final saved preference for this session")
    check("ordinary_editor_process", result["exit_code"] == 0 and not result["timed_out"] and
          result["inputs_unchanged"] and capture.get("complete") and
          0 <= capture.get("physics_ticks", 601) <= 600 and result.get("seconds", 181) <= 180,
          result, "successful actual Release Editor process <=180s and <=600 aggregate fixed ticks")
    names = (["hinge-basic", "hinge-quaternion", "hinge-limited", "cv-free", "cv-limited"]
             if role != "override-native" else [])
    images = {name + ".png": png_dimensions(data / (name + ".png")) for name in names}
    check("actual_viewport_images", capture.get("images") == len(names) and
          all(extent == (512, 288) for extent in images.values()),
          images, "five actual Tick-0 Editor viewport PNGs in each capture session")
    legacy = ("geometry-gallery", "box-stack", "sphere-box-stack", "sphere-lattice",
              "distance-chain", "ball-socket-p")
    room_names = ([f"room-{provider}-{scene}.png" for provider in ("native", "jolt")
                   for scene in legacy] if role == "override-native" else [])
    room_images = {name: png_dimensions(data / name) for name in room_names}
    check("actual_legacy_room_images", capture.get("room_images") == len(room_names) and
          all(extent == (512, 288) for extent in room_images.values()),
          room_images, "twelve actual Native/Jolt legacy scene viewport PNGs when the override session exercises both providers")
    ui_names = ({"fresh-jolt": (), "saved-native": ("editor-native-with-cards.png",),
                 "override-native": ("editor-native-override-with-cards.png",
                                     "editor-jolt-with-cards.png")})[role]
    ui_images = {name: png_dimensions(data / name) for name in ui_names}
    check("actual_full_editor_ui", all(extent is not None and 0 < extent[0] <= 4096 and
          0 < extent[1] <= 4096 for extent in ui_images.values()), ui_images,
          "actual primary Editor UI screenshots after the later installed-card sessions; fresh capture has no loaded-card claim")
    events_path = data / "editor-events.csv"
    events = rows(events_path) if events_path.is_file() else []
    clicked = {e["key"] for e in events if e["event"] == "item-clicked"}
    observed = {e["key"] for e in events if e["event"] == "observed"}
    angular_cards = {"preset.hinge-basic", "preset.hinge-quaternion",
                     "preset.hinge-limited", "preset.cv-free", "preset.cv-limited"}
    common = {"content-tab", "scene-load", "create-enhanced", "mass", "apply"}
    if role == "fresh-jolt":
        required_clicks = common | angular_cards | {"scale", "gravity", "limit-lower",
            "limit-upper", "limit-u", "limit-v", "mode-toggle", "impulse", "reset",
            "revert", "restore-defaults", "backend.native", "backend-apply"}
        required_observed = {"hinge-basic", "hinge-quaternion", "hinge-limited",
            "cv-free", "cv-limited", "enhanced", "applied-current", "applied-hinge",
            "applied-cv", "reset-retained", "invalid-mass", "reverted", "defaults",
            "reset", "running", "paused", "resumed", "impulse", "native"}
    elif role == "saved-native":
        required_clicks = common | angular_cards | {"scale", "gravity", "limit-u",
            "limit-v", "mode-toggle", "impulse", "reset", "revert", "restore-defaults",
            "backend.jolt", "backend-apply", "hierarchy.bar", "hierarchy.floor",
            "hierarchy.wall", "hierarchy.anchor"}
        required_observed = {"thumbnails-loaded", "hinge-basic", "hinge-quaternion",
            "hinge-limited", "cv-free", "cv-limited", "enhanced", "applied-cv",
            "invalid-mass", "reverted", "defaults", "reset", "running", "paused",
            "resumed", "impulse", "jolt", "selected-bar", "selected-floor",
            "selected-wall", "selected-anchor"}
    else:
        required_clicks = {"content-tab", "scene-load", "preset.hinge-limited",
            "backend.jolt", "backend.native", "backend-apply", "reset",
            "hierarchy.bar", "hierarchy.floor", "hierarchy.wall", "hierarchy.anchor"} | \
            {"preset." + name for name in legacy}
        required_observed = {"thumbnails-loaded", "hinge-limited", "jolt",
            "reset-reference", "selected-bar", "selected-floor", "selected-wall",
            "selected-anchor", "failed-switch"} | set(legacy)
    check("actual_widget_activation", required_clicks <= clicked,
          {"missing": sorted(required_clicks-clicked), "clicked": sorted(clicked)},
          "actual targeted ImGui items receive mouse clicks; no direct app method substitutes for UI controls")
    combo_open = "backend" in clicked or any(e["event"] == "combo-options-visible" and
                                              e["key"] == "backend" for e in events)
    check("backend_combo_open", combo_open,
          {"item_clicked": "backend" in clicked,
           "options_visible_after_mouse": any(e["event"] == "combo-options-visible" for e in events)},
          "injected actual combo mouse press/release opens real Native/Jolt Selectable popup; option and Apply require separate clicks")
    check("actual_state_readback", required_observed <= observed,
          {"missing": sorted(required_observed-observed)},
          "UI actions produce committed packet/provider/backend/lifecycle observations")
    host = [e["key"] for e in events if e["event"] == "host-key"]
    other = [e["event"] for e in events]
    expected_host = ({"fresh-jolt": {"S": 6, "P": 6, "R": 1},
                      "saved-native": {"S": 1, "P": 2, "R": 1},
                      "override-native": {"S": 0, "P": 0, "R": 0}})[role]
    check("host_lifecycle_shortcuts", all(host.count(key) == count for key, count in
          expected_host.items()) and (role == "override-native" or "viewport-click-observed" in other),
          {"host_keys": host, "viewport_focus_observed": "viewport-click-observed" in other},
          "actual SDL S/P/R event route after observed ImGui viewport activation")
    if role == "fresh-jolt":
        check("reset_from_running", "reset-started-running" in other,
              {"actual_running_reset_click": "reset-started-running" in other},
              "actual Reset click while Running followed by existing restored provider readback")
    expected_negative = ("failed-switch" in observed if role == "override-native" else
                         "stale-invalid-atomic-rejection" in other)
    check("atomic_negative_cases", expected_negative,
          {"event": expected_negative},
          "ordinary Editor verifies rejected mutation or injected provider switch without changing committed world")
    check("session_startup_event", any(e["event"] == "startup-precedence" and
          e["key"] == role for e in events), {"role": role},
          "actual session role is recorded at app startup")
    status = "FAIL" if any(c["result"] == "FAIL" for c in checks) else "PASS"
    report = {"status": status, "checks": checks, "matrix_sha256": MATRIX_SHA256,
              "scope": "single ordinary Editor session; linked thumbnail/card gate requires all three sessions",
              "auditor_sha256": sha(Path(__file__)),
              "raw_sha256": {p.name: sha(p) for p in data.iterdir() if p.is_file()}}
    write(directory / "audit.json", report)
    print(json.dumps({"status": status, "checks": len(checks)}, indent=2))


def audit_editor_aggregate(fresh_name: str, saved_name: str, override_name: str,
                           output_name: str) -> None:
    """Close the deferred five-card gate with three actual, separately audited sessions."""
    locations = [safe(ROOT / name) for name in (fresh_name, saved_name, override_name)]
    if len(set(locations)) != 3 or any(p.parent != BASE or not p.name.startswith("run-Release-")
                                      for p in locations):
        raise ValueError("Editor aggregate needs three distinct Phase 28 Release run directories")
    records = []
    for role, directory in zip(EDITOR_ROLES, locations):
        identity = json.loads((directory / "inputs.json").read_text(encoding="utf-8"))
        result = json.loads((directory / "result.json").read_text(encoding="utf-8"))
        audit = json.loads((directory / "audit.json").read_text(encoding="utf-8"))
        if identity.get("editor_session") != role or audit.get("status") != "PASS" or \
           not result.get("capture", {}).get("complete") or \
           result.get("inputs_unchanged") is not True:
            raise ValueError(f"Editor aggregate cannot use unqualified session: {directory}")
        records.append((identity, result, audit))
    if len({item[0]["executable"]["sha256"] for item in records}) != 1:
        raise ValueError("Linked Editor sessions used different application products")
    for first, second in ((0, 1), (1, 2)):
        ending = locations[first] / "imgui.ini"
        following = locations[second] / "initial-imgui.ini"
        if not ending.is_file() or not following.is_file() or sha(ending) != sha(following):
            raise ValueError("Saved Editor preference did not carry byte-exactly to next session")
    runtime = runtime_input_record()
    manifest = json.loads((ROOT / "tools/runtime_assets.json").read_text(encoding="utf-8"))
    names = ("hinge-basic", "hinge-quaternion", "hinge-limited", "cv-free", "cv-limited")
    images = {}
    for name in names:
        resource = f"Images/Editor/Scenes/{name}.png"
        source_name = runtime["resource_sources"][resource]
        source = safe(ROOT / source_name)
        staged = safe(ROOT / "bin/phase28-qualification/Release/assets" / resource)
        captured = locations[0] / "data" / (name + ".png")
        digest = sha(captured)
        if manifest.get("files", {}).get(resource) != source_name or \
           not source.is_file() or not staged.is_file() or \
           png_dimensions(captured) != (512, 288) or \
           sha(source) != digest or sha(staged) != digest:
            raise ValueError(f"Five-card source/staged bytes differ from actual Jolt capture: {name}")
        staged_key = staged.relative_to(ROOT).as_posix()
        if records[0][0]["sources"]["staged_files"].get(staged_key) is not None or \
           any(item[0]["sources"]["staged_files"].get(staged_key, {}).get("sha256") != digest
               for item in records[1:]):
            raise ValueError(f"Editor session asset transition is not pinned: {name}")
        images[resource] = digest
    required_ui = ((1, "editor-native-with-cards.png"),
                   (2, "editor-native-override-with-cards.png"),
                   (2, "editor-jolt-with-cards.png"))
    for index, name in required_ui:
        if png_dimensions(locations[index] / "data" / name) is None:
            raise ValueError(f"Linked actual Editor UI screenshot missing: {name}")
    output = safe(ROOT / output_name)
    if output.parent != BASE or not output.name.startswith("editor-aggregate-") or \
       output.suffix != ".json":
        raise ValueError("Editor aggregate output must be a new Phase 28 root evidence JSON")
    report = {"status": "PASS", "gate": "D_EDITOR-linked-thumbnail-and-precedence",
              "decision_sha256": EDITOR_ACCEPTANCE_SHA256,
              "sessions": [{"role": role, "path": directory.relative_to(ROOT).as_posix(),
                            "inputs_sha256": sha(directory / "inputs.json"),
                            "result_sha256": sha(directory / "result.json"),
                            "audit_sha256": sha(directory / "audit.json")}
                           for role, directory in zip(EDITOR_ROLES, locations)],
              "jolt_tick0_capture_and_installed_thumbnails": images,
              "full_editor_ui": [{"session": i, "name": name,
                                  "sha256": sha(locations[i] / "data" / name)}
                                 for i, name in required_ui]}
    write(output, report)
    print(json.dumps({"status": "PASS", "gate": report["gate"]}, indent=2))


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build", help="one affected isolated-output build")
    b.add_argument("--config", choices=["Debug", "Release"], required=True)
    b.add_argument("--attempt", required=True)
    s = sub.add_parser("stage", help="refresh isolated runtime assets after reviewed source installation")
    s.add_argument("--config", choices=["Debug", "Release"], required=True)
    s.add_argument("--attempt", required=True)
    r = sub.add_parser("run", help="one admitted RBS physical process; never retries")
    r.add_argument("--config", choices=["Debug", "Release"], required=True)
    r.add_argument("--case", choices=list(SCENE), required=True)
    r.add_argument("--backend", choices=["native", "jolt"], required=True)
    r.add_argument("--attempt", required=True)
    r.add_argument("--correction", help="existing Phase 28 correction JSON for a replacement")
    e = sub.add_parser("editor", help="one ordinary UI-driven Release Editor process")
    e.add_argument("--role", choices=list(EDITOR_ROLES), required=True)
    e.add_argument("--attempt", required=True)
    e.add_argument("--correction", help="existing Phase 28 correction JSON for a replacement")
    e.add_argument("--session-plan", help="frozen component-ui-02 script, source, executable, coverage and expectation packet")
    a = sub.add_parser("audit", help="audit one existing raw process; never runs RBS")
    a.add_argument("run_directory")
    a.add_argument("--output", help="create-only run-local supplemental audit JSON")
    a.add_argument("--allocation-evidence", help="run-local source-attributed heap evidence JSON")
    aggregate = sub.add_parser("audit-editor-aggregate", help="close linked three-session thumbnail and preference evidence")
    aggregate.add_argument("--fresh", required=True)
    aggregate.add_argument("--saved", required=True)
    aggregate.add_argument("--override", required=True)
    aggregate.add_argument("--output", required=True)
    sub.add_parser("matrix", help="print frozen matrix identity and admitted counts")
    args = p.parse_args()
    if args.command == "build":
        build(args.config, args.attempt)
    elif args.command == "stage":
        stage(args.config, args.attempt)
    elif args.command == "run":
        run(args.config, args.case, args.backend, args.attempt, args.correction)
    elif args.command == "editor":
        editor(args.role, args.attempt, args.correction, args.session_plan)
    elif args.command == "audit":
        audit(args.run_directory, args.output, args.allocation_evidence)
    elif args.command == "audit-editor-aggregate":
        audit_editor_aggregate(args.fresh, args.saved, args.override, args.output)
    else:
        m = matrix()
        campaign = accepted_run_budget()
        print(json.dumps({"sha256": MATRIX_SHA256,
                          "historical_matrix_limits": m["administrative_limits"],
                          "effective_campaign_limits": campaign["accepted_limits"],
                          "campaign_sha256": COMPONENT_ACCEPTANCE_SHA256,
                          "cases": list(SCENE)}, indent=2))


if __name__ == "__main__":
    main()
