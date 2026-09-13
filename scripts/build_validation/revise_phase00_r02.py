"""Append-only Revision 02 investigation and acceptance captures."""

import ctypes
from ctypes import wintypes
import datetime
import gzip
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from pe_runtime import ROOT, DUMPBIN, sha
from revise_phase00 import APPS, MSBUILD, process_windows, startup_probe

EVIDENCE = ROOT / "docs/build/evidence/phase-00/revision-02"
ENTRY = ROOT / ".codex/build/phase-00-revision-02-entry.json"
WORK = ROOT / "bin-int/build-phase-00-revision-02"
CDB = Path("C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe")
ARGS = ["--body-counts=50,100,200,500,1000,2000", "--warmup=2", "--samples=5", "--dt=0.008333333"]


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def allowed_paths():
    paths = {r["path"] for r in json.loads(ENTRY.read_text())["new_owned_files"]}
    for extension in (ROOT / ".codex/build").glob("phase-00-revision-02-ownership-*.json"):
        paths.update(r["path"] for r in json.loads(extension.read_text())["files"])
    return paths


def run(name, argv, cwd=ROOT, config=None, timeout=120, extra_env=None):
    output = EVIDENCE / "commands" / (name + ".txt.gz")
    metadata = output.with_name(name + ".json")
    for path in [output, metadata]:
        assert path.relative_to(ROOT).as_posix() in allowed_paths()
        assert not path.exists(), "Capture already exists: " + str(path)
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
    record = {"name": name, "revision": "02", "argv": [str(a) for a in argv],
              "command": subprocess.list2cmdline([str(a) for a in argv]), "cwd": str(cwd),
              "configuration": config, "started_utc": started, "elapsed_seconds": round(time.monotonic() - before, 3),
              "timeout_seconds": timeout, "timed_out": timed_out, "exit_code": code,
              "result": "INCOMPLETE_TIMEOUT" if timed_out else "PASS" if code == 0 else "FAIL",
              "environment_override": {"PYTHONDONTWRITEBYTECODE": "1", **(extra_env or {})},
              "raw_log": raw.relative_to(ROOT).as_posix(), "raw_sha256": sha(raw),
              "compressed_log": output.relative_to(ROOT).as_posix(), "compressed_sha256": sha(output),
              "revision_01_lineage": "f2094ad1bbddaa7cde782adcf2a1f0a28bd5249cd55dc8897589ef73b797230e"}
    write_json(metadata, record)
    print(json.dumps(record), flush=True)
    return code


def child_pids(parent):
    class ProcessEntry(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                    ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                    ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                    ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG),
                    ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32NextW.argtypes = kernel.Process32FirstW.argtypes
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    entry = ProcessEntry()
    entry.dwSize = ctypes.sizeof(entry)
    result = []
    ok = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
    while ok:
        if entry.th32ParentProcessID == parent:
            result.append(entry.th32ProcessID)
        ok = kernel.Process32NextW(snapshot, ctypes.byref(entry))
    kernel.CloseHandle(snapshot)
    return result


