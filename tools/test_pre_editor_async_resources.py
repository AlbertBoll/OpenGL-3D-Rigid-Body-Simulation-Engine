"""Phase 12 accepted bounded async adoption/diagnostic. Immutable evidence; zero retries."""
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
OUT = ROOT / "logs/pre_editor/phase-12/candidate-07"
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
    assert cfg == "Release", "Candidate 07 permits only the affected Release build"
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
        assert Path(properties["OutDir"]).resolve() == ROOT / f"bin/{cfg}/Phase12Candidate07/{Path(project).stem}"
        assert Path(properties["IntDir"]).resolve() == ROOT / f"bin-int/{cfg}/Phase12Candidate07/{Path(project).stem}"
        assert properties["PostBuildEventUseInBuild"] == "false"
        assert properties["VCToolsVersion"] == "14.44.35207" and properties["WindowsTargetPlatformVersion"] == "10.0.26100.0"
    reuse = manifest["glad_reuse"][cfg]
    assert identity(ROOT / reuse["source"]) == reuse["identity"]
    library = local(f"bin/{cfg}/Phase12Candidate07/glad/glad.lib")
    library.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / reuse["source"], library)
    reused_gengine = None
    build_projects = (PROJECTS[0], PROJECTS[-1])
    commands = [[str(MSBUILD), project, "/t:Build", *common, "/p:BuildProjectReferences=false", "/m:2", "/nr:false", "/nologo", "/v:normal",
                 "/bl:" + str(folder / (Path(project).stem + ".binlog"))] for project in build_projects]
    report = {"commands": commands, "reused_glad": reuse, "reused_gengine": reused_gengine, "cwd": str(ROOT), "recorded_utc": utc(), "result": "FAIL", "exits": []}
    started = time.monotonic()
    try:
        with (folder / "build.log").open("xb") as stream:
            for command in commands:
                result = subprocess.run(command, cwd=ROOT, env=environment(), stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=max(1, 1800 - (time.monotonic() - started)), creationflags=subprocess.CREATE_NO_WINDOW)
                report["exits"].append(result.returncode)
                assert result.returncode == 0, "Required affected build failed"
        verify(manifest)
        products = [f"bin/{cfg}/Phase12Candidate07/{name}/{name}.{extension}" for name, extension in [("GEngine", "lib"), ("glad", "lib"), ("RigidBodySimulation", "exe")]]
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
    assert cfg == "Release", "Candidate 07 stages only the affected Release closure"
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


def prepare():
    raise RuntimeError("Candidate 07 input preparation is disabled; use the reviewed, frozen scenario manifest")


def smoke(executable, directory, env, modules, markers, performance):
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
        watchdog = threading.Timer(120, lambda: child.kill() if child.poll() is None else None); watchdog.start()
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
            while time.monotonic()-started < 90 and child.poll() is None:
                text = "\n".join(p.read_text(encoding="utf-8",errors="replace") for p in (directory/"application.log",directory/"GEngine.log") if p.exists())
                assert "PRE_EDITOR_PHASE_12_FAIL" not in text, "Phase 12 runtime assertion failed"
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
            report["exit"]=child.wait(timeout=min(30,max(.001,120-(time.monotonic()-started))))
            report["close_to_exit_seconds"]=time.monotonic()-close
            assert report["exit"]==0, "Nonzero runtime exit"
            report["result"]="PASS"
        except Exception as error: report["reason"]=str(error)
        finally:
            watchdog.cancel()
            if child.poll() is None: child.terminate();child.wait(timeout=10);report["terminated"]=True
            report.setdefault("exit",child.returncode);report["seconds"]=time.monotonic()-started
    return report


def no_prior_failure():
    for path in OUT.glob("**/result.json"):
        assert json.loads(path.read_text())["result"] == "PASS", "Preserved failure stops further execution: " + str(path)


