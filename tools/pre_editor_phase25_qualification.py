"""Bounded Phase25 OD-07 qualification. No approval or publication operations.

The immutable accepted proposal is the policy. All attempts share ledger.json;
build/native failures are terminal until a separately recorded mechanical cause
and correction are supplied. Performance captures are never automatically retried.
"""
import argparse
import csv
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import time
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

import pre_editor_resting_contact_diagnosis as process_tools
from pre_editor_phase20_comprehensive import NativeOwner

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "logs/pre_editor/phase-25/game-acceptance-01"
HISTORY = ROOT / "logs/pre_editor/phase-25/revision03-01"
AUTH = ROOT / "docs/pre_editor/active/PHASE_25_GAME_ORIENTED_ACCEPTANCE_01.json"
PRODUCT_ROOT = "Phase25Revision03"
MSBUILD = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe")
VC = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207")
SUPPORT = ["RigidBodySimulation/tests/PhysicsCostChecks.h", "tools/pre_editor_phase25_qualification.py"]
NAMESPACE = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
ORDER = ["C-M2", "C-M3", "C-P3", "V-A", "I1"]


def now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def safe(path):
    path = Path(path)
    if not path.is_absolute():
        path = ROOT / path
    path = Path(os.path.abspath(path))
    assert path.is_relative_to(ROOT), f"output escapes worktree: {path}"
    for ancestor in [path, *path.parents]:
        if ancestor.exists():
            assert not ancestor.is_symlink()
            assert not (ancestor.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT), str(ancestor)
        if ancestor == ROOT:
            break
    return path


def write(path, value, *, replace=False):
    path = safe(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path == BASE / "ledger.json" and replace:
        events = safe(BASE / "ledger-events"); events.mkdir(exist_ok=True)
        previous = sorted(events.glob("*.json"))
        event = {"previous_sha256": sha(previous[-1]) if previous else sha(path), "ledger": value}
        with (events / f"{len(previous)+1:05}.json").open("x", encoding="utf-8") as stream:
            json.dump(event, stream, indent=2)
    with path.open("w" if replace else "x", encoding="utf-8", newline="\n") as stream:
        stream.write(value if isinstance(value, str) else json.dumps(value, indent=2) + "\n")


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def git(*args):
    return subprocess.check_output(["git", "-c", "safe.directory=" + ROOT.as_posix(), *args],
                                   cwd=ROOT, env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"})


def authority():
    a = read(AUTH)
    assert a["status"] == "OWNER_ACCEPTED_CONTINUE_EXISTING_EXECUTION"
    assert sha(ROOT / a["proposal"]["path"]) == a["proposal"]["sha256"]
    assert git("rev-parse", "HEAD").decode().strip() == a["baseline"]
    assert git("branch", "--show-current").decode().strip() == "pre-editor/refactor"
    assert not git("diff", "--cached", "--name-only").strip()
    assert set(git("diff", "--name-only").decode().splitlines()) <= set(a["production"] + a["supporting"])
    index = Path(git("rev-parse", "--git-path", "index").decode().strip())
    assert sha(index) == a["index_sha256"]
    canonical = "RigidBodySimulation/src/RigidBodySimulation.cpp"
    assert sha(ROOT / canonical) == a["source_before"][canonical]
    assert not (ROOT / "pre-editor-checkpoints/phase-25.json").exists()
    return a


def environment(configuration=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("GENGINE_", "PRE_EDITOR_"))}
    for name in ("CL", "_CL_", "LINK", "_LINK_"):
        env.pop(name, None)
    env["TEMP"] = env["TMP"] = str(safe(ROOT / "runtime/tmp"))
    Path(env["TEMP"]).mkdir(parents=True, exist_ok=True)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    if configuration:
        env["GENGINE_ASSET_ROOT"] = str(safe(ROOT / f"bin/{configuration}/assets"))
    return env


def fresh(kind, name):
    parent = safe(BASE / kind)
    parent.mkdir(parents=True, exist_ok=True)
    for i in range(1, 1000):
        folder = parent / f"{name}-{i:02}"
        if not folder.exists():
            folder.mkdir()
            return folder
    raise RuntimeError("evidence destination exhausted")


def account_analysis(ledger, pause=False):
    segment = ledger.get("analysis_active_segment")
    if segment and segment.get("started_utc"):
        end = now()
        elapsed = (dt.datetime.fromisoformat(end) - dt.datetime.fromisoformat(segment["started_utc"])).total_seconds()
        ledger["analysis_seconds"] += elapsed
        ledger.setdefault("analysis_segments", []).append({**segment, "completed_utc": end, "wall_seconds": elapsed})
    ledger["analysis_active_segment"] = None if pause else {"started_utc": now(), "purpose": "Bounded source/evidence diagnosis and qualification preparation"}
    write(BASE / "ledger.json", ledger, replace=True)
    assert ledger["analysis_seconds"] <= ledger["limits"]["analysis_seconds"], "cumulative offline-analysis bound"


def use(kind, folder, limit):
    ledger = read(BASE / "ledger.json")
    if kind in ("build", "native"):
        account_analysis(ledger, pause=True)
    assert not any(x.get("status") == "RUNNING" for x in ledger["attempts"]), "unfinished attempt"
    if ledger["automation_started_utc"] is None:
        ledger["automation_started_utc"] = now()
    elapsed = ledger["prior_combined_seconds"] + (dt.datetime.now(dt.timezone.utc) - dt.datetime.fromisoformat(ledger["automation_started_utc"])).total_seconds()
    elapsed -= sum(wait["seconds"] for wait in ledger.get("owner_review_waits", []))
    assert elapsed < ledger["limits"]["automation_seconds"]
    category = kind + "_seconds"
    aggregate = "analysis_seconds" if kind == "analysis" else kind + "_total_seconds"
    remaining = ledger["limits"][aggregate] - ledger[category]
    assert remaining > 0 and limit > 0
    if kind == "native":
        assert ledger["launches"] < ledger["limits"]["launches"]
    allowed = min(limit, remaining, ledger["limits"]["automation_seconds"] - elapsed)
    attempt = {"kind": kind, "path": folder.relative_to(ROOT).as_posix(), "started_utc": now(),
               "status": "RUNNING", "limit_seconds": allowed}
    ledger["attempts"].append(attempt)
    write(BASE / "ledger.json", ledger, replace=True)
    return allowed


def finish(kind, folder, elapsed, result):
    ledger = read(BASE / "ledger.json")
    entry = ledger["attempts"][-1]
    assert entry["path"] == folder.relative_to(ROOT).as_posix() and entry["status"] == "RUNNING"
    entry.update(status=result["status"], completed_utc=now(), wall_seconds=elapsed)
    ledger[kind + "_seconds"] += elapsed
    ledger["raw_native_bytes"] = ledger["prior_raw_native_bytes"] + sum(p.stat().st_size for p in (BASE / "native").glob("*/data/*") if p.is_file())
    if kind == "native" and result.get("execution", {}).get("ownership", {}).get("pid"):
        assert result["execution"]["ownership"].get("launch_counted_at_creation"), "creation must be charged before execution"
        if result.get("w64_diagnostic"):
            ledger["w64_diagnostic"]["diagnostic_launches"] += 1
        elif result.get("diagnostic"):
            ledger["memory_remediation"]["diagnostic_launches"] += 1
    write(folder / "result.json", result)
    entry["result_sha256"] = sha(folder / "result.json")
    ledger["analysis_active_segment"] = {"started_utc": now(), "purpose": "Post-attempt diagnosis/evidence review"}
    write(BASE / "ledger.json", ledger, replace=True)
    print(json.dumps({"path": str(folder / "result.json"), "status": result["status"],
                      "seconds": elapsed, "errors": result.get("errors", [])}), flush=True)


def source_inputs():
    a = authority()
    paths = set(a["production"] + SUPPORT + [a["proposal"]["path"],
        "RigidBodySimulation/src/RigidBodySimulation.cpp", "tools/pre_editor_resting_contact_lattice.txt",
        "tools/pre_editor_resting_contact_diagnosis.py", "tools/pre_editor_phase20_comprehensive.py",
        "docs/pre_editor/phases/PHASE_25.md", "docs/pre_editor/VALIDATION_CONTRACTS.md",
        "docs/pre_editor/active/OUTPUT_PATHS.json",
        AUTH.relative_to(ROOT).as_posix(),
        "docs/pre_editor/active/PHASE_25_OD07_DECISION_PROPOSAL_01.md",
        "docs/pre_editor/proposals/PHASE_25_OD07_REVISION_03/OD07_REVISION_03_DECISION_PROPOSAL.md",
        "docs/pre_editor/active/PHASE_25_OD07_REVISION_03_ACCEPTANCE.json"])
    for name in ("GEngine", "RigidBodySimulation"):
        project = ROOT / name / (name + ".vcxproj")
        paths.add(project.relative_to(ROOT).as_posix())
        tree = ET.parse(project)
        for node in tree.findall(".//m:ClCompile", NAMESPACE) + tree.findall(".//m:ClInclude", NAMESPACE):
            if "Include" in node.attrib:
                p = (project.parent / node.attrib["Include"]).resolve()
                assert p.is_relative_to(ROOT)
                paths.add(p.relative_to(ROOT).as_posix())
    # Resolve declared native link dependencies in the same search order.
    project = ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"
    tree = ET.parse(project)
    dirs = []
    for node in tree.findall(".//m:AdditionalLibraryDirectories", NAMESPACE):
        for name in (node.text or "").split(";"):
            if name and not name.startswith("%"):
                p = (project.parent / name).resolve()
                if p not in dirs: dirs.append(p)
    for node in tree.findall(".//m:AdditionalDependencies", NAMESPACE):
        for name in (node.text or "").split(";"):
            if not name or name.startswith("%") or not name.endswith(".lib"): continue
            matches = [d / name for d in dirs if (d / name).is_file()]
            assert matches, "unresolved declared native library: " + name
            paths.add(matches[0].relative_to(ROOT).as_posix())
    return {p: sha(ROOT / p) for p in sorted(paths)}


def strip_overlay(text):
    output = []
    depth = 0
    for line in text.splitlines(keepends=True):
        if not depth and line.strip() == "#ifdef GENGINE_PHASE25_QUALIFICATION":
            depth = 1
            continue
        if depth:
            if re.match(r"\s*#\s*if", line): depth += 1
            if re.match(r"\s*#\s*endif", line): depth -= 1
        else:
            output.append(line)
    assert depth == 0
    return "".join(output)


def static_check():
    a = authority()
    for path in a["production"]:
        before = (BASE / "entry" / path).read_text(encoding="utf-8")
        after = strip_overlay((ROOT / path).read_text(encoding="utf-8"))
        # Ignore only whitespace and comments, never ordinary tokens/directives.
        def tokens(text):
            return re.sub(r"\s+", "", re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S))
        assert tokens(before) == tokens(after), f"ordinary tokens changed: {path}"
    h = (ROOT / SUPPORT[0]).read_text(encoding="utf-8")
    assert "MaxSteps=20000" in h and "Capacity = 262144" in h
    fields = re.findall(r"std::uint64_t\s+(\w+)\{\}", (ROOT / a["production"][0]).read_text(encoding="utf-8").split("struct PhysicsProfileSnapshot", 1)[1].split("void ResetPhysicsProfile", 1)[0])
    folder = fresh("static", "ordinary")
    write(folder / "result.json", {"status": "PASS", "utc": now(), "ordinary_tokens_unchanged": a["production"],
                                    "profile_fields": fields, "inputs": {p: sha(ROOT / p) for p in a["production"] + SUPPORT}})
    print(folder / "result.json")


def build(configuration, mode, target, diagnostic=False, w64_diagnostic=False):
    authority()
    assert configuration in ("Debug", "Release") and mode in ("A", "M", "P", "O")
    assert not (diagnostic and w64_diagnostic)
    if w64_diagnostic:
        assert (configuration, mode, target) == ("Debug", "A", "RigidBodySimulation")
        ledger = read(BASE / "ledger.json")
        assert ledger["w64_diagnostic"]["diagnostic_launches"] == 0
        assert sha(ledger["w64_diagnostic"]["authorization"]) == ledger["w64_diagnostic"]["sha256"]
    if diagnostic:
        assert (configuration, mode, target) == ("Debug", "A", "RigidBodySimulation")
        amendment = read(ROOT / "docs/pre_editor/active/PHASE_25_MEMORY_REMEDIATION_ACCEPTANCE_01.json")
        assert amendment["status"] == "OWNER_AUTHORIZED_BOUNDED_MEMORY_OWNERSHIP_REMEDIATION"
    product_mode = mode + ("-W64" if w64_diagnostic else "-Ownership" if diagnostic else "")
    folder = fresh("builds", f"{configuration}-{product_mode}-{target}")
    out = safe(ROOT / f"bin/{configuration}/{PRODUCT_ROOT}/{product_mode}/{target}")
    intermediate = safe(ROOT / f"bin-int/{configuration}/{PRODUCT_ROOT}/{product_mode}/{target}")
    out.mkdir(parents=True, exist_ok=True); intermediate.mkdir(parents=True, exist_ok=True)
    frozen = source_inputs()
    for p in authority()["production"] + SUPPORT:
        destination = folder / "source" / p
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / p, destination)
    defines = "" if mode == "O" else "GENGINE_PHASE25_ISOLATED_BUILD;GENGINE_PHASE25_QUALIFICATION;"
    if mode in ("P", "A"): defines += "GE_ENABLE_PHYSICS_PROFILING;"
    if mode == "A": defines += "GENGINE_PHASE25_ALLOCATIONS;"
    if diagnostic: defines += "GENGINE_PHASE25_ALLOCATION_DIAGNOSTIC;"
    if w64_diagnostic: defines += "GENGINE_PHASE25_W64_DIAGNOSTIC;"
    undefines = "GENGINE_RENDER_BASELINE;GENGINE_PHASE24_QUALIFICATION;GENGINE_PHASE24_OUTLINE_QUALIFICATION;GENGINE_PHASE24_REPLACEMENT_MEASUREMENT;GENGINE_PHASE24_COLLIDER_VALIDATION"
    if mode in ("M", "O"): undefines += ";GE_ENABLE_PHYSICS_PROFILING"
    if mode == "O": undefines += ";GENGINE_PHASE25_QUALIFICATION;GENGINE_PHASE25_ISOLATED_BUILD;GENGINE_PHASE25_ALLOCATIONS"
    libs = []
    if target == "RigidBodySimulation":
        libs = [ROOT / f"bin/{configuration}/{PRODUCT_ROOT}/{mode}/GEngine/GEngine.lib",
                ROOT / f"external/glad/bin/{configuration}/glad/glad.lib"]
        assert all(p.is_file() for p in libs)
        engine_result = sorted((BASE / "builds").glob(f"{configuration}-{mode}-GEngine-*/result.json"))[-1]
        engine = read(engine_result)
        assert engine["status"] == "PASS" and all(sha(ROOT / p) == h for p,h in engine["products"].items())
        assert all(sha(p) == h for p,h in read(engine_result.with_name("actual-includes.json")).items()), "engine include drift"
    props = folder / "isolated.props"
    write(props, '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
          '<PropertyGroup>' + f'<OutDir>{out.as_posix()}/</OutDir><IntDir>{intermediate.as_posix()}/</IntDir>'
          '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild><TrackFileAccess>true</TrackFileAccess>'
          '</PropertyGroup><ItemGroup><ProjectReference Remove="@(ProjectReference)"/></ItemGroup>'
          '<ItemDefinitionGroup><ClCompile>' + f'<PreprocessorDefinitions>{defines}%(PreprocessorDefinitions)</PreprocessorDefinitions>'
          f'<UndefinePreprocessorDefinitions>{undefines};%(UndefinePreprocessorDefinitions)</UndefinePreprocessorDefinitions>'
          '<ShowIncludes>true</ShowIncludes><DebugInformationFormat>ProgramDatabase</DebugInformationFormat>'
          '</ClCompile><Link><AdditionalDependencies>' + escape(";".join(p.as_posix() for p in libs)) +
          ';%(AdditionalDependencies)</AdditionalDependencies><GenerateMapFile>true</GenerateMapFile>'
          f'<MapFileName>{(out / "RigidBodySimulation.map").as_posix()}</MapFileName>'
          '</Link></ItemDefinitionGroup></Project>\n')
    frozen[props.relative_to(ROOT).as_posix()] = sha(props)
    frozen.update({str(p): sha(p) for p in libs})
    frozen.update({str(p): sha(p) for p in [MSBUILD, VC / "bin/Hostx64/x64/cl.exe", VC / "bin/Hostx64/x64/link.exe"]})
    write(folder / "inputs.json", frozen)
    command = [str(MSBUILD), str(ROOT / target / (target + ".vcxproj")), "/t:Build", f"/p:Configuration={configuration}",
               "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207", "/p:WindowsTargetPlatformVersion=10.0.26100.0",
               "/p:BuildProjectReferences=false", "/p:ForceImportBeforeCppTargets=" + str(props), "/nr:false", "/m:2", "/nologo", "/v:normal"]
    limit = use("build", folder, 1200)
    write(folder / "invocation.json", {"command": command, "limit_seconds": limit, "utc": now(), "configuration": configuration, "mode": mode, "target": target})
    started = time.monotonic(); execution = None; errors = []; products = {}
    try:
        with (folder / "build.log").open("wb") as log:
            execution = process_tools.remediation_owned_build(command, environment(), log, limit)
        assert execution["exit_code"] == 0 and not execution["timed_out"] and not execution["error"], execution
        assert all(sha(Path(p) if Path(p).is_absolute() else ROOT / p) == digest for p, digest in frozen.items()), "input drift"
        product = out / (target + (".lib" if target == "GEngine" else ".exe"))
        products[product.relative_to(ROOT).as_posix()] = sha(product)
        if diagnostic:
            for extension in (".pdb", ".map"):
                symbol = out / (target + extension)
                products[symbol.relative_to(ROOT).as_posix()] = sha(symbol)
        if target == "RigidBodySimulation":
            for source in (ROOT / f"bin/{configuration}/RigidBodySimulation").glob("*.dll"):
                destination = out / source.name
                if destination.exists(): assert sha(destination) == sha(source)
                else: shutil.copyfile(source, destination)
                products[destination.relative_to(ROOT).as_posix()] = sha(destination)
        includes = {}
        for line in (folder / "build.log").read_text(encoding="utf-8", errors="replace").splitlines():
            if "including file:" in line:
                path = Path(line.split("including file:", 1)[1].strip())
                if path.is_file(): includes[str(path)] = sha(path)
        write(folder / "actual-includes.json", includes)
        for source in intermediate.rglob("*.tlog"):
            if not source.is_file():
                continue
            dest = folder / "tlogs" / source.relative_to(intermediate)
            dest.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, dest)
        authority()
    except BaseException as error:
        errors.append(repr(error))
    elapsed = time.monotonic() - started
    result = {"status": "PASS" if not errors and elapsed <= limit else "FAIL", "utc": now(), "configuration": configuration,
              "mode": mode, "target": target, "diagnostic": diagnostic or w64_diagnostic, "w64_diagnostic": w64_diagnostic, "execution": execution, "errors": errors, "products": products,
              "wall_seconds": elapsed, "inputs_sha256": sha(folder / "inputs.json")}
    finish("build", folder, elapsed, result)
    if result["status"] != "PASS": raise SystemExit(1)
    return folder / "result.json"