def debugger_probe(app):
    executable = ROOT / "bin/Debug" / app / (app + ".exe")
    dump = WORK / (app + "-baseline.dmp")
    assert not dump.exists()
    diagnostics = '.exr -1; kp; dv /t; r; u @rip-20 @rip+30; .dump /m ' + dump.as_posix() + '; q'
    command = 'sxe -c "' + diagnostics + '" av\n'
    if app == "GEngineEditor":
        command += 'sxe -c "' + diagnostics + '" bpe\n'
    command += 'g\n'
    script = WORK / (app + "-debugger-commands.txt")
    debugger_log = WORK / (app + "-debugger-output.txt")
    assert not script.exists() and not debugger_log.exists()
    script.write_text(command, encoding="ascii")
    argv = [CDB, "-lines", "-y", executable.parent, "-logo", debugger_log, "-cf", script, executable, *(ARGS if app == "PhysicsBenchmark" else [])]
    env = os.environ.copy()
    env["PATH"] = "C:\\Windows\\System32;C:\\Windows"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    print(json.dumps({"debugger_argv": [str(a) for a in argv], "debugger_sha256": sha(CDB), "exe_sha256": sha(executable), "PATH": env["PATH"]}), flush=True)
    process = subprocess.Popen([str(a) for a in argv], env=env, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    before = time.monotonic()
    close_sent = False
    while process.poll() is None and time.monotonic() - before < 180:
        time.sleep(1)
        if app == "Breakout" and not close_sent and time.monotonic() - before > 45:
            for pid in child_pids(process.pid):
                for window in process_windows(pid):
                    if window["class"] == "SDL_app":
                        user = ctypes.WinDLL("user32", use_last_error=True)
                        user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
                        sent = bool(user.PostMessageW(window["hwnd"], 0x10, 0, 0))
                        print(json.dumps({"normal_close_request": sent, "child_pid": pid, "window": window}), flush=True)
                        close_sent |= sent
    if process.poll() is None:
        process.kill()
        process.wait()
        if debugger_log.exists():
            print(debugger_log.read_text(errors="replace"), flush=True)
        return 2
    if debugger_log.exists():
        print(debugger_log.read_text(errors="replace"), flush=True)
    print(json.dumps({"debugger_exit_code": process.returncode, "dump_sha256": sha(dump) if dump.exists() else "ABSENT", "normal_close_sent": close_sent}), flush=True)
    return 0 if dump.exists() else 1


def main():
    action = sys.argv[1]
    if action == "_startup":
        return startup_probe(sys.argv[2], sys.argv[3])
    if action == "_debugger":
        return debugger_probe(sys.argv[2])
    if action == "debugger":
        app = sys.argv[2]
        name = "baseline-benchmark-Debug-cdb-retry" if app == "PhysicsBenchmark" else "baseline-Breakout-Debug-cdb-retry"
        if app == "GEngineEditor":
            name = "new-blocker-Editor-Debug-cdb"
        return run(name, [sys.executable, Path(__file__), "_debugger", app], cwd=ROOT if app == "PhysicsBenchmark" else ROOT / app, config="Debug", timeout=210)
    if action == "startup":
        config, app = sys.argv[2:4]
        return run(f"startup-{config}-{app}", [sys.executable, Path(__file__), "_startup", config, app], cwd=ROOT / app, config=config, timeout=90)
    if action == "imports":
        for config in ["Debug", "Release"]:
            for app in APPS:
                if run(f"imports-{config}-{app}", [DUMPBIN, "/imports", ROOT / "bin" / config / app / (app + ".exe")], config=config):
                    return 1
        return 0
    if action == "tests":
        config, selector = sys.argv[2:4]
        suffix = "-profile" if len(sys.argv) > 4 else ""
        return run(f"tests{suffix}-{config}-{selector}", [ROOT / "bin" / config / "PhysicsTests/PhysicsTests.exe", *([] if selector == "full" else ["--" + selector])], config=config, timeout=1800 if selector == "full" else 600)
    if action == "generate":
        mode = sys.argv[2]
        return run("premake-" + mode, [ROOT / "vendor/bin/premake/premake5.exe", *(["--physics-profiling"] if mode == "profile" else []), "vs2022"])
    if action == "build":
        config, mode = sys.argv[2:4]
        return run(f"build-{mode}-{config}", [MSBUILD, "GEngine.sln", "/m:2", "/nr:false", "/nologo", "/p:Configuration=" + config, "/p:Platform=x64", "/v:diag", "/t:Rebuild" if mode == "normal" else "/t:Build"], config=config, timeout=1800)
    if action == "benchmark":
        config, mode = sys.argv[2:4]
        args = ARGS + (["--steady-state-warmup-steps=4", "--steady-state-measured-steps=8"] if mode.endswith("-steady") else [])
        return run(f"benchmark-{config}-{mode}", [ROOT / "bin" / config / "PhysicsBenchmark/PhysicsBenchmark.exe", *args], config=config, timeout=1800)
    raise ValueError(action)


if __name__ == "__main__":
    sys.exit(main())
