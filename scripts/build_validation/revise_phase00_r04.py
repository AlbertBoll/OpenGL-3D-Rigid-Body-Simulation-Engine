"""Append-only B00-RUNTIME-010 reproduction; never edits application/build inputs."""

import concurrent.futures
import contextlib
import ctypes
import datetime
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

import revise_phase00_r03 as prior
from pe_runtime import ROOT, sha

EVIDENCE = ROOT / "docs/build/evidence/phase-00/revision-04"
WORK = ROOT / "bin-int/build-phase-00-revision-04"
ENTRY = ROOT / ".codex/build/phase-00-revision-04-entry.json"
prior.WORK = WORK


@contextlib.contextmanager
def protected_layout(name, app):
    # Preserve ignored layouts too; no two probes for one app may share its source CWD.
    path = ROOT / app / "imgui.ini"
    expected = {r["path"]: r["sha256"] for r in json.loads(ENTRY.read_text())["entry_files"]}
    before = path.read_bytes() if path.exists() else None
    actual = sha(path) if path.exists() else "ABSENT"
    assert actual == expected.get(path.relative_to(ROOT).as_posix(), "ABSENT")
    print(json.dumps({"protected_layout_before": actual}), flush=True)
    try:
        yield
    finally:
        generated = path.read_bytes() if path.exists() else b""
        capture = WORK / (name + "-generated-imgui.ini")
        assert not capture.exists()
        capture.write_bytes(generated)
        if before is not None and generated != before:
            path.write_bytes(before)
        elif before is None and path.exists():
            assert path.resolve().parent == (ROOT / app).resolve() and path.name == "imgui.ini"
            path.unlink()
        print(json.dumps({"protected_layout_after": sha(path) if path.exists() else "ABSENT",
                          "generated_layout_sha256": sha(capture), "generated_layout": str(capture)}), flush=True)


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def register(paths):
    path = EVIDENCE / "lineage.json"
    data = json.loads(path.read_text()) if path.exists() else {
        "revision": "04", "entry_sha256": sha(ENTRY),
        "previous_seal_sha256": sha(EVIDENCE / "revision-03-snapshot.json"),
        "previous_review_sha256": sha(EVIDENCE / "revision-03-review.md"),
        "additional_exact_ownership": []}
    known = {r["path"] for r in data["additional_exact_ownership"]}
    for p in paths:
        name = p.relative_to(ROOT).as_posix()
        if name not in known:
            assert not p.exists(), name
            data["additional_exact_ownership"].append({"path": name, "entry_exists": False,
                "classification": "PHASE_OWNED_NEW_TRACKED", "purpose": "Revision 04 investigation capture"})
    write(path, data)


