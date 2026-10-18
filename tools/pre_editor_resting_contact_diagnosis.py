"""Phase 19's accepted, bounded RBS diagnostic runner. No standalone Physics run."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import time
import statistics
from collections import Counter, defaultdict
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "logs/pre_editor/phase-19"
ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_19_OD06_SCOPE_ACCEPTANCE.json"
CONTINUATION = ROOT / "docs/pre_editor/active/PHASE_19_DIAGNOSTIC_CONTINUATION_AMENDMENT_01_ACCEPTANCE.json"
MSBUILD = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe")
DURATIONS = (1800, 600, 600, 240, 240, 240, 240, 600, 2400)
BODY_COUNTS = (185, 2, 2, 2, 2, 2, 2, 2, 5)
NAMES = ("lattice", "ground", "friction", "impact-0.5", "impact-1.0", "impact-1.5", "off-center", "spin", "stack")


def now():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def contained(path):
    path = Path(path).absolute()
    if not path.resolve().is_relative_to(ROOT):
        raise RuntimeError(f"output escapes PRE_EDITOR: {path}")
    for ancestor in [path, *path.parents]:
        if ancestor.exists() and getattr(ancestor.lstat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise RuntimeError(f"reparse output: {ancestor}")
        if ancestor == ROOT:
            break
    return path


def write_new(path, value):
    path = contained(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as f:
        json.dump(value, f, indent=2)
        f.write("\n")


def git(*args):
    return subprocess.check_output(["git", "-c", f"safe.directory={ROOT.as_posix()}", *args], cwd=ROOT,
                                   env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"})


def authority():
    a = json.loads(ACCEPTANCE.read_text())
    assert sha(ROOT / a["proposal"]["path"]) == a["proposal"]["sha256"]
    assert a["status"] == "ACCEPTED_FOR_PHASE19_DIAGNOSTIC_ENTRY_ONLY"
    return a


def owned():
    a = authority()
    return a["production_files"] + a["supporting_files"]


def continuation():
    a = json.loads(CONTINUATION.read_text())
    assert a["status"] == "ACCEPTED_FOR_EXISTING_PHASE19_START_ONLY"
    assert sha(ROOT / a["proposal"]["path"]) == a["proposal"]["sha256"]
    assert sha(ROOT / a["patch"]["path"]) == a["patch"]["sha256"]
    assert (a["native_launch_limit"], a["native_process_seconds"], a["native_total_seconds"]) == (5, 300, 1200)
    assert (a["build_command_seconds"], a["build_total_seconds"]) == (1200, 2400)
    return a


def campaign_runs():
    """Count every launch; only the explicitly pinned historical failure is excluded from PASS selection."""
    a = continuation()
    historical = ROOT / a["preserved_failure"]["path"]
    assert historical == EVIDENCE / "runs/Debug-off-01/result.json"
    assert sha(historical) == a["preserved_failure"]["sha256"]
    assert sha(historical.with_name("invocation.json")) == a["preserved_failure"]["invocation_sha256"]
    failed = json.loads(historical.read_text())
    assert failed["status"] == "FAIL" and failed["wall_seconds"] == a["preserved_failure"]["wall_seconds"]
    records = sorted((EVIDENCE / "runs").glob("*/invocation.json"),
                     key=lambda p: json.loads(p.read_text())["native_launch_number"])
    assert records and records[0] == historical.with_name("invocation.json")
    assert len(records) <= a["native_launch_limit"]
    sequence = [("Debug", "off"), ("Debug", "on"), ("Release", "off"), ("Release", "on")]
    successful = {}
    inputs = source_identity()
    for number, p in enumerate(records, 1):
        inv = json.loads(p.read_text())
        assert inv["native_launch_number"] == number, "unknown or duplicate native attempt"
        if number == 1:
            continue
        result_path = p.with_name("result.json")
        if not result_path.exists() or json.loads(result_path.read_text())["status"] != "PASS":
            raise RuntimeError("additional native attempt failed/incomplete; dependent run stopped")
        key = (inv["configuration"], inv["observation"])
        assert key == sequence[number - 2] and inv["inputs"] == inputs
        successful[key] = result_path
    return records, successful


def source_identity():
    paths = owned() + ["RigidBodySimulation/src/RigidBodySimulation.cpp", "GEngine/GEngine.vcxproj",
                       "RigidBodySimulation/RigidBodySimulation.vcxproj", "external/glad/glad.vcxproj",
                       "premake5.lua", "RigidBodySimulation/postbuild.py", "tools/postbuild.py"]
    return {p: sha(ROOT / p) for p in paths}


def guards():
    a = authority()
    assert git("rev-parse", "HEAD").decode().strip() == a["baseline"]
    assert not git("diff", "--cached", "--name-only").strip()
    assert not git("ls-files", "--unmerged").strip()
    changes = set(git("diff", "--name-only").decode().splitlines())
    assert changes <= set(owned()), changes - set(owned())
    entry = json.loads((EVIDENCE / "entry-01.json").read_text())
    canonical = "RigidBodySimulation/src/RigidBodySimulation.cpp"
    assert sha(ROOT / canonical) == entry["canonical_rbs"]["worktree"]["sha256"]
    for p, v in entry["protected_preexisting_untracked"].items():
        assert sha(ROOT / p) == v["sha256"], p
    assert sha(ROOT / "tools/pre_editor_resting_contact_lattice.txt") == entry["historical_input"]["sha256"]


def fresh_attempt(kind, label):
    parent = contained(EVIDENCE / kind)
    parent.mkdir(exist_ok=True)
    for number in range(1, 10000):
        p = parent / f"{label}-{number:02d}"
        if not p.exists():
            p.mkdir()
            return p
    raise RuntimeError("attempt destination unavailable")


def environment():
    env = dict(os.environ)
    for key in ("GENGINE_ASSET_ROOT", "GENGINE_BASELINE_OUTPUT", "CL", "_CL_"):
        env.pop(key, None)
    temporary = contained(ROOT / "runtime/tmp/phase-19")
    temporary.mkdir(parents=True, exist_ok=True)
    env["TMP"] = env["TEMP"] = str(temporary)
    return env


def usage(kind):
    return sum(json.loads(p.read_text()).get("wall_seconds", 0) for p in (EVIDENCE / kind).glob("*/result.json"))


def build(configuration):
    guards()
    remaining = authority()["build_total_seconds"] - usage("builds")
    if remaining <= 0:
        raise RuntimeError("accepted build wall budget exhausted")
    output = fresh_attempt("builds", configuration)
    inputs = source_identity()
    # Freeze the exact candidate, including failed-build candidates, before compiling.
    for p in owned():
        dest = contained(output / "source" / p)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / p, dest)
    command = [str(MSBUILD), str(ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"),
               "/t:Build", f"/p:Configuration={configuration}", "/p:Platform=x64", "/m:2", "/nr:false", "/v:minimal"]
    limit = min(1200, remaining)
    write_new(output / "invocation.json", {"started_utc": now(), "command": command, "timeout_seconds": limit,
                                          "inputs": inputs, "remaining_build_seconds": remaining})
    started = time.monotonic()
    with (output / "build.log").open("wb") as log:
        process = subprocess.Popen(command, cwd=ROOT, env=environment(), stdout=log, stderr=subprocess.STDOUT)
        timed_out = False
        try:
            exit_code = process.wait(timeout=limit)
        except subprocess.TimeoutExpired:
            timed_out = True
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=log, stderr=log, check=False)
            exit_code = process.wait(timeout=30)
    duration = time.monotonic() - started
    current = source_identity()
    result = {"configuration": configuration, "command": command, "exit_code": exit_code, "timed_out": timed_out,
              "wall_seconds": duration, "completed_utc": now(), "stable_inputs": current == inputs,
              "log_sha256": sha(output / "build.log"), "status": "PASS" if exit_code == 0 and not timed_out and inputs == current else "FAIL"}
    products = [ROOT / f"bin/{configuration}/RigidBodySimulation/RigidBodySimulation.exe",
                ROOT / f"bin/{configuration}/GEngine/GEngine.lib", ROOT / f"external/glad/bin/{configuration}/glad/glad.lib"]
    result["products"] = {str(p.relative_to(ROOT)): sha(p) for p in products if p.exists()}
    write_new(output / "result.json", result)
    print(json.dumps({**result, "evidence": str(output)}, indent=2))
    if result["status"] != "PASS":
        raise SystemExit(1)


def latest_build(configuration):
    attempts = sorted((EVIDENCE / "builds").glob(f"{configuration}-*/result.json"))
    if not attempts:
        raise RuntimeError("no build evidence")
    p = attempts[-1]
    r = json.loads(p.read_text())
    inv = json.loads(p.with_name("invocation.json").read_text())
    assert r["status"] == "PASS" and inv["inputs"] == source_identity(), "build/source mismatch"
    return p, r


def run(configuration, observation):
    guards()
    build_path, built = latest_build(configuration)
    limits = continuation()
    records, successful = campaign_runs()
    sequence = [("Debug", "off"), ("Debug", "on"), ("Release", "off"), ("Release", "on")]
    if len(records) >= limits["native_launch_limit"] or sequence[len(successful)] != (configuration, observation):
        raise RuntimeError("accepted native launch sequence/count exceeded")
    if observation == "on":
        off = successful[configuration, "off"]
    probe = subprocess.run(["powershell", "-NoProfile", "-Command",
                            "@(Get-Process -Name RigidBodySimulation -ErrorAction SilentlyContinue).Count"],
                           capture_output=True, text=True, check=True)
    if probe.stdout.strip() != "0":
        raise RuntimeError("an RBS process is already running; no intervention authorized")
    remaining = limits["native_total_seconds"] - usage("runs")
    if remaining <= 0:
        raise RuntimeError("accepted native wall budget exhausted")
    exe = ROOT / f"bin/{configuration}/RigidBodySimulation/RigidBodySimulation.exe"
    assert sha(exe) == built["products"][str(exe.relative_to(ROOT))]
    output = fresh_attempt("runs", f"{configuration}-{observation}")
    data = contained(output / "data")
    data.mkdir()
    cwd = contained(ROOT / f"runtime/RigidBodySimulation/{configuration}")
    cwd.mkdir(parents=True, exist_ok=True)
    settings = cwd / "imgui.ini"
    settings_before = settings.read_bytes() if settings.exists() else None
    if settings_before is not None:
        (output / "owner-imgui-before.ini").write_bytes(settings_before)
    command = [str(exe), "--resting-contact-diagnostic", observation, "--lattice",
               str(ROOT / "tools/pre_editor_resting_contact_lattice.txt"), "--output", str(data)]
    inputs = source_identity()
    limit = min(limits["native_process_seconds"], remaining)
    write_new(output / "invocation.json", {"started_utc": now(), "configuration": configuration,
        "observation": observation, "command": command, "cwd": str(cwd), "timeout_seconds": limit,
        "native_launch_number": len(records) + 1, "build_evidence": str(build_path.relative_to(ROOT)),
        "continuation_acceptance": {"path": str(CONTINUATION.relative_to(ROOT)), "sha256": sha(CONTINUATION)},
        "products": built["products"], "inputs": inputs,
        "settings_reset": "Preserve/restore the exact owner's initial imgui.ini between serial diagnostic runs; archive each run output."})
    started = time.monotonic()
    timed_out = False
    with (output / "console.log").open("wb") as log:
        process = subprocess.Popen(command, cwd=cwd, env=environment(), stdout=log, stderr=subprocess.STDOUT)
        try:
            exit_code = process.wait(timeout=limit)
        except subprocess.TimeoutExpired:
            timed_out = True
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=log, stderr=log, check=False)
            exit_code = process.wait(timeout=30)
    duration = time.monotonic() - started
    for name in ("GEngine.log", "imgui.ini"):
        src = cwd / name
        if src.exists():
            shutil.copyfile(src, output / ("runtime-" + name))
    # No competing app existed, and our process has exited. Preserve the owner's
    # original settings; logs and diagnostic output remain archived, never restored.
    if settings_before is not None:
        settings.write_bytes(settings_before)
    elif settings.exists():
        contained(settings).unlink()
    completed = json.loads((data / "result.json").read_text()) if (data / "result.json").exists() else {"complete": False}
    equal_inputs = inputs == source_identity()
    result = {"configuration": configuration, "observation": observation, "exit_code": exit_code,
        "timed_out": timed_out, "wall_seconds": duration, "completed_utc": now(), "stable_inputs": equal_inputs,
        "application_result": completed, "status": "PASS" if exit_code == 0 and not timed_out and equal_inputs and completed["complete"] else "FAIL",
        "evidence": {str(p.relative_to(output)): sha(p) for p in output.rglob("*") if p.is_file()}}
    if observation == "on" and result["status"] == "PASS":
        off_dir = off.parent / "data"
        matching = {name: sha(data / name) == sha(off_dir / name) for name in
                    ("bodies.bin", "stages.bin", "contacts.csv", "metrics.csv", "initial.csv", "islands.csv")}
        result["observer_noninterference"] = matching
        if not all(matching.values()):
            result["status"] = "FAIL"
    write_new(output / "result.json", result)
    print(json.dumps({k: v for k, v in result.items() if k != "evidence"}, indent=2))
    print("Evidence:", output)
    if result["status"] != "PASS":
        raise SystemExit(1)


def static_checks():
    guards()
    a = authority()
    checks = {"production_scope": len(a["production_files"]) == 11,
              "supporting_scope": len(a["supporting_files"]) == 3,
              "matrix_ticks": sum(DURATIONS) == 6960,
              "matrix_seconds": sum(DURATIONS) / 120 == 58}
    checks["lattice_hash"] = sha(ROOT / "tools/pre_editor_resting_contact_lattice.txt") == "79eb5a76b06726b77333fff93ddb47999671df025ef1c75322494b8ac62116c6"
    # These observations must not replace/delete/reorder any original solver statement.
    for p in a["production_files"][:4]:
        if p.endswith(".cpp"):
            diff = git("diff", "--unified=0", "--", p).decode()
            removed = [line for line in diff.splitlines() if line.startswith("-") and not line.startswith("---") and line[1:].strip()]
            checks["solver_original_statements_retained:" + p] = not removed
    out = fresh_attempt("static", "check")
    write_new(out / "result.json", {"time_utc": now(), "checks": checks, "inputs": source_identity(),
                                    "status": "PASS" if all(checks.values()) else "FAIL"})
    print(json.dumps(checks, indent=2))
    if not all(checks.values()):
        raise SystemExit(1)


def analyze():
    guards()
    records, successful = campaign_runs()
    assert len(records) == 5 and len(successful) == 4, "four complete original comparisons required"
    runs = {key: path.parent for key, path in successful.items()}
    output = fresh_attempt("analysis", "causal")
    body_format = struct.Struct("<IIIQQ13fBd")
    stage_format = struct.Struct("<IIII13fBd")
    summary = {"phase": "19", "kind": "CURRENT_RBS_DIAGNOSIS_NOT_REMEDY_QUALIFICATION", "recorded_utc": now(),
               "baseline": authority()["baseline"], "runs": {}, "causal": {}, "historical_120_second_gate": "NOT_RUN_NOT_PASSED",
               "preserved_failed_attempt": continuation()["preserved_failure"]}
    for (configuration, observation), folder in runs.items():
        data = folder / "data"
        counts = Counter()
        selected = defaultdict(list)
        with (data / "bodies.bin").open("rb") as f:
            while row := f.read(body_format.size):
                assert len(row) == body_format.size
                values = body_format.unpack(row)
                episode, tick, index, slot, generation = values[:5]
                assert episode < 9 and tick <= DURATIONS[episode] and index < BODY_COUNTS[episode]
                assert slot == index + 1 and generation > 0 and all(math.isfinite(v) for v in values[5:])
                counts[episode] += 1
                if episode != 0 or index in (10, 12, 22, 24):
                    selected[episode, index].append((tick, *values[5:]))
        assert [counts[i] for i in range(9)] == [(n + 1) * b for n, b in zip(DURATIONS, BODY_COUNTS)]
        metrics = defaultdict(list)
        with (data / "metrics.csv").open(newline="") as f:
            for row in csv.DictReader(f):
                assert row["finite"] == "1"
                metrics[int(row["episode"])].append({k: float(v) for k, v in row.items()})
        assert [len(metrics[i]) for i in range(9)] == list(DURATIONS)
        episodes = {}
        for ep in range(9):
            rows = metrics[ep]
            episodes[NAMES[ep]] = {"ticks": len(rows), "peak_energy_excluding_initial": max(r["energy"] for r in rows),
                "peak_penetration": max(r["penetration"] for r in rows),
                "tail_last_second_max_linear": max(r["max_linear"] for r in rows[-120:]),
                "tail_last_second_max_angular": max(r["max_angular"] for r in rows[-120:]),
                "final_active": rows[-1]["active"], "final_sleeping": rows[-1]["sleeping"],
                "wake_transitions": rows[-1]["wakes"], "sleep_transitions": rows[-1]["sleeps"],
                "named_vector_capacity_growth_bytes": rows[-1]["capacity_growth_bytes"]}
        tick_times = defaultdict(lambda: [0, 0, 0])
        with (data / "timing.csv").open(newline="") as f:
            for row in csv.DictReader(f):
                item = tick_times[int(row["episode"]), int(row["tick"])]
                item[0] += int(row["interval_ns"])
                item[1] += int(row["boundary_ns"])
                item[2] = int(row["detailed_ns"])
        assert len(tick_times) == 6960
        costs = {}
        for ep, name in enumerate(NAMES):
            values = [v[0] for (e, _), v in tick_times.items() if e == ep]
            common = [v[1] for (e, _), v in tick_times.items() if e == ep]
            detailed = [v[2] for (e, _), v in tick_times.items() if e == ep]
            costs[name] = {"samples": len(values), "physics_intervals_median_ns": statistics.median(values),
                           "physics_intervals_mean_ns": statistics.mean(values), "min_ns": min(values), "max_ns": max(values),
                           "common_boundary_mean_ns": statistics.mean(common), "detailed_recording_mean_ns": statistics.mean(detailed)}
        summary["runs"][f"{configuration}-{observation}"] = {
            "evidence": str(folder.relative_to(ROOT)), "result_sha256": sha(folder / "result.json"),
            "physical_state_rows": sum(counts.values()), "episodes": episodes, "costs": costs}
        if observation == "off":
            continue
        operations = Counter()
        impact = defaultdict(list)
        contributions = defaultdict(lambda: defaultdict(float))
        support_motion = []
        max_spin_linear = max_position_velocity = 0.0
        with (data / "events.csv").open(newline="") as f:
            for row in csv.DictReader(f):
                ep, tick, operation = (int(row[k]) for k in ("episode", "tick", "operation"))
                operations[operation] += 1
                a, b = int(row["a"]), int(row["b"])
                for side, index in (("a", a), ("b", b)):
                    if ep == 0 and index not in (10, 12, 22, 24):
                        continue
                    delta_y = float(row[f"{side}_after_vy"]) - float(row[f"{side}_before_vy"])
                    contributions[ep, tick, index][operation] += delta_y
                    other = "b" if side == "a" else "a"
                    if ep == 0 and delta_y > 0 and operation in (0, 1, 5) and float(row[f"{other}_before_vy"]) > 0:
                        if len(support_motion) < 20:
                            support_motion.append({"episode": ep, "tick": tick, "index": index, "operation": operation,
                                "delta_vy": delta_y, "support_before_vy": float(row[f"{other}_before_vy"]), "sequence": int(row["sequence"])})
                    if operation in (2, 3):
                        max_spin_linear = max(max_spin_linear, *(abs(float(row[f"{side}_after_v{axis}"]) - float(row[f"{side}_before_v{axis}"])) for axis in "xyz"))
                    if operation in (4, 8):
                        max_position_velocity = max(max_position_velocity, *(abs(float(row[f"{side}_after_{kind}{axis}"]) - float(row[f"{side}_before_{kind}{axis}"])) for kind in ("v", "w") for axis in "xyz"))
                if operation == 6:
                    speed = float(row["closing_speed"])
                    restitution = float(row["restitution"])
                    assert restitution == (.25 if -speed > 1.0 else 0.0), "actual restitution branch mismatch"
                    impact[ep].append({"tick": tick, "closing_speed": speed, "restitution": restitution,
                                      "a": a, "b": b, "sequence": int(row["sequence"])})
        assert max_spin_linear == 0 and max_position_velocity == 0
        stage_states = {}
        with (data / "stages.bin").open("rb") as f:
            while row := f.read(stage_format.size):
                assert len(row) == stage_format.size
                v = stage_format.unpack(row)
                ep, tick, stage, index = v[:4]
                # Stored float Vy. Sum actual changes in double; do not invent a
                # universal physical tolerance from the historical diagnostic.
                stage_states[ep, tick, stage, index] = v[4 + 8]
        accounting = {"warm_start_max_abs_vy_residual": 0.0, "solve_max_abs_vy_residual": 0.0,
                      "toi_integration_max_abs_vy_residual": 0.0, "position_max_abs_vy_residual": 0.0}
        groups = ((4, 5, (0,), "warm_start_max_abs_vy_residual"),
                  (6, 7, (1, 2, 3, 5), "solve_max_abs_vy_residual"),
                  (7, 8, (6, 7, 8), "toi_integration_max_abs_vy_residual"),
                  (8, 9, (4,), "position_max_abs_vy_residual"))
        for (ep, tick, stage, index), value in stage_states.items():
            for first, last, kinds, key in groups:
                if stage == last:
                    residual = value - stage_states[ep, tick, first, index] - sum(contributions[ep, tick, index][op] for op in kinds)
                    accounting[key] = max(accounting[key], abs(residual))
        upward = []
        for (ep, index), values in selected.items():
            if ep != 0:
                continue
            start = None
            for i, row in enumerate(values):
                tick, *state = row
                vy = state[8]
                if vy > 0 and start is None:
                    start = i
                if start is not None and (vy <= 0 or i == len(values) - 1):
                    end = i - 1 if vy <= 0 else i
                    lo, hi = max(0, start - 5), min(len(values) - 1, end + 5)
                    ops = Counter()
                    for t in range(values[lo][0], values[hi][0] + 1):
                        for op, contribution in contributions[ep, t, index].items():
                            ops[op] += contribution
                    upward.append({"index": index, "start_tick": values[start][0], "end_tick": values[end][0],
                        "window_start_tick": values[lo][0], "window_end_tick": values[hi][0],
                        "boundary_truncated": start < 5 or end + 5 >= len(values),
                        "peak_vy": max(v[9] for v in values[start:end + 1]),
                        "signed_operation_vy_contributions_over_window": dict(ops)})
                    start = None
        write_new(output / f"{configuration}-upward-windows.json", upward)
        summary["causal"][configuration] = {"operations": dict(operations), "accounting": accounting,
            "spin_rolling_direct_linear_change_max": max_spin_linear, "position_correction_velocity_change_max": max_position_velocity,
            "moving_support_examples": support_motion, "toi_events_by_episode": dict(impact), "upward_episodes": len(upward),
            "complete_windows": f"{configuration}-upward-windows.json"}
    off_cost = summary["runs"]["Release-off"]["costs"]
    on_cost = summary["runs"]["Release-on"]["costs"]
    summary["release_observer_overhead"] = {name: {
        "mean_interval_delta_ns": on_cost[name]["physics_intervals_mean_ns"] - off_cost[name]["physics_intervals_mean_ns"],
        "mean_interval_relative_delta": on_cost[name]["physics_intervals_mean_ns"] / off_cost[name]["physics_intervals_mean_ns"] - 1,
        "claim": "one matched pair, observed instrumentation cost; not a throughput acceptance or repeatability claim"} for name in NAMES}
    write_new(output / "summary.json", summary)
    print(json.dumps({"status": "PASS", "analysis": str(output), "native_launches": 4,
                      "ticks_per_run": 6960, "source_stable": True, "historical_stability_gate": "NOT_RUN"}, indent=2))


# The targeted amendment has its own authority and destinations. Historical
# campaign commands above retain their original meaning and immutable results.
TARGET = EVIDENCE / "targeted-60hz-wood-sphere158-01"
TARGET_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_19_TARGETED_60HZ_WOOD_SPHERE158_DIAGNOSTIC_AMENDMENT_01_ACCEPTANCE.json"
TARGET_PREPARATION = EVIDENCE / "proposals/targeted-60hz-wood-sphere158-01/preparation.json"


def target_authority():
    a = json.loads(TARGET_ACCEPTANCE.read_text())
    assert a["status"] == "ACCEPTED_WITH_CONTROLLING_OWNER_CLARIFICATIONS"
    assert sha(ROOT / a["proposal"]["path"]) == a["proposal"]["sha256"]
    assert (a["native_launch_cap"], a["per_process_seconds_limit"], a["native_seconds_limit"]) == (6, 300, 1200)
    assert (a["per_build_seconds_limit"], a["build_seconds_limit"]) == (1200, 2400)
    assert not a["unattended_launch_authorized"]
    return a


def target_owned():
    a = target_authority()["scope"]
    return a["production_modification_members"] + a["supporting_modification_members"]


def target_guards():
    a = target_authority()
    start = json.loads((TARGET / "implementation-start.json").read_text())
    assert git("rev-parse", "HEAD").decode().strip() == a["baseline"]
    assert git("branch", "--show-current").decode().strip() == "pre-editor/refactor"
    assert not git("diff", "--cached", "--name-only").strip() and not git("ls-files", "--unmerged").strip()
    index = Path(git("rev-parse", "--git-path", "index").decode().strip())
    assert sha(index) == start["index_sha256"]
    assert not (index.parent / "pre-editor-approval/phase-19.json").exists()
    # Previous phase members outside this amendment are retained verbatim.
    for path, digest in start["before_inputs"].items():
        if path not in target_owned():
            assert sha(ROOT / path) == digest, path
    prep = json.loads(TARGET_PREPARATION.read_text())
    for group in ("runs", "builds"):
        for row in prep["budget_evidence"][group]:
            assert sha(ROOT / row["path"]) == row["sha256"], row["path"]
    assert len(list((EVIDENCE / "runs").glob("*/invocation.json"))) == 5
    assert set(git("diff", "--name-only").decode().splitlines()) <= set(owned()) | set(target_owned())


def target_inputs():
    old = json.loads((EVIDENCE / "candidate-02/input-closure-03.json").read_text())
    paths = set(old["workspace_inputs"]) | set(target_owned()) | {TARGET_ACCEPTANCE.relative_to(ROOT).as_posix()}
    # Old review metadata is historical validation provenance, not source currency.
    inputs = {p: sha(ROOT / p) for p in sorted(paths)}
    external = {p: sha(p) for p in old["external_toolchain_inputs"]}
    for p, v in old["external_toolchain_inputs"].items():
        assert external[p] == v["sha256"], f"toolchain/dependency drift: {p}"
    discovery = {}
    for path, rule in old["discovery_rules_by_scope"].items():
        cpp = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".inc"}
        shader = {".glsl", ".vert", ".frag", ".geom", ".tesc", ".tese", ".comp", ".shader"}
        extensions = {"immediate directories and C/C++/shader header/source extensions": cpp | shader,
                      "immediate directories and C/C++ header/source extensions": cpp,
                      "immediate directories and .lib/.dll members": {".lib", ".dll"}}
        assert rule == "immediate all members" or rule in extensions, (path, rule)
        discovery[path] = sorted(x.name for x in (ROOT / path).iterdir()
                                 if rule == "immediate all members" or x.is_dir() or x.suffix.lower() in extensions[rule])
        assert discovery[path] == old["discovery_membership"][path], f"discovery drift: {path}"
    return {"workspace_inputs": inputs, "external_inputs": external, "discovery": discovery}


def target_usage():
    prep = json.loads(TARGET_PREPARATION.read_text())["budgets"]
    builds = list((TARGET / "builds").glob("*/result.json"))
    for inv in (TARGET / "builds").glob("*/invocation.json"):
        assert inv.with_name("result.json").exists(), "incomplete build attempt; reconcile actual usage first"
    runs = list((TARGET / "runs").glob("*/invocation.json"))
    return {"build_seconds": prep["build_consumed_seconds"] + sum(json.loads(p.read_text())["wall_seconds"] for p in builds),
            "native_seconds": prep["native_consumed_seconds"] + sum(json.loads(p.with_name("result.json").read_text())["wall_seconds"] for p in runs if p.with_name("result.json").exists()),
            "native_launches": 5 + sum(p.with_name("process.json").exists() for p in runs),
            "target_attempt_reservations": len(runs),
            "incomplete_target_attempts": sum(not p.with_name("result.json").exists() for p in runs)}


def target_fresh(kind, name, base=TARGET):
    parent = contained(base / kind)
    parent.mkdir(exist_ok=True)
    for n in range(1, 10000):
        dest = parent / f"{name}-{n:02d}"
        if not dest.exists():
            dest.mkdir()
            return dest
    raise RuntimeError("targeted evidence destination unavailable")


def target_build(configuration, attended=False):
    guards, inputs_now, usage = (attended_guards, attended_inputs, attended_usage) if attended else (target_guards, target_inputs, target_usage)
    guards()
    if attended:
        attended_frozen()
        assert json.loads((ATTENDED / "coverage.json").read_text())["status"] == "PASS"
    remaining = 2400 - usage()["build_seconds"]
    assert remaining > 0, "build budget exhausted"
    inputs = inputs_now()
    output = target_fresh("builds", configuration, ATTENDED if attended else TARGET)
    for path in attended_authority()["scope"] if attended else target_owned():
        dest = output / "source" / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / path, dest)
    command = [str(MSBUILD), str(ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"),
               "/t:Build", f"/p:Configuration={configuration}", "/p:Platform=x64", "/m:2", "/nr:false", "/v:minimal"]
    limit = min(1200, remaining)
    write_new(output / "invocation.json", {"started_utc": now(), "command": command, "timeout_seconds": limit,
        "remaining_aggregate_seconds": remaining, "inputs": inputs})
    started = time.monotonic()
    with (output / "build.log").open("wb") as log:
        process = subprocess.Popen(command, cwd=ROOT, env=environment(), stdout=log, stderr=subprocess.STDOUT)
        timed_out = False
        try:
            code = process.wait(timeout=limit)
        except subprocess.TimeoutExpired:
            timed_out = True
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=log, stderr=log, check=False)
            code = process.wait(timeout=30)
    duration = time.monotonic() - started
    stable = inputs == inputs_now()
    products = [f"bin/{configuration}/RigidBodySimulation/RigidBodySimulation.exe", f"bin/{configuration}/GEngine/GEngine.lib",
                f"external/glad/bin/{configuration}/glad/glad.lib"]
    result = {"configuration": configuration, "status": "PASS" if code == 0 and stable and not timed_out and duration <= limit else "FAIL",
              "exit_code": code, "timed_out": timed_out, "wall_seconds": duration, "completed_utc": now(),
              "stable_inputs": stable, "log_sha256": sha(output / "build.log"),
              "products": {p: sha(ROOT / p) for p in products if (ROOT / p).exists()}}
    write_new(output / "result.json", result)
    print(json.dumps({**result, "evidence": str(output), "cumulative": usage()}, indent=2))
    if result["status"] != "PASS":
        raise SystemExit(1)


def target_latest(configuration, inputs, base=TARGET):
    results = sorted((base / "builds").glob(f"{configuration}-*/result.json"))
    assert results, f"no targeted {configuration} build"
    result = results[-1]
    data = json.loads(result.read_text())
    assert data["status"] == "PASS"
    built_inputs = json.loads(result.with_name("invocation.json").read_text())["inputs"]
    reuse_reference = None
    if built_inputs != inputs:
        assert base == ATTENDED, "current inputs differ from build"
        reuse = json.loads((ATTENDED / "build-input-reuse.json").read_text())[configuration]
        assert reuse["result_sha256"] == sha(result) and reuse["current_inputs"] == inputs
        assert built_inputs["external_inputs"] == inputs["external_inputs"] and built_inputs["discovery"] == inputs["discovery"]
        delta = {p for p in set(built_inputs["workspace_inputs"]) | set(inputs["workspace_inputs"])
                 if built_inputs["workspace_inputs"].get(p) != inputs["workspace_inputs"].get(p)}
        assert delta == {"tools/pre_editor_resting_contact_diagnosis.py"}, "compiled inputs changed; build required"
        assert reuse["before_runner_sha256"] == built_inputs["workspace_inputs"][next(iter(delta))]
        reuse_reference = {"path": (ATTENDED / "build-input-reuse.json").relative_to(ROOT).as_posix(),
                           "sha256": sha(ATTENDED / "build-input-reuse.json")}
    for path, digest in data["products"].items():
        assert sha(ROOT / path) == digest, f"product changed: {path}"
    return {"path": result.relative_to(ROOT).as_posix(), "sha256": sha(result), "products": data["products"], "input_reuse": reuse_reference}


def target_strip_observation(source):
    """Compare original Physics tokens, separately reviewing observation branches.

    Only named observation declarations, payload branches and calls are removed.
    Braces are excluded to permit wrapping an original single statement in an
    observation scope; original branch predicates and statement order stay checked.
    """
    import re
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
    names = r"(?:diagnostic|cacheDiagnostic|sleepDiagnostic|gravityDiagnostic|integrationDiagnostic|toiDiagnostic)"
    source = re.sub(r"PhysicsDiagnostic::Observation\s+" + names + r"\([^;]*;", "", source)
    source = re.sub(r"PhysicsDiagnostic::(?:Mark|SetSlot)\([^;]*;", "", source)
    source = re.sub(names + r"\.Finish\(\);", "", source)
    pattern = re.compile(r"if\s*\(\s*" + names + r"\s*\)\s*")
    while (match := pattern.search(source)):
        start = match.end()
        if source[start] == "{":
            depth, end = 1, start + 1
            while depth:
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
        else:
            end = source.index(";", start) + 1
        removed = source[start:end]
        assert not re.search(r"\b(?:ApplyImpulse\w*|WakeUp|TrySleep|UpdateSleepTimer|SetBodyPose)\s*\(", removed), "mutation in observation payload"
        source = source[:match.start()] + source[end:]
    return re.findall(r'"(?:\\.|[^"\\])*"|\w+|[^\s{}]', source)


# Explicit padding-free little-endian record layout written field by field in C++.
TARGET_FIELDS = [(n, "I") for n in ("kind", "tick", "stage", "a", "b", "operation")]
TARGET_FIELDS += [(n, "Q") for n in ("sequence", "stamp_ns", "pair", "point", "old_point")]
TARGET_FIELDS += [(n, "i") for n in ("slot", "previous_slot", "reason")] + [("impulses_known", "I")]
TARGET_FIELDS += [(n, "d") for n in ("time", "duration", "restitution", "closing_speed", "penetration", "bias")]
TARGET_FIELDS += [(f"{v}_{axis}", "f") for v in ("normal", "anchor_a", "anchor_b", "local_a", "local_b", "tangent_u", "tangent_v",
    "linear_impulse_a", "linear_impulse_b", "angular_impulse_a", "angular_impulse_b") for axis in "xyz"]
TARGET_FIELDS += [(f"lambda_{when}_{n}", "d") for when in ("before", "after") for n in range(3)]
TARGET_FIELDS += [(f"jacobian_{row}_{n}", "d") for row in ("n", "u", "v") for n in range(12)]
TARGET_FIELDS += [(n, "Q") for n in ("feature_a", "feature_b", "constraint_token")]
TARGET_FIELDS += [(f"{state}_{name}", fmt) for state in ("before_a", "before_b", "after_a", "after_b")
                  for name, fmt in [(n, "f") for n in ("px", "py", "pz", "qw", "qx", "qy", "qz", "vx", "vy", "vz", "wx", "wy", "wz")] + [("sleeping", "B"), ("inactive", "d")]]
TARGET_WIRE = struct.Struct("<" + "".join(fmt for _, fmt in TARGET_FIELDS))


def target_decode(raw):
    values = dict(zip((n for n, _ in TARGET_FIELDS), TARGET_WIRE.unpack(raw)))
    if values["kind"] not in (0, 1, 2) or values["tick"] > 1800 or values["stage"] > 11 or max(values["a"], values["b"]) >= 185:
        raise ValueError("invalid target wire identity/tick/stage")
    if any(not math.isfinite(v) for v in values.values() if isinstance(v, float)):
        raise ValueError("nonfinite target record")
    return values


def target_read_trace(path):
    with path.open("rb") as stream:
        assert stream.read(8) == b"P19T60V1", "unknown target schema"
        while raw := stream.read(TARGET_WIRE.size):
            if len(raw) != TARGET_WIRE.size:
                raise ValueError("truncated target record")
            yield target_decode(raw)


def target_identity_join(bindings, uuid, slot, generation):
    """Evidence join requires UUID AND typed body generation, never a suffix/index."""
    if not slot or not generation or not uuid:
        raise ValueError("invalid identity")
    rows = [r for r in bindings if r["uuid"] == uuid]
    if len(rows) != 1 or (rows[0]["slot"], rows[0]["generation"]) != (slot, generation):
        raise ValueError("missing, ambiguous or stale target identity")
    if sum((r["slot"], r["generation"]) == (slot, generation) for r in bindings) != 1:
        raise ValueError("ambiguous RuntimeBody mapping")
    return rows[0]


def target_static():
    target_guards()
    checks = {}
    for path in ["GEngine/include/GEngine/Physics/PhysicsSystem.cpp", "GEngine/include/GEngine/Physics/Manifold.cpp",
                 "GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp"]:
        original = (TARGET / "source-before" / path).read_text()
        current = (ROOT / path).read_text()
        assert target_strip_observation(original) == target_strip_observation(current), f"non-observer Physics token change: {path}"
        checks[path] = "PASS original non-observation tokens, predicates and order; added scope braces reviewed separately"
    source = (ROOT / "RigidBodySimulation/tests/RestingContactDiagnosis.h").read_text().split("struct TargetSession", 1)[1]
    forbidden = ("SetPhysicsStep(", "SetPaused(", "ApplyImpulse(", "WakeUp(", "TrySleep(", "SetSolverIterations(",
                 "GetInverseInertiaTensorWorldSpace(", "BodySpaceToWorldSpace(", "GetBodyToWorldRotation(", "Identify(")
    assert not any(token in source for token in forbidden), "target recorder writes Physics/clock/cache/identity"
    main = (ROOT / "RigidBodySimulation/src/main.cpp").read_text()
    assert "constexpr auto scenePreset = Rbs::RbsScenePreset::SphereLattice;" in main
    assert "ImGui::IsKeyPressed(ImGuiKey_F10,false)" in (ROOT / "RigidBodySimulation/src/RbsFrame.cpp").read_text()
    # Round-trip exact signed impulses and generations through the independent wire reader.
    sample = [0] * len(TARGET_FIELDS)
    indices = {n: i for i, (n, _) in enumerate(TARGET_FIELDS)}
    sample[indices["linear_impulse_a_y"]] = -0.125
    sample[indices["linear_impulse_b_y"]] = 0.125
    sample[indices["pair"]] = 2**48 + 3
    parsed = target_decode(TARGET_WIRE.pack(*sample))
    assert parsed["linear_impulse_a_y"] == -parsed["linear_impulse_b_y"] == -0.125 and parsed["pair"] == 2**48 + 3
    for key, bad in (("a", 185), ("tick", 1801), ("before_a_vy", float("nan"))):
        invalid = sample.copy(); invalid[indices[key]] = bad
        try:
            target_decode(TARGET_WIRE.pack(*invalid))
        except ValueError:
            pass
        else:
            raise AssertionError(f"wire guard failed: {key}")
    identity = {"uuid": 901, "slot": 27, "generation": 44, "name": "wood_sphere158"}
    assert target_identity_join([identity], 901, 27, 44) == identity
    for table, uuid, slot, generation in (([identity], 901, 27, 43), ([identity], 158, 27, 44),
        ([identity, identity], 901, 27, 44), ([identity], 901, 0, 44)):
        try:
            target_identity_join(table, uuid, slot, generation)
        except ValueError:
            pass
        else:
            raise AssertionError("identity/generation rejection failed")
    output = target_fresh("static", "check")
    inputs = target_inputs()
    write_new(output / "schema.json", {"magic": "P19T60V1", "byte_order": "little", "record_bytes": TARGET_WIRE.size,
        "fields": TARGET_FIELDS, "stage_order": ["Begin", "Wake", "Gravity", "Broadphase", "Narrowphase", "WarmStart", "Islands", "Solve", "Integration", "Correction", "Sleep", "End"],
        "signs": "normal is solver A->B; impulse vectors are actual signed application to named endpoint; contact B->A normal negated; TOI closing_speed retains dot(vA-vB,n_BtoA)",
        "units": "seconds, world-length units, radians; mass from initial.csv; raw authoritative velocities",
        "point_identity": "pair/point serials from explicit collector lifetime and matching events; token is not lifetime identity",
        "timing_limits": "operation_observer_ns measures constructor snapshot/validation and Finish callback/snapshot/check; excludes payload assignments and disabled-hook overhead. Boundary/frame intervals measured directly; no 60Hz off/on comparison.",
        "missing_history": "new partners' earlier ticks are unobserved; current tick includes their earlier operations; sequence gaps denote unretained subjects, not dropped recorded events"})
    write_new(output / "result.json", {"status": "PASS", "kind": "STATIC_AND_OFFLINE_ONLY", "recorded_utc": now(),
        "checks": checks, "wire_negative_and_signed_checks": "PASS", "identity_generation_negative_checks": "PASS", "owner_preset_preserved": True,
        "inputs": inputs, "new_application_launches": 0, "runtime_identity_and_F10": "PENDING_SINGLE_INTERACTIVE_RUN"})
    print(json.dumps({"status": "PASS", "evidence": str(output), "record_bytes": TARGET_WIRE.size}, indent=2))


def target_prepare():
    target_guards()
    inputs = target_inputs()
    builds = {c: target_latest(c, inputs) for c in ("Debug", "Release")}
    static = sorted((TARGET / "static").glob("*/result.json"))[-1]
    assert json.loads(static.read_text())["inputs"] == inputs
    assert target_usage()["native_launches"] == 5 and 1200 - target_usage()["native_seconds"] >= 300
    cwd = contained(ROOT / "runtime/RigidBodySimulation/Release")
    assert cwd.is_dir()
    executable = ROOT / "bin/Release/RigidBodySimulation/RigidBodySimulation.exe"
    products = builds["Release"]["products"]
    command = [str(executable), "--targeted-entity-diagnostic", "wood_sphere158", "--output", str(TARGET / "runs/Release-target-01/data")]
    output = target_fresh("prelaunch", "ready")
    record = {"schema": 1, "status": "READY FOR OWNER-ATTENDED TARGET CAPTURE; NOT PHASE APPROVAL READY",
        "recorded_utc": now(), "inputs": inputs, "builds": builds, "static": {"path": static.relative_to(ROOT).as_posix(), "sha256": sha(static)},
        "command": command, "cwd": str(cwd), "asset_root": str(ROOT / "bin/Release/assets"), "executable_sha256": sha(executable),
        "runtime_settings": {str(p.relative_to(ROOT)): sha(p) for p in [cwd / "imgui.ini"] if p.exists()},
        "usage": target_usage(), "native_launches_performed_for_target": 0, "requires_owner_ready_after_handoff": True,
        "P19_INPUT_01": "Affected Debug/Release builds PASS; single Release integration PENDING",
        "off_on_60hz_equivalence": "NOT_MEASURED", "event_and_F10": "PENDING", "final_owner_acceptance": "PENDING"}
    write_new(output / "readiness.json", record)
    print(json.dumps({"status": record["status"], "readiness": str(output / "readiness.json"), "usage": record["usage"]}, indent=2))


def target_run(owner_ready):
    # This entry is intentionally impossible from preparation or a generic phase
    # acceptance alone. The owner must first receive the readiness handoff.
    target_guards()
    readiness = sorted((TARGET / "prelaunch").glob("*/readiness.json"))[-1]
    ready = json.loads(readiness.read_text())
    confirmation = json.loads(Path(owner_ready).read_text())
    assert confirmation["owner_present_and_ready"] is True
    assert confirmation["readiness_sha256"] == sha(readiness) and confirmation["owner_instruction"].strip()
    assert confirmation["recorded_utc"] >= ready["recorded_utc"]
    assert ready["inputs"] == target_inputs()
    for c in ("Debug", "Release"):
        target_latest(c, ready["inputs"])
    for p, digest in ready["runtime_settings"].items():
        assert sha(ROOT / p) == digest, "owner runtime settings changed; classify before launch"
    assert target_usage()["native_launches"] == 5 and 1200 - target_usage()["native_seconds"] >= 300
    probe = subprocess.run(["powershell", "-NoProfile", "-Command", "@(Get-Process -Name RigidBodySimulation -ErrorAction SilentlyContinue).Count"], capture_output=True, text=True, check=True)
    assert int(probe.stdout.strip()) == 0, "an RBS process is already open; do not intervene"
    output = contained(TARGET / "runs/Release-target-01")
    assert not output.exists(), "unique target attempt already reserved; no retry"
    (output / "data").mkdir(parents=True)
    command = ready["command"]
    write_new(output / "invocation.json", {"command": command, "cwd": ready["cwd"], "started_utc": now(),
        "native_launch_number": 6, "owner_ready": confirmation, "readiness_sha256": sha(readiness),
        "timeout_seconds": 300, "orderly_close_seconds": 270, "prior_usage": target_usage(), "inputs": ready["inputs"]})
    started = time.monotonic(); forced = False
    with (output / "console.log").open("wb") as log:
        process = subprocess.Popen(command, cwd=ready["cwd"], env=environment(), stdout=log, stderr=subprocess.STDOUT)
        write_new(output / "process.json", {"pid": process.pid, "created_utc": now()})
        try:
            code = process.wait(timeout=max(0, 270 - (time.monotonic() - started)))
        except subprocess.TimeoutExpired:
            # Close only the process created above; no owner/external process kill.
            subprocess.run(["powershell", "-NoProfile", "-Command", f"(Get-Process -Id {process.pid} -ErrorAction SilentlyContinue).CloseMainWindow()"], stdout=log, stderr=log, timeout=5, check=False)
            try:
                code = process.wait(timeout=max(0, 300 - (time.monotonic() - started)))
            except subprocess.TimeoutExpired:
                forced = True; process.kill(); code = process.wait(timeout=5)
    duration = time.monotonic() - started
    application = json.loads((output / "data/result.json").read_text()) if (output / "data/result.json").exists() else None
    for name in ("GEngine.log", "imgui.ini"):
        source = Path(ready["cwd"]) / name
        if source.exists():
            shutil.copyfile(source, output / f"runtime-{name}")
    after = target_inputs()
    # Normal ImGui persistence is preserved and archived, not restored to fake
    # input equality. It does not change the already consumed startup settings.
    runtime_paths = set(ready["runtime_settings"])
    def without_runtime(value):
        return {**value, "workspace_inputs": {p: h for p, h in value["workspace_inputs"].items() if p not in runtime_paths}}
    stable = without_runtime(ready["inputs"]) == without_runtime(after)
    result = {"status": "PASS" if code == 0 and not forced and duration <= 300 and stable and application and application["shutdown"] and not application["technical_failure"] else "FAIL",
        "exit_code": code, "forced_termination": forced, "wall_seconds": duration, "completed_utc": now(),
        "application": application, "stable_inputs": stable, "off_on_equivalence_measured": False,
        "event_attribution": "PENDING_OFFLINE_ANALYSIS_AND_OWNER_CORRELATION",
        "evidence": {p.relative_to(output).as_posix(): sha(p) for p in output.rglob("*") if p.is_file()}}
    write_new(output / "result.json", result)
    print(json.dumps({"status": result["status"], "evidence": str(output), "usage": target_usage()}, indent=2))


ATTENDED = EVIDENCE / "owner-attended-continuation-01"
ATTENDED_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_19_MINIMUM_OWNER_ATTENDED_CAPTURE_CONTINUATION_ACCEPTANCE.json"
ATTENDED_FREEZE = ATTENDED / "frozen-patch-02.json"


def attended_authority():
    a = json.loads(ATTENDED_ACCEPTANCE.read_text())
    assert a["status"] == "ACCEPTED_WITH_CONTROLLING_OWNER_CLARIFICATIONS"
    for name in ("proposal", "qualitative_owner_observation"):
        assert sha(ROOT / a[name]["path"]) == a[name]["sha256"]
    assert a["scope"] == ["RigidBodySimulation/tests/RestingContactDiagnosis.h", "tools/pre_editor_resting_contact_diagnosis.py"]
    assert (a["native_launch_cap"], a["additional_native_launches"], a["per_process_seconds_limit"], a["simulated_seconds_limit"], a["native_seconds_limit"]) == (7, 1, 300, 30, 1200)
    assert (a["per_build_seconds_limit"], a["build_seconds_limit"]) == (1200, 2400)
    assert not a["unattended_launch_authorized"] and a["owner_presence_confirmation_after_prelaunch_handoff_required"]
    return a


def attended_inputs():
    value = target_inputs()
    value["workspace_inputs"][ATTENDED_ACCEPTANCE.relative_to(ROOT).as_posix()] = sha(ATTENDED_ACCEPTANCE)
    return value


def attended_guards():
    target_guards()
    a = attended_authority()
    start = json.loads((ATTENDED / "implementation-start.json").read_text())
    assert sha(ATTENDED_ACCEPTANCE) == start["acceptance"]["sha256"]
    current = target_inputs()
    before = start["before_inputs"]
    assert current["external_inputs"] == before["external_inputs"] and current["discovery"] == before["discovery"]
    assert set(current["workspace_inputs"]) == set(before["workspace_inputs"])
    for p, digest in before["workspace_inputs"].items():
        if p not in a["scope"]:
            assert current["workspace_inputs"][p] == digest, f"outside continuation ownership: {p}"
    assert target_usage() == start["usage"], "historical usage changed; reconcile before continuation"


def attended_usage():
    value = dict(target_usage())
    for inv in (ATTENDED / "builds").glob("*/invocation.json"):
        assert inv.with_name("result.json").exists(), "incomplete build: reconcile usage"
        value["build_seconds"] += json.loads(inv.with_name("result.json").read_text())["wall_seconds"]
    for inv in (ATTENDED / "runs").glob("*/invocation.json"):
        value["target_attempt_reservations"] += 1
        value["native_launches"] += int(inv.with_name("process.json").exists())
        if inv.with_name("result.json").exists():
            value["native_seconds"] += json.loads(inv.with_name("result.json").read_text())["wall_seconds"]
        else:
            value["incomplete_target_attempts"] += 1
    return value


def attended_frozen():
    frozen = json.loads(ATTENDED_FREEZE.read_text())
    for p, digest in frozen["sources"].items():
        assert sha(ROOT / p) == digest, f"frozen patch drift: {p}"
    for key in ("patch", "coverage", "build_input_reuse"):
        assert sha(ROOT / frozen[key]["path"]) == frozen[key]["sha256"]
    return frozen


def attended_keep(a, b, target, selected, dynamic):
    # Exact value-only counterpart of TargetSession::Retain, including partial ticks.
    return (a == target or b == target or (a == b and a in selected) or
            (a in selected and a in dynamic) or (b in selected and b in dynamic))


def attended_coverage():
    """One offline pass over the original trace. Never changes original evidence."""
    attended_guards()
    scan_path = TARGET / "analysis/capture-01/scan.json"
    scan = json.loads(scan_path.read_text())
    data = TARGET / "runs/Release-target-01/data"
    bindings = list(csv.DictReader((data / "initial.csv").open(newline="")))
    target = scan["target_identity"]["index"]
    dynamic = {int(b["index"]) for b in bindings if int(b["type"]) == 1}
    assert len(dynamic) == 180 and target in dynamic and TARGET_WIRE.size == 864
    head = struct.Struct("<6I")
    original_hash, retained_hash = hashlib.sha256(), hashlib.sha256()
    target_before, target_after = hashlib.sha256(), hashlib.sha256()
    before_required, after_required = hashlib.sha256(), hashlib.sha256()
    counts = Counter(); operations = Counter(); ticks = []
    prior = set()
    with (data / "trace.bin").open("rb") as f:
        magic = f.read(8); assert magic == b"P19T60V1"
        original_hash.update(magic); retained_hash.update(magic)
        for tick_text, span in scan["tick_offsets"].items():
            tick = int(tick_text)
            raw = f.read(span["records"] * 864)
            assert len(raw) == span["records"] * 864
            original_hash.update(raw)
            headers = [head.unpack_from(raw, i) for i in range(0, len(raw), 864)]
            direct = set()
            for kind, rt, stage, a, b, op in headers:
                assert rt == tick
                if a != b and target in (a, b) and (kind == 2 or (kind == 1 and (op <= 10 or op == 22))):
                    direct.add(b if a == target else a)
            selected = direct | prior | {target}
            kept = 0; stages = defaultdict(set)
            for i, (kind, rt, stage, a, b, op) in enumerate(headers):
                chunk = memoryview(raw)[i*864:(i+1)*864]
                # Independent obligation: target, dynamic focus endpoint, and
                # static focus self-state. All payloads retained byte for byte.
                required = tick == 0 or target in (a, b) or bool({a, b} & selected & dynamic) or (a == b and a in selected)
                keep = tick == 0 or attended_keep(a, b, target, selected, dynamic)
                assert keep == required
                counts["original"] += 1
                if required: before_required.update(chunk)
                if target in (a, b): target_before.update(chunk); counts["target"] += 1
                if keep:
                    retained_hash.update(chunk); after_required.update(chunk); kept += 1
                    if target in (a, b): target_after.update(chunk)
                    if kind == 0: stages[a].add(stage)
                    if kind == 1: operations[op] += 1
                else:
                    assert a != b and target not in (a, b) and not ({a, b} & selected & dynamic)
                    assert ({a, b} & selected) - dynamic, "dropped row was not incidental static-support coverage"
                    counts["excluded_static_incidental"] += 1
            if tick:
                for body in selected:
                    assert stages[body] == set(range(12)), (tick, body, stages[body])
            counts["retained"] += kept
            ticks.append({"tick": tick, "direct": sorted(direct), "prior": sorted(prior), "retained": kept, "original": len(headers)})
            prior = direct
            if tick and tick % 300 == 0: print(f"Offline retention coverage: tick {tick}", flush=True)
        assert not f.read(1)
    assert original_hash.hexdigest() == scan["raw_trace_sha256"]
    assert target_before.digest() == target_after.digest() and before_required.digest() == after_required.digest()
    assert counts["original"] == scan["records"] and counts["target"] == scan["target_records"]
    write_new(ATTENDED / "coverage.json", {"status": "PASS", "recorded_utc": now(), "kind": "OFFLINE_SELECTION_PROOF_ONLY",
        "source_hashes": {p: sha(ROOT / p) for p in attended_authority()["scope"]}, "source_trace_sha256": original_hash.hexdigest(),
        "counts": counts, "retained_bytes": 8 + counts["retained"] * 864, "original_bytes": 8 + counts["original"] * 864,
        "target_payload_sha256": target_after.hexdigest(), "required_payload_sha256": after_required.hexdigest(),
        "prospective_retained_trace_sha256": retained_hash.hexdigest(), "retained_operations": operations, "ticks": ticks,
        "coverage": "Every target row, every current/previous actual dynamic partner row including other collisions, all selected static self-state/stages; operation payloads and order unchanged. All 185 initial bindings retained.",
        "unchanged": "All-world scratch, pair/point tracking, stage capture, frame/draw/camera/bookmark/overhead accounting; only final record serialization predicate changes.",
        "limitations": "Offline byte selection does not measure new runtime cost or 60Hz off/on equivalence. Future new partners' history before first interaction is not captured beyond the original current/previous-tick rule. No owner event attribution."})
    print(json.dumps({"status": "PASS", "counts": counts, "retained_bytes": 8 + counts["retained"]*864}))


def attended_desktop():
    """Read-only launcher session/window-station/input-desktop proof on Windows."""
    import ctypes as c
    from ctypes import wintypes as w
    import sys
    k, u, wt = c.WinDLL("kernel32", use_last_error=True), c.WinDLL("user32", use_last_error=True), c.WinDLL("wtsapi32", use_last_error=True)
    k.ProcessIdToSessionId.argtypes = [w.DWORD, c.POINTER(w.DWORD)]
    u.GetProcessWindowStation.restype = w.HANDLE
    u.GetThreadDesktop.argtypes = [w.DWORD]; u.GetThreadDesktop.restype = w.HANDLE
    u.OpenInputDesktop.argtypes = [w.DWORD, w.BOOL, w.DWORD]; u.OpenInputDesktop.restype = w.HANDLE
    u.CloseDesktop.argtypes = [w.HANDLE]
    u.GetUserObjectInformationW.argtypes = [w.HANDLE, c.c_int, w.LPVOID, w.DWORD, c.POINTER(w.DWORD)]
    wt.WTSQuerySessionInformationW.argtypes = [w.HANDLE, w.DWORD, c.c_int, c.POINTER(w.LPVOID), c.POINTER(w.DWORD)]
    wt.WTSFreeMemory.argtypes = [w.LPVOID]
    def name(handle):
        out = c.create_unicode_buffer(256); size = w.DWORD()
        if not handle or not u.GetUserObjectInformationW(handle, 2, out, c.sizeof(out), c.byref(size)):
            raise c.WinError(c.get_last_error())
        return out.value
    session = w.DWORD()
    if not k.ProcessIdToSessionId(os.getpid(), c.byref(session)): raise c.WinError(c.get_last_error())
    state, size = w.LPVOID(), w.DWORD()
    if not wt.WTSQuerySessionInformationW(None, session.value, 8, c.byref(state), c.byref(size)): raise c.WinError(c.get_last_error())
    try: active = c.cast(state, c.POINTER(w.DWORD)).contents.value == 0
    finally: wt.WTSFreeMemory(state)
    desktop = u.OpenInputDesktop(0, False, 1)
    try: input_name = name(desktop)
    finally:
        if desktop: u.CloseDesktop(desktop)
    return {"pid": os.getpid(), "session": session.value, "active_session": active,
        "window_station": name(u.GetProcessWindowStation()), "thread_desktop": name(u.GetThreadDesktop(k.GetCurrentThreadId())),
        "input_desktop": input_name, "stdin_tty": sys.stdin.isatty(), "stdout_tty": sys.stdout.isatty(), "recorded_utc": now()}


def attended_desktop_ok(info):
    return bool(info["session"] != 0 and info["active_session"] and info["stdin_tty"] and info["stdout_tty"] and
                info["window_station"].casefold() == "winsta0" and info["thread_desktop"] == info["input_desktop"])


def attended_windows(pid):
    import ctypes as c
    from ctypes import wintypes as w
    u, k, dwm = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True), c.WinDLL("dwmapi")
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    u.EnumWindows.argtypes = [callback, w.LPARAM]
    u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    u.IsWindowVisible.argtypes = [w.HWND]; u.IsIconic.argtypes = [w.HWND]
    u.GetForegroundWindow.restype = w.HWND
    u.GetThreadDesktop.argtypes = [w.DWORD]; u.GetThreadDesktop.restype = w.HANDLE
    u.GetUserObjectInformationW.argtypes = [w.HANDLE, c.c_int, w.LPVOID, w.DWORD, c.POINTER(w.DWORD)]
    k.ProcessIdToSessionId.argtypes = [w.DWORD, c.POINTER(w.DWORD)]
    dwm.DwmGetWindowAttribute.argtypes = [w.HWND, w.DWORD, w.LPVOID, w.DWORD]
    session = w.DWORD()
    if not k.ProcessIdToSessionId(pid, c.byref(session)): raise c.WinError(c.get_last_error())
    rows = []
    @callback
    def visit(hwnd, unused):
        owner = w.DWORD(); thread = u.GetWindowThreadProcessId(hwnd, c.byref(owner))
        if owner.value == pid:
            title = c.create_unicode_buffer(512); u.GetWindowTextW(hwnd, title, 512)
            if title.value == "RigidBodySimulation":
                rect, cloaked, desk, needed = w.RECT(), w.DWORD(), c.create_unicode_buffer(256), w.DWORD()
                rect_ok = bool(u.GetWindowRect(hwnd, c.byref(rect)))
                cloak_result = dwm.DwmGetWindowAttribute(hwnd, 14, c.byref(cloaked), c.sizeof(cloaked))
                desk_ok = bool(u.GetUserObjectInformationW(u.GetThreadDesktop(thread), 2, desk, c.sizeof(desk), c.byref(needed)))
                rows.append({"hwnd": int(hwnd), "pid": pid, "thread": thread, "session": session.value, "title": title.value,
                    "desktop": desk.value if desk_ok else None, "visible": bool(u.IsWindowVisible(hwnd)), "minimized": bool(u.IsIconic(hwnd)),
                    "rect": [rect.left, rect.top, rect.right, rect.bottom] if rect_ok else None,
                    "cloaked": cloaked.value if cloak_result == 0 else None, "dwm_hresult": cloak_result,
                    "foreground": hwnd == u.GetForegroundWindow()})
        return True
    if not u.EnumWindows(visit, 0): raise c.WinError(c.get_last_error())
    return {"recorded_utc": now(), "pid": pid, "session": session.value, "windows": rows}


def attended_visible(info, desktop):
    return any(w["session"] == desktop["session"] and w["desktop"] == desktop["input_desktop"] and
               w["visible"] and not w["minimized"] and w["cloaked"] == 0 and w["rect"] and
               w["rect"][2] > w["rect"][0] and w["rect"][3] > w["rect"][1] for w in info["windows"])


def attended_ready_confirmation(confirmation, readiness, ready):
    assert confirmation["owner_present_and_ready"] is True
    assert confirmation["readiness_sha256"] == sha(readiness) and confirmation["owner_instruction"].strip()
    assert confirmation["recorded_utc"] >= ready["recorded_utc"]


def attended_static():
    attended_guards(); frozen = attended_frozen()
    # Physics/static/frame/F10 checks are reused: all production inputs are exact.
    # Exercise denial conditions without native Windows calls or any process.
    good = {"session": 3, "active_session": True, "stdin_tty": True, "stdout_tty": True,
            "window_station": "WinSta0", "thread_desktop": "Default", "input_desktop": "Default"}
    assert attended_desktop_ok(good)
    for key, bad in (("session", 0), ("active_session", False), ("stdin_tty", False), ("stdout_tty", False),
                     ("window_station", "Service"), ("input_desktop", "Winlogon")):
        assert not attended_desktop_ok({**good, key: bad})
    w = {"session": 3, "desktop": "Default", "visible": True, "minimized": False, "cloaked": 0, "rect": [0, 0, 1280, 720]}
    assert attended_visible({"windows": [w]}, good)
    assert not attended_visible({"windows": []}, good)
    for key, bad in (("session", 4), ("desktop", None), ("visible", False), ("minimized", True), ("cloaked", 1), ("cloaked", None), ("rect", None), ("rect", [0, 0, 0, 720])):
        assert not attended_visible({"windows": [{**w, key: bad}]}, good)
    assert attended_keep(26, 4, 26, {26, 4}, {26, 31})
    assert attended_keep(4, 4, 26, {26, 4}, {26, 31})
    assert not attended_keep(4, 22, 26, {26, 4}, {26, 31})
    assert attended_keep(31, 22, 26, {26, 4, 31}, {26, 31})
    old = (ATTENDED / "source-before/RigidBodySimulation/tests/RestingContactDiagnosis.h").read_text()
    new = (ROOT / "RigidBodySimulation/tests/RestingContactDiagnosis.h").read_text()
    begin = new.index("        bool Retain(const Record& row)")
    end = new.index("        static void Event(", begin)
    restored = new[:begin] + new[end:]
    restored = restored.replace("if (s.Retain(row)) s.WriteRecord(row);", "if (s.selected[row.a] || s.selected[row.b]) s.WriteRecord(row);")
    restored = restored.replace("if (Retain(row)) WriteRecord(row);", "if (row.a==target || row.b==target || direct[row.a] || direct[row.b] || prior[row.a] || prior[row.b]) WriteRecord(row);")
    assert restored == old, "change outside reviewed serialization predicate"
    old_static = TARGET / "static/check-04/result.json"
    output = target_fresh("static", "check", ATTENDED)
    write_new(output / "result.json", {"status": "PASS", "recorded_utc": now(), "inputs": attended_inputs(),
        "frozen_patch_sha256": sha(ATTENDED_FREEZE), "coverage": frozen["coverage"],
        "checks": ["Two-file ownership and production identity", "Exact C++ delta limited to final serialization predicate in complete and partial tick paths",
                   "Static ground not causal expansion root; dynamic partner other contacts preserved", "Session/desktop/console and window-state fail-closed negative cases"],
        "reuse": {"path": old_static.relative_to(ROOT).as_posix(), "sha256": sha(old_static), "reason": "All production/frame/F10/wire/identity providers unchanged; original static PASS remains historical"},
        "native_window_api_integration": "PENDING_OWNER_DESKTOP_RUN", "new_application_launches": 0,
        "limitations": "Native presentation, F10, actual cost and visible integration are not proved by offline checks."})
    print(json.dumps({"status": "PASS", "evidence": str(output)}))


def attended_prepare():
    attended_guards(); attended_frozen()
    inputs = attended_inputs()
    builds = {c: target_latest(c, inputs, ATTENDED) for c in ("Debug", "Release")}
    static = sorted((ATTENDED / "static").glob("*/result.json"))[-1]
    assert json.loads(static.read_text())["status"] == "PASS" and json.loads(static.read_text())["inputs"] == inputs
    usage = attended_usage()
    assert usage["native_launches"] == 6 and not usage["incomplete_target_attempts"] and 1200-usage["native_seconds"] >= 300
    assert not (ATTENDED / "runs").exists(), "unique continuation already reserved"
    cwd = contained(ROOT / "runtime/RigidBodySimulation/Release")
    executable = ROOT / "bin/Release/RigidBodySimulation/RigidBodySimulation.exe"
    output = target_fresh("prelaunch", "ready", ATTENDED)
    record = {"status": "PRE-LAUNCH READY; NOT PHASE APPROVAL READY", "recorded_utc": now(), "inputs": inputs, "builds": builds,
        "static": {"path": static.relative_to(ROOT).as_posix(), "sha256": sha(static)}, "frozen_patch_sha256": sha(ATTENDED_FREEZE),
        "command": [str(executable), "--targeted-entity-diagnostic", "wood_sphere158", "--output", str(ATTENDED / "runs/Release-attended-01/data")],
        "cwd": str(cwd), "runtime_settings": {p.relative_to(ROOT).as_posix(): sha(p) for p in [cwd / "imgui.ini"] if p.exists()},
        "usage": usage, "owner_ready_record": str(output / "owner-ready.json"), "required_owner_presence_after_handoff": True,
        "P19_INPUT_01": "Affected builds PASS; owner-visible Release integration PENDING", "P19_OBS_01": "OPEN; event, F10, causal attribution and representative pacing pending",
        "new_native_launches": 0, "off_on_60hz_equivalence": "NOT_MEASURED", "approval": "NOT_AUTHORIZED"}
    write_new(output / "readiness.json", record)
    print(json.dumps({"status": record["status"], "readiness": str(output / "readiness.json"), "usage": usage}, indent=2))


def attended_run(owner_ready):
    """Only the owner runs this in their visible interactive PowerShell console."""
    import msvcrt
    attended_guards(); attended_frozen()
    readiness = sorted((ATTENDED / "prelaunch").glob("*/readiness.json"))[-1]
    ready = json.loads(readiness.read_text())
    confirmation = json.loads(Path(owner_ready).read_text())
    attended_ready_confirmation(confirmation, readiness, ready)
    assert attended_inputs() == ready["inputs"]
    for c in ("Debug", "Release"): target_latest(c, ready["inputs"], ATTENDED)
    usage = attended_usage()
    assert usage["native_launches"] == 6 and not usage["incomplete_target_attempts"] and 1200-usage["native_seconds"] >= 300
    output = contained(ATTENDED / "runs/Release-attended-01")
    assert not output.exists(), "unique attempt already reserved; NO RETRY"
    desktop = attended_desktop()
    assert attended_desktop_ok(desktop), "not an active interactive owner desktop; no RBS created"
    probe = subprocess.run(["powershell", "-NoProfile", "-Command", "@(Get-Process -Name RigidBodySimulation -ErrorAction SilentlyContinue).Count"], capture_output=True, text=True, check=True)
    assert int(probe.stdout.strip()) == 0, "RBS is already open; do not intervene"
    print("One capture only. A centered 1280x720 RigidBodySimulation window should open.")
    print("Return here and press V only after YOU SEE the RBS window; press N if it is not visible.")
    print("Then return focus to RBS. Press F10 when you see the delayed hop; do not pause or edit Physics.")
    assert input("Type READY to confirm you are present on this visible desktop: ").strip() == "READY", "not confirmed; no launch"
    desktop = attended_desktop(); assert attended_desktop_ok(desktop)
    (output / "data").mkdir(parents=True)
    write_new(output / "invocation.json", {"command": ready["command"], "cwd": ready["cwd"], "started_utc": now(),
        "native_launch_number": 7, "owner_ready": confirmation, "console_ready": True, "desktop": desktop,
        "readiness_sha256": sha(readiness), "timeout_seconds": 300, "orderly_close_seconds": 270, "prior_usage": usage, "inputs": ready["inputs"]})
    started = time.monotonic(); forced = False; acknowledged = False; close_requested = False; reason = ""
    window_samples = []; error = None; process = None; code = None
    native_probe_seconds = 0.; native_probe_count = 0
    with (output / "console.log").open("wb") as log:
        try:
            process = subprocess.Popen(ready["command"], cwd=ready["cwd"], env=environment(), stdout=log, stderr=subprocess.STDOUT)
            write_new(output / "process.json", {"pid": process.pid, "created_utc": now(), "launcher": desktop})
            print("Capture started. Press V here when the RBS window is visibly present, or N if unavailable.", flush=True)
            while process.poll() is None:
                elapsed = time.monotonic()-started
                probe_start = time.monotonic()
                # The child may exit between poll and the read-only native query.
                try:
                    info = attended_windows(process.pid)
                except OSError:
                    if process.poll() is not None: break
                    raise
                native_probe_seconds += time.monotonic()-probe_start; native_probe_count += 1
                # Changes plus explicit acknowledgment; no inference from engine submissions.
                if not window_samples or info["windows"] != window_samples[-1]["windows"]:
                    window_samples.append(info)
                if msvcrt.kbhit():
                    key = msvcrt.getwch().lower()
                    if key == "v" and not acknowledged:
                        current_desktop = attended_desktop()
                        if attended_desktop_ok(current_desktop) and attended_visible(info, current_desktop):
                            acknowledged = True
                            write_new(output / "owner-visible.json", {"owner_acknowledged_visible": True, "recorded_utc": now(),
                                "elapsed_seconds": elapsed, "native": info, "desktop": current_desktop, "input": "V in owner's console after seeing RBS"})
                            print("Visibility acknowledged. Return to RBS and press F10 at the delayed hop.", flush=True)
                        else:
                            reason = "owner acknowledgment lacked matching visible native window"
                    elif key == "n": reason = "owner reports RBS window unavailable"
                if (reason or elapsed >= 270) and not close_requested:
                    close_requested = True
                    subprocess.run(["powershell", "-NoProfile", "-Command", f"(Get-Process -Id {process.pid} -ErrorAction SilentlyContinue).CloseMainWindow()"], stdout=log, stderr=log, timeout=min(5, max(.1, 300-elapsed)), check=False)
                if time.monotonic()-started >= 300:
                    forced = True; process.kill(); break
                time.sleep(.1)
            code = process.wait(timeout=5)
        except BaseException as exc:
            error = f"{type(exc).__name__}: {exc}"
            if process is not None and process.poll() is None:
                # Preserve unique failed attempt, close only our child, never retry.
                try:
                    subprocess.run(["powershell", "-NoProfile", "-Command", f"(Get-Process -Id {process.pid} -ErrorAction SilentlyContinue).CloseMainWindow()"], stdout=log, stderr=log, timeout=5, check=False)
                    code = process.wait(timeout=max(.1, min(5, 300-(time.monotonic()-started))))
                except (subprocess.TimeoutExpired, OSError):
                    forced = True; process.kill(); code = process.wait(timeout=5)
    duration = time.monotonic()-started
    write_new(output / "native-window.json", {"launcher": desktop, "samples": window_samples, "owner_acknowledged_visible": acknowledged, "error": error, "close_reason": reason,
        "native_probe_seconds": native_probe_seconds, "native_probe_count": native_probe_count,
        "measurement_limit": "External native-query elapsed time only; application recorder intervals/backlog/discard are in data/result.json and frames.csv; no off/on equivalence."})
    application = json.loads((output / "data/result.json").read_text()) if (output / "data/result.json").exists() else None
    for name in ("GEngine.log", "imgui.ini"):
        source = Path(ready["cwd"]) / name
        if source.exists(): shutil.copyfile(source, output / f"runtime-{name}")
    after = attended_inputs()
    runtime_paths = set(ready["runtime_settings"])
    def without_runtime(value):
        return {**value, "workspace_inputs": {p: h for p, h in value["workspace_inputs"].items() if p not in runtime_paths}}
    stable = without_runtime(after) == without_runtime(ready["inputs"])
    technical = bool(code == 0 and not forced and not error and not reason and duration <= 300 and stable and application and application["shutdown"] and not application["technical_failure"])
    result = {"status": "CAPTURED_PENDING_ANALYSIS" if technical and acknowledged else "INCONCLUSIVE",
        "technical_status": "PASS" if technical else "FAIL", "owner_visual_observation": "VISIBLE_ACKNOWLEDGED_EVENT_REVIEW_PENDING" if acknowledged else "INCONCLUSIVE / NOT PERFORMED",
        "wall_seconds": duration, "exit_code": code, "forced_termination": forced, "error": error, "completed_utc": now(), "stable_inputs": stable,
        "application": application, "off_on_equivalence_measured": False, "event_attribution": "PENDING_OWNER_REPORT_AND_CAUSAL_ANALYSIS",
        "evidence": {p.relative_to(output).as_posix(): sha(p) for p in output.rglob("*") if p.is_file()}}
    write_new(output / "result.json", result)
    print(json.dumps({"status": result["status"], "evidence": str(output), "usage": attended_usage()}, indent=2))
    print("No retry. Report whether you saw the hop, pressed F10, and any visible slowdown. Keep all evidence.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("static", "build", "run", "analyze", "target-static", "target-build", "target-prepare", "target-run", "attended-coverage", "attended-static", "attended-build", "attended-prepare", "attended-run"))
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    parser.add_argument("--observation", choices=("off", "on"))
    parser.add_argument("--owner-ready-record")
    args = parser.parse_args()
    if args.action == "attended-coverage":
        attended_coverage()
    elif args.action == "attended-static":
        attended_static()
    elif args.action == "attended-build":
        assert args.configuration
        target_build(args.configuration, attended=True)
    elif args.action == "attended-prepare":
        attended_prepare()
    elif args.action == "attended-run":
        assert args.owner_ready_record, "post-handoff owner readiness confirmation required"
        attended_run(args.owner_ready_record)
    elif args.action == "target-static":
        target_static()
    elif args.action == "target-build":
        assert args.configuration
        target_build(args.configuration)
    elif args.action == "target-prepare":
        target_prepare()
    elif args.action == "target-run":
        assert args.owner_ready_record, "owner readiness confirmation required"
        target_run(args.owner_ready_record)
    elif args.action == "static":
        static_checks()
    elif args.action == "analyze":
        analyze()
    elif args.action == "build":
        assert args.configuration
        build(args.configuration)
    else:
        assert args.configuration and args.observation
        run(args.configuration, args.observation)