def checked_manifest():
    manifest = json.loads((OUT / "scenario-manifest.json").read_text())
    assert manifest["phase"] == "12" and manifest["candidate"] == "07"
    verify(manifest)
    historical = json.loads((ROOT / "logs/pre_editor/phase-12/candidate-04/scenario-manifest.json").read_text())
    # Window decoration is separately owner-amended. Measurement policy stays exact.
    assert manifest["protocol"] == historical["protocol"], "Measurement protocol changed"
    return manifest


def functional_reuse(manifest):
    record = json.loads((OUT / "functional-reuse.json").read_text())
    assert record["result"] == "PASS" and record["candidate"] == "07" and record["baseline_candidate"] == "04"
    assert set(manifest["owned_files"]) <= set(record["current_source_identities"])
    for path, expected in record["current_source_identities"].items():
        assert identity(local(path)) == expected and manifest["input_hashes"][path] == expected["sha256"], path
    scope = record["scope_evidence"]
    assert identity(local(scope["path"])) == scope["identity"]
    assert json.loads(local(scope["path"]).read_text())["result"] == "PASS", "Source-impact review incomplete"
    assert set(record["historical_results"]) == {"Debug", "Release"}
    results = {}
    for cfg, reference in record["historical_results"].items():
        expected = f"logs/pre_editor/phase-12/candidate-04/runtime-{cfg}-functional/result.json"
        assert reference["path"] == expected and identity(local(expected)) == reference["identity"]
        result = json.loads(local(expected).read_text())
        assert result["result"] == "PASS" and result["runtime_closure"] == "PASS" and result["exit"] == 0
        assert result["native_close_posted"] and not result.get("terminated", False)
        assert result["markers"]["PRE_EDITOR_PHASE_12_FUNCTIONAL_COMPLETE"]
        results[cfg] = result
    return record, results


def runtime_inputs(manifest):
    admission = json.loads((OUT / "runtime-admission-Release.json").read_text())
    assert admission["manifest"] == identity(OUT / "scenario-manifest.json")
    build_result = json.loads((OUT / "build-Release/result.json").read_text())
    assert build_result["result"] == "PASS"
    executable = manifest["cases"]["Release"]["executable"]
    assert identity(local(executable)) == admission["binary"] == build_result["products"][executable]
    for path, expected in admission["bundle"].items():
        assert identity(local(path)) == expected, path
    for module in admission["modules"].values():
        assert digest(module["path"]) == module["sha256"], module["path"]
    return admission


def elapsed_runtime(functional):
    # Historical functional coverage is reused, never presented as a fresh process.
    return sum(result["seconds"] for result in functional.values()) + sum(
        json.loads(path.read_text())["seconds"] for path in OUT.glob("runtime-Release-*/result.json"))


def admission_observation(folder):
    text = (folder / "GEngine.log").read_text(encoding="utf-8", errors="replace")
    predicates = [dict(predicate=name, status=status, expected=int(expected),
                       actual=int(actual) if actual != "UNEVALUATED" else actual)
        for name, status, expected, actual in re.findall(
            r"PRE_EDITOR_PHASE_12_ADMISSION predicate=(\w+) status=(PASS|FAIL|UNEVALUATED) expected=(-?\d+) actual=(-?\d+|UNEVALUATED)", text)]
    fields = ("root_id", "sdl_id", "hwnd", "logical_width", "logical_height", "client_width", "client_height",
              "drawable_width", "drawable_height", "flags", "style", "exstyle", "dpi", "current")
    native = [dict(zip(fields, map(int, values))) for values in re.findall(
        r"PRE_EDITOR_PHASE_12_WINDOW_OBSERVATION root_id=(\d+) sdl_id=(\d+) hwnd=(\d+) "
        r"logical=(\d+)x(\d+) client=(\d+)x(\d+) drawable=(\d+)x(\d+) "
        r"flags=(\d+) style=(\d+) exstyle=(\d+) dpi=(\d+) current=(\d+)", text)]
    observation = dict(predicates=predicates, native=native,
        boundary_passes=text.count("PRE_EDITOR_PHASE_12_WINDOW_BOUNDARY_PASS"))
    save(folder / "admission-observation.json", observation)
    return observation, text


