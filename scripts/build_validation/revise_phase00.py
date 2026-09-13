"""Append-only Phase 00 revision captures; original blocked evidence is immutable."""

import datetime
import ctypes
from ctypes import wintypes
import gzip
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from pe_runtime import DUMPBIN, ROOT, sha

EVIDENCE = ROOT / "docs/build/evidence/phase-00/revision-01"
ENTRY = ROOT / ".codex/build/phase-00-revision-01-entry.json"
WORK = ROOT / "bin-int/build-phase-00-revision-01"
MSBUILD = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe")
GIT = ["git", "-c", "safe.directory=C:/dev/GEngine-build"]
APPS = ["Breakout", "GEngineEditor", "RayTracing", "RigidBodySimulation"]


def process_windows(pid):
    user = ctypes.WinDLL("user32", use_last_error=True)
    windows = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.GetWindowTextLengthW.argtypes = [wintypes.HWND]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.EnumChildWindows.argtypes = [wintypes.HWND, callback_type, wintypes.LPARAM]

    def title(hwnd):
        text = ctypes.create_unicode_buffer(user.GetWindowTextLengthW(hwnd) + 1)
        user.GetWindowTextW(hwnd, text, len(text))
        return text.value

    def visit(hwnd, parameter):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            children = []
            def child(ch, ignored):
                value = title(ch)
                if value:
                    children.append(value)
                return True
            user.EnumChildWindows(hwnd, callback_type(child), 0)
            window_class = ctypes.create_unicode_buffer(256)
            user.GetClassNameW(hwnd, window_class, len(window_class))
            windows.append({"hwnd": int(hwnd), "title": title(hwnd), "class": window_class.value, "visible": bool(user.IsWindowVisible(hwnd)), "child_text": children})
        return True
    user.EnumWindows(callback_type(visit), 0)
    return windows


