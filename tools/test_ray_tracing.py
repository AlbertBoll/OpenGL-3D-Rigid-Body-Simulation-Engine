"""Build the ray tracer and validate deterministic CPU rendering, GL upload and storage lifetime."""
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
    parser.add_argument("--asan", action="store_true", help="Build an isolated instrumented GEngine library and probe")
    parser.add_argument("--smoke", action="store_true", help="Also require the real RayTracing application startup/close check")
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / "logs/rendering/phase12" / (config + ("-asan" if args.asan else ""))).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {"configuration": config, "asan": args.asan, "steps": []}
    env = {k: v for k, v in os.environ.items() if k.lower() != "path"}
    env["Path"] = os.environ.get("PATH", os.environ.get("Path", ""))
    for key in ("SDL_VIDEODRIVER", "ASAN_OPTIONS", "ASAN_SAVE_DUMPS", "ASAN_VCASAN_DEBUGGING"):
        env.pop(key, None)

    def invoke(name, command, timeout=60, cwd=ROOT, child_env=None, marker=None):
        command = [str(value) for value in command]
        log = out / (name + ".log")
        step = {"name": name, "command": command, "cwd": str(cwd), "log": str(log), "required_output": marker}
        report["steps"].append(step)
        try:
            result = subprocess.run(command, cwd=cwd, env=child_env or env, capture_output=True,
                                    timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            log.write_bytes(result.stdout + result.stderr)
            step["exit"] = result.returncode
            step["result"] = "PASS" if result.returncode == 0 else "FAIL"
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
        if not args.asan and not invoke("generate", [ROOT / "vendor/bin/premake/premake5.exe", "vs2022"], 120):
            return 1
        build = [msbuild, ROOT / "GEngine.sln", "/t:RayTracing",
                 "/m:1", "/nr:false", "/nologo", "/v:normal",
                 "/p:Configuration=" + config, "/p:Platform=x64", "/p:VCToolsVersion=" + vc.name,
                 "/p:WindowsTargetPlatformVersion=" + sdk_version, "/bl:" + str(out / "build.binlog")]
        if not args.asan and not invoke("build", build, 1200):
            return 1
        engine_library = ROOT / "bin" / config / "GEngine/GEngine.lib"
        if args.asan:
            asan_library = out / "GEngine"
            asan_library.mkdir(exist_ok=True)
            # Target-local ASan compatibility settings, matching Phase 04. Keep
            # generated projects, normal outputs and container annotations intact.
            overrides = out / "asan.targets"
            overrides.write_text('''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemDefinitionGroup>
    <ClCompile>
      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
      <BasicRuntimeChecks>Default</BasicRuntimeChecks>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
''', encoding="utf-8")
            build = [msbuild, ROOT / "GEngine/GEngine.vcxproj", "/t:Build", "/m:1", "/nr:false", "/nologo", "/v:normal",
                     "/p:Configuration=" + config, "/p:Platform=x64", "/p:VCToolsVersion=" + vc.name,
                     "/p:WindowsTargetPlatformVersion=" + sdk_version, "/p:EnableASAN=true",
                     "/p:OutDir=" + str(asan_library) + os.sep, "/p:IntDir=" + str(out / "GEngine-objects") + os.sep,
                     "/p:ForceImportAfterCppTargets=" + str(overrides),
                     "/p:BuildProjectReferences=false", "/bl:" + str(out / "asan-build.binlog")]
            if not invoke("asan-build", build, 1200):
                return 1
            engine_library = asan_library / "GEngine.lib"
        includes = [vc / "include", *(sdk / "Include" / sdk_version / part for part in ("ucrt", "shared", "um")),
                    ROOT / "GEngine/include", ROOT / "GEngine/include/GEngine", ROOT / "GEngine/include/external",
                    *(ROOT / "external" / part / "include" for part in ("sdl2", "spdlog", "glad", "assimp", "entt", "tbb"))]
        libraries = [vc / "lib/x64", sdk / "Lib" / sdk_version / "ucrt/x64",
                     sdk / "Lib" / sdk_version / "um/x64",
                     *(ROOT / "external" / part / "lib" for part in ("sdl2", "tbb", "assimp", "fmod"))]
        runtime = vc / "bin/Hostx64/x64"
        if args.asan and not (runtime / "clang_rt.asan_dynamic-x86_64.dll").is_file():
            raise FileNotFoundError("MSVC AddressSanitizer runtime is unavailable")
        sources = [ROOT / "tools/ray_tracing_probe.cpp"]
        report["instrumented_targets"] = ["GEngine", "ray-tracing-probe"] if args.asan else []
        executable = out / "ray-tracing-probe.exe"
        command = [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++20", "/EHsc", "/W3",
                   "/MTd" if config == "Debug" else "/MT", "/Od" if config == "Debug" else "/O2",
                   *(["/fsanitize=address", "/Zi", "/Fd" + str(out / "compiler.pdb")] if args.asan else []),
                   "/DSDL_MAIN_HANDLED", "/DGENGINE_PLATFORM_WINDOWS", "/DGENGINE_CONFIG_" + config.upper(),
                   *["/I" + str(p) for p in includes], "/external:W0", "/external:templates-",
                   *["/external:I" + str(p) for p in (ROOT / "external", ROOT / "GEngine/include/external", vc / "include")],
                   *sources,
                   "/Fo" + str(out) + os.sep,
                   "/Fe" + str(executable), "/link", "/SUBSYSTEM:CONSOLE",
                   *(["/DEBUG", "/INCREMENTAL:NO", "/PDB:" + str(out / "ray-tracing-probe.pdb")] if args.asan else []),
                   *(["/OPT:NOREF", "/OPT:NOICF"] if config == "Debug" else []),
                   *["/LIBPATH:" + str(p) for p in libraries], engine_library,
                   ROOT / "external/glad/bin" / config / "glad/glad.lib", "SDL2.lib", "SDL2_ttf.lib",
                   "tbb12.lib", "tbb12_debug.lib", "tbb.lib", "tbb_debug.lib", "assimp.lib",
                   "fmod64_vc.lib", "fmodL64_vc.lib", "fmodstudio64_vc.lib", "fmodstudioL64_vc.lib"]
        if not invoke("compile-probe", command, 120):
            return 1
        sdl = ROOT / "bin" / config / "GEngineEditor/SDL2.dll"
        report["inputs"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                            (executable, engine_library, sdl)}
        env["Path"] = str(runtime) + os.pathsep + str(sdl.parent) + os.pathsep + env["Path"]
        passed = invoke("ray-tracing", [executable], cwd=out,
                        marker="[PASS] ray-tracing cycles=2")
        if args.asan and passed:
            probe_log = (out / "ray-tracing.log").read_text(errors="replace")
            passed = "[SANITIZER] AddressSanitizer=on" in probe_log
            command = [str(executable), "--asan-failure-probe"]
            result = subprocess.run(command, cwd=out, env=env, capture_output=True, timeout=60,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            data = result.stdout + result.stderr
            (out / "asan-negative.log").write_bytes(data)
            negative = result.returncode != 0 and b"ERROR: AddressSanitizer: heap-buffer-overflow" in data
            report["steps"].append({"name": "asan-negative", "command": command, "exit": result.returncode,
                                    "result": "PASS" if negative else "FAIL", "log": str(out / "asan-negative.log")})
            passed &= negative
        if args.smoke and passed:
            import test_shadow_configuration as smoke
            smoke.OUT = out / "app-smoke"
            smoke.OUT.mkdir(exist_ok=True)
            passed = smoke.smoke(config, only="RayTracing:default")
            report["application_smoke"] = {"result": "PASS" if passed else "FAIL", "evidence": str(smoke.OUT)}
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