def check_observation(observation):
    expected = [("hidden", 1), ("width", 64), ("height", 64), ("swap_interval", 0)]
    assert observation["predicates"] == [dict(predicate=name, status="PASS", expected=value, actual=value)
                                         for name, value in expected], "Four-predicate admission incomplete"
    assert len(observation["native"]) == 1 and observation["boundary_passes"] == 1
    native = observation["native"][0]
    assert native["root_id"] == native["sdl_id"] != 0 and native["hwnd"] != 0 and native["current"] == 1
    assert all(native[key] == 64 for key in ("logical_width", "logical_height", "client_width", "client_height"))
    assert native["flags"] & 8 and native["flags"] & 16, "Hidden/borderless SDL flags missing"
    # Drawable dimensions and DPI remain observations, not a new pixel-size policy.


def sample_completeness(folder, lane, protocol):
    import csv
    def read(name):
        with (folder / name).open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
        assert rows and all(None not in row and all(value not in (None, "") for value in row.values()) for row in rows), name
        return rows
    samples, frames, idle = read("async.csv"), read("async-frames.csv"), read("idle-frames.csv")
    keys = [(kind, sample) for kind in ("texture", "mesh")
            for sample in range(-protocol["warmups_per_asset"], protocol["samples_per_asset"])]
    assert [(row["kind"], int(row["sample"])) for row in samples] == keys, "Missing, duplicate or reordered requests"
    assert [int(row["sample"]) for row in idle] == list(range(protocol["idle_samples"])), "Idle sample coverage"
    assert all(int(row["scheduler_ns"]) >= 0 for row in idle)
    grouped = {key: [] for key in keys}
    frame_keys = []
    for frame in frames:
        key = (frame["kind"], int(frame["sample"]))
        assert key in grouped, "Unexpected frame request"
        grouped[key].append(frame)
        if not frame_keys or frame_keys[-1] != key:
            frame_keys.append(key)
        assert all(int(frame[field]) >= 0 for field in ("frame", "state", "scheduler_ns", "drain_ns", "completed", "payload_bytes", "time_budget_reached"))
    assert frame_keys == keys, "Missing or reordered frame groups"
    queue_metrics = ("worker_wait_ns", "worker_decode_total_ns", "enqueue_wait_ns", "queue_wait_ns", "apply_total_ns", "max_drain_ns")
    texture_metrics = ("texture_read_ns", "texture_decode_ns", "texture_upload_ns", "texture_publication_ns", "texture_peak_bytes")
    stalls = []
    for row, key in zip(samples, keys):
        texture = key[0] == "texture"
        rows = grouped[key]
        assert rows and [int(frame["frame"]) for frame in rows] == list(range(len(rows)))
        for field, value in row.items():
            if field not in ("kind", "sample", "result") and value != "null":
                assert int(value) >= 0, field
        assert int(row["frames"]) == len(rows)
        assert int(row["max_scheduler_ns"]) == max(int(frame["scheduler_ns"]) for frame in rows)
        count = sum(int(frame["scheduler_ns"]) > protocol["stall_ns"] for frame in rows)
        assert int(row["stalls"]) == count and row["result"] == ("FAIL" if count else "PASS")
        assert int(row["completed"]) == sum(int(frame["completed"]) for frame in rows) == 1
        assert int(row["payload_bytes"]) == sum(int(frame["payload_bytes"]) for frame in rows) == (16777232 if texture else 288392)
        assert int(row["texture_calls"]) == (1 if texture else 0) and int(row["buffer_calls"]) == (0 if texture else 2)
        if texture:
            assert int(row["image_bytes"]) == 16777216
        assert int(row["peak_reserved_bytes"]) <= 256 * 1024 * 1024 and int(row["peak_queued_bytes"]) <= 128 * 1024 * 1024
        for field in queue_metrics:
            assert (row[field] != "null") == (lane == "ON"), "Queue observation availability"
        for field in texture_metrics:
            assert (row[field] != "null") == (lane == "ON" and texture), "Texture observation availability"
        if lane == "ON" and texture:
            assert all(int(row[field]) > 0 for field in texture_metrics[:4])
            assert int(row["texture_peak_bytes"]) <= 64 * 1024 * 1024
        if count:
            stalls.append(dict(kind=key[0], sample=key[1], stalls=count))
    coverage = dict(result="PASS", requests=len(samples), measured_requests=sum(key[1] >= 0 for key in keys),
                    warmup_requests=sum(key[1] < 0 for key in keys), idle_samples=len(idle), scheduler_frames=len(frames),
                    stall_result="FAIL" if stalls else "PASS", violations=stalls)
    save(folder / "sample-completeness.json", coverage)
    assert not any(row["kind"] == "mesh" for row in stalls), "New mesh stall stops the batch"
    return coverage


