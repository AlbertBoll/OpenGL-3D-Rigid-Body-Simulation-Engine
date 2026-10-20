"""Owner-authorized prospective Phase20 campaign; historical counters stay intact."""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from types import SimpleNamespace
from xml.sax.saxutils import escape

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("p20_preserved_runner", ROOT / "tools/pre_editor_resting_contact_diagnosis.py")
legacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)
BASE = ROOT / "logs/pre_editor/phase-20/comprehensive-remediation-01"
AUTH = ROOT / "docs/pre_editor/active/PHASE_20_COMPREHENSIVE_REMEDIATION_AUTHORIZATION_01.json"
sha, contained, write_new, now = legacy.sha, legacy.contained, legacy.write_new, legacy.now


def authority():
    a = json.loads(AUTH.read_text())
    assert a["prospective_limits"] == {"automated_RBS_launches": 30, "per_native_wall_seconds": 180,
        "native_seconds": 4500, "build_seconds": 6000, "complete_corrected_candidate_qualification_attempts": 4}
    assert legacy.git("rev-parse", "HEAD").decode().strip() == a["baseline_commit"]
    assert legacy.git("branch", "--show-current").decode().strip() == "pre-editor/refactor"
    assert not legacy.git("diff", "--cached", "--name-only").strip()
    assert not legacy.git("ls-files", "--unmerged").strip()
    index = Path(legacy.git("rev-parse", "--git-path", "index").decode().strip())
    assert sha(index) == a["index_sha256"]
    for p in a["canonical_owner_files_preserved"]:
        assert sha(ROOT / p) == a["baseline_source_hashes"][p], "owner source changed: " + p
    return a


def usage():
    total = {"build_seconds": 0., "native_seconds": 0., "native_launches": 0, "qualification_attempts": 0}
    for kind in ("builds", "native"):
        for invocation in sorted((BASE / kind).glob("*/invocation.json")):
            inv = json.loads(invocation.read_text())
            result = invocation.with_name("result.json")
            assert result.exists(), "unreconciled prior attempt: " + str(invocation)
            data = json.loads(result.read_text())
            seconds = data["wall_seconds"]
            assert isinstance(seconds, (int, float)) and math.isfinite(seconds) and seconds >= 0, "invalid charged duration"
            total["build_seconds" if kind == "builds" else "native_seconds"] += seconds
            if kind == "native":
                total["native_launches"] += 1
                total["qualification_attempts"] += int(inv["purpose"] == "qualification")
    return total


def update_ledger():
    record = {"schema": 1, "campaign": "comprehensive-remediation-01", "updated_utc": now(),
        "authority": AUTH.relative_to(ROOT).as_posix(), "usage": usage(),
        "historical_interval": "Preserved separately; manual totals UNKNOWN, not zero",
        "attempts": {kind: [p.relative_to(ROOT).as_posix() for p in sorted((BASE / kind).glob("*/invocation.json"))]
            for kind in ("builds", "native")}}
    temporary = BASE / "ledger.pending.json"
    temporary.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    temporary.replace(BASE / "ledger.json")


def fresh(kind, label):
    parent = contained(BASE / kind)
    parent.mkdir(exist_ok=True)
    for number in range(1, 1000):
        path = parent / f"{label}-{number:02d}"
        if not path.exists():
            path.mkdir()
            return path
    raise RuntimeError("attempt namespace exhausted")


def environment():
    env = dict(os.environ)
    for key in list(env):
        if key in ("CL", "_CL_", "GENGINE_ASSET_ROOT", "GENGINE_BASELINE_OUTPUT") or (key.startswith("GENGINE_") and "REQUEST" in key):
            env.pop(key)
    tmp = contained(ROOT / "runtime/tmp/phase20-comprehensive")
    tmp.mkdir(parents=True, exist_ok=True)
    env["TMP"] = env["TEMP"] = str(tmp)
    return env


