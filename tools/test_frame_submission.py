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

from rendering_validation import ROOT, toolchain


def application_smoke(executable, directory, env):
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
                    record["native_close_posted"] = bool(user32.PostMessageW(ready, 0x0010, 0, 0))
                    record["exit"] = child.wait(timeout=30)
                    record["result"] = "PASS" if record["native_close_posted"] and record["exit"] == 0 else "FAIL"
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
    args = parser.parse_args()
    config = args.configuration
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
        if not args.no_build and not invoke("generate", [ROOT / "vendor/bin/premake/premake5.exe", "vs2022"], 120):
            return 1
        report["compiler_return_guards"] = env["CL"]
        build = [msbuild, ROOT / "GEngine.sln", "/t:GEngineEditor;Breakout;RayTracing;RigidBodySimulation;PhysicsTests;PhysicsBenchmark",
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
                    "GEngine/include/GEngine/Core/FrameBuffer.h", "GEngine/src/Core/FrameBuffer.cpp",
                    "GEngine/include/GEngine/Renderer/SceneRenderResources.h", "GEngine/src/Renderer/SceneRenderResources.cpp",
                    "RigidBodySimulation/src/RigidBodySimulation.cpp",
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
        report["architecture"] = {"no_new_exception": "PASS", "consumer_boundary": "PASS", "application_pipeline_ownership": "PASS"}

        executable = out / "frame-submission-probe.exe"
        command = [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++23preview", "/EHsc", "/W3",
                   "/MTd" if config == "Debug" else "/MT", "/Od" if config == "Debug" else "/O2",
                   "/DSDL_MAIN_HANDLED", "/DGENGINE_PLATFORM_WINDOWS", "/DGENGINE_CONFIG_" + config.upper(),
                   *["/I" + str(p) for p in includes], "/external:W0", "/external:templates-",
                   *["/external:I" + str(p) for p in (ROOT / "external", ROOT / "GEngine/include/external", vc / "include")],
                   ROOT / "tools/frame_submission_probe.cpp",
                   "/Fo" + str(out) + os.sep,
                   "/Fe" + str(executable), "/link", "/SUBSYSTEM:CONSOLE",
                   *(["/OPT:NOREF", "/OPT:NOICF"] if config == "Debug" else []),
                   *["/LIBPATH:" + str(p) for p in libraries], ROOT / "bin" / config / "GEngine/GEngine.lib",
                   ROOT / "external/glad/bin" / config / "glad/glad.lib", "SDL2.lib", "SDL2_ttf.lib",
                   "tbb12.lib", "tbb12_debug.lib", "tbb.lib", "tbb_debug.lib", "assimp.lib",
                   "fmod64_vc.lib", "fmodL64_vc.lib", "fmodstudio64_vc.lib", "fmodstudioL64_vc.lib"]
        if not invoke("compile-probe", command, 120):
            return 1
        sdl = ROOT / "bin" / config / "GEngineEditor/SDL2.dll"
        report["inputs"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                            (executable, ROOT / "bin" / config / "GEngine/GEngine.lib", sdl)}
        env["Path"] = str(sdl.parent) + os.pathsep + env["Path"]
        env["GENGINE_ASSET_ROOT"] = str(ROOT / "bin" / config / "assets")
        env["GENGINE_SHADOW_RESOLUTION"] = "256"
        passed = invoke("frame-submission", [executable], cwd=out,
                        marker="[PASS] pass-invalidation unchanged/revisions/targets/deferred/multiple/failure")
        if not passed:
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
