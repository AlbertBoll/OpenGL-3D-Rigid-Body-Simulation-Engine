"""Validate serial/parallel extraction, deterministic frames and retained versions; measure optional adoption."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import statistics
import winreg

from rendering_validation import ROOT, toolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=["Debug", "Release"], required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--no-build", action="store_true", help="Reuse matching affected-consumer builds")
    parser.add_argument("--benchmark", action="store_true", help="Three matched Release series, serial/1/2/8 workers")
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / "logs/rendering/phase45/final" / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {"configuration": config, "steps": []}
    if args.benchmark and config != "Release":
        parser.error("Adoption measurements use Release")
    # Declared before running any timed comparison; no post-hoc workload/tolerance tuning.
    report["measurement_policy"] = {"scenes": [32, 1024, 8192], "workers": [0, 1, 2, 8],
        "series": 3, "warmup_per_mode_series": 4, "samples_per_mode_series": 21,
        "noise_percent": 5, "required_extraction_gain_percent": 10, "max_frame_regression_percent": 5,
        "frame_cpu_scope": "complete extraction call plus RenderVisibility::Build; no Physics, GL submission or GPU",
        "ordering": "rotate modes for each sample; identical frozen scene and camera",
        "fallback": "serial default unless a parallel mode meets the gate in all three medium/large series"}
    (out / "measurement-policy.json").write_text(json.dumps(report["measurement_policy"], indent=2) + "\n")
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
                    *(ROOT / "external" / part / "include" for part in ("sdl2", "spdlog", "glad", "assimp", "entt", "tbb"))]
        libraries = [vc / "lib/x64", sdk / "Lib" / sdk_version / "ucrt/x64",
                     sdk / "Lib" / sdk_version / "um/x64",
                     *(ROOT / "external" / part / "lib" for part in ("sdl2", "tbb", "assimp", "fmod"))]
        consumer = ROOT / "tools/render_extraction_probe.cpp"
        native_free = [path for path in includes if not any(part in path.parts for part in ("sdl2", "glad", "assimp", "tbb"))]
        if not invoke("frame-consumer-boundary", [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++23preview",
                      "/EHsc", "/W3", "/MTd" if config == "Debug" else "/MT", "/c", "/showIncludes",
                      "/DEXTRACTION_SCHEMA_ONLY", "/D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING",
                      *["/I" + str(p) for p in native_free], consumer, "/Fo" + str(out / "frame-consumer.obj")], 120):
            return 1
        dependencies = (out / "frame-consumer-boundary.log").read_text(errors="replace").lower().replace("\\", "/")
        if any(token in dependencies for token in ("/glad/", "/sdl2/", "/opengl/", "/imgui/", "/assimp/", "/gepch.h", "backend.h")):
            report["reason"] = "Concrete backend declaration leaked into the extraction consumer"
            return 1
        report["consumer_boundary"] = "PASS"
        for rel in ("GEngine/include/GEngine/Renderer/RenderExtraction.h", "GEngine/src/Renderer/RenderExtraction.cpp",
                    "GEngine/include/GEngine/Renderer/RenderTasks.h", "GEngine/src/Renderer/RenderTasks.cpp",
                    "GEngine/include/GEngine/Renderer/RenderFrame.h", "GEngine/src/Renderer/RenderFrame.cpp",
                    "GEngine/include/GEngine/Scene/RenderState.h", "GEngine/src/Scene/RenderState.cpp"):
            source = re.sub(r"//[^\n]*|/\*.*?\*/", "", (ROOT / rel).read_text(), flags=re.S)
            if re.search(r"\b(throw|try|catch|enable_if|exception_ptr)\b", source):
                report["reason"] = "New exception/SFINAE boundary: " + rel
                return 1
        report["architecture"] = {"no_new_exception": "PASS", "consumer_boundary": "PASS"}

        executable = out / "render-extraction-probe.exe"
        command = [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++23preview", "/EHsc", "/W3",
                   "/MTd" if config == "Debug" else "/MT", "/Od" if config == "Debug" else "/O2",
                   "/DSDL_MAIN_HANDLED", "/DGENGINE_PLATFORM_WINDOWS", "/DGENGINE_CONFIG_" + config.upper(),
                   *["/I" + str(p) for p in includes], "/external:W0", "/external:templates-",
                   *["/external:I" + str(p) for p in (ROOT / "external", ROOT / "GEngine/include/external", vc / "include")],
                   ROOT / "tools/render_extraction_probe.cpp",
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
        passed = invoke("render-extraction", [executable, *(["--benchmark"] if args.benchmark else [])], timeout=600, cwd=out,
                        marker="[PASS] render-extraction ")
        if passed and args.benchmark:
            samples = []
            for line in (out / "render-extraction.log").read_text().splitlines():
                if line.startswith("[SAMPLE] "):
                    samples.append({key: float(value) if "." in value else int(value)
                                    for key, value in (field.split("=") for field in line.split()[1:])})
            if len(samples) != 3 * 3 * 21 * 4:
                report["reason"] = "Incomplete matched measurements"
                passed = False
                return 1
            summary = []
            for size in (32, 1024, 8192):
                for series in range(3):
                    for workers in (0, 1, 2, 8):
                        group = [s for s in samples if (s["scene"], s["series"], s["workers"]) == (size, series, workers)]
                        row = {"scene": size, "series": series, "workers": workers}
                        for metric in ("preparation_us", "extraction_us", "call_us", "frame_cpu_us", "extraction_allocations", "frame_allocations", "requested_bytes"):
                            values = sorted(s[metric] for s in group)
                            row[metric] = {"median": statistics.median(values), "p95": values[19]}
                        summary.append(row)
            qualifies = []
            for workers in (2, 8):
                good = True
                for size in (1024, 8192):
                    for series in range(3):
                        serial = next(r for r in summary if (r["scene"], r["series"], r["workers"]) == (size, series, 0))
                        candidate = next(r for r in summary if (r["scene"], r["series"], r["workers"]) == (size, series, workers))
                        good &= candidate["extraction_us"]["median"] <= serial["extraction_us"]["median"] * .90
                        good &= all(candidate["frame_cpu_us"][stat] <= serial["frame_cpu_us"][stat] * 1.05 for stat in ("median", "p95"))
                if good:
                    qualifies.append(workers)
            benchmark = {"policy": report["measurement_policy"], "samples": samples, "summary": summary,
                         "qualifying_parallel_modes": qualifies,
                         "decision": "Review qualifying mode" if qualifies else "Keep serial default; optional bounded path only"}
            (out / "benchmark.json").write_text(json.dumps(benchmark, indent=2) + "\n")
            report["benchmark"] = {"result": "PASS", "samples": len(samples), "decision": benchmark["decision"]}
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