def inputs(role, mode):
    old = json.loads((ROOT / "logs/pre_editor/phase-20/conditional-remediation-01/candidates/candidate-10/frozen.json").read_text())["inputs"]
    paths = set(old["workspace_inputs"])
    paths.update([AUTH.relative_to(ROOT).as_posix(), Path(__file__).relative_to(ROOT).as_posix()])
    projects = ("GEngine/GEngine.vcxproj", "external/glad/glad.vcxproj", "RigidBodySimulation/RigidBodySimulation.vcxproj")
    for project in projects:
        definition = ROOT / project
        paths.add(project)
        for node in ET.parse(definition).iter():
            if node.tag.rsplit("}", 1)[-1] not in ("ClCompile", "ClInclude", "ProjectReference"): continue
            value = node.get("Include")
            if not value: continue
            assert "$" not in value and "*" not in value, "unevaluated project membership"
            path = (definition.parent / value).resolve()
            paths.add(contained(path).relative_to(ROOT).as_posix())
    # Completed first isolated compilation exposes the exact consumed closure.
    # Omitted-input objects are preserved and invalidated before the next build;
    # their new compilation is frozen prospectively, never retroactively trusted.
    extra_external = set()
    closure = BASE / "proofs/build-01-closure-gap-final-01.json"
    if closure.exists():
        for value in json.loads(closure.read_text())["consumed_input_hashes_at_audit"]:
            path = Path(value).resolve()
            if str(path).casefold().startswith(str(ROOT).casefold() + os.sep):
                paths.add(os.path.relpath(path, ROOT).replace("\\", "/"))
            else:
                extra_external.add(str(path))
    if mode == "coupled":
        paths.update(p.relative_to(ROOT).as_posix() for p in (ROOT / "RigidBodySimulation/tests").glob("Coupled*.h"))
        paths.update(p.relative_to(ROOT).as_posix() for p in (BASE / "overlays" / "current").rglob("*.cpp"))
    if mode == "qualification":
        paths.update(p.relative_to(ROOT).as_posix() for p in (BASE / "overlays/qualification").rglob("*.cpp"))
    if role == "baseline":
        paths.difference_update({"GEngine/include/GEngine/Physics/GJK.cpp", "GEngine/include/GEngine/Physics/PhysicsSystem.cpp"})
        paths.update(p.relative_to(ROOT).as_posix() for p in (BASE / "overlays" / "baseline").rglob("*.cpp"))
    scopes = set(old["discovery"]) | {"RigidBodySimulation/tests"}
    scopes.update(Path(p).parent.as_posix() for p in paths if Path(p).suffix.lower() in (".h", ".hpp", ".inl", ".cpp", ".c"))
    discovery = {}
    for scope in sorted(scopes):
        directory = ROOT / scope
        discovery[scope] = sorted(p.name for p in directory.iterdir() if p.is_file())
        for name in discovery[scope]:
            paths.add((Path(scope) / name).as_posix())
    # The replaced production bodies are not compilation inputs of the baseline.
    if role == "baseline":
        replaced = {"gengine/include/gengine/physics/gjk.cpp", "gengine/include/gengine/physics/physicssystem.cpp"}
        paths = {p for p in paths if p.casefold() not in replaced}
    workspace = {p: sha(ROOT / p) for p in sorted(paths)}
    external = {p: sha(p) for p in old["external_inputs"]}
    assert external == old["external_inputs"], "toolchain/dependency drift requires classification"
    external.update({p: sha(p) for p in sorted(extra_external)})
    return {"workspace": workspace, "external": external, "discovery": discovery, "role": role, "mode": mode}


def compilation_inputs(record):
    # Native products bind actual translation units, generated overlays, headers,
    # assets and toolchain. Runner/review-only repairs do not require recompiling.
    return {**record, "workspace": {p: h for p, h in record["workspace"].items()
        if (not p.startswith(("docs/pre_editor/", "logs/pre_editor/")) or p.endswith((".cpp", ".h", ".hpp", ".inl")))
        and p not in ("tools/pre_editor_resting_contact_diagnosis.py", "tools/pre_editor_phase20_comprehensive.py")}}


