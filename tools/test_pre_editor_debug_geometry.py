"""Bounded Phase18 probe and existing RBS smoke; no generation or rebuild."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import winreg

from rendering_validation import toolchain
from test_pre_editor_geometry_templates import application_smoke

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--configuration", choices=("Debug", "Release"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compile-only", action="store_true")
    parser.add_argument("--no-measure", action="store_true", help="Reuse unaffected recorded observer measurement")
    parser.add_argument("--skip-smoke", action="store_true", help="RBS integration will be recorded separately")
    args = parser.parse_args()
    config = args.configuration
    out = args.output.resolve()
    out.relative_to(ROOT / "logs/pre_editor/phase-18")
    out.mkdir(parents=True, exist_ok=False)
    report = {"configuration": config, "steps": [], "result": "INCOMPLETE"}
    def identity(path):
        return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    def invoke(name, command, cwd, env, timeout):
        step = {"name": name, "command": list(map(str, command)), "cwd": str(cwd), "timeout": timeout,
                "started": time.time(), "result": "INCOMPLETE"}
        report["steps"].append(step)
        try:
            run = subprocess.run(step["command"], cwd=cwd, env=env, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            (out / (name + ".log")).write_bytes(run.stdout)
            step.update(exit=run.returncode, result="PASS" if run.returncode == 0 else "FAIL")
        except subprocess.TimeoutExpired as error:
            (out / (name + ".log")).write_bytes(error.stdout or b"")
            step.update(result="INCOMPLETE", reason="Owned process exceeded frozen deadline")
        print(step["result"], name, flush=True)
        return step["result"] == "PASS"
    try:
        _, vc = toolchain()
        assert vc.name == "14.44.35207"
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots") as key:
            sdk = Path(winreg.QueryValueEx(key, "KitsRoot10")[0])
        version = "10.0.26100.0"
        includes = [vc / "include", *(sdk / "Include" / version / p for p in ("ucrt", "shared", "um")),
                    ROOT / "GEngine/include", ROOT / "GEngine/include/GEngine", ROOT / "GEngine/include/external",
                    *(ROOT / "external" / p / "include" for p in ("sdl2", "spdlog", "glad", "assimp", "entt", "tbb", "fmod"))]
        libraries = [vc / "lib/x64", sdk / "Lib" / version / "ucrt/x64", sdk / "Lib" / version / "um/x64",
                     *(ROOT / "external" / p / "lib" for p in ("sdl2", "tbb", "assimp", "fmod"))]
        env = dict(os.environ)
        for key in list(env):
            if key.upper().startswith("GENGINE_") or key.upper() in ("CL", "_CL_", "SDL_VIDEODRIVER", "PATH"):
                env.pop(key)
        appdir = ROOT / "bin" / config / "RigidBodySimulation"
        env["Path"] = os.pathsep.join(map(str, [vc / "bin/Hostx64/x64", appdir, ROOT / "bin" / config])) + os.pathsep + os.environ.get("PATH", "")
        env["TMP"] = env["TEMP"] = str(ROOT / "runtime/tmp")
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        env["GENGINE_ASSET_ROOT"] = str(ROOT / "bin" / config / "assets")
        library = ROOT / "bin" / config / "GEngine/GEngine.lib"
        executable = out / "phase18-probe.exe"
        source = ROOT / "tools/pre_editor_debug_geometry_probe.cpp"
        report["inputs"] = [identity(p) for p in (source, Path(__file__), library)]
        report["toolchain"] = {"vc": str(vc), "sdk": version, "crt": "MTd" if config == "Debug" else "MT"}
        command = [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++23preview", "/EHsc", "/W3",
                   "/MTd" if config == "Debug" else "/MT", "/Od" if config == "Debug" else "/O2",
                   "/DSDL_MAIN_HANDLED", "/DGENGINE_PLATFORM_WINDOWS", "/DGENGINE_WINDOW_SDL",
                   "/DGENGINE_CONFIG_" + config.upper(), "/D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING",
                   *["/I" + str(p) for p in includes], source, "/Fo" + str(out) + os.sep, "/Fe" + str(executable),
                   "/link", "/SUBSYSTEM:CONSOLE", *["/LIBPATH:" + str(p) for p in libraries], library,
                   ROOT / "external/glad/bin" / config / "glad/glad.lib", "SDL2.lib", "SDL2_ttf.lib",
                   "tbb12.lib", "tbb12_debug.lib", "tbb.lib", "tbb_debug.lib", "assimp.lib",
                   "fmod64_vc.lib", "fmodL64_vc.lib", "fmodstudio64_vc.lib", "fmodstudioL64_vc.lib"]
        if not invoke("compile", command, ROOT, env, 120):
            return 1
        report["probe"] = identity(executable)
        if args.compile_only:
            report["result"] = "COMPILE_ONLY"
            return 0
        runtime = ROOT / "runtime/RigidBodySimulation" / config
        runtime.mkdir(parents=True, exist_ok=True)
        if not invoke("focused", [executable, *(["--measure"] if config == "Release" and not args.no_measure else [])], runtime, env, 90):
            return 1
        if args.skip_smoke:
            report.update(result="PASS", smoke="NOT RUN HERE; separately required interactive integration")
            return 0
        smoke = application_smoke(appdir / "RigidBodySimulation.exe", runtime, env)
        report["smoke"] = smoke
        for name in ("application.log", "GEngine.log", "smoke.json"):
            if (runtime / name).exists():
                shutil.copyfile(runtime / name, out / name)
        report["result"] = "PASS" if smoke["result"] == "PASS" else "FAIL"
        return 0 if report["result"] == "PASS" else 1
    finally:
        report["finished"] = time.time()
        (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