def run(label, built_path):
    authority()
    frozen_runtime = read(BASE / "input-freeze.json")
    current_configuration = "Debug" if label == "D1" else "Release"
    assert sha(AUTH) == frozen_runtime["authority_sha256"], "authority drift"
    assert read(BASE / "preparation/evidence-reuse-correction01.json")["status"] == "PASS"
    assert all(sha(ROOT / p) == h for p,h in frozen_runtime["runtime_inputs"].items() if f"/{current_configuration}/" in p or p.startswith(f"runtime/RigidBodySimulation/{current_configuration}/")), "runtime input drift"
    assert source_inputs() == frozen_runtime["source_inputs"], "source/membership drift"
    assert all(sha(p) == h for p,h in frozen_runtime["actual_compiled_inputs"].items()), "compiled input drift"
    w64_diagnostic = label == "WD1"
    diagnostic = label in ("MD1", "MD2", "WD1")
    replacement_checks = label == "C-R"
    temporary_peaks = label == "C-T"
    assert diagnostic or replacement_checks or temporary_peaks or label in ORDER and label != "I1", "ordinary integration uses its separate final handoff"
    if w64_diagnostic:
        ledger = read(BASE / "ledger.json")
        assert ledger["w64_diagnostic"]["diagnostic_launches"] == 0
        assert sha(ledger["w64_diagnostic"]["authorization"]) == ledger["w64_diagnostic"]["sha256"]
    if diagnostic and not w64_diagnostic:
        ledger = read(BASE / "ledger.json")
        amendment = read(ROOT / "docs/pre_editor/active/PHASE_25_MEMORY_REMEDIATION_ACCEPTANCE_01.json")
        assert amendment["status"] == "OWNER_AUTHORIZED_BOUNDED_MEMORY_OWNERSHIP_REMEDIATION"
        assert ledger["memory_remediation"]["diagnostic_launches"] < 2
        assert label == "MD" + str(ledger["memory_remediation"]["diagnostic_launches"] + 1)
    completed = []
    for result_path in (BASE / "native").glob("*/result.json"):
        result = read(result_path)
        if result.get("diagnostic"):
            continue
        disposition = result_path.parent / "correction-authority.json"
        if disposition.exists() and read(disposition).get("invalidated_for_qualification"):
            continue
        if result.get("label") in ("C-R", "C-T"):
            assert result["status"] == "CAPTURE_COMPLETE" or (result_path.parent / "correction-authority.json").exists()
            continue
        if result["status"] == "PASS_FUNCTIONAL" or result["status"] == "CAPTURE_COMPLETE": completed.append(result["label"])
        else:
            assert diagnostic or (result_path.parent / "correction-authority.json").exists(), "failed attempt needs demonstrated mechanical correction"
    if replacement_checks or temporary_peaks:
        assert set(completed) == {"D1", "C-A"}, "affected R check must precede CPU calibration"
    elif not diagnostic:
        assert set(completed) == set(ORDER[:ORDER.index(label)]), "run order/retry guard"
    if label == "V-A":
        assert read(BASE / "absolute-admission.json")["status"] == "PASS"
        verify_a1_binding()
    if label != "D1" and not diagnostic:
        assert (BASE / "linked-allocation-coverage-correction01.json").exists(), "linked coverage proof required before C"
        assert read(BASE / "linked-allocation-coverage-correction01.json")["status"] == "PASS"
    built_path = safe(built_path); built = read(built_path)
    assert built["status"] == "PASS" and built["target"] == "RigidBodySimulation"
    configuration = "Debug" if label == "D1" or diagnostic else "Release"
    mode = "A" if label in ("D1", "C-A", "C-R", "C-T", "V-A") or diagnostic else label[2]
    assert built["configuration"] == configuration and built["mode"] == mode
    assert bool(built.get("diagnostic")) == diagnostic, "diagnostic product cannot qualify"
    assert bool(built.get("w64_diagnostic")) == w64_diagnostic, "diagnostic purpose mismatch"
    expected = read(built_path.with_name("inputs.json"))
    assert sha(built_path.with_name("inputs.json")) == built["inputs_sha256"]
    assert all(sha(Path(p) if Path(p).is_absolute() else ROOT / p) == digest for p, digest in expected.items()), "stale build inputs"
    assert all(sha(ROOT / p) == digest for p, digest in built["products"].items())
    no_interference()
    assert power_state() == frozen_runtime["power"], "power state/scheme drift"
    ledger = read(BASE / "ledger.json")
    raw_used = ledger["prior_raw_native_bytes"] + sum(p.stat().st_size for p in (BASE / "native").glob("*/data/*") if p.is_file())
    assert raw_used + 64*1024*1024 <= 512*1024*1024, "raw storage reservation"
    assert ledger["prior_new_evidence_bytes"] + sum(p.stat().st_size for p in BASE.rglob("*") if p.is_file()) < 2*1024**3, "cumulative new evidence storage cap"
    folder = fresh("native", label); data = folder / "data"; data.mkdir()
    request = folder / "request.txt"
    write(request, f"{data}\n{ROOT / 'tools/pre_editor_resting_contact_lattice.txt'}\n{label}\n{mode}\n")
    cwd = safe(ROOT / f"runtime/RigidBodySimulation/{configuration}"); cwd.mkdir(parents=True, exist_ok=True)
    exe = next(ROOT / p for p in built["products"] if p.endswith(".exe"))
    # Immutable launch config uses relative package discovery, not the environment
    # overload. Keep build products in their mode directories; stage matching bytes
    # at this phase-owned execution root so it resolves canonical bin/config/assets.
    execution_products = {}
    for p, digest in built["products"].items():
        source = ROOT / p
        if source.suffix.lower() not in (".exe", ".dll", ".pdb"):
            continue
        destination = safe(ROOT / f"bin/{configuration}/{PRODUCT_ROOT}" / source.name)
        if destination != source and (not destination.exists() or sha(destination) != digest):
            if destination.exists():
                saved = folder / "previous-execution-products" / destination.name
                saved.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(destination, saved)
            shutil.copyfile(source, destination)
        assert sha(destination) == digest
        execution_products[destination.relative_to(ROOT).as_posix()] = digest
        if source.suffix.lower() == ".exe": exe = destination
    write(folder / "execution-products.json", execution_products)
    env = environment(configuration); env["GENGINE_PHASE25_REQUEST"] = str(request)
    for name in ("imgui.ini", "GEngine.log"):
        if (cwd / name).exists(): shutil.copyfile(cwd / name, folder / ("before-" + name))
    target = time.monotonic() + 30 if label in ("C-M2", "V-A") else read(BASE / "schedule.json")["quiescent_monotonic"] + 30
    remaining = target - time.monotonic()
    assert remaining >= 0, "fixed acquisition schedule missed; no favorable rescheduling"
    limit = use("native", folder, 300 if label == "D1" or diagnostic else 600)
    assert limit == (300 if label == "D1" or diagnostic else 600), "full process reservation unavailable"
    remaining = target-time.monotonic(); assert remaining >= 0, "fixed acquisition deadline missed"
    time.sleep(remaining)
    assert target <= time.monotonic() <= target+1, "fixed acquisition request deadline"
    write(folder / "invocation.json", {"label": label, "command": [str(exe)], "cwd": str(cwd), "limit_seconds": limit,
                                     "build": str(built_path), "build_sha256": sha(built_path), "utc": now()})
    started = time.monotonic(); execution = None; errors = []; fixture = None
    try:
        with (folder / "console.log").open("wb") as log:
            execution = process_tools.remediation_owned_build([str(exe)], env, log, limit,
                _factory=lambda audit: Revision03Owner(audit, cwd, target))
        assert execution["exit_code"] == 0 and not execution["timed_out"] and not execution["error"], execution
        fixture = read(data / "fixture.json")
        if w64_diagnostic:
            assert (data / "w64-states.csv").is_file() and (data / "w64-events.csv").is_file()
            assert read(data / "w64-diagnostic.json")["completed_steps"] >= 240
            assert fixture["allocation_errors"] == 0, fixture
        elif diagnostic:
            assert (data / "allocation-stacks.csv").is_file() and (data / "allocation-lifetime.bin").is_file()
            assert fixture["allocation_errors"] == 0, fixture
        else:
            assert fixture["status"] == "COMPLETE", fixture
            assert fixture["rows_completed"] == (12 if replacement_checks else 21) and fixture["allocation_errors"] == 0
            if label == "D1": assert fixture["records"] == 3732
            if label != "D1": assert fixture["records"] == (5400 if replacement_checks else 18960)
        assert sum(p.stat().st_size for p in data.iterdir() if p.is_file()) <= 64*1024**2, "per-process raw storage cap"
        assert all(sha(Path(p) if Path(p).is_absolute() else ROOT / p) == digest for p, digest in expected.items()), "source drift"
        authority()
    except BaseException as error:
        errors.append(repr(error))
        if (data / "fixture.json").exists(): fixture = read(data / "fixture.json")
    for name in ("imgui.ini", "GEngine.log"):
        if (cwd / name).exists(): shutil.copyfile(cwd / name, folder / ("after-" + name))
    if not (folder / "after-GEngine.log").is_file(): errors.append("missing current application log")
    else:
        diagnostics = (folder / "after-GEngine.log").read_text(errors="replace") + (folder / "console.log").read_text(errors="replace")
        if re.search(r"\[(?:error|critical)\]|Runtime asset failure|Application (?:initialization|runtime) (?:failed|error)|Cannot make the owning shutdown context current|GL_INVALID_(?:ENUM|VALUE|OPERATION)|GL_OUT_OF_MEMORY", diagnostics, re.I):
            errors.append("application/GL/shutdown error logged")
    elapsed = time.monotonic() - started
    status = "DIAGNOSTIC_CAPTURE_COMPLETE_GATE_UNCHANGED" if diagnostic else "PASS_FUNCTIONAL" if label == "D1" else "CAPTURE_COMPLETE"
    result = {"status": status if not errors and elapsed <= limit else "FAIL",
              "label": label, "diagnostic": diagnostic, "w64_diagnostic": w64_diagnostic, "fixture": fixture, "execution": execution, "errors": errors, "wall_seconds": elapsed,
              "evidence": {p.relative_to(folder).as_posix(): sha(p) for p in folder.rglob("*") if p.is_file()}}
    finish("native", folder, elapsed, result)
    if errors: raise SystemExit(1)
    return folder


class Revision03Owner(NativeOwner):
    def __init__(self, audit, cwd, target):
        super().__init__(audit, cwd); self.target = target

    def start(self, command, env, log):
        self.audit["create_request_monotonic"] = time.monotonic()
        self.audit["scheduled_target_monotonic"] = self.target
        assert self.target <= self.audit["create_request_monotonic"] <= self.target+1
        try:
            super().start(command, env, log)
        finally:
            if self.audit.get("pid"):
                ledger = read(BASE / "ledger.json"); ledger["launches"] += 1
                write(BASE / "ledger.json", ledger, replace=True)
                self.audit["launch_counted_at_creation"] = True
                self.audit["create_completed_monotonic"] = time.monotonic()
        assert self.audit["create_completed_monotonic"] <= self.target+1, "actual creation missed schedule"

    def poll(self):
        code, active = super().poll()
        if code is not None and active == 0 and "quiescent_monotonic" not in self.audit:
            self.audit["quiescent_monotonic"] = time.monotonic()
            write(BASE / "schedule.json", {"quiescent_monotonic": self.audit["quiescent_monotonic"], "utc": now(), "pid": self.audit.get("pid")}, replace=True)
        return code, active


def no_interference():
    command = "@(Get-Process | Where-Object {$_.ProcessName -match '^(MSBuild|cl|link|RigidBodySimulation|PhysicsTests|PhysicsBenchmark|cmake|ninja)$'}).Count"
    result = subprocess.run(["powershell", "-NoProfile", "-Command", command], capture_output=True, text=True, check=True)
    assert int(result.stdout.strip()) == 0, "active build/native process; no process intervention"


def power_state():
    import ctypes
    class Power(ctypes.Structure):
        _fields_ = [("AC",ctypes.c_ubyte),("battery",ctypes.c_ubyte),("percent",ctypes.c_ubyte),("status",ctypes.c_ubyte),("life",ctypes.c_ulong),("full",ctypes.c_ulong)]
    p = Power(); assert ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(p))
    assert p.AC == 1, "AC supply required"
    scheme = subprocess.check_output(["powercfg", "/getactivescheme"], text=True).strip()
    return {"AC":p.AC,"scheme":scheme}


def freeze_campaign(builds):
    authority(); no_interference()
    runtime = {}
    for configuration in ("Debug", "Release"):
        asset_root = ROOT / f"bin/{configuration}/assets"
        manifest = asset_root / "startup/RigidBodySimulation.txt"
        paths = [manifest] + [asset_root / p for p in manifest.read_text().splitlines()[1:] if p]
        ini = ROOT / f"runtime/RigidBodySimulation/{configuration}/imgui.ini"
        if ini.is_file(): paths.append(ini)
        runtime.update({p.relative_to(ROOT).as_posix():sha(p) for p in paths})
    compiled = {}
    products = {}
    for label,path in builds.items():
        record = read(path); assert record["status"] == "PASS"
        compiled.update(read(Path(path).with_name("actual-includes.json")))
        products.update(record["products"])
    assert all(sha(p) == h for p,h in compiled.items()), "actual include input drift"
    assert all(sha(ROOT / p) == h for p,h in products.items()), "product drift"
    write(BASE / "input-freeze.json", {"utc":now(),"baseline":authority()["baseline"],"source_inputs":source_inputs(),
        "actual_compiled_inputs":compiled,"runtime_inputs":runtime,"products":products,"builds":{k:{"path":str(v),"sha256":sha(v)} for k,v in builds.items()},
        "power":power_state(),"authority_sha256":sha(AUTH),"C_V":"Identical product per mode. Existing3CPU plus missing3CPU; A2 independent allocation verification. No versiondelta claim.",
        "schedule":"M2/A2/I1:30s after finalpreflight; M3/P3:30s after ownedquiescence; request/creation0..1s; disclosed sessionbreak and analysisbarriers",
        "statistics":"Accepted Game Amendment01: perprocess original absolutes, exactM sleep, diagnosticsunder namedexceptions; no IID/population/causaloverhead claim",
        "workloads":"Proposal01 exact21rows18960steps; W64Amendment01 unchanged; fullDebug3732steps"})