def make_props(folder, role, mode, configuration):
    assert role in ("current", "baseline") and mode in ("coupled", "qualification", "normal")
    product = f"Phase20Comprehensive/{role}/{mode}"
    definitions = "GE_ENABLE_PHYSICS_PROFILING;" if mode != "normal" and configuration == "Release" else ""
    if mode == "coupled":
        definitions += "GE_ENABLE_COUPLED_COLLISION_OBSERVATION;GE_ENABLE_COUPLED_SOLVER_REPLAY;"
    replacements = []
    if mode == "coupled":
        for name in ("main.cpp", "RbsFrame.cpp"):
            overlay = BASE / "overlays/current/RigidBodySimulation/src" / name
            assert overlay.exists(), str(overlay)
            replacements.append(("RigidBodySimulation", "src\\" + name, overlay))
    if mode == "qualification":
        # Preserve canonical owner bytes. Only isolated bootstrap asset location
        # changes; the original qualification CLI, scheduler and workload remain.
        canonical = (ROOT / "RigidBodySimulation/src/main.cpp").read_text()
        original = '        settings.engine.assetRootUtf8=(std::filesystem::absolute(args[0]).parent_path().parent_path()/"assets").generic_string();'
        assert canonical.count(original) == 1, "qualification asset binding needs review"
        replacement = ('#ifdef _DEBUG\n        settings.engine.assetRootUtf8="' +
            (ROOT / "bin/Debug/assets").as_posix() + '";\n#else\n        settings.engine.assetRootUtf8="' +
            (ROOT / "bin/Release/assets").as_posix() + '";\n#endif')
        overlay = BASE / "overlays/qualification/RigidBodySimulation/src/main.cpp"
        overlay.parent.mkdir(parents=True, exist_ok=True)
        content = canonical.replace(original, replacement)
        if overlay.exists():
            assert overlay.read_text() == content, "qualification asset overlay drift"
        else:
            with overlay.open("x", encoding="utf-8", newline="\n") as stream: stream.write(content)
        replacements.append(("RigidBodySimulation", "src\\main.cpp", overlay))
    if role == "baseline":
        for name in ("GJK.cpp", "PhysicsSystem.cpp"):
            overlay = BASE / "overlays/baseline/GEngine/include/GEngine/Physics" / name
            assert overlay.exists(), str(overlay)
            replacements.append(("GEngine", "include\\GEngine\\Physics\\" + name, overlay))
    body = ['<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">', '<PropertyGroup>',
        f'<OutDir>{ROOT.as_posix()}/bin/$(Configuration)/{product}/$(MSBuildProjectName)/</OutDir>',
        f'<IntDir>{ROOT.as_posix()}/bin-int/$(Configuration)/{product}/$(MSBuildProjectName)/</IntDir>',
        '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild>', '</PropertyGroup>', '<ItemDefinitionGroup>',
        f'<ClCompile><PreprocessorDefinitions>{definitions}%(PreprocessorDefinitions)</PreprocessorDefinitions>',
        '<AdditionalIncludeDirectories>' + ROOT.as_posix() + '/RigidBodySimulation/src;' + ROOT.as_posix() +
        '/RigidBodySimulation/tests;' + ROOT.as_posix() + '/GEngine/include/GEngine/Physics;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>',
        '</ClCompile></ItemDefinitionGroup>']
    for project, original, overlay in replacements:
        body.append(f'<ItemGroup Condition="\'$(MSBuildProjectName)\'==\'{project}\'">'
            f'<ClCompile Remove="{original}"/><ClCompile Include="{escape(overlay.as_posix())}"/></ItemGroup>')
    body.append('</Project>')
    p = folder / "isolated.props"
    p.write_text("\n".join(body) + "\n", encoding="utf-8")
    return p, product


