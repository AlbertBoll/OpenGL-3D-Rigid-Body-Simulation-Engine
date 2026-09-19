"""Build and validate immutable submission, scheduling and pass invalidation."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import winreg
import ctypes
from ctypes import wintypes
import time
import csv
import statistics
from rendering_baseline import LAYOUT, APPS, samples, quantiles, image_region

from rendering_validation import ROOT, toolchain


def loaded_modules(pid):
    """Inspect this process, including the actual app-local/system DLL paths."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE),
                                         wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
    psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
    process = kernel.OpenProcess(0x0400 | 0x0010, False, pid)
    if not process:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        modules = (wintypes.HMODULE * 1024)()
        needed = wintypes.DWORD()
        if not psapi.EnumProcessModulesEx(process, modules, ctypes.sizeof(modules), ctypes.byref(needed), 3):
            raise ctypes.WinError(ctypes.get_last_error())
        if needed.value > ctypes.sizeof(modules):
            raise OSError("Module inspection buffer too small")
        result = {}
        for module in modules[:needed.value // ctypes.sizeof(wintypes.HMODULE)]:
            name = ctypes.create_unicode_buffer(32768)
            if not psapi.GetModuleFileNameExW(process, module, name, len(name)):
                raise ctypes.WinError(ctypes.get_last_error())
            path = Path(name.value).resolve()
            result[path.name.lower()] = str(path)
        return result
    finally:
        kernel.CloseHandle(process)


def application_smoke(executable, directory, env, required_modules=None):
    """Exercise only the launched process's window, then request native close."""
    directory.mkdir(parents=True, exist_ok=True)
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM,
                                          wintypes.LPARAM, wintypes.UINT, wintypes.UINT, ctypes.POINTER(ctypes.c_size_t)]
    log = directory / "application.log"
    record = {"command": [str(executable)], "cwd": str(directory), "log": str(log),
              "binary_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(), "result": "FAIL"}
    child_env = dict(env)
    child_env.pop("GENGINE_BASELINE_OUTPUT", None)
    record["asset_root"] = child_env.get("GENGINE_ASSET_ROOT")
    record["path"] = next((v for k, v in child_env.items() if k.lower() == "path"), "")
    with log.open("wb") as output:
        child = subprocess.Popen([str(executable)], cwd=directory, env=child_env,
                                 stdout=output, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        record["pid"] = child.pid
        try:
            deadline = time.monotonic() + 60
            owned = []
            @callback_type
            def enumerate_window(hwnd, _):
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                window_class = ctypes.create_unicode_buffer(256)
                title = ctypes.create_unicode_buffer(256)
                user32.GetClassNameW(hwnd, window_class, 256)
                user32.GetWindowTextW(hwnd, title, 256)
                if pid.value == child.pid and window_class.value == "SDL_app" and title.value and user32.IsWindowVisible(hwnd):
                    owned.append(hwnd)
                return True
            ready = None
            while time.monotonic() < deadline and child.poll() is None:
                owned.clear()
                user32.EnumWindows(enumerate_window, 0)
                reply = ctypes.c_size_t()
                for hwnd in owned:
                    if user32.SendMessageTimeoutW(hwnd, 0, 0, 0, 2, 250, ctypes.byref(reply)):
                        ready = hwnd
                        break
                if ready:
                    break
                time.sleep(.2)
            if ready is None:
                record["reason"] = "No responsive owned window before exit/deadline"
            else:
                title = ctypes.create_unicode_buffer(256)
                user32.GetWindowTextW(ready, title, 256)
                record["window_title"] = title.value
                record["window_class"] = "SDL_app"
                time.sleep(3)
                reply = ctypes.c_size_t()
                if child.poll() is not None:
                    record["reason"] = "Application exited before the shutdown request"
                elif not user32.SendMessageTimeoutW(ready, 0, 0, 0, 2, 5000, ctypes.byref(reply)):
                    record["reason"] = "Owned window became unresponsive"
                else:
                    record["responsive"] = True
                    closure = True
                    if required_modules is not None:
                        record["loaded_modules"] = loaded_modules(child.pid)
                        record["required_modules"] = required_modules
                        for name, expected in required_modules.items():
                            actual = record["loaded_modules"].get(name.lower())
                            if (actual is None or Path(actual) != Path(expected["path"]).resolve()
                                    or hashlib.sha256(Path(actual).read_bytes()).hexdigest() != expected["sha256"]):
                                closure = False
                        record["runtime_closure"] = "PASS" if closure else "FAIL"
                    record["native_close_posted"] = bool(user32.PostMessageW(ready, 0x0010, 0, 0))
                    record["exit"] = child.wait(timeout=30)
                    record["result"] = "PASS" if closure and record["native_close_posted"] and record["exit"] == 0 else "FAIL"
        except OSError as error:
            record["reason"] = str(error)
        except subprocess.TimeoutExpired:
            record["reason"] = "Owned process failed to shut down after native close"
            debugger = Path(r"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe")
            if debugger.is_file() and child.poll() is None:
                diagnostic = directory / "shutdown-stack.log"
                with diagnostic.open("wb") as stack:
                    try:
                        subprocess.run([str(debugger), "-p", str(child.pid), "-c", "~* kb;qd"],
                                       stdout=stack, stderr=subprocess.STDOUT, timeout=20,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
                    except (OSError, subprocess.TimeoutExpired) as error:
                        record["diagnostic_error"] = str(error)
                record["shutdown_stack"] = str(diagnostic)
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=10)
                record["terminated"] = True
            record.setdefault("exit", child.returncode)
    (directory / "smoke.json").write_text(json.dumps(record, indent=2) + "\n")
    print(f"[{record['result']}] smoke {executable.stem}: exit={record['exit']}", flush=True)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=["Debug", "Release"], required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--no-build", action="store_true", help="Reuse matching affected-consumer builds")
    parser.add_argument("--smoke", action="store_true", help="All four graphical applications: responsiveness and native shutdown")
    parser.add_argument("--scene-variants", action="store_true", help="Compile and smoke each authored rigid-body scene")
    parser.add_argument("--timing-baseline", action="store_true", help="Release: capture the secondary Phase 48 per-pass reference")
    parser.add_argument("--state-cache-measure", action="store_true", help="Three frozen submission series, 120 warmup and 240 samples each")
    parser.add_argument("--state-cache-baseline", type=Path, help="Measurement-only link against an explicitly preserved predecessor library; requires --no-build")
    parser.add_argument("--draw-sort-measure", action="store_true", help="Matched mixed-resource ordering workload, three frozen series")
    parser.add_argument("--draw-sort-baseline", type=Path, help="Preserved Phase 52 library, measurement only; requires --no-build")
    parser.add_argument("--draw-sort-reference", type=Path, help="Baseline results.json for exact image/work and reduced bind gates")
    args = parser.parse_args()
    if args.draw_sort_baseline and (not args.no_build or not args.draw_sort_measure):
        parser.error("Sort baseline requires --no-build --draw-sort-measure")
    if args.draw_sort_reference and (not args.draw_sort_measure or args.draw_sort_baseline):
        parser.error("Sort reference requires candidate --draw-sort-measure")

    if args.state_cache_baseline and (not args.no_build or not args.state_cache_measure):
        parser.error("Preserved baseline requires --no-build --state-cache-measure")
    config = args.configuration
    if args.timing_baseline and (config != "Release" or args.no_build):
        parser.error("Timing capture requires a fresh Release measurement build")
    out = (args.output or ROOT / "logs/rendering/phase47/final" / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {"configuration": config, "steps": []}
    env = {k: v for k, v in os.environ.items() if k.lower() != "path"}
    env["Path"] = os.environ.get("PATH", os.environ.get("Path", ""))
    env.pop("SDL_VIDEODRIVER", None)
    env["CL"] = env.get("CL", "") + " /we4715 /we4716"

    def invoke(name, command, timeout=60, cwd=ROOT, child_env=None, marker=None, expected_failure=False):
        command = [str(value) for value in command]
        log = out / (name + ".log")
        step = {"name": name, "command": command, "cwd": str(cwd), "log": str(log), "required_output": marker, "expected_failure": expected_failure}
        report["steps"].append(step)
        try:
            result = subprocess.run(command, cwd=cwd, env=child_env or env, capture_output=True,
                                    timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            log.write_bytes(result.stdout + result.stderr)
            step["exit"] = result.returncode
            step["result"] = "PASS" if (result.returncode != 0 if expected_failure else result.returncode == 0) else "FAIL"
            if marker and marker.encode() not in result.stdout:
                step["result"] = "FAIL"
        except subprocess.TimeoutExpired as error:
            log.write_bytes((error.stdout or b"") + (error.stderr or b""))
            step.update(exit=None, result="FAIL", reason="Timeout; owned child terminated")
        print(f"[{step['result']}] {name}: exit={step['exit']}", flush=True)
        return step["result"] == "PASS"

    passed = False
    try:
        msbuild, vc = toolchain()  # Existing VS2022/v143 discovery and static CRT policy.
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots") as key:
            sdk = Path(winreg.QueryValueEx(key, "KitsRoot10")[0])
        sdk_version = max((p.name for p in (sdk / "Lib").iterdir()
                           if (p / "um/x64/kernel32.lib").is_file()), key=lambda name: tuple(map(int, name.split("."))))
        report["toolchain"] = {"msbuild": str(msbuild), "msvc": str(vc), "sdk": str(sdk), "sdk_version": sdk_version}
        if vc.name != "14.44.35207" or sdk_version != "10.0.26100.0":
            report["reason"] = "Recorded compiler/SDK unavailable; toolchain migration is not authorized"
            return 1
        if not args.no_build and not invoke("generate", [ROOT / "vendor/bin/premake/premake5.exe", *(["--render-baseline"] if args.timing_baseline else []), "vs2022"], 120):
            return 1
        report["compiler_return_guards"] = env["CL"]
        targets = "GEngineEditor;Breakout;RayTracing;RigidBodySimulation;" + ("PhysicsBenchmark;RenderingValidation" if args.timing_baseline else "PhysicsTests;PhysicsBenchmark")
        build = [msbuild, ROOT / "GEngine.sln", "/t:" + targets,
                 "/m:1", "/nr:false", "/nologo", "/v:normal",
                 "/p:Configuration=" + config, "/p:Platform=x64", "/p:VCToolsVersion=" + vc.name,
                 "/p:WindowsTargetPlatformVersion=" + sdk_version, "/bl:" + str(out / "build.binlog")]
        if not args.no_build and not invoke("build", build, 1200):
            return 1
        includes = [vc / "include", *(sdk / "Include" / sdk_version / part for part in ("ucrt", "shared", "um")),
                    ROOT / "GEngine/include", ROOT / "GEngine/include/GEngine", ROOT / "GEngine/include/external",
                    *(ROOT / "external" / part / "include" for part in ("sdl2", "spdlog", "glad", "assimp", "entt", "tbb", "fmod"))]
        libraries = [vc / "lib/x64", sdk / "Lib" / sdk_version / "ucrt/x64",
                     sdk / "Lib" / sdk_version / "um/x64",
                     *(ROOT / "external" / part / "lib" for part in ("sdl2", "tbb", "assimp", "fmod"))]
        consumer = ROOT / "tools/frame_submission_probe.cpp"
        native_free = [path for path in includes if not any(part in path.parts for part in ("sdl2", "glad", "assimp", "tbb"))]
        if not invoke("frame-consumer-boundary", [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++23preview",
                      "/EHsc", "/W3", "/MTd" if config == "Debug" else "/MT", "/c", "/showIncludes",
                      "/DSUBMISSION_SCHEMA_ONLY", "/D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING",
                      *["/I" + str(p) for p in native_free], consumer, "/Fo" + str(out / "frame-consumer.obj")], 120):
            return 1
        dependencies = (out / "frame-consumer-boundary.log").read_text(errors="replace").lower().replace("\\", "/")
        if any(token in dependencies for token in ("/glad/", "/sdl2/", "/opengl/", "/imgui/", "/assimp/", "/gepch.h", "backend.h")):
            report["reason"] = "Concrete backend declaration leaked into the extraction consumer"
            return 1
        report["consumer_boundary"] = "PASS"
        app_includes = [path for path in includes if not any(part in path.parts for part in ("sdl2", "glad", "assimp"))]
        if not invoke("application-consumer-boundary", [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++23preview",
                      "/EHsc", "/W3", "/MTd" if config == "Debug" else "/MT", "/c", "/showIncludes",
                      "/DGENGINE_PLATFORM_WINDOWS", "/DGENGINE_CONFIG_" + config.upper(),
                      *["/I" + str(p) for p in app_includes], "/I" + str(ROOT / "RigidBodySimulation/include"),
                      ROOT / "RigidBodySimulation/src/RigidBodySimulation.cpp", "/Fo" + str(out / "app-consumer.obj")], 120):
            return 1
        app_dependencies = (out / "application-consumer-boundary.log").read_text(errors="replace").lower().replace("\\", "/")
        if any(token in app_dependencies for token in ("/glad/", "/sdl2/", "/opengl/", "/assimp/", "/gepch.h", "backend.h", "imgui_impl_")):
            report["reason"] = "Concrete backend leaked into the normal application translation unit"
            return 1
        for rel in ("GEngine/include/GEngine/Renderer/FrameSubmission.h", "GEngine/src/Renderer/FrameSubmission.cpp",
                    "GEngine/src/Renderer/GLStateCache.h", "GEngine/src/Renderer/DrawOrdering.h", "GEngine/src/Assets/ShaderBackend.h",
                    "GEngine/src/Assets/Sampler.cpp", "GEngine/src/Mesh/GpuMesh.cpp",
                    "GEngine/include/GEngine/Renderer/PassTiming.h", "GEngine/src/Renderer/PassTiming.cpp",
                    "GEngine/include/GEngine/Core/Window.h", "GEngine/src/Windows/SDLWindow.cpp",
                    "GEngine/include/GEngine/Core/FrameBuffer.h", "GEngine/src/Core/FrameBuffer.cpp",
                    "GEngine/include/GEngine/Renderer/SceneRenderResources.h", "GEngine/src/Renderer/SceneRenderResources.cpp",
                    "RigidBodySimulation/src/RigidBodySimulation.cpp", "RigidBodySimulation/src/main.cpp",
                    "GEngine/include/GEngine/Renderer/FrameScheduler.h", "GEngine/src/Renderer/FrameScheduler.cpp",
                    "GEngine/src/Core/BaseApp.cpp", "Breakout/src/BreakoutApp.cpp", "RayTracing/src/RayTracing.cpp"):
            source = re.sub(r"//[^\n]*|/\*.*?\*/", "", (ROOT / rel).read_text(), flags=re.S)
            if re.search(r"\b(throw|try|catch|enable_if|exception_ptr)\b", source):
                report["reason"] = "New exception/SFINAE boundary: " + rel
                return 1
        submission = (ROOT / "GEngine/src/Renderer/FrameSubmission.cpp").read_text()
        if re.search(r"GetComponent|GetAllEntities|GetGroupEntities|AssetsManager|ShapeManager|ShaderManager|BeginFrame|BeginPublication", submission):
            report["reason"] = "Submission performs ECS/manager lookup or creates an access scope"
            return 1
        for rel, owner in (("RigidBodySimulation/src/RigidBodySimulation.cpp", "RigidBodySimulationApp"),
                           ("GEngine/src/Core/BaseApp.cpp", "BaseApp"),
                           ("Breakout/src/BreakoutApp.cpp", "BreakoutApp"),
                           ("RayTracing/src/RayTracing.cpp", "RayTracingAPP")):
            source = (ROOT / rel).read_text()
            body = source.split("void " + owner + "::Render()", 1)[1].split("\n}", 1)[0] if owner == "RigidBodySimulationApp" else source.split("void " + owner + "::Render()", 1)[1].split("\n    }", 1)[0]
            if "FrameScheduler::Render" not in body or re.search(r"\b(SwapBuffer|BeginUI|EndUI|ExtractRenderFrame|BindAndBlitToScreen)\s*\(", body):
                report["reason"] = "Application still sequences frame pipeline: " + rel
                return 1
        submission_body=submission.split("        void Apply(",1)[1]
        if re.search(r"\bgl(?:UseProgram|BindVertexArray|BindFramebuffer|BindTexture|BindSampler|ActiveTexture|Enable|Disable|Viewport|Scissor|DepthMask|DepthFunc|ColorMask|StencilMask|BlendFuncSeparate|BlendEquationSeparate|CullFace|FrontFace|PolygonMode|LineWidth|DepthRange|ClearDepth|ClearColor|DrawBuffer|ReadBuffer)\s*\(", submission_body):
            report["reason"]="Migrated submission bypasses private state cache"
            return 1
        report["architecture"] = {"no_new_exception": "PASS", "consumer_boundary": "PASS", "application_pipeline_ownership": "PASS"}

        baseline_library = args.draw_sort_baseline or args.state_cache_baseline
        library = baseline_library.resolve() if baseline_library else ROOT / "bin" / config / "GEngine/GEngine.lib"
        executable = out / "frame-submission-probe.exe"
        command = [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++23preview", "/EHsc", "/W3",
                   *(["/DGENGINE_RENDER_COUNTERS=1", "/DGE_ENABLE_PHYSICS_PROFILING"] if args.timing_baseline else []),
                   "/MTd" if config == "Debug" else "/MT", "/Od" if config == "Debug" else "/O2",
                   "/DSDL_MAIN_HANDLED", "/DGENGINE_PLATFORM_WINDOWS", "/DGENGINE_CONFIG_" + config.upper(),
                   *["/I" + str(p) for p in includes], "/external:W0", "/external:templates-",
                   *["/external:I" + str(p) for p in (ROOT / "external", ROOT / "GEngine/include/external", vc / "include")],
                   "/I" + str(ROOT / "RigidBodySimulation/include"),
                   ROOT / "tools/frame_submission_probe.cpp", ROOT / "RigidBodySimulation/src/main.cpp",
                   "/Fo" + str(out) + os.sep,
                   "/Fe" + str(executable), "/link", "/SUBSYSTEM:CONSOLE",
                   *(["/OPT:NOREF", "/OPT:NOICF"] if config == "Debug" else []),
                   *["/LIBPATH:" + str(p) for p in libraries], library,
                   ROOT / "external/glad/bin" / config / "glad/glad.lib", "SDL2.lib", "SDL2_ttf.lib",
                   "tbb12.lib", "tbb12_debug.lib", "tbb.lib", "tbb_debug.lib", "assimp.lib",
                   "fmod64_vc.lib", "fmodL64_vc.lib", "fmodstudio64_vc.lib", "fmodstudioL64_vc.lib"]
        if not invoke("compile-probe", command, 120):
            return 1
        sdl = ROOT / "bin" / config / "GEngineEditor/SDL2.dll"
        report["inputs"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                            (executable, library, sdl)}
        env["Path"] = str(sdl.parent) + os.pathsep + env["Path"]
        env["GENGINE_ASSET_ROOT"] = str(ROOT / "bin" / config / "assets")
        env["GENGINE_SHADOW_RESOLUTION"] = "256"
        env["GENGINE_PASS_TIMING"] = "1"
        if baseline_library:
            env["GENGINE_PREDECESSOR_PLACEMENT"] = "1"
        else:
            env.pop("GENGINE_PREDECESSOR_PLACEMENT", None)
        if args.draw_sort_measure:
            protocol = {"workload": "64 overlapping opaque/masked draws; 4 pipelines, 8 materials, 2 meshes; 64x64 linear RGBA8; fixed orthographic camera; no Physics/picking; empty shadow targets cached after warmup",
                "warmup": 120, "samples": 240, "series": 3,
                "noise_policy": "Run-median spread above 10% is NOISY. Timing is descriptive; exact image/work equality and reduced program/texture/VAO calls are required regardless of timing.",
                "scope": "Submit CPU only, includes ordering; excludes extraction, simulation, readback and swap. No GPU timing claim."}
            report["draw_sort_protocol"] = protocol
            (out / "draw-sort-protocol.json").write_text(json.dumps(protocol, indent=2))
            records = []
            for series in range(3):
                capture = out / f"draw-sort-{series}.csv"
                measured_env = dict(env, GENGINE_DRAW_SORT_MEASURE=str(capture))
                if not invoke(f"draw-sort-{series}", [executable], 180, out, measured_env,
                              marker="[PASS] draw-sort opaque/masked-equivalence/immutable/transparent-overlap/stable-ties"):
                    return 1
                with capture.open(newline="") as stream:
                    rows = list(csv.DictReader(stream))
                if len(rows) != 240:
                    return 1
                cpu = sorted(int(r["cpu_ns"]) for r in rows)
                counts = {k: sorted({int(r[k]) for r in rows}) for k in rows[0] if k not in ("iteration", "cpu_ns")}
                if any(len(v) != 1 for v in counts.values()) or sum(counts[k][0] for k in ("DrawArrays", "DrawElements")) != 64:
                    return 1
                records.append({"series": series, "cpu_median_ns": statistics.median(cpu),
                    "cpu_p95_ns": cpu[int(.95*(len(cpu)-1))], "driver_calls": counts,
                    "image_sha256": hashlib.sha256(Path(str(capture)+".rgba").read_bytes()).hexdigest()})
            medians = [r["cpu_median_ns"] for r in records]
            report["draw_sort_measurements"] = {"runs": records,
                "median_spread": (max(medians)-min(medians))/statistics.median(medians)}
            if args.draw_sort_reference:
                reference = json.loads(args.draw_sort_reference.read_text())
                previous = reference["draw_sort_measurements"]["runs"]
                matching = reference["configuration"] == config and reference["draw_sort_protocol"] == protocol
                for before, after in zip(previous, records, strict=True):
                    matching &= before["image_sha256"] == after["image_sha256"]
                    for name in ("DrawArrays", "DrawElements", "BindSampler"):
                        matching &= before["driver_calls"][name] == after["driver_calls"][name]
                    for name in ("UseProgram", "BindVertexArray", "BindTexture"):
                        matching &= after["driver_calls"][name][0] < before["driver_calls"][name][0]
                report["draw_sort_comparison"] = {"reference": str(args.draw_sort_reference),
                    "result": "PASS" if matching else "FAIL"}
                if not matching:
                    return 1
        if args.state_cache_measure:
            protocol = {"workload": "one immutable 64-box shared-material frame; 128 shadow + 64 picking + 64 color draws; 64x64 linear RGBA8; 256 shadow maps; no Physics", "warmup": 120, "samples": 240, "series": 3,
                "noise_policy": "Run median spread above 10% is NOISY. Timing is descriptive; no speedup claim unless stable. Required gates are exact image/work counts and fewer driver state mutations.",
                "scope": "Submit CPU only; excludes extraction, simulation, readback and swap. GPU timings are not measured by this protocol."}
            report["state_cache_protocol"] = protocol
            (out / "state-cache-protocol.json").write_text(json.dumps(protocol, indent=2))
            records=[]
            for series in range(3):
                capture = out / f"state-cache-{series}.csv"
                measured_env=dict(env, GENGINE_STATE_CACHE_MEASURE=str(capture))
                if not invoke(f"state-cache-{series}", [executable], 180, out, measured_env, marker="[PASS] state-measure"):
                    return 1
                with capture.open(newline="") as stream:
                    rows=list(csv.DictReader(stream))
                if len(rows)!=240:
                    return 1
                cpu=sorted(int(r["cpu_ns"]) for r in rows)
                counts={k:sorted({int(r[k]) for r in rows}) for k in rows[0] if k not in ("iteration","cpu_ns")}
                if any(len(v)!=1 for v in counts.values()) or sum(counts[k][0] for k in ("DrawArrays","DrawElements"))!=256:
                    return 1
                records.append({"series":series,"cpu_median_ns":statistics.median(cpu),"cpu_p95_ns":cpu[int(.95*(len(cpu)-1))],
                    "driver_calls":counts,"image_sha256":hashlib.sha256(Path(str(capture)+".rgba").read_bytes()).hexdigest()})
            medians=[r["cpu_median_ns"] for r in records]
            report["state_cache_measurements"]={"runs":records,"median_spread":(max(medians)-min(medians))/statistics.median(medians)}
        if baseline_library:
            passed=True
            return 0
        passed = invoke("frame-submission", [executable], cwd=out,
                        marker="[PASS] pass-invalidation unchanged/revisions/targets/deferred/multiple/failure")
        if not passed:
            return 1
        if "[PASS] pass-timing delayed/unavailable/reuse/no-block/labels/move/context/retirement" not in (out / "frame-submission.log").read_text(errors="replace"):
            passed = False
            return 1
        probe_log=(out / "frame-submission.log").read_text(errors="replace")
        if "[PASS] state-cache redundant/transition/external/queried-GL" not in probe_log or "[PASS] state-cache submission-boundary/routing/pass-to-pass/image/VAO-restore" not in probe_log:
            passed=False
            return 1
        if "[PASS] draw-sort debug-source-order" not in probe_log or "[PASS] draw-sort opaque/masked-equivalence/immutable/transparent-overlap/stable-ties" not in probe_log:
            passed = False
            return 1
        if "[PASS] window-placement application/semantic-create/size/DPI/GL/fullscreen/no-reposition" not in probe_log:
            passed = False
            return 1
        if args.scene_variants:
            flags = ("activate_sphere_lattice", "activate_boxes_stacking", "activate_sphere_diamond", "activate_sphere_boxes_stacking")
            link_args = command[command.index("/link"):]
            for flag in flags:
                variant = out / flag
                variant.mkdir(parents=True, exist_ok=True)
                variant_exe = variant / "RigidBodySimulation.exe"
                variant_compile = [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++23preview", "/EHsc", "/W3",
                    "/MTd" if config == "Debug" else "/MT", "/Od" if config == "Debug" else "/O2",
                    "/DGENGINE_PLATFORM_WINDOWS", "/DGENGINE_CONFIG_" + config.upper(),
                    *["/D" + name + "=" + str(int(name == flag)) for name in flags],
                    *["/I" + str(p) for p in app_includes], "/I" + str(ROOT / "RigidBodySimulation/include"),
                    ROOT / "RigidBodySimulation/src/RigidBodySimulation.cpp", ROOT / "RigidBodySimulation/src/main.cpp",
                    "/Fo" + str(variant) + os.sep, "/Fe" + str(variant_exe), *link_args]
                if not invoke("compile-" + flag, variant_compile, 120):
                    passed = False
                    return 1
                # Variant binaries live outside bin/<config>; retain the same package.
                variant_env = dict(env)
                variant_env["GENGINE_ASSET_ROOT"] = str(ROOT / "bin" / config / "assets")
                record = application_smoke(variant_exe, variant / "run", variant_env)
                report.setdefault("variant_smokes", []).append(record)
                if record["result"] != "PASS":
                    passed = False
                    return 1
        if args.smoke:
            for app in ("RigidBodySimulation", "GEngineEditor", "Breakout", "RayTracing"):
                record = application_smoke(ROOT / "bin" / config / app / (app + ".exe"), out / "smoke" / app, env)
                report.setdefault("application_smokes", []).append(record)
                if record["result"] != "PASS":
                    passed = False
                    return 1
        if args.timing_baseline:
            reference = {"schema": 1, "phase": "48", "protocol": "phase48-per-pass-v1",
                "common_reference": "rendering-checkpoints/phase-13-baseline.json",
                "configuration": "Release --render-baseline; C++23; /MT; opt-in pass timing",
                "toolchain": report["toolchain"], "repeats": 3, "warmup_frames": 120, "samples_per_run": 240,
                "settings": {"window": [1280, 720], "vsync": 0, "shadow_resolution": 4096,
                             "workload": "default startup scene, fixed camera/layout, zero update delta, no injected input"},
                "noise_policy": "Run median spread >10% is NOISY; exact submitted counts are the fallback; no speedup claim.",
                "coverage": "GPU spans cover submitted scene passes and resolve. UI may switch contexts and presentation is CPU-only. Legacy scene item counts and external UI counts are unknown. Query results missing at shutdown remain abandoned, never zero. Cached/unrequested passes are absent, never timed as zero.",
                "applications": {}}
            for app in (*APPS, "SubmissionFixture"):
                runs = []
                fixture = app == "SubmissionFixture"
                app_exe = executable if fixture else ROOT / "bin" / config / app / (app + ".exe")
                for repeat in range(3):
                    directory = out / "baseline" / app / str(repeat)
                    directory.mkdir(parents=True, exist_ok=True)
                    if (directory / "frames.csv").exists():
                        raise ValueError("Preserve existing capture; choose a new output directory")
                    (directory / "imgui.ini").write_text(LAYOUT)
                    child_env = dict(env, GENGINE_BASELINE_OUTPUT=str(directory), GENGINE_PASS_TIMING_OUTPUT=str(directory),
                                     GENGINE_SHADOW_RESOLUTION="4096")
                    if fixture:
                        child_env.pop("GENGINE_BASELINE_OUTPUT")
                        child_env.pop("GENGINE_PASS_TIMING_OUTPUT")
                        child_env["GENGINE_SHADOW_RESOLUTION"] = "256"
                        child_env["GENGINE_TIMING_FIXTURE"] = str(directory / "passes-fixture.csv")
                    inputs = {"binary_sha256": hashlib.sha256(app_exe.read_bytes()).hexdigest(),
                              "layout_sha256": hashlib.sha256(LAYOUT.encode()).hexdigest(),
                              "runtime_dlls": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in app_exe.parent.glob("*.dll")}}
                    if not invoke(f"baseline-{app}-{repeat}", [app_exe], 300, directory, child_env):
                        passed = False
                        return 1
                    if fixture:
                        fixture_log = (out / f"baseline-{app}-{repeat}.log").read_text(errors="replace")
                        if "[PASS] timing-fixture" not in fixture_log:
                            raise ValueError("Missing active-pass fixture")
                        match = re.search(r"Image comparison: renderer=(.*?) vendor=(.*?) version=(.*?);", fixture_log)
                        if not match:
                            raise ValueError("Missing fixture hardware identity")
                        metadata = {"GL_RENDERER": match[1], "GL_VENDOR": match[2], "GL_VERSION": match[3],
                                    "color": "64x64 RGBA8 linear; single sample", "shadows": "256x256; five cascade layers; six point faces",
                                    "camera": "eye=(0,2,8), target=(0,0,0), FOV=45, aspect=1, near=.1, far=20",
                                    "workload": "one box, directional and point lights; frozen; explicit invalidation each iteration; no simulation; four drain frames"}
                    else:
                        frames, metadata = samples(directory)
                        metadata["targets"] = (directory / "targets.txt").read_text()
                        # This old field describes the unchanged Phase 13 harness,
                        # not the separate per-pass stream established here.
                        metadata.pop("GPU_time", None)
                    paths = list(directory.glob("passes-*.csv"))
                    if len(paths) != 1:
                        raise ValueError("Matched single-window baseline expected one timing stream")
                    with paths[0].open(newline="") as file:
                        all_passes = list(csv.DictReader(file))
                    rows = [r for r in all_passes if 121 <= int(r["frame"]) <= 360]
                    if not rows or any(r["completed"] != "1" for r in rows):
                        raise ValueError("Missing or failed baseline pass")
                    if any(r["gpu_status"] == "available" and (not r["gpu_ns"] or int(r["collected_frame"])-int(r["frame"]) < 2) for r in rows):
                        raise ValueError("Invalid GPU result association")
                    if any(r["gpu_status"] in ("pending", "unsupported", "pool-exhausted") for r in rows):
                        raise ValueError("Maintained GPU baseline did not provide usable query coverage")
                    evidence = [paths[0], out / f"baseline-{app}-{repeat}.log"] if fixture else [paths[0], directory / "frames.csv", directory / "runtime.txt", directory / "targets.txt", directory / "diagnostic.bmp"]
                    inputs["evidence"] = {str(p.relative_to(ROOT)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest() for p in evidence}
                    runs.append((rows, metadata, inputs))
                hardware = [{k: v for k, v in meta.items() if k.startswith("GL")} for _, meta, _ in runs]
                if any(h != hardware[0] for h in hardware):
                    raise ValueError("GPU/driver changed between repeats")
                fractions = None
                if not fixture:
                    image_name = "diagnostic.bmp"
                    images = [image_region(out / "baseline" / app / str(i) / image_name) for i in range(3)]
                    fractions = [sum(a != b for a, b in zip(images[0], img))/len(img) for img in images]
                    if max(fractions) > .005:
                        raise ValueError("Frozen diagnostic scene differs by more than 0.5%")
                labels = sorted({r["pass"] for rows, _, _ in runs for r in rows})
                metrics = {}
                for label in labels:
                    groups = [[r for r in rows if r["pass"] == label] for rows, _, _ in runs]
                    if any(len(g) != 240 or len({r["frame"] for r in g}) != 240 for g in groups):
                        raise ValueError("Incomplete per-pass CPU sample sequence: " + app + "/" + label)
                    signatures = {(r["items"], r["draws"], r["items_known"], r["draws_known"]) for g in groups for r in g}
                    if len(signatures) != 1:
                        raise ValueError("Submitted workload changed across samples: " + app + "/" + label)
                    signature = next(iter(signatures))
                    metric = {"submitted_items": int(signature[0]) if signature[2] == "1" else None,
                              "submitted_draws": int(signature[1]) if signature[3] == "1" else None,
                              "gpu_status_counts": {s: sum(r["gpu_status"] == s for g in groups for r in g)
                                                    for s in sorted({r["gpu_status"] for g in groups for r in g})}}
                    for key in ("cpu_ns", "gpu_ns"):
                        values = [[int(r[key]) for r in g if r[key]] for g in groups]
                        if not any(values):
                            metric[key] = None
                            continue
                        if any(len(v) < 230 for v in values):
                            raise ValueError("Insufficient usable GPU timing coverage")
                        medians = [statistics.median(v) for v in values]
                        center = statistics.median(medians)
                        spread = (max(medians)-min(medians))/center if center else 0
                        metric[key] = {**quantiles([v for values_run in values for v in values_run]),
                                       "run_medians": medians, "run_median_spread": spread,
                                       "quality": "NOISY" if spread > .10 else "STABLE"}
                    metrics[label] = metric
                reference["applications"][app] = {"passes": metrics, "runtime": runs[0][1], "hardware": hardware[0],
                    "changed_pixel_fractions": fractions, "inputs": [inputs for _, _, inputs in runs]}
            (out / "phase-48-baseline.json").write_text(json.dumps(reference, indent=2) + "\n")
            report["timing_baseline"] = str(out / "phase-48-baseline.json")
        return 0 if passed else 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        passed = False
        report["reason"] = str(error)
        return 1
    finally:
        report.update(result="PASS" if passed else "FAIL", exit=0 if passed else 1)
        (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"[{report['result']}] results: {out / 'results.json'}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