def finalize_campaign_record(status, evidence):
    ledger = read(BASE / "ledger.json"); account_analysis(ledger, pause=True)
    ledger = read(BASE / "ledger.json")
    folder = fresh("stops" if status != "PASS" else "completed", "campaign")
    write(folder / "handoff.json", {"utc":now(),"status":status,"evidence":evidence,"ledger_sha256":sha(BASE / "ledger.json"),
        "usage":{k:ledger[k] for k in ("analysis_seconds","build_seconds","native_seconds","launches")},
        "source_hashes":{p:sha(ROOT/p) for p in authority()["production"]+SUPPORT},
        "historical173":"FAILED unchanged","Revision03_97":"FAILED unchanged","Revision02":"inactive","human":"PENDING; no human validation claimed",
        "seal":False,"publication":False,"phase26":False})
    print(json.dumps({"status":status,"handoff":str(folder / "handoff.json")}),flush=True)


def prepare_builds():
    static_check()
    records = {}
    for configuration, mode in (("Debug","A"),("Release","A"),("Release","M"),("Release","P"),("Release","O")):
        for target in ("GEngine","RigidBodySimulation"):
            label = f"{configuration}-{mode}-{target}"
            # Reuse only a successful current-campaign product with exact inputs.
            candidates = sorted((BASE / "builds").glob(label+"-*/result.json"))
            match = None
            if candidates:
                candidate = candidates[-1]; record = read(candidate)
                if record['status'] == 'PASS':
                    inputs = read(candidate.with_name('inputs.json'))
                    if all(sha(Path(p) if Path(p).is_absolute() else ROOT/p)==h for p,h in inputs.items()) and all(sha(ROOT/p)==h for p,h in record['products'].items()):
                        match = candidate
            records[label] = str(match or build(configuration,mode,target))
    write(BASE / "prepared-builds.json", records)
    print(json.dumps({"status":"CURRENT_BUILDS_PREPARED","records":records}),flush=True)


"""Revision03 evaluator fragment for integration into the single admitted runner.

No writer, subprocess, product selection, native launch, or implicit evidence reuse.
Callers freeze these bytes before acquisition and bind returned records to the
exact input/product and current C-A prerequisites. All units are nanoseconds.
"""
import csv as _r3csv
import hashlib as _r3hashlib
import json as _r3json
import math as _r3math
import statistics as _r3stats
from pathlib import Path as _R3Path

R3_STAGES = ('broadphaseTimeNs', 'narrowphaseTimeNs', 'manifoldTimeNs',
             'contactResolutionTimeNs', 'contactIslandBuildTimeNs', 'solverTimeNs')
R3_CONTRASTS = ((2, 1), (3, 2), (4, 2), (5, 4), (6, 2))
R3_STATS = ('mean', 'median', 'p95')
R3_CONTROL_LABELS = ('C-M1', 'C-P1', 'C-P2', 'C-M2', 'C-M3', 'C-P3')
R3_CANDIDATE_LABELS = ('V-P1', 'V-M1', 'V-M2', 'V-P2', 'V-P3', 'V-M3')
R3_REQUIRED_PRECHECKS = ('D1', 'C-A', 'linked_allocation_coverage',
                         'source_product_freeze', 'phase20_resource_impact')
R3_CLAIM = ('Finite-campaign empirical raw+q instrumented costs; aggregate-stage '
            'raw+q is not a physical upper endpoint. No independent-step, '
            'confidence-level, population-tail, or algorithm-regression claim.')


def _r3_read(path):
    return _r3json.loads(_R3Path(path).read_text(encoding='utf-8-sig'))


def _r3_digest(path):
    h = _r3hashlib.sha256()
    with _R3Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576), b''):
            h.update(chunk)
    return h.hexdigest()


def _r3_issue(gate, key, outcome, **details):
    return dict(gate=gate, key=key, outcome=outcome, **details)


def _r3_status(issues, complete=True):
    if any(x['outcome'] == 'FAIL' for x in issues):
        return 'FAIL'
    if issues:
        return 'INCONCLUSIVE'
    return 'PASS' if complete else 'INCOMPLETE'


def _r3_numeric(values):
    assert values and all(isinstance(v, (int, float)) and not isinstance(v, bool)
                          and _r3math.isfinite(v) and v >= 0 for v in values)
    return values


def _r3_summary(values, window=None):
    _r3_numeric(values)
    n = len(values)
    ordered = sorted(values)
    stats = {'mean': sum(values) / n, 'median': ordered[_r3math.ceil(.5*n)-1],
             'p95': ordered[_r3math.ceil(.95*n)-1]}
    detail = {'samples': n, 'minimum': ordered[0], 'maximum': ordered[-1],
              'p99': ordered[_r3math.ceil(.99*n)-1]}
    if window is not None:
        assert n >= window > 0
        running = sum(values[:window])
        peak = running
        for i in range(window, n):
            running += values[i] - values[i-window]
            peak = max(peak, running)
        stats['sustained'] = peak / window
    return stats, detail


def rev03_summarize_capture(folder, profile_fields, reference_folder, stage_proofs):
    """Read one COMPLETE Release M/P capture and the fresh C-A parity reference.

    stage_proofs is frozen source evidence keyed by stage. A proof has a 64-digit
    source_sha256, source_statement, and either always_invoked=True or
    zero_invocation_counters=[profile field names]. The latter must prove that
    ALL timer invocations are absent when all named counters are zero.
    """
    folder, reference_folder = _R3Path(folder), _R3Path(reference_folder)
    fixture = _r3_read(folder/'data/fixture.json')
    reference_fixture = _r3_read(reference_folder/'data/fixture.json')
    result = _r3_read(folder/'result.json')
    mode, label, quantum = fixture['mode'], result['label'], fixture['q_ns']
    assert mode in ('M', 'P') and label[2] == mode
    assert result['status'] == 'CAPTURE_COMPLETE'
    assert fixture['status'] == reference_fixture['status'] == 'COMPLETE'
    assert reference_fixture['mode'] == 'A'
    assert 0 < quantum <= 1000 and quantum == reference_fixture['q_ns']
    assert fixture['rows_completed'] == 21 and fixture['records'] == 18960
    assert fixture['allocation_errors'] == 0
    with (folder/'data/steps.csv').open(newline='') as stream:
        rows = list(_r3csv.DictReader(stream))
    with (reference_folder/'data/steps.csv').open(newline='') as stream:
        reference = list(_r3csv.DictReader(stream))
    with (folder/'data/rows.csv').open(newline='') as stream:
        identities = list(_r3csv.DictReader(stream))
    with (reference_folder/'data/rows.csv').open(newline='') as stream:
        reference_identities = list(_r3csv.DictReader(stream))
    assert len(rows) == len(reference) == 18960
    assert identities == reference_identities and len(identities) == 21
    index = {name: 'profile_'+str(i) for i, name in enumerate(profile_fields)}
    assert all(name in index for name in R3_STAGES)
    semantic = ['row', 'tick', 'measured', 'sleeping', 'bodies', 'islands',
                'command_kind', 'state', 'physical_state', 'penetration']
    if mode == 'P':
        semantic += [index[name] for name in profile_fields if 'TimeNs' not in name]
    mismatches = [i for i, (a, b) in enumerate(zip(rows, reference))
                  if any(a[k] != b[k] for k in semantic)]
    issues = []
    if mismatches:
        issues.append(_r3_issue('state_work_ordinal_parity', label, 'FAIL',
                                count=len(mismatches), first_ordinals=mismatches[:10]))
    common = _r3_read(folder/'data/common-observer.json')
    for key, expected in (('clock_loop_reads', 4096), ('physical_width', 1280),
                          ('physical_height', 720), ('swap_interval', 0),
                          ('manual_frame_limit', 0)):
        if common[key] != expected:
            issues.append(_r3_issue('common_observer', key, 'FAIL',
                                    actual=common[key], required=expected))
    statistics, diagnostics = {}, {}

    def add(row, boundary, values, caps, window=None, absent=False):
        computed, detail = _r3_summary(values, window)
        diagnostics[f'{row}/{boundary}'] = detail
        for statistic, value in computed.items():
            key = f'{row}/{boundary}/{statistic}'
            statistics[key] = dict(value_ns=value, absolute_ns=caps[statistic],
                                   absent_proven=absent)
            if not absent and value > caps[statistic]:
                issues.append(_r3_issue('absolute', key, 'FAIL', actual_ns=value,
                                        ceiling_ns=caps[statistic]))

    offset = 0
    for row in range(21):
        count = 1800 if row == 7 else 2520 if row == 8 else 450 if row >= 9 else 1320
        warm = 60 if row == 7 else 150 if row >= 9 else 120
        allrows = rows[offset:offset+count]
        offset += count
        assert len(allrows) == count
        assert all(int(r['row']) == row and int(r['tick']) == i
                   and int(r['measured']) == int(i >= warm)
                   for i, r in enumerate(allrows)), 'workload/order/window mismatch'
        selected = allrows[warm:]
        step = 1e9 / (120 if row == 7 else 60)
        caps = {'mean': .5*step, 'median': .5*step,
                'p95': .75*step, 'sustained': .75*step}
        values = [int(r['total_ns'])+quantum for r in selected]
        add(row, 'total', values, caps, 120 if row == 7 else 60)
        diagnostics[f'{row}/total'].update(
            dt_ns=step, dt_exceedance_count=sum(v > step for v in values),
            dt_exceedance_time_ns=sum(v-step for v in values if v > step))
        diagnostics[f'{row}/state_check'] = _r3_summary(
            [int(r['state_check_ns']) for r in selected])[0]
        if mode == 'P':
            for stage in R3_STAGES:
                raw = [int(r[index[stage]]) for r in selected]
                absent = False
                if not any(raw):
                    proof = stage_proofs.get(stage, {})
                    digest = proof.get('source_sha256', '')
                    valid = (len(digest) == 64 and all(c in '0123456789abcdef' for c in digest)
                             and bool(proof.get('source_statement')))
                    counters = proof.get('zero_invocation_counters', [])
                    if valid and counters and all(k in index for k in counters):
                        absent = all(int(r[index[k]]) == 0 for r in selected for k in counters)
                    if not absent and not (valid and proof.get('always_invoked') is True):
                        issues.append(_r3_issue('zero_raw_invocation_disposition',
                                                f'{row}/{stage}', 'INCONCLUSIVE'))
                add(row, stage, [v+quantum for v in raw], caps, absent=absent)
                diagnostics[f'{row}/{stage}']['invocation_status'] = (
                    'ABSENT_SOURCE_AND_COUNTER_PROVEN' if absent else 'EMPIRICAL_INSTRUMENTED')
        if row >= 9:
            add(row, 'fit', [int(r['fit_ns'])+quantum for r in selected],
                dict(mean=500000, median=500000, p95=1000000))
            cap = 1000000 if (row-9) % 2 else 500000
            add(row, 'command', [int(r['command_ns'])+quantum for r in selected],
                dict(mean=cap, median=cap, p95=2*cap))
        if row == 8:
            for boundary, cap in (('wake_command', 1000000), ('wake_first', step),
                                  ('wake_second', step), ('wake_sum', 2*step)):
                values = []
                for tick in range(120, 2520, 120):
                    if boundary == 'wake_command':
                        value = int(allrows[tick]['command_ns'])+quantum
                    elif boundary == 'wake_first':
                        value = int(allrows[tick]['total_ns'])+quantum
                    elif boundary == 'wake_second':
                        value = int(allrows[tick+1]['total_ns'])+quantum
                    else:
                        value = int(allrows[tick]['total_ns'])+int(allrows[tick+1]['total_ns'])+2*quantum
                    values.append(value)
                key, value = f'{row}/{boundary}/max', max(values)
                statistics[key] = dict(value_ns=value, absolute_ns=cap, absent_proven=False)
                diagnostics[f'{row}/{boundary}'] = dict(events=values, **_r3_summary(values)[1])
                if value > cap:
                    issues.append(_r3_issue('wake_absolute', key, 'FAIL',
                                            actual_ns=value, ceiling_ns=cap))
    assert offset == 18960
    return dict(schema=3, label=label, mode=mode, q_ns=quantum,
                status=_r3_status(issues), issues=issues, statistics=statistics,
                diagnostics=diagnostics, common_observer=common, claim=R3_CLAIM,
                semantic_columns=semantic, parity_mismatches=len(mismatches),
                steps_sha256=_r3_digest(folder/'data/steps.csv'),
                result_sha256=_r3_digest(folder/'result.json'),
                reference_steps_sha256=_r3_digest(reference_folder/'data/steps.csv'),
                stage_proofs=stage_proofs)


def _r3_group(summaries, prefix, complete):
    assert summaries and all(s['label'].startswith(prefix+'-') for s in summaries)
    labels = [s['label'] for s in summaries]
    expected = R3_CONTROL_LABELS if prefix == 'C' else R3_CANDIDATE_LABELS
    assert len(set(labels)) == len(labels) and all(x in expected for x in labels)
    if complete:
        assert set(labels) == set(expected), 'all six preassigned CPU processes required'
    quantum = summaries[0]['q_ns']
    assert 0 < quantum <= 1000 and all(s['q_ns'] == quantum for s in summaries)
    groups = {mode: sorted([s for s in summaries if s['mode'] == mode],
                           key=lambda s: s['label']) for mode in ('M', 'P')}
    for mode, group in groups.items():
        if group:
            keys = {f'{row}/total/{stat}' for row in range(21)
                    for stat in (*R3_STATS, 'sustained')}
            keys |= {f'{row}/{boundary}/{stat}' for row in range(9, 21)
                     for boundary in ('fit', 'command') for stat in R3_STATS}
            keys |= {f'8/{boundary}/max' for boundary in
                     ('wake_command', 'wake_first', 'wake_second', 'wake_sum')}
            if mode == 'P':
                keys |= {f'{row}/{stage}/{stat}' for row in range(21)
                         for stage in R3_STAGES for stat in R3_STATS}
            assert all(set(s['statistics']) == keys for s in group), 'incomplete statistic membership'
    return quantum, groups


def _r3_cost_budget(values, quantum, absolute):
    _r3_numeric(values)
    low, high = min(values), max(values)
    spread = high-low+2*quantum
    allowance = max(.05*low, 4*quantum)
    budget = low+allowance-spread
    old_e = max(.025*_r3stats.median(values), 4*quantum)
    return dict(values_ns=values, L_ns=low, Uc_ns=spread, D_ns=allowance,
                B_ns=budget, absolute_ns=absolute,
                headroom_pass=spread < allowance,
                absolute_pass=all(x <= absolute for x in values),
                budget_ceiling_pass=budget <= absolute,
                original_2_5_percent_envelope_ns=old_e,
                original_standalone_gate_descriptive_pass=spread <= old_e)


def _r3_vals(group, key):
    return [s['statistics'][key]['value_ns'] for s in group]


def _r3_absent(group, key):
    return all(s['statistics'][key].get('absent_proven', False) for s in group)


def _r3_observer_keys(statistics):
    return [key for key in statistics if key.split('/')[1] in
            ('total', 'fit', 'command', 'wake_command', 'wake_first', 'wake_second', 'wake_sum')]


