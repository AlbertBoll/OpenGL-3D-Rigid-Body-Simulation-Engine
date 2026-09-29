"""Focused RBS setup/native-close and typed late shader-failure checks.

Consumes an exact Phase 03 manifest. No builds, asset edits, or other apps.
The reused native-close primitive targets only the launched process's window.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from test_frame_submission import application_smoke

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def local_path(value):
    path = Path(value)
    if not path.is_absolute():
        path = ROOT / path
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError("Path escapes PRE_EDITOR worktree: " + str(path))
    for parent in (path, *path.parents):
        if parent == ROOT.parent:
            break
        if parent.exists() and parent.lstat().st_file_attributes & 0x400:
            raise ValueError("Reparse point is not admitted: " + str(parent))
    return path


def run_case(case, output, layout):
    executable = local_path(case["executable"])
    directory = local_path(case["cwd"])
    asset_root = local_path(case["asset_root"])
    assert executable.name == "RigidBodySimulation.exe"
    assert digest(executable) == case["binary_sha256"]
    for name, sha in case["asset_hashes"].items():
        assert digest(local_path(asset_root / name)) == sha, name
    directory.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=False)
    for name in ("imgui.ini", "GEngine.log", "application.log", "smoke.json"):
        if (directory / name).exists():
            shutil.copy2(directory / name, output / ("before-" + name))
    (directory / "imgui.ini").write_bytes(layout.encode("utf-8"))
    env = {k: v for k, v in os.environ.items()
           if not k.upper().startswith(("GENGINE_", "SDL_", "ASAN_"))}
    env.update(GENGINE_ASSET_ROOT=str(asset_root), TMP=str(ROOT / "runtime/tmp"),
               TEMP=str(ROOT / "runtime/tmp"), PYTHONDONTWRITEBYTECODE="1")
    start = time.monotonic()
    if case["expected_exit"] == 0:
        record = application_smoke(executable, directory, env, case["runtime_modules"])
    else:
        record = {"command": [str(executable)], "cwd": str(directory),
                  "asset_root": str(asset_root), "binary_sha256": digest(executable),
                  "result": "FAIL"}
        with (directory / "application.log").open("wb") as stream:
            try:
                process = subprocess.run([str(executable)], cwd=directory, env=env,
                                         stdout=stream, stderr=subprocess.STDOUT,
                                         timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
                record["exit"] = process.returncode
                record["result"] = "PASS" if process.returncode == 1 else "FAIL"
            except subprocess.TimeoutExpired:
                record.update(exit=None, reason="60-second limit; owned child terminated")
    record["name"] = case["name"]
    # Archive each run before another process can truncate the logger/settings.
    for name in ("GEngine.log", "imgui.ini", "application.log", "shutdown-stack.log"):
        if (directory / name).exists():
            shutil.copy2(directory / name, output / name)
    text = (output / "application.log").read_text(encoding="utf-8", errors="replace")
    if (output / "GEngine.log").exists():
        text += (output / "GEngine.log").read_text(encoding="utf-8", errors="replace")
    record["required_markers"] = {m: m in text for m in case["required_markers"]}
    record["forbidden_markers"] = {m: m in text for m in case["forbidden_markers"]}
    if not all(record["required_markers"].values()) or any(record["forbidden_markers"].values()):
        record["result"] = "FAIL"
    record["elapsed_seconds"] = round(time.monotonic() - start, 3)
    for name, sha in case["asset_hashes"].items():
        assert digest(local_path(asset_root / name)) == sha, name
    (output / "result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()
    manifest_path = local_path(args.manifest)
    manifest = json.loads(manifest_path.read_bytes())
    assert manifest["phase"] == "03" and manifest["application"] == "RigidBodySimulation"
    for name, sha in manifest["input_hashes"].items():
        assert digest(local_path(name)) == sha, name
    report = {"manifest_sha256": digest(manifest_path), "result": "RUNNING", "cases": []}
    output = local_path(manifest["output"])
    output.mkdir(parents=True, exist_ok=False)
    for case in manifest["cases"]:
        record = run_case(case, local_path(output / case["name"]), manifest["layout"])
        report["cases"].append(record)
        report["result"] = "PASS" if all(c["result"] == "PASS" for c in report["cases"]) else "FAIL"
        (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{record['result']}: {record['name']}; exit={record['exit']}", flush=True)
        if record["result"] != "PASS":
            return 1
    for name, sha in manifest["input_hashes"].items():
        assert digest(local_path(name)) == sha, name
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