def run(name, mode, app="RayTracing", delay=0):
    time.sleep(delay)
    raw = WORK / (name + ".txt")
    compressed = EVIDENCE / "commands" / (name + ".txt.gz")
    metadata = compressed.with_name(name + ".json")
    assert not raw.exists() and not compressed.exists() and not metadata.exists()
    retained = WORK / ("harness-" + sha(Path(__file__)) + ".py")
    if not retained.exists():
        retained.write_bytes(Path(__file__).read_bytes())
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    argv = [sys.executable, "-B", str(Path(__file__)), "_" + mode, name, app]
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    before = time.monotonic()
    with raw.open("xb") as stream:
        process = subprocess.Popen(argv, cwd=ROOT / app, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        print(json.dumps({"started": name, "pid": process.pid, "utc": started}), flush=True)
        # Child owns normal close and bounded cleanup. Do not strand a debuggee by killing its debugger.
        code = process.wait()
    compressed.parent.mkdir(exist_ok=True)
    compressed.write_bytes(gzip.compress(raw.read_bytes(), mtime=0))
    samples = []
    for line in raw.read_text(errors="replace").splitlines():
        try:
            samples.append(json.loads(line))
        except ValueError:
            pass
    results = [r for r in samples if "exit_hex" in r or "debugger_result" in r]
    record = {"name": name, "revision": "04", "mode": mode, "app": app,
              "argv": argv, "cwd": str(ROOT / app), "configuration": "Release",
              "started_utc": started, "elapsed_seconds": round(time.monotonic() - before, 3),
              "exit_code": code, "result": "PASS" if code == 0 else "FAIL",
              "process_result": results, "raw_log": raw.relative_to(ROOT).as_posix(), "raw_sha256": sha(raw),
              "compressed_log": compressed.relative_to(ROOT).as_posix(), "compressed_sha256": sha(compressed),
              "harness_sha256": sha(Path(__file__)), "retained_harness": str(retained),
              "unchanged_startup_implementation": {"path": "scripts/build_validation/revise_phase00_r03.py", "sha256": sha(Path(prior.__file__))},
              "environment_override": {"PYTHONDONTWRITEBYTECODE": "1", "PATH": "C:\\Windows\\System32;C:\\Windows"},
              "environment_identity_sha256": hashlib.sha256(json.dumps(sorted(env.items())).encode()).hexdigest()}
    write(metadata, record)
    print(json.dumps({"completed": name, "result": record["result"], "process_result": results}), flush=True)
    return record


def debugger(name, app):
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    executable = ROOT / "bin/Release" / app / (app + ".exe")
    dump = WORK / (name + ".dmp")
    script = WORK / (name + "-cdb.txt")
    log = WORK / (name + "-cdb-output.txt")
    for p in (dump, script, log):
        assert not p.exists()
    diagnostic = '.echo R04_EXCEPTION_CAPTURE; .lastevent; .exr -1; .ecxr; r; kv; lm v; ~* kv; !runaway; !peb; .dump /ma ' + dump.as_posix() + '; q'
    script.write_text('sxe -c "' + diagnostic + '" 0xc0000409\n'
                      'sxe -c "' + diagnostic + '" av\n'
                      'sxe -c ".lastevent; q" epr\ng\n', encoding="ascii")
    argv = [str(prior.CDB), "-hd", "-lines", "-y", str(executable.parent), "-logo", str(log), "-cf", str(script), str(executable)]
    env = os.environ.copy()
    env["PATH"] = "C:\\Windows\\System32;C:\\Windows"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    print(json.dumps({"debugger_argv": argv, "debugger_sha256": sha(prior.CDB), "exe_sha256": sha(executable),
                      "cwd": str(Path.cwd()), "PATH": env["PATH"], "heap_policy": "-hd: ordinary heap; no page heap or verifier"}), flush=True)
    process = subprocess.Popen(argv, env=env, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    before = time.monotonic()
    close_sent = False
    children = []
    while process.poll() is None and time.monotonic() - before < 120:
        time.sleep(1)
        children = prior.child_pids(process.pid) or children
        if not close_sent and time.monotonic() - before >= 60:
            for pid in children:
                for window in prior.process_windows(pid):
                    if window["class"] == "SDL_app":
                        user = ctypes.WinDLL("user32", use_last_error=True)
                        user.PostMessageW.argtypes = [prior.wintypes.HWND, prior.wintypes.UINT, prior.wintypes.WPARAM, prior.wintypes.LPARAM]
                        sent = bool(user.PostMessageW(window["hwnd"], 0x10, 0, 0))
                        print(json.dumps({"normal_close_request": sent, "elapsed_seconds": time.monotonic()-before, "pid": pid}), flush=True)
                        close_sent |= sent
    timed_out = process.poll() is None
    if timed_out:
        # q is CDB's normal quit command and terminates only this launched debuggee.
        process.kill()
        process.wait()
        for pid in children:
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.OpenProcess.restype = prior.wintypes.HANDLE
            kernel.TerminateProcess.argtypes = [prior.wintypes.HANDLE, prior.wintypes.UINT]
            kernel.CloseHandle.argtypes = [prior.wintypes.HANDLE]
            handle = kernel.OpenProcess(1, False, pid)
            if handle:
                kernel.TerminateProcess(handle, 0xDEAD)
                kernel.CloseHandle(handle)
    text = log.read_text(errors="replace") if log.exists() else ""
    print(text, flush=True)
    import re
    clean = bool(re.search(r"Exit process .*?, code 0\s*$", text, re.M))
    modules = []
    for module in re.findall(r"Image path:\s*(.+)", text):
        path = Path(module.strip())
        modules.append({"path": str(path), "sha256": sha(path) if path.is_file() else "UNAVAILABLE"})
    result = {"debugger_result": "EXCEPTION_CAPTURED" if dump.exists() else "CLEAN_EXIT" if clean else "INCOMPLETE",
              "debugger_exit_code": process.returncode, "normal_close_sent": close_sent, "forced_termination": timed_out,
              "dump": str(dump), "dump_sha256": sha(dump) if dump.exists() else "ABSENT", "module_identities": modules}
    print(json.dumps(result), flush=True)
    return 0 if close_sent and clean and not dump.exists() and not timed_out else 1


def batch(kind, count, first=1):
    # Freeze exact output ownership before any concurrent process writes.
    plans = []
    for i in range(first, first + count):
        apps = [("RayTracing", 0)]
        if "overlap" in kind:
            apps += [("Breakout", .219), ("RigidBodySimulation", .363)]
        plans.append([(f"{kind}-{i:02d}-{app}",
                       "native" if kind.startswith("native") and app == "RayTracing" else
                       "debugger" if kind.startswith("debugger") and app == "RayTracing" else "startup", app, delay) for app, delay in apps])
    register([EVIDENCE / "commands" / (p[0] + ext) for group in plans for p in group for ext in (".json", ".txt.gz")])
    (EVIDENCE / "commands").mkdir(exist_ok=True)
    for group in plans:
        # All overlap is intentional and belongs to this experiment. Iterations remain serial.
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(group)) as pool:
            records = [f.result() for f in [pool.submit(run, *p) for p in group]]
        if any(r["result"] != "PASS" for r in records):
            print("BATCH_STOPPED_AFTER_NONPASS; retain every raw record", flush=True)
            break


def acceptance():
    """Graphics acceptance is exclusive; deliberate overlap exists only in diagnostics."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, prior.wintypes.BOOL, prior.wintypes.LPCWSTR]
    kernel.CreateMutexW.restype = prior.wintypes.HANDLE
    kernel.ReleaseMutex.argtypes = [prior.wintypes.HANDLE]
    kernel.CloseHandle.argtypes = [prior.wintypes.HANDLE]
    handle = kernel.CreateMutexW(None, True, "Local\\GEngineBuildPhase00GraphicsAcceptance")
    assert handle, ctypes.get_last_error()
    if ctypes.get_last_error() == 183:
        kernel.CloseHandle(handle)
        raise RuntimeError("Another graphics acceptance harness holds the gate")
    try:
        plans = [(f"acceptance-{i:02d}-{app}", app) for i, app in enumerate(
            ["RayTracing", "Breakout", "RigidBodySimulation", "RayTracing"], 1)]
        register([EVIDENCE / "commands" / (name + ext) for name, app in plans for ext in (".json", ".txt.gz")])
        for name, app in plans:
            # Existing foreign probes are a precondition failure, never processes to kill.
            check = subprocess.run(["C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe", "-NoProfile", "-Command",
                "$p = @(Get-Process RayTracing,Breakout,RigidBodySimulation,GEngineEditor,cdb -ErrorAction SilentlyContinue); if ($p.Count) { $p | Select-Object ProcessName,Id | ConvertTo-Json; exit 1 }; exit 0"],
                capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
            assert check.returncode == 0, "Concurrent graphics/debugger process: " + check.stdout
            record = run(name, "startup", app)
            if record["result"] != "PASS":
                raise RuntimeError("Mandatory serial acceptance failed: " + name)
    finally:
        kernel.ReleaseMutex(handle)
        kernel.CloseHandle(handle)


if __name__ == "__main__":
    action = sys.argv[1]
    if action == "acceptance":
        acceptance()
        sys.exit(0)
    if action.startswith("_"):
        name, app = sys.argv[2:4]
        with protected_layout(name, app):
            if action == "_native":
                import native_debug_phase00_r04
                sys.exit(native_debug_phase00_r04.probe(name, app))
            sys.exit(prior.startup_probe("Release", app, name) if action == "_startup" else debugger(name, app))
    batch(action, int(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 1)