def rev03_control_admission(summaries, prerequisites):
    """Complete C admission. PASS is impossible without every named prerequisite.

    prerequisite values are identity-bound records with status == PASS, prepared
    by the caller. C-A must include fresh full allocation/state/ordinal admission.
    The returned object is frozen create-only before ANY V acquisition.
    """
    quantum, groups = _r3_group(summaries, 'C', True)
    issues = [item for s in summaries for item in s.get('issues', [])]
    for s in summaries:
        if s['status'] != 'PASS':
            issues.append(_r3_issue('process_admission', s['label'], 'INCONCLUSIVE',
                                    actual_status=s['status']))
    for name in R3_REQUIRED_PRECHECKS:
        if prerequisites.get(name, {}).get('status') != 'PASS':
            issues.append(_r3_issue('prerequisite', name, 'INCONCLUSIVE'))
    costs, scaling, sleep, observer = {}, {}, {}, {}
    for mode, group in groups.items():
        for key, first in group[0]['statistics'].items():
            fullkey = mode+'/'+key
            absences = [s['statistics'][key].get('absent_proven', False) for s in group]
            assert all(s['statistics'][key]['absolute_ns'] == first['absolute_ns'] for s in group)
            if all(absences):
                costs[fullkey] = dict(status='N/A_ABSENT_PROVEN', values_ns=_r3_vals(group, key))
                continue
            if any(absences):
                issues.append(_r3_issue('invocation_parity', fullkey, 'INCONCLUSIVE'))
            record = _r3_cost_budget(_r3_vals(group, key), quantum, first['absolute_ns'])
            costs[fullkey] = record
            if not record['headroom_pass']:
                issues.append(_r3_issue('relative_headroom', fullkey, 'INCONCLUSIVE',
                                        Uc_ns=record['Uc_ns'], required_strictly_below_D_ns=record['D_ns'],
                                        numeric_gate_pass=False, inequality='Uc < D'))
            if not record['absolute_pass']:
                issues.append(_r3_issue('absolute', fullkey, 'FAIL', values_ns=record['values_ns'],
                                        ceiling_ns=record['absolute_ns']))
            if not record['budget_ceiling_pass']:
                issues.append(_r3_issue('unclipped_budget_absolute', fullkey, 'FAIL',
                                        B_ns=record['B_ns'], ceiling_ns=record['absolute_ns']))
        boundaries = ('total',) if mode == 'M' else ('total',)+R3_STAGES
        for numerator, denominator in R3_CONTRASTS:
            for boundary in boundaries:
                for statistic in R3_STATS:
                    nk, dk = f'{numerator}/{boundary}/{statistic}', f'{denominator}/{boundary}/{statistic}'
                    key = f'{mode}/{numerator}:{denominator}/{boundary}/{statistic}'
                    nv, dv = _r3_vals(group, nk), _r3_vals(group, dk)
                    na, da = _r3_absent(group, nk), _r3_absent(group, dk)
                    if na and da:
                        scaling[key] = dict(branch='N/A_ABSENT_PROVEN')
                        continue
                    ratio = not na and not da and all(v > 20*quantum for v in dv)
                    if ratio:
                        values = [a/b for a, b in zip(nv, dv)]
                        envelope = .05*_r3stats.median(values)
                        record = dict(branch='ratio', C_values=values, C_median=_r3stats.median(values),
                                      range_ceiling=envelope, B=max(values)*1.05)
                    else:
                        values = [a-b for a, b in zip(nv, dv)]
                        reference = max(nv+dv)
                        envelope = max(.025*reference, 4*quantum)
                        record = dict(branch='signed_difference', C_values=values,
                                      range_ceiling=envelope, C_maximum_cost_ns=reference,
                                      B=max(values)+max(.05*reference, 4*quantum))
                    record.update(numerator_values_ns=nv, denominator_values_ns=dv,
                                  numerator_absent=na, denominator_absent=da,
                                  C_range=max(values)-min(values))
                    scaling[key] = record
                    if record['C_range'] > envelope:
                        issues.append(_r3_issue('scaling_control_range', key, 'INCONCLUSIVE',
                                                actual=record['C_range'], ceiling=envelope,
                                                numeric_gate_pass=False, inequality='C range <= frozen range ceiling'))
    # The original resolved-savings obligation is for M total; P scaling remains above.
    for statistic in R3_STATS:
        sk, ak = f'6/total/{statistic}', f'2/total/{statistic}'
        sv, av = _r3_vals(groups['M'], sk), _r3_vals(groups['M'], ak)
        us, ua = costs['M/'+sk]['Uc_ns'], costs['M/'+ak]['Uc_ns']
        left, right = max(sv)+us, min(av)-ua
        passed = left <= right if statistic == 'p95' else left < right
        sleep[statistic] = dict(S_ns=sv, A_ns=av, U_S_ns=us, U_A_ns=ua,
                                left_ns=left, right_ns=right, inclusive=statistic == 'p95', passed=passed)
        if not passed:
            issues.append(_r3_issue('resolved_sleep_savings', statistic, 'INCONCLUSIVE',
                                    left_ns=left, right_ns=right, inclusive=statistic == 'p95',
                                    numeric_gate_pass=False,
                                    inequality='max(S)+U_S <= min(A)-U_A' if statistic == 'p95' else 'max(S)+U_S < min(A)-U_A'))
    for key in _r3_observer_keys(groups['M'][0]['statistics']):
        mv, pv = _r3_vals(groups['M'], key), _r3_vals(groups['P'], key)
        signed = _r3stats.median(pv)-_r3stats.median(mv)
        uncertainty = max(pv)-min(pv)+max(mv)-min(mv)+4*quantum
        ceiling = max(.1*_r3stats.median(mv), 5000)
        observer[key] = dict(M_ns=mv, P_ns=pv, ordered_pairs_ns=list(zip(mv, pv)),
                              O_ns=signed, J_ns=uncertainty, ceiling_ns=ceiling,
                              actual_ns=abs(signed)+uncertainty)
        if abs(signed)+uncertainty > ceiling:
            issues.append(_r3_issue('observer', key, 'INCONCLUSIVE',
                                    actual_ns=abs(signed)+uncertainty, ceiling_ns=ceiling,
                                    numeric_gate_pass=False, inequality='abs(O)+J <= frozen observer ceiling'))
    return dict(schema=3, phase='25', status=_r3_status(issues), q_ns=quantum,
                claim=R3_CLAIM, issues=issues, cost_budgets=costs, scaling=scaling,
                sleep=sleep, observer=observer, prerequisites=prerequisites,
                captures=[dict(label=s['label'], steps_sha256=s.get('steps_sha256'),
                               result_sha256=s.get('result_sha256')) for s in summaries])


def rev03_candidate_admission(summaries, frozen):
    assert frozen['status'] == 'PASS', 'candidate admission requires frozen successful C admission'
    quantum, groups = _r3_group(summaries, 'V', True)
    assert quantum == frozen['q_ns']
    issues = [item for s in summaries for item in s.get('issues', [])]
    costs, scaling, sleep, observer = {}, {}, {}, {}
    for s in summaries:
        if s['status'] != 'PASS':
            issues.append(_r3_issue('process_admission', s['label'], 'INCONCLUSIVE', actual_status=s['status']))
    for mode, group in groups.items():
        for key, first in group[0]['statistics'].items():
            fullkey = mode+'/'+key
            control = frozen['cost_budgets'][fullkey]
            values = _r3_vals(group, key)
            if control.get('status') == 'N/A_ABSENT_PROVEN':
                absent = _r3_absent(group, key)
                costs[fullkey] = dict(status='N/A_ABSENT_PROVEN' if absent else 'FAIL')
                if not absent:
                    issues.append(_r3_issue('invocation_parity', fullkey, 'FAIL'))
                continue
            if any(s['statistics'][key].get('absent_proven', False) for s in group):
                issues.append(_r3_issue('invocation_parity', fullkey, 'FAIL'))
            uv = max(values)-min(values)+2*quantum
            actual, budget = max(values)+uv, control['B_ns']
            relative = actual <= budget
            sensitivity = control['Uc_ns']+uv <= control['D_ns']
            record = dict(values_ns=values, Uv_ns=uv, actual_ns=actual, B_ns=budget,
                          relative_pass=relative, sensitivity_resolved=sensitivity,
                          original_frozen_envelope_ns=control['original_2_5_percent_envelope_ns'],
                          original_v_range_descriptive_pass=uv <= control['original_2_5_percent_envelope_ns'])
            costs[fullkey] = record
            if not relative:
                issues.append(_r3_issue('relative_numeric', fullkey, 'FAIL', actual_ns=actual, B_ns=budget))
                if not sensitivity:
                    issues.append(_r3_issue('relative_sensitivity', fullkey, 'INCONCLUSIVE',
                                            Uc_ns=control['Uc_ns'], Uv_ns=uv, D_ns=control['D_ns']))
            assert first['absolute_ns'] == control['absolute_ns']
            if any(v > control['absolute_ns'] for v in values):
                issues.append(_r3_issue('absolute', fullkey, 'FAIL', values_ns=values,
                                        ceiling_ns=control['absolute_ns']))
    for key, control in frozen['scaling'].items():
        mode, contrast, boundary, statistic = key.split('/')
        n, d = contrast.split(':')
        group = groups[mode]
        nk, dk = f'{n}/{boundary}/{statistic}', f'{d}/{boundary}/{statistic}'
        if control['branch'] == 'N/A_ABSENT_PROVEN':
            if not (_r3_absent(group, nk) and _r3_absent(group, dk)):
                issues.append(_r3_issue('scaling_invocation_parity', key, 'FAIL'))
            scaling[key] = dict(branch=control['branch'])
            continue
        nv, dv = _r3_vals(group, nk), _r3_vals(group, dk)
        if control['branch'] == 'ratio':
            assert all(v > 0 for v in dv)
            values = [a/b for a, b in zip(nv, dv)]
        else:
            values = [a-b for a, b in zip(nv, dv)]
        spread = max(values)-min(values)
        scaling[key] = dict(branch=control['branch'], V_values=values, B=control['B'], V_range=spread)
        if max(values) > control['B']:
            issues.append(_r3_issue('scaling_candidate_ceiling', key, 'FAIL',
                                    actual=max(values), ceiling=control['B']))
        if control['branch'] == 'ratio' and spread > control['range_ceiling']:
            issues.append(_r3_issue('scaling_candidate_range', key, 'FAIL',
                                    actual=spread, ceiling=control['range_ceiling']))
    for statistic, control in frozen['sleep'].items():
        sk, ak = f'6/total/{statistic}', f'2/total/{statistic}'
        sv, av = _r3_vals(groups['M'], sk), _r3_vals(groups['M'], ak)
        us = max(control['U_S_ns'], max(sv)-min(sv)+2*quantum)
        ua = max(control['U_A_ns'], max(av)-min(av)+2*quantum)
        left, right = max(sv)+us, min(av)-ua
        passed = left <= right if statistic == 'p95' else left < right
        sleep[statistic] = dict(left_ns=left, right_ns=right, U_S_ns=us, U_A_ns=ua, passed=passed)
        if not passed:
            issues.append(_r3_issue('resolved_sleep_savings', statistic, 'INCONCLUSIVE',
                                    left_ns=left, right_ns=right, inclusive=statistic == 'p95',
                                    numeric_gate_pass=False,
                                    inequality='max(S)+max(Uc_S,Uv_S) <= min(A)-max(Uc_A,Uv_A)' if statistic == 'p95' else 'max(S)+max(Uc_S,Uv_S) < min(A)-max(Uc_A,Uv_A)'))
    for key, control in frozen['observer'].items():
        mv, pv = _r3_vals(groups['M'], key), _r3_vals(groups['P'], key)
        signed = _r3stats.median(pv)-_r3stats.median(mv)
        uncertainty = max(pv)-min(pv)+max(mv)-min(mv)+4*quantum
        actual = abs(signed)+uncertainty
        observer[key] = dict(M_ns=mv, P_ns=pv, ordered_pairs_ns=list(zip(mv, pv)),
                              O_ns=signed, J_ns=uncertainty, actual_ns=actual,
                              ceiling_ns=control['ceiling_ns'])
        if actual > control['ceiling_ns']:
            issues.append(_r3_issue('observer', key, 'INCONCLUSIVE', actual_ns=actual,
                                    ceiling_ns=control['ceiling_ns'], numeric_gate_pass=False,
                                    inequality='abs(O)+J <= frozen observer ceiling'))
    return dict(schema=3, status=_r3_status(issues), q_ns=quantum, claim=R3_CLAIM,
                issues=issues, costs=costs, scaling=scaling, sleep=sleep, observer=observer,
                remaining_mandatory_gates=['fresh V-A exact allocation comparison', 'ordinary I1'])


def rev03_partial_control_stop(summaries):
    """Only irreversible numeric proofs; never a partial PASS or permission for V.

    For observed controls a=min,b=max, every completion has L<=a and Uc>=b-a+2q.
    D(L)<=D(a). Therefore U_lower>=D_upper proves Uc<D is impossible.
    B>absolute is NOT safely decisive on partial controls and is not tested here.
    Ratio/difference branch, sleep, and observer are completed at the C barrier.
    """
    quantum, groups = _r3_group(summaries, 'C', False)
    decisive = [item for s in summaries for item in s.get('issues', [])
                if item['outcome'] == 'FAIL']
    for mode, group in groups.items():
        if not group:
            continue
        for key, first in group[0]['statistics'].items():
            if _r3_absent(group, key):
                continue
            values = _r3_vals(group, key)
            if any(v > first['absolute_ns'] for v in values):
                decisive.append(_r3_issue('absolute', mode+'/'+key, 'FAIL', values_ns=values,
                                          ceiling_ns=first['absolute_ns']))
            lower = max(values)-min(values)+2*quantum
            upper = max(.05*min(values), 4*quantum)
            if lower >= upper:
                decisive.append(_r3_issue('headroom_mathematically_impossible', mode+'/'+key,
                                          'INCONCLUSIVE', observed_values_ns=values,
                                          Uc_lower_ns=lower, D_upper_ns=upper,
                                          numeric_gate_pass=False, inequality='Uc < D impossible: Uc_lower >= D_upper',
                                          remaining_original_controls=3-len(group)))
    return dict(status='STOP_DECISIVE' if decisive else 'NO_DECISIVE_PARTIAL_FAILURE',
                issues=decisive, permits_V=False, claim=R3_CLAIM,
                proof='For all possible remaining controls: L<=observed min, Uc>=observed range+2q, D<=max(.05*observed min,4q). Equality blocks the strict Uc<D admission.')


def rev03_analyzer_selftest():
    """Deterministic synthetic boundary tests; no application or historical writer."""
    import copy
    checks = []
    def verify(name, condition):
        assert condition, name
        checks.append(name)
    b = _r3_cost_budget([10000, 10100, 10200], 10, 1000000)
    verify('exact L/Uc/D/B formula', (b['L_ns'], b['Uc_ns'], b['D_ns'], b['B_ns']) == (10000, 220, 500, 10280))
    verify('strict headroom equality fails', not _r3_cost_budget([10000, 10480, 10000], 10, 1000000)['headroom_pass'])
    verify('unclipped absolute budget rejected', not _r3_cost_budget([1000, 1000, 1000], 1, 1040)['budget_ceiling_pass'])
    summary, detail = _r3_summary([1, 100, 2, 3], 2)
    verify('nearest-rank and ordered rolling window', summary == dict(mean=26.5, median=2, p95=100, sustained=51))
    verify('descriptive p99 retained', detail['p99'] == 100)
    def make(label):
        mode = label[2]
        statistics = {}
        for row in range(21):
            value = 5000 if row == 6 else 10000
            caps = dict(mean=1000000, median=1000000, p95=1500000, sustained=1500000)
            for stat in (*R3_STATS, 'sustained'):
                statistics[f'{row}/total/{stat}'] = dict(value_ns=value, absolute_ns=caps[stat], absent_proven=False)
            if mode == 'P':
                for stage in R3_STAGES:
                    for stat in R3_STATS:
                        statistics[f'{row}/{stage}/{stat}'] = dict(value_ns=value, absolute_ns=caps[stat], absent_proven=False)
            if row >= 9:
                for boundary in ('fit', 'command'):
                    for stat in R3_STATS:
                        statistics[f'{row}/{boundary}/{stat}'] = dict(value_ns=10000, absolute_ns=1000000, absent_proven=False)
        for boundary in ('wake_command', 'wake_first', 'wake_second', 'wake_sum'):
            statistics[f'8/{boundary}/max'] = dict(value_ns=10000, absolute_ns=1000000, absent_proven=False)
        return dict(label=label, mode=mode, q_ns=10, status='PASS', issues=[], statistics=statistics)
    controls = [make(label) for label in R3_CONTROL_LABELS]
    prereqs = {name: dict(status='PASS', identity='synthetic-only') for name in R3_REQUIRED_PRECHECKS}
    frozen = rev03_control_admission(controls, prereqs)
    verify('complete synthetic C admission', frozen['status'] == 'PASS')
    verify('all required scaling including Solver pass sensitivity',
           'P/5:4/solverTimeNs/p95' in frozen['scaling'] and len(frozen['scaling']) == 120)
    verify('all original per-process statistic cardinalities',
           len(controls[0]['statistics']) == 160 and len(controls[1]['statistics']) == 538)
    candidates = [make(label) for label in R3_CANDIDATE_LABELS]
    valid = rev03_candidate_admission(candidates, frozen)
    verify('held-out synthetic candidate accepted', valid['status'] == 'PASS')
    equality = copy.deepcopy(candidates)
    for s in equality:
        if s['mode'] == 'M':
            s['statistics']['0/total/mean']['value_ns'] = frozen['cost_budgets']['M/0/total/mean']['B_ns']-20
    eq = rev03_candidate_admission(equality, frozen)
    verify('candidate relative equality accepted', eq['costs']['M/0/total/mean']['relative_pass'])
    equality[1]['statistics']['0/total/mean']['value_ns'] += 1
    eq = rev03_candidate_admission(equality, frozen)
    verify('one bad candidate never pooled away', any(x['gate'] == 'relative_numeric' for x in eq['issues']))
    partial = copy.deepcopy(controls[:3])
    partial[2]['statistics']['0/total/mean']['value_ns'] = 10480
    stopped = rev03_partial_control_stop(partial)
    verify('partial equality is decisively impossible', stopped['status'] == 'STOP_DECISIVE')
    verify('partial controls never authorize V', not stopped['permits_V'])
    verify('missing C-A prerequisite blocks', rev03_control_admission(controls, {})['status'] == 'INCONCLUSIVE')
    missing = copy.deepcopy(candidates)
    for s in missing:
        s['statistics'].pop('0/total/mean')
    try:
        rev03_candidate_admission(missing, frozen)
    except AssertionError:
        checks.append('missing required statistic blocks complete admission')
    else:
        raise AssertionError('missing required statistic accepted')
    noisy_sleep = copy.deepcopy(candidates)
    for s in noisy_sleep:
        if s['label'] == 'V-M3':
            s['statistics']['6/total/mean']['value_ns'] = 9000
    ns = rev03_candidate_admission(noisy_sleep, frozen)
    verify('sleep uses max fresh and frozen uncertainty',
           ns['sleep']['mean']['U_S_ns'] == 4020 and not ns['sleep']['mean']['passed'])
    noisy_observer = copy.deepcopy(candidates)
    for s in noisy_observer:
        if s['mode'] == 'P':
            s['statistics']['0/total/mean']['value_ns'] = 16000
    no = rev03_candidate_admission(noisy_observer, frozen)
    verify('observer retains frozen ceiling and signed O',
           no['observer']['0/total/mean']['O_ns'] == 6000 and
           no['observer']['0/total/mean']['ceiling_ns'] == 5000 and
           any(x['gate'] == 'observer' and x['numeric_gate_pass'] is False for x in no['issues']))
    small = copy.deepcopy(controls)
    for s in small:
        if s['mode'] == 'P':
            for row in (1, 2, 3, 4, 5, 6):
                for stat in R3_STATS:
                    s['statistics'][f'{row}/solverTimeNs/{stat}']['value_ns'] = 200
    diff = rev03_control_admission(small, prereqs)
    verify('denominator exactly20q freezes difference', diff['scaling']['P/5:4/solverTimeNs/mean']['branch'] == 'signed_difference')
    # Exhaustively check the partial impossibility bound against arbitrary missing values.
    for q in (1, 10):
        for a, b in ((10, 12), (100, 110), (1000, 1100)):
            if b-a+2*q >= max(.05*a, 4*q):
                for third in range(0, 5000, 7):
                    verify_condition = not _r3_cost_budget([a, b, third], q, 1000000)['headroom_pass']
                    assert verify_condition, (a, b, third, q)
    checks.append('partial impossibility exhaustive adversarial completions')
    return dict(status='PASS', count=len(checks), checks=checks, claim='Synthetic evaluator checks only; no qualification result.')