def build(role, mode, configuration, limit):
    a = authority(); used = usage()
    if role == "baseline":
        for path in ("GEngine/include/GEngine/Physics/Manifold.cpp", "GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp", "GEngine/include/GEngine/Physics/BoxContact.h", "GEngine/include/GEngine/Physics/PhysicsBody.cpp"):
            assert sha(ROOT / path) == a["baseline_source_hashes"][path], "baseline overlay does not cover changed response source: " + path
    limit = min(float(limit), a["prospective_limits"]["build_seconds"] - used["build_seconds"])
    assert limit > 10, "build budget exhausted"
    folder = fresh("builds", f"{configuration}-{role}-{mode}")
    write_new(folder / "invocation.json", {"purpose": "build", "started_utc": now(), "role": role,
        "mode": mode, "configuration": configuration, "limit_seconds": limit, "usage_before": used,
        "authority_sha256": sha(AUTH)})
    started = time.monotonic(); errors = []; execution = None; frozen = None; products = {}; dependencies = {}
    try:
        props, product = make_props(folder, role, mode, configuration)
        props_hash = sha(props)
        frozen = inputs(role, mode); write_new(folder / "inputs.json", frozen)
        common = [f"/p:Configuration={configuration}", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
            "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:ForceImportBeforeCppTargets=" + str(props), "/nr:false", "/nologo"]
        for project in ("GEngine/GEngine.vcxproj", "external/glad/glad.vcxproj", "RigidBodySimulation/RigidBodySimulation.vcxproj"):
            logpath = folder / (Path(project).stem + "-evaluation.log")
            command = [str(legacy.MSBUILD), str(ROOT / project), *common,
                "/getProperty:OutDir,IntDir,TargetPath,PostBuildEventUseInBuild,VCToolsVersion,WindowsTargetPlatformVersion",
                "/getItem:ClCompile,ProjectReference"]
            with logpath.open("wb") as log:
                evaluated = legacy.remediation_owned_build(command, environment(), log, min(30., limit - (time.monotonic() - started)))
            write_new(logpath.with_suffix(".json"), evaluated)
            assert evaluated["exit_code"] == 0 and not evaluated["timed_out"] and not evaluated["error"], evaluated
            evaluated_json = json.loads(logpath.read_text(encoding="utf-8-sig")); values = evaluated_json["Properties"]
            for key in ("OutDir", "IntDir", "TargetPath"):
                assert product.replace("/", "\\") in str(contained(values[key])).replace("/", "\\"), (key, values[key])
            assert values["PostBuildEventUseInBuild"] == "false"
            assert values["VCToolsVersion"] == "14.44.35207" and values["WindowsTargetPlatformVersion"] == "10.0.26100.0"
            items = evaluated_json["Items"]["ClCompile"]
            bound = {str((ROOT / p).resolve()).casefold() for p in frozen["workspace"]}
            assert all(str(Path(item["FullPath"]).resolve()).casefold() in bound for item in items), "unbound compilation source"
            references = {str(Path(item["FullPath"]).resolve()).casefold() for item in evaluated_json["Items"].get("ProjectReference", [])}
            wanted_refs = {str((ROOT / p).resolve()).casefold() for p in ("GEngine/GEngine.vcxproj", "external/glad/glad.vcxproj")} if Path(project).stem == "RigidBodySimulation" else set()
            assert references == wanted_refs, "project dependency drift"
            originals = []
            if Path(project).stem == "RigidBodySimulation" and mode == "coupled": originals = ["main.cpp", "RbsFrame.cpp"]
            if Path(project).stem == "RigidBodySimulation" and mode == "qualification": originals = ["main.cpp"]
            if Path(project).stem == "GEngine" and role == "baseline": originals = ["GJK.cpp", "PhysicsSystem.cpp"]
            for name in originals:
                selected = [item for item in items if Path(item["FullPath"]).name == name]
                assert len(selected) == 1 and (BASE / "overlays") in Path(selected[0]["FullPath"]).parents, (name, selected)
            for item in items:
                defines = item.get("PreprocessorDefinitions", "")
                assert ("GE_ENABLE_PHYSICS_PROFILING" in defines) == (mode != "normal" and configuration == "Release")
                assert ("GE_ENABLE_COUPLED_COLLISION_OBSERVATION" in defines) == (mode == "coupled")
                assert ("GE_ENABLE_COUPLED_SOLVER_REPLAY" in defines) == (mode == "coupled")
        command = [str(legacy.MSBUILD), str(ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"), *common,
            "/t:Build", "/m:2", "/v:normal", "/bl:" + str(folder / "build.binlog")]
        write_new(folder / "command.json", command)
        command_hash = sha(folder / "command.json")
        with (folder / "build.log").open("wb") as log:
            execution = legacy.remediation_owned_build(command, environment(), log, limit - (time.monotonic() - started))
        assert execution["exit_code"] == 0 and not execution["timed_out"] and not execution["error"], execution
        assert inputs(role, mode) == frozen, "build inputs changed during execution"
        assert sha(props) == props_hash and sha(folder / "command.json") == command_hash
        for name, extension in (("GEngine", "lib"), ("glad", "lib"), ("RigidBodySimulation", "exe")):
            p = ROOT / f"bin/{configuration}/{product}/{name}/{name}.{extension}"
            products[p.relative_to(ROOT).as_posix()] = sha(p)
        target = ROOT / f"bin/{configuration}/{product}/RigidBodySimulation"
        for dll in sorted((ROOT / f"bin/{configuration}/RigidBodySimulation").glob("*.dll")):
            shutil.copy2(dll, target / dll.name)
            dependencies[(target / dll.name).relative_to(ROOT).as_posix()] = sha(target / dll.name)
        for name in ("gepch.obj", "PhysicsProfile.obj", "GJK.obj", "PhysicsSystem.obj", "ConstraintPenetration.obj"):
            p = ROOT / f"bin-int/{configuration}/{product}/GEngine/{name}"
            products[p.relative_to(ROOT).as_posix()] = sha(p)
        authority()
    except BaseException as e:
        errors.append(repr(e))
    duration = time.monotonic() - started
    result = {"status": "PASS" if not errors and duration <= limit else "FAIL", "completed_utc": now(),
        "role": role, "mode": mode, "configuration": configuration, "wall_seconds": duration,
        "limit_seconds": limit, "errors": errors, "execution": execution, "products": products,
        "runtime_dependencies": dependencies, "inputs_sha256": sha(folder / "inputs.json") if frozen else None,
        "props_sha256": sha(folder / "isolated.props") if (folder / "isolated.props").exists() else None,
        "command_sha256": sha(folder / "command.json") if (folder / "command.json").exists() else None}
    write_new(folder / "result.json", result)
    update_ledger()
    print(json.dumps({"result": str(folder / "result.json"), **result, "usage": usage()}, indent=2), flush=True)
    if result["status"] != "PASS": raise SystemExit(1)


class NativeOwner(legacy._RemediationBuildOwner):
    """Same held job/process handles as build owner; explicit runtime cwd."""
    def __init__(self, audit, cwd, expected_identity=None):
        super().__init__(audit); self.cwd = cwd; self.expected_identity = expected_identity

    def start(self, command, env, log):
        import msvcrt
        api = self.winapi; duplicates = []
        try:
            with open(os.devnull, "rb") as stdin:
                own = api.GetCurrentProcess()
                for stream in (stdin, log):
                    duplicates.append(api.DuplicateHandle(own, msvcrt.get_osfhandle(stream.fileno()),
                        own, 0, True, api.DUPLICATE_SAME_ACCESS))
                startup = subprocess.STARTUPINFO(dwFlags=api.STARTF_USESTDHANDLES,
                    hStdInput=duplicates[0], hStdOutput=duplicates[1], hStdError=duplicates[1],
                    lpAttributeList={"handle_list": duplicates.copy()})
                self.process, self.thread, pid, tid = api.CreateProcess(command[0], subprocess.list2cmdline(command),
                    None, None, True, 0x4 | subprocess.NORMAL_PRIORITY_CLASS | api.CREATE_NO_WINDOW,
                    env, str(self.cwd), startup)
                self.audit.update(pid=pid, thread_id=tid, created_suspended=True, cwd=str(self.cwd), owner_visible_validation=False)
        finally:
            for handle in duplicates: api.CloseHandle(handle)
        self.checked(self.kernel.AssignProcessToJobObject(self.job, self.process))
        self.assigned = True; self.audit["assigned_before_resume"] = True
        self.audit["native_identity"] = legacy.remediation_process_identity(SimpleNamespace(_handle=self.process))
        if self.expected_identity is not None:
            assert self.audit["native_identity"] == self.expected_identity, "Control02 priority/affinity differs before resume"


def run_coupled(build_result, scene, ticks, limit, replay_tick=0):
    a = authority(); used = usage()
    assert used["native_launches"] < 30
    limit = min(float(limit), 180., 4500. - used["native_seconds"])
    assert limit > 10
    assert scene in ("SphereLattice", "BoxStack", "SphereBoxStack") and 1 <= ticks <= 1200
    assert replay_tick == 0 or (replay_tick == 603 and scene == "SphereBoxStack" and ticks == 610)
    build_result = contained(build_result); built = json.loads(build_result.read_text())
    assert built["status"] == "PASS" and built["configuration"] == "Release" and built["mode"] == "coupled"
    for name, key in (("inputs.json", "inputs_sha256"), ("isolated.props", "props_sha256"), ("command.json", "command_sha256")):
        assert sha(build_result.with_name(name)) == built[key], "build metadata drift: " + name
    expected = json.loads(build_result.with_name("inputs.json").read_text())
    assert compilation_inputs(inputs(built["role"], built["mode"])) == compilation_inputs(expected), "stale diagnostic build"
    assert all(sha(ROOT / p) == v for group in ("products", "runtime_dependencies") for p, v in built[group].items())
    probe = subprocess.run(["powershell", "-NoProfile", "-Command", "@(Get-Process RigidBodySimulation -ErrorAction SilentlyContinue).Count"], capture_output=True, text=True, check=True)
    assert int(probe.stdout.strip()) == 0, "existing RBS process: no intervention"
    folder = fresh("native", f"{built['role']}-{scene}"); data = folder / "data"; data.mkdir()
    cwd = contained(ROOT / "runtime/RigidBodySimulation/Release/Phase20Comprehensive" / folder.name); cwd.mkdir(parents=True)
    exe = next(ROOT / p for p in built["products"] if p.endswith(".exe"))
    command = [str(exe), "--coupled-physics-diagnostic", scene, "--output", str(data), "--ticks", str(ticks)]
    if replay_tick: command += ["--solver-replay-tick", str(replay_tick)]
    machine = legacy.remediation_machine()
    write_new(folder / "invocation.json", {"purpose": "coupled", "started_utc": now(), "command": command,
        "cwd": str(cwd), "limit_seconds": limit, "usage_before": used, "build_result": str(build_result),
        "build_result_sha256": sha(build_result), "prospective_launch": used["native_launches"] + 1,
        "authority_sha256": sha(AUTH), "machine_before": machine, "owner_visible_validation": False, "automatic_retry": False,
        "cloned_replay_tick": replay_tick})
    started = time.monotonic(); execution = None; errors = []; native = None
    try:
        env = environment(); env["GENGINE_ASSET_ROOT"] = str(ROOT / "bin/Release/assets")
        with (folder / "console.log").open("wb") as log:
            execution = legacy.remediation_owned_build(command, env, log, limit,
                _factory=lambda audit: NativeOwner(audit, cwd))
        result_path = data / "result.json"
        native = json.loads(result_path.read_text()) if result_path.exists() else None
        assert compilation_inputs(inputs(built["role"], built["mode"])) == compilation_inputs(expected), "native source/input drift"
        authority()
    except BaseException as e:
        errors.append(repr(e))
    evidence = {}; exported = 0
    try:
        for name in ("GEngine.log", "imgui.ini"):
            if (cwd / name).exists(): shutil.copy2(cwd / name, folder / ("runtime-" + name))
        exported = sum(p.stat().st_size for p in data.rglob("*") if p.is_file())
        evidence = {p.relative_to(folder).as_posix(): sha(p) for p in folder.rglob("*") if p.is_file()}
    except BaseException as e:
        errors.append("evidence finalization: " + repr(e))
    duration = time.monotonic() - started
    ok = bool(execution and execution["exit_code"] == 0 and not execution["timed_out"] and not execution["error"] and
        not errors and native and native.get("capture_complete") and not native.get("technical_failure", True) and
        native.get("shutdown") and native.get("preset") == scene and native.get("requested_ticks") == ticks and
        ticks <= native.get("ticks", -1) <= ticks + 1 and
        native.get("cloned_replay_tick") == replay_tick and
        (not (ticks == 1200 and scene == "SphereBoxStack") or
            (isinstance(native.get("contact_lifecycle_fixtures"), dict) and
             native["contact_lifecycle_fixtures"].get("complete") is True and
             native["contact_lifecycle_fixtures"].get("good") is True)) and
        (not replay_tick or (isinstance(native.get("cloned_replay"), dict) and
            native["cloned_replay"].get("complete") is True and native["cloned_replay"].get("good") is True)) and
        all(isinstance(native.get(name), dict) and native[name].get("buffered_logical_bytes") == 0
            and native[name].get("physical_bytes", 0) >= 8
            and native[name].get("logical_records", 0) > 0
            for name in ("physics_stream", "query_stream")) and
        0 < native.get("owned_scratch_bytes", 0) <= 64*1024*1024 and
        0 <= native.get("peak_query_bytes", -1) <= 4*1024*1024 and
        0 <= native.get("query_export_bytes", -1) <= 8388608 and exported <= 8388608 and duration <= limit)
    result = {"status": "PASS_CAPTURE_INTEGRITY_ANALYSIS_PENDING" if ok else "FAIL_OR_INCONCLUSIVE_PRESERVED", "completed_utc": now(),
        "wall_seconds": duration, "errors": errors, "execution": execution, "native": native,
        "exports_bytes": exported, "owner_visible_validation": False,
        "evidence": evidence}
    write_new(folder / "result.json", result)
    update_ledger()
    print(json.dumps({"result": str(folder / "result.json"), "status": result["status"], "wall_seconds": duration,
        "native": native, "errors": errors, "usage": usage()}, indent=2), flush=True)
    if not ok: raise SystemExit(1)



def run_qualification(build_result, prelaunch_proof):
    """One original fixed candidate attempt; offline acceptance is a separate command."""
    a = authority(); used = usage(); limit = 120.
    assert used["native_launches"] < 30 and used["qualification_attempts"] < 4
    assert used["native_seconds"] + limit <= 4500., "full original120second reservation unavailable"
    build_result, prelaunch_proof = contained(build_result), contained(prelaunch_proof)
    assert build_result.is_relative_to(BASE / "builds") and prelaunch_proof.is_relative_to(BASE / "proofs")
    built = json.loads(build_result.read_text()); proof = json.loads(prelaunch_proof.read_text())
    assert (built["status"], built["role"], built["mode"], built["configuration"]) == ("PASS", "current", "qualification", "Release")
    for name, key in (("inputs.json", "inputs_sha256"), ("isolated.props", "props_sha256"), ("command.json", "command_sha256")):
        assert sha(build_result.with_name(name)) == built[key], "build metadata drift: " + name
    expected = json.loads(build_result.with_name("inputs.json").read_text())
    assert compilation_inputs(inputs("current", "qualification")) == compilation_inputs(expected), "stale qualification build"
    assert all(sha(ROOT / p) == h for group in ("products", "runtime_dependencies") for p, h in built[group].items())
    assert proof["status"] == "PASS_MANDATORY_CANDIDATE_PRELAUNCH_GATES"
    for group in ("artifacts", "evidence", "source_hashes"):
        assert proof[group] and all(sha(contained(ROOT / p)) == h for p, h in proof[group].items()), "prelaunch binding drift: " + group
    for flag in ("exact_grid_equivalence", "complete_friction_policy", "light_path_unchanged", "control02_toolchain_options_comparable"):
        assert proof[flag] is True, "prelaunch obligation incomplete: " + flag
    def reference(ref):
        path = contained(ROOT / ref["path"])
        assert sha(path) == ref["sha256"], "prelaunch reference drift: " + str(path)
        return json.loads(path.read_text())
    for configuration in ("Debug", "Release"):
        ref = proof["build_results"][configuration]; product = reference(ref)
        assert (product["status"], product["role"], product["mode"], product["configuration"]) == ("PASS", "current", "qualification", configuration)
        assert all(sha(ROOT / p) == h for group in ("products", "runtime_dependencies") for p, h in product[group].items())
        frozen = json.loads((ROOT / ref["path"]).with_name("inputs.json").read_text())
        assert sha((ROOT / ref["path"]).with_name("inputs.json")) == product["inputs_sha256"]
        assert compilation_inputs(inputs("current", "qualification")) == compilation_inputs(frozen), "stale required build"
        if configuration == "Release": assert ref["sha256"] == sha(build_result)
    stack = reference(proof["stack_proof"]); storage = reference(proof["storage_proof"])
    assert stack["status"] == "PASS_COMPILED_DEBUG_RELEASE_ADDITIONAL_CAPACITY" and stack["gate_bytes"] == 4096
    assert all(0 <= stack[c]["added_capacity_upper_bytes"] <= 4096 for c in ("debug_public", "release_public"))
    assert storage["status"] == "PASS_COMPILED_STORAGE_BOUND_AND_SOURCE_ALLOCATION_REVIEW"
    bounds = storage["bounds"]
    assert bounds["accepted_total_named_scratch_limit_bytes"] == 67108864 and bounds["separate_query_projection_limit_bytes"] == 4194304
    assert bounds["accepted_export_limit_bytes"] == 8388608 and 0 < bounds["post_candidate_audit_bytes"] <= 1048576
    profile = reference(proof["profiling_proof"])
    assert profile["status"] == "PASS_COMPILED_EXISTING_PROFILER_ENABLED" and profile["build_result_sha256"] == sha(build_result)
    assert all(profile[group] and all(sha(contained(ROOT / p)) == h for p, h in profile[group].items()) for group in ("artifacts", "evidence"))
    control_path = ROOT / "logs/pre_editor/phase-20/conditional-remediation-01/runs/Release-control-02/result.json"
    assert sha(control_path) == "00a915b5010bb66fa95d0b9525487c57ce2226255614c4218e3e3af218c05451"
    control = json.loads(control_path.read_text())
    machine = legacy.remediation_machine()
    assert legacy.remediation_machine_identity(machine) == legacy.remediation_machine_identity(control["environment"]["machine_before"]), "Control02 CPU/topology/power differs before launch"
    probe = subprocess.run(["powershell", "-NoProfile", "-Command", "@(Get-Process RigidBodySimulation -ErrorAction SilentlyContinue).Count"], capture_output=True, text=True, check=True)
    assert int(probe.stdout.strip()) == 0, "existing RBS process: no intervention"
    assert sha(legacy.FOCUSED_LATTICE) == legacy.FOCUSED_LATTICE_HASH
    folder = fresh("native", "current-qualification"); data = folder / "data"; data.mkdir()
    cwd = contained(ROOT / "runtime/RigidBodySimulation/Release/Phase20Comprehensive" / folder.name); cwd.mkdir(parents=True)
    request = folder / "focused-request.txt"
    request_text = ("P20_POST_CANDIDATE_FOCUSED_V1\ncandidate\n" + legacy.FOCUSED_LATTICE_HASH + "\n" +
        legacy.REPLAY_INPUT_HASH + "\n25 50 16 12 4 11 4980 68 120\nEND\n")
    with request.open("x", encoding="ascii", newline="\n") as stream: stream.write(request_text)
    exe = next(ROOT / p for p in built["products"] if p.endswith(".exe"))
    command = [str(exe), "--resting-contact-diagnostic", "on", "--lattice", str(legacy.FOCUSED_LATTICE), "--output", str(data)]
    binding = {"request_sha256": sha(request), "build_result_sha256": sha(build_result),
        "prelaunch_proof_sha256": sha(prelaunch_proof), "runner_sha256": sha(__file__), "authority_sha256": sha(AUTH)}
    environment_record = {"machine_before": machine, "owner_visible_validation": False}
    env = environment(); env[legacy.FOCUSED_ENV] = str(request); env["GENGINE_ASSET_ROOT"] = str(ROOT / "bin/Release/assets")
    write_new(folder / "invocation.json", {"purpose": "qualification", "started_utc": now(), "command": command,
        "cwd": str(cwd), "limit_seconds": limit, "usage_before": used, "build_result": str(build_result),
        "prelaunch_proof": str(prelaunch_proof), **binding, "prospective_launch": used["native_launches"] + 1,
        "prospective_qualification_attempt": used["qualification_attempts"] + 1, "environment": environment_record,
        "owner_visible_validation": False, "automatic_retry": False})
    execution = None; errors = []; native = None; stable = False; evidence = {}; exported = 0
    started = time.monotonic()
    try:
        with (folder / "console.log").open("wb") as log:
            execution = legacy.remediation_owned_build(command, env, log, limit - (time.monotonic() - started),
                _factory=lambda audit: NativeOwner(audit, cwd, control["environment"]["native_process"]))
    except BaseException as error:
        errors.append("owned execution: " + repr(error))
    duration = time.monotonic() - started
    # Wall charge is the owned native lifetime, including setup/termination;
    # offline hashing/analysis is outside the original native timing protocol.
    try:
        environment_record["machine_after"] = legacy.remediation_machine()
        environment_record["stable_machine_identity"] = legacy.remediation_machine_identity(machine) == legacy.remediation_machine_identity(environment_record["machine_after"])
    except BaseException as error:
        environment_record["stable_machine_identity"] = False
        errors.append("machine after: " + repr(error))
    environment_record["native_process"] = (execution or {}).get("ownership", {}).get("native_identity")
    environment_record["native_process_error"] = None if environment_record["native_process"] is not None else "missing owned process identity"
    try:
        result_path = data / "result.json"; native = json.loads(result_path.read_text()) if result_path.exists() else None
        stable = compilation_inputs(inputs("current", "qualification")) == compilation_inputs(expected)
        assert stable and all(sha(ROOT / p) == h for group in ("products", "runtime_dependencies") for p, h in built[group].items()), "native source/product drift"
        assert sha(request) == binding["request_sha256"] and request.read_text() == request_text
        assert sha(build_result) == binding["build_result_sha256"] and sha(prelaunch_proof) == binding["prelaunch_proof_sha256"]
        assert sha(__file__) == binding["runner_sha256"] and sha(AUTH) == binding["authority_sha256"]
        authority()
    except BaseException as error:
        stable = False
        errors.append("native input binding: " + repr(error))
    try:
        for name in ("GEngine.log", "imgui.ini"):
            if (cwd / name).exists(): shutil.copy2(cwd / name, folder / ("runtime-" + name))
        exported = sum(p.stat().st_size for p in data.rglob("*") if p.is_file())
        evidence = {p.relative_to(folder).as_posix(): sha(p) for p in folder.rglob("*") if p.is_file()}
    except BaseException as error:
        errors.append("evidence finalization: " + repr(error))
    fixed_counts = {"episodes": 11, "ticks": 4980, "collision_calls": 50, "direct_gjk_calls": 16,
        "ca_helper_calls": 12, "projection_helper_calls": 4, "profiled_ticks": 4980, "post_ticks": 1680,
        "post_stage_records": 288, "physical_failures": 0}
    ok = bool(execution and execution["exit_code"] == 0 and not execution["timed_out"] and not execution["error"] and
        not errors and stable and environment_record["stable_machine_identity"] and native and
        all(native.get(k) == v for k, v in fixed_counts.items()) and native.get("complete") and native.get("shutdown") and
        native.get("conditional") and native.get("candidate") and native.get("profiling_enabled") and native.get("physics_world_ns", 0) > 0 and
        native.get("post_candidate_policy") and not native.get("overflow", True) and not native.get("failure") and
        0 < native.get("post_event_records", 0) <= 1024 and 0 < native.get("post_storage_bytes", 0) <= 1048576 and
        native.get("post_sliding", 0) > 0 and exported <= 8388608 and duration <= limit)
    result = {"status": "PASS_FOCUSED_NATIVE_INTEGRITY_OFFLINE_VALIDATION_PENDING" if ok else "FAIL_OR_INCONCLUSIVE_PRESERVED",
        "completed_utc": now(), "wall_seconds": duration, "errors": errors, "execution": execution,
        "native": native, "stable_inputs": stable, "exports_bytes": exported, "environment": environment_record,
        "owner_visible_validation": False, "evidence": evidence, "physics_acceptance_claim": False}
    write_new(folder / "result.json", result); update_ledger()
    print(json.dumps({"result": str(folder / "result.json"), "status": result["status"], "wall_seconds": duration,
        "errors": errors, "usage": usage(), "offline_analysis_required": True}, indent=2), flush=True)
    if not ok: raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser(); sub = parser.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build"); b.add_argument("--role", choices=("current", "baseline"), required=True)
    b.add_argument("--mode", choices=("coupled", "qualification", "normal"), default="coupled")
    b.add_argument("--configuration", choices=("Debug", "Release"), required=True); b.add_argument("--limit", type=float, default=600)
    n = sub.add_parser("run"); n.add_argument("--build-result", type=Path, required=True); n.add_argument("--scene", required=True)
    n.add_argument("--ticks", type=int, default=900); n.add_argument("--limit", type=float, default=180)
    n.add_argument("--solver-replay-tick", type=int, choices=(0, 603), default=0)
    q = sub.add_parser("qualify"); q.add_argument("--build-result", type=Path, required=True)
    q.add_argument("--prelaunch-proof", type=Path, required=True)
    sub.add_parser("usage")
    args = parser.parse_args()
    if args.command == "usage":
        print(json.dumps(usage(), indent=2)); return
    lock = BASE / "execution.lock"
    with lock.open("x", encoding="utf-8") as stream:
        stream.write(json.dumps({"pid": os.getpid(), "started_utc": now(), "command": args.command}))
    try:
        if args.command == "build": build(args.role, args.mode, args.configuration, args.limit)
        elif args.command == "run": run_coupled(args.build_result, args.scene, args.ticks, args.limit, args.solver_replay_tick)
        elif args.command == "qualify": run_qualification(args.build_result, args.prelaunch_proof)
    finally:
        lock.unlink()


if __name__ == "__main__":
    main()
