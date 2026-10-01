"""Run the bounded Phase 07 functional checks inside RigidBodySimulation only."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
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
        raise ValueError("Input/output escapes PRE_EDITOR worktree: " + str(path))
    for parent in (path, *path.parents):
        if parent == ROOT.parent:
            break
        if parent.exists() and parent.lstat().st_file_attributes & 0x400:
            raise ValueError("Reparse output/input ancestor: " + str(parent))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    args = parser.parse_args()
    manifest_path = local(args.manifest)
    manifest = json.loads(manifest_path.read_bytes())
    assert manifest["phase"] == "07" and manifest["application"] == "RigidBodySimulation"
    assert manifest["protocol"]["repeats"] == 0
    assert len(manifest["cases"]) == 2
    assert {(c["configuration"], c["gallery_macro"]) for c in manifest["cases"]} == {("Debug", 1), ("Release", 0)}
    for name, sha in manifest["input_hashes"].items():
        assert digest(local(name)) == sha, name
    output = local(manifest["output"])
    output.mkdir(parents=True, exist_ok=False)
    report = {"phase": "07", "candidate": manifest["candidate"], "result": "RUNNING",
              "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "manifest_sha256": digest(manifest_path), "cases": []}
    started = time.monotonic()
    for case in manifest["cases"]:
        executable = local(case["executable"])
        directory = local(case["cwd"])
        assets = local(case["asset_root"])
        assert executable.name == "RigidBodySimulation.exe"
        assert digest(executable) == case["binary_sha256"]
        for name, sha in case["asset_hashes"].items():
            assert digest(local(assets / name)) == sha, name
        for module in case["runtime_modules"].values():
            assert digest(local(module["path"])) == module["sha256"]
        directory.mkdir(parents=True, exist_ok=True)
        archive = local(output / case["name"])
        archive.mkdir(exist_ok=False)
        for name in ("imgui.ini", "GEngine.log", "application.log", "smoke.json", "shutdown-stack.log"):
            if (directory / name).exists():
                shutil.copy2(directory / name, archive / ("before-" + name))
        (directory / "imgui.ini").write_bytes(manifest["layout"].encode("utf-8"))
        env = {k: v for k, v in os.environ.items()
               if not k.upper().startswith(("GENGINE_", "SDL_", "ASAN_"))}
        for key in list(env):
            if key.lower() == "path":
                parts = [v for v in env[key].split(os.pathsep)
                         if "gengine-rendering" not in v.lower()]
                env[key] = str(executable.parent) + os.pathsep + os.pathsep.join(parts)
        env.update(GENGINE_ASSET_ROOT=str(assets), GENGINE_PRE_EDITOR_PARAMETRIC_GEOMETRY="1",
                   TMP=str(local("runtime/tmp")), TEMP=str(local("runtime/tmp")),
                   PYTHONDONTWRITEBYTECODE="1")
        local("runtime/tmp").mkdir(exist_ok=True, parents=True)
        case_start = time.monotonic()
        record = application_smoke(executable, directory, env, case["runtime_modules"], case["required_markers"])
        for name in ("GEngine.log", "application.log", "imgui.ini", "smoke.json", "shutdown-stack.log"):
            if (directory / name).exists():
                shutil.copy2(directory / name, archive / name)
        observed = ""
        for name in ("GEngine.log", "application.log"):
            if (archive / name).exists():
                observed += (archive / name).read_text(encoding="utf-8", errors="replace")
        record.update(name=case["name"], configuration=case["configuration"],
                      gallery_macro=case["gallery_macro"],
                      elapsed_seconds=round(time.monotonic() - case_start, 3))
        record["required_markers"] = {m: m in observed for m in case["required_markers"]}
        record["forbidden_markers"] = {m: m in observed for m in case["forbidden_markers"]}
        record["inputs_unchanged"] = all(digest(local(n)) == h for n, h in manifest["input_hashes"].items())
        record["inputs_unchanged"] &= all(digest(local(assets / n)) == h for n, h in case["asset_hashes"].items())
        record["inputs_unchanged"] &= digest(executable) == case["binary_sha256"]
        if (not all(record["required_markers"].values()) or any(record["forbidden_markers"].values())
                or not record["inputs_unchanged"]
                or record["elapsed_seconds"] > manifest["protocol"]["per_process_cap_seconds"]):
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