"""Revision03 allocation helpers, for inclusion in the owned Phase25 runner.

No entry point and no import-time work. Uses runner ROOT, VC, safe, sha, read,
write, environment. All dumpbin work is static inspection, never an app launch.
The caller charges elapsed analysis and evidence bytes to its existing ledger.
"""

import itertools


ALLOCATION_FIELDS = (
    "allocations", "frees", "requested", "usable", "peak_requested", "peak_usable",
    "transient_requested", "transient_usable", "surviving_requested", "surviving_usable",
    "live_requested", "live_usable", "peak_temporary_requested", "peak_temporary_usable")
ALLOCATION_PHYSICS_OBJECTS = (
    "PhysicsSystem", "PhysicsWorld", "PhysicsBody", "PhysicsProfile", "Shape",
    "ShapeBox", "ShapeSphere", "ShapeCapsule", "ShapeConvex", "Bounds", "Broadphase",
    "GJK", "Manifold", "Contact", "Constraint", "ConstraintPenetration")
ALLOCATION_INTEGRATION_OBJECTS = ("_Scene", "SceneRenderResources", "MeshAsset", "RbsUi", "RbsFrame")
ALLOCATION_NATIVE_RE = re.compile(
    r"^(?:__imp_)?(?:_?(?:malloc|calloc|realloc|recalloc|free|msize)(?:_dbg)?|"
    r"_aligned_(?:malloc|realloc|recalloc|free|msize)(?:_dbg)?|"
    r"Heap(?:Alloc|ReAlloc|Free)|Rtl(?:Allocate|ReAllocate|Free)Heap|"
    r"CoTaskMem(?:Alloc|Realloc|Free)|Virtual(?:Alloc|Free)(?:Ex)?|"
    r"Local(?:Alloc|ReAlloc|Free)|Global(?:Alloc|ReAlloc|Free))$")


def allocation_stage_proofs():
    proofs = read(BASE / "preparation/stage-proofs.json")
    assert all(p["source_sha256"] == sha(ROOT / "GEngine/include/GEngine/Physics/PhysicsSystem.cpp") for p in proofs.values())
    return proofs


def allocation_parse_coff(text):
    # CODE relocation rows only. SECREL/SECTION debug references are not calls,
    # and COFF SYMBOL TABLE declarations are not relocation records.
    sections = {}
    for match in re.finditer(r"^.*\bSECT([0-9A-F]+)\b.*notype \(\)\s+(?:External|Static)\s*\|\s*(\S+)", text, re.M):
        sections.setdefault(int(match.group(1),16),set()).add(match.group(2))
    relocations = {}; section = None
    row = re.compile(r"^\s+([0-9A-F]+)\s+(REL32(?:_[1-5])?|ADDR64|ADDR32NB|ADDR32)\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)")
    for line in text.splitlines():
        heading = re.fullmatch(r"RELOCATIONS #([0-9A-F]+)",line)
        if heading: section=int(heading.group(1),16);continue
        if line.startswith("COFF SYMBOL TABLE"): break
        match=row.match(line)
        if section is not None and match:
            relocations.setdefault(section,[]).append({"offset":int(match.group(1),16),"type":match.group(2),"symbol":match.group(3)})
    return sections,relocations


def allocation_classify_sites(name,native,sections,relocations):
    sites=[]
    for section,rows in relocations.items():
        owners=sorted(sections.get(section,()))
        symbols=[row["symbol"] for row in rows]
        # These named CRT helpers implement locale/regex/format infrastructure.
        # Current successful Fit/Replace/GeometrySource paths do not call them.
        locale_owner=any(("_Maklocstr@" in symbol or "_Getcat@?$" in symbol and any(kind in symbol for kind in ("codecvt@","collate@","ctype@","num_get@","num_put@","numpunct@")) or
            symbol.startswith(("??1?$_Tidy_guard@V?$numpunct@","??1?$_Yarn@","??1_Locinfo@","??1?$numpunct@","??_G?$numpunct@","??1?$ctype@","??_G?$ctype@","??1?$collate@","??_G?$collate@"))) for symbol in owners)
        regex_owner=name=="SceneRenderResources" and any(symbol.startswith(("??_G?$_Node_class@","??_G?$_Node_str@")) for symbol in owners)
        format_owner=name=="SceneRenderResources" and any("?$_Buf@" in symbol and "_Insert2@" in symbol for symbol in owners)
        hook_remove="?Remove@Phase25Memory@@YAXPEAX@Z" in symbols
        hook_allocate=any(symbol.startswith("?Allocate@Phase25Memory@@") for symbol in owners)
        request=name=="RbsFrame" and any(symbol.startswith("?Initialize@Session@PhysicsCost@Rbs@@") for symbol in owners)
        for row in rows:
            if row["symbol"] not in native:continue
            symbol=row["symbol"];base=symbol.removeprefix("__imp_")
            counted=name=="RbsFrame" and ((base in ("free","_aligned_free") and hook_remove) or (base in ("malloc","_aligned_malloc","_msize","_aligned_msize") and hook_allocate))
            outside=(locale_owner and base in ("calloc","free")) or (regex_owner and base=="free") or (format_owner and base=="realloc") or (request and base=="free")
            admitted=name not in ALLOCATION_PHYSICS_OBJECTS and (counted or outside)
            sites.append({"symbol":symbol,"section":section,"offset":row["offset"],"owners":owners,
                "hook_reference":"Phase25Memory::Remove" if counted and hook_remove else "Phase25Memory::Allocate" if counted else None,
                "status":"PASS" if admitted else "INCOMPLETE",
                "disposition":"existing counted global-new/delete backing call (including compiler inlining)" if counted else "specific CRT locale/regex/format or Initialize request path outside successful measured intervals" if outside else "unclassified native allocator/deallocator"})
    return sites