def admission_pins(manifest):
    return dict(scenario=identity(OUT / "scenario-manifest.json"), release_build=identity(OUT / "build-Release/result.json"),
                runtime=identity(OUT / "runtime-admission-Release.json"), functional_reuse=identity(OUT / "functional-reuse.json"),
                preflight=identity(OUT / "runtime-Release-preflight/result.json"),
                preflight_observation=identity(OUT / "runtime-Release-preflight/admission-observation.json"),
                binary=identity(local(manifest["cases"]["Release"]["executable"])))


def checked_preflight():
    result = json.loads((OUT / "runtime-Release-preflight/result.json").read_text())
    assert result["result"] == "PASS" and result["runtime_closure"] == "PASS" and result["exit"] == 0
    assert result["native_close_posted"] and not result.get("terminated", False)
    check_observation(json.loads((OUT / "runtime-Release-preflight/admission-observation.json").read_text()))
    assert result["preflight_only"] and result["presentation_count"] == 1
    return result


def launch(lane, series=None, preflight_only=False):
    no_prior_failure()
    manifest = checked_manifest()
    reuse, functional = functional_reuse(manifest)
    admitted = runtime_inputs(manifest)
    elapsed = elapsed_runtime(functional)
    assert elapsed + manifest["protocol"]["runtime_seconds_each"] <= manifest["protocol"]["runtime_seconds_total"], "Aggregate runtime budget"
    if not preflight_only:
        assert lane in ("OFF", "ON") and series in range(3)
        checked_preflight()
        comparison = json.loads((OUT / "comparability-admission.json").read_text())
        assert comparison["status"] == "ADMITTED_WITH_DECLARED_ATTRIBUTION_LIMITS" and comparison["identities"] == admission_pins(manifest)
        order = [(i, value) for i in range(3) for value in ("OFF", "ON")]
        position = order.index((series, lane))
        for i, value in order[:position]:
            assert json.loads((OUT / f"runtime-Release-{value}-{i}/result.json").read_text())["result"] == "PASS"
        assert all(not (OUT / f"runtime-Release-{value}-{i}").exists() for i, value in order[position:]), "Batch sequence already attempted"
    else:
        assert lane == "OFF" and series is None
        assert not (OUT / "comparability-admission.json").exists(), "Preflight cannot repeat after admission"
        assert not any(OUT.glob("runtime-Release-O[NF]*-*")), "Preflight must precede measurement"
    name = "runtime-Release-preflight" if preflight_only else f"runtime-Release-{lane}-{series}"
    folder = local(OUT / name); folder.mkdir(exist_ok=False)
    case = manifest["cases"]["Release"]
    directory = local(case["cwd"] if preflight_only else f"runtime/RigidBodySimulation/Release/phase12-candidate07-{lane}-{series}")
    if preflight_only:
        assert {p.name for p in directory.iterdir()} == {"imgui.ini"}
        assert identity(directory / "imgui.ini") == admitted["initial_layout"]
    else:
        directory.mkdir(exist_ok=False); (directory / "imgui.ini").write_text("")
    env = environment(); env["GENGINE_ASSET_ROOT"] = str(local(case["asset_root"]))
    key = next((key for key in env if key.lower() == "path"), "PATH")
    env[key] = str(ROOT / "external/tbb/bin") + os.pathsep + env.get(key, "")
    env["GENGINE_PRE_EDITOR_ASYNC_PERFORMANCE"] = lane
    if preflight_only:
        env["GENGINE_PRE_EDITOR_ASYNC_ADMISSION_ONLY"] = "1"
    else:
        assert "GENGINE_PRE_EDITOR_ASYNC_ADMISSION_ONLY" not in env
    markers = ["PRE_EDITOR_PHASE_12_ENVIRONMENT_PASS", "PRE_EDITOR_PHASE_12_WINDOW_BOUNDARY_PASS"] + (
        ["PRE_EDITOR_PHASE_12_PREFLIGHT_PRESENT_PASS", "PRE_EDITOR_PHASE_12_ADMISSION_ONLY_COMPLETE"] if preflight_only else
        ["PRE_EDITOR_PHASE_12_PROTOCOL_ADMITTED", "PRE_EDITOR_PHASE_12_PERFORMANCE_COMPLETE"])
    save(folder / "invocation.json", dict(configuration="Release", lane=lane, series=series, preflight_only=preflight_only,
        markers=markers, binary=admitted["binary"], scenario_identity=identity(OUT / "scenario-manifest.json"),
        runtime_admission_identity=identity(OUT / "runtime-admission-Release.json"),
        functional_reuse_identity=identity(OUT / "functional-reuse.json"),
        comparability_identity=None if preflight_only else identity(OUT / "comparability-admission.json"),
        runtime_inputs={p.name: identity(p) for p in directory.iterdir() if p.is_file()},
        environment={key: value for key, value in env.items() if key.startswith("GENGINE_") or key in ("TMP", "TEMP")},
        deadline_seconds=30, process_cap_seconds=120, aggregate_cap_seconds=960, aggregate_elapsed_before=elapsed))
    report = smoke(local(case["executable"]), directory, env, admitted["modules"], markers, True)
    for path in directory.iterdir():
        if path.is_file() and path.name in ("application.log", "GEngine.log", "imgui.ini", "async.csv", "async-frames.csv", "idle-frames.csv"):
            shutil.copy2(path, folder / path.name)
    try:
        observation, log = admission_observation(folder)
        check_observation(observation)
        report["admission_observation"] = identity(folder / "admission-observation.json")
        if preflight_only:
            assert not list(directory.glob("*.csv")), "Preflight unexpectedly produced CSVs"
            assert "PRE_EDITOR_PHASE_12_PROTOCOL_ADMITTED" not in log and "PRE_EDITOR_PHASE_12_PERFORMANCE_COMPLETE" not in log
            assert log.count("PRE_EDITOR_PHASE_12_ADMISSION_ONLY_COMPLETE") == 1
            assert re.findall(r"PRE_EDITOR_PHASE_12_PREFLIGHT_PRESENT_PASS count=(\d+)", log) == ["1"]
            report.update(preflight_only=True, presentation_count=1)
        else:
            assert "PRE_EDITOR_PHASE_12_ADMISSION_ONLY_COMPLETE" not in log and "PRE_EDITOR_PHASE_12_PREFLIGHT_PRESENT_PASS" not in log
            report["sample_coverage"] = sample_completeness(folder, lane, manifest["protocol"])
        assert report["seconds"] <= 120 and elapsed + report["seconds"] <= 960, "Runtime budget exceeded"
        assert report["binary"] == admitted["binary"]
        verify(manifest); runtime_inputs(manifest); functional_reuse(manifest)
    except Exception as error:
        report.update(result="FAIL", validation_error=str(error))
    report["aggregate_runtime_seconds"] = elapsed + report["seconds"]
    save(folder / "result.json", report)
    print(json.dumps({key: value for key, value in report.items() if key != "loaded_modules"}), flush=True)
    if report["result"] != "PASS":
        raise SystemExit(1)


