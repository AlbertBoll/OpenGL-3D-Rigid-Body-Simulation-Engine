"""Build affected consumers and validate deterministic shutdown on Windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import winreg

from rendering_validation import ROOT, toolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=["Debug", "Release"], required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--smoke", action="store_true", help="Smoke the four already-built applications without rebuilding")
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / "logs/rendering/phase18" / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if args.smoke:
        import test_shadow_configuration as smoke
        smoke.OUT = out
        outcomes = [smoke.smoke(config, app + ":default") for app in
                    ("GEngineEditor", "RigidBodySimulation", "Breakout", "RayTracing")]
        passed = all(outcomes)
        (out / "results.json").write_text(json.dumps({"configuration": config,
            "result": "PASS" if passed else "FAIL", "exit": 0 if passed else 1,
            "evidence": "Per-application smoke JSON contains native exits, commands and observations"}, indent=2) + "\n")
        return 0 if passed else 1
    report = {"configuration": config, "steps": []}
    env = {k: v for k, v in os.environ.items() if k.lower() != "path"}
    env["Path"] = os.environ.get("PATH", os.environ.get("Path", ""))
    env.pop("SDL_VIDEODRIVER", None)
    env["CL"] = env.get("CL", "") + " /we4715 /we4716"
    env["GENGINE_ASSET_ROOT"] = str(ROOT / "bin" / config / "assets")
    env["GENGINE_SHADOW_RESOLUTION"] = "32"

    def invoke(name, command, timeout=60, cwd=ROOT, child_env=None, marker=None, expected_exit=0):
        command = [str(value) for value in command]
        log = out / (name + ".log")
        step = {"name": name, "command": command, "cwd": str(cwd), "log": str(log), "required_output": marker,
                "timeout_seconds": timeout, "expected_exit": expected_exit}
        report["steps"].append(step)
        try:
            result = subprocess.run(command, cwd=cwd, env=child_env or env, capture_output=True,
                                    timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            log.write_bytes(result.stdout + result.stderr)
            step["exit"] = result.returncode
            step["result"] = "PASS" if result.returncode == expected_exit else "FAIL"
            if marker and marker.encode() not in result.stdout + result.stderr:
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
        if not invoke("generate", [ROOT / "vendor/bin/premake/premake5.exe", "vs2022"], 120):
            return 1
        report["compiler_return_guards"] = env["CL"]
        build = [msbuild, ROOT / "GEngine.sln", "/t:GEngineEditor;Breakout;RayTracing;RigidBodySimulation;PhysicsTests;PhysicsBenchmark",
                 "/m:1", "/nr:false", "/nologo", "/v:normal",
                 "/p:Configuration=" + config, "/p:Platform=x64", "/p:VCToolsVersion=" + vc.name,
                 "/p:WindowsTargetPlatformVersion=" + sdk_version, "/bl:" + str(out / "build.binlog")]
        if not invoke("build", build, 1200):
            return 1
        includes = [vc / "include", *(sdk / "Include" / sdk_version / part for part in ("ucrt", "shared", "um")),
                    ROOT / "GEngine/include", ROOT / "GEngine/include/GEngine", ROOT / "GEngine/include/external",
                    *(ROOT / "external" / part / "include" for part in ("sdl2", "spdlog", "glad", "assimp", "entt", "tbb"))]
        libraries = [vc / "lib/x64", sdk / "Lib" / sdk_version / "ucrt/x64",
                     sdk / "Lib" / sdk_version / "um/x64",
                     *(ROOT / "external" / part / "lib" for part in ("sdl2", "tbb", "assimp", "fmod"))]
        executable = out / "shutdown-probe.exe"
        command = [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++20", "/EHsc", "/W3",
                   "/MTd" if config == "Debug" else "/MT", "/Od" if config == "Debug" else "/O2",
                   "/DSDL_MAIN_HANDLED", "/DGENGINE_PLATFORM_WINDOWS", "/DGENGINE_CONFIG_" + config.upper(),
                   *["/I" + str(p) for p in includes], "/external:W0", "/external:templates-",
                   *["/external:I" + str(p) for p in (ROOT / "external", ROOT / "GEngine/include/external", vc / "include")],
                   ROOT / "tools/shutdown_probe.cpp",
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
        outcomes = []
        for mode in ("--lifetimes", "--minimized", "--application-failure", "--imgui-context-failure",
                     "--imgui-platform-failure", "--sdl-failure", "--window-failure", "--empty-windows", "--window-owners", "--resource-moves",
                     "--cache-ownership", "--manager-failures", "--context-thread"):
            runtime = out / mode[2:]
            runtime.mkdir(exist_ok=True)
            child_env = dict(env)
            if mode == "--sdl-failure":
                child_env["SDL_VIDEODRIVER"] = "phase18-unavailable-driver"
            elif mode == "--window-failure":
                child_env["SDL_VIDEODRIVER"] = "dummy"
            timeout = 180 if mode in ("--lifetimes", "--minimized", "--application-failure", "--manager-failures") else 60
            outcomes.append(invoke(mode[2:], [executable, mode], timeout=timeout, cwd=runtime, child_env=child_env,
                                   marker="[PASS] shutdown " + mode))
        rejected = ["context-create", "context-switch", "context-delete", "window-teardown", "root-teardown", "imgui"]
        if config == "Debug":
            rejected += ["create", "upload", "delete", "submit", "readback", "detached"]
        for case in rejected:
            mode = "--reject-" + case
            runtime = out / mode[2:]
            runtime.mkdir(exist_ok=True)
            # EngineContext already terminates on wrong-thread destruction.
            marker = None if case == "root-teardown" else "[GLThread] assertion failed:"
            outcomes.append(invoke(mode[2:], [executable, mode], cwd=runtime,
                                   marker=marker, expected_exit=86))
        passed = all(outcomes)
        return 0 if passed else 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report["reason"] = str(error)
        return 1
    finally:
        report.update(result="PASS" if passed else "FAIL", exit=0 if passed else 1)
        (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"[{report['result']}] results: {out / 'results.json'}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