def allocation_linked_coverage(build_result, destination, engine_intermediate=None, rbs_intermediate=None):
    """Fresh Release A object/map/CRT audit. Leaves unexplained imports INCOMPLETE.

    All inspected raw COFF dumps and exact identities are retained. Known locale
    and formatting calls are excluded only from successful timed paths, with
    source evidence; this is not whole-application/native heap coverage.
    """
    build_result = safe(build_result)
    built = read(build_result)
    assert built["status"] == "PASS" and built["configuration"] == "Release" and built["mode"] == "A"
    assert built["target"] == "RigidBodySimulation"
    assert all(sha(ROOT / p) == h for p,h in built["products"].items()), "current linked product differs from successful build"
    destination = safe(destination)
    destination.mkdir(parents=True, exist_ok=False)
    engine_intermediate = safe(engine_intermediate or ROOT / "bin-int/Release/Phase25Revision03/A/GEngine")
    rbs_intermediate = safe(rbs_intermediate or ROOT / "bin-int/Release/Phase25Revision03/A/RigidBodySimulation")
    dumpbin = VC / "bin/Hostx64/x64/dumpbin.exe"
    exe = next(ROOT / p for p in built["products"] if p.lower().endswith(".exe"))
    map_path = exe.with_suffix(".map")
    assert map_path.is_file(), "fresh allocation executable map missing"
    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    shutil.copyfile(map_path, destination / "Release-A.map")
    checks = []; objects = {}; required_cpp = set(); sources = {}

    def check(ok, name, actual=None):
        checks.append({"gate": name, "status": "PASS" if ok else "INCOMPLETE", "actual": actual})

    # Current scope is the actual successful providers reviewed for Revision03.
    physics = ROOT / "GEngine/include/GEngine/Physics"
    for name in ALLOCATION_PHYSICS_OBJECTS:
        parent = physics / "Constraints" if name.startswith("Constraint") else physics
        for suffix in (".cpp", ".h"):
            p = parent / (name + suffix)
            if p.is_file(): sources[p.relative_to(ROOT).as_posix()] = sha(p)
    for relative in ("GEngine/include/GEngine/Physics/BoxContact.h", "GEngine/include/GEngine/Physics/PhysicsShapeError.h",
        "GEngine/include/GEngine/Physics/ContactIsland.h", "GEngine/include/GEngine/Physics/Constraints/SolverMath.h",
        "GEngine/include/GEngine/Physics/detail/ConvexHull.h", "GEngine/src/Scene/_Scene.cpp",
        "GEngine/src/Renderer/SceneRenderResources.cpp", "GEngine/src/Mesh/MeshAsset.cpp",
        "GEngine/include/GEngine/Assets/AssetRegistry.h", "GEngine/include/GEngine/Core/Log.h",
        "RigidBodySimulation/src/RbsUi.cpp", "RigidBodySimulation/tests/PhysicsCostChecks.h"):
        sources[relative] = sha(ROOT / relative)

    # malloc family imports in integration COMDATs are inspected via relocations.
    # No native allocator is admitted in a Physics provider object.
    for name in ALLOCATION_PHYSICS_OBJECTS + ALLOCATION_INTEGRATION_OBJECTS:
        parent = rbs_intermediate if name in ("RbsUi", "RbsFrame") else engine_intermediate
        matches = list(parent.rglob(name + ".obj"))
        assert len(matches) == 1, ("missing/ambiguous current object", name, matches)
        obj = matches[0]; before = sha(obj)
        dump = destination / (name + ".txt")
        with dump.open("xb") as log:
            completed = subprocess.run([str(dumpbin), "/nologo", "/symbols", "/directives", "/relocations", str(obj)],
                cwd=ROOT, env=environment(), stdout=log, stderr=subprocess.STDOUT,
                timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
        assert completed.returncode == 0 and sha(obj) == before, "dumpbin failure/object drift"
        text = dump.read_text(encoding="utf-8", errors="replace")
        undefined = sorted({m.group(1) for m in re.finditer(r"^.*\bUNDEF\b.*\bExternal\s*\|\s*(\S+)", text, re.M)})
        native = [s for s in undefined if ALLOCATION_NATIVE_RE.fullmatch(s)]
        cpp = [s for s in undefined if s.startswith(("??2@YAPEAX_K", "??_U@YAPEAX_K", "??3@YAXPEAX", "??_V@YAXPEAX"))]
        required_cpp.update(cpp)
        check("RuntimeLibrary=MT_StaticRelease" in text and "/DEFAULTLIB:LIBCMT" in text.upper() and
              "RuntimeLibrary=MD" not in text, name + ": static Release CRT")
        sections, relocations = allocation_parse_coff(text)
        definitions = sorted({symbol for symbols in sections.values() for symbol in symbols})
        own_methods = [symbol for symbol in definitions if "@" + name + "@GEngine@@" in symbol]
        noncontributing = name == "Contact" and not definitions or name == "Constraint" and bool(own_methods) and not any(symbol in map_text for symbol in own_methods)
        check(name.lower() + ".obj" in map_text.lower() or noncontributing, name + ": linked object present",
              {"noncontributing_archive_member":noncontributing,"own_methods":own_methods if noncontributing else []})
        sites = allocation_classify_sites(name, native, sections, relocations)
        check(not native if name in ALLOCATION_PHYSICS_OBJECTS else all(site["status"] == "PASS" for site in sites) and set(native) <= {site["symbol"] for site in sites},
              name + ": native allocator closure", {"imports": native, "sites": sites})
        objects[name] = {"path": obj.relative_to(ROOT).as_posix(), "sha256": before, "raw_sha256": sha(dump),
            "native_imports": native, "cpp_symbols": cpp, "native_callsites": sites}

    bindings = {}
    for symbol in sorted(required_cpp):
        # Custom placement forms are outside replaceable heap-new overloads.
        if "ImNewWrapper" in symbol: continue
        lines = [line for line in map_text.splitlines() if symbol in line.split()]
        bindings[symbol] = lines
        check(bool(lines) and all("rbsframe.obj" in line.lower() for line in lines), "global allocation binding: " + symbol, lines)
    check(bool(required_cpp), "observed allocating/deallocating object references")
    hook_source = (ROOT / "RigidBodySimulation/tests/PhysicsCostChecks.h").read_text()
    check(all(t in hook_source for t in ("_aligned_msize(p,alignment,0):_msize(p)", "if(observing)++errors;", "Sentinel();return !failed;", "void Export()", "Sentinel();")),
          "usable-byte accounting, observer error and startup/export sentinel source")
    result = {"schema": 1, "status": "PASS" if all(c["status"] == "PASS" for c in checks) else "INCOMPLETE",
        "build_result": str(build_result), "build_sha256": sha(build_result), "executable": str(exe), "exe_sha256": sha(exe),
        "map_sha256": sha(map_path), "dumpbin_sha256": sha(dumpbin), "objects": objects, "inputs": sources,
        "bindings": bindings, "checks": checks,
        "scope": "Fresh linked Release A successful complete Physics Update, FitMeshCollider, legacy direct/combined replacement and geometry lookup. Not whole-app or failed-provider native allocation coverage.",
        "source_disposition": "Successful FitMeshCollider has scalar/descriptor/borrowed-vertex operations only. ReplaceMeshCollider's format/string paths occur only on rejected commands; fixture rejects such results. Successful GeometrySource reads metadata and existing source storage. Physics direct native calls are forbidden; locale/format COMDATs remain outside successful timed paths. Current source hashes bind this reviewed statement.",
        "dynamic_sentinels": "Required anew in full C-A/V-A; this static audit does not claim they ran.",
        "errors":[c["gate"] for c in checks if c["status"] != "PASS"]}
    write(destination / "result.json", result)
    return result


def allocation_check(result_path, coverage_path, control_result=None):
    """Validate a complete fresh A capture and optionally every V<=C ordinal.

    Returns a compact report binding raw CSV identities; never rewrites evidence.
    Warm-up/setup, measured, teardown remain identifiable in the raw records.
    """
    result_path = safe(result_path); result = read(result_path)
    data = result_path.parent / "data"; fixture = read(data / "fixture.json")
    coverage_path = safe(coverage_path); coverage = read(coverage_path)
    failures = []; failed_count = 0; checks = 0; maxima = {}; row_metrics = {}; usable_unknown = False

    def gate(ok, name, actual=None):
        nonlocal checks, failed_count
        checks += 1
        if not ok:
            failed_count += 1
            if len(failures) < 128: failures.append({"gate": name, "actual": actual})

    gate(coverage["status"] == "PASS", "linked current allocation coverage")
    gate(all(sha(ROOT / p) == h for p, h in coverage["inputs"].items()), "linked coverage source identity")
    gate(result.get("status") == "CAPTURE_COMPLETE" and result.get("label") == ("V-A" if control_result else "C-A"), "capture result and role")
    execution = result.get("execution") or {}
    gate(execution.get("exit_code") == 0 and not execution.get("timed_out") and not execution.get("error") and
        execution.get("ownership", {}).get("quiescent_before_handle_close"), "clean owned process shutdown")
    gate(fixture.get("status") == "COMPLETE" and not fixture.get("failure") and fixture.get("mode") == "A" and
        fixture.get("records") == 18960 and fixture.get("rows_completed") == 21, "full current A suite")
    for key in ("allocation_errors", "physics_live_usable", "fixture_live_usable"):
        gate(fixture.get(key) == 0, key, fixture.get(key))
    gate(0 < fixture.get("observer_bytes", 0) <= 64 * 1024 * 1024, "recorder <=64MiB", fixture.get("observer_bytes"))
    owners = list(csv.DictReader((data / "process-static-owners.csv").open(newline="")))
    gate(not owners, "Release A has no Debug process-static lifetime exception", owners)
    row_names = ["F64", "A16", "A64", "A256", "B64", "B64-P2", "S64", "L120", "W64"] + [f"R{i}-{route}" for i in range(6) for route in ("direct", "mesh")]
    expected_totals = [1320] * 7 + [1800, 2520] + [450] * 12
    identities = list(csv.DictReader((data / "rows.csv").open(newline="")))
    gate(len(identities) == 21, "row identity count")
    for i, row in enumerate(identities):
        gate(int(row["row"]) == i and row["name"] == row_names[i] and int(row["input_hash"]) != 0 and int(row["order_hash"]) != 0 and int(row["live_after_teardown"]) == 0,
             "row identity/teardown " + str(i), row)
    census = list(csv.DictReader((data / "row-allocation-totals.csv").open(newline="")))
    gate(len(census) == 21, "row census count")
    for i, row in enumerate(census):
        gate(int(row["row"]) == i and all(int(v) >= 0 for v in row.values()), "row census ordinal " + str(i))
        gate(int(row["allocations"]) == int(row["frees"]), "row setup/teardown count balance " + str(i), row)
        gate(int(row["usable"]) >= int(row["requested"]) and int(row["peak_usable"]) >= int(row["peak_requested"]), "row usable-byte coverage " + str(i))
        gate(int(row["peak_usable"]) <= 64 * 1024 * 1024, "row live Physics <=64MiB " + str(i))
        gate(int(row["process_static_live"]) == int(row["process_static_allocated"]) == 0, "Release static owner census " + str(i))
    control_path = safe(control_result) if control_result else None
    control_data = control_path.parent / "data" if control_path else None
    if control_path:
        control = read(control_path)
        gate(control.get("status") == "CAPTURE_COMPLETE" and control.get("label") == "C-A", "control capture reference")
        gate(read(control_data / "fixture.json")["q_ns"] == fixture["q_ns"], "control/candidate q parity")
        gate(identities == list(csv.DictReader((control_data / "rows.csv").open(newline=""))), "full row input/order/teardown parity")
        control_census = list(csv.DictReader((control_data / "row-allocation-totals.csv").open(newline="")))
        for i, (v, c) in enumerate(itertools.zip_longest(census, control_census)):
            gate(v is not None and c is not None and v.keys() == c.keys(), "row census membership " + str(i))
            if v is None or c is None: continue
            for key in v:
                gate(int(v[key]) <= int(c[key]) if key != "row" else v[key] == c[key], "row V<=C " + str(i) + ":" + key, {"V": v[key], "C": c[key]})
    source = (ROOT / "GEngine/include/GEngine/Physics/PhysicsProfile.h").read_text().split("struct PhysicsProfileSnapshot", 1)[1].split("void ResetPhysicsProfile", 1)[0]
    profile_fields = re.findall(r"std::uint64_t\s+(\w+)\{\}", source)
    parity = ["row", "tick", "measured", "sleeping", "bodies", "islands", "command_kind", "state", "physical_state", "penetration"] + [f"profile_{i}" for i, field in enumerate(profile_fields) if not field.endswith("TimeNs")]
    count = 0; row_counts = [0] * 21; fit_operations = 0; commands = 0
    with (data / "steps.csv").open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = parity + [f"{boundary}_memory_{i}" for boundary in ("step", "fit", "command") for i in range(14)]
        gate(set(required) <= set(reader.fieldnames or ()), "complete memory fields including temporary peaks and physical parity")
        if not set(required) <= set(reader.fieldnames or ()): return {"status": "INCOMPLETE", "failure_count": failed_count, "failures": failures}
        control_stream = (control_data / "steps.csv").open(newline="") if control_data else None
        try:
            controls = csv.DictReader(control_stream) if control_stream else itertools.repeat(None)
            pairs = itertools.zip_longest(reader, controls) if control_stream else zip(reader, controls)
            for record, prior in pairs:
                count += 1
                if record is None or control_stream and prior is None:
                    gate(False, "matched ordinal count", count); continue
                row = int(record["row"]); tick = int(record["tick"])
                gate(0 <= row < 21, "row range", row)
                if not 0 <= row < 21: continue
                gate(tick == row_counts[row], "source-order ordinal", [row, tick, row_counts[row]])
                gate(row == next((i for i, n in enumerate(row_counts) if n < expected_totals[i]), -1), "source-order row", row)
                row_counts[row] += 1
                measured = tick >= (150 if row >= 9 else 60 if row == 7 else 120)
                gate(int(record["measured"]) == int(measured), "measurement window", [row, tick])
                if prior:
                    for key in parity: gate(record[key] == prior[key], "state/work/ordinal parity " + key, [row, tick, record[key], prior[key]])
                for boundary in ("step", "fit", "command"):
                    values = [int(record[f"{boundary}_memory_{i}"]) for i in range(14)]
                    if any(values[i] >= 2**63 for i in (3,5,7,9,11,13)):
                        usable_unknown = True
                        gate(False,"usable-byte uncertainty",[row,tick,boundary])
                    gate(all(v >= 0 for v in values), "nonnegative census", [row, tick, boundary])
                    for requested, usable in ((2,3),(4,5),(6,7),(8,9),(10,11),(12,13)):
                        gate(values[usable] >= values[requested], "usable-byte admission", [row,tick,boundary,requested])
                    gate(values[8] == values[2] - values[6] and values[9] == values[3] - values[7], "retained allocation accounting", [row,tick,boundary])
                    gate(values[13] <= 8*1024*1024, "transient usable per operation <=8MiB", [row,tick,boundary,values[13]])
                    gate(values[5] <= 64*1024*1024 and values[11] <= 64*1024*1024, "Physics owned usable <=64MiB", [row,tick,boundary])
                    if boundary == "fit":
                        gate(values[0] == 0 and values[2] == values[3] == values[8] == values[9] == values[12] == values[13] == 0, "exact zero fit allocation", [row,tick])
                    for i, value in enumerate(values):
                        field = boundary + ":" + ALLOCATION_FIELDS[i]
                        maxima[field] = max(maxima.get(field, 0), value)
                        item = row_metrics.setdefault(str(row), {}).setdefault(field, {"sum":0,"max":0})
                        item["sum"] += value; item["max"] = max(item["max"],value)
                        if prior: gate(value <= int(prior[f"{boundary}_memory_{i}"]), "ordinal V<=C " + field, [row,tick,value,int(prior[f"{boundary}_memory_{i}"])])
                if row >= 9 and measured: fit_operations += 1
                if int(record["command_kind"]) in (2,3):
                    commands += 1
                    gate(int(record["command_memory_0"]) > 0, "replacement allocation visible", [row,tick])
            gate(row_counts == expected_totals and count == 18960, "all full-suite ordinals", row_counts)
            gate(fit_operations == 3600 and commands == 3960, "all fitting/replacement operations", [fit_operations,commands])
        finally:
            if control_stream: control_stream.close()
    evidence = {name: sha(data / name) for name in ("fixture.json", "steps.csv", "rows.csv", "row-allocation-totals.csv", "process-static-owners.csv")}
    return {"schema":1,"status":"INCONCLUSIVE" if usable_unknown else "PASS" if not failed_count else "FAIL", "result":str(result_path), "result_sha256":sha(result_path),
        "coverage":str(coverage_path),"coverage_sha256":sha(coverage_path),"raw":evidence,"ordinal_count":count,
        "control_result":str(control_path) if control_path else None,"control_sha256":sha(control_path) if control_path else None,
        "checks":checks,"failure_count":failed_count,"failures_first_128":failures,"errors":failures,"maxima":maxima,"row_metrics":row_metrics,
        "scope":"All source-order ordinals, including warm-ups; row totals include setup/teardown separately from operation records. Every matched V value <= C, no percentage allowance. Raw file identity freezes all C ordinals.",
        "sentinels":"Fresh COMPLETE fixture with zero allocation_errors binds startup and export scalar/array/aligned/nothrow/vector/cross-boundary sentinels ; recorder writes are covered by separately bound static nonallocation proof, not the dormant observing flag."}


"""Ordinary I1: one existing owned-job process, no diagnostic child process.

Integrate after builds, before final evaluator/source freeze. The caller prepares
an immutable compatibility record before acquisition. Runtime smoke proves only
ordinary startup/responsiveness/native shutdown; physical/API and source/compile
compatibility are separate referenced evidence, and no human acceptance is implied.
"""


class OrdinarySmokeOwner(Revision03Owner):
    def __init__(self, audit, cwd, target):
        super().__init__(audit, cwd, target)
        import ctypes
        from ctypes import wintypes
        self.ct = ctypes; self.wt = wintypes
        self.user = ctypes.WinDLL("user32", use_last_error=True)
        self.callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        self.user.EnumThreadWindows.argtypes = [wintypes.DWORD, self.callback_type, wintypes.LPARAM]
        self.user.EnumThreadWindows.restype = wintypes.BOOL
        self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        self.user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        self.user.IsWindowVisible.argtypes = [wintypes.HWND]
        self.user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        self.user.SendMessageTimeoutW.argtypes = [wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,
            wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)]
        self.ready_at = None; self.next_probe = 0; self.closed = False
        self.audit["ordinary_smoke"] = {"status":"PENDING","native_close_posted":False,
            "physical_visual_acceptance":"NOT_PERFORMED","replacement_ui_actions":"NOT_PERFORMED"}

    def poll(self):
        code, active = super().poll()
        if code is not None: return code, active
        current = time.monotonic()
        if current < self.next_probe or self.closed: return code, active
        self.next_probe = current + .2
        owned = []
        @self.callback_type
        def enumerate_owned(hwnd, _):
            pid = self.wt.DWORD(); self.user.GetWindowThreadProcessId(hwnd,self.ct.byref(pid))
            name = self.ct.create_unicode_buffer(256); title = self.ct.create_unicode_buffer(256)
            self.user.GetClassNameW(hwnd,name,256);self.user.GetWindowTextW(hwnd,title,256)
            if pid.value == self.audit["pid"] and name.value == "SDL_app" and title.value and self.user.IsWindowVisible(hwnd):
                owned.append((hwnd,title.value))
            return True
        self.ct.set_last_error(0)
        enumeration_ok = bool(self.user.EnumThreadWindows(self.audit["thread_id"], enumerate_owned, 0))
        enumeration_error = self.ct.get_last_error()
        self.audit["ordinary_smoke"]["enumeration"] = {"method":"EnumThreadWindows held root thread", "returned":enumeration_ok,"last_error":enumeration_error,"owned_visible_windows":len(owned)}
        if not enumeration_ok and enumeration_error:
            raise RuntimeError("owned thread window enumeration error:"+str(enumeration_error))
        ready = None
        for hwnd,title in owned:
            reply = self.ct.c_size_t()
            if self.user.SendMessageTimeoutW(hwnd,0,0,0,2,250,self.ct.byref(reply)):
                ready = (hwnd,title);break
        smoke = self.audit["ordinary_smoke"]
        if self.ready_at is None:
            if ready:
                self.ready_at=current;smoke.update(first_responsive_monotonic=current,window_title=ready[1],window_class="SDL_app")
            elif current - self.audit["create_completed_monotonic"] >= 60:
                raise RuntimeError("I1 no responsive owned SDL window within60s")
        else:
            assert ready is not None, "I1 owned SDL window stopped responding"
            smoke["last_responsive_monotonic"]=current
            if current-self.ready_at >= 10:
                # Normal event loop and wall-time scheduler continue throughout.
                # WM_CLOSE goes only to this owned SDL HWND; no desktop takeover.
                smoke["native_close_posted"] = bool(self.user.PostMessageW(ready[0],0x0010,0,0))
                assert smoke["native_close_posted"], "I1 native close request failed"
                smoke.update(status="RESPONSIVE_NATIVE_CLOSE_REQUESTED",normal_runtime_seconds=current-self.ready_at)
                self.closed=True
        return code, active


def ordinary_i1(built_path, compatibility_path, *, freeze_path=None):
    """Run final I1 after all C/V gates pass. No implicit UI/human PASS.

    compatibility record requires source/API/compile coverage rows below. Each
    row must provide status PASS, evidence path and sha256. This is a record of
    actual checks and justified reuse, not a waiver or invented UI execution.
    """
    authority()
    assert read(BASE/"qualification-before-I1.json")["status"]=="PASS"
    compatible=read(safe(compatibility_path))
    for key in ("ordinary_tokens","ordinary_compile_flags_closure","current_support_replacement_api","current_resource_lifetime"):
        record=compatible[key]
        assert record["status"]=="PASS" and sha(safe(record["evidence"]))==record["sha256"], ("I1 compatibility evidence",key)
    built_path=safe(built_path);built=read(built_path)
    assert built["status"]=="PASS" and built["mode"]=="O" and built["configuration"]=="Release" and built["target"]=="RigidBodySimulation"
    assert not built.get("diagnostic")
    frozen=read(safe(freeze_path) if freeze_path else BASE/"input-freeze.json")
    assert source_inputs()==frozen["source_inputs"]
    assert sha(AUTH)==frozen["authority_sha256"]
    assert all(sha(ROOT/p)==h for p,h in frozen["runtime_inputs"].items() if "/Release/" in p or p.startswith("runtime/RigidBodySimulation/Release/")), "ordinary runtime inputs drift"
    assert all(sha(p)==h for p,h in frozen["actual_compiled_inputs"].items())
    assert all(sha(ROOT/p)==h for p,h in built["products"].items())
    no_interference();assert power_state()==frozen["power"]
    folder=fresh("native","I1");cwd=safe(ROOT/"runtime/RigidBodySimulation/Release")
    products={};exe=None
    for relative,digest in built["products"].items():
        source=ROOT/relative
        if source.suffix.lower() not in (".exe",".dll"):continue
        destination=safe(ROOT/f"bin/Release/{PRODUCT_ROOT}"/source.name)
        if not destination.exists() or sha(destination)!=digest:shutil.copyfile(source,destination)
        assert sha(destination)==digest;products[str(destination)]=digest
        if source.suffix.lower()==".exe":exe=destination
    assert exe is not None
    for name in ("imgui.ini","GEngine.log"):
        if (cwd/name).is_file():shutil.copyfile(cwd/name,folder/("before-"+name))
    env=environment("Release")
    assert not any(k.startswith(("GENGINE_PHASE","GENGINE_COLLIDER","GENGINE_BASELINE")) for k in env)
    target=time.monotonic()+30
    launch_preflight_utc=now()
    log_before_mtime=(cwd/"GEngine.log").stat().st_mtime_ns if (cwd/"GEngine.log").is_file() else None
    limit=use("native",folder,120)
    assert limit==120, "I1 full120s process/cleanup reservation unavailable"
    write(folder/"invocation.json",{"label":"I1","command":[str(exe)],"cwd":str(cwd),"limit_seconds":limit,
        "build":str(built_path),"build_sha256":sha(built_path),"ordinary_products":products,
        "compatibility":str(compatibility_path),"compatibility_sha256":sha(safe(compatibility_path)),"target_monotonic":target,"utc":now()})
    remaining=target-time.monotonic();assert remaining>=0,"I1 fixed schedule missed; no rescheduling"
    time.sleep(remaining)
    started=time.monotonic();execution=None;errors=[]
    try:
        with (folder/"console.log").open("xb") as log:
            execution=process_tools.remediation_owned_build([str(exe)],env,log,limit,
                _factory=lambda audit:OrdinarySmokeOwner(audit,cwd,target))
        assert execution["exit_code"]==0 and not execution["timed_out"] and not execution["error"],execution
        assert (cwd/"GEngine.log").is_file() and (cwd/"GEngine.log").stat().st_size > 0, "missing fresh ordinary application log"
        assert (cwd/"GEngine.log").stat().st_mtime_ns != log_before_mtime, "stale ordinary application log"
        assert (cwd/"GEngine.log").stat().st_mtime >= dt.datetime.fromisoformat(launch_preflight_utc).timestamp(), "ordinary log predates launch preflight"
        smoke=execution["ownership"].get("ordinary_smoke",{})
        assert execution["exit_code"]==0 and not execution["timed_out"] and not execution["error"],execution
        assert execution["ownership"]["quiescent_before_handle_close"] and smoke.get("native_close_posted") and smoke.get("normal_runtime_seconds",0)>=10,smoke
        assert not execution["ownership"].get("terminate_job_succeeded"), "I1 exit required termination"
        assert all(sha(p)==h for p,h in products.items()),"I1 product drift"
        authority()
    except BaseException as error:errors.append(repr(error))
    for name in ("imgui.ini","GEngine.log"):
        if (cwd/name).is_file():shutil.copyfile(cwd/name,folder/("after-"+name))
    console=(folder/"console.log").read_text(errors="replace") if (folder/"console.log").exists() else ""
    application=(folder/"after-GEngine.log").read_text(errors="replace") if (folder/"after-GEngine.log").exists() else ""
    diagnostics=console+"\n"+application
    if re.search(r"\[(?:error|critical)\]|Runtime asset failure|Application (?:initialization|runtime) (?:failed|error)|Cannot make the owning shutdown context current|GL_INVALID_(?:ENUM|VALUE|OPERATION)|GL_OUT_OF_MEMORY",diagnostics,re.I):
        errors.append("I1 application/GL/shutdown error logged; inspect retained logs")
    elapsed=time.monotonic()-started
    result={"status":"PASS" if not errors and elapsed<=limit else "FAIL","label":"I1","execution":execution,"errors":errors,"wall_seconds":elapsed,
        "coverage":"Ordinary uninstrumented Release startup, ten seconds of responsive normal event-loop lifetime, native WM_CLOSE, clean owned-job exit and log inspection. Current support/replacement/resource checks and ordinary source/compile equivalence referenced separately.",
        "limitations":["No automated replacement UI gesture or visual support/impact judgment in I1.","Source/API compatibility reuse does not claim UI interaction execution.","Owner final Release build/runtime inspection remains required."],
        "compatibility":compatible,"evidence":{p.relative_to(folder).as_posix():sha(p) for p in folder.rglob("*") if p.is_file()}}
    finish("native",folder,elapsed,result)
    if result["status"]!="PASS":raise SystemExit(1)
    return folder


