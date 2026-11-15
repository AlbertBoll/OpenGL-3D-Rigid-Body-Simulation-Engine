"""Bounded desktop check of RBS's real pre-window asset failure dialog."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
from rbs_editor_capture import ROOT, capture_visible_client, user


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,default=ROOT/'logs/ui/owner-corrections-05/startup-error-02')
    folder=parser.parse_args().output.resolve()
    if not folder.is_relative_to(ROOT/'logs/ui/owner-corrections-05'):
        raise ValueError('Use a new Owner Corrections 05 evidence destination')
    folder.mkdir(parents=True, exist_ok=False)
    exe = ROOT / 'bin/Release/RigidBodySimulation/RigidBodySimulation.exe'
    package=folder/'app'
    package.mkdir()
    isolated=package/exe.name
    shutil.copy2(exe,isolated)
    for library in exe.parent.glob('*.dll'):
        shutil.copy2(library,package/library.name)
    started = time.monotonic()
    with (folder / 'stdout.log').open('wb') as log:
        process = subprocess.Popen([str(isolated)], cwd=folder, stdout=log, stderr=subprocess.STDOUT)
    (folder/'session.json').write_text(json.dumps(dict(pid=process.pid,command=[str(isolated)],sha256=hashlib.sha256(exe.read_bytes()).hexdigest()),indent=2))
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    found = []

    @callback_type
    def inspect(hwnd, unused):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == process.pid and user.IsWindowVisible(hwnd):
            title = ctypes.create_unicode_buffer(256)
            user.GetWindowTextW(hwnd, title, 256)
            if title.value == 'GEngine initialization failed':
                found.append(hwnd)
        return True

    while time.monotonic() - started < 10 and process.poll() is None:
        user.EnumWindows(inspect, 0)
        if found:
            break
        time.sleep(.1)
    if not found:
        raise RuntimeError('The explicit startup error dialog did not appear')
    capture_visible_client(found[0], folder / 'asset-error-dialog.png')
    user.PostMessageW(found[0], 0x0010, 0, 0)
    code = process.wait(timeout=10)
    result = dict(result='PASS' if code == 1 else 'FAIL', exit_code=code,
                  sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  seconds=time.monotonic()-started, injected='Isolated copy of final executable/runtime DLLs without an asset package; canonical resources untouched')
    (folder / 'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))
    return 0 if code == 1 else 1


if __name__ == '__main__':
    raise SystemExit(main())
