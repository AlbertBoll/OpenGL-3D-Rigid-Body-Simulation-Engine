"""Capture the unchanged Premake reference; never stage or repair source files."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "docs/build/evidence/phase-00"
ENTRY = ROOT / ".codex/build/phase-00-entry.json"
VS = Path("C:/Program Files/Microsoft Visual Studio/2022/Community")
MSBUILD = VS / "MSBuild/Current/Bin/MSBuild.exe"
GIT = ["git", "-c", "safe.directory=C:/dev/GEngine-build"]


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def git(*args):
    return subprocess.run(GIT + list(args), cwd=ROOT, capture_output=True, check=True).stdout.decode("utf-8")


def run(name, argv, cwd=ROOT, config=None, timeout=None):
    entry = json.loads(ENTRY.read_text(encoding="utf-8"))
    log = EVIDENCE / (name + ".txt")
    assert log.relative_to(ROOT).as_posix() in entry["exact_owned_paths"]
    assert not log.exists(), "Do not overwrite recorded validation"
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    before = time.monotonic()
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    timed_out = False
    with log.open("wb") as output:
        process = subprocess.Popen([str(a) for a in argv], cwd=cwd, env=env,
                                   stdout=output, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            code = process.wait()
            timed_out = True
    record = {"name": name, "argv": [str(a) for a in argv],
              "command": subprocess.list2cmdline([str(a) for a in argv]),
              "cwd": str(cwd), "configuration": config, "started_utc": started,
              "elapsed_seconds": round(time.monotonic() - before, 3),
              "exit_code": code, "timed_out": timed_out,
              "result": "TIMEOUT" if timed_out else "PASS" if code == 0 else "FAIL",
              "log": log.relative_to(ROOT).as_posix(), "log_sha256": sha(log),
              "environment": "environment.json; PYTHONDONTWRITEBYTECODE=1"}
    records_path = EVIDENCE / "commands.json"
    records = json.loads(records_path.read_text()) if records_path.exists() else []
    records.append(record)
    write_json(records_path, records)
    print(json.dumps(record), flush=True)
    return code


def initialize():
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    entry = json.loads(ENTRY.read_text(encoding="utf-8"))
    write_json(EVIDENCE / "entry-inventory.json", entry)
    (EVIDENCE / "remote-refs.txt").write_text(entry["remote_verification"]["stdout"], encoding="utf-8")
    vswhere = Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
    tools = [Path(sys.executable), MSBUILD, vswhere, ROOT / "vendor/bin/premake/premake5.exe"]
    for version in sorted((VS / "VC/Tools/MSVC").iterdir()):
        tools.extend(version / "bin/Hostx64/x64" / name for name in ["cl.exe", "link.exe", "lib.exe", "dumpbin.exe"])
    versions = {}
    for key, argv in {"python": [sys.executable, "--version"], "git": GIT + ["--version"],
                      "msbuild": [str(MSBUILD), "-version", "-nologo"],
                      "premake": [str(ROOT / "vendor/bin/premake/premake5.exe"), "--version"],
                      "vswhere": [str(vswhere), "-all", "-products", "*", "-format", "json"]}.items():
        result = subprocess.run(argv, capture_output=True, text=True)
        versions[key] = {"argv": argv, "exit_code": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr}
    write_json(EVIDENCE / "environment.json", {
        "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "platform": platform.platform(), "machine": platform.machine(),
        "python": sys.version, "versions": versions,
        "tools": [{"path": str(p), "sha256": sha(p) if p.is_file() else "ABSENT"} for p in tools],
        "windows_sdk_include_versions": [p.name for p in Path("C:/Program Files (x86)/Windows Kits/10/Include").iterdir()],
        "environment": {key: os.environ.get(key) for key in ["PATH", "INCLUDE", "LIB", "LIBPATH", "CL", "_CL_", "LINK", "WindowsSdkDir", "WindowsSDKVersion", "VCToolsInstallDir", "VSINSTALLDIR", "TEMP", "TMP"]},
        "build_environment_override": {"PYTHONDONTWRITEBYTECODE": "1"},
        "selection": "Explicit VS2022 MSBuild; no vswhere -latest and no package/tool installation."})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["initialize", "generate", "build"])
    parser.add_argument("--config", choices=["Debug", "Release"])
    parser.add_argument("--profiling", action="store_true")
    args = parser.parse_args()
    if args.action == "initialize":
        initialize()
        return 0
    if args.action == "generate":
        options = ["--physics-profiling"] if args.profiling else []
        return run("premake-profile" if args.profiling else "premake-normal",
                   [ROOT / "vendor/bin/premake/premake5.exe", *options, "vs2022"])
    assert args.config
    name = "build-" + ("profile-" if args.profiling else "") + args.config
    # Fresh entry contains no compiled outputs. Build (not Clean/Rebuild) preserves tracked DLLs.
    return run(name, [MSBUILD, "GEngine.sln", "/m:2", "/nr:false", "/nologo",
                      "/t:GEngine,PhysicsBenchmark" if args.profiling else "/t:Build",
                      "/p:Configuration=" + args.config, "/p:Platform=x64", "/v:diag"],
               config=args.config)


if __name__ == "__main__":
    sys.exit(main())
