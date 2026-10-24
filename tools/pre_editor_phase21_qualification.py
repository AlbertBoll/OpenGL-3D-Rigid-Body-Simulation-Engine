"""Bounded, owner-directed Phase21 qualification. No publication operations."""
import argparse
import csv
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import time
from xml.sax.saxutils import escape

import pre_editor_resting_contact_diagnosis as process_tools
from pre_editor_phase20_comprehensive import NativeOwner

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "logs/pre_editor/phase-21"
AUTH = ROOT / "docs/pre_editor/active/PHASE_21_OD06_OD07_ACCEPTANCE_01.json"
DIAG_AUTH = ROOT / "docs/pre_editor/active/PHASE_21_PENETRATION_DIAGNOSTIC_ACCEPTANCE_01.json"
QUAL_AUTH_02 = ROOT / "docs/pre_editor/active/PHASE_21_QUALIFICATION_AUTHORITY_02.json"
QUAL_AUTH_03 = ROOT / "docs/pre_editor/active/PHASE_21_QUALIFICATION_AUTHORITY_03.json"
QUAL_AUTH = ROOT / "docs/pre_editor/active/PHASE_21_QUALIFICATION_AUTHORITY_04.json"
LONG_AUTH = ROOT / "docs/pre_editor/active/PHASE_21_LONG_TAIL_DIAGNOSTIC_AUTHORITY_01.json"
CAUSAL_AUTH = ROOT / "docs/pre_editor/active/PHASE_21_L60_CAUSAL_DIAGNOSTIC_AUTHORITY_01.json"
REFINEMENT_AUTH_01 = ROOT / "docs/pre_editor/active/PHASE_21_COUPLED_REFINEMENT_EXECUTION_AUTHORITY_01.json"
REFINEMENT_AUTH_01_SHA = "edb1090df8fb76eae57d3d1c73ca1b1569b8aef990d9cee3e898fb89d9c5e2b7"
REFINEMENT_AUTH = ROOT / "docs/pre_editor/active/PHASE_21_EXPANDED_ENGINEERING_AUTHORITY_02.json"
REFINEMENT_AUTH_SHA = "23909704629156f8814145e4d1445b42ca1e027ce6dbf665b14198162d52ffd3"
REFINEMENT_AUTHORITIES = {REFINEMENT_AUTH_SHA, REFINEMENT_AUTH_01_SHA}
REFINEMENT_OWNED = {"GEngine/include/GEngine/Physics/Manifold.cpp",
    "GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h",
    "GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp",
    "RigidBodySimulation/tests/RestingContactDiagnosis.h", "tools/pre_editor_phase21_qualification.py"}
