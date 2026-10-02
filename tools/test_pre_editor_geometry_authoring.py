"""Run the accepted Phase 08 authoring protocol inside RBS, once per configuration."""
import argparse
import ctypes
from ctypes import wintypes
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import threading
import time

from test_pre_editor_geometry_templates import application_smoke

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def local(value):
    path = Path(value)
    if not path.is_absolute():
        path = ROOT / path
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError("Path escapes PRE_EDITOR worktree: " + str(path))
    for parent in (path, *path.parents):
        if parent == ROOT.parent:
            break
        if parent.exists() and parent.lstat().st_file_attributes & 0x400:
            raise ValueError("Reparse ancestor: " + str(parent))
    return path


def observe_window(executable, stopped, observations):
    """Read only the launched executable's visible SDL window; no input injection."""
    user = ctypes.WinDLL("user32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.GetDpiForWindow.argtypes = [wintypes.HWND]
    user.GetWindowDpiAwarenessContext.argtypes = [wintypes.HWND]
    user.GetWindowDpiAwarenessContext.restype = wintypes.HANDLE
    user.GetAwarenessFromDpiAwarenessContext.argtypes = [wintypes.HANDLE]
    user.GetAwarenessFromDpiAwarenessContext.restype = ctypes.c_int
    user.SetThreadDpiAwarenessContext.argtypes = [wintypes.HANDLE]
    user.SetThreadDpiAwarenessContext.restype = wintypes.HANDLE
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    previous = user.SetThreadDpiAwarenessContext(wintypes.HANDLE(-4))

    @callback_type
    def inspect(hwnd, _):
        name = ctypes.create_unicode_buffer(256)
        user.GetClassNameW(hwnd, name, 256)
        if name.value != "SDL_app" or not user.IsWindowVisible(hwnd):
            return True
        pid = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        process = kernel.OpenProcess(0x1000, False, pid.value)
        if not process:
            return True
        try:
            image = ctypes.create_unicode_buffer(32768)
            length = wintypes.DWORD(len(image))
            if not kernel.QueryFullProcessImageNameW(process, 0, image, ctypes.byref(length)):
                return True
            if Path(image.value).resolve() != executable.resolve():
                return True
            rect = wintypes.RECT()
            dpi = user.GetDpiForWindow(hwnd)
            if user.GetClientRect(hwnd, ctypes.byref(rect)) and dpi:
                pixels = [rect.right - rect.left, rect.bottom - rect.top]
                target_context = user.GetWindowDpiAwarenessContext(hwnd)
                target_previous = user.SetThreadDpiAwarenessContext(target_context) if target_context else None
                if not target_previous:
                    return True
                try:
                    logical_rect = wintypes.RECT()
                    if not user.GetClientRect(hwnd, ctypes.byref(logical_rect)):
                        return True
                    logical = [logical_rect.right - logical_rect.left, logical_rect.bottom - logical_rect.top]
                finally:
                    user.SetThreadDpiAwarenessContext(target_previous)
                if not all(logical) or not previous:
                    return True
                observations.update(pid=pid.value, client_pixels=pixels, window_dpi=dpi,
                                    logical_client=logical,
                                    physical_to_logical_scale=[p / v for p, v in zip(pixels, logical)],
                                    window_dpi_awareness=user.GetAwarenessFromDpiAwarenessContext(target_context),
                                    logical_measurement="GetClientRect under target window DPI context",
                                    thread_per_monitor_dpi_aware=bool(previous))
        finally:
            kernel.CloseHandle(process)
        return True

    try:
        while not stopped.is_set() and not observations:
            user.EnumWindows(inspect, 0)
            stopped.wait(.05)
    finally:
        if previous:
            user.SetThreadDpiAwarenessContext(previous)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    manifest_path = local(parser.parse_args().manifest)
    manifest = json.loads(manifest_path.read_bytes())
    assert manifest["phase"] == "08" and manifest["application"] == "RigidBodySimulation"
    assert manifest["protocol"]["repeats"] == 0
    configurations = [case["configuration"] for case in manifest["cases"]]
    assert configurations in (["Debug"], ["Release"], ["Debug", "Release"])
    assert manifest["protocol"]["runs"] == len(configurations)
    for path, identity in manifest["input_hashes"].items():
        assert digest(local(path)) == identity, path
    output = local(manifest["output"])
    output.mkdir(parents=True, exist_ok=False)
    report = {"phase": "08", "candidate": manifest["candidate"], "result": "RUNNING",
              "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "manifest_sha256": digest(manifest_path), "cases": []}
    campaign_start = time.monotonic()
    for case in manifest["cases"]:
        executable, directory, assets = [local(case[key]) for key in ("executable", "cwd", "asset_root")]
        assert executable.name == "RigidBodySimulation.exe" and digest(executable) == case["binary_sha256"]
        for path, identity in manifest["input_hashes"].items():
            assert digest(local(path)) == identity, path
        for path, identity in case["asset_hashes"].items():
            assert digest(local(assets / path)) == identity, path
        for module in case["runtime_modules"].values():
            assert digest(local(module["path"])) == module["sha256"]
        directory.mkdir(parents=True, exist_ok=True)
        archive = local(output / case["configuration"].lower())
        archive.mkdir(exist_ok=False)
        names = ("imgui.ini", "GEngine.log", "application.log", "smoke.json", "shutdown-stack.log")
        for name in names:
            if (directory / name).exists():
                shutil.copy2(directory / name, archive / ("before-" + name))
        (directory / "imgui.ini").write_bytes(manifest["layout"].encode("utf-8"))
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("GENGINE_", "SDL_", "ASAN_"))}
        for key in list(env):
            if key.lower() == "path":
                env[key] = str(executable.parent) + os.pathsep + os.pathsep.join(
                    part for part in env[key].split(os.pathsep) if "gengine-rendering" not in part.lower())
        env.update(GENGINE_ASSET_ROOT=str(assets), GENGINE_PRE_EDITOR_GEOMETRY_AUTHORING="1",
                   TMP=str(local("runtime/tmp")), TEMP=str(local("runtime/tmp")), PYTHONDONTWRITEBYTECODE="1")
        local("runtime/tmp").mkdir(parents=True, exist_ok=True)
        stopped, observation = threading.Event(), {}
        observer = threading.Thread(target=observe_window, args=(executable, stopped, observation), daemon=True)
        observer.start()
        started = time.monotonic()
        try:
            record = application_smoke(executable, directory, env, case["runtime_modules"], case["required_markers"])
        finally:
            stopped.set()
            observer.join(timeout=2)
        for name in names:
            if (directory / name).exists():
                shutil.copy2(directory / name, archive / name)
        observed = "\n".join((archive / name).read_text(encoding="utf-8", errors="replace")
                             for name in ("GEngine.log", "application.log") if (archive / name).exists())
        record.update(configuration=case["configuration"], window=observation,
                      elapsed_seconds=round(time.monotonic() - started, 3))
        record["window_matches_requested_client"] = observation.get("logical_client") == manifest["viewport"]["requested_client_logical"]
        record["required_markers"] = {m: m in observed for m in case["required_markers"]}
        record["forbidden_markers"] = {m: m in observed for m in case["forbidden_markers"]}
        record["ownership_work"] = re.findall(r"PRE_EDITOR_PHASE_08_OWNERS population=(\d+) queries=100 key_comparisons=(\d+) copy_additions=(\d+) receipt_bytes=(\d+) copied_receipt_bytes=(\d+)", observed)
        # Each logger writes stdout and file; compare the unique exact observations.
        record["ownership_work"] = sorted(set(record["ownership_work"]), key=lambda row: int(row[0]))
        record["ownership_work_pass"] = [int(row[0]) for row in record["ownership_work"]] == [1, 64, 1024] and all(
            int(row[1]) <= 900 and row[0] == row[2] for row in record["ownership_work"])
        record["presentation"] = sorted(set(re.findall(r"PRE_EDITOR_PHASE_08_PRESENTATION[^\r\n]+", observed)))
        record["inputs_unchanged"] = all(digest(local(path)) == identity for path, identity in manifest["input_hashes"].items())
        record["inputs_unchanged"] &= all(digest(local(assets / path)) == identity for path, identity in case["asset_hashes"].items())
        record["inputs_unchanged"] &= digest(executable) == case["binary_sha256"]
        if (not all(record["required_markers"].values()) or any(record["forbidden_markers"].values())
                or not record["inputs_unchanged"] or not record["ownership_work_pass"]
                or not record["window_matches_requested_client"] or not record["presentation"]
                or record["elapsed_seconds"] > manifest["protocol"]["per_process_cap_seconds"]):
            record["result"] = "FAIL"
        report["cases"].append(record)
        report.update(result=record["result"], elapsed_seconds=round(time.monotonic() - campaign_start, 3))
        if report["elapsed_seconds"] > manifest["protocol"]["total_runtime_cap_seconds"]:
            report["result"] = "FAIL"
        (archive / "result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(record["result"] + ": Phase 08 " + case["configuration"], flush=True)
        if report["result"] != "PASS":
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