def qualification_campaign():
    records = read(BASE / "prepared-builds-final.json")
    coverage_path = BASE / "linked-allocation-coverage-correction01.json"
    assert read(coverage_path)["status"] == "PASS"
    assert (BASE / "input-freeze.json").exists()
    profile_fields = re.findall(r"std::uint64_t\s+(\w+)\{\}", (ROOT / "GEngine/include/GEngine/Physics/PhysicsProfile.h").read_text().split("struct PhysicsProfileSnapshot",1)[1].split("void ResetPhysicsProfile",1)[0])
    proofs = allocation_stage_proofs()
    controls=[]; candidates=[]; prerequisite={}
    try:
        for label in ORDER[:-1]:
            configuration = "Debug" if label == "D1" else "Release"
            mode = "A" if label in ("D1","C-A","V-A") else label[2]
            folder = run(label, records[f"{configuration}-{mode}-RigidBodySimulation"])
            if label == "D1":
                prerequisite["D1"]={"status":"PASS","result":str(folder/'result.json'),"sha256":sha(folder/'result.json')}
            elif label in ("C-A","V-A"):
                result = allocation_check(folder/'result.json', coverage_path, BASE/'native/C-A-01/result.json' if label=='V-A' else None)
                write(BASE/f"analysis/{label}-allocation.json",result)
                assert result['status']=='PASS', ("allocation gate",result)
                prerequisite[label]=result
            else:
                summary = rev03_summarize_capture(folder,profile_fields,BASE/'native/C-A-01',proofs)
                write(BASE/f"analysis/{label}-cpu.json",summary)
                assert summary['status']=='PASS', ("individual CPU/identity gate",summary['issues'])
                group=controls if label.startswith('C') else candidates;group.append(summary)
                if label.startswith('C'):
                    partial=rev03_partial_control_stop(controls)
                    write(BASE/f"analysis/{label}-partial.json",partial)
                    assert partial['status']!='STOP_DECISIVE', ("irreversible control gate",partial['issues'])
            if label=='C-P3':
                prerequisite['linked_allocation_coverage']={'status':'PASS','path':str(coverage_path),'sha256':sha(coverage_path)}
                prerequisite['source_product_freeze']={'status':'PASS','path':str(BASE/'input-freeze.json'),'sha256':sha(BASE/'input-freeze.json')}
                prerequisite['phase20_resource_impact']=read(BASE/'preparation/query-resource-impact.json')
                budget=rev03_control_admission(controls,prerequisite)
                write(BASE/'budget-admission.json',budget)
                assert budget['status']=='PASS', ('control admission',budget['issues'])
            if label=='V-M3':
                candidate=rev03_candidate_admission(candidates,read(BASE/'budget-admission.json'))
                write(BASE/'candidate-cpu-admission.json',candidate)
                assert candidate['status']=='PASS', ('candidate admission',candidate['issues'])
        write(BASE/'qualification-before-I1.json',{'status':'PASS','utc':now(),'required_next':'ordinary Release I1 then owner human acceptance'})
        def evidence(path):
            return {'status':'PASS','evidence':str(path),'sha256':sha(path)}
        compatible={'ordinary_tokens':evidence(BASE/'preparation/final-ordinary-proof.json'),
            'ordinary_compile_flags_closure':evidence(BASE/'preparation/ordinary-flags.json'),
            'current_support_replacement_api':evidence(BASE/'native/V-A-01/result.json'),
            'current_resource_lifetime':evidence(BASE/'analysis/V-A-allocation.json')}
        write(BASE/'I1-compatibility.json',compatible)
        ordinary_i1(records['Release-O-RigidBodySimulation'],BASE/'I1-compatibility.json')
        finalize_campaign_record('PASS',{'qualification':str(BASE/'qualification-before-I1.json'),'ordinary_I1':str(BASE/'native/I1-01/result.json')})
    except BaseException as error:
        finalize_campaign_record('STOPPED_QUALIFICATION_GATE',{'error':repr(error)})
        raise


"""Pure Amendment01 CPU evaluator; integrate into the sole admitted runner.

Inputs are six records produced by frozen rev03_summarize_capture. The caller
retains its raw-workload, process, authority, source/product and coverage guards.
No writer, product selection, subprocess, native launch, or implicit evidence reuse.
"""
import math as _game_math
import statistics as _game_statistics

GAME_AMENDMENT_SHA256 = '4d96e687c21813d0fc30437753ac733426aaef71d5d9007bb314befcc812de48'
GAME_DISPOSITIONS = ('P25-CURRENT-ABSOLUTE', 'P25-FINITE-SCALING',
                     'P25-INSTRUMENTED-COST', 'P25-SINGLE-IMPLEMENTATION')
GAME_CPU_LABELS = ('C-M1', 'C-M2', 'C-M3', 'C-P1', 'C-P2', 'C-P3')
GAME_STAGES = ('broadphaseTimeNs', 'narrowphaseTimeNs', 'manifoldTimeNs',
               'contactResolutionTimeNs', 'contactIslandBuildTimeNs', 'solverTimeNs')
GAME_CONTRASTS = ((2, 1), (3, 2), (4, 2), (5, 4), (6, 2))
GAME_STATISTICS = ('mean', 'median', 'p95')
GAME_CLAIM = ('Finite current-cost acceptance at the declared M/P instrumented '
              'boundaries. P stages are empirical inclusive raw+q costs, not '
              'physical upper endpoints or ordinary uninstrumented stage costs. '
              'Scaling ratios and M/P perturbation are descriptive under the '
              'accepted limitations; no confidence, stationarity, complexity-law, '
              'causal-overhead, gameplay-frame, or hard-real-time guarantee.')


def _game_keys(mode):
    keys = {f'{row}/total/{stat}' for row in range(21)
            for stat in (*GAME_STATISTICS, 'sustained')}
    keys |= {f'{row}/{boundary}/{stat}' for row in range(9, 21)
             for boundary in ('fit', 'command') for stat in GAME_STATISTICS}
    keys |= {f'8/{boundary}/max' for boundary in
             ('wake_command', 'wake_first', 'wake_second', 'wake_sum')}
    if mode == 'P':
        keys |= {f'{row}/{stage}/{stat}' for row in range(21)
                 for stage in GAME_STAGES for stat in GAME_STATISTICS}
    return keys


def _game_absolute(key):
    row, boundary, statistic = key.split('/')
    row = int(row)
    dt = 1e9 / (120 if row == 7 else 60)
    if boundary == 'fit':
        return 1000000 if statistic == 'p95' else 500000
    if boundary == 'command':
        base = 1000000 if (row-9) % 2 else 500000
        return 2*base if statistic == 'p95' else base
    if boundary == 'wake_command':
        return 1000000
    if boundary in ('wake_first', 'wake_second'):
        return dt
    if boundary == 'wake_sum':
        return 2*dt
    return (.75 if statistic in ('p95', 'sustained') else .5)*dt


def _game_status(issues):
    return 'FAIL' if any(i['outcome'] == 'FAIL' for i in issues) else 'INCONCLUSIVE' if issues else 'PASS'


def _game_finite_nonnegative(value):
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and _game_math.isfinite(value) and value >= 0)


def game_cpu_admission(summaries, *, acceptance):
    """Evaluate exactly C-M1/M2/M3 and C-P1/P2/P3 under accepted Amendment01.

    acceptance accepts the activation's direct schema: proposal.sha256 and
    accepted_exceptions (the four disposition IDs). The alternative bridge keys
    amendment_sha256 and accepted_dispositions have the same exact meanings.
    Its authority/path/hash must be verified by the runner before this call.
    PASS covers only the complete CPU/coverage-parity/sleep evaluation; A2, I1,
    remaining source/coverage prerequisites and owner human acceptance stay separate.
    """
    issues = []
    def issue(gate, key, outcome, **details):
        issues.append(dict(gate=gate, key=key, outcome=outcome, **details))
    report = dict(schema=1, policy='PHASE_25_GAME_ORIENTED_ACCEPTANCE_AMENDMENT_01',
                  policy_sha256=GAME_AMENDMENT_SHA256, claim=GAME_CLAIM,
                  specific_owner_acceptance=acceptance, issues=issues,
                  accepted_dispositions=list(GAME_DISPOSITIONS),
                  remaining_mandatory_gates=['fresh A2 exact allocation comparison',
                                             'ordinary I1', 'final Owner Release|x64 acceptance'],
                  caller_required_guards=['accepted authority and exact source/product/runtime reuse',
                                          'full raw workload/count/invocation coverage and process identity',
                                          'allocation/recorder/Phase20 proof and all integrity guards'],
                  historical_outcomes='Original173 and Revision03_97 failures remain unchanged.')
    amendment = acceptance.get('proposal', {}).get('sha256', acceptance.get('amendment_sha256'))
    dispositions = acceptance.get('accepted_exceptions', acceptance.get('accepted_dispositions', []))
    if amendment != GAME_AMENDMENT_SHA256 or set(dispositions) != set(GAME_DISPOSITIONS):
        issue('explicit_amendment_acceptance', 'authority', 'INCONCLUSIVE')
    labels = [s.get('label') for s in summaries]
    if len(summaries) != 6 or set(labels) != set(GAME_CPU_LABELS) or len(set(labels)) != 6:
        issue('complete_preassigned_process_membership', 'six_cpu_processes', 'INCONCLUSIVE',
              actual_labels=labels, required_labels=list(GAME_CPU_LABELS))
    for s in summaries:
        label, mode = s.get('label'), s.get('mode')
        if mode not in ('M', 'P') or label not in GAME_CPU_LABELS or label[2] != mode:
            issue('mode_label_identity', str(label), 'INCONCLUSIVE', actual_mode=mode)
    quantum = summaries[0].get('q_ns') if summaries else None
    if (not _game_finite_nonnegative(quantum) or not 0 < quantum <= 1000
            or any(s.get('q_ns') != quantum for s in summaries)):
        issue('common_quantum_admission', 'q', 'INCONCLUSIVE',
              actual_quanta=[s.get('q_ns') for s in summaries])
    report['q_ns'] = quantum
    if issues:
        report['status'] = _game_status(issues)
        return report
    groups = {mode: sorted([s for s in summaries if s['mode'] == mode],
                           key=lambda s: s['label']) for mode in ('M', 'P')}
    if any(len(group) != 3 for group in groups.values()):
        issue('complete_mode_membership', 'three_per_mode', 'INCONCLUSIVE')
    malformed = False
    for s in summaries:
        label, mode = s['label'], s['mode']
        if s.get('status') != 'PASS':
            issue('individual_process_admission', label,
                  'FAIL' if s.get('status') == 'FAIL' else 'INCONCLUSIVE',
                  actual_status=s.get('status'))
        for original in s.get('issues', []):
            issue('individual_process_issue', label,
                  'FAIL' if original.get('outcome') == 'FAIL' else 'INCONCLUSIVE',
                  original_issue=original)
        if s.get('parity_mismatches') != 0:
            issue('state_work_ordinal_parity', label, 'FAIL' if s.get('parity_mismatches') else 'INCONCLUSIVE',
                  actual=s.get('parity_mismatches'))
        if set(s.get('statistics', {})) != _game_keys(mode):
            issue('complete_statistic_membership', label, 'INCONCLUSIVE')
            malformed = True
            continue
        for key, value in s['statistics'].items():
            if not _game_finite_nonnegative(value.get('value_ns')):
                issue('finite_numeric_statistic', label+'/'+key, 'INCONCLUSIVE')
                malformed = True
                continue
            ceiling = _game_absolute(key)
            if value.get('absolute_ns') != ceiling:
                issue('unchanged_absolute_definition', label+'/'+key, 'FAIL',
                      recorded=value.get('absolute_ns'), required=ceiling)
            boundary = key.split('/')[1]
            absent = value.get('absent_proven', False)
            if absent:
                proof = s.get('stage_proofs', {}).get(boundary, {})
                digest = proof.get('source_sha256', '')
                valid = (mode == 'P' and boundary in GAME_STAGES and len(digest) == 64
                         and all(c in '0123456789abcdef' for c in digest)
                         and bool(proof.get('source_statement'))
                         and bool(proof.get('zero_invocation_counters'))
                         and s.get('diagnostics', {}).get('/'.join(key.split('/')[:2]), {}).get('invocation_status')
                             == 'ABSENT_SOURCE_AND_COUNTER_PROVEN')
                if not valid:
                    issue('source_counter_absence_proof', label+'/'+key, 'INCONCLUSIVE')
            elif value['value_ns'] > ceiling:
                issue('absolute', label+'/'+key, 'FAIL', actual_ns=value['value_ns'],
                      ceiling_ns=ceiling, numeric_gate_pass=False, inequality='value <= absolute ceiling')
    if malformed or any(len(group) != 3 for group in groups.values()):
        report['status'] = _game_status(issues)
        return report
    def values(mode, key):
        return [s['statistics'][key]['value_ns'] for s in groups[mode]]
    def absent(mode, key):
        return all(s['statistics'][key].get('absent_proven', False) for s in groups[mode])
    def unresolved(mode, key):
        return any(s['statistics'][key].get('absent_proven', False)
                   or s['statistics'][key].get('unresolved', False) for s in groups[mode])
    costs, scaling, sleep, observer = {}, {}, {}, {}
    for mode, group in groups.items():
        for key in sorted(_game_keys(mode)):
            vv = values(mode, key)
            absence = [s['statistics'][key].get('absent_proven', False) for s in group]
            if any(absence) and not all(absence):
                issue('invocation_parity_across_repeats', mode+'/'+key, 'FAIL')
            ceiling = _game_absolute(key)
            outcomes = ['N/A_ABSENT_PROVEN' if a else 'PASS' if v <= ceiling else 'FAIL'
                        for v, a in zip(vv, absence)]
            costs[mode+'/'+key] = dict(labels=[s['label'] for s in group], values_ns=vv,
                                       absolute_ns=ceiling, per_process_outcomes=outcomes,
                                       observed_range_ns=max(vv)-min(vv))
        boundaries = ('total',) if mode == 'M' else ('total',)+GAME_STAGES
        for numerator, denominator in GAME_CONTRASTS:
            for boundary in boundaries:
                for statistic in GAME_STATISTICS:
                    nk, dk = f'{numerator}/{boundary}/{statistic}', f'{denominator}/{boundary}/{statistic}'
                    key = f'{mode}/{numerator}:{denominator}/{boundary}/{statistic}'
                    nv, dv = values(mode, nk), values(mode, dk)
                    if absent(mode, nk) and absent(mode, dk):
                        scaling[key] = dict(branch='N/A_ABSENT_PROVEN',
                                            actual_outcome='N/A_SOURCE_AND_ZERO_INVOCATION_PROVEN',
                                            accepted_disposition='P25-FINITE-SCALING',
                                            legacy_control_spread=dict(actual_outcome='N/A_SOURCE_AND_ZERO_INVOCATION_PROVEN'),
                                            held_out_V_comparison='NOT_RUN_NO_DISTINCT_IMPLEMENTATION')
                        continue
                    ratio = (not unresolved(mode, nk) and not unresolved(mode, dk)
                             and all(v > 20*quantum for v in dv))
                    vv = [a/b if ratio else a-b for a, b in zip(nv, dv)]
                    legacy_spread = max(vv)-min(vv)
                    legacy_ceiling = (.05*_game_statistics.median(vv) if ratio
                                      else max(.025*max(nv+dv), 4*quantum))
                    legacy_pass = legacy_spread <= legacy_ceiling
                    legacy = dict(actual_outcome='PASS' if legacy_pass else 'FAIL',
                                  numeric_gate_pass=legacy_pass, actual_spread=legacy_spread,
                                  ceiling=legacy_ceiling,
                                  inequality='range(ratios) <= .05*median(ratios)' if ratio
                                      else 'range(differences) <= max(.025*max(all numerator/denominator values),4q)',
                                  disposition='ACCEPTED_EXCEPTION',
                                  accepted_disposition='P25-FINITE-SCALING',
                                  affects_mandatory_status=False,
                                  interpretation='Original control-spread formula evaluated descriptively on the fixed current repeat set; no claim that the stopped historical protocol passed.')
                    scaling[key] = dict(branch='ratio' if ratio else 'signed_difference',
                                        labels=[s['label'] for s in group], values=vv,
                                        range=max(vv)-min(vv), numerator_values_ns=nv,
                                        denominator_values_ns=dv, actual_outcome='DESCRIPTIVE_ONLY',
                                        accepted_disposition='P25-FINITE-SCALING',
                                        legacy_control_spread=legacy,
                                        held_out_V_comparison='NOT_RUN_NO_DISTINCT_IMPLEMENTATION',
                                        acceptance_ceiling=None)
    for statistic in GAME_STATISTICS:
        sv, av = values('M', f'6/total/{statistic}'), values('M', f'2/total/{statistic}')
        us, ua = max(sv)-min(sv)+2*quantum, max(av)-min(av)+2*quantum
        left, right = max(sv)+us, min(av)-ua
        passed = left <= right if statistic == 'p95' else left < right
        sleep[statistic] = dict(S_ns=sv, A_ns=av, U_S_ns=us, U_A_ns=ua,
                                left_ns=left, right_ns=right, inclusive=statistic == 'p95',
                                numeric_gate_pass=passed, actual_outcome='PASS' if passed else 'FAIL')
        if not passed:
            issue('resolved_sleep_savings', statistic, 'FAIL', left_ns=left, right_ns=right,
                  numeric_gate_pass=False, inclusive=statistic == 'p95',
                  inequality='max(S)+U_S <= min(A)-U_A' if statistic == 'p95' else 'max(S)+U_S < min(A)-U_A',
                  sensitivity='INCONCLUSIVE_RESOLVED_SAVINGS_NOT_ESTABLISHED')
    for key in sorted(_game_keys('M')):
        mv, pv = values('M', key), values('P', key)
        signed = _game_statistics.median(pv)-_game_statistics.median(mv)
        uncertainty = max(pv)-min(pv)+max(mv)-min(mv)+4*quantum
        threshold = max(.10*_game_statistics.median(mv), 5000)
        actual = abs(signed)+uncertainty
        passed = actual <= threshold
        observer[key] = dict(M_ns=mv, P_ns=pv, ordered_pairs_ns=list(zip(mv, pv)),
                              O_ns=signed, J_ns=uncertainty, T_ns=threshold, actual_ns=actual,
                              inequality='abs(O)+J <= max(.10*median(M),5000ns)',
                              numeric_gate_pass=passed, actual_outcome='PASS' if passed else 'FAIL',
                              disposition='ACCEPTED_EXCEPTION', accepted_disposition='P25-INSTRUMENTED-COST',
                              causal_overhead_status='INCONCLUSIVE_NOT_IDENTIFIED',
                              mandatory_absolute_gate_waived=False)
    report.update(status=_game_status(issues), costs=costs, scaling=scaling,
                  sleep=sleep, observer=observer,
                  statistic_count=sum(len(s['statistics']) for s in summaries),
                  observer_numeric_failures=sum(not r['numeric_gate_pass'] for r in observer.values()),
                  captures=[dict(label=s['label'], steps_sha256=s.get('steps_sha256'),
                                 result_sha256=s.get('result_sha256'),
                                 reference_steps_sha256=s.get('reference_steps_sha256')) for s in summaries])
    return report