CONTROL02 = ROOT / "logs/pre_editor/phase-20/conditional-remediation-01/runs/Release-control-02"
CONTROL02_SHA = "00a915b5010bb66fa95d0b9525487c57ce2226255614c4218e3e3af218c05451"
PRIOR = ROOT / "logs/pre_editor/phase-20/comprehensive-remediation-01/performance-final-01/relevant-inputs.json"
MSBUILD = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe")
OWNED = {"RigidBodySimulation/tests/RestingContactDiagnosis.h", "tools/pre_editor_phase21_qualification.py"}
BUILD_NAMES = {"Debug": "Debug-current-qualification-03", "Release": "Release-current-qualification-01"}
RUNNER_PATH = "tools/pre_editor_phase21_qualification.py"
LINK_INPUT_PROOF = ROOT / "logs/pre_editor/phase-20/comprehensive-remediation-01/proofs/performance-control02-options-01.json"
LINK_INPUT_PROOF_SHA = "76655e12783fd99e186c70b8ca067d49ebf749bd810b00368071e7d74760e195"


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def safe(p):
    p = Path(p)
    if not p.is_absolute():
        p = ROOT / p
    assert p.resolve().is_relative_to(ROOT), str(p)
    for a in (p, *p.parents):
        if a.exists():
            assert not a.is_symlink() and not (getattr(a.lstat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT), str(a)
        if a == ROOT:
            break
    return p


def write(p, value):
    p = safe(p)
    p.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(value, bytes):
        with p.open("xb") as f:
            f.write(value)
        return
    with p.open("x", encoding="utf-8", newline="\n") as f:
        if isinstance(value, str):
            f.write(value)
        else:
            json.dump(value, f, indent=2)
            f.write("\n")


def git(*args):
    return subprocess.check_output(["git", "-c", "safe.directory=" + ROOT.as_posix(), *args], cwd=ROOT,
                                   env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"})


def authority():
    if REFINEMENT_AUTH.exists():
        return refinement_authority()
    a = json.loads(AUTH.read_text())
    assert a["status"] == "ACCEPTED_OWNER_DIRECTED_PROSPECTIVE_REVISION_CONTINUE_EXISTING_START"
    for key in ("proposal", "contract_amendment"):
        assert sha(ROOT / a[key]["path"]) == a[key]["sha256"]
    assert git("rev-parse", "HEAD").decode().strip() == a["baseline"]
    assert git("branch", "--show-current").decode().strip() == "pre-editor/refactor"
    assert not git("diff", "--cached", "--name-only").strip()
    assert set(git("diff", "--name-only").decode().splitlines()) <= OWNED
    entry = json.loads((BASE / "entry-01/entry.json").read_text())
    assert sha(Path(entry["index"]["path"])) == entry["index"]["sha256"]
    for p, v in entry["protected"].items():
        if p != "docs/pre_editor/active/REFACTOR_STATE.md":
            assert sha(ROOT / p) == v["sha256"], p
    assert sha(ROOT / "RigidBodySimulation/src/RigidBodySimulation.cpp") == entry["canonical_rbs"]["worktree"]["sha256"]
    assert not (ROOT / "pre-editor-checkpoints/phase-21.json").exists()
    if DIAG_AUTH.exists():
        d = json.loads(DIAG_AUTH.read_text())
        assert d["status"] == "ACCEPTED_DIAGNOSTIC_ONLY_CONTINUATION_01"
        assert d["baseline"] == a["baseline"] and set(d["implementation_files"]) == OWNED
        assert d["native_launches"] == 1 and d["unchanged_penetration_limit"] == a["criteria"]["lattice_penetration"] == .05
        assert not any(d[k] for k in ("production_changes_authorized", "qualification_retry_authorized", "approval_or_publication_authorized"))
        for key in ("primary_authority", "proposal", "plan", "preservation"):
            assert sha(safe(d[key]["path"])) == d[key]["sha256"], key
        a["diagnostic"] = d
    if QUAL_AUTH_02.exists():
        q = json.loads(QUAL_AUTH_02.read_text())
        assert q["status"] == "ACCEPTED_CONTINUE_EXISTING_START_AMENDMENT_02"
        assert q["baseline"] == a["baseline"] and set(q["implementation_files"]) == OWNED
        assert q["limits"] == a["limits"]
        assert not any(q[k] for k in ("production_changes_authorized", "new_standalone_diagnostic_authorized", "approval_or_publication_authorized"))
        for key in ("activation", "matrix", "preservation", "primary_authority", "consumed_diagnostic_authority"):
            assert sha(safe(q[key]["path"])) == q[key]["sha256"], key
        active = json.loads(safe(q["activation"]["path"]).read_text())
        assert active["status"] == "ACCEPTED_ACTIVE_PROSPECTIVE_AMENDMENT_02"
        for item in active["package"].values():
            assert sha(safe(item["path"])) == item["sha256"]
        a["qualification"] = q
    if LONG_AUTH.exists():
        d = json.loads(LONG_AUTH.read_text())
        assert d["status"] == "ACCEPTED_ONE_TIME_LONG_TAIL_DIAGNOSTIC_ONLY"
        assert d["baseline"] == a["baseline"] and set(d["implementation_files"]) == OWNED and d["limits"] == a["limits"]
        assert (d["configuration"], d["case"], d["native_launches"], d["completed_ticks"], d["hz"], d["passes"], d["sleep"]) == ("Release", "long-tail", 1, 14400, 120, 1, True)
        assert d["ordinary_duration_violation_continuation_only"]
        assert not any(d[k] for k in ("qualification_retry_authorized", "production_changes_authorized", "threshold_changes_authorized", "approval_or_publication_authorized"))
        for key in ("primary_authority", "amended_qualification_authority", "prior_failure", "plan", "preservation"):
            assert sha(safe(d[key]["path"])) == d[key]["sha256"], key
        a["long_tail"] = d
    if QUAL_AUTH_03.exists():
        q = json.loads(QUAL_AUTH_03.read_text())
        assert q["status"] == "ACCEPTED_CONTINUE_EXISTING_START_AMENDMENT_03"
        assert q["baseline"] == a["baseline"] and set(q["implementation_files"]) == OWNED
        assert q["limits"] == a["limits"] and q["native_attempt_cap"] == 7 and q["cumulative_native_attempt_cap"] == 12
        assert q["build_command_cap_per_configuration"] == 1
        assert not any(q[k] for k in ("production_changes_authorized", "new_standalone_diagnostic_authorized", "approval_or_publication_authorized"))
        for key in ("activation", "matrix", "preservation", "feasibility", "plan", "prior_qualification_authority", "consumed_long_tail_authority", "primary_authority"):
            assert sha(safe(q[key]["path"])) == q[key]["sha256"], key
        active = json.loads(safe(q["activation"]["path"]).read_text())
        assert active["status"] == "ACCEPTED_ACTIVE_PROSPECTIVE_AMENDMENT_03"
        assert (active["sphere_sphere_ms"], active["sphere_boundary_ms"], active["continuous_recovery_ms"]) == (200,100,250)
        for item in active["package"].values():
            assert sha(safe(item["path"])) == item["sha256"]
        assert json.loads(safe(q["feasibility"]["path"]).read_text())["status"] == "PASS_SOURCE_ONLY_FEASIBILITY_NOT_P1_CLOSURE"
        a["historical_qualification"] = a.get("qualification")
        a["qualification"] = q
    if QUAL_AUTH.exists():
        q = json.loads(QUAL_AUTH.read_text())
        assert q["status"] == "ACCEPTED_STOP05_BOUNDED_SUPPORT_RECOVERY_AUTHORITY_04"
        assert q["baseline"] == a["baseline"] and set(q["implementation_files"]) == OWNED
        assert q["limits"] == a["limits"] and q["native_attempt_cap"] == 7 and q["cumulative_native_attempt_cap"] == 13
        assert q["build_command_cap_per_configuration"] == q["additional_Debug_build_commands"] == q["replacement_Debug_native_attempts"] == 1
        assert not any(q[k] for k in ("production_changes_authorized", "new_standalone_diagnostic_authorized", "approval_or_publication_authorized"))
        for key in ("activation", "matrix", "preservation", "feasibility", "plan", "prior_qualification_authority", "consumed_long_tail_authority", "primary_authority", "prior_stop", "accepted_proposal"):
            assert sha(safe(q[key]["path"])) == q[key]["sha256"], key
        assert q["prior_qualification_authority"]["sha256"] == sha(QUAL_AUTH_03)
        assert q["activation"] == a["qualification"]["activation"] and q["slots"] == a["qualification"]["slots"]
        feasibility = json.loads(safe(q["feasibility"]["path"]).read_text())
        assert feasibility["status"] == "PASS_BOUNDED_SOURCE_TIMING_FEASIBILITY_NOT_RUNTIME_GUARANTEE_OR_P1_CLOSURE"
        assert not feasibility["definite_execution_blocker_identified"]
        a["stopped_qualification_03"] = a["qualification"]
        a["qualification"] = q
    if CAUSAL_AUTH.exists():
        c = json.loads(CAUSAL_AUTH.read_text())
        assert c["status"] == "ACCEPTED_ONE_TIME_L60_TICK73_CAUSAL_CAPTURE"
        assert c["baseline"] == a["baseline"] and set(c["implementation_files"]) == OWNED and c["limits"] == a["limits"]
        assert (c["configuration"],c["case"],c["build_commands"],c["native_launches"],c["ticks"],c["hz"],c["passes"],c["sleep"]) == ("Release","L60-causal",1,1,73,60,1,True)
        assert c["cumulative_native_attempt_cap"] == 14 and c["additional_launch_separate_from_blocked_qualification_slots"]
        assert not any(c[k] for k in ("production_changes_authorized","qualification_continuation_authorized","threshold_changes_authorized","approval_or_publication_authorized"))
        for key in ("primary_authority","stopped_qualification_authority","prior_failure","prior_handoff","feasibility","preservation","plan"):
            assert sha(safe(c[key]["path"])) == c[key]["sha256"], key
        assert json.loads(safe(c["feasibility"]["path"]).read_text())["status"] == "PASS_SOURCE_ONLY_BOUNDED_CAUSAL_CAPTURE_FEASIBILITY"
        a["causal"] = c
    return a


def fresh(kind, name):
    for i in range(1, 1000):
        p = safe(BASE / kind / f"{name}-{i:02}")
        if not p.exists():
            p.mkdir(parents=True)
            return p
    raise RuntimeError("no fresh evidence destination")


def usage():
    result = {"build_seconds": 0., "native_seconds": 0., "native_launches": 0, "build_commands": 0}
    for kind, key in (("builds", "build_seconds"), ("native", "native_seconds")):
        for p in (BASE / kind).glob("*/invocation.json"):
            result_path = p.with_name("result.json")
            assert result_path.exists(), "unfinished attempt: " + str(p)
            result[key] += json.loads(result_path.read_text())["wall_seconds"]
            if kind == "native":
                result["native_launches"] += int(json.loads(result_path.read_text()).get("launch_started", False))
            else:
                result["build_commands"] += 1
    return result


def environment():
    env = os.environ.copy()
    for key in list(env):
        if key in ("CL", "_CL_", "GENGINE_ASSET_ROOT", "GENGINE_BASELINE_OUTPUT") or (key.startswith("GENGINE_") and "REQUEST" in key):
            env.pop(key)
    temp = safe(ROOT / "runtime/tmp/phase-21")
    temp.mkdir(parents=True, exist_ok=True)
    env.update(TMP=str(temp), TEMP=str(temp), GIT_OPTIONAL_LOCKS="0")
    return env


def refinement_prebuilt_inputs():
    """The already accepted prebuilt link inputs, not generated build products."""
    assert sha(safe(LINK_INPUT_PROOF)) == LINK_INPUT_PROOF_SHA
    prior = json.loads(LINK_INPUT_PROOF.read_text())
    libraries = {p: h for p, h in prior["matched_relevant_build_dependency_asset_inputs"].items()
                 if p.startswith("external/") and p.endswith(".lib")}
    assert len(libraries) == 13
    return libraries


def inputs():
    """Reuse the actual RBS consumed closure, dropping superseded Phase20 governance/overlays."""
    prior = json.loads(PRIOR.read_text())
    paths = {p for p in prior["workspace_hashes"] if not p.startswith(("logs/", "docs/pre_editor/active/PHASE_20", "docs/pre_editor/phases/PHASE_20"))}
    paths.update(OWNED)
    if REFINEMENT_AUTH.exists():
        paths.update(REFINEMENT_OWNED)
        paths.update(refinement_prebuilt_inputs())
        paths.add(REFINEMENT_AUTH.relative_to(ROOT).as_posix())
        paths.update(v["path"] for v in json.loads(REFINEMENT_AUTH.read_text()).values()
                     if isinstance(v, dict) and "path" in v and "sha256" in v)
    paths.update([AUTH.relative_to(ROOT).as_posix(), "docs/pre_editor/active/PHASE_21_PROSPECTIVE_CONTRACT_AMENDMENT_01.md",
                  "docs/pre_editor/active/PHASE_21_OD06_OD07_PROPOSAL_01.md", "docs/pre_editor/phases/PHASE_21.md"])
    if DIAG_AUTH.exists():
        d = json.loads(DIAG_AUTH.read_text())
        paths.update([DIAG_AUTH.relative_to(ROOT).as_posix(), d["plan"]["path"], d["proposal"]["path"]])
    if QUAL_AUTH.exists():
        q = json.loads(QUAL_AUTH.read_text())
        paths.update([QUAL_AUTH.relative_to(ROOT).as_posix(), q["activation"]["path"], q["matrix"]["path"]])
        paths.update([q["feasibility"]["path"], q["plan"]["path"]])
        paths.add(QUAL_AUTH_03.relative_to(ROOT).as_posix())
        active = json.loads(safe(q["activation"]["path"]).read_text())
        paths.update(v["path"] for v in active["package"].values())
    if LONG_AUTH.exists():
        d = json.loads(LONG_AUTH.read_text())
        paths.update([LONG_AUTH.relative_to(ROOT).as_posix(), d["plan"]["path"]])
    if CAUSAL_AUTH.exists():
        d = json.loads(CAUSAL_AUTH.read_text())
        paths.update([CAUSAL_AUTH.relative_to(ROOT).as_posix(), d["plan"]["path"], d["feasibility"]["path"]])
    scopes = {p: names for p, names in prior["discovery"].items() if not p.startswith("logs/")}
    discovery = {}
    for p in scopes:
        directory = safe(p)
        discovery[p] = sorted(x.name for x in directory.iterdir() if x.is_file() and x.suffix.lower() in
                              {".h", ".hpp", ".cpp", ".c", ".inl", ".hh", ".hxx", ".inc", ".cxx", ".ixx"})
        assert discovery[p] == scopes[p], "include discovery drift: " + p
    workspace = {p: sha(safe(p)) for p in sorted(paths)}
    if REFINEMENT_AUTH.exists():
        assert all(workspace[p] == h for p, h in refinement_prebuilt_inputs().items()), "prebuilt link input drift"
    external = {p: sha(p) for p in prior["external_toolchain_dependency_hashes"]}
    assert external == prior["external_toolchain_dependency_hashes"], "toolchain/dependency change"
    unexpected = {p for p, v in prior["workspace_hashes"].items() if p in workspace and workspace[p] != v}
    allowed = {"RigidBodySimulation/tests/RestingContactDiagnosis.h", "RigidBodySimulation/src/main.cpp"}
    if REFINEMENT_AUTH.exists():
        allowed.update(REFINEMENT_OWNED)
    assert unexpected <= allowed, sorted(unexpected)
    return {"workspace_hashes": workspace, "external_toolchain_dependency_hashes": external,
            "discovery": discovery, "source_closure_origin": {"path": str(PRIOR.relative_to(ROOT)), "sha256": sha(PRIOR)},
            "classified_differences": sorted(unexpected)}


def libraries(configuration):
    old = ROOT / "logs/pre_editor/phase-20/comprehensive-remediation-01/builds" / BUILD_NAMES[configuration] / "result.json"
    data = json.loads(old.read_text())
    assert data["status"] == "PASS"
    products = {p: h for p, h in data["products"].items() if p.endswith(".lib")}
    assert len(products) == 2
    for p, h in products.items():
        assert sha(safe(p)) == h, "changed reusable dependency: " + p
    return {"original_build": str(old.relative_to(ROOT)), "original_build_sha256": sha(old), "products": products,
            "impact": "All engine/glad production, project, header and external inputs match the original qualification closure. Only the RBS fixture and separately approved normal main.cpp preset differ. No engine/glad rebuild required."}


def long_tail_guard(configuration):
    a = authority()
    assert "long_tail" in a and configuration == "Release"
    used = usage()
    assert all(used[k] >= v for k, v in a["long_tail"]["usage_floor"].items()), "no budget reset"
    for path in (BASE / "native").glob("*/invocation.json"):
        inv = json.loads(path.read_text())
        assert inv.get("long_tail_authority_sha256") != sha(LONG_AUTH) and inv["case"] != "long-tail", "single long-tail diagnostic consumed; no retry"


def causal_guard(configuration, native=False):
    c = authority()["causal"]
    assert configuration == "Release"
    used = usage()
    assert all(used[k] >= v for k,v in c["usage_floor"].items())
    assert used["native_launches"] < c["cumulative_native_attempt_cap"]
    for p in (BASE/"native").glob("*/invocation.json"):
        assert json.loads(p.read_text()).get("causal_authority_sha256") != sha(CAUSAL_AUTH), "one L60 causal diagnostic consumed; no retry"
    if not native:
        for p in (BASE/"builds").glob("*/invocation.json"):
            assert json.loads(p.read_text()).get("causal_authority_sha256") != sha(CAUSAL_AUTH), "one affected causal Release build consumed"


def build(configuration, long_tail=False, causal=False):
    assert not REFINEMENT_AUTH.exists(), "old authorities are consumed; use refinement-build"
    a = authority()
    assert not (long_tail and causal)
    if causal:
        causal_guard(configuration)
    elif long_tail:
        long_tail_guard(configuration)
    elif "qualification" in a:
        qualification_guard(configuration)
    elif "diagnostic" in a:
        assert configuration == "Debug", "diagnostic-only authority"
        assert not list((BASE / "native").glob("Debug-penetration-*/invocation.json")), "diagnostic attempt already consumed"
    used = usage()
    limit = min(a["limits"]["build_command_seconds"], a["limits"]["build_total_seconds"] - used["build_seconds"])
    assert limit > 10
    frozen = inputs()
    reuse = libraries(configuration)
    folder = fresh("builds", configuration)
    product_name = "Phase21L60Causal" if causal else "Phase21Qualification"
    outdir = safe(ROOT / f"bin/{configuration}/{product_name}")
    intdir = safe(ROOT / f"bin-int/{configuration}/{product_name}/RigidBodySimulation")
    outdir.mkdir(parents=True, exist_ok=True)
    intdir.mkdir(parents=True, exist_ok=True)
    defines = "GE_ENABLE_PHYSICS_PROFILING;" if configuration == "Release" else ""
    libs = ";".join((ROOT / p).as_posix() for p in reuse["products"])
    props = folder / "isolated.props"
    write(props, '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n<PropertyGroup>'
          f'<OutDir>{outdir.as_posix()}/</OutDir><IntDir>{intdir.as_posix()}/</IntDir>'
          '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>'
          '<ItemGroup><ProjectReference Remove="@(ProjectReference)"/></ItemGroup><ItemDefinitionGroup><ClCompile>'
          f'<PreprocessorDefinitions>{defines}%(PreprocessorDefinitions)</PreprocessorDefinitions>'
          '</ClCompile><Link><AdditionalDependencies>' + escape(libs) + ';%(AdditionalDependencies)</AdditionalDependencies>'
          '</Link></ItemDefinitionGroup></Project>\n')
    frozen["generated_props"] = {props.relative_to(ROOT).as_posix(): sha(props)}
    write(folder / "inputs.json", frozen)
    write(folder / "reuse.json", reuse)
    write(folder / "fixture.h", (ROOT / "RigidBodySimulation/tests/RestingContactDiagnosis.h").read_text())
    write(folder / "runner.py", Path(__file__).read_text())
    command = [str(MSBUILD), str(ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"), "/t:Build",
               f"/p:Configuration={configuration}", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
               "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:BuildProjectReferences=false",
               "/p:ForceImportBeforeCppTargets=" + str(props), "/nr:false", "/m:2", "/nologo", "/v:normal"]
    write(folder / "invocation.json", {"started_utc": now(), "configuration": configuration, "limit_seconds": limit,
                                     "command": command, "authority_sha256": sha(AUTH), "qualification_authority_sha256": sha(QUAL_AUTH) if "qualification" in a and not long_tail and not causal else None,
                                     "causal_authority_sha256": sha(CAUSAL_AUTH) if causal else None,
                                     "long_tail_authority_sha256": sha(LONG_AUTH) if long_tail else None, "usage_before": used})
    start = time.monotonic()
    execution = None
    errors = []
    products = {}
    try:
        with (folder / "build.log").open("wb") as log:
            execution = process_tools.remediation_owned_build(command, environment(), log, limit)
        assert execution["exit_code"] == 0 and not execution["timed_out"] and not execution["error"], execution
        current = inputs()
        assert all(current[k] == frozen[k] for k in current), "source changed during build"
        exe = outdir / "RigidBodySimulation.exe"
        assert exe.exists()
        products[exe.relative_to(ROOT).as_posix()] = sha(exe)
        runtime = {}
        for source in (ROOT / f"bin/{configuration}/RigidBodySimulation").glob("*.dll"):
            target = safe(outdir / source.name)
            if target.exists():
                assert sha(target) == sha(source), "runtime dependency mismatch"
            else:
                shutil.copyfile(source, target)
            runtime[source.relative_to(ROOT).as_posix()] = sha(source)
            products[target.relative_to(ROOT).as_posix()] = sha(target)
        assert runtime, "missing canonical runtime DLL sources"
        write(folder / "runtime-inputs.json", runtime)
        authority()
    except BaseException as error:
        errors.append(repr(error))
    elapsed = time.monotonic() - start
    result = {"status": "PASS" if not errors and elapsed <= limit else "FAIL", "configuration": configuration,
              "completed_utc": now(), "wall_seconds": elapsed, "execution": execution, "errors": errors,
              "products": products, "inputs_sha256": sha(folder / "inputs.json"), "props_sha256": sha(props),
              "engine_glad_reuse": reuse}
    write(folder / "result.json", result)
    print(json.dumps({"result": str(folder / "result.json"), "status": result["status"], "errors": errors, "wall_seconds": elapsed}), flush=True)
    if errors:
        raise SystemExit(1)


def qualification_guard(configuration, case=None):
    """Authority04: one replacement Debug and six gated Release slots; no reset."""
    q = json.loads(QUAL_AUTH.read_text())
    used = usage()
    assert all(used[k] >= v for k, v in q["usage_floor"].items()), "no budget reset"
    assert used["native_launches"] <= q["cumulative_native_attempt_cap"]
    if case is None:
        prior = [p for p in (BASE/"builds").glob("*/invocation.json") if json.loads(p.read_text()).get("qualification_authority_sha256") == sha(QUAL_AUTH) and json.loads(p.read_text())["configuration"] == configuration]
        assert not prior, "authority04 build command for this configuration already consumed"
    current = []
    for path in (BASE / "native").glob("*/invocation.json"):
        inv = json.loads(path.read_text())
        if inv.get("qualification_authority_sha256") != sha(QUAL_AUTH):
            continue
        result = json.loads(path.with_name("result.json").read_text())
        assert result["status"] == "CAPTURE_COMPLETE_ANALYSIS_PENDING", "qualification stopped after failed/incomplete attempt"
        analyses = list((BASE / "analysis").glob(path.parent.name + "-*/analysis.json"))
        assert len(analyses) == 1 and json.loads(analyses[0].read_text())["result"] == "PASS_NUMERIC_HUMAN_QUALITATIVE_PENDING", "required affected analysis has not passed"
        analysis = json.loads(analyses[0].read_text())
        assert analysis.get("attribution", {}).get("result") == "CLOSED_FOR_OBSERVED_CASE", "P1 attribution gates remain blocking"
        current.append([inv["configuration"], inv["case"]])
    if configuration == "Release":
        assert ["Debug", "debug"] in current, "Debug smoke must pass first"
    if case is not None:
        assert len(current) < q["native_attempt_cap"] and used["native_launches"] < q["cumulative_native_attempt_cap"]
        assert [configuration, case] == q["slots"][len(current)] and [configuration, case] not in current, "no extra, unordered or repeated slot"
        if case == "L120":
            assert ["Release", "prefix"] in current


def run(configuration, case, build_path):
    assert not REFINEMENT_AUTH.exists(), "old authorities are consumed; use refinement-run"
    a = authority()
    used = usage()
    long_tail = case == "long-tail"
    causal = case == "L60-causal"
    if causal:
        causal_guard(configuration, native=True)
    elif long_tail:
        long_tail_guard(configuration)
    elif "qualification" in a:
        qualification_guard(configuration, case)
    elif "diagnostic" in a:
        assert configuration == "Debug" and case == "penetration", "diagnostic only; no qualification continuation"
        assert not list((BASE / "native").glob("Debug-penetration-*/invocation.json")), "single diagnostic attempt already consumed"
    else:
        assert (configuration == "Debug" and case == "debug") or (configuration == "Release" and case in ("prefix", "L120", "L60", "LA", "LP", "small"))
    proposed_limit = a["limits"]["prefix_native_seconds"] if case == "prefix" else a["limits"]["long_native_seconds"] if case in ("L120", "L60", "LA", "LP", "long-tail", "L60-causal") else a["limits"]["debug_small_native_seconds"]
    limit = min(proposed_limit, a["limits"]["native_total_seconds"] - used["native_seconds"])
    assert limit > 10
    build_path = safe(build_path)
    built = json.loads(build_path.read_text())
    assert built["status"] == "PASS" and built["configuration"] == configuration
    if long_tail:
        assert json.loads(build_path.with_name("invocation.json").read_text()).get("long_tail_authority_sha256") == sha(LONG_AUTH)
    if causal:
        assert json.loads(build_path.with_name("invocation.json").read_text()).get("causal_authority_sha256") == sha(CAUSAL_AUTH)
    expected = json.loads(build_path.with_name("inputs.json").read_text())
    current = inputs()
    assert all(current[k] == expected[k] for k in current), "build inputs changed"
    for p, h in {**built["products"], **built["engine_glad_reuse"]["products"]}.items():
        assert sha(safe(p)) == h, p
    probe = subprocess.run(["powershell", "-NoProfile", "-Command", "@(Get-Process RigidBodySimulation -ErrorAction SilentlyContinue).Count"], capture_output=True, text=True, check=True)
    assert int(probe.stdout.strip()) == 0, "existing RBS process; do not interfere"
    folder = fresh("native", configuration + "-" + case)
    data = safe(folder / "data")
    data.mkdir()
    cwd = safe(ROOT / f"runtime/RigidBodySimulation/{configuration}")
    cwd.mkdir(parents=True, exist_ok=True)
    magic = "P21_L60_CAUSAL_01 " if causal else "P21_LONG_TAIL_01 " if long_tail else "P21_GAME_ENGINE_03 " if "qualification" in a else "P21_GAME_ENGINE_01 "
    write(folder / "phase21-request.txt", magic + case + " END\n")
    exe = next(ROOT / p for p in built["products"] if p.endswith(".exe"))
    command = [str(exe), "--resting-contact-diagnostic", "off" if case == "prefix" else "on", "--lattice",
               str(ROOT / "tools/pre_editor_resting_contact_lattice.txt"), "--output", str(data)]
    env = environment()
    env["GENGINE_P21_REQUEST"] = str(folder / "phase21-request.txt")
    env["GENGINE_ASSET_ROOT"] = str(ROOT / f"bin/{configuration}/assets")
    machine = process_tools.remediation_machine()
    runtime_before = {}
    for name in ("imgui.ini", "GEngine.log"):
        p = cwd / name
        if p.exists():
            safe(p)
            runtime_before[name] = sha(p)
            shutil.copyfile(p, folder / ("before-" + name))
    write(folder / "invocation.json", {"started_utc": now(), "case": case, "configuration": configuration,
          "command": command, "cwd": str(cwd), "limit_seconds": limit, "build": str(build_path.relative_to(ROOT)),
          "build_sha256": sha(build_path), "authority_sha256": sha(AUTH), "runtime_before": runtime_before,
          "diagnostic_authority_sha256": sha(DIAG_AUTH) if case == "penetration" else None,
          "qualification_authority_sha256": sha(QUAL_AUTH) if "qualification" in a and not long_tail and not causal else None,
          "causal_authority_sha256": sha(CAUSAL_AUTH) if causal else None,
          "long_tail_authority_sha256": sha(LONG_AUTH) if long_tail else None,
          "machine": machine, "usage_before": used, "automatic_retry": False})
    start = time.monotonic()
    execution = None
    errors = []
    native = None
    try:
        with (folder / "console.log").open("wb") as log:
            execution = process_tools.remediation_owned_build(command, env, log, limit,
                         _factory=lambda audit: NativeOwner(audit, cwd))
        for name in ("imgui.ini", "GEngine.log"):
            if (cwd / name).exists():
                shutil.copyfile(cwd / name, folder / ("after-" + name))
        if (data / "result.json").exists():
            native = json.loads((data / "result.json").read_text())
        assert sum(p.stat().st_size for p in data.iterdir() if p.is_file()) <= a["limits"]["export_bytes_per_process"]
        if "qualification" in a:
            capacity = json.loads((data / "pair-capacity.json").read_text())
            assert capacity["incremental_bytes"] <= 2*1024*1024 and capacity["trace_record_bytes"] == (72 if long_tail else 192) and capacity["event_record_bytes"] == 64
            assert sum((data / name).stat().st_size for name in ("pair-trace.bin", "pair-events.bin", "pair-capacity.json", "anchor-examples.csv", "energy-examples.csv") if (data/name).exists()) <= 8*1024*1024
        if case == "penetration" and (data / "first-failure.bin").exists():
            assert (data / "first-failure.bin").stat().st_size <= a["diagnostic"]["packet_bytes"]
        authority()
        after_inputs = inputs()
        assert all(after_inputs[k] == expected[k] for k in after_inputs), "inputs changed during native execution"
        assert not execution["timed_out"] and not execution["error"], execution
        if causal:
            packet = json.loads((data/"causal-result.json").read_text())
            assert execution["exit_code"] == 1 and packet["capture_complete"] and packet["original_P1_depth_FAIL"] and packet["errors"] == 0, packet
            assert packet["last_tick"] == 73 and packet["completed_windows"] == 2
            assert packet["profiling_enabled"] and packet["export_bytes_upper_bound"] <= a["limits"]["export_bytes_per_process"]
            assert sum(p.stat().st_size for p in data.glob("causal-*.csv")) <= 2*1024*1024
            assert sum((data/name).stat().st_size for name in ("pair-trace.bin","pair-events.bin","pair-capacity.json","anchor-examples.csv","energy-examples.csv") if (data/name).exists()) + sum(p.stat().st_size for p in data.glob("causal-*.csv")) <= 8*1024*1024
            assert capacity["observer_bytes"] <= a["limits"]["additional_recorder_bytes"]
            assert capacity["observer_bytes"] + capacity["session_bytes"] <= a["limits"]["total_named_diagnostic_bytes"]
        else:
            assert execution["exit_code"] == 0, execution
            assert native and native["complete"] and native["shutdown"], native
        if long_tail:
            assert native["diagnostic_only"] and native["ticks"] == 14400
        if native:
            assert native["observer_bytes"] <= a["limits"]["additional_recorder_bytes"]
            assert native["observer_bytes"] + native["session_bytes"] <= a["limits"]["total_named_diagnostic_bytes"]
        assert sum(p.stat().st_size for p in data.iterdir() if p.is_file()) <= a["limits"]["export_bytes_per_process"]
        if configuration == "Release" and native:
            assert native["profiling_enabled"]
        authority()
    except BaseException as error:
        errors.append(repr(error))
    elapsed = time.monotonic() - start
    evidence = {p.name: sha(p) for p in data.iterdir() if p.is_file()}
    result = {"status": "CAPTURE_COMPLETE_ANALYSIS_PENDING" if not errors else "FAIL_OR_INCOMPLETE",
              "completed_utc": now(), "wall_seconds": elapsed, "execution": execution, "native": native,
              "launch_started": bool(execution and execution["ownership"].get("pid")), "errors": errors, "evidence": evidence}
    if causal:
        result["status"] = "DIAGNOSTIC_CAPTURE_COMPLETE_WITH_P1_FAIL" if not errors else "INCONCLUSIVE_CAUSAL_CAPTURE"
        result["qualification_claim"] = "NONE; P21-L60-DEPTH-01 remains mandatory FAIL; expected fail-fast exit1 is not clean application-shutdown PASS. No qualification slot reused."
        result["causal_authority_consumed"] = True
    if case == "penetration":
        result["diagnostic_capture"] = "PACKET_PRESENT_ANALYSIS_PENDING" if "first-failure.bin" in evidence else "INCONCLUSIVE_NO_PACKET"
        result["qualification_claim"] = "NONE; original failure retained even if diagnostic packet is complete"
    if long_tail:
        result["qualification_claim"] = "NONE; diagnostic only, P21-PEN-01/P21-PEN-02 remain FAIL; complete means capture coverage only"
        result["one_time_diagnostic_authority_consumed"] = True
    write(folder / "result.json", result)
    print(json.dumps({"result": str(folder / "result.json"), "status": result["status"], "native": native, "errors": errors, "wall_seconds": elapsed}), flush=True)
    if errors:
        raise SystemExit(1)


def analyze_attribution(data, invocation, metrics, check, output):
    """Bounded existing operands; class coverage and physical work, not a solver theorem."""
    start_failures = []
    def gate(name, condition):
        check(name, condition)
        if not condition:
            start_failures.append(name)
    with (data/"attribution.csv").open(newline="") as f:
        ledger = list(csv.DictReader(f))
    keys = [(int(r["episode"]),int(r["tick"])) for r in ledger]
    gate("attribution exact tick coverage", keys == [(int(r["episode"]),int(r["tick"])) for r in metrics])
    by_key = {(int(r["episode"]),int(r["tick"])):r for r in metrics}
    prior_checks = 0
    boundary_sums = []
    for r in ledger:
        gate("finite attribution ledger", all(math.isfinite(float(v)) for v in r.values()))
        gate("P1 operation/anchor invariants", int(r["failures"]) == 0 and int(r["checks"]) > prior_checks)
        prior_checks = int(r["checks"])
        metric = by_key[int(r["episode"]),int(r["tick"])]
        end, metric_end, stage_end = float(r["end_energy"]), float(metric["energy"]), float(r["stage_11"])
        metric_difference, stage_difference = end-metric_end, end-stage_end
        # The stage sum groups per-body K+U; the original metric sums K and U
        # separately. Only their approved arithmetic reconstruction allowance applies.
        allowance = float(r["allowance"])
        gate("attribution End boundary", abs(metric_difference) <= allowance and abs(stage_difference) <= allowance)
        boundary_sums.append((int(r["episode"]),int(r["tick"]),end,metric_end,stage_end,metric_difference,stage_difference,allowance))
        gate("full tick energy reconstruction", abs(float(r["residual"])) <= float(r["allowance"]))
        for op in range(24):
            observed = float(metric[f"op_kinetic_{op}"])+float(metric[f"op_potential_{op}"])
            gate("operation ledger covers original metrics", abs(observed-float(r[f"measured_{op}"])) <= float(r["allowance"]))
    sums_path = safe(output / "energy-boundary-reconstruction.csv")
    with sums_path.open("x", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(("episode","tick","original_end_energy","original_metric_energy","original_stage_11","end_minus_metric","end_minus_stage","approved_arithmetic_allowance"))
        writer.writerows(boundary_sums)
    geometry = lattice_geometry()
    with (data/"anchor-examples.csv").open(newline="") as f:
        examples = [[float(x) for x in row] for row in csv.reader(f)]
    classes = {}
    for v in examples:
        gate("complete finite anchor operand packet", len(v)==72 and all(math.isfinite(x) for x in v))
        if len(v)!=72:
            continue
        ep,tick,category,kind,ia,ib,born = map(int,v[:7])
        gate("supported discrepancy class", category in (1,2,3,4) and category==(1 if ia>=5 and ib>=5 else 3)+int(born!=tick))
        a={"position":v[32:35],"orientation":v[52:56]}
        b={"position":v[35:38],"orientation":v[56:60]}
        independent = independent_overlap(geometry[ia],a,geometry[ib],b)["depth"]
        gate("anchor packet independent geometry", abs(independent-v[67])<=1e-10)
        actual=-sum((v[60+j]-v[63+j])*v[38+j] for j in range(3))
        gate("original anchor projection reproduced", abs((v[66]-actual)-v[70])<=1e-10)
        difference=max(0.,v[66])-max(0.,v[67])
        gate("complete discrepancy decomposition", abs(difference-sum(v[68:72]))<=1e-10)
        classes.setdefault(str(category),{"source":"analytic sphere witness" if category<3 else "GJK sphere/box witness","age":"current" if category%2 else "retained","examples":[]})["examples"].append({"episode":ep,"tick":tick,"pair":[ia,ib],"birth_tick":born,"kind":"first" if kind==0 else "worst","anchor_raw":v[66],"geometry_raw":v[67],"projection":v[68],"retained_witness":v[69],"float_arithmetic":v[70],"zero_clipping":v[71]})
    events=list(PAIR_EVENT.iter_unpack((data/"pair-events.bin").read_bytes()))
    discrepancy_count=sum(e[6] for e in events if e[3]==7)
    last=ledger[-1] if ledger else {}
    class_count=sum(int(last.get(f"class_differences_{i}",0)) for i in range(1,5))
    gate("every discrepant pair sample attributed by class", discrepancy_count==class_count)
    for i in range(1,5):
        if int(last.get(f"class_contacts_{i}",0)):
            gate("class first/worst operands retained", str(i) in classes and len(classes[str(i)]["examples"])==2)
            classes.setdefault(str(i),{}).update(contact_samples=int(last[f"class_contacts_{i}"]),discrepant_pair_samples=int(last[f"class_differences_{i}"]),maximum_difference=float(last[f"class_max_{i}"]))
    with (data/"energy-examples.csv").open(newline="") as f:
        energy_examples=[[float(x) for x in row] for row in csv.reader(f)]
    for e in energy_examples:
        gate("complete finite energy operand packet", len(e)==145 and all(math.isfinite(x) for x in e))
        if len(e)==145:
            gate("no recorded invariant failure", int(e[2])!=2)
    if ledger:
        gate("positive-work operands retained", bool(energy_examples))
    windows=[]
    for ep in sorted({int(r["episode"]) for r in ledger}):
        selected=[r for r in ledger if int(r["episode"])==ep]
        known=[r for r in selected if 150<=int(r["tick"])<=159]
        deltas=[float(r["end_energy"])-float(r["begin_energy"]) for r in selected]
        peak=max(range(len(deltas)),key=deltas.__getitem__) if deltas else None
        windows.append({"episode":ep,"largest_tick_gain":deltas[peak] if peak is not None else None,
                        "largest_tick_gain_at":int(selected[peak]["tick"]) if peak is not None else None,
                        "ticks150_159_operation_changes":{str(op):sum(float(r[f"measured_{op}"]) for r in known) for op in range(24)},
                        "ticks150_159_predicted_work":{str(op):sum(float(r[f"expected_{op}"]) for r in known) for op in range(24)},
                        "maximum_ledger_residual":max((abs(float(r["residual"])) for r in selected),default=0.)})
    return {"result":"CLOSED_FOR_OBSERVED_CASE" if not start_failures else "P1_BLOCKING_FAIL_OR_INCOMPLETE",
            "failures":sorted(set(start_failures)),"ledger_ticks":len(ledger),"checks":prior_checks,
            "discrepancy_classes":classes,"all_observed_discrepancy_samples":discrepancy_count,
            "energy_windows":windows,"energy_operand_packets":len(energy_examples),
            "End_sum_reconstruction":{"path":sums_path.relative_to(ROOT).as_posix(),"sha256":sha(sums_path),"rows":len(boundary_sums),"maximum_absolute_difference":max((max(abs(r[5]),abs(r[6])) for r in boundary_sums),default=0.),"tolerance":"Existing per-tick arithmetic reconstruction allowance only; no physical energy criterion changed."},
            "attribution":"Distinct analytic support and retained witness projections, with source-linked creation/refresh and transported normal checks; conservative max unchanged. Warm-start work uses actual pre-impulse velocities and applied impulses; coupled one-pass response need not satisfy a two-body total-dissipation theorem.",
            "limits":"This closes the measured attribution only when all recorded source/physical invariants pass. Depth, recovery, global/rest energy, drift, contact, lifetime and visual gates remain independent. No historical outcome changed."}


def refinement_prefix_profile(data, invocation, capture, episodes, metrics, initials, check):
    """Check the frozen minimal observer's exports without inventing observations."""
    failures = []
    def gate(name, condition):
        name = "minimal prefix " + name
        check(name, condition)
        if not condition:
            failures.append(name)
    command = invocation["command"]
    gate("Release C2 observation off", invocation["configuration"] == "Release" and invocation["slot"] == "C2" and
        any(command[i:i+2] == ["--resting-contact-diagnostic", "off"] for i in range(len(command)-1)))
    gate("exact episode", len(episodes) == 1 and all(episodes[0][k] == v for k, v in
        {"episode":"0", "name":"prefix", "hz":"120", "passes":"1", "sleep":"1", "ticks":"1800", "bodies":"185"}.items()))
    gate("exact initial membership", set(initials) == {(0, i) for i in range(185)})
    gate("finite initial operands", all(math.isfinite(float(v)) for row in initials.values() for v in row.values()))
    identities = [(int(r["slot"]), int(r["generation"])) for r in initials.values()]
    gate("unique typed initial identities", len(set(identities)) == 185 and
        all(0 <= slot < 2**64 and 0 <= generation < 2**64 for slot, generation in identities))
    gate("exact ordinal physical samples", [(int(r["episode"]), int(r["tick"])) for r in metrics] ==
        [(0, tick) for tick in range(1, 1801)])
    gate("typed state hashes and body counts", all(0 <= int(r["state_hash"]) < 2**64 and int(r["bodies"]) == 185 for r in metrics))
    streams = {"bodies.bin", "metrics.csv", "frames.csv", "initial.csv", "episodes.csv", "impulse.csv",
        "pair-trace.bin", "pair-events.bin", "refinement-work.csv"}
    required = streams | {"pair-capacity.json", "case-0-capture.json", "result.json"}
    gate("exact evidence membership", {p.name for p in data.iterdir()} == required and set(capture["evidence"]) == required)
    marker = json.loads((data / "case-0-capture.json").read_text())
    offsets = marker["stream_end_offsets"]
    gate("exact committed stream membership", set(offsets) == streams)
    gate("committed case identity", marker["name"] == "prefix" and marker["hz"] == 120 and
        marker["attribution_checks"] == marker["attribution_failures"] == 0)
    gate("complete committed streams", all((data / name).is_file() and 0 <= end <= (data / name).stat().st_size and
        (name == "frames.csv" or end == (data / name).stat().st_size) for name, end in offsets.items()))
    gate("empty early lattice body export", (data / "bodies.bin").stat().st_size == 0)
    gate("no wake fixture substitution", not process_tools.remediation_csv(data / "impulse.csv"))
    capacity = json.loads((data / "pair-capacity.json").read_text())
    gate("actual typed export profile", capacity["policy"] == "AMENDMENT_02" and
        (capacity["body_record_bytes"], capacity["trace_record_bytes"], capacity["event_record_bytes"]) == (93, 192, 64))
    native = capture["native"]
    gate("complete native minimal capture", native["complete"] and native["shutdown"] and not native["failure"] and
        native["episodes"] == 1 and native["ticks"] == 1800 and native["profiling_enabled"])
    gate("no hidden native findings", all(native[k] == 0 for k in ("duration_violations", "duration_violation_samples",
        "pending_recovery_episodes", "refinement_direct_checks", "refinement_direct_failures", "refinement_summary_ticks",
        "refinement_eligible_ticks", "refinement_extra_events")))
    detailed = ["event_ns", "normal_impulse", "friction_impulse", "restitution_sum"] + [
        f"op_{kind}_{i}" for i in range(24) for kind in ("count", "kinetic", "potential")]
    gate("no detailed metric observations", all(float(r[k]) == 0 for r in metrics for k in detailed))
    return {"result":"DEFERRED_TO_C4_FULL_OBSERVATION" if not failures else "FAIL_OR_INCOMPLETE",
        "failures":failures, "physical_samples":len(metrics), "body_record_bytes":93, "body_records":0,
        "required_followup":"C4 full attribution/work qualification and exact same compiled/source candidate first1800 physical-state hashes.",
        "scope":"Minimal export completeness only; no attribution, angular certificate, graph coverage or P1 closure is claimed."}


def analyze(result_path):
    """One analysis of immutable observations; no native launch or changed limit."""
    authority()
    result_path = safe(result_path)
    capture = json.loads(result_path.read_text())
    invocation = json.loads(result_path.with_name("invocation.json").read_text())
    refinement = invocation.get("refinement_authority_sha256") in REFINEMENT_AUTHORITIES
    minimal_prefix = refinement and invocation["case"] == "prefix"
    if refinement and invocation["case"] == "phase20-original-candidate":
        return analyze_original_phase20(result_path)
    long_tail = invocation.get("long_tail_authority_sha256") is not None
    amended = invocation.get("qualification_authority_sha256") is not None or long_tail or refinement
    if refinement:
        assert result_path.with_name("phase21-request.txt").read_text() == "P21_COUPLED_REFINEMENT_01 " + invocation["case"] + " END\n"
    elif long_tail:
        assert invocation["long_tail_authority_sha256"] == sha(LONG_AUTH)
        assert invocation["case"] == "long-tail" and result_path.with_name("phase21-request.txt").read_text() == "P21_LONG_TAIL_01 long-tail END\n"
    elif amended:
        assert invocation["qualification_authority_sha256"] == sha(QUAL_AUTH), "exact prospective authority"
        assert result_path.with_name("phase21-request.txt").read_text().startswith("P21_GAME_ENGINE_03 ")
    data = result_path.parent / "data"
    for name, h in capture["evidence"].items():
        assert sha(data / name) == h, name
    output = fresh("analysis", result_path.parent.name)
    failures = []
    def check(name, condition):
        if not condition:
            failures.append(name)
    check("complete native capture", capture["status"] == "CAPTURE_COMPLETE_ANALYSIS_PENDING")
    def rows(name):
        with (data / name).open(newline="", encoding="utf-8") as f:
            return list(csv.DictReader(f))
    episodes = rows("episodes.csv")
    metrics = rows("metrics.csv")
    if refinement or invocation.get("qualification_authority_sha256") == sha(QUAL_AUTH):
        for e in episodes:
            marker_path = data / ("case-"+e["episode"]+"-capture.json")
            check("completed-case durable marker", marker_path.exists())
            if not marker_path.exists():
                continue
            marker = json.loads(marker_path.read_text())
            check("completed-case explicit capture status", marker["capture_complete"] and not marker["qualification_pass"] and
                  marker["episode"] == int(e["episode"]) and marker["ticks"] == marker["expected_ticks"] == int(e["ticks"]))
            for name, offset in marker["stream_end_offsets"].items():
                stream = data / name
                check("durable case stream coverage", stream.exists() and 0 <= offset <= stream.stat().st_size)
                if stream.exists() and offset > 0 and name.endswith(".csv"):
                    with stream.open("rb") as f:
                        f.seek(offset-1)
                        check("durable case complete CSV record", f.read(1) == b"\n")
                if name in ("bodies.bin", "pair-trace.bin", "pair-events.bin"):
                    check("durable case complete typed binary record", offset % {"bodies.bin":93 if minimal_prefix else 101,"pair-trace.bin":192,"pair-events.bin":64}[name] == 0)
    initial_rows = rows("initial.csv")
    initials = {(int(x["episode"]), int(x["index"])): x for x in initial_rows}
    prefix_profile = None
    if minimal_prefix:
        check("minimal prefix no duplicate initial rows", len(initial_rows) == len(initials))
        prefix_profile = refinement_prefix_profile(data, invocation, capture, episodes, metrics, initials, check)
    recovery03 = (refinement and not minimal_prefix) or invocation.get("qualification_authority_sha256") == sha(QUAL_AUTH)
    body_format = struct.Struct("<IIIQQ13fBdI" + ("d" if long_tail or recovery03 else ""))
    body_rows = {}
    with (data / "bodies.bin").open("rb") as f:
        while raw := f.read(body_format.size):
            assert len(raw) == body_format.size, "truncated body record"
            v = body_format.unpack(raw)
            key = (v[0], v[2])
            initial = initials[key]
            check("body identity", v[3] == int(initial["slot"]) and v[4] == int(initial["generation"]))
            check("finite body", all(math.isfinite(x) for x in v[5:18]) and math.isfinite(v[19]))
            if long_tail or recovery03:
                check("finite per-body penetration", math.isfinite(v[21]) and v[21] >= 0)
            body_rows.setdefault(key, []).append(v)
    attribution = prefix_profile if minimal_prefix else analyze_attribution(data, invocation, metrics, check, output) if recovery03 else None
    refinement_report = analyze_refinement(data, invocation, metrics, body_rows, initials, check) if refinement else None
    reports = []
    pair_report = analyze_pairs(data, invocation, initials, check) if amended else None
    for e in episodes:
        eid, hz, limit = int(e["episode"]), int(e["hz"]), int(e["ticks"])
        rr = [x for x in metrics if int(x["episode"]) == eid]
        check(e["name"] + " exact ticks", [int(x["tick"]) for x in rr] == list(range(1, limit + 1)))
        check(e["name"] + " finite metrics", all(math.isfinite(float(v)) for x in rr for v in x.values()))
        dynamic = [i for (ep, i), x in initials.items() if ep == eid and float(x["inverse_mass"]) > 0]
        report = {"name": e["name"], "hz": hz, "ticks": len(rr), "expected_ticks": limit,
                  "initial_energy": float(e["initial_energy"]), "classification": "DIAGNOSTIC" if invocation["case"] in ("LA", "LP", "long-tail") else "QUALIFICATION",
                  "max_penetration": max((float(x["penetration"]) for x in rr), default=0),
                  "peak_energy": max([float(e["initial_energy"]), *[float(x["energy"]) for x in rr]]),
                  "sleep_transitions": sum(int(x["sleeps"]) for x in rr), "wake_transitions": sum(int(x["wakes"]) for x in rr),
                  "max_capacity_bytes": max((int(x["capacity"]) for x in rr), default=0),
                  "capacity_growth_bytes": max((int(x["growth"]) for x in rr), default=0)}
        is_lattice = len(dynamic) == 180
        if is_lattice:
            check(e["name"] + " initial lattice energy", report["initial_energy"] == 30240.)
            if not amended:
                check(e["name"] + " lattice penetration safety", report["max_penetration"] <= .05)
            check(e["name"] + " lattice energy safety", report["peak_energy"] <= 30542.4)
            report["historical_comparison_only"] = {"penetration_0035": report["max_penetration"] <= .035,
                                                    "energy_30391_2": report["peak_energy"] <= 30391.2}
        tail = [x for x in rr if int(x["tick"]) > (110 * hz if is_lattice else limit - 2 * hz)]
        if is_lattice and limit == 120 * hz:
            bins = [[x for x in tail if (110 + s) * hz < int(x["tick"]) <= (111 + s) * hz] for s in range(10)]
            check(e["name"] + " tail coverage", all(len(b) == hz for b in bins))
            report["quiet_mean"] = sum(int(x["quiet"]) for x in tail) / len(tail) if tail else None
            report["one_second_quiet_means"] = [sum(int(x["quiet"]) for x in b) / len(b) if b else None for b in bins]
            report["energy_one_second"] = [{"min": min(float(x["energy"]) for x in b), "max": max(float(x["energy"]) for x in b),
                                            "mean": sum(float(x["energy"]) for x in b) / len(b), "end": float(b[-1]["energy"])} for b in bins if b]
            if invocation["case"] in ("L120", "L60", "long-tail"):
                threshold = 162 if hz == 120 else 153
                floor = 153 if hz == 120 else 144
                check(e["name"] + " tail quiet mean", report["quiet_mean"] is not None and report["quiet_mean"] >= threshold)
                check(e["name"] + " each one-second quiet mean", all(x is not None and x >= floor for x in report["one_second_quiet_means"]))
            report["per_body"] = []
            if long_tail or recovery03:
                report["tail_sleeping_mean"] = sum(int(x["sleeping"]) for x in tail)/len(tail) if tail else None
                report["tail_wakes"] = sum(int(x["wakes"]) for x in tail)
                report["tail_sleeps"] = sum(int(x["sleeps"]) for x in tail)
                report["tail_peak_penetration"] = max((float(x["penetration"]) for x in tail),default=None)
                report["one_second_sleeping_means"] = [sum(int(x["sleeping"]) for x in b)/len(b) if b else None for b in bins]
                report["full_one_second"] = []
                for second in range(120):
                    bb = [x for x in rr if second*hz < int(x["tick"]) <= (second+1)*hz]
                    if bb:
                        report["full_one_second"].append({"second":second,"samples":len(bb),"quiet_mean":sum(int(x["quiet"]) for x in bb)/len(bb),
                            "sleeping_mean":sum(int(x["sleeping"]) for x in bb)/len(bb),"wakes":sum(int(x["wakes"]) for x in bb),"sleeps":sum(int(x["sleeps"]) for x in bb),
                            "max_penetration":max(float(x["penetration"]) for x in bb),"energy_min":min(float(x["energy"]) for x in bb),"energy_max":max(float(x["energy"]) for x in bb),
                            "energy_end":float(bb[-1]["energy"]),"contacts_min":min(int(x["contacts"]) for x in bb),"contacts_max":max(int(x["contacts"]) for x in bb)})
        elif not is_lattice and invocation["case"] != "spherebox-support":
            check(e["name"] + " support penetration", max((float(x["penetration"]) for x in tail), default=math.inf) <= .035)
            report["per_body"] = []
        if invocation["case"] == "spherebox-support":
            report["per_body"] = []
        for i in dynamic:
            bb = body_rows.get((eid, i), [])
            cutoff = 110 * hz if is_lattice else limit - 2 * hz
            selected = [v for v in bb if v[1] > cutoff]
            if is_lattice and limit != 120 * hz:
                continue
            check(e["name"] + f" body {i} tail coverage", len(selected) == (10 if is_lattice else 2) * hz)
            if not selected:
                continue
            setting = initials[(eid, i)]
            def quiet(v):
                return bool(v[18]) or (math.sqrt(sum(x*x for x in v[12:15])) <= float(setting["linear_sleep"]) and
                                      math.sqrt(sum(x*x for x in v[15:18])) <= float(setting["angular_sleep"]))
            fraction = sum(quiet(v) for v in selected) / len(selected)
            first = next((v for v in bb if v[1] == cutoff), selected[0])
            per = {"index": i, "quiet_fraction": fraction, "sleeping_fraction": sum(bool(v[18]) for v in selected)/len(selected),
                   "max_displacement_from_tail_start": max(math.sqrt(sum((v[5+j]-first[5+j])**2 for j in range(3))) for v in selected),
                   "final_displacement": math.sqrt(sum((selected[-1][5+j]-first[5+j])**2 for j in range(3))),
                   "vertical_range": max(v[6] for v in selected)-min(v[6] for v in selected),
                   "max_abs_vy": max(abs(v[13]) for v in selected),
                   "zero_contact_samples": sum(v[20] == 0 for v in selected)}
            report["per_body"].append(per)
            if long_tail or recovery03:
                previous_samples = [first,*selected[:-1]]
                per.update(max_observed_pair_penetration=max(v[21] for v in selected),
                           tail_wakes=sum(bool(old[18]) and not v[18] for old,v in zip(previous_samples,selected)),
                           tail_sleeps=sum(not old[18] and bool(v[18]) for old,v in zip(previous_samples,selected)),
                           contact_count_changes=sum(old[20]!=v[20] for old,v in zip(previous_samples,selected)),
                           min_contacts=min(v[20] for v in selected),max_contacts=max(v[20] for v in selected),
                           max_linear_speed=max(math.sqrt(sum(x*x for x in v[12:15])) for v in selected),
                           max_angular_speed=max(math.sqrt(sum(x*x for x in v[15:18])) for v in selected))
            if not is_lattice and invocation["case"] != "spherebox-support":
                check(e["name"] + f" body {i} quiet fraction", fraction >= (.9 if len(dynamic) == 4 else 1.))
                if len(dynamic) == 1:
                    check(e["name"] + " final vertical range", per["vertical_range"] <= .02)
                    check(e["name"] + " final vertical speed", per["max_abs_vy"] <= .05)
                    wakes = rows("impulse.csv")
                    impulse = [x for x in wakes if int(x["episode"]) == eid]
                    check(e["name"] + " prescribed sleeping impulse", len(impulse) == 1 and impulse[0]["after_tick"] == "300" and impulse[0]["sleeping_before"] == "1" and impulse[0]["awake_after"] == "1")
                    check(e["name"] + " next-tick wake", any(v[1] == 301 and not v[18] for v in bb))
        cpu_fields = [p for p in metrics[0] if p.endswith("TimeNs") or p.startswith("sleep_ns_")] if metrics else []
        report["CPU_characterization_only"] = {}
        for label, population in (("full", rr), ("active_first_15_seconds", [x for x in rr if int(x["tick"]) <= 15*hz]), ("tail", tail)):
            report["CPU_characterization_only"][label] = {}
            for field in cpu_fields + ["callback_ns", "event_ns"]:
                values = sorted(int(x[field]) for x in population)
                report["CPU_characterization_only"][label][field] = {"samples": len(values), "mean_ns": sum(values)/len(values),
                      "p95_ns": values[math.ceil(.95*len(values))-1], "max_ns": max(values)} if values else {"samples": 0}
        reports.append(report)
    prefix_parity = None
    if invocation["case"] == "L120" and capture["status"] == "CAPTURE_COMPLETE_ANALYSIS_PENDING":
        prefixes = list((BASE / "native").glob("Release-prefix-*/result.json"))
        if refinement:
            prefixes = [p.with_name("result.json") for p, inv, _ in refinement_records("native", invocation["wave"])
                        if inv["case"] == "prefix"]
            if not prefixes and invocation["correction"]:
                correction = reference(invocation["correction"])
                reuse = reference(correction["unaffected_reuse"]["C2"]["analysis"])
                prefixes = [safe(reuse["capture"])]
        valid = [p for p in prefixes if json.loads(p.read_text())["status"] == "CAPTURE_COMPLETE_ANALYSIS_PENDING"]
        check("one prefix reference", len(valid) == 1)
        if len(valid) == 1:
            with (valid[0].parent / "data/metrics.csv").open(newline="") as f:
                before = list(csv.DictReader(f))
            check("off/on per-tick physical-state equality", len(before) == 1800 and [x["state_hash"] for x in before] == [x["state_hash"] for x in metrics[:1800]])
            if refinement:
                prefix_inv = json.loads(valid[0].with_name("invocation.json").read_text())
                prefix_analysis = refinement_analysis_pass(valid[0].with_name("invocation.json"), prefix_inv,
                    json.loads(valid[0].read_text()))
                check("prefix same exact Release build", prefix_inv["configuration"] == invocation["configuration"] == "Release" and
                    (prefix_inv["build"], prefix_inv["build_sha256"]) == (invocation["build"], invocation["build_sha256"]))
                built = reference({"path":invocation["build"], "sha256":invocation["build_sha256"]})
                prefix_sources = reference(prefix_inv["prelaunch_proof"])["source_hashes"]
                current_sources = reference(invocation["prelaunch_proof"])["source_hashes"]
                physical_sources = REFINEMENT_OWNED - {RUNNER_PATH}
                check("prefix same production and support sources", all(
                    prefix_sources.get(p) == current_sources.get(p) and p in current_sources for p in physical_sources))
                check("prefix exact ordinal comparison", [(int(r["episode"]), int(r["tick"])) for r in before] ==
                    [(int(r["episode"]), int(r["tick"])) for r in metrics[:1800]] == [(0, tick) for tick in range(1, 1801)])
                check("prefix deferred evidence closed by full C4", attribution["result"] == "CLOSED_FOR_OBSERVED_CASE" and
                    refinement_report["result"] == "PASS_BOUNDED_REFINEMENT_WORK")
                prefix_parity = {"prefix_capture":{"path":valid[0].relative_to(ROOT).as_posix(), "sha256":sha(valid[0])},
                    "prefix_analysis":prefix_analysis, "build":{"path":invocation["build"], "sha256":invocation["build_sha256"]},
                    "products":built["products"], "physical_source_hashes":{p:current_sources[p] for p in sorted(physical_sources)},
                    "physical_state_samples":1800, "detailed_evidence":"Full C4 attribution/refinement gates remain independently required."}
    frame_rows = rows("frames.csv")
    for frame in frame_rows:
        value = float(frame["discarded"])
        check("finite nonnegative discarded wall-time evidence", math.isfinite(value) and value >= 0)
    result = {"phase": 21, "recorded_utc": now(), "result": "PASS_NUMERIC_HUMAN_QUALITATIVE_PENDING" if not failures else "FAIL_OR_INCOMPLETE",
              "capture": str(result_path.relative_to(ROOT)), "capture_sha256": sha(result_path), "failures": sorted(set(failures)),
              "episodes": reports, "qualitative_guard": "Owner visual review still required; per-body drift/contact/energy evidence is not automatically qualitative PASS.",
              "discarded_wall_seconds": sum(float(x["discarded"]) for x in frame_rows),
              "time_interpretation": "Complete ordinal fixed-step sample coverage qualifies simulated intervals. Reported scheduler wall-time loss is not hidden or a real-time pace PASS.",
              "historical_results_unchanged": True}
    result["pair_policy"] = pair_report
    result["attribution"] = attribution
    if prefix_parity is not None:
        prefix_parity["result"] = "CLOSED_BY_C4_FULL_OBSERVATION_AND_EXACT_PHYSICAL_PARITY" if not failures else "FAIL_OR_INCOMPLETE"
        result["prefix_parity"] = prefix_parity
    if refinement:
        result["refinement"] = refinement_report
    if long_tail:
        result["result"] = "DIAGNOSTIC_COMPLETE_WITH_RECORDED_FINDINGS" if capture["status"] == "CAPTURE_COMPLETE_ANALYSIS_PENDING" else "INCONCLUSIVE_INCOMPLETE_LONG_TAIL_DIAGNOSTIC"
        result["qualification_claim"] = "NONE; P21-PEN-01/P21-PEN-02 and observed duration failures remain FAIL. Tail thresholds are unchanged reference comparisons only."
        result["long_term_stability"] = "REQUIRES_CONSOLIDATED_REVIEW" if capture["status"] == "CAPTURE_COMPLETE_ANALYSIS_PENDING" else "INCONCLUSIVE"
    write(output / "analysis.json", result)
    print(json.dumps({"analysis": str(output / "analysis.json"), "result": result["result"], "failure_count":len(result["failures"]),
                      "failures": result["failures"][:12] if long_tail else result["failures"]}), flush=True)
    if failures:
        raise SystemExit(1)


PAIR_TRACE = struct.Struct("<4I4Q26f3d4I")
LONG_PAIR_TRACE = struct.Struct("<4I5d4I")
PAIR_EVENT = struct.Struct("<12I2d")
HISTORICAL_PAIRS = {(143,179),(144,180),(145,181),(146,182),(141,177),(131,167),(132,168),
                    (133,169),(136,172),(125,161),(126,162),(127,163),(128,164),(130,166),
                    (120,156),(121,157),(122,158),(124,160),(115,151),(116,152),(118,154)}


def recovery_verdict(samples, hz, sphere03=False):
    """Independent discrete audit of one complete episode, without clock resets."""
    n, h = hz//(5 if sphere03 else 10), hz//4
    first = samples[0][0]
    if [k for k, _ in samples] != list(range(first, first+len(samples))):
        return "INCONCLUSIVE_MISSING_SAMPLES"
    if samples[0][1] <= .05:
        return "INCONCLUSIVE_MISSING_EXCEEDANCE"
    run = streak = 0
    recovery_start = None
    for k, d in samples:
        if d > .05:
            run += 1
            streak = 0
            recovery_start = None
            if run > n or k >= first+n:
                return "FAIL_RETURN_DEADLINE"
        else:
            run = 0
            if not streak:
                recovery_start = k
            streak += 1
            if recovery_start > first+n or k > first+n+h-1:
                return "FAIL_CONFIRMATION_DEADLINE"
            if streak == h:
                return "RECOVERED" if k == samples[-1][0] else "INCONCLUSIVE_EXTRA_EPISODE_SAMPLES"
    return "INCONCLUSIVE_PENDING"


def pair_selfcheck():
    assert PAIR_TRACE.size == 192 and PAIR_EVENT.size == 64
    cases = 0
    for hz in (60, 120):
        n, h = hz//10, hz//4
        def verdict(depths):
            return recovery_verdict(list(enumerate(depths, 143)), hz)
        assert verdict([.10]*n+[.05]*h) == "RECOVERED"
        assert verdict([.10]*(n+1)) == "FAIL_RETURN_DEADLINE"
        assert verdict([.10]+[.05]*(h-1)) == "INCONCLUSIVE_PENDING"
        assert verdict([.10]*n+[.05]+[.10]) == "FAIL_RETURN_DEADLINE"
        assert verdict([.10]+[.05]*(n-2)+[.10]+[.05]*h) == "RECOVERED"
        assert recovery_verdict([(1,.10),(3,.05)], hz) == "INCONCLUSIVE_MISSING_SAMPLES"
        cases += 6
    return {"policy_boundary_cases": cases, "trace_bytes": PAIR_TRACE.size, "event_bytes": PAIR_EVENT.size,
            "historical_trace_bytes": 21*240*PAIR_TRACE.size, "geometry": penetration_selfcheck()}


def expand_long_pair(v, initials, geometry):
    """Decode exact relative geometry; orientations of fixed boxes come from initial.csv.

    Zero velocity placeholders are internal only, never reported as observations.
    Full per-body poses/velocities are independently retained for every tail tick.
    """
    ep, tick, a, b = v[:4]
    ia, ib = initials[ep,a], initials[ep,b]
    qa = tuple(float(ia[k]) for k in ("qw","qx","qy","qz"))
    qb = tuple(float(ib[k]) for k in ("qw","qx","qy","qz"))
    dimension = min(2*geometry[b]["radius"], min(hi-lo for lo,hi in zip(geometry[a]["low"],geometry[a]["high"]))) if a<5 else 2*min(geometry[a]["radius"],geometry[b]["radius"])
    return (ep,tick,a,b,int(ia["slot"]),int(ia["generation"]),int(ib["slot"]),int(ib["generation"]),
            0.,0.,0.,*qa,*([0.]*6),*v[4:7],*qb,*([0.]*6),v[7],v[8],dimension,*v[9:13])


def long_tail_selfcheck():
    checks = pair_selfcheck()
    assert LONG_PAIR_TRACE.size == 72 and struct.calcsize("<IIIQQ13fBdId") == 101
    # Re-encode preserved observations; this is a format/geometry check, not a
    # new Physics measurement or retrospective qualification.
    old = BASE / "native/Debug-debug-03/data"
    with (old/"initial.csv").open(newline="") as f:
        initials = {(int(v["episode"]),int(v["index"])):v for v in csv.DictReader(f)}
    geometry = lattice_geometry()
    checked = set()
    for v in PAIR_TRACE.iter_unpack((old/"pair-trace.bin").read_bytes()):
        kind = "boundary" if v[2]<5 else "sphere"
        if kind in checked:
            continue
        delta = tuple(v[21+j]-v[8+j] for j in range(3))
        packed = LONG_PAIR_TRACE.pack(*v[:4],*delta,v[34],v[35],*v[37:41])
        u = expand_long_pair(LONG_PAIR_TRACE.unpack(packed),initials,geometry)
        assert len(u)==41 and u[:8]==v[:8] and u[34:]==v[34:]
        overlap = max(0.,independent_overlap(geometry[u[2]],{"position":u[8:11],"orientation":u[11:15]},
                                            geometry[u[3]],{"position":u[21:24],"orientation":u[24:28]})["depth"])
        assert abs(overlap-v[35])<=1e-10
        checked.add(kind)
        if len(checked)==2:
            break
    assert checked=={"sphere","boundary"}
    for hz in (60,120):
        n,h=hz//10,hz//4
        late=list(enumerate([.1]*(n+1)+[.04]*h,100))
        assert recovery_verdict(late,hz)=="FAIL_RETURN_DEADLINE" and all(d<=.05 for _,d in late[-h:])
    checks.update(compact_geometry_round_trips=sorted(checked),compact_record_bytes=72,tail_body_bytes=101,
                  late_recovery_remains_failure=True,minimum_tail_body_export_bytes=1201*185*101,
                  historical21_trace_bytes=240*21*72)
    return checks


def analyze_pairs(data, invocation, initials, check):
    geometry = lattice_geometry()
    long_tail = invocation["case"] == "long-tail"
    recovery03 = invocation.get("refinement_authority_sha256") in REFINEMENT_AUTHORITIES or invocation.get("qualification_authority_sha256") == sha(QUAL_AUTH)
    minimal_prefix = invocation.get("refinement_authority_sha256") in REFINEMENT_AUTHORITIES and invocation["case"] == "prefix"
    def records(name, fmt):
        raw = (data/name).read_bytes()
        check("complete " + name, len(raw) % fmt.size == 0)
        return list(fmt.iter_unpack(raw[:len(raw)//fmt.size*fmt.size]))
    trace = records("pair-trace.bin", LONG_PAIR_TRACE if long_tail else PAIR_TRACE)
    if long_tail:
        trace = [expand_long_pair(v,initials,geometry) for v in trace]
    events = records("pair-events.bin", PAIR_EVENT)
    check("pair export capacity", sum((data/n).stat().st_size for n in ("pair-trace.bin", "pair-events.bin", "pair-capacity.json", "anchor-examples.csv", "energy-examples.csv") if (data/n).exists()) <= 8*1024*1024)
    episodes, pair_data, known = {}, {}, {}
    disagreements = []
    for v in trace:
        ep, tick, a, b = v[:4]
        check("pair finite trace", all(math.isfinite(x) for x in v[8:37]))
        check("pair identity", 0 <= a < b < 185 and v[4:8] == (int(initials[ep,a]["slot"]),int(initials[ep,a]["generation"]),int(initials[ep,b]["slot"]),int(initials[ep,b]["generation"])))
        flags, first, run, safe_start = v[37:41]
        states = [{"position": v[j:j+3], "orientation": v[j+3:j+7]} for j in (8,21)]
        independent = max(0., independent_overlap(geometry[a], states[0], geometry[b], states[1])["depth"])
        check("independent pair geometry agrees with recorder", abs(independent-v[35]) <= 1e-10)
        dimension = min(2*geometry[b]["radius"], min(hi-lo for lo,hi in zip(geometry[a]["low"],geometry[a]["high"]))) if a < 5 else 2*min(geometry[a]["radius"],geometry[b]["radius"])
        check("pair collider dimension", dimension == v[36])
        depth = max(0.,v[34],independent)
        sleeping = ((flags&1) and a>=5) or (flags&2)
        settled = bool(flags&64)
        moving = (a>=5 and (not flags&4 or not flags&16)) or (not flags&8 or not flags&32)
        check("dynamic depth cap", depth <= min(.1*dimension,.20))
        check("rest/sleep penetration", depth <= .05 or (not sleeping and not settled))
        if first == tick:
            check("dynamic episode entry", depth>.05 and moving and not sleeping and not settled)
        if flags&128 and abs(v[34]-independent)>1e-5:
            disagreements.append({"episode":ep,"tick":tick,"pair":[a,b],"anchor_depth":v[34],"geometric_overlap":independent})
        if flags&256:
            known.setdefault((a,b),[]).append(tick)
        if first:
            episodes.setdefault((ep,a,b,first),[]).append((tick,depth))
            per = pair_data.setdefault((ep,a,b),{"episodes":set(),"unsafe_samples":0,"unsafe_per_second":{},"peak":0.,"observed_first":tick,"observed_last":tick})
            per["episodes"].add(first)
            per["peak"] = max(per["peak"],depth)
            per["observed_last"] = tick
            if depth>.05:
                hz = 60 if invocation["case"] == "L60" else 120
                per["unsafe_samples"] += 1
                second = str((tick-1)//hz)
                per["unsafe_per_second"][second] = per["unsafe_per_second"].get(second,0)+1
    reports = []
    hz = 60 if invocation["case"] == "L60" else 120
    for key, samples in episodes.items():
        verdict = recovery_verdict(samples,hz,recovery03 and key[1]>=5)
        matching = [e for e in events if tuple(e[:3]) == key[:3] and e[4] == key[3] and e[3] in ((1,6) if long_tail else (1,))]
        check("pair recovery " + str(key), verdict == "RECOVERED" and len(matching)==1)
        if matching:
            check("pair recovery event exposure", matching[0][6] == sum(d>.05 for _,d in samples) and matching[0][5] == samples[-1][0])
        episode_report = {"episode":key[0],"pair":list(key[1:3]),"first_tick":key[3],"last_tick":samples[-1][0],"result":verdict,
                        "unsafe_samples":sum(d>.05 for _,d in samples),"unsafe_seconds":sum(d>.05 for _,d in samples)/hz,
                        "sample_interval_to_last_seconds":(samples[-1][0]-key[3])/hz,"peak_depth":max(d for _,d in samples)}
        if long_tail:
            longest = run = 0
            for _,depth in samples:
                run = run+1 if depth>.05 else 0
                longest = max(longest,run)
            confirmed = len(matching)==1 and len(samples)>=hz//4 and all(d<=.05 for _,d in samples[-hz//4:])
            duration_events = [e for e in events if tuple(e[:3])==key[:3] and e[4]==key[3] and e[3]==5]
            check("diagnostic violation event coverage " + str(key), len(duration_events)==int(verdict.startswith("FAIL_")))
            if matching:
                check("diagnostic full recovery streak " + str(key), confirmed and matching[0][3]==(6 if verdict.startswith("FAIL_") else 1))
            episode_report.update(longest_consecutive_unsafe_samples=longest,first_duration_violation_tick=duration_events[0][5] if duration_events else None,
                                  eventual_sampled_recovery_confirmed=confirmed,recovery_confirmed_tick=samples[-1][0] if confirmed else None,
                                  successful_streak_start_tick=samples[-1][0]-hz//4+1 if confirmed else None,
                                  policy_failure_preserved=verdict.startswith("FAIL_"))
        reports.append(episode_report)
    if invocation["case"] in ("L120","long-tail"):
        check("all21 first2s exact coverage", set(known)==HISTORICAL_PAIRS and all(v==list(range(1,241)) for v in known.values()))
    recurrence = []
    for key, values in pair_data.items():
        values["episode_count"] = len(values.pop("episodes"))
        values["cumulative_exposure_seconds"] = values["unsafe_samples"]/hz
        aggregates = [e for e in events if tuple(e[:3])==key and e[3] in (3,4)]
        check("pair summary exposure and episode counts", len(aggregates)==1 and aggregates[0][7]==values["episode_count"] and aggregates[0][8]==values["unsafe_samples"])
        recurrence.append({"episode":key[0],"pair":list(key[1:]),**values})
    if recovery03 and not minimal_prefix:
        # Closure is established by complete source-supported class accounting in
        # analyze_attribution. Original differences remain visible below.
        check("attribution ledger present", (data/"attribution.csv").is_file())
    elif not minimal_prefix:
        check("no unexplained anchor/geometry disagreement", not disagreements)
    result = {"policy":"AMENDMENT_03" if recovery03 else "AMENDMENT_02","measurement":"End after PostSolve and sleep; max current retained-anchor and independent geometry",
            "trace_records":len(trace),"event_records":len(events),"episodes":reports,"per_pair_recurrence":recurrence,
            "anchor_geometry_disagreement_count":len(disagreements),"first_disagreements":disagreements[:20],
            "disagreement_disposition":"Differences above geometry reconstruction roundoff require explanation; no automatic numeric PASS. No depth acceptance epsilon is added.",
            "stability_claim":"Episode compliance alone never establishes overall stability; existing jitter/drift/energy/contact and owner visual guards remain open."}
    if minimal_prefix:
        result["disagreement_disposition"] = "DEFERRED_TO_C4_FULL_OBSERVATION; selected discrepancies remain reported. Full C4 attribution and exact same-candidate physical parity are required."
        result["native_recovery_guard_ms"] = {"sphere_sphere":100, "sphere_boundary":100}
        result["admitted_recovery_guard_ms"] = {"sphere_sphere":200, "sphere_boundary":100}
        result["native_guard_interpretation"] = "The stricter compiled prefix guard remains blocking; no actual native failure is waived or reclassified."
    if long_tail or (recovery03 and not minimal_prefix):
        result["all_observed_pair_disagreements"] = [{"episode":e[0],"pair":list(e[1:3]),"first_tick":e[4],"last_tick":e[5],"samples":e[6],"maximum_depth_difference":e[12]} for e in events if e[3]==7]
        result["all_observed_disagreement_samples"] = sum(e[6] for e in events if e[3]==7)
        check("all-pair discrepancy summary covers exported discrepancies", result["all_observed_disagreement_samples"]>=len(disagreements))
        result["duration_failure_episodes"] = sum(e[3]==5 for e in events)
        result["late_sampled_recoveries"] = sum(e[3]==6 for e in events)
        result["pending_episodes_at_stop"] = sum(e[3]==3 for e in events)
        result["format"] = ("72-byte relative geometry" if long_tail else "192-byte full selected-pair geometry/velocity") + "; complete selected samples and all retained/pending discrepancy counts; full typed identities pinned."
    return result


PEN_HEADER = struct.Struct("<8s12I5d4Q")
PEN_BODY = struct.Struct("<IQQ13fdB")
PEN_CONTACT = struct.Struct("<6I23fd")
PEN_POSITION = struct.Struct("<3I27fd")
PEN_STAGES = {4: "Narrowphase_before_solve_and_integration", 8: "Integration_before_position_correction",
              9: "Correction_after_PostSolve", 11: "End_after_sleep"}


def lattice_geometry():
    lines = (ROOT / "tools/pre_editor_resting_contact_lattice.txt").read_text().splitlines()
    assert int(lines[0]) == len(lines) - 1 == 185
    geometry = []
    for line in lines[1:]:
        v = list(map(float, line.split()))
        count = int(v[21])
        assert len(v) == 22 + count * 3
        g = {"shape": int(v[0]), "radius": v[20]}
        if g["shape"] == 1:
            points = [v[i:i+3] for i in range(22, len(v), 3)]
            g.update(low=[min(p[j] for p in points) for j in range(3)],
                     high=[max(p[j] for p in points) for j in range(3)])
        geometry.append(g)
    return geometry


def independent_overlap(ga, a, gb, b):
    """Analytic geometry from poses and source bounds; no solver/contact-depth input."""
    if ga["shape"] == gb["shape"] == 0:
        distance = math.dist(a["position"], b["position"])
        return {"method": "sphere_sphere_radius_sum_minus_center_distance", "depth": ga["radius"] + gb["radius"] - distance,
                "center_distance": distance, "radii": [ga["radius"], gb["radius"]], "rounding_bound": 1e-12}
    if ga["shape"] == 1 and gb["shape"] == 0:
        ga, a, gb, b = gb, b, ga, a
    assert ga["shape"] == 0 and gb["shape"] == 1, "unsupported contributing geometry"
    w, x, y, z = b["orientation"]
    assert abs(w*w+x*x+y*y+z*z-1) < 1e-5, "unsupported boundary orientation"
    # GLM's raw-quaternion transform, including its tiny scale deviation. Orthogonal
    # columns are required; do not silently substitute an OBB for a skewed shape.
    axes = [(1-2*(y*y+z*z), 2*(x*y+w*z), 2*(x*z-w*y)),
            (2*(x*y-w*z), 1-2*(x*x+z*z), 2*(y*z+w*x)),
            (2*(x*z+w*y), 2*(y*z-w*x), 1-2*(x*x+y*y))]
    lengths = [math.sqrt(sum(v*v for v in axis)) for axis in axes]
    unit = [[v/n for v in axis] for axis, n in zip(axes, lengths)]
    orthogonality = max(abs(sum(unit[i][k]*unit[j][k] for k in range(3))) for i, j in ((0,1),(0,2),(1,2)))
    assert orthogonality <= 1e-12, "nonorthogonal boundary needs unsupported parallelepiped analysis"
    middle = [(lo+hi)/2 for lo, hi in zip(gb["low"], gb["high"])]
    center = [b["position"][k]+sum(axes[j][k]*middle[j] for j in range(3)) for k in range(3)]
    local = [sum((a["position"][k]-center[k])*axis[k] for k in range(3)) for axis in unit]
    half = [(hi-lo)*n/2 for lo, hi, n in zip(gb["low"], gb["high"], lengths)]
    excess = [abs(v)-h for v, h in zip(local, half)]
    signed_distance = math.sqrt(sum(max(v, 0)**2 for v in excess)) + min(max(excess), 0)
    return {"method": "sphere_oriented_box_signed_distance", "depth": ga["radius"]-signed_distance,
            "sphere_radius": ga["radius"], "box_center": center, "box_axes": unit, "box_half_extents": half,
            "sphere_in_box_frame": local, "signed_distance": signed_distance, "orthogonality_error": orthogonality,
            "rounding_bound": 1e-5}


def penetration_selfcheck():
    sphere = {"shape": 0, "radius": 1.}
    box = {"shape": 1, "low": [-2., -.5, -3.], "high": [2., .5, 3.]}
    state = lambda p, q=(1.,0.,0.,0.): {"position": p, "orientation": q}
    base = state((0.,0.,0.))
    checks = [independent_overlap(sphere, base, sphere, state((2.,0.,0.)))["depth"] == 0,
              abs(independent_overlap(sphere, base, sphere, state((1.9,0.,0.)))["depth"]-.1) < 1e-12,
              abs(independent_overlap(sphere, state((0.,1.4,0.)), box, base)["depth"]-.1) < 1e-12,
              independent_overlap(sphere, base, box, base)["depth"] == 1.5,
              abs(independent_overlap(box, state((0.,0.,0.), (math.sqrt(.5),0.,0.,math.sqrt(.5))),
                                      sphere, state((1.4,0.,0.)))["depth"]-.1) < 1e-12]
    worst = PEN_HEADER.size+4*(16+185*PEN_BODY.size+1024*PEN_CONTACT.size)+1024*PEN_POSITION.size
    assert all(checks) and worst <= 1024*1024
    return {"analytic_cases": len(checks), "worst_packet_serialized_bytes": worst,
            "field_bytes": [PEN_HEADER.size, PEN_BODY.size, PEN_CONTACT.size, PEN_POSITION.size]}


def read_penetration_packet(path):
    raw = path.read_bytes()
    assert len(raw) <= 1024*1024
    offset = 0
    def take(fmt):
        nonlocal offset
        values = fmt.unpack_from(raw, offset)
        offset += fmt.size
        assert all(math.isfinite(v) for v in values if isinstance(v, float)), "nonfinite packet field"
        return values
    h = take(PEN_HEADER)
    assert h[:2] == (b"P21PEN01", 1) and h[4] == 4
    p = dict(zip(("episode", "tick", "stage_count", "position_count", "source", "a", "b", "manifold", "slot", "passes", "hz"), h[2:13]))
    p.update(zip(("metric", "ground_max", "energy", "penetration_limit", "energy_limit"), h[13:18]))
    p.update(zip(("packet_memory_bytes", "recorder_memory_bytes", "session_memory_bytes", "named_memory_bytes"), h[18:22]))
    assert p["episode"] == 0 and p["passes"] == 1 and p["hz"] == 120 and p["position_count"] <= 1024
    assert p["penetration_limit"] == .05 and p["energy_limit"] == 30542.4
    assert p["packet_memory_bytes"] <= 1024*1024 and p["recorder_memory_bytes"] <= 8*1024*1024 and p["named_memory_bytes"] <= 64*1024*1024
    p["stages"] = []
    for expected_stage in PEN_STAGES:
        stage, tick, count, contacts = take(struct.Struct("<4I"))
        assert stage == expected_stage and tick == p["tick"] and count == 185 and contacts <= 1024
        s = {"stage": stage, "name": PEN_STAGES[stage], "bodies": [], "contacts": []}
        for i in range(count):
            b = take(PEN_BODY)
            assert b[0] == i and b[1] and b[2] and b[17] in (0, 1)
            s["bodies"].append({"index": i, "slot": b[1], "generation": b[2], "position": b[3:6], "orientation": b[6:10],
                                "linear": b[10:13], "angular": b[13:16], "inactive": b[16], "sleeping": bool(b[17])})
        for _ in range(contacts):
            c = take(PEN_CONTACT)
            v = dict(zip(("a", "b", "manifold", "slot", "feature_a", "feature_b"), c[:6]))
            for j, name in enumerate(("cached_a", "cached_b", "local_a", "local_b", "normal_b_to_a", "current_a", "current_b")):
                v[name] = c[6+3*j:9+3*j]
            v.update(separation=c[27], toi=c[28], depth=c[29])
            assert 0 <= v["a"] < 185 and 0 <= v["b"] < 185 and v["a"] != v["b"]
            reconstructed = -sum((a-b)*n for a, b, n in zip(v["current_a"], v["current_b"], v["normal_b_to_a"]))
            assert abs(reconstructed-v["depth"]) <= 1e-5, "contact depth serialization/reconstruction"
            s["contacts"].append(v)
        p["stages"].append(s)
    p["positions"] = []
    for _ in range(p["position_count"]):
        c = take(PEN_POSITION)
        v = dict(zip(("a", "b", "slot"), c[:3]))
        assert 0 <= v["a"] < 185 and 0 <= v["b"] < 185
        for j, name in enumerate(("before_a", "after_a", "before_b", "after_b", "normal_a_to_b", "anchor_a", "anchor_b", "local_a", "local_b")):
            v[name] = c[3+3*j:6+3*j]
        v["depth_before_own_correction"] = c[30]
        p["positions"].append(v)
    assert offset == len(raw), "trailing or missing packet fields"
    p["serialized_bytes"] = len(raw)
    return p


def analyze_penetration(result_path):
    """Offline cause attribution only. No result changes a qualification outcome."""
    a = authority()
    assert "diagnostic" in a
    result_path = safe(result_path)
    capture = json.loads(result_path.read_text())
    invocation = json.loads(result_path.with_name("invocation.json").read_text())
    assert invocation["case"] == "penetration" and invocation["diagnostic_authority_sha256"] == sha(DIAG_AUTH)
    output = fresh("analysis", "penetration-diagnostic")
    report = {"phase": 21, "recorded_utc": now(), "classification": "INCONCLUSIVE", "qualification": "NOT_READY",
              "capture": result_path.relative_to(ROOT).as_posix(), "capture_sha256": sha(result_path),
              "penetration_limit": .05, "historical_outcomes_unchanged": True, "further_launch_authorized": False}
    try:
        data = result_path.parent / "data"
        for name, digest in capture["evidence"].items():
            assert sha(data / name) == digest, "evidence changed: " + name
        report["offline_selfcheck"] = penetration_selfcheck()
        p = read_penetration_packet(data / "first-failure.bin")
        report["packet_sha256"] = sha(data / "first-failure.bin")
        report["packet_header"] = {k:v for k, v in p.items() if k not in ("stages", "positions")}
        execution = capture["execution"]
        assert execution["exit_code"] == 1 and not execution["timed_out"] and not execution["error"]
        assert execution["ownership"]["quiescent_before_handle_close"] and execution["ownership"]["job_active_processes"] == 0
        assert "Phase21 lattice safety bound" in result_path.with_name("console.log").read_text(errors="replace")
        metrics = list(csv.DictReader((data / "metrics.csv").open(newline="")))
        assert [int(r["tick"]) for r in metrics] == list(range(1, p["tick"]+1))
        assert float(metrics[-1]["penetration"]) == p["metric"] and float(metrics[-1]["energy"]) == p["energy"]
        assert all(float(r["penetration"]) <= .05 and float(r["energy"]) <= 30542.4 for r in metrics[:-1])
        initials = list(csv.DictReader((data / "initial.csv").open(newline="")))
        assert len(initials) == 185
        geometry = lattice_geometry()
        for s in p["stages"]:
            for b, initial in zip(s["bodies"], initials):
                assert b["index"] == int(initial["index"]) and b["slot"] == int(initial["slot"]) and b["generation"] == int(initial["generation"])
                assert geometry[b["index"]]["shape"] == int(initial["shape"])
                if geometry[b["index"]]["shape"] == 0:
                    assert geometry[b["index"]]["radius"] == float(initial["radius"])
        end = p["stages"][-1]
        assert len(end["contacts"]) == int(metrics[-1]["contacts"])
        assert max([p["ground_max"], 0.] + [c["depth"] for c in end["contacts"]]) == p["metric"]
        i, j = p["a"], p["b"]
        assert 0 <= i < 185 and 0 <= j < 185
        if p["source"] == 2:
            contributor = next(c for c in end["contacts"] if (c["a"],c["b"],c["manifold"],c["slot"]) == (i,j,p["manifold"],p["slot"]))
            assert contributor["depth"] == p["metric"]
        else:
            assert p["source"] == 1 and j == 4 and p["ground_max"] == p["metric"]
            contributor = {"source": "independent_ground_depth", "a": i, "b": j}
        report["measurement"] = {"stage": PEN_STAGES[11], "formula": "max(0, ground top minus body bottom, -dot(current local-anchor A minus B, retained contact normal B-to-A))",
                                 "contributor": contributor, "body_a": end["bodies"][i], "body_b": end["bodies"][j],
                                 "geometries": [geometry[i], geometry[j]], "independent_ground_max": p["ground_max"]}
        pair_reports = []
        for s in p["stages"]:
            pair_contacts = [c for c in s["contacts"] if {c["a"],c["b"]} == {i,j}]
            pair_reports.append({"stage": s["name"], "body_a": s["bodies"][i], "body_b": s["bodies"][j],
                                 "independent_overlap": independent_overlap(geometry[i], s["bodies"][i], geometry[j], s["bodies"][j]),
                                 "pair_contacts": pair_contacts,
                                 "stage_max_retained_contact_depth": max([0.] + [c["depth"] for c in s["contacts"]])})
        report["contributing_pair_by_stage"] = pair_reports
        # Every applied PostSolve translation is already reported by the existing
        # observer. Reconcile the whole correction sequence, retaining only events
        # that move either member of the actual contributing pair in the report.
        current = [dict(b) for b in p["stages"][1]["bodies"]]
        related = []
        for sequence, event in enumerate(p["positions"]):
            ai, bi = event["a"], event["b"]
            assert current[ai]["position"] == event["before_a"] and current[bi]["position"] == event["before_b"], "correction event continuity"
            before = independent_overlap(geometry[i], current[i], geometry[j], current[j])["depth"]
            current[ai]["position"], current[bi]["position"] = event["after_a"], event["after_b"]
            if {ai,bi} & {i,j}:
                related.append({"sequence": sequence, **event, "contributor_overlap_before": before,
                                "contributor_overlap_after": independent_overlap(geometry[i], current[i], geometry[j], current[j])["depth"]})
        for b, corrected, final in zip(current, p["stages"][2]["bodies"], end["bodies"]):
            assert b["position"] == corrected["position"] == final["position"], "unaccounted position change"
            for field in ("orientation", "linear", "angular"):
                assert b[field] == corrected[field], "unaccounted PostSolve nontranslation"
            assert corrected["orientation"] == final["orientation"]
        report["position_correction"] = {"all_events": len(p["positions"]), "complete_translation_reconciliation": True,
                                         "events_touching_contributing_pair": related, "end_pose_matches_post_correction": True}
        previous = list(csv.DictReader((BASE / "native/Debug-debug-02/data/metrics.csv").open(newline="")))
        comparable = min(len(previous), len(metrics))
        parity = [r["state_hash"] for r in previous[:comparable]] == [r["state_hash"] for r in metrics[:comparable]]
        report["prior_failed_prefix_comparison"] = {"ticks": comparable, "physical_state_hashes_equal": parity,
                                                    "role": "observer noninterference comparison only; historical FAIL remains FAIL"}
        assert parity, "diagnostic prefix diverged; attribution to original failure is incomplete"
        report["native_failure_preserved"] = True
        report["accepted_gate"] = "FAIL" if p["metric"] > .05 else "OTHER_SAFETY_FAILURE"
        geo = pair_reports[-1]["independent_overlap"]
        if p["metric"] > .05 and geo["depth"] - geo["rounding_bound"] > .05:
            report["classification"] = "CONFIRMED_POST_CORRECTION_GEOMETRIC_OVERLAP"
            before = pair_reports[1]["independent_overlap"]["depth"]
            report["causal_scope"] = "Contributing pair already exceeds 0.05 before PostSolve and still exceeds it after correction/end." if before > .05 else "Contributing pair crosses 0.05 during the reconciled PostSolve translation sequence."
            report["minimum_owner_decision"] = "Keep P21-PEN-01 blocking. Any proposed production remedy needs separate explicit scope/authority; no observer-only correction or criterion relaxation is justified by this packet."
        elif p["metric"] > .05 and geo["depth"] + geo["rounding_bound"] <= .05:
            report["classification"] = "CONFIRMED_CONTACT_METRIC_GEOMETRY_DISAGREEMENT"
            report["minimum_owner_decision"] = "Review the exact demonstrated metric/geometry mismatch before authorizing any correction; preserve the accepted measurement meaning and failed gate. No automatic rerun or Physics edit."
        else:
            report["limitation"] = "Geometric attribution does not decisively classify the penetration failure."
    except (AssertionError, OSError, ValueError, KeyError, IndexError, StopIteration, struct.error) as error:
        report["classification"] = "INCONCLUSIVE"
        report["limitation"] = repr(error)
    if report["classification"] == "INCONCLUSIVE":
        report["minimum_owner_decision"] = "Retain the blocker; decide whether to authorize a specifically scoped next investigation. No repeat is authorized by this diagnostic."
    write(output / "analysis.json", report)
    print(json.dumps({"analysis": str(output / "analysis.json"), "classification": report["classification"],
                      "limitation": report.get("limitation"), "minimum_owner_decision": report["minimum_owner_decision"]}), flush=True)


def analyze_refinement_resistance(rows, operation_counts, check, scope):
    """Validate bounded angular certificates against independent operation counts."""
    failures = []
    def gate(name, condition):
        name = scope + " " + name
        check(name, condition)
        if not condition:
            failures.append(name)
    def count(value):
        result = int(value)
        assert not isinstance(value, bool) and str(value) == str(result) and 0 <= result < 2**64
        return result
    integer_fields = ("events", "certified", "forced_retractions", "positive_retractions", "failures")
    real_fields = ("positive_work", "max_objective_ratio", "max_gap_ratio")
    totals = {op:{field:0 for field in (*integer_fields, *real_fields)} for op in ("spin", "rolling")}
    complete = {op:0 for op in totals}
    gate("certificate row coverage", len(rows) > 0 and len(rows) == len(operation_counts))
    for index, row in enumerate(rows):
        for oi, op in enumerate(totals):
            try:
                values = {field:count(row[op+"_"+field]) for field in integer_fields}
                for field in real_fields:
                    raw = row[op+"_"+field]
                    values[field] = float(raw)
                    assert not isinstance(raw, bool) and math.isfinite(values[field]) and values[field] >= 0
                actual = count(operation_counts[index][oi])
            except (AssertionError, KeyError, IndexError, TypeError, ValueError, OverflowError):
                gate(op+" complete finite typed certificate", False)
                continue
            complete[op] += 1
            gate(op+" actual operation coverage", values["events"] == actual)
            gate(op+" every operation certified", values["certified"] == values["events"] and
                values["failures"] == values["events"]-values["certified"] == 0)
            gate(op+" retraction classification counts", 0 <= values["positive_retractions"] <=
                values["forced_retractions"] <= values["events"])
            gate(op+" positive retraction work classification", (values["positive_retractions"] == 0) ==
                (values["positive_work"] == 0))
            gate(op+" unchanged objective and gap allowance", values["max_objective_ratio"] <= 1 and
                values["max_gap_ratio"] <= 1)
            gate(op+" empty certificate has zero maxima", values["events"] != 0 or
                values["max_objective_ratio"] == values["max_gap_ratio"] == 0)
            for field in integer_fields:
                totals[op][field] += values[field]
            totals[op]["positive_work"] += values["positive_work"]
            for field in ("max_objective_ratio", "max_gap_ratio"):
                totals[op][field] = max(totals[op][field], values[field])
    gate("finite certificate aggregate", all(math.isfinite(v) for op in totals.values() for v in op.values()))
    return {"result":"PASS_ANGULAR_CERTIFICATE_COVERAGE" if not failures else "FAIL_OR_INCOMPLETE",
        "failures":sorted(set(failures)), "rows":len(rows), "complete_rows":complete, "operations":totals,
        "required_classification":"Reported positive work belongs to certified forced retractions with an infeasible old accumulator; feasible-old incremental dissipation remains required by the observer.",
        "allowance":"Objective and independent gap ratios use the unchanged tau=1e-5*max(1,pairKinetic); no additional acceptance tolerance.",
        "scope":"Certificate coverage only. Global energy, physical, CPU, ToI and all remaining mandatory gates stay independent; this record does not close P1 or qualify the candidate."}


def analyze_refinement(data, invocation, metrics, body_rows, initials, check):
    failures = []
    def gate(name, condition):
        check(name, condition)
        if not condition:
            failures.append(name)
    work = process_tools.remediation_csv(data / "refinement-work.csv")
    minimal_prefix = invocation["case"] == "prefix"
    key = lambda row: (int(row["episode"]), int(row["tick"]))
    gate("refinement exact per-tick work coverage", [key(r) for r in work] == [key(r) for r in metrics])
    gate("refinement finite evidence", all(math.isfinite(float(v)) for r in work for v in r.values()))
    resistance = {"result":"DEFERRED_TO_C4_FULL_OBSERVATION", "scope":"Detailed operation callbacks are disabled; no angular certificate coverage is claimed."} if minimal_prefix else analyze_refinement_resistance(work,
        [(r.get("op_count_2"), r.get("op_count_3")) for r in metrics], gate, "refinement tick")
    if minimal_prefix:
        fields = set(("episode,tick,summary_mask,flags,graph_scratch_bytes,manifold_capacity,constraint_bytes,bodies,points,manifolds,"
            "components,eligible_components,eligible_points,unsupported_components,mixed_components,kinematic_components,sleeping_bodies,prepared_rows,"
            "pointer_comparisons,union_hops,compression_hops,extra_visits,normal_attempts,tangent_attempts,tangent_bisections,spin_attempts,rolling_attempts,"
            "rolling_bisections,block_attempts,block_masks,finite_preflights,finite_failure,normal_rejected,tangent_rejected,spin_rejected,rolling_rejected,"
            "block_rejected,ordinary_impulse_events,reverse_impulse_events,forward_impulse_events,ordinary_expected_work,reverse_expected_work,forward_expected_work,"
            "ordinary_measured_work,reverse_measured_work,forward_measured_work,upward_correction,ordered_impulse_events,operation_order_hash,graph_planning_ns").split(","))
        fields |= {op+"_"+field for op in ("spin", "rolling") for field in ("events", "certified", "forced_retractions",
            "positive_retractions", "positive_work", "max_objective_ratio", "max_gap_ratio", "failures")}
        gate("minimal prefix complete work schema", all(set(r) == fields for r in work))
        gate("minimal prefix zero unobserved work", all(float(value) == 0 for row in work for name, value in row.items()
            if name not in ("episode", "tick", "operation_order_hash")))
        gate("minimal prefix unchanged initial operation order", all(int(r["operation_order_hash"]) == 14695981039346656037 for r in work))
    expanded = invocation.get("refinement_authority_sha256") == REFINEMENT_AUTH_SHA
    if expanded:
        gate("graph planning time samples present", all("graph_planning_ns" in r for r in work))
        gate("graph planning finite nonnegative integer nanoseconds", all(
            "graph_planning_ns" in r and 0 <= int(r["graph_planning_ns"]) < 2**64 for r in work))
    proof = reference(invocation["prelaunch_proof"])
    storage = reference(proof["storage_proof"])
    upper_capacity = storage["manifold_vector_capacity_upper"]
    actual_constraint_bytes = storage["baseline_constraint_bytes"] + storage["added_constraint_bytes"]
    limits = {"bodies":180, "points":480, "manifolds":480, "extra_visits":960,
        "normal_attempts":2880, "tangent_attempts":2880, "tangent_bisections":92160,
        "spin_attempts":960, "rolling_attempts":960, "rolling_bisections":46080,
        "block_attempts":480, "block_masks":3840, "pointer_comparisons":172800,
        "union_hops":171840, "compression_hops":32220}
    observed = invocation["case"] != "prefix"
    summaries, eligible, ordinary, reverse, forward = 0, 0, 0, 0, 0
    for row in work:
        gate("refinement work nonnegative/bounded", all(0 <= int(row[k]) <= bound for k, bound in limits.items()))
        gate("refinement complete summaries", int(row["summary_mask"]) in (0, 7))
        gate("refinement no capacity fallback or finite abort", not int(row["flags"]) & 1 and int(row["finite_failure"]) == 0)
        gate("exact two local visits per eligible point", int(row["extra_visits"]) == 2*int(row["eligible_points"]))
        if int(row["summary_mask"]):
            summaries += 1
            eligible += int(row["eligible_components"]) > 0
            gate("compiled/runtime graph scratch match", 0 < int(row["graph_scratch_bytes"]) <= 3072)
            gate("actual retained constraint storage bound", int(row["manifold_capacity"]) <= upper_capacity and
                int(row["constraint_bytes"]) == actual_constraint_bytes)
        ordinary += int(row["ordinary_impulse_events"])
        reverse += int(row["reverse_impulse_events"])
        forward += int(row["forward_impulse_events"])
    if observed and invocation["case"] in ("debug", "L120", "L60", "LA", "LP", "small", "spherebox-support"):
        gate("observed loaded-component coverage", summaries > 0 and eligible > 0 and reverse > 0 and forward > 0)
    direct = []
    direct_resistance = None
    if invocation["case"] == "debug":
        direct = [json.loads(line) for line in (data / "direct-refinement.jsonl").read_text().splitlines()]
        required = set(REFINEMENT_DIRECT_CHECKS)
        gate("bounded direct invariant check membership", {r["name"] for r in direct} == required and len(direct) == len(required))
        gate("bounded direct invariant checks PASS", all(r["pass"] is True for r in direct))
        direct_resistance = analyze_refinement_resistance(direct,
            [(r.get("spin_operations"), r.get("rolling_operations")) for r in direct], gate, "refinement direct")
        coherent = [r for r in direct if r["name"] == "coherent-block-normal-friction-resistance"]
        gate("direct resistance and forced positive spin coverage", len(coherent) == 1 and
            all(isinstance(coherent[0].get(field), int) and not isinstance(coherent[0][field], bool) and
                coherent[0][field] > 0 for field in ("spin_operations", "rolling_operations", "spin_positive_retractions")))
    support = None
    if invocation["case"] == "spherebox-support":
        episodes = process_tools.remediation_csv(data / "episodes.csv")
        gate("exact two support cases", [(r["name"], int(r["hz"]), int(r["ticks"])) for r in episodes] ==
            [("SphereBox-support", 60, 1200), ("SphereBoxStack-support", 60, 1200)])
        sphere = [r for r in metrics if int(r["episode"]) == 0]
        body = body_rows.get((0, 1), [])
        gate("SphereBox-support full body sample coverage", [v[1] for v in body] == list(range(1, 1201)))
        rest = [v for v in body if v[1] >= 181]
        rest_metrics = [r for r in sphere if int(r["tick"]) >= 181]
        upward = [float(r["upward_correction"]) for r in work if int(r["episode"]) == 0 and int(r["tick"]) > 60]
        gate("SphereBox-support upward correction", len(upward) == 1140 and max(upward, default=math.inf) <= .005)
        gate("SphereBox-support grounded depth", len(rest_metrics) == 1020 and max((float(r["penetration"]) for r in rest_metrics), default=math.inf) <= .035)
        gate("SphereBox-support grounded vertical range", len(rest) == 1020 and max((v[6] for v in rest), default=math.inf)-min((v[6] for v in rest), default=-math.inf) <= .005)
        gate("SphereBox-support grounded vertical speed", max((abs(v[13]) for v in rest), default=math.inf) <= .01)
        gate("SphereBox-support grounded energy rise", bool(rest_metrics) and max(float(r["energy"])-float(rest_metrics[0]["energy"]) for r in rest_metrics) <= .001)
        terminal = [r for r in sphere if int(r["tick"]) > 1140]
        gate("SphereBox-support final60 quiet/no wakes", len(terminal) == 60 and all(int(r["quiet"]) == 1 and int(r["wakes"]) == 0 for r in terminal))
        for (ep, index), rows in body_rows.items():
            if ep != 1 or float(initials[ep, index]["inverse_mass"]) <= 0:
                continue
            gate("SphereBoxStack full dynamic sample coverage", [v[1] for v in rows] == list(range(1,1201)))
            for v in rows:
                quiet = bool(v[18]) or (math.sqrt(sum(x*x for x in v[12:15])) <= .05 and math.sqrt(sum(x*x for x in v[15:18])) <= .02)
                gate("SphereBoxStack supported rest depth", not (quiet and v[20]) or v[21] <= .05)
        support = {"grounded_from_tick":181, "grounded_samples":len(rest), "terminal_samples":len(terminal),
            "normal_stack_has_no_all_terminal_quiet_gate":True, "observation_seconds_per_case":20}
    def distribution(values):
        values = sorted(values)
        return {"samples":len(values), "mean_ns":sum(values)/len(values) if values else None,
            "p95_ns":values[math.ceil(.95*len(values))-1] if values else None, "max_ns":max(values) if values else None}
    graph_timing = distribution([int(r["graph_planning_ns"]) for r in work
        if "graph_planning_ns" in r and int(r["summary_mask"]) == 7])
    observer_costs = {field:distribution([int(r[field]) for r in metrics]) for field in ("callback_ns","event_ns")}
    return {"result":("MINIMAL_PREFIX_COMPLETE_C4_REQUIRED" if minimal_prefix else "PASS_BOUNDED_REFINEMENT_WORK") if not failures else "FAIL_OR_INCOMPLETE", "failures":sorted(set(failures)),
        "rows":len(work), "summary_ticks":summaries, "eligible_ticks":eligible,
        "impulse_events":{"ordinary":ordinary,"reverse":reverse,"forward":forward},
        "maxima":{field:max((int(r[field]) for r in work), default=0) for field in limits},
        "actual_max_manifold_capacity":max((int(r["manifold_capacity"]) for r in work), default=0),
        "compiled_constraint_bytes":actual_constraint_bytes, "retained_added_bytes_at_observed_max":
            4*max((int(r["manifold_capacity"]) for r in work), default=0)*storage["added_constraint_bytes"],
        "direct_checks":direct, "support":support,
        "angular_resistance_certificates":{"ticks":resistance, "direct":direct_resistance},
        "graph_planning_time":graph_timing, "observer_costs_separate":observer_costs,
        "graph_timing_interpretation":"Detailed-observer samples with complete summaries only; includes timer overhead. Minimal/no-summary rows excluded. No subtraction from original C3 World/Solver CPU acceptance or any other CPU gate.",
        "scope":"Minimal prefix summary coverage is checked by C4 physical parity; no baseline physical PASS reused."}


REFINEMENT_DIRECT_CHECKS = (
    "exact-180-body-480-contact-capacity",
    "full-component-lateral", "no-repeated-warmstart", "LP-once-per-step", "PostSolve-single-translation",
    "identity-lifecycle", "shared-static-separates-components", "unsupported-component", "unloaded-static-boundary",
    "first-occurrence-determinism", "kinematic-boundary", "mixed-sleeping-component", "sleeping-preparation-reset",
    "zero-step-no-opportunity", "whole-collector-body-overflow", "whole-collector-manifold-overflow",
    "whole-collector-point-overflow", "coherent-block-normal-friction-resistance", "independent-extra-impulse-energy",
    "material-restitution-preserved", "finite-arithmetic-abort", "zero-effective-denominator")


def analyze_original_phase20(result_path):
    """Original query/helper/physical/36-cost gates, using preserved pure helpers."""
    invocation = json.loads(result_path.with_name("invocation.json").read_text())
    capture = json.loads(result_path.read_text())
    data = result_path.parent / "data"
    output = fresh("analysis", result_path.parent.name)
    report = {"phase":21, "recorded_utc":now(), "capture":result_path.relative_to(ROOT).as_posix(),
        "capture_sha256":sha(result_path), "checks":{}, "historical_results_unchanged":True,
        "result":"FAIL_OR_INCOMPLETE", "scope":"Original Phase20 protocol in C3; no old authority spent"}
    cache = {}
    def checked(name, operation):
        try:
            report["checks"][name] = {"status":"PASS", "detail":operation()}
        except (AssertionError, OSError, ValueError, KeyError, TypeError, csv.Error) as error:
            report["checks"][name] = {"status":"FAIL_OR_INCOMPLETE", "reason":repr(error)}
    def rows(name):
        result = process_tools.remediation_csv(data / name)
        assert all(None not in r and all(v not in (None, "") for v in r.values()) for r in result)
        return result
    def finite(values):
        assert all(math.isfinite(float(v)) for r in values for v in r.values())
    def integrity():
        assert capture["status"] == "CAPTURE_COMPLETE_ANALYSIS_PENDING" and not capture["errors"]
        assert all(sha(data / p) == h for p, h in capture["evidence"].items())
        assert set(capture["evidence"]) == {p.name for p in data.iterdir() if p.is_file()}
        assert sum(p.stat().st_size for p in data.iterdir() if p.is_file()) <= 8*1024*1024
        expected = "P20_POST_CANDIDATE_FOCUSED_V1\ncandidate\n" + process_tools.FOCUSED_LATTICE_HASH + "\n" + process_tools.REPLAY_INPUT_HASH + "\n25 50 16 12 4 11 4980 68 120\nEND\n"
        request = result_path.with_name("focused-request.txt")
        assert request.read_text() == expected and sha(request) == invocation["request_sha256"]
        assert sha(process_tools.FOCUSED_LATTICE) == process_tools.FOCUSED_LATTICE_HASH
        # The request's terminal120 token is a checked legacy protocol identity,
        # not a clock used by Session::Open. Expanded authority changes only the
        # owned process envelope; all68simulated seconds/4980ticks/CPU gates stay.
        limit = 240 if invocation["refinement_authority_sha256"] == REFINEMENT_AUTH_SHA else 120
        assert invocation["limit_seconds"] == limit and not invocation["automatic_retry"]
        assert capture["wall_seconds"] <= limit + 5 and capture["execution"]["wall_seconds"] <= limit
        execution = capture["execution"]
        assert execution["exit_code"] == 0 and not execution["timed_out"] and not execution["error"]
        owner = execution["ownership"]
        assert owner["assigned_before_resume"] and owner["kill_on_job_close"] and owner["quiescent_before_handle_close"] and not owner["breakaway_allowed"]
        native = json.loads((data / "result.json").read_text())
        assert native == capture["native"]
        assert native["complete"] and native["shutdown"] and native["conditional"] and native["candidate"] and not native["failure"] and not native["overflow"]
        fixed = {"episodes":11,"ticks":4980,"collision_calls":50,"direct_gjk_calls":16,"ca_helper_calls":12,
            "projection_helper_calls":4,"profiled_ticks":4980,"post_ticks":1680,"post_stage_records":288,"physical_failures":0}
        assert all(native[k] == v for k,v in fixed.items())
        assert native["profiling_enabled"] and native["post_candidate_policy"] and native["physics_world_ns"] > 0
        assert 0 < native["post_event_records"] <= 1024 and 0 < native["post_storage_bytes"] <= 1024*1024 and native["post_sliding"] > 0
        return fixed
    def control_identity():
        assert sha(CONTROL02 / "result.json") == CONTROL02_SHA
        control = json.loads((CONTROL02 / "result.json").read_text())
        assert all(sha(CONTROL02 / p) == h for p, h in control["evidence"].items())
        assert control["native"]["complete"] and control["native"]["shutdown"]
        old = process_tools.CONDITIONAL
        assert sha(old / "focused-query-inputs.json") == "66bc00d24373289301cdcc9596b4fec7598e1ee053b0372bcf61f84c9f5a4c5b"
        previous = old / "runs/Release-candidate-01/data"
        assert sha(previous / "focused-queries.csv") == "411f7cbbf6b9cf3b468cfe6261124548dfb8993ca43f67abaf7da6dfdbe94be6"
        assert sha(previous / "focused-helpers.jsonl") == "fce255fe4776c0ef4d542cf62fe208603cda54bbada53356616df536f8127d21"
        cache.update(control=control, previous=previous)
        return {"control_result_sha256":CONTROL02_SHA,"historical_failures_preserved":True}
    def queries():
        input_result = process_tools.remediation_query_inputs(data)
        assert input_result["pass"] and input_result["rows"] == input_result["expected_rows"] == 50
        assert (CONTROL02 / "data/focused-inputs.csv").read_bytes() == (data / "focused-inputs.csv").read_bytes()
        actual = rows("focused-queries.csv"); finite(actual)
        assert [(int(r["case"]),int(r["mode"])) for r in actual] == [(i,j) for i in range(25) for j in range(2)]
        for off,on in zip(actual[::2],actual[1::2]):
            assert all(off[k] == on[k] for k in off if k not in ("mode","elapsed_ns","observer_ns"))
        assert all(r["pointers_ok"] == r["unchanged"] == r["geometry_ok"] == "1" for r in actual)
        old = process_tools.remediation_csv(CONTROL02 / "data/focused-queries.csv")
        for before,after in zip(old,actual):
            if before["hit"] == "1":
                assert after["hit"] == "1" and all(before[k] == after[k] for k in before if k.startswith("contact_bits_"))
        previous = process_tools.remediation_csv(cache["previous"] / "focused-queries.csv")
        exclude = lambda r:{k:v for k,v in r.items() if k not in ("elapsed_ns","observer_ns")}
        assert len(previous) == len(actual) and list(map(exclude,previous)) == list(map(exclude,actual))
        return input_result
    def helpers():
        actual = [json.loads(line) for line in (data / "focused-helpers.jsonl").read_text().splitlines()]
        assert [(r["kind"],r["case"],r["mode"]) for r in actual] == [(k,i,j) for k,n in (("gjk",8),("ca",6),("projection",2)) for i in range(n) for j in range(2)]
        for off,on in zip(actual[::2],actual[1::2]):
            assert {k:v for k,v in off.items() if k != "mode"} == {k:v for k,v in on.items() if k != "mode"}
        assert all(r["pass"] is True for r in actual)
        assert all(r["expected_build_behavior"] and r["unchanged"] for r in actual if r["kind"] == "ca")
        assert actual == [json.loads(line) for line in (cache["previous"] / "focused-helpers.jsonl").read_text().splitlines()]
        return {"rows":len(actual)}
    def episodes():
        durations = (1800,300,300,120,120,120,120,300,1200,300,300)
        counts = (185,2,2,2,2,2,2,2,5,2,2)
        qualification,frames,initial = rows("focused-qualification.csv"),rows("frames.csv"),rows("initial.csv")
        finite(qualification); finite(frames); finite(initial)
        assert [(int(r["episode"]),int(r["tick"])) for r in qualification] == [(ep,tick) for ep,n in enumerate(durations) for tick in range(1,n+1)]
        old = process_tools.remediation_csv(CONTROL02 / "data/initial.csv")
        exclude = lambda r:{k:v for k,v in r.items() if k not in ("slot","generation")}
        assert list(map(exclude,initial)) == list(map(exclude,old))
        detail = {}
        for ep,n in enumerate(durations):
            q = [r for r in qualification if int(r["episode"]) == ep]
            f = [r for r in frames if int(r["episode"]) == ep]
            bodies = [r for r in initial if int(r["episode"]) == ep]
            assert len(bodies) == len({(r["slot"],r["generation"]) for r in bodies}) == counts[ep]
            assert all(int(r["bodies"]) == counts[ep] for r in q)
            assert f and int(f[-1]["tick"]) == int(f[-1]["total_steps"]) == n
            assert all(int(r["tick"]) == int(r["total_steps"]) and 0 <= int(r["steps_this_frame"]) <= 2 and float(r["pending"]) >= 0 and float(r["discarded"]) >= 0 for r in f)
            detail[str(ep)] = {"ticks":n,"peak_energy":max(float(r["energy"]) for r in q),
                "max_penetration":max(float(r["penetration"]) for r in q),"discarded_seconds":float(f[-1]["discarded"]),
                "maximum_capacity_bytes":max(int(r["capacity_bytes"]) for r in q),"final_sleeping":int(q[-1]["sleeping"])}
        return detail
    def gates():
        expected = ("normal_nonnegative","coulomb","friction_energy","zero_resistance","impulse_equation",
            "sphere_energy_residual","rest_penetration","rest_height_range","rest_vertical_speed","upward_correction",
            "quiet_window","unexplained_wake","rest_energy","missed_penetrating_contact","impact_response",
            "accumulated_friction_energy","friction_optimality","friction_cache_continuity","persistent_response_energy",
            "persistent_response_balance","friction_certificate")
        actual = rows("focused-gates.csv")
        assert [(int(r["gate"]),r["name"]) for r in actual] == list(enumerate(expected))
        assert all(int(r["checks"]) > 0 and int(r["failures"]) == int(r["examples"]) == 0 for r in actual)
        return actual
    def friction():
        result = process_tools.post_analyze_friction(data)
        write(output / "post-friction.json", result)
        assert result["pass"]
        return {"path":(output / "post-friction.json").relative_to(ROOT).as_posix(),"sha256":sha(output / "post-friction.json")}
    def cost():
        actual = rows("focused-cpu.csv")
        assert [(int(r["episode"]),int(r["tick"])) for r in actual] == [(ep,t) for ep,n in ((0,1800),(8,1200),(10,300)) for t in range(1,n+1)]
        current = process_tools.remediation_cost(data)
        comparison = process_tools.remediation_compare_cost(process_tools.remediation_cost(CONTROL02 / "data"),current)
        write(output / "cost.json",current); write(output / "cost-comparison.json",comparison)
        report["cost_comparison"] = comparison
        assert len(comparison["rows"]) == 36
        assert not comparison["any_numeric_exceedance"] and not comparison["any_inconclusive_spread"]
        assert all(r["numeric_pass"] and r["comparability_pass"] and len(r["block_difference_ns"]) == 5 for r in comparison["rows"])
        return {"comparisons":36,"status":comparison["status"]}
    def comparability():
        current,old = capture["environment"],cache["control"]["environment"]
        identity = process_tools.remediation_machine_identity
        assert current["stable_machine_identity"] and identity(current["machine_before"]) == identity(current["machine_after"]) == identity(old["machine_before"])
        assert current["native_process"] == old["native_process"]
        refinement_prelaunch(safe(invocation["prelaunch_proof"]["path"]),safe(invocation["build"]),"Release")
        return {"hardware_power_identity_equal":True,"priority_affinity_equal":True,"compiled_source_options_profile_capacity_bound":True}
    for name,operation in (("integrity_lifecycle_counts",integrity),("Control02_preserved_identity",control_identity),
        ("frozen_inputs_queries50",queries),("helpers32",helpers),("episodes11_ticks4980",episodes),
        ("physical_gates21",gates),("friction_causal_windows",friction),("raw_CPU_comparisons36",cost),
        ("machine_toolchain_profile_capacity_comparability",comparability)):
        checked(name,operation)
    report["failures"] = [name for name,value in report["checks"].items() if value["status"] != "PASS"]
    if not report["failures"]:
        report["result"] = "PASS_NUMERIC_HUMAN_QUALITATIVE_PENDING"
    write(output / "analysis.json", report)
    print(json.dumps({"analysis":str(output / "analysis.json"),"result":report["result"],"failures":report["failures"]}),flush=True)
    if report["failures"]:
        raise SystemExit(1)


def reference(ref):
    path = safe(ref["path"])
    assert sha(path) == ref["sha256"], "reference drift: " + str(path)
    return json.loads(path.read_text())


def refinement_authority():
    """Distinct prospective grant; historical authorities never supply new slots."""
    assert sha(REFINEMENT_AUTH) == REFINEMENT_AUTH_SHA, "accepted authority changed"
    a = json.loads(REFINEMENT_AUTH.read_text())
    assert a["status"] == "ACCEPTED_EXPANDED_AUTONOMOUS_ENGINEERING_CAMPAIGN_02"
    assert set(a["implementation_files"]) == REFINEMENT_OWNED
    assert a["production_changes_authorized"] and not a["threshold_changes_authorized"] and not a["approval_or_publication_authorized"]
    assert git("rev-parse", "HEAD").decode().strip() == a["baseline"]
    assert git("branch", "--show-current").decode().strip() == "pre-editor/refactor"
    assert not git("diff", "--cached", "--name-only").strip()
    assert set(git("diff", "--name-only").decode().splitlines()) <= REFINEMENT_OWNED
    for value in a.values():
        if isinstance(value, dict) and "path" in value and "sha256" in value:
            assert sha(safe(value["path"])) == value["sha256"], value["path"]
    admission = reference(a["admission"])
    assert a["algorithm"] == admission["algorithm"]
    assert a["initial_matrix"] == reference(a["matrix"])["matrix"]
    # Only operational envelopes changed. Preserve every frozen workload field.
    without_deadline = lambda row: {k:v for k,v in row.items() if k != "native_deadline_seconds"}
    assert [without_deadline(row) for row in a["initial_matrix"]] == [without_deadline(row) for row in admission["initial_matrix"]]
    assert [row["native_deadline_seconds"] for row in a["initial_matrix"]] == [360,180,240,360,360,300,360,360,300]
    assert (a["limits"]["native_total_seconds"], a["limits"]["build_total_seconds"], a["cumulative_native_attempt_cap"],
            a["cumulative_build_command_cap"], a["limits"]["build_command_seconds"]) == (12000,24000,64,64,1800)
    assert a["limits"]["native_wrapper_allowance_seconds"] == 5
    assert a["superseded_operational_authority"]["sha256"] == REFINEMENT_AUTH_01_SHA
    campaign_entry = reference(a["campaign_entry"])
    assert set(campaign_entry["source_snapshots"]) == REFINEMENT_OWNED
    for source, ref in campaign_entry["source_snapshots"].items():
        assert sha(safe(ref["path"])) == ref["sha256"] == campaign_entry["source_hashes"][source]
    preservation = reference(a["preservation"])
    assert sha(Path(preservation["index"]["path"])) == preservation["index"]["sha256"], "owner index changed"
    entry = json.loads((BASE / "entry-01/entry.json").read_text())
    for name, value in entry["protected"].items():
        if name != "docs/pre_editor/active/REFACTOR_STATE.md":
            assert sha(safe(name)) == value["sha256"], "protected input changed: " + name
    for name, value in preservation["files"].items():
        assert sha(safe(value["saved"]["path"])) == value["saved"]["sha256"], "preserved raw bytes changed"
        if name in ("RigidBodySimulation/src/RigidBodySimulation.cpp", "RigidBodySimulation/src/main.cpp"):
            assert sha(safe(name)) == value["sha256"], "owner source changed: " + name
    assert not (ROOT / "pre-editor-checkpoints/phase-21.json").exists(), "sealed candidate"
    return a


def refinement_machine():
    assert sha(CONTROL02 / "result.json") == CONTROL02_SHA
    control = json.loads((CONTROL02 / "result.json").read_text())
    current = process_tools.remediation_machine()
    assert process_tools.remediation_machine_identity(current) == process_tools.remediation_machine_identity(
        control["environment"]["machine_before"]), "Control02 CPU/topology/power differs; no new control authorized"
    return current, control


def refinement_records(kind, wave=None):
    records = []
    for path in (BASE / kind).glob("*/invocation.json"):
        inv = json.loads(path.read_text())
        if inv.get("refinement_authority_sha256") == REFINEMENT_AUTH_SHA and (wave is None or inv.get("wave") == wave):
            result_path = path.with_name("result.json")
            assert result_path.exists(), "unfinished attempt: " + str(path)
            records.append((path, inv, json.loads(result_path.read_text())))
    return sorted(records, key=lambda r: r[1]["started_utc"])


def refinement_analysis_pass(path, inv, result):
    assert result["status"] == "CAPTURE_COMPLETE_ANALYSIS_PENDING", "failed/incomplete slot blocks continuation"
    analyses = list((BASE / "analysis").glob(path.parent.name + "-*/analysis.json"))
    assert len(analyses) == 1, "exactly one immutable analysis required"
    report = json.loads(analyses[0].read_text())
    assert report["capture_sha256"] == sha(path.with_name("result.json"))
    assert report["result"] == "PASS_NUMERIC_HUMAN_QUALITATIVE_PENDING", "slot analysis not PASS"
    if inv["case"] == "prefix":
        assert inv["slot"] == "C2" and inv["configuration"] == "Release"
        assert report.get("attribution", {}).get("result") == "DEFERRED_TO_C4_FULL_OBSERVATION", "minimal prefix profile incomplete"
        assert report.get("attribution", {}).get("physical_samples") == 1800
        assert report.get("refinement", {}).get("result") == "MINIMAL_PREFIX_COMPLETE_C4_REQUIRED", "minimal work profile incomplete"
        assert report["refinement"]["angular_resistance_certificates"]["ticks"]["result"] == "DEFERRED_TO_C4_FULL_OBSERVATION"
    elif inv["case"] != "phase20-original-candidate":
        assert report.get("attribution", {}).get("result") == "CLOSED_FOR_OBSERVED_CASE", "P1 attribution incomplete"
        assert report.get("refinement", {}).get("result") == "PASS_BOUNDED_REFINEMENT_WORK", "refinement work/coverage incomplete"
        if inv["case"] == "L120":
            parity = report.get("prefix_parity", {})
            assert parity.get("result") == "CLOSED_BY_C4_FULL_OBSERVATION_AND_EXACT_PHYSICAL_PARITY", "C4 prefix obligation remains open"
            assert parity["physical_state_samples"] == 1800
            reference(parity["prefix_capture"])
            prefix = reference(parity["prefix_analysis"])
            assert prefix["capture_sha256"] == parity["prefix_capture"]["sha256"]
    return {"path": analyses[0].relative_to(ROOT).as_posix(), "sha256": sha(analyses[0])}


def refinement_failure(ref):
    """Bind a concrete Phase21 failure, including compiled FAIL after build PASS."""
    path = safe(ref["path"])
    assert path.is_relative_to(BASE), "failure evidence belongs to another phase"
    failure = reference(ref)
    status = str(failure.get("status", "")) + " " + str(failure.get("result", ""))
    assert "FAIL" in status or "INCOMPLETE" in status or "NOT_READY" in status, "not failed/incomplete evidence"
    if "replacement_failure" in failure:  # Preserved STOP09 handoff.
        nested = safe(failure["replacement_failure"]["path"])
        assert nested != path, "cyclic failure reference"
        return refinement_failure(failure["replacement_failure"])
    capture_path = safe(failure["capture"]) if "capture" in failure else path
    assert capture_path.is_relative_to(BASE)
    if "capture" in failure:
        assert sha(capture_path) == failure["capture_sha256"], "failed capture changed"
    invocation = json.loads(capture_path.with_name("invocation.json").read_text())
    assert invocation.get("refinement_authority_sha256") in REFINEMENT_AUTHORITIES or invocation.get("authority_sha256") == sha(AUTH), "unbound Phase21 failure"
    return {"path":path.relative_to(ROOT).as_posix(), "sha256":ref["sha256"]}


def refinement_candidate(correction_path):
    assert correction_path, "a frozen expanded candidate record is required; no blind retry"
    path = safe(correction_path)
    assert path.is_relative_to(BASE)
    ref = {"path":path.relative_to(ROOT).as_posix(), "sha256":sha(path)}
    candidate = reference(ref)
    assert candidate["status"] == "FROZEN_EXPANDED_CAMPAIGN_CANDIDATE"
    assert candidate["refinement_authority_sha256"] == REFINEMENT_AUTH_SHA
    name = candidate["candidate_id"]
    assert name and len(name) <= 64 and name[0].isalnum() and all(c in "abcdefghijklmnopqrstuvwxyz0123456789-_" for c in name)
    assert candidate["algorithm_unchanged"] and candidate["criteria_unchanged"]
    assert candidate.get("cause", candidate.get("demonstrated_defect")) and candidate["impact_analysis"]
    assert not candidate["repeat_to_pass"] and not candidate["algorithm_redesign"]
    assert set(candidate["changed_files"]) <= REFINEMENT_OWNED
    assert candidate["changed_files"] or candidate.get("execution_correction"), "unchanged-source retry requires a demonstrated execution correction"
    assert candidate["failed_attempts"] and candidate["evidence"]
    for failure in candidate["failed_attempts"]:
        refinement_failure(failure)
    for evidence in candidate["evidence"]:
        assert sha(safe(evidence["path"])) == evidence["sha256"], "candidate cause/impact evidence changed"
    assert set(candidate["candidate_hashes"]) == REFINEMENT_OWNED
    assert all(sha(safe(p)) == h for p,h in candidate["candidate_hashes"].items()), "frozen candidate source changed"
    configurations = candidate["build_configurations"]
    assert configurations == [c for c in BUILD_NAMES if c in configurations], "one ordered build per affected configuration"
    reused_builds = candidate.get("reuse_builds", {})
    assert set(reused_builds) <= set(BUILD_NAMES) and not set(reused_builds) & set(configurations)
    if reused_builds:
        assert candidate.get("execution_correction") and set(candidate["changed_files"]) <= {RUNNER_PATH}
    if candidate.get("engine_reuse"):
        assert set(candidate["engine_reuse"]) <= set(configurations)
        assert set(candidate["changed_files"]) <= {RUNNER_PATH, "RigidBodySimulation/tests/RestingContactDiagnosis.h"}
    return candidate, ref


def refinement_remaining_plan(a, candidate, used, native, builds):
    remaining_slots = [s for s in a["initial_matrix"] if s["slot"] in candidate["affected_slots"]][len(native):]
    remaining_builds = [c for c in candidate["build_configurations"] if not any(inv["configuration"] == c for _,inv,_ in builds)]
    native_reservation = sum(s["native_deadline_seconds"] + s["wrapper_reservation_seconds"] for s in remaining_slots)
    build_reservation = len(remaining_builds) * a["limits"]["build_command_seconds"]
    # Counts are definite requirements. Whole-matrix ceiling sums are planning
    # information, not invented minimum runtimes; each next call reserves its
    # full ceiling and all actual elapsed time remains charged.
    assert used["native_launches"] + len(remaining_slots) <= a["cumulative_native_attempt_cap"], "insufficient remaining required native slots"
    assert used["build_commands"] + len(remaining_builds) <= a["cumulative_build_command_cap"], "insufficient remaining required build slots"
    return {"remaining_slots":[s["slot"] for s in remaining_slots], "remaining_build_configurations":remaining_builds,
        "remaining_full_native_reservations_seconds":native_reservation, "remaining_full_build_reservations_seconds":build_reservation,
        "all_remaining_full_reservations_fit":used["native_seconds"] + native_reservation <= a["limits"]["native_total_seconds"] and
            used["build_seconds"] + build_reservation <= a["limits"]["build_total_seconds"],
        "interpretation":"Forward ceiling estimate; full next-operation reservation is enforced separately. No runtime guarantee or budget reset."}


def refinement_guard(configuration, case=None, correction_path=None):
    a = refinement_authority()
    used = usage()  # Includes every old authority and rejects every unfinished attempt.
    assert all(round(used[k], 3) >= v for k,v in a["usage_floor"].items()), "historical charge floor changed"
    assert used["native_launches"] <= a["cumulative_native_attempt_cap"] and used["build_commands"] <= a["cumulative_build_command_cap"]
    assert used["native_seconds"] <= a["limits"]["native_total_seconds"] and used["build_seconds"] <= a["limits"]["build_total_seconds"]
    candidate, candidate_ref = refinement_candidate(correction_path)
    wave = candidate["candidate_id"]
    slots = a["initial_matrix"]
    selected = candidate["affected_slots"]
    assert selected and selected == [s["slot"] for s in slots if s["slot"] in selected], "affected slots must retain frozen order"
    slots = [s for s in slots if s["slot"] in selected]
    for _, inv, _ in refinement_records("native") + refinement_records("builds"):
        if inv["wave"] == wave:
            assert inv["correction"] == candidate_ref, "candidate identity was already consumed with different frozen bytes"
    builds, native = refinement_records("builds", wave), refinement_records("native", wave)
    # Earlier candidates' FAIL/overrun records remain charged and immutable.
    # Only the current candidate's failed attempt blocks its own continuation;
    # a repair requires a distinct cause-bound candidate record.
    for _, inv, result in builds:
        assert result["status"] == "PASS" and not result.get("charged_overrun"), "current candidate build failed; freeze a justified successor"
    passed = {}
    assert [inv["slot"] for _,inv,_ in native] == selected[:len(native)], "candidate native order changed"
    for path, inv, result in native:
        passed[inv["slot"]] = refinement_analysis_pass(path, inv, result)
        assert not result.get("charged_overrun"), "current candidate overrun; freeze a justified successor"
    reused = candidate.get("unaffected_reuse", {})
    assert set(reused) == {s["slot"] for s in a["initial_matrix"]} - set(selected)
    assert not set(candidate["changed_files"]) & set(a["production_files"]) or len(slots) == 9, "production response changes require all nine affected cases"
    for slot, record in reused.items():
        assert record["relevant_inputs_unchanged"] is True
        assert sha(safe(record["impact_proof"]["path"])) == record["impact_proof"]["sha256"]
        report = reference(record["analysis"])
        assert report["result"] == "PASS_NUMERIC_HUMAN_QUALITATIVE_PENDING"
        capture_path = safe(report["capture"])
        assert capture_path.is_relative_to(BASE) and sha(capture_path) == report["capture_sha256"]
        inv = json.loads(capture_path.with_name("invocation.json").read_text())
        assert inv["slot"] == slot and inv.get("refinement_authority_sha256") in REFINEMENT_AUTHORITIES
        assert refinement_analysis_pass(capture_path.with_name("invocation.json"), inv, json.loads(capture_path.read_text())) == record["analysis"]
        # A source-response change cannot be hidden by omitting it from changed_files.
        previous = json.loads(safe(inv["build"]).with_name("inputs.json").read_text())
        assert all(sha(safe(p)) == previous["workspace_hashes"][p] for p in a["production_files"])
        identities = record["relevant_input_hashes"]
        assert identities and all(sha(safe(p)) == h for p,h in identities.items())
        assert set(a["production_files"]) <= set(identities)
        assert all(sha(p) == h for p,h in previous["external_toolchain_dependency_hashes"].items())
        passed[slot] = record["analysis"]
    if configuration == "Release":
        assert "C1" in passed, "Debug functional/P1 gates must close before Release build/work"
    a["remaining_mandatory_plan"] = refinement_remaining_plan(a, candidate, used, native, builds)
    if case is None:
        assert configuration in candidate["build_configurations"]
        assert not any(inv["configuration"] == configuration for _,inv,_ in builds), "candidate build configuration already consumed"
        assert used["build_commands"] < a["cumulative_build_command_cap"]
        assert used["build_seconds"] + a["limits"]["build_command_seconds"] <= a["limits"]["build_total_seconds"], "full next build reservation unavailable"
        return a, used, wave, candidate_ref, None
    assert len(native) < len(slots), "all selected native slots already consumed"
    slot = slots[len(native)]
    assert (configuration,case) == (slot["configuration"],slot["case"]), "frozen nine-slot order; no repeat or substitute"
    assert used["native_launches"] < a["cumulative_native_attempt_cap"]
    assert used["native_seconds"] + slot["native_deadline_seconds"] + 5 <= a["limits"]["native_total_seconds"], "full native plus wrapper reservation unavailable"
    return a, used, wave, candidate_ref, slot


def refinement_build(configuration, correction_path=None):
    a, used, wave, correction, _ = refinement_guard(configuration, correction_path=correction_path)
    limit = a["limits"]["build_command_seconds"]
    start = time.monotonic()
    machine, _ = refinement_machine()  # before consuming a build slot
    frozen = inputs()
    candidate = reference(correction)
    engine = refinement_engine_reuse(candidate, configuration, frozen) if configuration in candidate.get("engine_reuse", {}) else None
    # An explicit support-only candidate may reuse the exact frozen engine.
    historical = ROOT / "logs/pre_editor/phase-20/comprehensive-remediation-01/builds" / BUILD_NAMES[configuration] / "result.json"
    old = json.loads(historical.read_text())
    glad = {p: h for p, h in old["products"].items() if p.endswith("/glad.lib")}
    assert old["status"] == "PASS" and len(glad) == 1 and all(sha(safe(p)) == h for p, h in glad.items())
    folder = fresh("builds", configuration + "-refinement-" + wave)
    product = "Phase21Refinement/" + folder.name
    props = folder / "isolated.props"
    defines = "GE_ENABLE_PHYSICS_PROFILING;" if configuration == "Release" else ""
    glad_path = next(iter(glad))
    engine_reference = '<ProjectReference Remove="..\\GEngine\\GEngine.vcxproj"/>' if engine else ""
    link_inputs = [ROOT / glad_path] + ([ROOT / engine["library"]] if engine else [])
    write(props, '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        '<PropertyGroup>'
        f'<OutDir>{ROOT.as_posix()}/bin/$(Configuration)/{product}/$(MSBuildProjectName)/</OutDir>'
        f'<IntDir>{ROOT.as_posix()}/bin-int/$(Configuration)/{product}/$(MSBuildProjectName)/</IntDir>'
        '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>\n'
        '<ItemGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\'">'
        '<ProjectReference Remove="..\\external\\glad\\glad.vcxproj"/>' + engine_reference + '</ItemGroup>\n'
        '<ItemDefinitionGroup><ClCompile>'
        f'<PreprocessorDefinitions>{defines}%(PreprocessorDefinitions)</PreprocessorDefinitions>'
        '</ClCompile></ItemDefinitionGroup>\n'
        '<ItemDefinitionGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\'"><Link>'
        '<AdditionalDependencies>' + ';'.join(escape(p.as_posix()) for p in link_inputs) + ';%(AdditionalDependencies)</AdditionalDependencies>'
        '</Link></ItemDefinitionGroup></Project>\n')
    frozen["generated_props"] = {props.relative_to(ROOT).as_posix(): sha(props)}
    write(folder / "inputs.json", frozen)
    write(folder / "runner.py", Path(__file__).read_bytes())
    for p in REFINEMENT_OWNED - {"tools/pre_editor_phase21_qualification.py"}:
        write(folder / "source" / p, safe(p).read_bytes())
    command = [str(MSBUILD), str(ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"), "/t:Build",
        f"/p:Configuration={configuration}", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
        "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:BuildProjectReferences=true",
        "/p:ForceImportBeforeCppTargets=" + str(props), "/nr:false", "/m:2", "/nologo", "/v:normal",
        "/bl:" + str(folder / "build.binlog")]
    write(folder / "command.json", command)
    write(folder / "invocation.json", {"started_utc": now(), "configuration": configuration,
        "refinement_authority_sha256": REFINEMENT_AUTH_SHA, "wave": wave, "candidate_id":wave, "correction": correction,
        "limit_seconds": limit, "command": command, "usage_before": used, "machine_before": machine,
        "remaining_mandatory_plan":a["remaining_mandatory_plan"],
        "engine_build": "fresh RBS; exact matching engine dependency reused" if engine else "affected GEngine/RBS; inherited MT/MTd and existing Release profiler",
        "engine_reuse": engine, "reused_glad": glad})
    errors, products, runtime = [], {}, {}
    execution = None
    try:
        with (folder / "build.log").open("wb") as log:
            remaining = limit - (time.monotonic() - start)
            assert remaining > 10, "build wrapper exhausted charged allowance"
            execution = process_tools.remediation_owned_build(command, environment(), log, remaining)
        assert execution["exit_code"] == 0 and not execution["timed_out"] and not execution["error"], execution
        current = inputs()
        assert all(current[k] == frozen[k] for k in current), "source changed during build"
        if engine:
            assert all(not safe(ROOT / f"{base}/{configuration}/{product}/GEngine").exists() for base in ("bin", "bin-int")), "unexpected fresh engine build"
            products.update(engine["products"])
        for name, extension in ((("RigidBodySimulation", "exe"),) if engine else (("GEngine", "lib"), ("RigidBodySimulation", "exe"))):
            p = safe(ROOT / f"bin/{configuration}/{product}/{name}/{name}.{extension}")
            products[p.relative_to(ROOT).as_posix()] = sha(p)
        for project, names in (("GEngine", ("gepch", "Manifold", "ConstraintPenetration", "PhysicsProfile", "PhysicsSystem", "GJK")),
                               ("RigidBodySimulation", ("main", "RbsFrame", "RbsScene"))):
            if engine and project == "GEngine":
                continue
            for name in names:
                p = safe(ROOT / f"bin-int/{configuration}/{product}/{project}/{name}.obj")
                products[p.relative_to(ROOT).as_posix()] = sha(p)
        outdir = safe(ROOT / f"bin/{configuration}/{product}/RigidBodySimulation")
        for source in (ROOT / f"bin/{configuration}/RigidBodySimulation").glob("*.dll"):
            target = safe(outdir / source.name)
            assert not target.exists(), "fresh build output dependency already exists"
            shutil.copyfile(source, target)
            runtime[source.relative_to(ROOT).as_posix()] = sha(source)
            products[target.relative_to(ROOT).as_posix()] = sha(target)
        assert runtime, "missing canonical runtime dependencies"
        write(folder / "runtime-inputs.json", runtime)
        reference(correction)
        refinement_authority()
    except BaseException as error:
        errors.append(repr(error))
    engine_stable = None
    if engine:
        try:
            engine_stable = all(sha(safe(p)) == h for p, h in engine["products"].items())
            assert engine_stable, "reused engine/PDB changed during build"
        except BaseException as error:
            engine_stable = False
            errors.append(repr(error))
    elapsed = time.monotonic() - start
    if elapsed > limit:
        errors.append("full charged build allowance exceeded; no further work")
    result = {"status": "PASS" if not errors else "FAIL", "configuration": configuration, "completed_utc": now(),
        "wall_seconds": elapsed, "charged_overrun": elapsed > limit, "limit_seconds":limit, "execution": execution, "errors": errors,
        "products": products, "reused_glad": glad, "inputs_sha256": sha(folder / "inputs.json"),
        "props_sha256": sha(props), "command_sha256": sha(folder / "command.json"), "product_directory": product,
        "engine_reuse": engine, "reused_engine_unchanged_after_build": engine_stable}
    write(folder / "result.json", result)
    print(json.dumps({"result": str(folder / "result.json"), "status": result["status"], "errors": errors,
                      "wall_seconds": elapsed, "prelaunch_proof_required": True}), flush=True)
    if errors:
        raise SystemExit(1)


def refinement_prelaunch(proof_path, build_path, configuration):
    """Consume actual compiled evidence; never manufacture proof from source sizes."""
    proof_path, build_path = safe(proof_path), safe(build_path)
    proof = json.loads(proof_path.read_text())
    assert proof["status"] == "PASS_COUPLED_REFINEMENT_PRELAUNCH"
    assert proof["refinement_authority_sha256"] == REFINEMENT_AUTH_SHA
    assert proof["configuration"] == configuration and proof["build_result"]["sha256"] == sha(build_path)
    assert safe(proof["build_result"]["path"]) == build_path
    assert reference(proof["build_result"])["status"] == "PASS"
    for group in ("artifacts", "evidence", "source_hashes"):
        assert proof[group] and all(sha(safe(p)) == h for p, h in proof[group].items()), group + " drift"
    assert set(proof["source_hashes"]) == REFINEMENT_OWNED
    stack = reference(proof["stack_proof"])
    assert stack["status"] == "PASS_COMPILED_ADDITIONAL_CAPACITY" and stack["gate_bytes"] == 4096
    assert 0 <= stack["added_capacity_upper_bytes"] <= 4096
    assert 0 < stack["graph_scratch_bytes"] <= 3072 and 0 <= stack["collector_counter_bytes"] <= 64
    assert set(stack["paths"]) >= {"disjoint_query", "admission", "solve", "common_ancestor"}
    assert stack["graph_returns_before_solve"] and stack["compiled_objects_bound"]
    storage = reference(proof["storage_proof"])
    assert storage["status"] == "PASS_COMPILED_STORAGE_BOUND_AND_SOURCE_ALLOCATION_REVIEW"
    assert storage["baseline_constraint_bytes"] > 0
    assert 0 <= storage["added_constraint_bytes"] <= 8
    assert storage["retained_added_bytes_upper"] == 4 * storage["manifold_vector_capacity_upper"] * storage["added_constraint_bytes"]
    assert storage["capacity_basis"] and storage["native_actual_capacity_still_required"] is True
    for key, ceiling in (("named_bytes_upper", 64*1024*1024), ("observer_bytes", 8*1024*1024),
        ("incremental_pair_attribution_bytes", 2*1024*1024), ("query_bytes", 4*1024*1024), ("post_audit_bytes", 1024*1024)):
        assert 0 <= storage[key] <= ceiling, key
    assert storage["no_new_heap_allocations"] and storage["no_global_or_tls_cache"]
    assert proof["toolchain_crt_options_unchanged"] and proof["control02_toolchain_options_comparable"]
    if configuration == "Release":
        profile = reference(proof["profiling_proof"])
        assert profile["status"] == "PASS_COMPILED_EXISTING_PROFILER_ENABLED"
        assert profile["build_result_sha256"] == sha(build_path)
        for group in ("artifacts", "evidence"):
            assert profile[group] and all(sha(safe(p)) == h for p, h in profile[group].items())
    return {"path": proof_path.relative_to(ROOT).as_posix(), "sha256": sha(proof_path)}


def refinement_reuse_input_delta(current, expected, prebuilt, *, allowed_changes=None):
    """Whole-build reuse stays runner-only; the engine branch can also rebuild RCD."""
    allowed_changes = {RUNNER_PATH} if allowed_changes is None else set(allowed_changes)
    assert allowed_changes in ({RUNNER_PATH}, {RUNNER_PATH, "RigidBodySimulation/tests/RestingContactDiagnosis.h"})
    assert set(expected) == set(current) | {"generated_props"}, "build input schema changed"
    assert all(current[k] == expected[k] for k in current if k != "workspace_hashes"), "non-workspace build inputs changed"
    old, new = expected["workspace_hashes"], current["workspace_hashes"]
    assert set(old) <= set(new), "build input removed"
    added = set(new) - set(old)
    assert added <= set(prebuilt), "unclassified build input added"
    assert all(new[p] == h for p, h in prebuilt.items()), "prebuilt input differs from accepted identity"
    changed = {p for p in old if old[p] != new[p]}
    assert changed <= allowed_changes, "compiled/workload input changed"
    return {"changed_inputs": sorted(changed), "serialized_prebuilt_inputs": sorted(added)}


def refinement_engine_reuse(candidate, configuration, current):
    reuse = candidate["engine_reuse"][configuration]
    allowed = {RUNNER_PATH, "RigidBodySimulation/tests/RestingContactDiagnosis.h"}
    assert set(candidate["changed_files"]) <= allowed and configuration in candidate["build_configurations"]
    build_path = safe(reuse["build_result"]["path"])
    assert build_path.is_relative_to(BASE / "builds")
    built = reference(reuse["build_result"])
    assert built["status"] == "PASS" and not built.get("charged_overrun") and built["configuration"] == configuration
    invocation = json.loads(build_path.with_name("invocation.json").read_text())
    assert invocation["refinement_authority_sha256"] == REFINEMENT_AUTH_SHA
    previous = reference(invocation["correction"])
    assert previous["candidate_id"] == invocation["wave"] and previous["refinement_authority_sha256"] == REFINEMENT_AUTH_SHA
    for name, key in (("inputs.json", "inputs_sha256"), ("isolated.props", "props_sha256"), ("command.json", "command_sha256")):
        assert sha(build_path.with_name(name)) == built[key]
    expected = json.loads(build_path.with_name("inputs.json").read_text())
    prebuilt = refinement_prebuilt_inputs()
    supplement = reference(reuse["dependency_input_supplement"])
    assert supplement["status"] == "PASS_UNCHANGED_PREBUILT_LINK_INPUT_IDENTITY_REUSE_BEFORE_NATIVE" and supplement["inputs"] == prebuilt
    assert safe(supplement["standing_prior_identity"]["path"]) == LINK_INPUT_PROOF
    assert supplement["standing_prior_identity"]["sha256"] == LINK_INPUT_PROOF_SHA
    delta = refinement_reuse_input_delta(current, expected, prebuilt, allowed_changes=allowed)
    impact = reference(reuse["impact_proof"])
    assert impact["status"] == "PASS_RBS_SUPPORT_ONLY_ENGINE_REUSE_IMPACT"
    assert set(impact["changed_files"]) == set(candidate["changed_files"]) == set(delta["changed_inputs"])
    assert impact["source_before_hashes"] == {p: expected["workspace_hashes"][p] for p in delta["changed_inputs"]}
    assert impact["source_after_hashes"] == {p: current["workspace_hashes"][p] for p in delta["changed_inputs"]}
    assert impact["engine_inputs_unchanged"] is True and impact["rcd_not_consumed_by_engine"] is True and impact["criteria_unchanged"] is True
    assert impact["build_result"] == reuse["build_result"]
    product = built["product_directory"]
    library = f"bin/{configuration}/{product}/GEngine/GEngine.lib"
    names = ("gepch", "Manifold", "ConstraintPenetration", "PhysicsProfile", "PhysicsSystem", "GJK")
    paths = [library] + [f"bin-int/{configuration}/{product}/GEngine/{name}.obj" for name in names]
    products = {p: built["products"][p] for p in paths}
    pdb = safe(reuse["engine_pdb"]["path"])
    assert pdb == safe(library).with_suffix(".pdb") and sha(pdb) == reuse["engine_pdb"]["sha256"]
    products[pdb.relative_to(ROOT).as_posix()] = reuse["engine_pdb"]["sha256"]
    assert all(sha(safe(p)) == h for p, h in products.items()), "reused engine product drift"
    return {"status": "REUSE_MATCHING_ENGINE_COMPILE_RBS", "references": reuse, "library": library,
            "products": products, **delta, "fresh_executable_binding_required": True}


def refinement_reused_build(candidate, configuration, build_path, build_invocation, expected, current):
    reuse = candidate.get("reuse_builds", {}).get(configuration)
    assert reuse, "different candidate build requires explicit matching-build reuse"
    assert candidate.get("execution_correction") and set(candidate["changed_files"]) <= {RUNNER_PATH}
    assert configuration not in candidate["build_configurations"]
    assert safe(reuse["build_result"]["path"]) == build_path and reuse["build_result"]["sha256"] == sha(build_path)
    assert reference(reuse["build_result"])["status"] == "PASS"
    previous_candidate = reference(build_invocation["correction"])
    assert previous_candidate["candidate_id"] == build_invocation["wave"]
    assert previous_candidate["refinement_authority_sha256"] == REFINEMENT_AUTH_SHA
    old_proof = reference(reuse["prelaunch_proof"])
    assert old_proof["status"] == "PASS_COUPLED_REFINEMENT_PRELAUNCH"
    assert old_proof["refinement_authority_sha256"] == REFINEMENT_AUTH_SHA and old_proof["configuration"] == configuration
    assert safe(old_proof["build_result"]["path"]) == build_path and old_proof["build_result"]["sha256"] == sha(build_path)
    assert set(old_proof["source_hashes"]) == REFINEMENT_OWNED
    assert old_proof["source_hashes"][RUNNER_PATH] == expected["workspace_hashes"][RUNNER_PATH]
    assert all(sha(safe(p)) == h for p, h in old_proof["source_hashes"].items() if p != RUNNER_PATH)
    for group in ("artifacts", "evidence"):
        assert old_proof[group] and all(sha(safe(p)) == h for p, h in old_proof[group].items()), "reused compiled evidence changed"
    prebuilt = refinement_prebuilt_inputs()
    supplement = reference(reuse["dependency_input_supplement"])
    assert supplement["status"] == "PASS_UNCHANGED_PREBUILT_LINK_INPUT_IDENTITY_REUSE_BEFORE_NATIVE"
    assert supplement["inputs"] == prebuilt
    assert safe(supplement["standing_prior_identity"]["path"]) == LINK_INPUT_PROOF
    assert supplement["standing_prior_identity"]["sha256"] == LINK_INPUT_PROOF_SHA
    delta = refinement_reuse_input_delta(current, expected, prebuilt)
    impact = reference(reuse["impact_proof"])
    assert impact["status"] == "PASS_RUNNER_ONLY_EXECUTION_REPAIR_BUILD_REUSE_IMPACT"
    assert set(impact["changed_files"]) == set(candidate["changed_files"]) == set(delta["changed_inputs"])
    assert impact["runner_before_sha256"] == expected["workspace_hashes"][RUNNER_PATH]
    assert impact["runner_after_sha256"] == current["workspace_hashes"][RUNNER_PATH]
    assert impact["compiled_inputs_unchanged"] is True and impact["criteria_unchanged"] is True
    assert impact["build_result"] == reuse["build_result"]
    return {"status": "REUSED_MATCHING_BUILD_AND_COMPILED_EVIDENCE", "references": reuse, **delta,
            "original_candidate": build_invocation["correction"],
            "compiled_proof_references": {k: old_proof[k] for k in ("stack_proof", "storage_proof", "profiling_proof") if k in old_proof},
            "scope": "Runner-only execution correction; no rebuild, product rewrite or historical evidence refresh."}


def refinement_asset_inventory(asset_root, directories=None):
    asset_root = safe(asset_root)
    if not asset_root.exists():
        return {}
    assert asset_root.is_dir(), "asset root is not a directory"
    files = {}
    pending = [asset_root]
    while pending:
        for path in sorted(pending.pop().iterdir()):
            path = safe(path)  # Reject reparse entries before traversing them.
            if path.is_dir():
                if directories is not None:
                    directories.add(path.relative_to(asset_root).as_posix())
                pending.append(path)
                continue
            assert path.is_file(), "unsupported asset entry"
            files[path.relative_to(asset_root).as_posix()] = sha(path)
    return files


def refinement_stage_assets(configuration, exe, current, folder):
    """Create only the authenticated package selected explicitly by diagnostic main.cpp."""
    started = time.monotonic()
    canonical = safe(ROOT / f"bin/{configuration}/assets")
    destination = safe(exe.parent.parent / "assets")
    assert exe.name == "RigidBodySimulation.exe" and exe.parent.name == "RigidBodySimulation"
    assert destination.is_relative_to(ROOT / f"bin/{configuration}/Phase21Refinement") and destination != canonical
    prefix = canonical.relative_to(ROOT).as_posix() + "/"
    expected = {p[len(prefix):]: h for p, h in current["workspace_hashes"].items() if p.startswith(prefix)}
    assert expected and "startup/RigidBodySimulation.txt" in expected
    assert all(not Path(p).is_absolute() and ".." not in Path(p).parts for p in expected)
    # InitializeResources resolves Models even though the diagnostic forbids
    # optional mesh imports. Its directory metadata is the only extra input.
    required_dirs = {"Models"} | {parent.as_posix() for p in expected for parent in Path(p).parents if parent != Path(".")}
    assert len(required_dirs) == 16 and "Images" in required_dirs
    source_dirs = {p: safe(canonical / p).is_dir() for p in sorted(required_dirs)}
    sources = {p: sha(safe(canonical / p)) for p in expected}
    existed = destination.exists()
    previous_dirs = set()
    previous = refinement_asset_inventory(destination, previous_dirs)
    before = folder / "asset-staging-before.json"
    write(before, {"recorded_utc": now(), "source_root": str(canonical), "destination_root": str(destination),
                   "destination_existed": existed, "destination_files_before": previous,
                   "destination_directories_before": sorted(previous_dirs), "required_directories": sorted(required_dirs),
                   "source_directory_presence": source_dirs,
                   "expected_hashes": expected, "source_hashes_before": sources,
                   "policy": "Record prior absence/contents before create-only staging. No existing file is overwritten."})
    assert sources == expected, "canonical asset input drift"
    assert all(source_dirs.values()), "required canonical asset directory missing"
    assert previous_dirs <= required_dirs, "unbound directories in isolated asset package"
    assert set(previous) <= set(expected), "unbound files in isolated asset package"
    assert all(h == expected[p] for p, h in previous.items()), "existing isolated asset differs; preserve and stop"
    lines = (canonical / "startup/RigidBodySimulation.txt").read_text().splitlines()
    assert lines[0] == "GENGINE_STARTUP_ASSETS_V1 RigidBodySimulation" and len(lines) > 1
    assert all(p and p in expected for p in lines[1:]), "startup dependency outside bound package"
    for relative in sorted(required_dirs):
        target = safe(destination / relative)
        target.mkdir(parents=True, exist_ok=True)
        assert safe(target).is_dir()
    for relative, digest in expected.items():
        target = safe(destination / relative)
        if target.exists():
            assert target.is_file() and sha(target) == digest
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        safe(target)
        with safe(canonical / relative).open("rb") as source, target.open("xb") as output:
            shutil.copyfileobj(source, output, 1024*1024)
        assert sha(target) == digest, "staged asset differs from bound source"
    actual_dirs = set()
    assert refinement_asset_inventory(destination, actual_dirs) == expected and actual_dirs == required_dirs
    result = {"status": "PASS_AUTHENTICATED_ISOLATED_ASSET_PACKAGE", "source_root": str(canonical),
              "destination_root": str(destination), "files": expected, "created_files": len(expected) - len(previous),
              "directories": sorted(actual_dirs), "source_directory_presence": source_dirs,
              "created_directories": sorted(actual_dirs - previous_dirs),
              "reused_files": len(previous), "bytes": sum((canonical / p).stat().st_size for p in expected),
              "before": {"path": before.relative_to(ROOT).as_posix(), "sha256": sha(before)},
              "wall_seconds": time.monotonic() - started, "charged_inside_native_wrapper": True}
    after = folder / "asset-staging.json"
    write(after, result)
    return {"path": after.relative_to(ROOT).as_posix(), "sha256": sha(after)}


def refinement_run(configuration, case, build_path, proof_path, correction_path=None):
    a, used, wave, correction, slot = refinement_guard(configuration, case, correction_path)
    build_path = safe(build_path)
    built = json.loads(build_path.read_text())
    build_invocation = json.loads(build_path.with_name("invocation.json").read_text())
    assert built["status"] == "PASS" and built["configuration"] == configuration
    assert build_invocation["refinement_authority_sha256"] == REFINEMENT_AUTH_SHA
    for name, key in (("inputs.json", "inputs_sha256"), ("isolated.props", "props_sha256"), ("command.json", "command_sha256")):
        assert sha(build_path.with_name(name)) == built[key]
    expected = json.loads(build_path.with_name("inputs.json").read_text())
    current = inputs()
    candidate = reference(correction)
    build_reuse = None
    if build_invocation["wave"] == wave:
        assert build_invocation["correction"] == correction
        assert all(current[k] == expected[k] for k in current), "build inputs changed"
    else:
        build_reuse = refinement_reused_build(candidate, configuration, build_path, build_invocation, expected, current)
    assert all(sha(safe(p)) == h for p, h in {**built["products"], **built["reused_glad"]}.items())
    proof = refinement_prelaunch(proof_path, build_path, configuration)
    if build_reuse:
        current_proof = reference(proof)
        assert all(current_proof[k] == v for k, v in build_reuse["compiled_proof_references"].items()), "reused compiled subproof changed"
    machine, control = refinement_machine()
    probe = subprocess.run(["powershell", "-NoProfile", "-Command", "@(Get-Process RigidBodySimulation -ErrorAction SilentlyContinue).Count"], capture_output=True, text=True, check=True)
    assert int(probe.stdout.strip()) == 0, "existing RBS process; do not interfere"
    folder = fresh("native", configuration + "-refinement-" + wave + "-" + case)
    data = safe(folder / "data")
    data.mkdir()
    cwd = safe(ROOT / f"runtime/RigidBodySimulation/{configuration}")
    cwd.mkdir(parents=True, exist_ok=True)
    original = case == "phase20-original-candidate"
    request = folder / ("focused-request.txt" if original else "phase21-request.txt")
    request_text = ("P20_POST_CANDIDATE_FOCUSED_V1\ncandidate\n" + process_tools.FOCUSED_LATTICE_HASH + "\n" +
        process_tools.REPLAY_INPUT_HASH + "\n25 50 16 12 4 11 4980 68 120\nEND\n") if original else "P21_COUPLED_REFINEMENT_01 " + case + " END\n"
    write(request, request_text)
    exe = next(ROOT / p for p in built["products"] if p.endswith(".exe"))
    command = [str(exe), "--resting-contact-diagnostic", "off" if case == "prefix" else "on", "--lattice",
               str(process_tools.FOCUSED_LATTICE), "--output", str(data)]
    env = environment()
    env[process_tools.FOCUSED_ENV if original else "GENGINE_P21_REQUEST"] = str(request)
    env["GENGINE_ASSET_ROOT"] = str(ROOT / f"bin/{configuration}/assets")
    runtime_before = {}
    for name in ("imgui.ini", "GEngine.log"):
        p = cwd / name
        if p.exists():
            runtime_before[name] = sha(safe(p))
            shutil.copyfile(p, folder / ("before-" + name))
    limit = slot["native_deadline_seconds"]
    write(folder / "invocation.json", {"started_utc": now(), "case": case, "configuration": configuration,
        "slot": slot["slot"], "wave": wave, "candidate_id":wave, "correction": correction, "refinement_authority_sha256": REFINEMENT_AUTH_SHA,
        "command": command, "cwd": str(cwd), "limit_seconds": limit, "wrapper_allowance_seconds": 5,
        "build": build_path.relative_to(ROOT).as_posix(), "build_sha256": sha(build_path), "prelaunch_proof": proof,
        "build_reuse": build_reuse,
        "request_sha256": sha(request), "runtime_before": runtime_before, "machine": machine,
        "usage_before": used, "remaining_mandatory_plan":a["remaining_mandatory_plan"],
        "original_protocol_wall_token":120 if original else None, "automatic_retry": False})
    start = time.monotonic()
    execution, native = None, None
    staged_assets = None
    errors, evidence = [], {}
    environment_record = {"machine_before": machine}
    try:
        staged_assets = refinement_stage_assets(configuration, exe, current, folder)
        with (folder / "console.log").open("wb") as log:
            execution = process_tools.remediation_owned_build(command, env, log, limit,
                _factory=lambda audit: NativeOwner(audit, cwd, control["environment"]["native_process"] if original else None))
        assert execution["exit_code"] == 0 and not execution["timed_out"] and not execution["error"], execution
        owner = execution["ownership"]
        assert owner["quiescent_before_handle_close"] and owner["job_active_processes"] == 0 and owner["assigned_before_resume"]
        native = json.loads((data / "result.json").read_text())
        assert native["complete"] and native["shutdown"]
        if original:
            expected_counts = {"episodes": 11, "ticks": 4980, "collision_calls": 50, "direct_gjk_calls": 16,
                "ca_helper_calls": 12, "projection_helper_calls": 4, "profiled_ticks": 4980,
                "post_ticks": 1680, "post_stage_records": 288, "physical_failures": 0}
            assert all(native.get(k) == v for k, v in expected_counts.items())
            assert native["candidate"] and native["conditional"] and native["post_candidate_policy"]
            assert not native["overflow"] and not native["failure"] and native["physics_world_ns"] > 0
            assert 0 < native["post_event_records"] <= 1024 and 0 < native["post_storage_bytes"] <= 1024*1024
            assert native["post_sliding"] > 0
        else:
            assert native["ticks"] == slot["completed_ticks"]
            assert native["observer_bytes"] <= 8*1024*1024 and native["observer_bytes"] + native["session_bytes"] <= 64*1024*1024
            capacity = json.loads((data / "pair-capacity.json").read_text())
            assert capacity["incremental_bytes"] <= 2*1024*1024 and capacity["trace_record_bytes"] == 192 and capacity["event_record_bytes"] == 64
            assert sum((data / n).stat().st_size for n in ("pair-trace.bin", "pair-events.bin", "pair-capacity.json", "anchor-examples.csv", "energy-examples.csv") if (data / n).exists()) <= 8*1024*1024
        if configuration == "Release":
            assert native["profiling_enabled"]
    except BaseException as error:
        errors.append(repr(error))
    # Full wrapper/evidence work is charged. The +5 reservation never extends
    # the held native process deadline, including its existing cleanup reserve.
    try:
        after = process_tools.remediation_machine()
        environment_record.update(machine_after=after,
            stable_machine_identity=process_tools.remediation_machine_identity(machine) == process_tools.remediation_machine_identity(after),
            native_process=(execution or {}).get("ownership", {}).get("native_identity"))
        assert environment_record["stable_machine_identity"], "machine changed"
        if original:
            assert environment_record["native_process"] == control["environment"]["native_process"]
        for name in ("imgui.ini", "GEngine.log"):
            if (cwd / name).exists():
                shutil.copyfile(cwd / name, folder / ("after-" + name))
        assert sha(request) == json.loads((folder / "invocation.json").read_text())["request_sha256"]
        assert inputs() == current, "inputs changed during native execution"
        if staged_assets:
            assets = reference(staged_assets)
            asset_dirs = set()
            assert refinement_asset_inventory(safe(assets["destination_root"]), asset_dirs) == assets["files"], "isolated asset package changed"
            assert sorted(asset_dirs) == assets["directories"], "isolated asset directories changed"
            assert all(safe(Path(assets["source_root"]) / p).is_dir() for p in assets["directories"]), "canonical asset directory changed"
        assert all(sha(safe(p)) == h for p, h in {**built["products"], **built["reused_glad"]}.items())
        reference(correction)
        refinement_authority()
        exported = sum(p.stat().st_size for p in data.iterdir() if p.is_file())
        assert exported <= (8 if original else 64)*1024*1024
        evidence = {p.name: sha(p) for p in data.iterdir() if p.is_file()}
    except BaseException as error:
        errors.append(repr(error))
    elapsed = time.monotonic() - start
    if elapsed > limit + 5:
        errors.append("charged native plus wrapper reservation exceeded; stop further work")
    result = {"status": "CAPTURE_COMPLETE_ANALYSIS_PENDING" if not errors else "FAIL_OR_INCOMPLETE",
        "completed_utc": now(), "wall_seconds": elapsed, "charged_overrun": elapsed > limit + 5,
        "execution": execution, "native": native, "launch_started": bool(execution and execution["ownership"].get("pid")),
        "errors": errors, "evidence": evidence, "environment": environment_record,
        "staged_assets": staged_assets,
        "qualification_claim": "NONE until required offline numerical/performance/coverage analysis"}
    write(folder / "result.json", result)
    print(json.dumps({"result": str(folder / "result.json"), "status": result["status"], "errors": errors, "wall_seconds": elapsed}), flush=True)
    if errors:
        raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="operation", required=True)
    b = sub.add_parser("build")
    b.add_argument("configuration", choices=BUILD_NAMES)
    b.add_argument("--long-tail", action="store_true")
    b.add_argument("--l60-causal", action="store_true")
    n = sub.add_parser("run")
    n.add_argument("configuration", choices=BUILD_NAMES)
    n.add_argument("case")
    n.add_argument("--build", required=True)
    q = sub.add_parser("analyze")
    q.add_argument("result")
    d = sub.add_parser("analyze-penetration")
    d.add_argument("result")
    sub.add_parser("check-penetration")
    sub.add_parser("check-pairs")
    sub.add_parser("check-long-tail")
    sub.add_parser("preflight")
    rb = sub.add_parser("refinement-build")
    rb.add_argument("configuration", choices=BUILD_NAMES)
    rb.add_argument("--candidate-record", "--correction-record", dest="correction_record", required=True)
    rn = sub.add_parser("refinement-run")
    rn.add_argument("configuration", choices=BUILD_NAMES)
    rn.add_argument("case")
    rn.add_argument("--build", required=True)
    rn.add_argument("--prelaunch-proof", required=True)
    rn.add_argument("--candidate-record", "--correction-record", dest="correction_record", required=True)
    sub.add_parser("refinement-preflight")
    args = parser.parse_args()
    if args.operation == "refinement-build":
        refinement_build(args.configuration, args.correction_record)
    elif args.operation == "refinement-run":
        refinement_run(args.configuration, args.case, args.build, args.prelaunch_proof, args.correction_record)
    elif args.operation == "refinement-preflight":
        a = refinement_authority()
        machine, _ = refinement_machine()
        closure = inputs()
        print(json.dumps({"status": "PASS_READ_ONLY_SOURCE_MACHINE_PREFLIGHT_NOT_COMPILED_OR_NATIVE_QUALIFICATION",
            "usage": usage(), "limits": a["limits"], "workspace_inputs": len(closure["workspace_hashes"]),
            "external_inputs": len(closure["external_toolchain_dependency_hashes"]),
            "control02_machine_comparable": True, "machine": machine}, indent=2))
    elif args.operation == "build":
        build(args.configuration, args.long_tail, args.l60_causal)
    elif args.operation == "run":
        run(args.configuration, args.case, args.build)
    elif args.operation == "analyze":
        analyze(args.result)
    elif args.operation == "analyze-penetration":
        analyze_penetration(args.result)
    elif args.operation == "check-penetration":
        authority()
        print(json.dumps(penetration_selfcheck()))
    elif args.operation == "check-pairs":
        authority()
        print(json.dumps(pair_selfcheck()))
    elif args.operation == "check-long-tail":
        authority()
        print(json.dumps(long_tail_selfcheck()))
    else:
        assert not REFINEMENT_AUTH.exists(), "use refinement-preflight; historical GEngine library reuse is invalid"
        authority()
        closure = inputs()
        print(json.dumps({"status": "PASS", "workspace_inputs": len(closure["workspace_hashes"]),
                          "external_inputs": len(closure["external_toolchain_dependency_hashes"]), "usage": usage(),
                          "libraries": {c: libraries(c) for c in BUILD_NAMES}}, indent=2))


if __name__ == "__main__":
    main()