def preflight():
    launch("OFF", preflight_only=True)


def run(cfg, lane="OFF", series=0):
    assert cfg == "Release", "Only the final Release performance batch is admitted"
    launch(lane, series)


def admit():
    no_prior_failure()
    manifest = checked_manifest(); reuse, functional = functional_reuse(manifest)
    staged = runtime_inputs(manifest); preflight = checked_preflight()
    assert preflight["binary"] == staged["binary"]
    save(OUT / "comparability-admission.json", dict(status="ADMITTED_WITH_DECLARED_ATTRIBUTION_LIMITS", recorded_utc=utc(),
        identities=admission_pins(manifest), functional=reuse["historical_results"], scope_evidence=reuse["scope_evidence"],
        protocol=manifest["performance"], measurement_protocol=manifest["protocol"], mesh_limits=manifest["protocol"]["mesh_unavailable"],
        boundary="actual hidden borderless logical/client64, swap0; complete scheduler hidden Present verified; no simulation/UI/draw; fresh original requests; unchanged warmups/samples",
        claim="current applicable absolute stall gate and OFF/ON observer comparison; no historical whole-engine speedup claim"))


def summarize():
    import csv,statistics,math
    no_prior_failure();rows={};pairs=[];violations=[]
    for i in range(3):
        for lane in ("OFF","ON"):
            p=OUT/f"runtime-Release-{lane}-{i}/async.csv"
            values=list(csv.DictReader(p.open()));assert len(values)==20
            rows[i,lane]=[v for v in values if int(v["sample"])>=0]
            for v in values:
                if int(v["stalls"]):violations.append(dict(series=i,lane=lane,**v))
        for kind in ("texture","mesh"):
            for metric in ("max_scheduler_ns","ready_ns"):
                off=statistics.median(int(v[metric]) for v in rows[i,"OFF"] if v["kind"]==kind)
                on=statistics.median(int(v[metric]) for v in rows[i,"ON"] if v["kind"]==kind)
                pairs.append(dict(series=i,kind=kind,metric=metric,off=off,on=on,limit=max(off*.05,50000),pass_admission=abs(on-off)<=max(off*.05,50000)))
    summaries={}
    for kind in ("texture","mesh"):
        for lane in ("OFF","ON"):
            summaries[kind+"_"+lane]={}
            for metric in ("request_ns","ready_ns","max_scheduler_ns","worker_decode_total_ns","apply_total_ns","texture_upload_ns","texture_publication_ns"):
                values=[int(v[metric]) for i in range(3) for v in rows[i,lane] if v["kind"]==kind and v[metric]!="null"]
                if not values:summaries[kind+"_"+lane][metric]=None;continue
                meds=[statistics.median(int(v[metric]) for v in rows[i,lane] if v["kind"]==kind) for i in range(3)]
                spread=(max(meds)-min(meds))/statistics.median(meds) if statistics.median(meds) else None
                summaries[kind+"_"+lane][metric]=dict(median=statistics.median(values),p95=sorted(values)[math.ceil(.95*len(values))-1],maximum=max(values),run_medians=meds,spread=spread,quality="NOISY" if spread is None or spread>.1 else "STABLE")
    admitted=all(v["pass_admission"] for v in pairs)
    save(OUT/"performance-summary.json",dict(stall_result="FAIL" if violations else "PASS",observer_result="PASS" if admitted else "INCONCLUSIVE",residual="OPEN" if violations or not admitted else "ELIGIBLE_FOR_GATE_REVIEW",violations=violations,pairs=pairs,summaries=summaries,mesh_unavailable_reason="Accepted OD-04 amendment 01; aggregate Decode and Apply only",gpu_time=None,measured_vram=None))
    print(json.dumps(dict(stall_result="FAIL" if violations else "PASS",observer_result="PASS" if admitted else "INCONCLUSIVE",violating_requests=len(violations))),flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("step", choices=["prepare", "build", "stage", "preflight", "run", "admit", "summarize"])
    parser.add_argument("--configuration", choices=["Release"])
    parser.add_argument("--lane", choices=["OFF", "ON"], default="OFF")
    parser.add_argument("--series", type=int, default=0)
    args = parser.parse_args()
    if args.step in ("prepare", "preflight", "admit", "summarize"):
        globals()[args.step]()
    else:
        if not args.configuration:
            parser.error("Release configuration required")
        no_prior_failure()
        if args.step == "run":
            run(args.configuration, args.lane, args.series)
        else:
            globals()[args.step](args.configuration)
