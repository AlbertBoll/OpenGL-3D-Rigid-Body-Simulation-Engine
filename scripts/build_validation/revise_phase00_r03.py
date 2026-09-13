"""Append-only Revision 03 investigation and acceptance captures."""

import ctypes
import contextlib
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
from revise_phase00 import APPS, MSBUILD, process_windows, process_modules

EVIDENCE = ROOT / "docs/build/evidence/phase-00/revision-03"
ENTRY = ROOT / ".codex/build/phase-00-revision-03-entry.json"
WORK = ROOT / "bin-int/build-phase-00-revision-03"
CDB = Path("C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe")
ARGS = ["--body-counts=50,100,200,500,1000,2000", "--warmup=2", "--samples=5", "--dt=0.008333333"]
LAYOUT_SHA = "d6facac12733ba8dd41ace25b4f972ece052128c3cf9afce933a46fdec266eb7"


@contextlib.contextmanager
def protected_layout(name, app):
    path = ROOT / app / "imgui.ini"
    relative = path.relative_to(ROOT).as_posix()
    protected = {r["path"]: r["sha256"] for r in json.loads(ENTRY.read_text())["protected"]}
    if relative not in protected:
        yield
        return
    expected = protected[relative]
    if app == "GEngineEditor":
        assert expected == LAYOUT_SHA
    before = path.read_bytes()
    assert sha(path) == expected, "Layout differs from verified checkpoint"
    print(json.dumps({"protected_layout_before": expected}), flush=True)
    try:
        yield
    finally:
        generated = path.read_bytes() if path.exists() else b""
        capture = WORK / (name + "-generated-imgui.ini")
        assert not capture.exists()
        capture.write_bytes(generated)
        changed = generated != before
        if changed:
            path.write_bytes(before)
        assert sha(path) == expected
        print(json.dumps({"protected_layout_after": sha(path), "layout_changed_during_run": changed,
                          "generated_layout": str(capture), "generated_layout_sha256": sha(capture)}), flush=True)


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def allowed_paths():
    paths = {r["path"] for r in json.loads(ENTRY.read_text())["new_owned_files"]}
    for extension in (ROOT / ".codex/build").glob("phase-00-revision-03-ownership-*.json"):
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
    helper = Path(__file__)
    helper_hash = sha(helper)
    retained_helper = WORK / ("harness-" + helper_hash + ".py")
    if not retained_helper.exists():
        retained_helper.write_bytes(helper.read_bytes())
    assert sha(retained_helper) == helper_hash
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
    record = {"name": name, "revision": "03", "argv": [str(a) for a in argv],
              "command": subprocess.list2cmdline([str(a) for a in argv]), "cwd": str(cwd),
              "configuration": config, "started_utc": started, "elapsed_seconds": round(time.monotonic() - before, 3),
              "harness_sha256": helper_hash, "retained_harness": str(retained_helper),
              "timeout_seconds": timeout, "timed_out": timed_out, "exit_code": code,
              "result": "INCOMPLETE_TIMEOUT" if timed_out else "PASS" if code == 0 else "FAIL",
              "environment_override": {"PYTHONDONTWRITEBYTECODE": "1", **(extra_env or {})},
              "raw_log": raw.relative_to(ROOT).as_posix(), "raw_sha256": sha(raw),
              "compressed_log": output.relative_to(ROOT).as_posix(), "compressed_sha256": sha(output),
              "revision_02_lineage": "42e372560f4f0c2b650ba99ad735743a0c57e5fac3ac11aeadec89af74440885"}
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


def debugger_probe(name, close_seconds, config="Debug", app="GEngineEditor"):
    executable = ROOT / "bin" / config / app / (app + ".exe")
    dump = WORK / (name + ".dmp")
    assert not dump.exists()
    diagnostics = '.exr -1; kp; dv /t; .frame 0; r; .dump /m ' + dump.as_posix() + '; q'
    command = 'sxe -c "' + diagnostics + '" av\n'
    command += 'sxe -c "' + diagnostics + '" 0xc0000409\n'
    if app == "GEngineEditor":
        command += 'sxe -c "' + diagnostics + '" bpe\n'
    if name == "editor-camera-ownership":
        camera_script = WORK / (name + "-camera.txt")
        assert not camera_script.exists()
        camera_script.write_text('.echo SCENEAPP_CAMERA_BEFORE_DERIVED_DELETE\n?? ((SceneApp*)@rcx)->m_EditorCamera\ngc\n', encoding="ascii")
        command += 'bu GEngineEditor!SceneApp::~SceneApp "$$><' + camera_script.as_posix() + '"\n'
    fixed = name.startswith(("editor-fixed-", "resources-"))
    if name.startswith("resources-"):
        return_script = WORK / (name + "-file-return.txt")
        assert not return_script.exists()
        return_script.write_text('.printf "RESOURCE_END tid=%x handle=%p\\n", @$tid, @rax\ngc\n', encoding="ascii")
        for api, fmt in [("CreateFileW", "%mu"), ("CreateFileA", "%ma")]:
            entry_script = WORK / (name + "-" + api + ".txt")
            assert not entry_script.exists()
            entry_script.write_text('.printf "RESOURCE_BEGIN tid=%x access=%x path=' + fmt + '\\n", @$tid, @rdx, @rcx\n'
                                    + 'bp /1 @$ra "$$><' + return_script.as_posix() + '"\ngc\n', encoding="ascii")
            command += 'bu kernelbase!' + api + ' "$$><' + entry_script.as_posix() + '"\n'
    command += 'sxe -c ".lastevent; q" epr\n'
    command += 'g\n'
    script = WORK / (name + "-debugger-commands.txt")
    debugger_log = WORK / (name + "-debugger-output.txt")
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
    while process.poll() is None and time.monotonic() - before < close_seconds + 120:
        time.sleep(1)
        if not close_sent and time.monotonic() - before > close_seconds:
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
    if fixed:
        text = debugger_log.read_text(errors="replace")
        import re
        clean = bool(re.search(r"Exit process .*?, code 0\s*$", text, re.M))
        return 0 if close_sent and not dump.exists() and clean else 1
    return 0 if dump.exists() else 1