def process_modules(process):
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
    psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
    modules = (wintypes.HMODULE * 1024)()
    needed = wintypes.DWORD()
    if not psapi.EnumProcessModulesEx(int(process._handle), modules, ctypes.sizeof(modules), ctypes.byref(needed), 3):
        return {"error": ctypes.get_last_error()}
    rows = []
    for module in modules[:needed.value // ctypes.sizeof(wintypes.HMODULE)]:
        path = ctypes.create_unicode_buffer(32768)
        if psapi.GetModuleFileNameExW(int(process._handle), module, path, len(path)):
            file = Path(path.value)
            rows.append({"path": path.value, "sha256": sha(file) if file.is_file() else "UNREADABLE"})
    return rows


def startup_probe(config, app):
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    executable = ROOT / "bin" / config / app / (app + ".exe")
    assert Path.cwd() == ROOT / app
    env = os.environ.copy()
    env["PATH"] = "C:\\Windows\\System32;C:\\Windows"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    smoke_seconds = 45
    print(json.dumps({"exe": str(executable), "exe_sha256": sha(executable), "cwd": str(Path.cwd()), "PATH": env["PATH"], "smoke_seconds": smoke_seconds}), flush=True)
    process = subprocess.Popen([str(executable)], cwd=ROOT / app, env=env, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    observations = []
    before = time.monotonic()
    while process.poll() is None and time.monotonic() - before < smoke_seconds:
        time.sleep(1)
        windows = process_windows(process.pid)
        sample = {"elapsed_seconds": round(time.monotonic() - before, 2), "windows": windows}
        if len(observations) in (0, 4, 13):
            sample["modules"] = process_modules(process)
        observations.append(sample)
        print(json.dumps(sample), flush=True)
    alive = process.poll() is None
    windows = process_windows(process.pid) if alive else []
    if alive:
        user = ctypes.WinDLL("user32", use_last_error=True)
        user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        for window in windows:
            if window["class"] == "SDL_app":
                user.PostMessageW(window["hwnd"], 0x0010, 0, 0)
    terminated = False
    try:
        code = process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        terminated = True
        process.kill()
        code = process.wait()
    dialogs = [w for sample in observations for w in sample["windows"]
               if any(word in (w["title"] + " ".join(w["child_text"])).lower() for word in ["assertion failed", "runtime error", "debug error"])]
    visible = any(w["visible"] for sample in observations for w in sample["windows"])
    created = any(w["class"] == "SDL_app" for sample in observations for w in sample["windows"])
    result = {"alive_for_smoke": alive, "visible_window_observed": visible, "SDL_window_created": created,
              "window_policy": "STARTF_USESHOWWINDOW/SW_HIDE keeps the validation window hidden; close the SDL window by class even when hidden", "diagnostic_dialogs": dialogs,
              "exit_code": code, "exit_hex": hex(code & 0xffffffff), "forced_termination": terminated}
    app_log = ROOT / app / "GEngine.log"
    if app_log.is_file():
        result["application_log"] = {"path": str(app_log), "sha256": sha(app_log), "text": app_log.read_text(errors="replace")}
    print(json.dumps(result), flush=True)
    return 0 if alive and created and not dialogs and code == 0 and not terminated else 1


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def run(name, argv, cwd=ROOT, config=None, timeout=120, extra_env=None):
    entry = json.loads(ENTRY.read_text())
    allowed = {r["path"] for r in entry["new_owned_files"]}
    output = EVIDENCE / "commands" / (name + ".txt.gz")
    metadata = output.with_name(name + ".json")
    assert output.relative_to(ROOT).as_posix() in allowed
    assert metadata.relative_to(ROOT).as_posix() in allowed
    assert not output.exists() and not metadata.exists(), "Capture already exists"
    output.parent.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    raw = WORK / (name + ".txt")
    assert not raw.exists(), "Raw capture already exists"
    env = os.environ.copy()
    env.update({"PYTHONDONTWRITEBYTECODE": "1", **(extra_env or {})})
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    before = time.monotonic()
    timed_out = False
    with raw.open("wb") as stream:
        process = subprocess.Popen([str(a) for a in argv], cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
        print(json.dumps({"started": name, "pid": process.pid, "timeout_seconds": timeout}), flush=True)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            code = process.wait()
            timed_out = True
    output.write_bytes(gzip.compress(raw.read_bytes(), mtime=0))
    record = {"name": name, "revision": "01", "argv": [str(a) for a in argv],
              "command": subprocess.list2cmdline([str(a) for a in argv]), "cwd": str(cwd),
              "configuration": config, "started_utc": started, "elapsed_seconds": round(time.monotonic() - before, 3),
              "timeout_seconds": timeout, "timed_out": timed_out, "exit_code": code,
              "result": "INCOMPLETE_TIMEOUT" if timed_out else "PASS" if code == 0 else "FAIL",
              "environment_override": {"PYTHONDONTWRITEBYTECODE": "1", **(extra_env or {})},
              "raw_log": raw.relative_to(ROOT).as_posix(), "raw_sha256": sha(raw),
              "compressed_log": output.relative_to(ROOT).as_posix(), "compressed_sha256": sha(output),
              "initial_lineage": "86621b4bcbcbd46a79eaacc35de270b0b833b6eda9b1138a503b2b1eeb1f2a70"}
    write_json(metadata, record)
    print(json.dumps(record), flush=True)
    return code


def main():
    action = sys.argv[1]
    if action == "_startup":
        return startup_probe(sys.argv[2], sys.argv[3])
    if action == "startup":
        config, app = sys.argv[2:4]
        suffix = "-recheck" if len(sys.argv) > 4 else ""
        return run(f"startup-{config}-{app}{suffix}", [sys.executable, Path(__file__), "_startup", config, app], cwd=ROOT / app, config=config, timeout=75)
    if action == "imports":
        for config in ["Debug", "Release"]:
            for app in APPS:
                if run(f"imports-{config}-{app}", [DUMPBIN, "/imports", ROOT / "bin" / config / app / (app + ".exe")], config=config):
                    return 1
        return 0
    if action == "tests":
        config, selector = sys.argv[2:4]
        argv = [ROOT / "bin" / config / "PhysicsTests/PhysicsTests.exe"]
        if selector != "full":
            argv.append("--" + selector)
        return run(f"tests-{config}-{selector}", argv, config=config, timeout=1800 if selector == "full" else 300)
    if action == "generate":
        mode = sys.argv[2]
        return run("premake-" + mode, [ROOT / "vendor/bin/premake/premake5.exe", *(["--physics-profiling"] if mode == "profile" else []), "vs2022"])
    if action == "build":
        config, mode = sys.argv[2:4]
        return run("build-" + ("" if mode == "normal" else mode + "-") + config,
                   [MSBUILD, "GEngine.sln", "/m:2", "/nr:false", "/nologo", "/p:Configuration=" + config,
                    "/p:Platform=x64", "/v:diag", "/t:GEngine,PhysicsBenchmark" if mode == "profile" else "/t:Build"],
                   config=config, timeout=1800)
    if action == "benchmark":
        config, mode = sys.argv[2:4]
        argv = [ROOT / "bin" / config / "PhysicsBenchmark/PhysicsBenchmark.exe", "--body-counts=50,100,200,500,1000,2000",
                "--warmup=2", "--samples=5", "--dt=0.008333333"]
        if mode.endswith("-steady"):
            argv += ["--steady-state-warmup-steps=4", "--steady-state-measured-steps=8"]
        return run(f"benchmark-{config}-{mode}", argv, config=config, timeout=900)
    raise ValueError(action)


if __name__ == "__main__":
    sys.exit(main())
