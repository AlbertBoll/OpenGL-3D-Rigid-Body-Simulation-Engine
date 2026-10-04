"""Bounded Phase 09 GEngine/RBS build and runtime checks; no retries or sealing."""
import argparse
import ctypes
from ctypes import wintypes
import datetime
import hashlib
import json
import math
import os
import re
from pathlib import Path
import shutil
import subprocess
import struct
import threading
import time
import zlib

from test_frame_submission import loaded_modules
from test_pre_editor_geometry_authoring import local, observe_window

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    local(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def window_for(pid):
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    found = []

    @callback_type
    def inspect(hwnd, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(128)
        user.GetClassNameW(hwnd, name, 128)
        if owner.value == pid and name.value == "SDL_app" and user.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user.EnumWindows(inspect, 0)
    return found[0] if found else None


def capture(hwnd, destination, observe_boundary=None):
    """Capture only this RBS client window; no desktop or unrelated applications."""
    user = ctypes.WinDLL("user32", use_last_error=True)
    gdi = ctypes.WinDLL("gdi32", use_last_error=True)
    user.GetDC.argtypes = [wintypes.HWND]
    user.GetDC.restype = wintypes.HDC
    user.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
    user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
    gdi.CreateCompatibleDC.argtypes = [wintypes.HDC]
    gdi.CreateCompatibleDC.restype = wintypes.HDC
    gdi.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
    gdi.CreateCompatibleBitmap.restype = wintypes.HBITMAP
    gdi.SelectObject.argtypes = [wintypes.HDC, wintypes.HANDLE]
    gdi.SelectObject.restype = wintypes.HANDLE
    gdi.DeleteObject.argtypes = [wintypes.HANDLE]
    gdi.DeleteDC.argtypes = [wintypes.HDC]

    class Header(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("width", wintypes.LONG), ("height", wintypes.LONG),
                    ("planes", wintypes.WORD), ("bits", wintypes.WORD), ("compression", wintypes.DWORD),
                    ("image_size", wintypes.DWORD), ("xppm", wintypes.LONG), ("yppm", wintypes.LONG),
                    ("used", wintypes.DWORD), ("important", wintypes.DWORD)]

    gdi.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT,
                            ctypes.c_void_p, ctypes.POINTER(Header), wintypes.UINT]
    rect = wintypes.RECT()
    if not user.GetClientRect(hwnd, ctypes.byref(rect)):
        return False
    width, height = rect.right, rect.bottom
    dc = user.GetDC(hwnd)
    memory = gdi.CreateCompatibleDC(dc)
    bitmap = gdi.CreateCompatibleBitmap(dc, width, height)
    previous = gdi.SelectObject(memory, bitmap)
    try:
        if observe_boundary:
            observe_boundary("before_readback")
        if not user.PrintWindow(hwnd, memory, 3):
            return False
        gdi.SelectObject(memory, previous)
        pixels = ctypes.create_string_buffer(width * height * 4)
        header = Header(ctypes.sizeof(Header), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
        if gdi.GetDIBits(memory, bitmap, 0, height, pixels, ctypes.byref(header), 0) != height:
            return False
        # Bind the image to the actual pixel acquisition interval. PNG encoding
        # and disk I/O below must not extend that interval into a later pose.
        if observe_boundary:
            observe_boundary("after_readback")
        raw = pixels.raw
        rgb = bytearray(width * height * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
        rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
        local(destination).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                                       + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
        return True
    finally:
        gdi.SelectObject(memory, previous)
        gdi.DeleteObject(bitmap)
        gdi.DeleteDC(memory)
        user.ReleaseDC(hwnd, dc)


def checked_view(line, case):
    fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
    stage, sample = int(fields["stage"]), int(fields["sample"])
    if not 0 <= stage < 16 or int(fields["mode"]) != 1 + stage % 2:
        raise RuntimeError("Unexpected comparison scenario")
    primitive = (stage - 2) // 2 if 2 <= stage < 14 else -1
    focal = [0, 2, -22] if primitive < 0 else [-10 + 4 * primitive, 2, -20]
    pitch, yaw, distance = .35, (0 if primitive < 0 else .65), (38 if primitive < 0 else 7)
    forward = [math.sin(yaw) * math.cos(pitch), -math.sin(pitch), -math.cos(yaw) * math.cos(pitch)]
    expected = [focal[i] - forward[i] * distance for i in range(3)]
    position = [float(v) for v in fields["position"].split(",")]
    if len(position) != 3 or any(not math.isfinite(v) or abs(v - e) > 1e-4 for v, e in zip(position, expected)):
        raise RuntimeError("Actual submitted camera position does not match reviewed pose: " + line)
    for key, value in (("pitch", pitch), ("yaw", yaw), ("fov", math.pi / 4), ("near", .1), ("far", 1000)):
        actual = float(fields[key])
        if not math.isfinite(actual) or abs(actual - value) > 1e-5:
            raise RuntimeError("Actual camera angle/projection mismatch: " + key)
    viewport = [int(v) for v in fields["viewport"].split(",")]
    if viewport != case["render_viewport"]:
        raise RuntimeError("Actual render viewport mismatch: " + fields["viewport"])
    aspect = viewport[0] / viewport[1]
    f = 1 / math.tan(math.pi / 8)
    projection = [f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, -1000.1 / 999.9, -1, 0, 0, -200 / 999.9, 0]
    actual = [float(v) for v in fields["projection"].split(",")]
    if len(actual) != 16 or any(not math.isfinite(v) or abs(v - e) > 1e-5 for v, e in zip(actual, projection)):
        raise RuntimeError("Actual submitted projection matrix mismatch")
    if (stage < 14 and (sample != 0 or int(fields["motion_step"]) != 0)) or (stage >= 14 and
            (sample not in (1, 2, 3, 4) or int(fields["motion_step"]) != sample * 15)):
        raise RuntimeError("Actual deterministic transform pose mismatch")
    return f"{stage:02d}-{sample}", fields


def runtime(case, directory, env, report):
    executable = local(case["executable"])
    cwd = local(case["cwd"])
    cwd.mkdir(parents=True, exist_ok=True)
    for name in ("imgui.ini", "GEngine.log"):
        if (cwd / name).exists():
            shutil.copy2(cwd / name, directory / ("before-" + name))
    # The new process has not initialized logging yet. Retain the old log above,
    # then remove stale markers from this owned mutable output before observation.
    (cwd / "GEngine.log").write_bytes(b"")
    (cwd / "imgui.ini").write_text(case["layout"], encoding="utf-8")
    env.update(GENGINE_ASSET_ROOT=str(local(case["assets"])), GENGINE_PRE_EDITOR_MATERIAL_AUTHORING="1",
               GENGINE_PRE_EDITOR_MATERIAL_ALPHA_FIXTURE=str(local(case["alpha_fixture"])))
    report.update(command=[str(executable)], cwd=str(cwd), binary_sha256=digest(executable),
                  assets=case["assets"], automated_visual_result="PENDING_INSPECTION", human_result="PENDING")
    stop = threading.Event()
    observation = {}
    observer = threading.Thread(target=observe_window, args=(executable, stop, observation), daemon=True)
    observer.start()
    begun = time.monotonic()
    with (directory / "application.log").open("wb") as log:
        child = subprocess.Popen([str(executable)], cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        report["pid"] = child.pid
        watchdog = threading.Timer(90, lambda: child.kill() if child.poll() is None else None)
        watchdog.start()
        try:
            captures = report["captures"] = {}
            while child.poll() is None and time.monotonic() - begun < 85:
                engine = cwd / "GEngine.log"
                engine_text = engine.read_text(encoding="utf-8", errors="replace") if engine.exists() else ""
                text = engine_text
                text += (directory / "application.log").read_text(encoding="utf-8", errors="replace")
                if "PRE_EDITOR_PHASE_09_FAIL" in text or "[error]" in text or "[critical]" in text:
                    raise RuntimeError("RBS reported a failed check or runtime error")
                hwnd = window_for(child.pid)
                if hwnd:
                    views = re.findall(r"PRE_EDITOR_PHASE_09_VIEW stage=[^\r\n]+", engine_text)
                    beginnings = re.findall(r"PRE_EDITOR_PHASE_09_VIEW_BEGIN stage=(\d+)", engine_text)
                    if views:
                        key, fields = checked_view(views[-1], case)
                        if key not in captures and beginnings and int(beginnings[-1]) == int(fields["stage"]):
                            # A marker follows successful submission; wait for that
                            # frame's presentation, then reject a changed scenario.
                            time.sleep(.08)
                            name = directory / f"view-{key}.png"
                            boundaries = {}
                            def observe_boundary(boundary):
                                observed = engine.read_text(encoding="utf-8", errors="replace")
                                stages = re.findall(r"PRE_EDITOR_PHASE_09_VIEW_BEGIN stage=(\d+)", observed)
                                markers = re.findall(r"PRE_EDITOR_PHASE_09_VIEW stage=[^\r\n]+", observed)
                                boundaries[boundary] = {"seconds": time.monotonic() - begun,
                                    "stage": stages[-1] if stages else None,
                                    "marker": markers[-1] if markers else None,
                                    "matches": bool(stages and markers and stages[-1] == fields["stage"]
                                                    and markers[-1] == views[-1])}
                            item = captures[key] = {"path": str(name), "actual": fields,
                                                    "marker": views[-1], "readback_boundaries": boundaries}
                            item["captured"] = capture(hwnd, name, observe_boundary)
                            item["serialization_finished_seconds"] = time.monotonic() - begun
                            if not item["captured"] or set(boundaries) != {"before_readback", "after_readback"} or not all(
                                    boundary["matches"] for boundary in boundaries.values()):
                                raise RuntimeError("Capture unavailable or scenario changed during capture")
                            item["sha256"] = digest(name)
                    if all(marker in text for marker in case["markers"]):
                        if len(captures) != 22:
                            raise RuntimeError("Required static/matched-motion capture missing; no retry")
                        report["loaded_modules"] = loaded_modules(child.pid)
                        for name, expected in case["modules"].items():
                            actual = report["loaded_modules"].get(name.lower())
                            if actual is None or Path(actual).resolve() != local(expected["path"]) or digest(actual) != expected["sha256"]:
                                raise RuntimeError("Loaded dependency mismatch: " + name)
                        user = ctypes.WinDLL("user32", use_last_error=True)
                        user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
                        if not user.PostMessageW(hwnd, 0x0010, 0, 0):
                            raise RuntimeError("Native RBS close failed")
                        report["exit"] = child.wait(timeout=15)
                        if report["exit"] != 0:
                            raise RuntimeError("RBS shutdown failed")
                        if not all(item["captured"] for item in captures.values()):
                            raise RuntimeError("Required comparison capture unavailable")
                        report["captures"] = captures
                        report["result"] = "PASS"
                        break
                time.sleep(.05)
            else:
                raise RuntimeError("RBS exited early or exceeded the marker deadline")
        finally:
            watchdog.cancel()
            if child.poll() is None:
                child.kill()
                child.wait(timeout=10)
                report["forced_stop"] = True
            report.setdefault("exit", child.returncode)
            stop.set()
            observer.join(timeout=2)
            report["window"] = observation
            report["seconds"] = time.monotonic() - begun
            for name in ("imgui.ini", "GEngine.log"):
                if (cwd / name).exists():
                    shutil.copy2(cwd / name, directory / name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--mode", choices=("build", "runtime"), required=True)
    parser.add_argument("--configuration", choices=("Debug", "Release"), required=True)
    args = parser.parse_args()
    manifest_path = local(args.manifest)
    manifest = json.loads(manifest_path.read_bytes())
    assert manifest["phase"] == "09" and manifest["repeats"] == 0
    output = local(manifest["evidence_root"]) / (args.mode + "-" + args.configuration)
    output.mkdir(parents=True, exist_ok=False)
    report = {"phase": "09", "candidate": manifest["candidate"], "result": "FAIL", "mode": args.mode,
              "configuration": args.configuration, "manifest_sha256": digest(manifest_path),
              "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("GENGINE_", "SDL_", "ASAN_"))}
    env.update(TMP=str(local("runtime/tmp")), TEMP=str(local("runtime/tmp")), PYTHONDONTWRITEBYTECODE="1")
    began = time.monotonic()
    try:
        for path, expected in manifest["input_hashes"].items():
            if digest(local(path)) != expected:
                raise RuntimeError("Pre-campaign behavior input mismatch: " + path)
        for path, expected in manifest["external_inputs"].items():
            if digest(path) != expected["sha256"]:
                raise RuntimeError("Pre-campaign toolchain/dependency mismatch: " + path)
        for path in manifest["writer_roots"]:
            local(path)
        if args.mode == "build":
            command = [manifest["msbuild"], "RigidBodySimulation/RigidBodySimulation.vcxproj", "/t:Build",
                       "/p:Configuration=" + args.configuration, "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
                       "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/m:1", "/nr:false", "/nologo", "/v:normal"]
            report.update(command=command, cwd=str(ROOT), required_projects=["GEngine", "glad", "RigidBodySimulation"])
            with (output / "build.log").open("wb") as log:
                completed = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                           timeout=1800, creationflags=subprocess.CREATE_NO_WINDOW)
            report["exit"] = completed.returncode
            if completed.returncode:
                raise RuntimeError("Required dependency build failed; no retry authorized")
            report["products"] = {p: digest(local(p)) for p in manifest["products"][args.configuration]}
            report["result"] = "PASS"
        else:
            case = manifest["cases"][args.configuration]
            if digest(local(case["executable"])) != case["binary_sha256"]:
                raise RuntimeError("Runtime product differs from the admitted binary")
            for name, expected in case["asset_hashes"].items():
                if digest(local(Path(case["assets"]) / name)) != expected:
                    raise RuntimeError("Staged runtime asset mismatch: " + name)
            runtime(case, output, env, report)
        for path, expected in manifest["input_hashes"].items():
            if digest(local(path)) != expected:
                raise RuntimeError("Behavior input changed during validation: " + path)
        for path, expected in manifest["external_inputs"].items():
            if digest(path) != expected["sha256"]:
                raise RuntimeError("Toolchain/dependency changed during validation: " + path)
    except Exception as error:
        report.update(result="FAIL", reason=str(error), exception_type=type(error).__name__)
    finally:
        report["elapsed_seconds"] = time.monotonic() - began
        write_json(output / "result.json", report)
    print(json.dumps({k: report[k] for k in ("mode", "configuration", "result", "elapsed_seconds")}, indent=2), flush=True)
    if report["result"] != "PASS":
        print(report.get("reason", "Required gate failed"), flush=True)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
