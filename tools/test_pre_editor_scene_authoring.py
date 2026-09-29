"""Run the Phase 04 authoring cases inside RBS, once per admitted configuration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import time

from test_frame_submission import application_smoke

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
    assert manifest["phase"] == "04" and manifest["application"] == "RigidBodySimulation"
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
        env.update(GENGINE_ASSET_ROOT=str(assets), GENGINE_PRE_EDITOR_SCENE_AUTHORING="1",
                   TMP=str(ROOT / "runtime/tmp"), TEMP=str(ROOT / "runtime/tmp"),
                   PYTHONDONTWRITEBYTECODE="1")
        case_start = time.monotonic()
        record = application_smoke(executable, directory, env, case["runtime_modules"])
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
