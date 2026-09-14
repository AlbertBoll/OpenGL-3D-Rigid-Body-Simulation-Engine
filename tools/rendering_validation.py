"""Build/run the small Windows rendering validation target with the existing toolchain."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def toolchain():
    installer = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    result = subprocess.run([str(installer), "-latest", "-version", "[17.0,18.0)", "-products", "*", "-requires",
                             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                            capture_output=True, text=True, check=True)
    if not result.stdout.strip():
        raise FileNotFoundError("Visual Studio 2022 C++ tools are not installed")
    vs = Path(result.stdout.strip())
    version = (vs / "VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt").read_text().strip()
    return vs / "MSBuild/Current/Bin/MSBuild.exe", vs / "VC/Tools/MSVC" / version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Debug")
    parser.add_argument("--gl", action="store_true", help="Also require the optional hidden OpenGL 4.6 fixture")
    parser.add_argument("--asan", action="store_true", help="Instrument this target and verify ASan detects an isolated heap overflow")
    parser.add_argument("--no-build", action="store_true", help="Use an already built matching binary")
    parser.add_argument("--output", type=Path, help="Directory for commands, logs and results.json")
    args = parser.parse_args()
    label = args.configuration + ("-asan" if args.asan else "")
    out = (args.output or ROOT / "logs/rendering/validation" / label).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {"configuration": args.configuration, "asan": args.asan, "gl_requested": args.gl, "steps": []}
    env = {k: v for k, v in os.environ.items() if k.lower() != "path"}
    env["Path"] = os.environ.get("PATH", os.environ.get("Path", ""))
    # An owner setting must not open a debugger, save dumps, or mask a negative control.
    for key in ("ASAN_OPTIONS", "ASAN_SAVE_DUMPS", "ASAN_VCASAN_DEBUGGING", "SDL_VIDEODRIVER"):
        env.pop(key, None)

    def invoke(name, command, expected=0, marker=None, child_env=None, timeout=60):
        log = out / (name + ".log")
        step = {"name": name, "command": [str(x) for x in command], "cwd": str(ROOT),
                "expected_exit": expected, "required_output": marker, "log": str(log)}
        report["steps"].append(step)
        try:
            result = subprocess.run(step["command"], cwd=ROOT, env=child_env or env, capture_output=True,
                                    timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            data = result.stdout + result.stderr
            log.write_bytes(data)
            step["exit"] = result.returncode
            exit_ok = result.returncode != 0 if expected == "nonzero" else result.returncode == expected
            step["result"] = "PASS" if exit_ok and (marker is None or marker in data.decode(errors="replace")) else "FAIL"
        except subprocess.TimeoutExpired as error:
            log.write_bytes((error.stdout or b"") + (error.stderr or b""))
            step.update(result="FAIL", reason="process timed out and was terminated", exit=None)
        print(f"[{step['result']}] {name}: exit={step['exit']}", flush=True)
        return step

    code = 1
    try:
        if os.name != "nt":
            report["reason"] = "This entry point supports the recorded Windows/MSVC toolchain; no toolchain migration is attempted"
            code = 3
            return code
        msbuild, vc = toolchain()
        report["toolchain"] = {"msbuild": str(msbuild), "msvc": str(vc)}
        runtime = vc / "bin/Hostx64/x64"
        if args.asan and not (runtime / "clang_rt.asan_dynamic-x86_64.dll").is_file():
            report["reason"] = "MSVC AddressSanitizer component is not installed"
            code = 3
            return code
        env["Path"] = str(runtime) + os.pathsep + env["Path"]
        binary_dir = ROOT / "bin" / label / "RenderingValidation"
        executable = binary_dir / "RenderingValidation.exe"
        if not args.no_build:
            if invoke("generate", [ROOT / "vendor/bin/premake/premake5.exe", "vs2022"], timeout=120)["result"] != "PASS":
                return code
            command = [msbuild, ROOT / "RenderingValidation/RenderingValidation.vcxproj", "/t:Build", "/m:1", "/nr:false",
                       "/nologo", "/v:normal", "/p:Configuration=" + args.configuration, "/p:Platform=x64",
                       "/p:VCToolsVersion=" + vc.name,
                       "/p:EnableASAN=" + ("true" if args.asan else "false"), "/bl:" + str(out / "build.binlog")]
            if args.asan:
                command += ["/p:OutDir=" + str(binary_dir) + os.sep,
                            "/p:IntDir=" + str(ROOT / "bin-int" / label / "RenderingValidation") + os.sep]
            if invoke("build", command, timeout=600)["result"] != "PASS":
                return code
        report["binary"] = {"path": str(executable), "sha256": hashlib.sha256(executable.read_bytes()).hexdigest()}
        # Pure CPU operation must not initialize SDL video, even on a headless host.
        cpu_env = dict(env, SDL_VIDEODRIVER="rendering-validation-invalid-driver")
        cpu_marker = "AddressSanitizer=" + ("on" if args.asan else "off")
        invoke("cpu-self-test", [executable, "--self-test"], marker=cpu_marker, child_env=cpu_env)
        invoke("failure-probe", [executable, "--failure-probe"], expected=1, marker="[FAIL] failure-probe", child_env=cpu_env)
        invoke("exception-probe", [executable, "--exception-probe"], expected=1, marker="[FAIL] exception-probe", child_env=cpu_env)
        invoke("usage-probe", [executable, "--unknown-mode"], expected=2, marker="[USAGE]", child_env=cpu_env)
        if args.asan:
            invoke("asan-failure-probe", [executable, "--asan-failure-probe"], expected="nonzero",
                   marker="ERROR: AddressSanitizer: heap-buffer-overflow", child_env=cpu_env)
        if args.gl:
            # Reuse the existing tracked SDL runtime; no application is launched.
            sdl = ROOT / "bin" / args.configuration / "GEngineEditor/SDL2.dll"
            if not sdl.is_file():
                report["reason"] = "Existing tracked SDL2 runtime missing: " + str(sdl)
                code = 3 if all(s["result"] == "PASS" for s in report["steps"]) else 1
                return code
            gl_env = dict(env, Path=str(sdl.parent) + os.pathsep + env["Path"])
            report["sdl_runtime"] = {"path": str(sdl), "sha256": hashlib.sha256(sdl.read_bytes()).hexdigest()}
            step = invoke("hidden-gl-context", [executable, "--gl"], marker="[PASS] hidden-gl-context", child_env=gl_env)
            if step["exit"] == 3 and all(s["result"] == "PASS" for s in report["steps"][:-1]):
                report["reason"] = "Optional GL prerequisite unavailable; see hidden-gl-context.log"
                code = 3
                return code
        code = 0 if all(step["result"] == "PASS" for step in report["steps"]) else 1
        return code
    except (OSError, subprocess.SubprocessError) as error:
        report["reason"] = str(error)
        return code
    finally:
        report["result"] = {0: "PASS", 1: "FAIL", 3: "UNAVAILABLE"}[code]
        report["exit"] = code
        (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"[{report['result']}] results: {out / 'results.json'}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