def startup_probe(config, app, name):
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    executable = ROOT / "bin" / config / app / (app + ".exe")
    assert Path.cwd() == ROOT / app
    env = os.environ.copy()
    env["PATH"] = "C:\\Windows\\System32;C:\\Windows"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    seconds = 120 if app == "GEngineEditor" else 60
    print(json.dumps({"exe": str(executable), "exe_sha256": sha(executable), "cwd": str(Path.cwd()), "PATH": env["PATH"], "smoke_seconds": seconds}), flush=True)
    process = subprocess.Popen([str(executable)], env=env, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    observations = []
    before = time.monotonic()
    while process.poll() is None and time.monotonic() - before < seconds:
        time.sleep(1)
        sample = {"elapsed_seconds": round(time.monotonic() - before, 2), "windows": process_windows(process.pid)}
        if len(observations) in (0, 14, 44, 99):
            sample["modules"] = process_modules(process)
        observations.append(sample)
        print(json.dumps(sample), flush=True)
    alive = process.poll() is None
    if alive:
        user = ctypes.WinDLL("user32", use_last_error=True)
        user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        for w in process_windows(process.pid):
            if w["class"] == "SDL_app":
                print(json.dumps({"normal_close_request": bool(user.PostMessageW(w["hwnd"], 0x10, 0, 0))}), flush=True)
    forced = False
    try:
        code = process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        forced = True
        process.kill()
        code = process.wait()
    created = any(w["class"] == "SDL_app" for s in observations for w in s["windows"])
    dialogs = [w for s in observations for w in s["windows"] if any(x in (w["title"] + " ".join(w["child_text"])).lower() for x in ["assertion failed", "runtime error", "debug error"])]
    log = ROOT / app / "GEngine.log"
    result = {"alive_for_smoke": alive, "SDL_window_created": created, "diagnostic_dialogs": dialogs, "exit_code": code, "exit_hex": hex(code & 0xffffffff), "forced_termination": forced,
              "application_log": {"sha256": sha(log), "text": log.read_text(errors="replace")} if log.exists() else None}
    print(json.dumps(result), flush=True)
    return 0 if alive and created and not dialogs and code == 0 and not forced else 1


def main():
    action = sys.argv[1]
    if action == "_startup":
        config, app = sys.argv[2:4]
        name = sys.argv[4] if len(sys.argv) > 4 else f"startup-{config}-{app}"
        with protected_layout(name, app):
            return startup_probe(config, app, name)
    if action == "_debugger":
        name, seconds, config = sys.argv[2:5]
        app = sys.argv[5] if len(sys.argv) > 5 else "GEngineEditor"
        with protected_layout(name, app):
            return debugger_probe(name, float(seconds), config, app)
    if action == "_resources":
        config, app = sys.argv[2:4]
        name = sys.argv[4] if len(sys.argv) > 4 else f"resources-{config}-{app}"
        with protected_layout(name, app):
            return debugger_probe(name, 120 if app == "GEngineEditor" else 60, config, app)
    if action == "resources":
        config, app = sys.argv[2:4]
        name = sys.argv[4] if len(sys.argv) > 4 else f"resources-{config}-{app}"
        return run(name, [sys.executable, Path(__file__), "_resources", config, app, name], cwd=ROOT / app, config=config, timeout=300)
    if action == "debugger":
        name, seconds = sys.argv[2:4]
        config = sys.argv[4] if len(sys.argv) > 4 else "Debug"
        return run(name, [sys.executable, Path(__file__), "_debugger", name, seconds, config], cwd=ROOT / "GEngineEditor", config=config, timeout=float(seconds) + 150)
    if action == "startup":
        config, app = sys.argv[2:4]
        return run(f"startup-{config}-{app}", [sys.executable, Path(__file__), "_startup", config, app], cwd=ROOT / app, config=config, timeout=180)
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
