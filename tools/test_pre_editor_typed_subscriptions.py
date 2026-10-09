"""Phase 13 typed subscription lifetime, scale bridge and bounded owner-loop observations."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "logs/pre_editor/phase-13/candidate-04"
PROJECTS = ["GEngine/GEngine.vcxproj", "external/glad/glad.vcxproj", "RigidBodySimulation/RigidBodySimulation.vcxproj"]
MSBUILD = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/amd64/MSBuild.exe")


def local(value):
    path = Path(value)
    if not path.is_absolute():
        path = ROOT / path
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError("Output escapes PRE_EDITOR: " + str(path))
    for ancestor in (path, *path.parents):
        if ancestor == ROOT.parent:
            break
        if ancestor.exists() and ancestor.lstat().st_file_attributes & 0x400:
            raise ValueError("Reparse ancestor: " + str(ancestor))
    return path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def identity(path):
    return {"sha256": digest(path), "size": Path(path).stat().st_size}


def save(path, value):
    path = local(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def environment():
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("GENGINE_", "SDL_", "ASAN_"))}
    temporary = local("runtime/tmp")
    temporary.mkdir(parents=True, exist_ok=True)
    env.update(TMP=str(temporary), TEMP=str(temporary), PYTHONDONTWRITEBYTECODE="1",
               GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="safe.directory", GIT_CONFIG_VALUE_0=ROOT.as_posix())
    return env


def compile_members():
    result = []
    for project in PROJECTS:
        for node in ET.parse(ROOT / project).findall(".//{http://schemas.microsoft.com/developer/msbuild/2003}ClCompile"):
            if node.get("Include"):
                path = local((ROOT / project).parent / node.get("Include")).resolve()
                if path.is_file():
                    result.append(path.relative_to(ROOT).as_posix())
    return sorted(result)


def discovery():
    return {scope: sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / scope).glob(pattern) if p.is_file())
            for scope, pattern in [("GEngine/src", "**/*.cpp"), ("RigidBodySimulation/src", "**/*.cpp"), ("external/glad/src", "**/*.c")]}


def verify(manifest):
    for name, expected in manifest["input_hashes"].items():
        if digest(local(name)) != expected:
            raise RuntimeError("Changed relevant input: " + name)
    for name, expected in manifest["external_inputs"].items():
        if identity(name) != expected:
            raise RuntimeError("Changed external input: " + name)
    if compile_members() != manifest["compile_members"] or discovery() != manifest["compile_discovery"]:
        raise RuntimeError("Changed build membership")
    for path in manifest["include_preceding_absent"]:
        if (ROOT / path).exists():
            raise RuntimeError("New include resolution candidate: " + path)


def build(cfg):
    manifest = json.loads((OUT / "scenario-manifest.json").read_text()); verify(manifest)
    folder = local(OUT / f"build-{cfg}"); folder.mkdir()
    common = [f"/p:Configuration={cfg}", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
              "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:ForceImportBeforeCppTargets=" + str(OUT / "isolated.props")]
    for project in PROJECTS:
        command = [str(MSBUILD), project, *common, "/getProperty:OutDir,IntDir,TargetPath,PostBuildEventUseInBuild,VCToolsVersion,WindowsTargetPlatformVersion", "/nologo"]
        result = subprocess.run(command, cwd=ROOT, env=environment(), capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
        save(folder / (Path(project).stem + "-evaluation.json"), {"command": command, "exit": result.returncode, "stdout": result.stdout.decode(errors="replace"), "stderr": result.stderr.decode(errors="replace")})
        assert result.returncode == 0
        properties = json.loads(result.stdout)["Properties"]
        assert Path(properties["OutDir"]).resolve() == ROOT / f"bin/{cfg}/Phase13Candidate04/{Path(project).stem}"
        assert Path(properties["IntDir"]).resolve() == ROOT / f"bin-int/{cfg}/Phase13Candidate04/{Path(project).stem}"
        assert properties["PostBuildEventUseInBuild"] == "false"
        assert properties["VCToolsVersion"] == "14.44.35207" and properties["WindowsTargetPlatformVersion"] == "10.0.26100.0"
    reuse = manifest["glad_reuse"][cfg]
    assert identity(ROOT / reuse["source"]) == reuse["identity"]
    library = local(f"bin/{cfg}/Phase13Candidate04/glad/glad.lib")
    library.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / reuse["source"], library)
    engine_reuse = manifest["engine_debug_reuse"] if cfg == "Debug" else None
    elapsed_before = engine_reuse["prior_total_build_seconds"] if engine_reuse else 0
    if engine_reuse:
        assert identity(ROOT / engine_reuse["source"]) == engine_reuse["identity"]
        assert identity(ROOT / engine_reuse["build_evidence"]) == engine_reuse["build_evidence_identity"]
        library = local(f"bin/{cfg}/Phase13Candidate04/GEngine/GEngine.lib")
        library.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / engine_reuse["source"], library)
    affected_projects = [PROJECTS[-1]] if engine_reuse else [PROJECTS[0], PROJECTS[-1]]
    commands = [[str(MSBUILD), project, "/t:Build", *common, "/p:BuildProjectReferences=false", "/m:2", "/nr:false", "/nologo", "/v:normal",
                 "/bl:" + str(folder / (Path(project).stem + ".binlog"))] for project in affected_projects]
    report = {"commands": commands, "reused_glad": reuse, "reused_engine": engine_reuse, "prior_seconds": elapsed_before, "cwd": str(ROOT), "recorded_utc": utc(), "result": "FAIL", "exits": []}
    started = time.monotonic()
    try:
        with (folder / "build.log").open("xb") as stream:
            for command in commands:
                result = subprocess.run(command, cwd=ROOT, env=environment(), stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=max(1, 1800 - elapsed_before - (time.monotonic() - started)), creationflags=subprocess.CREATE_NO_WINDOW)
                report["exits"].append(result.returncode)
                assert result.returncode == 0, "Required affected build failed"
        verify(manifest)
        products = [f"bin/{cfg}/Phase13Candidate04/{name}/{name}.{extension}" for name, extension in [("GEngine", "lib"), ("glad", "lib"), ("RigidBodySimulation", "exe")]]
        report.update(result="PASS", products={p: identity(ROOT / p) for p in products})
    except Exception as error:
        report["reason"] = str(error)
    finally:
        report["seconds"] = time.monotonic() - started
        save(folder / "result.json", report)
    print(json.dumps(report), flush=True)
    if report["result"] != "PASS":
        raise SystemExit(1)


def stage(cfg):
    import postbuild
    manifest = json.loads((OUT / "scenario-manifest.json").read_text()); verify(manifest)
    assert json.loads((OUT / f"build-{cfg}/result.json").read_text())["result"] == "PASS"
    case = manifest["cases"][cfg]
    executable = local(case["executable"]); destination = executable.parent; assets = local(case["asset_root"])
    assert not assets.exists()
    for name in postbuild.runtime_dlls(cfg, "RigidBodySimulation"):
        source = ROOT / f"bin/{cfg}/RigidBodySimulation" / name
        if name == "assimp-vc140-mt.dll":
            source = ROOT / postbuild.ASSIMP_RUNTIME_SOURCE
        target = local(destination / name)
        assert not target.exists()
        shutil.copy2(source, target)
        assert digest(source) == digest(target)
    # TBB runtimes are loaded from the pinned external directory, as in the predecessor.
    postbuild.stage_runtime_assets(ROOT, assets)
    postbuild.stage_assimp_runtime(ROOT, destination)
    directory = local(case["cwd"]); directory.mkdir(parents=True, exist_ok=False)
    (directory / "imgui.ini").write_text(case["layout"], encoding="utf-8")
    modules = {}
    for name, old in case["expected_modules"].items():
        source = Path(old["path"])
        selected = destination / source.name
        if not selected.exists():
            selected = source if source.is_absolute() else ROOT / source
        assert digest(selected) == old["sha256"], str(selected)
        modules[name] = {"path": str(selected.resolve()), "sha256": digest(selected)}
    bundle = {p.relative_to(ROOT).as_posix(): identity(p) for base in (destination, assets) for p in base.rglob("*") if p.is_file() and p.suffix != ".pdb"}
    for path, source in manifest["asset_members"].items():
        assert digest(assets / path) == digest(ROOT / source)
    save(OUT / f"runtime-admission-{cfg}.json", {"recorded_utc": utc(), "binary": identity(executable), "modules": modules,
         "bundle": bundle, "initial_layout": identity(directory / "imgui.ini"), "manifest": identity(OUT / "scenario-manifest.json")})
    print(cfg + " runtime closure staged and verified", flush=True)


def smoke(executable, directory, env, modules, markers, performance, deadline):
    import ctypes
    from ctypes import wintypes
    from test_frame_submission import loaded_modules
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    started = time.monotonic()
    report = dict(result="FAIL", command=[str(executable)], cwd=str(directory), binary=identity(executable), recorded_utc=utc())
    with (directory / "application.log").open("xb") as stream:
        child = subprocess.Popen([str(executable)], cwd=directory, env=env, stdout=stream, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        report["pid"] = child.pid
        watchdog = threading.Timer(max(.001, deadline-time.monotonic()), lambda: child.kill() if child.poll() is None else None); watchdog.start()
        owned = []
        @callback
        def find_window(hwnd, _):
            pid=wintypes.DWORD(); user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
            name=ctypes.create_unicode_buffer(256); title=ctypes.create_unicode_buffer(256)
            user.GetClassNameW(hwnd,name,256);user.GetWindowTextW(hwnd,title,256)
            if pid.value==child.pid and name.value=="SDL_app" and title.value and (performance or user.IsWindowVisible(hwnd)):
                owned.append((hwnd,title.value))
            return True
        try:
            selected = None
            while time.monotonic() < min(started+150, deadline-30) and child.poll() is None:
                text = "\n".join(p.read_text(encoding="utf-8",errors="replace") for p in (directory/"application.log",directory/"GEngine.log") if p.exists())
                assert "PRE_EDITOR_PHASE_13_FAIL" not in text, "Phase 13 runtime assertion failed"
                if all(marker in text for marker in markers):
                    owned.clear();user.EnumWindows(find_window,0)
                    if owned: selected=owned[0];break
                time.sleep(.1)
            assert selected is not None, "Required completion markers/owned window missing before exit/deadline"
            report["window_title"]=selected[1];report["markers"]={m:True for m in markers}
            loaded=loaded_modules(child.pid)
            report["loaded_modules"]={name:dict(path=path,**identity(path)) for name,path in loaded.items()}
            closure=all(name.lower() in loaded and Path(loaded[name.lower()]).resolve()==Path(v["path"]).resolve() and digest(loaded[name.lower()])==v["sha256"] for name,v in modules.items())
            report["runtime_closure"]="PASS" if closure else "FAIL"
            assert closure, "Runtime dependency identity mismatch"
            close=time.monotonic();report["native_close_posted"]=bool(user.PostMessageW(selected[0],0x0010,0,0));report["close_elapsed_seconds"]=close-started
            assert report["native_close_posted"], "Native close failed"
            report["exit"]=child.wait(timeout=min(30,max(.001,deadline-time.monotonic())))
            report["close_to_exit_seconds"]=time.monotonic()-close
            assert report["exit"]==0, "Nonzero runtime exit"
            report["result"]="PASS"
        except Exception as error: report["reason"]=str(error)
        finally:
            watchdog.cancel()
            if child.poll() is None: child.terminate();child.wait(timeout=10);report["terminated"]=True
            report.setdefault("exit",child.returncode);report["seconds"]=time.monotonic()-started
    return report


def prepare():
    policy = json.loads((ROOT / "docs/pre_editor/active/OUTPUT_PATHS.json").read_text())
    assert Path(policy["root"]).resolve() == ROOT
    local(OUT).mkdir(parents=True, exist_ok=True)
    scope = json.loads((ROOT / "docs/pre_editor/active/PHASE_13_SCOPE_CANDIDATE_04.json").read_text())
    owned = scope["production"] + scope["focused_validation"]
    prior_path = ROOT / "logs/pre_editor/phase-12/candidate-04/scenario-manifest.json"
    prior = json.loads(prior_path.read_text())
    # This is the required RBS/GEngine compile/read dependency closure, not a
    # repository inventory. Changed sources and new direct dependencies are added.
    names = {p.casefold(): p for p in prior["input_hashes"] if not p.lower().startswith(
        ("logs/", "runtime/", "bin-int/", "docs/pre_editor/active/", "docs/pre_editor/phases/"))}
    names = {k: p for k, p in names.items() if not (p.lower().startswith("bin/") and p.lower().endswith((".exe", "/gengine.lib", "/glad.lib")))}
    # Other phase Python campaigns are not consumed by this harness. Historical
    # C++ checks remain included by RBS and therefore remain compile inputs.
    names = {k: p for k, p in names.items() if not p.startswith("tools/test_pre_editor_")}
    props = OUT / "isolated.props"
    with local(props).open("x", encoding="utf-8") as stream:
        stream.write('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n<PropertyGroup>\n'
            f'<OutDir>{ROOT}\\bin\\$(Configuration)\\Phase13Candidate04\\$(MSBuildProjectName)\\</OutDir>\n'
            f'<IntDir>{ROOT}\\bin-int\\$(Configuration)\\Phase13Candidate04\\$(MSBuildProjectName)\\</IntDir>\n'
            '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild>\n</PropertyGroup>\n'
            "<ItemDefinitionGroup Condition=\"'$(MSBuildProjectName)'=='RigidBodySimulation'\"><ClCompile>"
            '<PreprocessorDefinitions>GENGINE_SUBSCRIPTION_NATIVE_OBSERVER;%(PreprocessorDefinitions)</PreprocessorDefinitions>'
            f'<AdditionalIncludeDirectories>{ROOT}/external/glad/include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>'
            '</ClCompile></ItemDefinitionGroup>\n</Project>\n')
    extra = owned + PROJECTS + ["docs/pre_editor/phases/PHASE_13.md", "docs/pre_editor/VALIDATION_CONTRACTS.md",
        "docs/pre_editor/active/PHASE_13_OD05_PROPOSAL.md", "docs/pre_editor/active/PHASE_13_OD05_ACCEPTANCE.json",
        "docs/pre_editor/active/PHASE_13_SCOPE_AMENDMENT_01_PROPOSAL.md",
        "docs/pre_editor/active/PHASE_13_SCOPE_AMENDMENT_01_ACCEPTANCE.json",
        "docs/pre_editor/active/PHASE_13_VALIDATION_AMENDMENT_01_PROPOSAL.md",
        "docs/pre_editor/active/PHASE_13_VALIDATION_AMENDMENT_01_ACCEPTANCE.json",
        (OUT / "correction-02.json").relative_to(ROOT).as_posix(),
        "docs/pre_editor/active/PHASE_13_SCOPE_CANDIDATE_04.json", "docs/pre_editor/active/OUTPUT_PATHS.json",
        "tools/test_pre_editor_geometry_authoring.py", "tools/test_pre_editor_geometry_templates.py",
        "tools/test_frame_submission.py", "tools/rendering_baseline.py", "tools/rendering_validation.py", "tools/postbuild.py",
        "GEngine/include/GEngine/Assets/AssetHandle.h", "GEngine/include/GEngine/Core/Log.h",
        "GEngine/include/GEngine/Core/Platform.h", "external/entt/include/entt/entt.hpp",
        props.relative_to(ROOT).as_posix()]
    for p in extra:
        names[p.casefold()] = p
    assets = json.loads((ROOT / "tools/runtime_assets.json").read_text())
    for p in assets["files"].values(): names[p.casefold()] = p
    external = {p: identity(p) for p in prior["external_inputs"]}
    external[str(MSBUILD)] = identity(MSBUILD)
    # These standard-library facilities are direct new dependencies; transitively
    # consumed compiler/SDK headers are retained from the build/read closure.
    include = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/include")
    for p in ("atomic", "expected", "functional", "memory", "new", "string", "string_view", "thread", "tuple",
              "type_traits", "utility", "array", "cstring", "mutex", "chrono", "fstream", "filesystem"):
        external[str(include / p)] = identity(include / p)
    for cfg in ("Debug", "Release"):
        for prefix in ("bin", "bin-int"):
            assert not local(f"{prefix}/{cfg}/Phase13Candidate04").exists(), "Preserve prior outputs"
    for p in owned:
        destination = local(OUT / "source-snapshot" / p); destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("xb") as stream: stream.write((ROOT / p).read_bytes())
    cases = {}
    for cfg in ("Debug", "Release"):
        case = prior["cases"][cfg]
        cases[cfg] = {"executable": f"bin/{cfg}/Phase13Candidate04/RigidBodySimulation/RigidBodySimulation.exe",
            "cwd": f"runtime/RigidBodySimulation/{cfg}/phase13-candidate04",
            "asset_root": f"bin/{cfg}/Phase13Candidate04/assets", "layout": case["layout"],
            "expected_modules": case["expected_modules"],
            "markers": ["PRE_EDITOR_PHASE_13_LIFETIME_PASS", "PRE_EDITOR_PHASE_13_SCALE_PASS",
                "PRE_EDITOR_PHASE_13_ACTIONS_PASS", "PRE_EDITOR_PHASE_13_PRESENTATION_PASS", "PRE_EDITOR_PHASE_13_COMPLETE"]}
    for cfg, reuse in prior["glad_reuse"].items():
        assert identity(ROOT / reuse["source"]) == reuse["identity"]
        assert all(digest(ROOT / p) == value for p, value in reuse["input_hashes"].items())
        assert identity(ROOT / reuse["build_evidence"]) == reuse["build_evidence_identity"]
    manifest = {"phase": "13", "candidate": "04", "baseline": scope["baseline"], "recorded_utc": utc(),
        "owned_files": owned, "input_hashes": {p: digest(local(p)) for p in sorted(names.values())},
        "external_inputs": external, "compile_members": compile_members(), "compile_discovery": discovery(),
        "include_preceding_absent": sorted(p for p in prior["include_preceding_absent"] if not (ROOT / p).exists()),
        "dependency_membership_basis": {prior_path.relative_to(ROOT).as_posix(): identity(prior_path)},
        "cases": cases, "glad_reuse": prior["glad_reuse"],
        "engine_debug_reuse": json.loads((OUT / "correction-02.json").read_text())["reused_engine_debug"],
        "prior_runs": json.loads((OUT / "correction-02.json").read_text())["prior_runs"], "asset_members": assets["files"],
        "environment": json.loads((OUT / "environment-observation-02.json").read_text(encoding="utf-8-sig")),
        "toolchain": "MSVC14.44.35207/v143/SDK10.0.26100.0/C++23; Debug MTd, Release MT",
        "reference_viewport": prior["reference_viewport"], "lighting": prior["lighting"], "quality": prior["quality"],
        "camera": "Maintained RBS source camera, no Phase12 diagnostic mode or automatic camera override",
        "physics": "Maintained RBS fixed step/settings from source; temporary deterministic bridge scenes; no Physics scheduler change",
        "fixture": "Explicit header traces and Scene entities; no randomness. Registered Diamond CPU source for Box/Convex, radius2 Sphere. Full source snapshot is the fixture identity.",
        "protocol": {"build_seconds_each_configuration":1800,"runtime_seconds_each":180,"runtime_seconds_total":360,
            "native_close_seconds":30,"processes":3,"repeats":0,"release_pairs":3,"warmup_each_lane":120,
            "samples_each_lane":240,"dispatches_per_iteration":64,"listeners":8,"deferred_per_iteration":64,
            "payload_bytes":32,"queue_capacity":256,"drain_limit":64,"max_depth":16,
            "pass":"All declared lifetime/ordering/shape/copy/move/rollback/teardown invariants; complete cost observations; exact identities; no GL/context failure; exit0",
            "stop":"Unexpected build/runtime/invariant/GL error, identity conflict, missing metrics or time limit. Infrastructure correction only under accepted bounded loop.",
            "cost_scope":"Diagnostic only; no new absolute/relative timing or allocation threshold, speedup or qualified p99 claim",
            "measurement":"Owner-loop Update: complete 64-event synchronous batch and post64+drain64 separately; ON per-event dispatch and post-to-callback; OFF/ON outer clocks; logs outside measured spans",
            "observer":"ON provider allocation/byte/lookup and queue copy/move/occupancy counters plus timestamps; OFF optional metrics unavailable, not zero; outer clock-pair duration separately recorded",
            "human_acceptance":"Routing/queue contract and safe callback ownership; maintained RBS actions/shape behavior"},
        "derived_outputs":"Candidate-specific bin/bin-int, runtime logs/settings and raw evidence; not behavior input fingerprints",
        "approval_membership":"Compile globs for exact participating projects plus preceding-include absence; asset manifest defines loaded membership"}
    verify(manifest); save(OUT / "scenario-manifest.json", manifest)
    print(f"Phase 13 inputs frozen: {len(names)} repository and {len(external)} toolchain inputs; two runtime cases.", flush=True)


def cost_summary(folder):
    import csv
    import statistics
    frames = list(csv.DictReader((folder / "cost-frames.csv").open()))
    events = list(csv.DictReader((folder / "cost-events.csv").open()))
    assert len(frames) == 1440 and len(events) == 46080, "Incomplete declared samples"
    def stats(values):
        values = sorted(values)
        return {"median":statistics.median(values),"empirical_p95":values[int(.95*(len(values)-1))],"maximum":max(values)}
    lanes = []
    for pair in range(3):
        for on in range(2):
            lane = [row for row in frames if int(row["pair"]) == pair and int(row["on"]) == on]
            assert [int(row["sample"]) for row in lane] == list(range(240))
            assert all(row["sync_calls"] == "512" and row["queue_calls"] == "64" and row["backlog"] == "0" for row in lane)
            if on:
                assert all(row["payload_copies"] == "64" and row["payload_moves"] == "64" and row["peak_backlog"] == "64" for row in lane)
            lanes.append({"pair":pair,"on":bool(on),"samples":240,
                "sync_batch_ns":stats([int(row["sync_batch_ns"]) for row in lane]),
                "queue_batch_ns":stats([int(row["queue_batch_ns"]) for row in lane]),
                "clock_pair_ns":stats([int(row["clock_pair_ns"]) for row in lane]),
                "optional_counters":{k:stats([int(row[k]) for row in lane]) for k in
                    ("allocations","allocated_bytes","lookups","payload_copies","payload_moves","peak_backlog")} if on else "UNOBSERVED"})
    expected = [(pair,sample,event) for pair in range(3) for sample in range(240) for event in range(64)]
    assert [(int(v["pair"]),int(v["sample"]),int(v["event"])) for v in events] == expected
    assert all(int(v["sync_ns"]) >= 0 and int(v["queue_to_callback_ns"]) >= 0 for v in events)
    summary = {"result":"PASS","claim":"Complete diagnostic observations; no performance threshold/speedup/p99 qualification",
        "lanes":lanes,"units":"nanoseconds; batch counts64 events; 8 synchronous listeners; 32-byte payload",
        "observer_median_difference_ns":[{k:lanes[2*i+1][k]["median"]-lanes[2*i][k]["median"] for k in ("sync_batch_ns","queue_batch_ns")} for i in range(3)],
        "event_on":{k:stats([int(row[k]) for row in events]) for k in ("sync_ns","queue_to_callback_ns")},
        "limitations":"Same-process sequential pairs, uncontrolled clocks/thermals/load; provider storage only; queue payload move copies its inline storage; optional OFF counters not observed"}
    save(folder / "cost-summary.json", summary)


def run(cfg):
    from test_pre_editor_geometry_authoring import observe_window
    manifest = json.loads((OUT / "scenario-manifest.json").read_text()); verify(manifest)
    for result in OUT.glob("**/result.json"):
        assert json.loads(result.read_text())["result"] == "PASS", "Preserved failure stops dependent execution"
    prior_seconds = 0
    for prior in manifest["prior_runs"]:
        assert identity(ROOT / prior["path"]) == prior["identity"], "Prior campaign result changed"
        result = json.loads((ROOT / prior["path"]).read_text())
        assert result["seconds"] == prior["seconds"] and result["result"] == prior["result"]
        prior_seconds += prior["seconds"]
    current = list(OUT.glob("runtime-*/attempt.json"))
    for reservation in current:
        result_path = reservation.parent / "result.json"
        assert result_path.exists(), "An earlier reserved launch has no result; stop for review"
        result = json.loads(result_path.read_text())
        assert result["result"] == "PASS", "Current candidate runtime failure stops execution"
        prior_seconds += result["seconds"]
    assert len(manifest["prior_runs"]) + len(current) < manifest["protocol"]["processes"], "Total launch limit reached"
    assert (cfg == "Debug" and not current) or (cfg == "Release" and len(current) == 1 and
        (OUT / "runtime-Debug/result.json").exists()), "Accepted order is corrected Debug, then Release"
    limit_seconds = min(180, 360-prior_seconds)
    assert limit_seconds > 30, "Insufficient remaining aggregate time for validation and close"
    admitted = json.loads((OUT / f"runtime-admission-{cfg}.json").read_text())
    for path, expected in admitted["bundle"].items(): assert identity(ROOT / path) == expected, path
    case = manifest["cases"][cfg]; directory = local(case["cwd"])
    folder = local(OUT / f"runtime-{cfg}"); folder.mkdir()
    assert identity(directory / "imgui.ini") == admitted["initial_layout"]
    env = environment(); env.update(GENGINE_ASSET_ROOT=str(local(case["asset_root"])),
        GENGINE_PRE_EDITOR_SUBSCRIPTIONS="1", GENGINE_PRE_EDITOR_SUBSCRIPTION_OUTPUT=str(folder))
    key = next((k for k in env if k.lower() == "path"), "PATH")
    env[key] = str(ROOT / "external/tbb/bin") + os.pathsep + env.get(key, "")
    save(folder / "attempt.json", {"recorded_utc":utc(), "configuration":cfg,
        "ordinal":len(manifest["prior_runs"])+len(current)+1, "prior_seconds":prior_seconds,
        "limit_seconds":limit_seconds, "prior_runs":manifest["prior_runs"],
        "acceptance":"docs/pre_editor/active/PHASE_13_VALIDATION_AMENDMENT_01_ACCEPTANCE.json"})
    stopped = threading.Event(); observations = {}
    observer = threading.Thread(target=observe_window,args=(local(case["executable"]),stopped,observations)); observer.start()
    started = time.monotonic()
    try:
        report = smoke(local(case["executable"]),directory,env,admitted["modules"],case["markers"],False,started+limit_seconds)
    finally:
        stopped.set(); observer.join()
    for name in ("application.log","GEngine.log","imgui.ini"):
        if (directory/name).exists(): shutil.copy2(directory/name,folder/name)
    text = "\n".join((folder/name).read_text(encoding="utf-8",errors="replace") for name in ("application.log","GEngine.log") if (folder/name).exists())
    report["seconds"] = time.monotonic()-started; report["window_observations"] = observations
    report["prior_runtime_seconds"] = prior_seconds; report["campaign_runtime_seconds"] = prior_seconds + report["seconds"]
    report["teardown_marker"] = "PRE_EDITOR_PHASE_13_APP_RELEASED" in text
    report["presentation"] = sorted(set(re.findall(r"PRE_EDITOR_PHASE_13_PRESENTATION_PASS[^\r\n]+",text)))
    report["unexpected_failure"] = "PRE_EDITOR_PHASE_13_FAIL" in text or "Subscription operation rejected" in text
    try:
        assert report["result"] == "PASS" and observations and report["teardown_marker"] and report["presentation"] and not report["unexpected_failure"]
        assert report["seconds"] <= limit_seconds
        assert report["campaign_runtime_seconds"] <= 360
        assert any("NVIDIA GeForce RTX 3070 Laptop GPU" in line for line in report["presentation"]), "Actual GL renderer mismatch"
        if cfg == "Release": cost_summary(folder)
        verify(manifest)
    except Exception as error:
        report["result"] = "FAIL"; report["postcheck_reason"] = str(error)
    save(folder / "result.json", report)
    print(json.dumps({k:v for k,v in report.items() if k not in ("loaded_modules",)}),flush=True)
    if report["result"] != "PASS": raise SystemExit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("step",choices=["prepare","build","stage","run"])
    parser.add_argument("--configuration",choices=["Debug","Release"])
    args = parser.parse_args()
    if args.step == "prepare": prepare()
    elif args.configuration: globals()[args.step](args.configuration)
    else: parser.error("Configuration required")
