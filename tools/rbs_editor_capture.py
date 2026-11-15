"""Local RBS-only visual review driver; records actual client pixels, never composites UI.

Each action requires the session file of the process this tool launched. Use capture
and inspect its image before choosing coordinates. Existing owner runtime/ini is unused.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import time
import zlib

from test_pre_editor_material_authoring import window_for

ROOT = Path(__file__).resolve().parents[1]
user = ctypes.WinDLL("user32", use_last_error=True)
user.SetThreadDpiAwarenessContext.argtypes = [wintypes.HANDLE]
user.SetThreadDpiAwarenessContext(wintypes.HANDLE(-4))
user.SetForegroundWindow.argtypes = [wintypes.HWND]
user.GetForegroundWindow.restype = wintypes.HWND
user.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                            ctypes.c_int, ctypes.c_int, wintypes.UINT]
user.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
user.SetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_long]
user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]


def capture_visible_client(hwnd, destination, settle=True):
    """Read physical screen pixels of the foreground client, avoiding PrintWindow DPI virtualization."""
    user.SetForegroundWindow(hwnd)
    if settle:
        time.sleep(.3)
    if user.GetForegroundWindow() != hwnd:
        raise RuntimeError("RBS must be foreground for a visible-client capture")
    user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.GetDC.argtypes = [wintypes.HWND]
    user.GetDC.restype = wintypes.HDC
    user.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
    rect, origin = wintypes.RECT(), wintypes.POINT()
    user.GetClientRect(hwnd, ctypes.byref(rect))
    user.ClientToScreen(hwnd, ctypes.byref(origin))
    width, height = rect.right, rect.bottom
    gdi = ctypes.WinDLL("gdi32")
    gdi.CreateCompatibleDC.argtypes = [wintypes.HDC]
    gdi.CreateCompatibleDC.restype = wintypes.HDC
    gdi.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
    gdi.CreateCompatibleBitmap.restype = wintypes.HBITMAP
    gdi.SelectObject.argtypes = [wintypes.HDC, wintypes.HANDLE]
    gdi.SelectObject.restype = wintypes.HANDLE
    gdi.BitBlt.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                          wintypes.HDC, ctypes.c_int, ctypes.c_int, wintypes.DWORD]
    gdi.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT,
                            ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]
    gdi.DeleteObject.argtypes = [wintypes.HANDLE]
    gdi.DeleteDC.argtypes = [wintypes.HDC]
    screen = user.GetDC(None)
    memory = gdi.CreateCompatibleDC(screen)
    bitmap = gdi.CreateCompatibleBitmap(screen, width, height)
    previous = gdi.SelectObject(memory, bitmap)
    try:
        if not gdi.BitBlt(memory, 0, 0, width, height, screen, origin.x, origin.y, 0x00CC0020):
            raise RuntimeError("Visible-client BitBlt failed")
        gdi.SelectObject(memory, previous)
        pixels = ctypes.create_string_buffer(width * height * 4)
        header = ctypes.create_string_buffer(struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, 0, 0, 0, 0, 0))
        if gdi.GetDIBits(memory, bitmap, 0, height, pixels, header, 0) != height:
            raise RuntimeError("Visible-client pixel readback failed")
        raw = pixels.raw
        rgb = bytearray(width * height * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
        rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
        destination.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                                + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    finally:
        gdi.SelectObject(memory, previous)
        gdi.DeleteObject(bitmap)
        gdi.DeleteDC(memory)
        user.ReleaseDC(None, screen)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("session", type=Path)
    parser.add_argument("action", choices=["launch", "capture", "click", "double", "drag", "type", "key", "resize", "scroll", "hover", "minimum", "close"])
    parser.add_argument("--name", default="capture")
    parser.add_argument("--config", default="Release", choices=["Debug", "Release"])
    parser.add_argument("--scene", default="distance-chain")
    parser.add_argument("--backend", default="native")
    parser.add_argument("--scale", type=int, choices=[0, 1, 2, 3, 4])
    parser.add_argument("--settings-from", type=Path)
    parser.add_argument("--x", type=int, default=0)
    parser.add_argument("--y", type=int, default=0)
    parser.add_argument("--to-x", type=int, default=0)
    parser.add_argument("--to-y", type=int, default=0)
    parser.add_argument("--text", default="")
    parser.add_argument("--ctrl", action="store_true")
    parser.add_argument("--alt", action="store_true")
    parser.add_argument("--shift", action="store_true")
    parser.add_argument("--startup-seconds", type=float, default=0)
    parser.add_argument("--button", choices=["left", "right", "middle"], default="left")
    parser.add_argument("--tooltip", action="store_true")
    parser.add_argument("--cancel-drag", action="store_true", help="Press Escape while the physical drag button is still held")
    parser.add_argument("--decorated", action="store_true")
    parser.add_argument("--relative", action="store_true", help="Send relative drag motion for SDL captured camera gestures")
    parser.add_argument("--click-after-wheel", action="store_true", help="Keep modifiers held through wheel, pre-click capture and endpoint click at the same pointer position")
    parser.add_argument("--selection-trace", action="store_true")
    parser.add_argument("--pass-timing", action="store_true")
    parser.add_argument("--wheel-steps", type=int, default=1, choices=range(1,101), help="Repeated real wheel events while modifiers remain held")
    parser.add_argument("--motion-captures", action="store_true", help="Capture real client pixels during the drag")
    args = parser.parse_args()
    folder = args.session.resolve()
    if not folder.is_relative_to(ROOT / "logs/ui"):
        raise ValueError("Review session must remain under logs/ui")
    session = folder / "session.json"
    if args.action == "launch":
        launch_start = time.perf_counter()
        folder.mkdir(parents=True, exist_ok=True)
        if session.exists():
            raise ValueError("Use a new session folder; preserve previous evidence")
        runtime = folder / "runtime"
        runtime.mkdir(exist_ok=True)
        if args.settings_from:
            source = args.settings_from.resolve()
            if not source.is_relative_to(ROOT):
                raise ValueError("Settings source must be in this worktree")
            (runtime / "imgui.ini").write_bytes(source.read_bytes())
        elif args.scale is not None:
            (runtime / "imgui.ini").write_text(f"[RbsEditor][Theme]\nScale={args.scale}\n\n")
        exe = ROOT / f"bin/{args.config}/RigidBodySimulation/RigidBodySimulation.exe"
        command = [str(exe), "--scene", args.scene, "--backend", args.backend]
        with (folder / "stdout.log").open("w") as stream:
            env=os.environ.copy()
            if args.selection_trace:env["GENGINE_RBS_SELECTION_TRACE"]="1"
            if args.pass_timing:env["GENGINE_PASS_TIMING_OUTPUT"]=str(folder)
            process = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,env=env)
        data = dict(pid=process.pid, command=command, sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                    capture="Physical foreground RBS client pixels via BitBlt; no UI compositing",
                    scale_override=args.scale, settings_source=str(args.settings_from) if args.settings_from else None)
        session.write_text(json.dumps(data, indent=2))
        for _ in range(100):
            hwnd = window_for(process.pid)
            if hwnd:
                break
            if process.poll() is not None:
                raise RuntimeError(f"RBS exited {process.returncode}: {(folder / 'stdout.log').read_text()}")
            time.sleep(.2)
        else:
            raise RuntimeError("RBS window unavailable")
        data["first_visible_seconds"] = time.perf_counter() - launch_start
        session.write_text(json.dumps(data, indent=2))
        if not args.startup_seconds:
            time.sleep(2)
    else:
        data = json.loads(session.read_text())
        hwnd = window_for(data["pid"])
        if not hwnd:
            raise RuntimeError("Launched RBS process has no visible SDL window")
    if args.action in ("launch", "resize"):
        # Client-only review window, with no desktop capture or display-settings change.
        style = user.GetWindowLongW(hwnd, -16)
        style = (style | 0x00CF0000) if args.decorated else (style & ~0x00CF0000)
        user.SetWindowLongW(hwnd, -16, style)
        width,height=args.x or 1920,args.y or 1080
        if args.decorated:
            rect=wintypes.RECT(0,0,width,height)
            user.AdjustWindowRectExForDpi(ctypes.byref(rect),style,False,0,user.GetDpiForWindow(hwnd))
            width,height=rect.right-rect.left,rect.bottom-rect.top
        user.SetWindowPos(hwnd, None, 0, 0, width,height, 0x0024)
        if not args.startup_seconds:
            time.sleep(1)
    if args.action == "launch" and args.startup_seconds:
        if not 0 < args.startup_seconds <= 30:
            raise ValueError("Startup capture is bounded to 30 seconds")
        frames=[]
        until=time.perf_counter()+args.startup_seconds
        while time.perf_counter()<until and process.poll() is None:
            destination=folder/f"startup-{len(frames):03d}.png"
            capture_visible_client(hwnd,destination,settle=False)
            frames.append(dict(file=destination.name,seconds=time.perf_counter()-launch_start))
            time.sleep(.15)
        (folder/"startup-timing.json").write_text(json.dumps(frames,indent=2))
    if args.action == "minimum":
        class MinMax(ctypes.Structure):
            _fields_=[(name,wintypes.POINT) for name in ("reserved","max_size","max_position","min_track","max_track")]
        info=MinMax(); user.SendMessageW(hwnd,0x24,0,ctypes.byref(info))
        bounds=wintypes.RECT();user.GetClientRect(hwnd,ctypes.byref(bounds))
        (folder/(args.name+".json")).write_text(json.dumps(dict(dpi=user.GetDpiForWindow(hwnd),minimum_track=[info.min_track.x,info.min_track.y],client=[bounds.right,bounds.bottom])))
    if args.action in ("click", "double", "drag", "type", "key", "scroll", "hover"):
        user.SetForegroundWindow(hwnd)
        time.sleep(.15)
        if user.GetForegroundWindow() != hwnd:
            raise RuntimeError("RBS is not foreground; input not sent")
        def key(code, up=False):
            user.keybd_event(code, user.MapVirtualKeyW(code, 0), 2 if up else 0, 0)
        def point(x, y):
            p = wintypes.POINT(x, y)
            user.ClientToScreen(hwnd, ctypes.byref(p))
            user.SetCursorPos(p.x, p.y)
        if args.ctrl:
            key(0x11)
        if args.alt:
            key(0x12)
        if args.shift:
            key(0x10)
        if args.action == "hover":
            point(args.x,args.y)
        elif args.action == "scroll":
            point(args.x,args.to_y or 500);time.sleep(.15)
            for _ in range(args.wheel_steps):
                user.mouse_event(0x800,0,0,args.y*120,0);time.sleep(.03)
            if args.click_after_wheel:
                time.sleep(.15)
                capture_visible_client(hwnd,folder/(args.name+"-before-click.png"),settle=False)
                user.mouse_event(2,0,0,0,0);time.sleep(.05);user.mouse_event(4,0,0,0,0)
        elif args.action in ("click", "double", "drag"):
            point(args.x, args.y)
            time.sleep(.1)
            down,up={"left":(2,4),"right":(8,16),"middle":(32,64)}[args.button]
            user.mouse_event(down, 0, 0, 0, 0)
            if args.action == "drag":
                # Give a slow Debug frame the initial title/grip press before
                # sending movement; otherwise it may first observe the cursor outside.
                time.sleep(.2)
                for i in range(1, 16):
                    if args.relative:
                        dx=(args.to_x-args.x)*i//15-(args.to_x-args.x)*(i-1)//15
                        dy=(args.to_y-args.y)*i//15-(args.to_y-args.y)*(i-1)//15
                        user.mouse_event(1,dx,dy,0,0)
                    else:
                        point(args.x + (args.to_x - args.x) * i // 15,
                              args.y + (args.to_y - args.y) * i // 15)
                    time.sleep(.05)
                    if args.motion_captures and i in (5,10,15):
                        motion=folder/f"{args.name}-motion-{i:02d}.png"
                        if motion.exists():raise ValueError("Motion capture already exists")
                        capture_visible_client(hwnd,motion,settle=False)
            if args.action == "drag" and args.cancel_drag:
                key(0x1B);time.sleep(.08);key(0x1B,True);time.sleep(.1)
            time.sleep(.05)
            user.mouse_event(up, 0, 0, 0, 0)
            if args.action == "double":
                time.sleep(.07)
                user.mouse_event(2, 0, 0, 0, 0)
                user.mouse_event(4, 0, 0, 0, 0)
        elif args.action == "type":
            for char in args.text:
                user.PostMessageW(hwnd, 0x0102, ord(char), 0)
        else:
            code = {"enter": 0x0D, "escape": 0x1B, "tab": 0x09, "a": 0x41,
                    "q": 0x51, "w": 0x57, "e": 0x45, "r": 0x52, "s": 0x53, "t": 0x54, "f": 0x46,
                    "z": 0x5A,"y": 0x59,"p": 0x50,"f2": 0x71}[args.text]
            key(code)
            time.sleep(.05)
            key(code, True)
        if args.ctrl:
            key(0x11, True)
        if args.alt:
            key(0x12, True)
        if args.shift:
            key(0x10, True)
        time.sleep(.6)
    if args.action == "close":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        process_handle = kernel.OpenProcess(0x1000 | 0x100000, False, data["pid"])
        user.PostMessageW(hwnd, 0x0010, 0, 0)
        code = wintypes.DWORD(259)
        if process_handle:
            kernel.WaitForSingleObject(process_handle, 10000)
            kernel.GetExitCodeProcess(process_handle, ctypes.byref(code))
            kernel.CloseHandle(process_handle)
        result = {"closed": window_for(data["pid"]) is None, "exit_code": code.value}
        (folder / "close.json").write_text(json.dumps(result))
        print(result)
        return
    destination = folder / (args.name + ".png")
    if destination.exists():
        raise ValueError("Capture already exists; choose a new name")
    if not args.tooltip:
        user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        bounds = wintypes.RECT()
        user.GetClientRect(hwnd, ctypes.byref(bounds))
        pointer = wintypes.POINT(bounds.right - 5, bounds.bottom - 5)
        user.ClientToScreen(hwnd, ctypes.byref(pointer))
        user.SetCursorPos(pointer.x, pointer.y)
        time.sleep(.2)
    capture_visible_client(hwnd, destination)
    print(destination)


if __name__ == "__main__":
    main()
