"""Run admitted Phase 05 CPU/resource cases and async integration inside RBS only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import time

import ctypes
from ctypes import wintypes
import subprocess
import threading
from test_frame_submission import loaded_modules

def application_smoke(executable, directory, env, required_modules=None, required_markers=()):
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
        watchdog = threading.Timer(120, lambda: child.kill() if child.poll() is None else None)
        watchdog.start()
        try:
            deadline = time.monotonic() + 75
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
                    observed = log.read_text(encoding="utf-8", errors="replace")
                    engine_log = directory / "GEngine.log"
                    if engine_log.exists():
                        observed += engine_log.read_text(encoding="utf-8", errors="replace")
                    if all(marker in observed for marker in required_markers):
                        break
                    ready = None
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
        finally:
            watchdog.cancel()
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=10)
                record["terminated"] = True
            record.setdefault("exit", child.returncode)
    (directory / "smoke.json").write_text(json.dumps(record, indent=2) + "\n")
    print(f"[{record['result']}] smoke {executable.stem}: exit={record['exit']}", flush=True)
    return record


ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def local(value):
    path = Path(value)
    if not path.is_absolute():
        path = ROOT / path
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError("Output/input escapes worktree: " + str(path))
    for parent in (path, *path.parents):
        if parent == ROOT.parent:
            break
        if parent.exists() and parent.lstat().st_file_attributes & 0x400:
            raise ValueError("Reparse point: " + str(parent))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    args = parser.parse_args()
    manifest_path = local(args.manifest)
    manifest = json.loads(manifest_path.read_bytes())
    assert manifest["phase"] == "05" and manifest["application"] == "RigidBodySimulation"
    assert manifest["protocol"]["repeats"] == 0
    for name, sha in manifest["input_hashes"].items():
        assert digest(local(name)) == sha, name
    output = local(manifest["output"])
    output.mkdir(parents=True, exist_ok=False)
    report = {"result": "RUNNING", "manifest_sha256": digest(manifest_path), "cases": []}
    started = time.monotonic()
    for case in manifest["cases"]:
        executable = local(case["executable"])
        directory = local(case["cwd"])
        assets = local(case["asset_root"])
        assert executable.name == "RigidBodySimulation.exe"
        assert digest(executable) == case["binary_sha256"]
        for name, sha in case["asset_hashes"].items():
            assert digest(local(assets / name)) == sha, name
        directory.mkdir(parents=True, exist_ok=True)
        archive = local(output / case["name"])
        archive.mkdir(exist_ok=False)
        for name in ("imgui.ini", "GEngine.log", "application.log", "smoke.json"):
            if (directory / name).exists():
                shutil.copy2(directory / name, archive / ("before-" + name))
        (directory / "imgui.ini").write_bytes(manifest["layout"].encode("utf-8"))
        env = {k: v for k, v in os.environ.items()
               if not k.upper().startswith(("GENGINE_", "SDL_", "ASAN_"))}
        env.update(GENGINE_ASSET_ROOT=str(assets), GENGINE_PRE_EDITOR_RESOURCE_OWNERSHIP="1", GENGINE_ASYNC_MESH_SMOKE="1",
                   TMP=str(ROOT / "runtime/tmp"), TEMP=str(ROOT / "runtime/tmp"),
                   PYTHONDONTWRITEBYTECODE="1")
        case_start = time.monotonic()
        record = application_smoke(executable, directory, env, case["runtime_modules"], case["required_markers"])
        for name in ("GEngine.log", "application.log", "imgui.ini", "smoke.json", "shutdown-stack.log"):
            if (directory / name).exists():
                shutil.copy2(directory / name, archive / name)
        observed = ""
        for name in ("GEngine.log", "application.log"):
            if (archive / name).exists():
                observed += (archive / name).read_text(encoding="utf-8", errors="replace")
        record["required_markers"] = {m: m in observed for m in case["required_markers"]}
        record["forbidden_markers"] = {m: m in observed for m in case["forbidden_markers"]}
        record["elapsed_seconds"] = round(time.monotonic() - case_start, 3)
        record["name"] = case["name"]
        if (not all(record["required_markers"].values()) or any(record["forbidden_markers"].values())
                or record["elapsed_seconds"] > manifest["protocol"]["per_process_cap_seconds"]):
            record["result"] = "FAIL"
        record["inputs_unchanged"] = all(digest(local(assets / n)) == h for n, h in case["asset_hashes"].items())
        record["inputs_unchanged"] &= all(digest(local(n)) == h for n, h in manifest["input_hashes"].items())
        if not record["inputs_unchanged"]:
            record["result"] = "FAIL"
        report["cases"].append(record)
        report["result"] = record["result"]
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        if report["elapsed_seconds"] > manifest["protocol"]["total_runtime_cap_seconds"]:
            report["result"] = "FAIL"
        (archive / "result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(record["result"] + ": " + case["name"], flush=True)
        if report["result"] != "PASS":
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