def game_evaluator_selftest():
    """Synthetic adversarial tests only; no native qualification result."""
    import copy
    checks = []
    def verify(name, condition):
        assert condition, name
        checks.append(name)
    acceptance = dict(proposal=dict(sha256=GAME_AMENDMENT_SHA256),
                      accepted_exceptions=list(GAME_DISPOSITIONS), purpose='SYNTHETIC_ONLY')
    def sample(label):
        mode = label[2]
        stats = {}
        for key in _game_keys(mode):
            row = int(key.split('/')[0])
            stats[key] = dict(value_ns=5000 if row == 6 else 10000,
                              absolute_ns=_game_absolute(key), absent_proven=False)
        return dict(label=label, mode=mode, q_ns=10, status='PASS', issues=[],
                    parity_mismatches=0, statistics=stats, diagnostics={}, stage_proofs={})
    summaries = [sample(label) for label in GAME_CPU_LABELS]
    base = game_cpu_admission(summaries, acceptance=acceptance)
    verify('six independent summaries satisfy retained synthetic gates', base['status'] == 'PASS')
    verify('full statistic scaling and observer membership',
           base['statistic_count'] == 2094 and len(base['scaling']) == 120 and len(base['observer']) == 160)
    verify('mandatory Solver pass contrast reported', 'P/5:4/solverTimeNs/p95' in base['scaling'])
    bad = copy.deepcopy(summaries)
    bad[0]['statistics']['0/total/mean']['value_ns'] = _game_absolute('0/total/mean')+1
    verify('single bad process cannot average away', game_cpu_admission(bad, acceptance=acceptance)['status'] == 'FAIL')
    bad = copy.deepcopy(summaries)
    for s in bad:
        if s['mode'] == 'M':
            s['statistics']['6/total/mean']['value_ns'] = 10000
    failed = game_cpu_admission(bad, acceptance=acceptance)
    verify('sleep failure blocks despite all absolute values passing',
           failed['status'] == 'FAIL' and not failed['sleep']['mean']['numeric_gate_pass'])
    bad = copy.deepcopy(summaries)
    for s in bad:
        if s['mode'] == 'P':
            s['statistics']['0/total/mean']['value_ns'] = 16000
    diagnosed = game_cpu_admission(bad, acceptance=acceptance)
    verify('observer FAIL remains diagnostic under accepted exception',
           diagnosed['status'] == 'PASS' and diagnosed['observer']['0/total/mean']['actual_outcome'] == 'FAIL'
           and diagnosed['observer']['0/total/mean']['disposition'] == 'ACCEPTED_EXCEPTION')
    verify('missing mode process blocks', game_cpu_admission(summaries[:-1], acceptance=acceptance)['status'] == 'INCONCLUSIVE')
    bad = copy.deepcopy(summaries)
    bad[0]['q_ns'] = 100
    verify('different q blocks', game_cpu_admission(bad, acceptance=acceptance)['status'] == 'INCONCLUSIVE')
    bad = copy.deepcopy(summaries)
    for s in bad:
        s['q_ns'] = 0
    verify('zero q blocks', game_cpu_admission(bad, acceptance=acceptance)['status'] == 'INCONCLUSIVE')
    bad = copy.deepcopy(summaries)
    bad[0]['statistics'].pop('0/total/mean')
    verify('missing statistic blocks', game_cpu_admission(bad, acceptance=acceptance)['status'] == 'INCONCLUSIVE')
    bad = copy.deepcopy(summaries)
    bad[0]['statistics']['0/total/mean']['value_ns'] = float('nan')
    verify('nonfinite statistic blocks', game_cpu_admission(bad, acceptance=acceptance)['status'] == 'INCONCLUSIVE')
    bad = copy.deepcopy(summaries)
    bad[0]['parity_mismatches'] = 1
    verify('state work parity failure blocks', game_cpu_admission(bad, acceptance=acceptance)['status'] == 'FAIL')
    bad = copy.deepcopy(summaries)
    bad[0]['statistics']['0/total/mean']['absolute_ns'] *= 2
    verify('altered absolute ceiling blocks', game_cpu_admission(bad, acceptance=acceptance)['status'] == 'FAIL')
    bad = copy.deepcopy(summaries)
    for s in bad:
        if s['mode'] == 'P':
            for row in (4, 5):
                s['statistics'][f'{row}/solverTimeNs/mean']['value_ns'] = 200
    difference = game_cpu_admission(bad, acceptance=acceptance)
    verify('exact20q denominator selects signed difference',
           difference['scaling']['P/5:4/solverTimeNs/mean']['branch'] == 'signed_difference')
    unauthorized = dict(acceptance, accepted_exceptions=[])
    verify('exception cannot apply without exact accepted dispositions',
           game_cpu_admission(summaries, acceptance=unauthorized)['status'] == 'INCONCLUSIVE')
    bad = copy.deepcopy(summaries)
    for s in bad:
        if s['mode'] == 'M':
            for stat in GAME_STATISTICS:
                s['statistics'][f'6/total/{stat}']['value_ns'] = 9960
    equality = game_cpu_admission(bad, acceptance=acceptance)
    verify('sleep strict mean median equality fails but p95 equality passes',
           not equality['sleep']['mean']['numeric_gate_pass']
           and not equality['sleep']['median']['numeric_gate_pass']
           and equality['sleep']['p95']['numeric_gate_pass'])
    legacy_fail = copy.deepcopy(summaries)
    legacy_fail[-1]['statistics']['5/solverTimeNs/mean']['value_ns'] = 20000
    diagnostic = game_cpu_admission(legacy_fail, acceptance=acceptance)
    legacy = diagnostic['scaling']['P/5:4/solverTimeNs/mean']['legacy_control_spread']
    verify('legacy ratio spread FAIL retained diagnostically without blocking absolutes',
           diagnostic['status'] == 'PASS' and legacy['actual_outcome'] == 'FAIL'
           and legacy['actual_spread'] == 1 and legacy['ceiling'] == .05
           and legacy['affects_mandatory_status'] is False)
    legacy_fail[0]['statistics']['0/total/mean']['value_ns'] = _game_absolute('0/total/mean')+1
    guarded = game_cpu_admission(legacy_fail, acceptance=acceptance)
    verify('legacy spread exception cannot waive absolute failure',
           guarded['status'] == 'FAIL' and any(i['gate'] == 'absolute' for i in guarded['issues']))
    legacy_fail[0]['statistics']['0/total/mean']['value_ns'] = 10000
    for s in legacy_fail:
        if s['mode'] == 'M':
            s['statistics']['6/total/mean']['value_ns'] = 10000
    guarded = game_cpu_admission(legacy_fail, acceptance=acceptance)
    verify('legacy spread exception cannot waive sleep failure',
           guarded['status'] == 'FAIL' and any(i['gate'] == 'resolved_sleep_savings' for i in guarded['issues']))
    verify('legacy signed difference spread uses original floor',
           difference['scaling']['P/5:4/solverTimeNs/mean']['legacy_control_spread']['ceiling'] == 40)
    return dict(status='PASS', count=len(checks), checks=checks,
                claim='Synthetic evaluator verification only; no native evidence or phase acceptance.')


def verify_a1_binding():
    binding = read(BASE / "preparation/A1-raw-binding.json")
    assert binding["status"] == "PASS"
    for key in ("result", "allocation_analysis", "coverage", "reuse"):
        item = binding[key]
        assert sha(item["path"]) == item["sha256"], ("A1 binding drift", key)
    assert all(sha(path) == h for path, h in binding["raw"].items()), "A1 raw drift"
    return Path(binding["result"]["path"])


def game_campaign():
    """One accepted continuation; existing evidence is never rerun or overwritten."""
    authority()
    assert not list((BASE / "native").glob("*/result.json")), "campaign already attempted; no automatic resume/retry"
    assert read(BASE / "preparation/acquisition-admission.json")["status"] == "PASS"
    records = read(BASE / "prepared-builds-final.json")
    coverage = BASE / "linked-allocation-coverage-correction01.json"
    proofs = allocation_stage_proofs()
    fields = re.findall(r"std::uint64_t\s+(\w+)\{\}", (ROOT / "GEngine/include/GEngine/Physics/PhysicsProfile.h").read_text().split("struct PhysicsProfileSnapshot",1)[1].split("void ResetPhysicsProfile",1)[0])
    reference = verify_a1_binding().parent
    controls = [read(BASE / f"analysis/reused-{label}.json") for label in ("C-M1","C-P1","C-P2")]
    try:
        for label in ("C-M2","C-M3","C-P3"):
            folder = run(label, records[f"Release-{label[2]}-RigidBodySimulation"])
            result = rev03_summarize_capture(folder, fields, reference, proofs)
            write(BASE / f"analysis/{label}-cpu.json", result)
            assert result["status"] == "PASS", ("individual absolute/state/work gate", result["issues"])
            controls.append(result)
        admission = game_cpu_admission(controls, acceptance=authority())
        write(BASE / "absolute-admission.json", admission)
        assert admission["status"] == "PASS", ("retained mandatory CPU/sleep gate", admission["issues"])
        a2 = run("V-A", records["Release-A-RigidBodySimulation"])
        memory = allocation_check(a2 / "result.json", coverage, verify_a1_binding())
        memory["policy_role"] = "A2 same-product allocation consistency; no versiondelta claim"
        write(BASE / "analysis/A2-allocation.json", memory)
        assert memory["status"] == "PASS", ("A2 exact memory/ownership gate", memory)
        compatible = read(BASE / "preparation/ordinary-compatibility.json")
        compatible["current_resource_lifetime"]["fresh_A2"] = {"path":str(BASE / "analysis/A2-allocation.json"), "sha256":sha(BASE / "analysis/A2-allocation.json")}
        write(BASE / "I1-compatibility.json", compatible)
        write(BASE / "qualification-before-I1.json", {"status":"PASS", "utc":now(), "CPU":sha(BASE / "absolute-admission.json"), "A2":sha(BASE / "analysis/A2-allocation.json"), "required_next":"ordinary I1 and separate Owner Release rebuild/inspection"})
        ordinary_i1(records["Release-O-RigidBodySimulation"], BASE / "I1-compatibility.json")
        finalize_campaign_record("PASS", {"absolute_sleep":str(BASE / "absolute-admission.json"), "A2":str(BASE / "analysis/A2-allocation.json"), "I1":str(BASE / "native/I1-01/result.json"), "accepted_exceptions":list(GAME_DISPOSITIONS)})
    except BaseException as error:
        finalize_campaign_record("STOPPED_RETAINED_MANDATORY_GATE", {"error":repr(error)})
        raise


def main():
    parser = argparse.ArgumentParser(__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("static")
    sub.add_parser("prepare")
    sub.add_parser("freeze")
    sub.add_parser("campaign")
    sub.add_parser("selftest")
    b = sub.add_parser("build"); b.add_argument("configuration", choices=["Debug", "Release"]); b.add_argument("mode", choices=["A", "M", "P", "O"]); b.add_argument("target", choices=["GEngine", "RigidBodySimulation"])
    b.add_argument("--ownership-diagnostic", action="store_true")
    b.add_argument("--w64-diagnostic", action="store_true")
    r = sub.add_parser("run"); r.add_argument("label", choices=ORDER+["MD1", "MD2", "WD1", "C-R", "C-T"]); r.add_argument("built")
    args = parser.parse_args()
    if args.command == "static": static_check()
    elif args.command == "prepare": prepare_builds()
    elif args.command == "selftest": print(json.dumps({"original_parser":rev03_analyzer_selftest(),"game":game_evaluator_selftest()}))
    elif args.command == "campaign": game_campaign()
    elif args.command == "freeze":
        records=read(BASE/'prepared-builds-final.json')
        coverage=allocation_linked_coverage(records['Release-A-RigidBodySimulation'],BASE/'allocation-coverage')
        write(BASE/'linked-allocation-coverage-correction01.json',coverage)
        assert coverage['status']=='PASS',coverage
        freeze_campaign(records)
    elif args.command == "build": build(args.configuration, args.mode, args.target, args.ownership_diagnostic, args.w64_diagnostic)
    elif args.command == "run": run(args.label, args.built)


if __name__ == "__main__": main()
