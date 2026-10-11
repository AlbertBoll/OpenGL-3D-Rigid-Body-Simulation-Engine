"""Phase 15 isolated RBS control/candidate build, identity and evidence runner."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import csv
import math
import statistics
import ctypes
from ctypes import wintypes
from contextlib import contextmanager
import test_pre_editor_rbs_modularity as closure

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "logs/pre_editor/phase-15"
ENTRY = BASE / "entry-01/entry.json"
ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_15_SHUTDOWN_SCOPE_ACCEPTANCE_01.json"
PRIOR = ROOT / "logs/pre_editor/phase-14/packet-b/candidate-10/scenario-manifest.json"
PROJECTS = closure.PROJECTS
MSBUILD = closure.MSBUILD
local, identity, digest = closure.local, closure.identity, closure.digest


def save(path, data):
    path = local(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(data, stream, indent=2)
        stream.write("\n")


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def git(*args):
    return subprocess.check_output(["git", "--no-optional-locks", *args], cwd=ROOT)


def guards():
    entry = json.loads(ENTRY.read_text())
    accepted = json.loads(ACCEPTANCE.read_text())
    assert git("rev-parse", "HEAD").decode().strip() == entry["baseline"]
    assert git("branch", "--show-current").decode().strip() == "pre-editor/refactor"
    assert digest(entry["index"]["path"]) == entry["index"]["sha256"]
    assert hashlib.sha256(git("ls-files", "--stage", "-v")).hexdigest() == entry["index"]["logical_sha256"]
    changed = set(git("diff", "--name-only", "HEAD").decode().splitlines())
    changed.update(git("ls-files", "--others", "--exclude-standard").decode().splitlines())
    assert changed <= set(accepted["owned_files"]), str(changed - set(accepted["owned_files"]))
    for name, old in entry["read_input_identities"].items():
        if name not in accepted["owned_files"] and name != "docs/pre_editor/active/REFACTOR_STATE.md":
            assert digest(ROOT / name) == old["sha256"], name
    return entry, accepted


def environment():
    return closure.environment()


def prepare(out, candidate, control, reuse=None, reuse_glad=None, reuse_debug=None):
    entry, accepted = guards()
    prior = json.loads(PRIOR.read_text())
    out.mkdir(parents=True, exist_ok=False)
    for cfg in ("Debug", "Release"):
        for base in ("bin", "bin-int"):
            assert not local(f"{base}/{cfg}/Phase15Candidate{candidate}").exists()
    props = out / "isolated.props"
    macro = "GENGINE_INPUT_VALIDATION" + (";GENGINE_INPUT_CONTROL" if control else "")
    props.write_text('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        '<PropertyGroup>'
        f'<OutDir>{ROOT.as_posix()}/bin/$(Configuration)/Phase15Candidate{candidate}/$(MSBuildProjectName)/</OutDir>'
        f'<IntDir>{ROOT.as_posix()}/bin-int/$(Configuration)/Phase15Candidate{candidate}/$(MSBuildProjectName)/</IntDir>'
        '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>\n'
        '<ItemDefinitionGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\'">'
        '<ClCompile>'
        f'<PreprocessorDefinitions>{macro};%(PreprocessorDefinitions)</PreprocessorDefinitions>'
        f'<AdditionalIncludeDirectories>{ROOT.as_posix()}/external/sdl2/include;{ROOT.as_posix()}/external/glad/include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>'
        '</ClCompile></ItemDefinitionGroup>\n</Project>\n', encoding="utf-8")
    # Actual target membership plus ordered include resolution; not all repository sources.
    names = set(PROJECTS + accepted["owned_files"] + [
        "premake5.lua", "external/glad/premake5.lua", "tools/postbuild.py", "tools/runtime_assets.json",
        "tools/test_pre_editor_rbs_modularity.py", "CODING_STYLE.md", ".clang-format",
        "docs/pre_editor/phases/PHASE_15.md", "docs/pre_editor/VALIDATION_CONTRACTS.md",
        "docs/pre_editor/active/OUTPUT_PATHS.json", "docs/pre_editor/active/PHASE_15_OD05_PROPOSAL.md",
        "docs/pre_editor/active/PHASE_15_OD05_SCOPE_ACCEPTANCE.json",
        ACCEPTANCE.relative_to(ROOT).as_posix(), props.relative_to(ROOT).as_posix()])
    names.update(prior["asset_members"].values())
    # Consumed dependency libraries and compiler-generated inputs from the providing closure.
    names.update(p for p in prior["input_hashes"] if p.endswith((".lib", ".dll", ".inl")))
    absent = set()
    closure.include_closure(names, absent)
    names = {p for p in names if (ROOT / p).is_file()}
    absent = {p for p in absent if not (ROOT / p).exists()}
    # Diagnostic native headers use the same pinned backend installation.
    for folder in ("external/sdl2/include", "external/glad/include"):
        names.update(p.relative_to(ROOT).as_posix() for p in (ROOT / folder).rglob("*.h"))
    for name in accepted["owned_files"]:
        destination = local(out / "source-snapshot" / name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, destination)
    machine_cmd = "$ErrorActionPreference='Stop'; @{cpu=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors);memory=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;os=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber);gpu=(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion);power=(powercfg /getactivescheme)} | ConvertTo-Json -Depth 4"
    machine = subprocess.run(["powershell", "-NoProfile", "-Command", machine_cmd], capture_output=True, check=True)
    manifest = {"phase": 15, "candidate": candidate, "control": control, "recorded_utc": utc(),
        "baseline": entry["baseline"], "owned_files": accepted["owned_files"],
        "input_hashes": {p: digest(ROOT / p) for p in sorted(names)},
        "external_inputs": {p: identity(p) for p in prior["external_inputs"]},
        "compile_members": closure.compile_members(), "compile_discovery": closure.discovery(),
        "include_preceding_absent": sorted(absent), "asset_members": prior["asset_members"],
        "expected_modules": prior["expected_modules"], "layout": prior["layout"],
        "machine": json.loads(machine.stdout), "toolchain": prior["toolchain"],
        "protocol": {"fixture": "GeometryGallery at accepted baseline; exact scene/camera/light/quality/physics/assets in closure",
            "window_logical": [1280, 720], "pacing": "VSync off, manual cap 0", "seed": "none; deterministic source object list",
            "measurements": "3 OFF/ON pairs, 120 warmup + 240 samples per lane, 64 native events per iteration",
            "events": "16 x [P down, P up, motion (1,-1), wheel (0,+1)]",
            "boundary": "PrepareForUpdate through EventManager PollEvents and InputManager Update; injection before start; no render/physics/sleep inside",
            "latency": "native injection to maintained semantic consumer/route observer; no physical-to-display claim",
            "limits": {"latency_max_ms": 16.667, "median_delta_ms": .100, "p95_delta_ms": .250,
                "build_seconds": 1800, "runtime_seconds": 180, "native_close_seconds": 30},
            "classification": "FAIL: genuine violated gate. INCONCLUSIVE: demonstrated infrastructure/missing data; preserve attempt. No favorable retry.",
            "external_load": "idle interactive desktop; no concurrent builds/profilers during samples"},
        "derived_outputs": "Versioned bin/bin-int products; isolated runtime logs/settings; source snapshot immutable",
        "human_gate": "Release|x64 Rebuild Solution and canonical human-built RBS runtime inspection before explicit SEAL"}
    if reuse:
        old = local(BASE / ("candidate-" + reuse))
        original = json.loads((old / "scenario-manifest.json").read_text())
        built = json.loads((old / "build-Release/result.json").read_text())
        assert control and original["control"] and built["exits"][:2] == [0, 0]
        allowed = {"RigidBodySimulation/tests/InputRoutingChecks.h", "tools/test_pre_editor_input_routing.py",
                   "RigidBodySimulation/src/RbsValidation.cpp"}
        for name, value in original["input_hashes"].items():
            if name not in allowed and not name.endswith("/isolated.props"):
                assert digest(ROOT / name) == value, "Reuse input changed: " + name
        manifest["reuse"] = {"candidate": reuse, "manifest": identity(old / "scenario-manifest.json"),
            "build": identity(old / "build-Release/result.json"),
            "libraries": {p: identity(ROOT / f"bin/Release/Phase15Candidate{reuse}/{p}/{p}.lib") for p in ("glad", "GEngine")},
            "reason": "Unchanged engine/dependency inputs and options; only RBS diagnostic fixture and evidence runner changed"}
    if reuse_glad:
        assert not control and not reuse
        old = local(BASE / ("candidate-" + reuse_glad))
        original = json.loads((old / "scenario-manifest.json").read_text())
        built = json.loads((old / "build-Debug/result.json").read_text())
        assert built["exits"][0] == 0
        allowed = {"GEngine/src/Managers/InputManager.cpp", "tools/test_pre_editor_input_routing.py",
                   "RigidBodySimulation/tests/InputRoutingChecks.h", "RigidBodySimulation/src/RbsValidation.cpp",
                   "GEngine/src/Core/GEngine.cpp"}
        for name, value in original["input_hashes"].items():
            if name not in allowed and not name.endswith("/isolated.props"):
                assert digest(ROOT / name) == value, "Reuse input changed: " + name
        manifest["reuse_glad"] = {"candidate": reuse_glad, "configuration": "Debug",
            "manifest": identity(old / "scenario-manifest.json"),
            "build": identity(old / "build-Debug/result.json"),
            "library": identity(ROOT / f"bin/Debug/Phase15Candidate{reuse_glad}/glad/glad.lib"),
            "reason": "Only declared engine implementation, RBS fixture/integration and evidence runner changed; glad source, compiler and options match"}
    if reuse_debug:
        assert not control and not reuse and not reuse_glad
        old = local(BASE / ("candidate-" + reuse_debug))
        original = json.loads((old / "scenario-manifest.json").read_text())
        built = json.loads((old / "build-Debug/result.json").read_text())
        assert built["result"] == "PASS"
        allowed = {"RigidBodySimulation/tests/InputRoutingChecks.h", "RigidBodySimulation/src/RbsValidation.cpp",
                   "tools/test_pre_editor_input_routing.py"}
        for name, value in original["input_hashes"].items():
            if name not in allowed and not name.endswith("/isolated.props"):
                assert digest(ROOT / name) == value, "Reuse input changed: " + name
        assert (old / "isolated.props").read_text().replace("Phase15Candidate" + reuse_debug,
            "Phase15Candidate" + candidate) == props.read_text()
        manifest["reuse_debug"] = {"candidate": reuse_debug, "manifest": identity(old / "scenario-manifest.json"),
            "build": identity(old / "build-Debug/result.json"),
            "libraries": {name: identity(ROOT / f"bin/Debug/Phase15Candidate{reuse_debug}/{name}/{name}.lib")
                          for name in ("glad", "GEngine")},
            "reason": "Only diagnostic RBS fixture/integration and runner differ; engine/dependency source and options match exactly"}
    save(out / "scenario-manifest.json", manifest)
    verify(out)
    print("Frozen", len(names), "relevant inputs", flush=True)


def verify(out):
    guards()
    manifest = json.loads((out / "scenario-manifest.json").read_text())
    for name, expected in manifest["input_hashes"].items():
        assert digest(ROOT / name) == expected, name
    for name, expected in manifest["external_inputs"].items():
        assert identity(name) == expected, name
    assert manifest["compile_members"] == closure.compile_members()
    assert manifest["compile_discovery"] == closure.discovery()
    assert all(not (ROOT / p).exists() for p in manifest["include_preceding_absent"])
    return manifest


def build(out, cfg):
    manifest = verify(out)
    candidate = manifest["candidate"]
    folder = local(out / ("build-" + cfg))
    folder.mkdir()
    report = {"result": "FAIL", "recorded_utc": utc(), "commands": [], "exits": []}
    common = [f"/p:Configuration={cfg}", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
              "/p:WindowsTargetPlatformVersion=10.0.26100.0",
              "/p:ForceImportBeforeCppTargets=" + str(out / "isolated.props")]
    start = time.monotonic()
    try:
        projects = (PROJECTS[1], PROJECTS[0], PROJECTS[2])
        if "reuse_debug" in manifest and cfg == "Debug":
            reuse = manifest["reuse_debug"]
            for name, expected in reuse["libraries"].items():
                source = ROOT / f"bin/Debug/Phase15Candidate{reuse['candidate']}/{name}/{name}.lib"
                assert identity(source) == expected
                destination = local(f"bin/{cfg}/Phase15Candidate{candidate}/{name}/{name}.lib")
                destination.parent.mkdir(parents=True, exist_ok=False)
                shutil.copyfile(source, destination)
            report["reuse_debug"] = reuse
            projects = (PROJECTS[2],)
        if "reuse_glad" in manifest and cfg == manifest["reuse_glad"]["configuration"]:
            reuse = manifest["reuse_glad"]
            source = ROOT / f"bin/{cfg}/Phase15Candidate{reuse['candidate']}/glad/glad.lib"
            assert identity(source) == reuse["library"]
            destination = local(f"bin/{cfg}/Phase15Candidate{candidate}/glad/glad.lib")
            destination.parent.mkdir(parents=True, exist_ok=False)
            shutil.copyfile(source, destination)
            report["reuse_glad"] = reuse
            projects = (PROJECTS[0], PROJECTS[2])
        if "reuse" in manifest:
            reuse = manifest["reuse"]
            assert cfg == "Release"
            for name, expected in reuse["libraries"].items():
                source = ROOT / f"bin/Release/Phase15Candidate{reuse['candidate']}/{name}/{name}.lib"
                assert identity(source) == expected
                destination = local(f"bin/{cfg}/Phase15Candidate{candidate}/{name}/{name}.lib")
                destination.parent.mkdir(parents=True, exist_ok=False)
                shutil.copyfile(source, destination)
            report["reuse"] = reuse
            projects = (PROJECTS[2],)
        for project in projects:
            args = [str(MSBUILD), project, *common]
            evaluated = subprocess.run(args + ["/getProperty:OutDir,IntDir,TargetPath,PostBuildEventUseInBuild,VCToolsVersion,WindowsTargetPlatformVersion", "/nologo"],
                                       cwd=ROOT, env=environment(), capture_output=True)
            save(folder / (Path(project).stem + "-evaluation.json"), {"command": args, "exit": evaluated.returncode,
                "stdout": evaluated.stdout.decode(errors="replace"), "stderr": evaluated.stderr.decode(errors="replace")})
            assert evaluated.returncode == 0
            properties = json.loads(evaluated.stdout)["Properties"]
            for key in ("OutDir", "IntDir", "TargetPath"):
                assert f"Phase15Candidate{candidate}" in str(local(properties[key]))
            assert properties["PostBuildEventUseInBuild"] == "false"
            assert properties["VCToolsVersion"] == "14.44.35207" and properties["WindowsTargetPlatformVersion"] == "10.0.26100.0"
            command = args + ["/t:Build", "/p:BuildProjectReferences=false", "/m:2", "/nr:false", "/nologo", "/v:normal",
                              "/bl:" + str(folder / (Path(project).stem + ".binlog"))]
            report["commands"].append(command)
            with (folder / (Path(project).stem + ".log")).open("xb") as stream:
                result = subprocess.run(command, cwd=ROOT, env=environment(), stdout=stream, stderr=subprocess.STDOUT,
                    timeout=max(1, 1800 - (time.monotonic() - start)), creationflags=subprocess.CREATE_NO_WINDOW)
            report["exits"].append(result.returncode)
            assert result.returncode == 0, Path(project).stem + " compilation failed"
        verify(out)
        products = [f"bin/{cfg}/Phase15Candidate{candidate}/{p}/{p}.{ext}" for p, ext in
                    [("glad", "lib"), ("GEngine", "lib"), ("RigidBodySimulation", "exe")]]
        report.update(result="PASS", products={p: identity(ROOT / p) for p in products})
    except Exception as error:
        report["reason"] = str(error)
    report["seconds"] = time.monotonic() - start
    save(folder / "result.json", report)
    print(json.dumps(report), flush=True)
    if report["result"] != "PASS":
        raise SystemExit(1)


def stage(out, cfg):
    import postbuild
    manifest = verify(out)
    assert json.loads((out / f"build-{cfg}/result.json").read_text())["result"] == "PASS"
    target = local(f"bin/{cfg}/Phase15Candidate{manifest['candidate']}")
    assert not (target / "assets").exists()
    postbuild.stage_runtime_assets(ROOT, target / "assets")
    for data in manifest["expected_modules"][cfg].values():
        source = Path(data["path"])
        assert digest(source) == data["sha256"], str(source)
        destination = local(target / "RigidBodySimulation" / source.name)
        assert not destination.exists()
        shutil.copyfile(source, destination)
    for asset, source in manifest["asset_members"].items():
        assert digest(target / "assets" / asset) == digest(ROOT / source)
    save(out / f"stage-{cfg}.json", {"result": "PASS", "recorded_utc": utc(),
        "products": {p.relative_to(ROOT).as_posix(): identity(p) for p in target.rglob("*") if p.is_file() and p.suffix != ".pdb"}})


@contextmanager
def desktop_envelope(out):
    """Temporarily hide only the proven interfering overlay; restore in finally."""
    user = ctypes.WinDLL("user32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.IsWindowVisible.argtypes = user.IsWindow.argtypes = [wintypes.HWND]
    user.ShowWindowAsync.argtypes = [wintypes.HWND, ctypes.c_int]
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    record = {"recorded_utc": utc(), "targets": [], "restored": False}
    def window_identity(handle):
        pid = wintypes.DWORD()
        user.GetWindowThreadProcessId(handle, ctypes.byref(pid))
        name = ctypes.create_unicode_buffer(256)
        user.GetClassNameW(handle, name, len(name))
        return pid.value, name.value
    def wait_visible(handle, visible):
        end = time.monotonic() + 2
        while bool(user.IsWindowVisible(handle)) != visible and time.monotonic() < end:
            time.sleep(.02)
        return bool(user.IsWindowVisible(handle)) == visible
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(handle, unused):
        pid, name = window_identity(handle)
        if name != "mininews" or not user.IsWindowVisible(handle):
            return True
        process = kernel.OpenProcess(0x1000, False, pid)
        if not process:
            return True
        path, length = ctypes.create_unicode_buffer(32768), wintypes.DWORD(32768)
        try:
            known = kernel.QueryFullProcessImageNameW(process, 0, path, ctypes.byref(length))
        finally:
            kernel.CloseHandle(process)
        if known and Path(path.value).name.lower() == "360huabao.exe":
            rect = wintypes.RECT()
            user.GetWindowRect(handle, ctypes.byref(rect))
            record["targets"].append({"handle": handle, "pid": pid, "class": name,
                "process": path.value, "visible_before": True,
                "rect": [rect.left, rect.top, rect.right, rect.bottom]})
        return True
    try:
        assert user.EnumWindows(visit, 0), "Native overlay enumeration failed"
        for target in record["targets"]:
            handle = target["handle"]
            target["hide_requested"] = bool(user.ShowWindowAsync(handle, 0))
            target["hidden"] = wait_visible(handle, False)
            assert target["hidden"], "Interfering overlay could not be hidden"
        yield
    finally:
        restored = True
        for target in record["targets"]:
            handle = target["handle"]
            if user.IsWindow(handle) and window_identity(handle) == (target["pid"], target["class"]):
                target["restore_requested"] = bool(user.ShowWindowAsync(handle, 8))
                target["restored"] = wait_visible(handle, True)
                restored &= target["restored"]
            else:
                target["restored"] = "Original window no longer exists; no foreign handle touched"
        record["restored"] = restored
        save(out / "desktop-envelope.json", record)
        assert restored, "Overlay visibility restoration failed"


def run(out, cfg):
    manifest = verify(out)
    candidate = manifest["candidate"]
    assert json.loads((out / f"stage-{cfg}.json").read_text())["result"] == "PASS"
    directory = local(f"runtime/RigidBodySimulation/{cfg}/phase-15-candidate-{candidate}")
    directory.mkdir(parents=True, exist_ok=False)
    (directory / "imgui.ini").write_text(manifest["layout"], encoding="utf-8")
    executable = local(f"bin/{cfg}/Phase15Candidate{candidate}/RigidBodySimulation/RigidBodySimulation.exe")
    report = {"result": "INCONCLUSIVE", "recorded_utc": utc(), "command": [str(executable)],
        "cwd": str(directory), "binary": identity(executable)}
    start = time.monotonic()
    with desktop_envelope(directory), (directory / "process-output.log").open("xb") as stream:
        process = subprocess.Popen([str(executable)], cwd=directory, env=environment(), stdout=stream,
            stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        report["pid"] = process.pid
        try:
            report["exit"] = process.wait(timeout=180)
            log = (directory / "GEngine.log").read_text(errors="replace")
            report["result"] = "PASS" if process.returncode == 0 and "PRE_EDITOR_PHASE_15_PASS" in log else "FAIL"
        except subprocess.TimeoutExpired:
            report["reason"] = "Runtime deadline; no PASS inferred"
            process.kill()
            process.wait()
    report["seconds"] = time.monotonic() - start
    report["desktop_envelope"] = identity(directory / "desktop-envelope.json")
    archive = local(out / ("runtime-" + cfg))
    archive.mkdir()
    for p in directory.iterdir():
        if p.is_file():
            shutil.copyfile(p, archive / p.name)
    report["evidence"] = {p.name: identity(p) for p in archive.iterdir() if p.is_file()}
    verify(out)
    save(out / f"run-{cfg}.json", report)
    print(json.dumps(report), flush=True)
    if report["result"] != "PASS":
        raise SystemExit(1)


def analyze(out):
    manifest = verify(out)
    control = BASE / "candidate-02"
    old = (control / "source-snapshot/RigidBodySimulation/tests/InputRoutingChecks.h").read_text()
    new = (ROOT / "RigidBodySimulation/tests/InputRoutingChecks.h").read_text()
    def measurement_region(source):
        return source.split("    inline PlatformResult Measure(BaseApp& app)", 1)[1].split("    inline void Describe", 1)[0]
    assert measurement_region(old) == measurement_region(new), "Measurement implementation drift requires comparability review"
    for cfg in ("Debug", "Release"):
        assert json.loads((out / f"run-{cfg}.json").read_text())["result"] == "PASS"
        log = (out / f"runtime-{cfg}/GEngine.log").read_text()
        for marker in ("STATE_PASS", "ACTIONS_PASS", "UI_PASS", "PHASE_15_PASS"):
            assert marker in log, marker
    def rows(folder):
        data = list(csv.DictReader((folder / "runtime-Release/input-cost.csv").open()))
        assert len(data) == 1440
        assert len({(r["pair"], r["on"], r["sample"]) for r in data}) == 1440
        assert all(math.isfinite(float(r[k])) for r in data for k in ("batch_ms", "max_latency_ms"))
        return data
    original, current = rows(control), rows(out)
    assert all(int(r["deliveries"]) == 48 for r in original)
    assert all(int(r["deliveries"]) == 64 and int(r["native_remaining"]) == 0 for r in current)
    groups = []
    passed = True
    for pair in range(3):
        for lane in range(2):
            select = lambda data: [r for r in data if int(r["pair"]) == pair and int(r["on"]) == lane]
            baseline, candidate = select(original), select(current)
            assert len(baseline) == len(candidate) == 240
            def stats(data):
                values = sorted(float(r["batch_ms"]) for r in data)
                return {"median_ms": statistics.median(values), "p95_ms": values[math.ceil(.95 * len(values)) - 1],
                        "max_ms": max(values), "max_latency_ms": max(float(r["max_latency_ms"]) for r in data),
                        "provider_allocations": sorted({int(r["allocations"]) for r in data}) if lane else None,
                        "provider_requested_bytes": sorted({int(r["requested_bytes"]) for r in data}) if lane else None,
                        "lookup_range": [min(int(r["lookups"]) for r in data), max(int(r["lookups"]) for r in data)] if lane else None,
                        "logical_value_copies": int(data[0]["logical_value_copies"])}
            a, b = stats(baseline), stats(candidate)
            valid = b["median_ms"] <= a["median_ms"] + .100 and b["p95_ms"] <= a["p95_ms"] + .250
            valid = valid and (not lane or b["max_latency_ms"] <= 16.667)
            passed &= valid
            groups.append({"pair": pair, "on": lane, "control": a, "candidate": b, "result": "PASS" if valid else "FAIL"})
    record = {"phase": 15, "result": "PASS" if passed else "FAIL", "recorded_utc": utc(),
        "groups": groups, "control": {"path": str(control), "run": identity(control / "run-Release.json"),
        "raw": identity(control / "runtime-Release/input-cost.csv")},
        "candidate_raw": identity(out / "runtime-Release/input-cost.csv"),
        "comparability": "Same measurement function bytes, native workload, clocks, compiler/CRT/configuration, scene dimensions and pinned dependencies; added input semantics are the candidate change",
        "limits": manifest["protocol"]["limits"],
        "counter_limits": "OFF optional counters are unmeasured; zero CSV placeholders are not observations. Provider storage only. Trivial copies are language-level transfers, not hardware memcpy measurements. SDL/driver allocation is unmeasured.",
        "observer_overhead_ms": [{"pair": pair,
            "control_median": groups[2 * pair + 1]["control"]["median_ms"] - groups[2 * pair]["control"]["median_ms"],
            "candidate_median": groups[2 * pair + 1]["candidate"]["median_ms"] - groups[2 * pair]["candidate"]["median_ms"]} for pair in range(3)]}
    save(out / "summary.json", record)
    print(json.dumps(record), flush=True)
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("operation", choices=["prepare", "build", "stage", "run", "verify", "analyze"])
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--control", action="store_true")
    parser.add_argument("--reuse")
    parser.add_argument("--reuse-glad")
    parser.add_argument("--reuse-debug")
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Release")
    args = parser.parse_args()
    assert len(args.candidate) == 2 and args.candidate.isdigit()
    output = local(BASE / ("candidate-" + args.candidate))
    if args.operation == "prepare":
        prepare(output, args.candidate, args.control, args.reuse, args.reuse_glad, args.reuse_debug)
    elif args.operation == "verify":
        verify(output)
    elif args.operation == "analyze":
        analyze(output)
    else:
        globals()[args.operation](output, args.configuration)
