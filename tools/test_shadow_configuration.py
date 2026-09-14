"""Windows Phase 03 checks. Build affected applications before running smoke mode."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "logs/rendering/phase03"


def environment():
    env = dict(os.environ)
    env["Path"] = env.pop("PATH", env.get("Path", ""))
    env.pop("GENGINE_SHADOW_RESOLUTION", None)
    env.pop("GENGINE_ASSET_ROOT", None)
    return env


def probe(config):
    vc = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207")
    sdk = Path("C:/Program Files (x86)/Windows Kits/10")
    includes = [vc / "include", *(sdk / "Include/10.0.26100.0" / p for p in ["ucrt", "shared", "um"]),
                ROOT / "GEngine/include/GEngine", ROOT / "GEngine/include/external",
                *(ROOT / "external" / p / "include" for p in ["sdl2", "spdlog", "glad", "assimp"])]
    executable = OUT / ("shadow-probe-" + config.lower() + ".exe")
    command = [str(vc / "bin/Hostx64/x64/cl.exe"), "/nologo", "/std:c++20", "/EHsc", "/W3",
               "/MTd" if config == "Debug" else "/MT", "/Od" if config == "Debug" else "/O2",
               "/DSDL_MAIN_HANDLED", "/DGENGINE_PLATFORM_WINDOWS", "/DGENGINE_CONFIG_" + config.upper(),
               "/D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING",
               *["/I" + str(p) for p in includes], "/external:W0", "/external:templates-",
               *["/external:I" + str(p) for p in [ROOT / "external", ROOT / "GEngine/include/external", vc / "include"]],
               str(ROOT / "tools/shadow_framebuffer_probe.cpp"), "/Fo" + str(executable.with_suffix(".obj")),
               "/Fe" + str(executable), "/link", "/SUBSYSTEM:CONSOLE",
               *["/LIBPATH:" + str(p) for p in [vc / "lib/x64", sdk / "Lib/10.0.26100.0/ucrt/x64",
                                              sdk / "Lib/10.0.26100.0/um/x64", ROOT / "external/sdl2/lib"]],
               str(ROOT / "bin" / config / "GEngine/GEngine.lib"),
               str(ROOT / "external/glad/bin" / config / "glad/glad.lib"), "SDL2.lib"]
    env = environment()
    compile_result = subprocess.run(command, cwd=OUT, env=env, capture_output=True)
    (OUT / (executable.stem + "-compile.log")).write_bytes(compile_result.stdout + compile_result.stderr)
    record = dict(configuration=config, command=command, compile_exit=compile_result.returncode)
    if compile_result.returncode == 0:
        env["Path"] = str(ROOT / "bin" / config / "GEngineEditor") + os.pathsep + env["Path"]
        run = subprocess.run([str(executable)], cwd=OUT, env=env, capture_output=True, timeout=60,
                             creationflags=subprocess.CREATE_NO_WINDOW)
        (OUT / (executable.stem + "-run.log")).write_bytes(run.stdout + run.stderr)
        record["run_exit"] = run.returncode
    record["result"] = "PASS" if record.get("run_exit") == 0 else "FAIL"
    (OUT / (executable.stem + ".json")).write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record), flush=True)
    return record["result"] == "PASS"


def smoke(config, only=None, label_prefix=""):
    user = c.WinDLL("user32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.IsHungAppWindow.argtypes = [w.HWND]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]

    def windows(pid):
        found = []

        @callback_type
        def visit(hwnd, unused):
            owner = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(owner))
            if owner.value == pid and user.IsWindowVisible(hwnd):
                cls = c.create_unicode_buffer(256)
                user.GetClassNameW(hwnd, cls, 256)
                found.append(dict(hwnd=hwnd, cls=cls.value, hung=bool(user.IsHungAppWindow(hwnd))))
            return True

        user.EnumWindows(visit, 0)
        return found

    results = []
    cases = [(app, None) for app in ["GEngineEditor", "RigidBodySimulation", "Breakout", "RayTracing"]]
    cases += [(app, "4096") for app in ["GEngineEditor", "RigidBodySimulation"]]
    cases += [(app, "invalid") for app in ["GEngineEditor", "RigidBodySimulation", "Breakout", "RayTracing"]]
    cases += [("GEngineEditor", value) for value in ["0", "8193", "-1", "2048junk", "999999999999999999999"]]
    if only:
        cases = [(app, value) for app, value in cases if app + ":" + (value or "default") == only]
        if not cases:
            raise ValueError("Unknown smoke case: " + only)
    for app, override in cases:
        label = label_prefix + config.lower() + "-" + app.lower() + "-" + (override or "default")
        executable = ROOT / "bin" / config / app / (app + ".exe")
        cwd = OUT / "runtime" / label
        cwd.mkdir(parents=True, exist_ok=True)
        env = environment()
        if override is not None:
            env["GENGINE_SHADOW_RESOLUTION"] = override
        negative = override not in [None, "4096"]
        record = dict(config=config, app=app, override=override, cwd=str(cwd), command=[str(executable)],
                      binary_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
        # Keep application settings exact; only disposable fixture files may be written.
        guards = []
        before = {}
        for path in [cwd / "imgui.ini", executable.parent / "imgui.ini"]:
            if path.exists():
                before[path] = path.read_bytes()
                handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0, None)
                assert handle != c.c_void_p(-1).value
                guards.append(handle)
        process = None
        try:
            with (OUT / (label + ".log")).open("wb") as log:
                process = subprocess.Popen([str(executable)], cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                record["pid"] = process.pid
                ready = 0
                observations = []
                for second in range(90):
                    time.sleep(1)
                    current = windows(process.pid)
                    observations.append(dict(second=second + 1, windows=current))
                    responsive = any(x["cls"] == "SDL_app" and not x["hung"] for x in current)
                    responsive &= not any(x["cls"] == "#32770" for x in current)
                    ready = ready + 1 if responsive and second >= 10 else 0
                    if process.poll() is not None or (not negative and ready >= 5):
                        break
                record["responsive"] = ready >= 5
                record["observations"] = observations
                if process.poll() is None:
                    for window in windows(process.pid):
                        if window["cls"] == "SDL_app":
                            user.PostMessageW(window["hwnd"], 0x10, 0, 0)
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    record["forced_termination"] = True
                    process.kill()  # Only the fixture-owned process.
                    process.wait(timeout=10)
                record["exit"] = process.returncode
            text = (OUT / (label + ".log")).read_text(errors="replace")
            if negative:
                passed = process.returncode == 1 and "GENGINE_SHADOW_RESOLUTION must be an integer from 1 to 8192" in text
                passed &= "[Shadows] cascade" not in text and "[Shadows] point" not in text
            else:
                size = override or "4096"
                source = "GENGINE_SHADOW_RESOLUTION (explicit)" if override else "safe default"
                expected = ["resolution=" + size + "x" + size + " source=" + source,
                            "cascade " + size + "x" + size + " layers/faces=6 format=GL_DEPTH_COMPONENT32F framebuffer=complete",
                            "point " + size + "x" + size + " layers/faces=6 format=GL_DEPTH_COMPONENT (driver-selected) framebuffer=complete"]
                record["diagnostics_match"] = all(token in text for token in expected)
                passed = ready >= 5 and process.returncode == 0 and record["diagnostics_match"]
                passed &= "allocation failed" not in text and "resolution=8192" not in text
            record["result"] = "PASS" if passed and not record.get("forced_termination") else "FAIL"
        finally:
            if process and process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            for handle in guards:
                kernel.CloseHandle(handle)
            assert all(path.read_bytes() == data for path, data in before.items())
        (OUT / (label + ".json")).write_text(json.dumps(record, indent=2) + "\n")
        print(json.dumps({k: v for k, v in record.items() if k != "observations"}), flush=True)
        results.append(record)
    suffix = "-" + only.replace(":", "-") if only else ""
    (OUT / (label_prefix + "smoke-" + config.lower() + suffix + ".json")).write_text(json.dumps(results, indent=2) + "\n")
    return all(r["result"] == "PASS" for r in results)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["probe", "smoke"])
    parser.add_argument("config", choices=["Debug", "Release"])
    parser.add_argument("--only", help="Run one smoke case, for example GEngineEditor:default")
    parser.add_argument("--label-prefix", default="", help="Keep rerun evidence separate")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    raise SystemExit(0 if (probe(args.config) if args.mode == "probe" else smoke(args.config, args.only, args.label_prefix)) else 1)
