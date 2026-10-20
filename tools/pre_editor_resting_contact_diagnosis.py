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



# Phase20: the explicit acceptance below is the only replay authority. Historical
# Phase19 actions, evidence meanings and their finished budgets remain unchanged.
REPLAY = ROOT / "logs/pre_editor/phase-20/query-replay-01"
REPLAY_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_20_QUERY_REPLAY_ACCEPTANCE_01.json"
REPLAY_INPUT_HASH = "0285e8dd4303d7aee1c4ebc41a504734faccf4b1b2309faf3bfb3a854b719ee6"
REPLAY_TICKS = [566, *range(579, 591)]


def replay_authority():
    a = json.loads(REPLAY_ACCEPTANCE.read_text())
    assert a["status"] == "ACCEPTED_DIAGNOSTIC_ONLY_WITH_CONTROLLING_CLARIFICATIONS"
    for key in ("proposal", "input", "entry", "closure_provider"):
        assert sha(ROOT / a[key]["path"]) == a[key]["sha256"], key
    assert a["input"]["sha256"] == REPLAY_INPUT_HASH
    assert a["autonomous_native_execution_authorized"] and not a["owner_attendance_required_for_replay"]
    assert not a["remedy_implementation_authorized"] and not a["automatic_retry"]
    assert a["limits"] == {"cumulative_native_launches": 8, "new_native_launches": 1,
        "native_process_seconds": 60, "aggregate_native_seconds": 1200, "build_command_seconds": 1200,
        "aggregate_build_seconds": 2400, "snapshots": 13, "sap_pair_checks": 13, "top_level_queries": 26,
        "live_scene_ticks": 0, "additional_recorder_bytes": 4194304, "additional_export_bytes": 8388608}
    return a


def replay_guards():
    a = replay_authority(); e = json.loads((ROOT / a["entry"]["path"]).read_text())
    assert git("rev-parse", "HEAD").decode().strip() == a["baseline"]
    assert git("branch", "--show-current").decode().strip() == "pre-editor/refactor"
    assert not git("diff", "--cached", "--name-only").strip() and not git("ls-files", "--unmerged").strip()
    index = Path(git("rev-parse", "--git-path", "index").decode().strip())
    assert sha(index) == e["preserved_inputs"]["index_sha256"]
    assert not (ROOT / "pre-editor-checkpoints/phase-20.json").exists()
    assert not (index.parent / "pre-editor-approval/PHASE_20.json").exists()
    assert set(git("diff", "--name-only").decode().splitlines()) <= set(a["scope"])
    for path, digest in e["preserved_inputs"]["protected_sha256"].items(): assert sha(ROOT / path) == digest, path
    canonical = "RigidBodySimulation/src/RigidBodySimulation.cpp"
    assert sha(ROOT / canonical) == e["preserved_inputs"]["canonical_rbs_sha256"]
    for path, digest in a["before_source_hashes"].items(): assert sha(REPLAY / "baseline" / path) == digest, path
    contained(REPLAY); contained(ROOT / "runtime/tmp/phase-20")
    return a


def replay_inputs():
    a = replay_authority(); provider = json.loads((ROOT / a["closure_provider"]["path"]).read_text())
    old = provider["inputs"]
    # Reuse the established exact build/include/asset scopes, replacing historical
    # diagnostic governance with this phase's actual contract and protocol.
    paths = {p for p in old["workspace_inputs"] if not p.startswith("docs/pre_editor/")}
    paths.update(a["scope"])
    paths.update(["docs/pre_editor/VALIDATION_CONTRACTS.md", "docs/pre_editor/phases/PHASE_20.md",
        "docs/pre_editor/active/OUTPUT_PATHS.json", "docs/pre_editor/active/PHASE_18_PLUS_MINIMUM_SUFFICIENT_VALIDATION.md",
        "docs/pre_editor/active/PHASE_18_PLUS_MSV_ACTIVATION.json", REPLAY_ACCEPTANCE.relative_to(ROOT).as_posix(),
        a["input"]["path"], "logs/pre_editor/phase-20/query-replay-01/protocol.json"])
    external = {p: sha(p) for p in old["external_inputs"]}
    assert external == old["external_inputs"], "pinned toolchain/dependency changed"
    discovery = {}
    cpp = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".inc"}
    shaders = {".glsl", ".vert", ".frag", ".geom", ".tesc", ".tese", ".comp", ".shader"}
    extensions = {"immediate directories and C/C++/shader header/source extensions": cpp | shaders,
                  "immediate directories and C/C++ header/source extensions": cpp,
                  "immediate directories and .lib/.dll members": {".lib", ".dll"}}
    for scope, rule in provider["discovery_rules_by_scope"].items():
        assert rule == "immediate all members" or rule in extensions
        discovery[scope] = sorted(x.name for x in (ROOT / scope).iterdir()
            if rule == "immediate all members" or x.is_dir() or x.suffix.lower() in extensions[rule])
        assert discovery[scope] == old["discovery"][scope], f"relevant discovery drift: {scope}"
    return {"workspace_inputs": {p: sha(ROOT / p) for p in sorted(paths)}, "external_inputs": external, "discovery": discovery}


def replay_usage():
    a = replay_authority(); usage = dict(a["consumed"])
    for inv in (REPLAY / "builds").glob("*/invocation.json"):
        result = inv.with_name("result.json"); assert result.exists(), "unreconciled build attempt"
        usage["build_seconds"] += json.loads(result.read_text())["wall_seconds"]
    for inv in (REPLAY / "runs").glob("*/invocation.json"):
        result = inv.with_name("result.json"); assert result.exists(), "unreconciled unique native attempt"
        usage["native_seconds"] += json.loads(result.read_text())["wall_seconds"]
        usage["native_launches"] += int(inv.with_name("process.json").exists())
    return usage


def replay_request_text():
    a = replay_authority(); data = json.loads((ROOT / a["input"]["path"]).read_text())
    assert [q["historical_tick"] for q in data["snapshots"]] == REPLAY_TICKS
    fields = [*("p"+x for x in "xyz"), *("q"+x for x in "wxyz"), *("v"+x for x in "xyz"), *("w"+x for x in "xyz")]
    lines = ["P20_QUERY_REPLAY_V1", REPLAY_INPUT_HASH, "13 26 13 0 60"]
    for q in data["snapshots"]:
        v = q["target_before_narrowphase"]; assert v["sleeping"] == 0 and v["inactive"] == 0
        bits = [struct.unpack("<I", struct.pack("<f", v[k]))[0] for k in fields]
        lines.append(str(q["historical_tick"]) + " " + " ".join(f"{b:08x}" for b in bits))
    return "\n".join([*lines, "END", ""])


def replay_request_valid(text):
    # Python preparation check; native parser separately compares all float bits
    # with the compiled thirteen-state table. No native-parser PASS is inferred.
    return text.split() == replay_request_text().split()


def replay_environment():
    env = dict(os.environ)
    for key in ("GENGINE_ASSET_ROOT", "GENGINE_BASELINE_OUTPUT", "CL", "_CL_"):
        env.pop(key, None)
    temporary = contained(ROOT / "runtime/tmp/phase-20")
    temporary.mkdir(parents=True, exist_ok=True)
    env["TMP"] = env["TEMP"] = str(temporary)
    env["GIT_OPTIONAL_LOCKS"] = "0"
    env.pop("GENGINE_P20_QUERY_REPLAY_REQUEST", None)
    return env


def replay_strip(source, header=False):
    import re
    source = re.sub(r"// P20_QUERY_OBSERVATION_BEGIN.*?// P20_QUERY_OBSERVATION_END", "", source, flags=re.S)
    source = re.sub(r",\s*PhysicsDiagnostic::queryObserver\s*\?\s*&query(?:Contact|Closest)Diagnostics\s*:\s*nullptr", "", source)
    if header:
        source = source.replace('#include "GJK.h"', '').replace('#include "Contact.h"', '')
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
    return re.findall(r'"(?:\\.|[^"\\])*"|\w+|[^\s]', source)


def replay_static():
    import re
    a = replay_guards(); checks = {}
    for path in a["scope"][:2]:
        old = (REPLAY / "baseline" / path).read_text(); current = (ROOT / path).read_text()
        header = path.endswith("PhysicsProfile.h")
        checks[path] = replay_strip(old, header) == replay_strip(current, header)
        if not header:
            blocks = re.findall(r"// P20_QUERY_OBSERVATION_BEGIN(.*?)// P20_QUERY_OBSERVATION_END", current, flags=re.S)
            assert len(blocks) == 15
            assert not any(re.search(r"\b(?:ApplyImpulse\w*|WakeUp|TrySleep|Update|SetBodyPose)\s*\(", b) for b in blocks)
            assert current.count("PhysicsDiagnostic::queryObserver ? &query") == 2
    request = replay_request_text(); assert replay_request_valid(request)
    for changed in (request.replace(REPLAY_INPUT_HASH, "0"*64), request.replace("13 26 13 0 60", "13 28 13 0 60"),
                    request.replace("566 ", "567 ", 1), request.replace("END", ""), request+"extra"):
        assert not replay_request_valid(changed)
    header = (ROOT / a["scope"][2]).read_text()
    native = re.findall(r"\{(\d+), \{((?:0x[0-9a-f]+u(?:, )?)+)\}\}", header)
    expected = [line.split() for line in request.splitlines()[3:-1]]
    assert len(native) == len(expected) == 13
    for (tick, words), expected_row in zip(native, expected):
        assert tick == expected_row[0] and [int(v, 16) for v in re.findall(r"0x([0-9a-f]+)u", words)] == [int(v, 16) for v in expected_row[1:]]
    assert "return false;\n            }\n            // BaseApp also renders" in header
    assert "static_assert(sizeof(QueryReplay)<=4*1024*1024" in header
    assert "PD::queryObserver=nullptr;" in header and "result.bits=QueryReplay::ContactBits(contact)" in header
    assert all(checks.values()), checks
    inputs = replay_inputs(); output = target_fresh("candidates", "candidate", REPLAY)
    patch = git("diff", "--binary", "--", *a["scope"])
    (output / "changes.patch").write_bytes(patch)
    for path in a["scope"]:
        dest = contained(output / "source" / path); dest.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(ROOT / path, dest)
    (output / "replay-request.txt").write_text(request, encoding="ascii", newline="\n")
    record = {"status": "PASS_STATIC_FROZEN_DIAGNOSTIC", "recorded_utc": now(), "inputs": inputs,
        "source_hashes": {p: sha(ROOT / p) for p in a["scope"]}, "patch_sha256": sha(output / "changes.patch"),
        "request_sha256": sha(output / "replay-request.txt"), "original_physics_token_identity": checks,
        "native_table_matches_proposal": True, "request_negative_static_checks": 5,
        "reviewed_invariants": ["Diagnostic callbacks only copy into bounded preallocated records; no production query sink I/O/allocation",
            "Only original GJK optional diagnostics pointer arguments differ; original calculations, branches, returns and caps retained",
            "BeforeUpdate returns false on explicit replay branch without reaching Scene Update; no normal fallback on malformed request",
            "All19 contact scalar/feature words compared bitwise; both pointer roles verified before normalization",
            "Known positive-control mismatch is INCONCLUSIVE, not physical success; native zero ticks and unchanged live values required"],
        "usage": replay_usage(), "native_runs": 0, "human_visual_validation": False}
    write_new(output / "frozen.json", record)
    print(json.dumps({"status": record["status"], "evidence": str(output), "usage": record["usage"]}, indent=2))


def replay_frozen():
    records = sorted((REPLAY / "candidates").glob("candidate-*/frozen.json")); assert records, "freeze required"
    path = records[-1]; data = json.loads(path.read_text())
    assert data["status"] == "PASS_STATIC_FROZEN_DIAGNOSTIC"
    assert all(sha(ROOT / p) == h for p, h in data["source_hashes"].items()), "candidate changed after freeze"
    assert sha(path.with_name("changes.patch")) == data["patch_sha256"]
    assert sha(path.with_name("replay-request.txt")) == data["request_sha256"]
    assert replay_inputs() == data["inputs"], "relevant input closure changed"
    return path, data


def replay_build(configuration):
    a = replay_guards(); frozen_path, frozen = replay_frozen(); remaining = 2400 - replay_usage()["build_seconds"]
    assert remaining > 5, "build budget exhausted"
    output = target_fresh("builds", configuration, REPLAY)
    command = [str(MSBUILD), str(ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"), "/t:Build",
        f"/p:Configuration={configuration}", "/p:Platform=x64", "/m:2", "/nr:false", "/v:minimal"]
    limit = min(1200, remaining)
    write_new(output / "invocation.json", {"started_utc": now(), "command": command, "timeout_seconds": limit,
        "inputs": frozen["inputs"], "frozen": {"path": frozen_path.relative_to(ROOT).as_posix(), "sha256": sha(frozen_path)},
        "remaining_aggregate_seconds": remaining})
    started = time.monotonic(); timed_out = False
    with (output / "build.log").open("wb") as log:
        process = subprocess.Popen(command, cwd=ROOT, env=replay_environment(), stdout=log, stderr=subprocess.STDOUT)
        try: code = process.wait(timeout=limit-5)
        except subprocess.TimeoutExpired:
            timed_out = True
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=log, stderr=log, check=False)
            code = process.wait(timeout=5)
    duration = time.monotonic()-started
    stable = replay_inputs() == frozen["inputs"]
    products = [f"bin/{configuration}/RigidBodySimulation/RigidBodySimulation.exe", f"bin/{configuration}/GEngine/GEngine.lib",
        f"external/glad/bin/{configuration}/glad/glad.lib"]
    result = {"status": "PASS" if code == 0 and stable and not timed_out and duration <= limit else "FAIL",
        "configuration": configuration, "exit_code": code, "timed_out": timed_out, "stable_inputs": stable,
        "wall_seconds": duration, "completed_utc": now(), "log_sha256": sha(output / "build.log"),
        "products": {p: sha(ROOT / p) for p in products if (ROOT / p).exists()}}
    write_new(output / "result.json", result)
    print(json.dumps({**result, "evidence": str(output), "usage": replay_usage()}, indent=2))
    if result["status"] != "PASS": raise SystemExit(1)


def replay_latest_build(configuration, inputs):
    paths = sorted((REPLAY / "builds").glob(f"{configuration}-*/result.json")); assert paths
    p = paths[-1]; d = json.loads(p.read_text()); assert d["status"] == "PASS"
    assert json.loads(p.with_name("invocation.json").read_text())["inputs"] == inputs
    assert len(d["products"]) == 3 and all(sha(ROOT / x) == h for x, h in d["products"].items())
    return {"path": p.relative_to(ROOT).as_posix(), "sha256": sha(p), "products": d["products"]}


def replay_prepare():
    a = replay_guards(); frozen_path, frozen = replay_frozen(); usage = replay_usage()
    assert usage["native_launches"] == 7 and usage["native_seconds"] + 60 <= 1200
    assert not (REPLAY / "runs/Release-replay-01").exists(), "unique native attempt already reserved"
    builds = {c: replay_latest_build(c, frozen["inputs"]) for c in ("Debug", "Release")}
    executable = ROOT / "bin/Release/RigidBodySimulation/RigidBodySimulation.exe"
    output = target_fresh("prelaunch", "ready", REPLAY)
    write_new(output / "readiness.json", {"status": "READY_FOR_AUTHORIZED_AUTONOMOUS_QUERY_REPLAY",
        "recorded_utc": now(), "authority_sha256": sha(REPLAY_ACCEPTANCE), "inputs": frozen["inputs"], "builds": builds,
        "frozen": {"path": frozen_path.relative_to(ROOT).as_posix(), "sha256": sha(frozen_path)},
        "request_sha256": frozen["request_sha256"], "usage": usage,
        "command": [str(executable), "--targeted-entity-diagnostic", "wood_sphere158", "--output", str(REPLAY / "runs/Release-replay-01/data")],
        "cwd": str(ROOT / "runtime/RigidBodySimulation/Release"), "owner_visible_validation": False,
        "desktop_requirement": "Owner clarification4 permits supported local execution without attendance; no live simulation"})
    print(json.dumps({"status": "READY_FOR_AUTHORIZED_AUTONOMOUS_QUERY_REPLAY", "readiness": str(output / "readiness.json")}, indent=2))


def replay_run():
    import platform
    a = replay_guards(); frozen_path, frozen = replay_frozen()
    ready_paths = sorted((REPLAY / "prelaunch").glob("ready-*/readiness.json")); assert ready_paths
    readiness = ready_paths[-1]; ready = json.loads(readiness.read_text())
    assert ready["status"] == "READY_FOR_AUTHORIZED_AUTONOMOUS_QUERY_REPLAY" and ready["authority_sha256"] == sha(REPLAY_ACCEPTANCE)
    assert ready["inputs"] == frozen["inputs"] and ready["request_sha256"] == frozen["request_sha256"]
    for c in ("Debug", "Release"): assert replay_latest_build(c, frozen["inputs"]) == ready["builds"][c]
    usage = replay_usage(); assert usage["native_launches"] == 7 and usage["native_seconds"]+60 <= 1200
    assert os.name == "nt", "native Windows session required"
    probe = subprocess.run(["powershell", "-NoProfile", "-Command", "@(Get-Process -Name RigidBodySimulation -ErrorAction SilentlyContinue).Count"], capture_output=True, text=True, check=True)
    assert int(probe.stdout.strip()) == 0, "another RBS process is active; no intervention"
    output = contained(REPLAY / "runs/Release-replay-01"); assert not output.exists(), "unique attempt consumed/reserved; no retry"
    environment_record = {"platform": platform.platform(), "launcher_pid": os.getpid(), "owner_visible_validation": False}
    try: environment_record["desktop"] = attended_desktop()
    except OSError as error: environment_record["desktop_query_error"] = str(error)
    output.mkdir(parents=True); (output / "data").mkdir()
    request = output / "replay-request.txt"; shutil.copyfile(frozen_path.with_name("replay-request.txt"), request)
    assert sha(request) == ready["request_sha256"] and replay_request_valid(request.read_text())
    env = replay_environment(); env["GENGINE_ASSET_ROOT"] = str(ROOT / "bin/Release/assets")
    env["GENGINE_P20_QUERY_REPLAY_REQUEST"] = str(request)
    write_new(output / "invocation.json", {"command": ready["command"], "cwd": ready["cwd"], "started_utc": now(),
        "native_launch_number": 8, "limit_seconds": 60, "usage_before": usage, "readiness_sha256": sha(readiness),
        "request_sha256": sha(request), "environment": environment_record, "automatic_retry": False})
    started = time.monotonic(); timed_out = False; samples = []; sample_at = .25
    with (output / "console.log").open("wb") as log:
        process = subprocess.Popen(ready["command"], cwd=ready["cwd"], env=env, stdout=log, stderr=subprocess.STDOUT)
        write_new(output / "process.json", {"pid": process.pid, "created_utc": now(), "native_launch_number": 8})
        while process.poll() is None:
            elapsed = time.monotonic()-started
            if elapsed >= 55:
                timed_out = True
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=log, stderr=log, check=False)
                break
            if elapsed >= sample_at and len(samples)<2:
                try: samples.append(attended_windows(process.pid))
                except OSError as error: samples.append({"error": str(error)})
                sample_at += 1
            time.sleep(.1)
        code = process.wait(timeout=5)
    duration = time.monotonic()-started
    native_path = output / "data/replay-result.json"; application_path = output / "data/result.json"
    native = json.loads(native_path.read_text()) if native_path.exists() else None
    application = json.loads(application_path.read_text()) if application_path.exists() else None
    for name in ("GEngine.log", "imgui.ini"):
        source = Path(ready["cwd"]) / name
        if source.exists(): shutil.copyfile(source, output / ("runtime-"+name))
    stable = replay_inputs() == frozen["inputs"]
    exports = sum(p.stat().st_size for p in (output / "data").glob("replay-*") if p.is_file())
    passed = (code == 0 and not timed_out and duration <= 60 and stable and sha(request) == ready["request_sha256"] and
        native and native["technical_pass"] and native["queries"] == 26 and native["pair_checks"] == 13 and native["scene_ticks"] == 0 and
        native["exact_results_match"] and native["live_state_unchanged"] and native["clean_shutdown"] and not native["overflow"] and
        native["additional_recorder_bytes"] <= 4194304 and exports <= 8388608 and application and application["ticks"] == 0 and application["shutdown"])
    result = {"status": "PASS_REPLAY_INTEGRITY_CAUSAL_ANALYSIS_PENDING" if passed else "FAIL_OR_INCONCLUSIVE_NO_RETRY",
        "exit_code": code, "timed_out": timed_out, "wall_seconds": duration, "completed_utc": now(),
        "stable_inputs": stable, "native": native, "application": application, "environment": environment_record,
        "window_samples": samples, "owner_visible_validation": False, "replay_export_bytes": exports,
        "evidence": {p.relative_to(output).as_posix(): sha(p) for p in output.rglob("*") if p.is_file()}}
    write_new(output / "result.json", result)
    print(json.dumps({"status": result["status"], "native": native, "wall_seconds": duration, "evidence": str(output), "usage": replay_usage()}, indent=2))
    if not passed: raise SystemExit(1)

# The conditional remedy has its own accepted authority and destinations. These
# actions implement only its first, zero-tick diagnosis; they cannot launch the
# later control/candidate/attended processes or admit a production correction.
CONDITIONAL = ROOT / "logs/pre_editor/phase-20/conditional-remediation-01"
CONDITIONAL_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_20_CONDITIONAL_REMEDIATION_ACCEPTANCE_01.json"
CONDITIONAL_TICKS = [580, 589, 566, 579, *range(581, 589), 590]
CONDITIONAL_ENV = "GENGINE_P20_CONDITIONAL_DIAGNOSIS_REQUEST"
CONDITIONAL_REFERENCE_QUERIES = REPLAY / "runs/Release-replay-01/data/replay-queries.csv"


def conditional_authority():
    a = json.loads(CONDITIONAL_ACCEPTANCE.read_text(encoding="utf-8"))
    assert a["status"] == "ACCEPTED_CONDITIONAL_REMEDIATION_WITH_CONTROLLING_CLARIFICATIONS"
    assert a["proposal"]["accepted"] and sha(ROOT / a["proposal"]["path"]) == a["proposal"]["sha256"]
    assert a["root"] == CONDITIONAL.relative_to(ROOT).as_posix()
    assert set(a["source_ownership"]) == {
        "GEngine/include/GEngine/Physics/GJK.cpp", "GEngine/include/GEngine/Physics/PhysicsSystem.cpp",
        "GEngine/include/GEngine/Physics/PhysicsProfile.h", "RigidBodySimulation/tests/RestingContactDiagnosis.h",
        "tools/pre_editor_resting_contact_diagnosis.py"}
    assert a["budgets"] == {"native_launch_cap": 12, "native_launches_consumed": 8,
        "native_seconds_consumed": 802.532, "native_aggregate_seconds": 1200,
        "build_seconds_consumed": 1663.484, "build_aggregate_seconds": 2400,
        "build_per_command_seconds": 1200, "diagnosis_seconds": 60, "control_seconds": 120,
        "candidate_seconds": 120, "attended_seconds": 60, "automatic_retries": 0}
    assert not a["publication_authorized"] and not a["phase21_authorized"]
    return a


def conditional_guards():
    a = conditional_authority()
    baseline = json.loads((CONDITIONAL / "baseline.json").read_text(encoding="utf-8"))
    assert git("rev-parse", "HEAD").decode().strip() == a["baseline"] == baseline["baseline"]
    assert git("branch", "--show-current").decode().strip() == baseline["branch"] == "pre-editor/refactor"
    assert not git("diff", "--cached", "--name-only").strip() and not git("ls-files", "--unmerged").strip()
    index = Path(git("rev-parse", "--git-path", "index").decode().strip())
    assert sha(index) == baseline["raw_index_sha256"]
    assert sha(CONDITIONAL / "baseline/raw-index") == baseline["raw_index_sha256"]
    assert not (ROOT / "pre-editor-checkpoints/phase-20.json").exists()
    assert not (index.parent / "pre-editor-approval/PHASE_20.json").exists()
    assert set(git("diff", "--name-only").decode().splitlines()) <= set(a["source_ownership"])
    for path, digest in baseline["source_hashes"].items():
        assert sha(CONDITIONAL / "baseline" / path) == digest, path
    canonical = "RigidBodySimulation/src/RigidBodySimulation.cpp"
    assert sha(ROOT / canonical) == sha(CONDITIONAL / "baseline" / canonical) == baseline["canonical_rbs_sha256"]
    entry = json.loads((ROOT / baseline["reused_entry"]).read_text())
    for path, digest in entry["preserved_inputs"]["protected_sha256"].items():
        assert sha(ROOT / path) == digest, path
    preserved = baseline["preserved_diagnostic"]
    assert sha(ROOT / preserved["path"]) == preserved["sha256"]
    handoff = json.loads((ROOT / preserved["path"]).read_text())
    original_result = handoff["native"]["result"]
    assert sha(ROOT / original_result["path"]) == original_result["sha256"]
    original = json.loads((ROOT / original_result["path"]).read_text())
    assert sha(CONDITIONAL_REFERENCE_QUERIES) == original["evidence"]["data/replay-queries.csv"]
    protocol = json.loads((CONDITIONAL / "protocol.json").read_text())
    assert protocol["authority"]["sha256"] == sha(CONDITIONAL_ACCEPTANCE)
    assert protocol["proposal"] == a["proposal"] and protocol["budgets"] == a["budgets"]
    assert protocol["diagnostic"]["order"] == CONDITIONAL_TICKS
    output_paths = json.loads((ROOT / "docs/pre_editor/active/OUTPUT_PATHS.json").read_text())
    assert Path(output_paths["root"]).resolve() == ROOT
    contained(CONDITIONAL); contained(ROOT / "runtime/tmp/phase-20")
    return a


def conditional_inputs():
    a = conditional_authority()
    # Reuse the previous replay's precise build/include/asset membership, not a
    # repository-wide inventory or its historical governance as current policy.
    old = json.loads((REPLAY / "candidates/candidate-02/frozen.json").read_text())["inputs"]
    provider_ref = replay_authority()["closure_provider"]
    provider = json.loads((ROOT / provider_ref["path"]).read_text())
    paths = {p for p in old["workspace_inputs"] if not p.startswith(("docs/pre_editor/", "logs/pre_editor/"))}
    paths.update(a["source_ownership"])
    paths.update(["docs/pre_editor/VALIDATION_CONTRACTS.md", "docs/pre_editor/phases/PHASE_20.md",
        "docs/pre_editor/active/OUTPUT_PATHS.json", "docs/pre_editor/active/PHASE_18_PLUS_MINIMUM_SUFFICIENT_VALIDATION.md",
        "docs/pre_editor/active/PHASE_18_PLUS_MSV_ACTIVATION.json", a["proposal"]["path"],
        CONDITIONAL_ACCEPTANCE.relative_to(ROOT).as_posix(),
        (CONDITIONAL / "baseline.json").relative_to(ROOT).as_posix(),
        (CONDITIONAL / "protocol.json").relative_to(ROOT).as_posix(),
        CONDITIONAL_REFERENCE_QUERIES.relative_to(ROOT).as_posix(), replay_authority()["input"]["path"]])
    external = {p: sha(p) for p in old["external_inputs"]}
    assert external == old["external_inputs"], "pinned toolchain/dependency changed"
    cpp = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".inc"}
    shaders = {".glsl", ".vert", ".frag", ".geom", ".tesc", ".tese", ".comp", ".shader"}
    extensions = {"immediate directories and C/C++/shader header/source extensions": cpp | shaders,
        "immediate directories and C/C++ header/source extensions": cpp,
        "immediate directories and .lib/.dll members": {".lib", ".dll"}}
    discovery = {}
    for scope, rule in provider["discovery_rules_by_scope"].items():
        assert rule == "immediate all members" or rule in extensions
        discovery[scope] = sorted(x.name for x in (ROOT / scope).iterdir()
            if rule == "immediate all members" or x.is_dir() or x.suffix.lower() in extensions[rule])
        assert discovery[scope] == old["discovery"][scope], f"relevant discovery drift: {scope}"
    return {"workspace_inputs": {p: sha(ROOT / p) for p in sorted(paths)},
        "external_inputs": external, "discovery": discovery}


def conditional_usage():
    budgets = conditional_authority()["budgets"]
    usage = {"build_seconds": budgets["build_seconds_consumed"],
        "native_seconds": budgets["native_seconds_consumed"], "native_launches": budgets["native_launches_consumed"]}
    for kind, field in (("builds", "build_seconds"), ("runs", "native_seconds")):
        for invocation in (CONDITIONAL / kind).glob("*/invocation.json"):
            result = invocation.with_name("result.json")
            assert result.exists(), f"unreconciled {kind} attempt: {invocation}"
            usage[field] += json.loads(result.read_text())["wall_seconds"]
            if kind == "runs":
                # The accepted unique slot is consumed by an attempted launch,
                # including a launch error; absence of a PID never grants retry.
                usage["native_launches"] += 1
    return usage


def conditional_request_text():
    data = json.loads((ROOT / replay_authority()["input"]["path"]).read_text())
    snapshots = {row["historical_tick"]: row for row in data["snapshots"]}
    assert set(snapshots) == set(CONDITIONAL_TICKS) and len(snapshots) == 13
    fields = [*("p"+x for x in "xyz"), *("q"+x for x in "wxyz"), *("v"+x for x in "xyz"), *("w"+x for x in "xyz")]
    lines = ["P20_CONDITIONAL_DIAGNOSIS_V1", REPLAY_INPUT_HASH, "13 26 13 0 60"]
    for tick in CONDITIONAL_TICKS:
        values = snapshots[tick]["target_before_narrowphase"]
        assert values["sleeping"] == 0 and values["inactive"] == 0
        words = [struct.unpack("<I", struct.pack("<f", values[key]))[0] for key in fields]
        lines.append(str(tick) + " " + " ".join(f"{word:08x}" for word in words))
    return "\n".join([*lines, "END", ""])


def conditional_environment():
    env = replay_environment()
    env.pop(CONDITIONAL_ENV, None)
    return env


def conditional_request_valid(text):
    # Independent preparation-side schema/input check. Native request parsing is
    # exercised only by the one authorized process and is never claimed here.
    fields = text.split()
    expected = conditional_request_text().split()
    if len(fields) != 3 + 4 + 13 * 14 + 1 or fields[:2] != expected[:2]:
        return False
    try:
        if [int(x, 10) for x in fields[2:7]] != [13, 26, 13, 0, 60]:
            return False
        for row, tick in enumerate(CONDITIONAL_TICKS):
            start = 7 + row * 14
            if int(fields[start], 10) != tick:
                return False
            words = [int(value, 16) for value in fields[start+1:start+14]]
            if any(word < 0 or word > 0xffffffff for word in words):
                return False
            if words != [int(value, 16) for value in expected[start+1:start+14]]:
                return False
        return fields[-1] == "END"
    except ValueError:
        return False


def conditional_data_integrity(directory, native):
    # Structural/identity checks only. Geometric causal admission remains a
    # separate offline decision on these preserved raw operands.
    checks = {}
    try:
        projection_header = ["snapshot", "point_count", "face_count", "selected_face", "epa_iterations", "convergence_reason",
            "failed_predicates", "overflow", "bias_bits", "origin_distance_bits", "lambda_sum_bits",
            "projection_error_squared_bits", "projection_scale_squared_bits"]
        projection_header += [f"{name}_{axis}_bits" for name in ("normal", "lambdas", "projected", "expected",
            "last_support_xyz", "last_support_a", "last_support_b", "last_support_normal") for axis in "xyz"]
        projection_header += ["last_origin_distance_bits", "last_expansion_distance_bits", "last_tolerance_bits", "last_selected_face",
            "bary_exit", "bary_valid_fields", "bary_normal_squared_bits", "bary_area_bits", "bary_axis"]
        projection_header += [f"{name}_{axis}_bits" for name in ("bary_projected", "bary_subareas", "bary_raw_weights", "bary_returned_weights") for axis in "xyz"]
        projection_header += ["bary_matched"]
        headers = {"epa-projections.csv": projection_header,
            "epa-points.csv": ["snapshot", "point", *(f"{name}_{axis}_bits" for name in ("xyz", "a", "b") for axis in "xyz")],
            "epa-faces.csv": ["snapshot", "face", "a", "b", "c"],
            "epa-attempts.csv": ["attempt", "tick", "iteration", "snapshot"],
            "replay-queries.csv": ["tick", "mode", "hit", "pointer_roles_ok", "clones_unchanged", "elapsed_ns", "observer_ns",
                *(f"contact_bits_{i}" for i in range(19))]}
        def rows(name):
            with (directory / name).open(newline="", encoding="utf-8") as stream:
                reader = csv.DictReader(stream)
                assert reader.fieldnames == headers[name], f"header: {name}"
                values = list(reader)
                assert all(None not in row and all(value not in (None, "") for value in row.values()) for row in values), name
                return values
        with CONDITIONAL_REFERENCE_QUERIES.open(newline="", encoding="utf-8") as stream:
            reference_rows = list(csv.DictReader(stream))
        reference = {int(row["tick"]): row for row in reference_rows if row["mode"] == "0"}
        answers = rows("replay-queries.csv")
        assert [(int(row["tick"]), int(row["mode"])) for row in answers] == [(tick, mode) for tick in CONDITIONAL_TICKS for mode in (0, 1)]
        words = ["hit", *(f"contact_bits_{i}" for i in range(19))]
        checks["preserved_query_words"] = all(all(int(row[key]) == int(reference[int(row["tick"])][key]) for key in words) for row in answers)
        checks["all_pointer_roles_and_clones"] = all(row["pointer_roles_ok"] == row["clones_unchanged"] == "1" for row in answers)
        checks["positive_controls_exact"] = all(all(int(row[key]) == int(reference[int(row["tick"])][key]) for key in words)
            for row in answers if int(row["tick"]) in (566, 589))
        projections, points, faces, attempts = (rows("epa-"+name+".csv") for name in ("projections", "points", "faces", "attempts"))
        assert 0 < len(projections) == native["epa_snapshots"] <= len(attempts) == native["epa_attempts"] <= 143
        assert [int(row["snapshot"]) for row in projections] == list(range(len(projections)))
        assert [int(row["attempt"]) for row in attempts] == list(range(len(attempts)))
        assert {int(row["tick"]) for row in attempts} == set(CONDITIONAL_TICKS)
        for tick in CONDITIONAL_TICKS:
            per_tick = [row for row in attempts if int(row["tick"]) == tick]
            assert [int(row["iteration"]) for row in per_tick] == list(range(1, len(per_tick)+1))
            assert len(per_tick) <= 11
        assert all(0 <= int(row["snapshot"]) < len(projections) for row in attempts)
        for row in projections:
            index = int(row["snapshot"]); point_count, face_count = int(row["point_count"]), int(row["face_count"])
            assert 4 <= point_count <= 68 and 4 <= face_count <= 256
            assert 0 <= int(row["selected_face"]) < face_count and 0 <= int(row["last_selected_face"]) < face_count
            assert row["overflow"] == "0" and row["bary_matched"] == "1"
            assert int(row["convergence_reason"]) in (1, 2) and 1 <= int(row["epa_iterations"]) <= 64
            assert 0 <= int(row["failed_predicates"]) <= 255 and int(row["bary_valid_fields"]) in (1, 15, 31)
            assert all(0 <= int(value) <= 0xffffffff for key, value in row.items() if key.endswith("_bits"))
            snapshot_points = [p for p in points if int(p["snapshot"]) == index]
            snapshot_faces = [f for f in faces if int(f["snapshot"]) == index]
            assert [int(p["point"]) for p in snapshot_points] == list(range(point_count))
            assert [int(f["face"]) for f in snapshot_faces] == list(range(face_count))
            assert all(0 <= int(value) <= 0xffffffff for p in snapshot_points for key, value in p.items() if key.endswith("_bits"))
            assert all(0 <= int(f[key]) < point_count for f in snapshot_faces for key in ("a", "b", "c"))
        assert all(0 <= int(row["snapshot"]) < len(projections) for row in [*points, *faces])
        checks["complete_epa_snapshot_and_attempt_structure"] = True
        checks["causal_admission_performed"] = False
        return all(value for key, value in checks.items() if key != "causal_admission_performed"), checks
    except (OSError, ValueError, KeyError, TypeError, AssertionError, csv.Error) as error:
        checks["structural_failure"] = f"{type(error).__name__}: {error}"
        return False, checks


def conditional_strip(source):
    import re
    begin, end = "// P20_EPA_OBSERVATION_BEGIN", "// P20_EPA_OBSERVATION_END"
    assert source.count(begin) == source.count(end)
    source = re.sub(re.escape(begin) + ".*?" + re.escape(end), "", source, flags=re.S)
    source = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
    return re.findall(r'"(?:\\.|[^"\\])*"|\w+|[^\s]', source)


def conditional_static():
    import re
    a = conditional_guards()
    checks = {}
    for path in ("GEngine/include/GEngine/Physics/GJK.cpp", "GEngine/include/GEngine/Physics/PhysicsProfile.h"):
        before = (CONDITIONAL / "baseline" / path).read_text()
        current = (ROOT / path).read_text()
        assert "// P20_EPA_OBSERVATION_BEGIN" in current, f"missing bounded observation: {path}"
        checks[path] = conditional_strip(before) == conditional_strip(current)
    physics = "GEngine/include/GEngine/Physics/PhysicsSystem.cpp"
    checks[physics] = sha(ROOT / physics) == sha(CONDITIONAL / "baseline" / physics)
    runner = "tools/pre_editor_resting_contact_diagnosis.py"
    before_runner = (CONDITIONAL / "baseline" / runner).read_text().split('if __name__ == "__main__":', 1)[0]
    checks["historical_runner_function_prefix"] = (ROOT / runner).read_text().startswith(before_runner)
    request = conditional_request_text()
    assert request.splitlines()[3].startswith("580 ") and request.splitlines()[4].startswith("589 ")
    malformed = [request.replace(REPLAY_INPUT_HASH, "0"*64), request.replace("13 26 13 0 60", "13 28 13 0 60"),
        request.replace("580 ", "581 ", 1), request.replace("END", ""), request+"extra"]
    assert conditional_request_valid(request) and all(not conditional_request_valid(text) for text in malformed)
    fixture = (ROOT / "RigidBodySimulation/tests/RestingContactDiagnosis.h").read_text()
    table = re.findall(r"\{(\d+), \{((?:0x[0-9a-f]+u(?:, )?)+)\}\}", fixture)
    native_rows = {int(tick): [int(v, 16) for v in re.findall(r"0x([0-9a-f]+)u", words)] for tick, words in table}
    expected = {int(line.split()[0]): [int(v, 16) for v in line.split()[1:]] for line in request.splitlines()[3:-1]}
    assert native_rows == expected, "native preserved input table differs"
    assert CONDITIONAL_ENV in fixture and "P20_CONDITIONAL_DIAGNOSIS_V1" in fixture
    assert all(checks.values()), checks
    inputs = conditional_inputs()
    output = target_fresh("candidates", "diagnosis", CONDITIONAL)
    patch = git("diff", "--binary", "--", *a["source_ownership"])
    (output / "changes.patch").write_bytes(patch)
    for path in a["source_ownership"]:
        dest = contained(output / "source" / path); dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / path, dest)
    (output / "diagnosis-request.txt").write_text(request, encoding="ascii", newline="\n")
    record = {"status": "PASS_STATIC_FROZEN_CONDITIONAL_DIAGNOSIS", "recorded_utc": now(),
        "authority_sha256": sha(CONDITIONAL_ACCEPTANCE), "inputs": inputs,
        "source_hashes": {p: sha(ROOT / p) for p in a["source_ownership"]},
        "patch_sha256": sha(output / "changes.patch"), "request_sha256": sha(output / "diagnosis-request.txt"),
        "original_physics_token_identity": checks, "ordered_ticks": CONDITIONAL_TICKS,
        "python_request_schema_negative_checks": 5, "native_parser_negative_tests": "NOT_RUN",
        "native_runs": 0, "usage": conditional_usage(),
        "stage": "diagnosis only; functional fixture/control readiness and causal admission not claimed",
        "limits": {"queries": 26, "pair_checks": 13, "scene_ticks": 0, "process_seconds": 60,
            "epa_points_per_snapshot": 68, "epa_faces_per_snapshot": 256,
            "additional_recorder_bytes": 4194304, "additional_export_bytes": 8388608}}
    write_new(output / "frozen.json", record)
    print(json.dumps({"status": record["status"], "evidence": str(output), "usage": record["usage"]}, indent=2))


def conditional_frozen():
    records = sorted((CONDITIONAL / "candidates").glob("diagnosis-*/frozen.json"))
    assert records, "diagnostic freeze required"
    path = records[-1]; data = json.loads(path.read_text())
    assert data["status"] == "PASS_STATIC_FROZEN_CONDITIONAL_DIAGNOSIS"
    assert data["authority_sha256"] == sha(CONDITIONAL_ACCEPTANCE)
    assert all(sha(ROOT / p) == digest for p, digest in data["source_hashes"].items()), "frozen diagnostic source changed"
    assert sha(path.with_name("changes.patch")) == data["patch_sha256"]
    assert sha(path.with_name("diagnosis-request.txt")) == data["request_sha256"]
    assert conditional_inputs() == data["inputs"], "relevant diagnostic closure changed"
    return path, data


def conditional_build(configuration):
    conditional_guards(); frozen_path, frozen = conditional_frozen()
    remaining = 2400 - conditional_usage()["build_seconds"]
    assert remaining > 5, "build budget exhausted"
    output = target_fresh("builds", configuration+"-diagnosis", CONDITIONAL)
    command = [str(MSBUILD), str(ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"), "/t:Build",
        f"/p:Configuration={configuration}", "/p:Platform=x64", "/m:2", "/nr:false", "/v:minimal"]
    limit = min(1200, remaining)
    env = conditional_environment()
    write_new(output / "invocation.json", {"started_utc": now(), "command": command, "timeout_seconds": limit,
        "inputs": frozen["inputs"], "frozen": {"path": frozen_path.relative_to(ROOT).as_posix(), "sha256": sha(frozen_path)},
        "remaining_aggregate_seconds": remaining, "stage": "diagnosis"})
    started = time.monotonic(); timed_out = False; code = None; launch_error = None
    with (output / "build.log").open("wb") as log:
        try:
            process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
            try: code = process.wait(timeout=limit-5)
            except subprocess.TimeoutExpired:
                timed_out = True
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=log, stderr=log, check=False)
                code = process.wait(timeout=5)
        except OSError as error:
            launch_error = str(error)
    duration = time.monotonic()-started
    stable = conditional_inputs() == frozen["inputs"]
    products = [f"bin/{configuration}/RigidBodySimulation/RigidBodySimulation.exe", f"bin/{configuration}/GEngine/GEngine.lib",
        f"external/glad/bin/{configuration}/glad/glad.lib"]
    result = {"status": "PASS" if code == 0 and stable and not timed_out and duration <= limit else "FAIL",
        "configuration": configuration, "exit_code": code, "launch_error": launch_error, "timed_out": timed_out,
        "stable_inputs": stable, "wall_seconds": duration, "completed_utc": now(), "log_sha256": sha(output / "build.log"),
        "products": {p: sha(ROOT / p) for p in products if (ROOT / p).exists()}}
    write_new(output / "result.json", result)
    print(json.dumps({**result, "evidence": str(output), "usage": conditional_usage()}, indent=2))
    if result["status"] != "PASS": raise SystemExit(1)


def conditional_latest_build(configuration, inputs):
    paths = sorted((CONDITIONAL / "builds").glob(configuration+"-diagnosis-*/result.json"))
    assert paths, f"missing affected {configuration} diagnosis build"
    path = paths[-1]; data = json.loads(path.read_text())
    assert data["status"] == "PASS" and json.loads(path.with_name("invocation.json").read_text())["inputs"] == inputs
    assert len(data["products"]) == 3 and all(sha(ROOT / p) == digest for p, digest in data["products"].items())
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path), "products": data["products"]}


def conditional_prepare():
    conditional_guards(); frozen_path, frozen = conditional_frozen(); usage = conditional_usage()
    assert usage["native_launches"] == 8 and usage["native_seconds"] + 60 <= 1200
    assert not (CONDITIONAL / "runs/Release-diagnosis-01").exists(), "unique diagnostic slot reserved/consumed"
    builds = {c: conditional_latest_build(c, frozen["inputs"]) for c in ("Debug", "Release")}
    output = target_fresh("prelaunch", "diagnosis-ready", CONDITIONAL)
    write_new(output / "readiness.json", {"status": "READY_FOR_AUTHORIZED_CONDITIONAL_DIAGNOSIS",
        "recorded_utc": now(), "authority_sha256": sha(CONDITIONAL_ACCEPTANCE), "inputs": frozen["inputs"], "builds": builds,
        "frozen": {"path": frozen_path.relative_to(ROOT).as_posix(), "sha256": sha(frozen_path)},
        "request_sha256": frozen["request_sha256"], "usage": usage,
        "command": [str(ROOT / "bin/Release/RigidBodySimulation/RigidBodySimulation.exe"),
            "--targeted-entity-diagnostic", "wood_sphere158", "--output", str(CONDITIONAL / "runs/Release-diagnosis-01/data")],
        "cwd": str(ROOT / "runtime/RigidBodySimulation/Release"), "owner_visible_validation": False,
        "desktop_requirement": "Supported local Codex Windows session; zero live ticks, no owner-visible assessment",
        "later_control_candidate_attended_authority": "Not selected by this action"})
    print(json.dumps({"status": "READY_FOR_AUTHORIZED_CONDITIONAL_DIAGNOSIS", "readiness": str(output / "readiness.json")}, indent=2))


def conditional_run():
    import platform
    conditional_guards(); frozen_path, frozen = conditional_frozen()
    paths = sorted((CONDITIONAL / "prelaunch").glob("diagnosis-ready-*/readiness.json")); assert paths
    readiness = paths[-1]; ready = json.loads(readiness.read_text())
    assert ready["status"] == "READY_FOR_AUTHORIZED_CONDITIONAL_DIAGNOSIS"
    assert ready["authority_sha256"] == sha(CONDITIONAL_ACCEPTANCE) and ready["inputs"] == frozen["inputs"]
    assert ready["request_sha256"] == frozen["request_sha256"]
    for c in ("Debug", "Release"): assert conditional_latest_build(c, frozen["inputs"]) == ready["builds"][c]
    usage = conditional_usage(); assert usage["native_launches"] == 8 and usage["native_seconds"]+60 <= 1200
    assert os.name == "nt", "supported native Windows session required"
    probe = subprocess.run(["powershell", "-NoProfile", "-Command", "@(Get-Process -Name RigidBodySimulation -ErrorAction SilentlyContinue).Count"],
        capture_output=True, text=True, check=True)
    assert int(probe.stdout.strip()) == 0, "another RBS process is active; no intervention"
    output = contained(CONDITIONAL / "runs/Release-diagnosis-01")
    assert not output.exists(), "unique diagnostic attempt consumed/reserved; no retry"
    environment_record = {"platform": platform.platform(), "launcher_pid": os.getpid(), "owner_visible_validation": False}
    try: environment_record["desktop"] = attended_desktop()
    except OSError as error: environment_record["desktop_query_error"] = str(error)
    env = conditional_environment(); env["GENGINE_ASSET_ROOT"] = str(ROOT / "bin/Release/assets")
    output.mkdir(parents=True); (output / "data").mkdir()
    request = output / "diagnosis-request.txt"; shutil.copyfile(frozen_path.with_name("diagnosis-request.txt"), request)
    assert sha(request) == ready["request_sha256"] and conditional_request_valid(request.read_text())
    env[CONDITIONAL_ENV] = str(request)
    write_new(output / "invocation.json", {"command": ready["command"], "cwd": ready["cwd"], "started_utc": now(),
        "native_launch_number": 9, "limit_seconds": 60, "usage_before": usage, "readiness_sha256": sha(readiness),
        "request_sha256": sha(request), "environment": environment_record, "automatic_retry": False})
    started = time.monotonic(); timed_out = False; code = None; launch_error = None; samples = []; sample_at = .25
    with (output / "console.log").open("wb") as log:
        try:
            process = subprocess.Popen(ready["command"], cwd=ready["cwd"], env=env, stdout=log, stderr=subprocess.STDOUT)
            write_new(output / "process.json", {"pid": process.pid, "created_utc": now(), "native_launch_number": 9})
            while process.poll() is None:
                elapsed = time.monotonic()-started
                if elapsed >= 55:
                    timed_out = True
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=log, stderr=log, check=False)
                    break
                if elapsed >= sample_at and len(samples) < 2:
                    try: samples.append(attended_windows(process.pid))
                    except OSError as error: samples.append({"error": str(error)})
                    sample_at += 1
                time.sleep(.1)
            code = process.wait(timeout=5)
        except OSError as error:
            launch_error = str(error)
    duration = time.monotonic()-started
    native_path = output / "data/replay-result.json"; application_path = output / "data/result.json"
    native = json.loads(native_path.read_text()) if native_path.exists() else None
    application = json.loads(application_path.read_text()) if application_path.exists() else None
    for name in ("GEngine.log", "imgui.ini"):
        source = Path(ready["cwd"]) / name
        if source.exists(): shutil.copyfile(source, output / ("runtime-"+name))
    stable = conditional_inputs() == frozen["inputs"]
    evidence_files = [p for p in (output / "data").iterdir() if p.is_file() and p.name.startswith(("replay-", "epa-"))]
    exports = sum(p.stat().st_size for p in evidence_files)
    required = ("epa-projections.csv", "epa-points.csv", "epa-faces.csv", "epa-attempts.csv")
    complete_exports = all((output / "data" / name).is_file() for name in required)
    data_pass, data_checks = conditional_data_integrity(output / "data", native) if native else (False, {"native_result": "MISSING"})
    passed = bool(code == 0 and not timed_out and duration <= 60 and stable and sha(request) == ready["request_sha256"] and
        native and native["technical_pass"] and native["queries"] == 26 and native["pair_checks"] == 13 and native["scene_ticks"] == 0 and
        native["exact_results_match"] and native["live_state_unchanged"] and native["clean_shutdown"] and not native["overflow"] and
        native["conditional_diagnosis"] and not native["epa_overflow"] and not native["epa_missing_barycentric"] and
        0 < native["epa_snapshots"] <= native["epa_attempts"] <= 143 and native["additional_recorder_bytes"] <= 4194304 and
        complete_exports and data_pass and exports <= 8388608 and application and application["ticks"] == 0 and application["shutdown"])
    result = {"status": "PASS_DIAGNOSIS_INTEGRITY_CAUSAL_ADMISSION_PENDING" if passed else "FAIL_OR_INCONCLUSIVE_NO_RETRY",
        "exit_code": code, "launch_error": launch_error, "timed_out": timed_out, "wall_seconds": duration,
        "completed_utc": now(), "stable_inputs": stable, "native": native, "application": application,
        "environment": environment_record, "window_samples": samples, "owner_visible_validation": False,
        "diagnosis_export_bytes": exports, "required_exports_present": complete_exports, "data_integrity": data_checks,
        "evidence": {p.relative_to(output).as_posix(): sha(p) for p in output.rglob("*") if p.is_file()}}
    write_new(output / "result.json", result)
    print(json.dumps({"status": result["status"], "native": native, "wall_seconds": duration,
        "evidence": str(output), "usage": conditional_usage()}, indent=2))
    if not passed: raise SystemExit(1)


FOCUSED_ENV = "GENGINE_P20_CONDITIONAL_FOCUSED_REQUEST"
FOCUSED_ADMISSION = CONDITIONAL / "analysis/diagnosis-01/causal-admission.json"
FOCUSED_LATTICE_HASH = "79eb5a76b06726b77333fff93ddb47999671df025ef1c75322494b8ac62116c6"
FOCUSED_LATTICE = ROOT / "tools/pre_editor_resting_contact_lattice.txt"
FOCUSED_DURATIONS = (1800, 300, 300, 120, 120, 120, 120, 300, 1200, 300, 300)
FOCUSED_COUNTS = (185, 2, 2, 2, 2, 2, 2, 2, 5, 2, 2)


CONTROL_CONTINUATION = ROOT / "docs/pre_editor/active/PHASE_20_CONTROL_CONTINUATION_ACCEPTANCE_01.json"
STACK_CONTINUATION = ROOT / "docs/pre_editor/active/PHASE_20_COMPILED_STACK_CONTINUATION_ACCEPTANCE_01.json"
AUTONOMOUS_CLOSURE = ROOT / "docs/pre_editor/active/PHASE_20_AUTONOMOUS_CLOSURE_ACCEPTANCE_01.json"
FOCUSED_CONTROL_ANALYSIS = CONDITIONAL / "analysis/control-02"
POST_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_20_POST_CANDIDATE_REMEDIATION_ACCEPTANCE_01.json"
FINAL_PREFLIGHT_ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_20_FINAL_PREFLIGHT_CONTINUATION_ACCEPTANCE_01.json"
POST_ACTIVE = False


def post_authority():
    a = json.loads(POST_ACCEPTANCE.read_text(encoding="utf-8"))
    assert a["status"] == "ACCEPTED_POST_CANDIDATE_REMEDIATION_01"
    for key in ("proposal", "predecessor"):
        ref = a[key]; assert sha(ROOT / ref["path"]) == ref["sha256"]
    assert a["scope"] == ["GEngine/include/GEngine/Physics/GJK.cpp",
        "RigidBodySimulation/tests/RestingContactDiagnosis.h", "tools/pre_editor_resting_contact_diagnosis.py"]
    assert a["friction_policy"] == "P20_POST_CANDIDATE_FRICTION_V1" and a["controls_reused"] == "Release-control-02"
    for path, digest in a["before_source_hashes"].items():
        if path not in a["scope"]: assert sha(ROOT / path) == digest, "post-candidate ownership exceeded"
    assert a["budgets"] == {"native_launch_cap": 14, "native_aggregate_seconds": 1200,
        "build_aggregate_seconds": 3000, "automatic_retries": 0,
        "native_slots": {"candidate": 13, "normal": 14},
        "native_stage_seconds": {"candidate": 120, "normal": 60},
        "build_stage_seconds": {"candidate": {"Debug": 120, "Release": 120}, "normal": {"Release": 360}},
        "build_stage_accounting_origin": "PHASE_20_CONTROL_CONTINUATION_ACCEPTANCE_01.recorded_utc; retain all consumed stage time"}
    assert not a["publication_authorized"] and not a["phase21_authorized"]
    continuation = json.loads(FINAL_PREFLIGHT_ACCEPTANCE.read_text(encoding="utf-8"))
    assert continuation["status"] == "ACCEPTED_FINAL_PREFLIGHT_BOUNDED_CONTINUATION_01"
    for key in ("prior_authority", "accepted_minimum_decision", "predecessor"):
        ref = continuation[key]; assert sha(ROOT / ref["path"]) == ref["sha256"]
    assert continuation["prior_authority"]["sha256"] == sha(POST_ACCEPTANCE)
    expected = json.loads(json.dumps(a["budgets"]))
    expected["build_stage_seconds"]["candidate"]["Debug"] = 210
    assert continuation["budgets"] == expected and continuation["scope"] == a["scope"]
    assert continuation["criteria_unchanged"] and not any(continuation[key]
        for key in ("publication_authorized", "rollback_authorized", "phase21_authorized"))
    a = {**a, "budgets": expected, "final_preflight_sha256": sha(FINAL_PREFLIGHT_ACCEPTANCE)}
    return a


def remediation_slot(stage):
    if POST_ACTIVE:
        assert stage == "candidate"
        return post_authority()["budgets"]["native_slots"][stage]
    return remediation_continuation()["budgets"]["native_slots"][stage]


def remediation_continuation():
    a = json.loads(CONTROL_CONTINUATION.read_text(encoding="utf-8"))
    assert a["status"] == "ACCEPTED_CONTROL_CONTINUATION_01"
    for key in ("proposal", "prior_authority", "failed_control", "patch"):
        reference = a[key]
        assert sha(ROOT / reference["path"]) == reference["sha256"]
    assert a["prior_authority"]["path"] == CONDITIONAL_ACCEPTANCE.relative_to(ROOT).as_posix()
    failed = json.loads((ROOT / a["failed_control"]["path"]).read_text())
    assert failed["status"] == a["failed_control"]["status"] == "FAIL_OR_INCONCLUSIVE_NO_RETRY"
    assert a["scope"] == ["RigidBodySimulation/tests/RestingContactDiagnosis.h", "tools/pre_editor_resting_contact_diagnosis.py"]
    assert a["budgets"] == {"native_launch_cap": 13, "native_aggregate_seconds": 1200,
        "build_aggregate_seconds": 3000, "automatic_retries": 0,
        "consumed_at_acceptance": {"native_launches": 10, "native_seconds": 811.438, "build_seconds": 2176.514},
        "build_stage_seconds": {"control": {"Debug": 90, "Release": 90},
            "candidate": {"Debug": 120, "Release": 120}, "normal": {"Release": 360}},
        "native_stage_seconds": {"control": 120, "candidate": 120, "normal": 60},
        "native_slots": {"control": 11, "candidate": 12, "normal": 13}}
    assert not a["publication_authorized"] and not a["phase21_authorized"]
    return a


def remediation_run_path(stage):
    assert stage in ("control", "candidate")
    if POST_ACTIVE and stage == "candidate": return contained(CONDITIONAL / "runs/Release-candidate-02")
    return contained(CONDITIONAL / ("runs/Release-control-02" if stage == "control" else "runs/Release-candidate-01"))


def remediation_stack_continuation():
    a = json.loads(STACK_CONTINUATION.read_text(encoding="utf-8"))
    assert a["status"] == "ACCEPTED_COMPILED_STACK_CONTINUATION_01"
    for key in ("proposal", "prior_authority", "failed_candidate"):
        reference = a[key]
        assert sha(ROOT / reference["path"]) == reference["sha256"]
    assert a["prior_authority"]["path"] == CONTROL_CONTINUATION.relative_to(ROOT).as_posix()
    assert a["scope"] == ["GEngine/include/GEngine/Physics/GJK.cpp", "tools/pre_editor_resting_contact_diagnosis.py"]
    assert a["budgets_unchanged"] == remediation_continuation()["budgets"]
    failed = json.loads((ROOT / a["failed_candidate"]["path"]).read_text())
    assert failed["status"] == "NOT_READY_MANDATORY_COMPILED_STACK_GATE_FAIL"
    assert a["consumed_at_acceptance"] == failed["usage"]
    assert not a["publication_authorized"] and not a["phase21_authorized"]
    return a


def remediation_autonomous_closure():
    a = json.loads(AUTONOMOUS_CLOSURE.read_text(encoding="utf-8"))
    assert a["status"] == "ACCEPTED_AUTONOMOUS_ENGINEERING_CLOSURE"
    assert a["scope"] == remediation_stack_continuation()["scope"]
    assert a["budgets_unchanged"] == remediation_continuation()["budgets"]
    for key in ("prior_authority", "predecessor"):
        reference = a[key]
        assert sha(ROOT / reference["path"]) == reference["sha256"]
    assert not a["publication_authorized"]
    return a


def remediation_analysis_path(stage):
    assert stage in ("control", "candidate")
    if POST_ACTIVE and stage == "candidate": return contained(CONDITIONAL / "analysis/candidate-02")
    return contained(FOCUSED_CONTROL_ANALYSIS if stage == "control" else CONDITIONAL / "analysis/candidate-01")


def remediation_admission():
    conditional_guards()
    remediation_continuation()
    if POST_ACTIVE: post_authority()
    a = json.loads(FOCUSED_ADMISSION.read_text())
    assert a["status"] == "ADMITTED_FAMILY_B_AND_SEPARATE_CA_CONDITIONAL_VALIDATION_PENDING"
    assert a["authority"]["sha256"] == sha(CONDITIONAL_ACCEPTANCE)
    for reference in a["evidence"]:
        assert sha(ROOT / reference["path"]) == reference["sha256"]
    diagnosis = json.loads((CONDITIONAL / "runs/Release-diagnosis-01/result.json").read_text())
    assert diagnosis["status"] == "PASS_DIAGNOSIS_INTEGRITY_CAUSAL_ADMISSION_PENDING"
    assert sha(FOCUSED_LATTICE) == FOCUSED_LATTICE_HASH
    return a


def remediation_request(stage):
    assert stage in ("control", "candidate")
    magic = "P20_POST_CANDIDATE_FOCUSED_V1" if POST_ACTIVE else "P20_CONDITIONAL_FOCUSED_V1"
    return (magic + "\n" + stage + "\n" + FOCUSED_LATTICE_HASH + "\n" +
            REPLAY_INPUT_HASH + "\n25 50 16 12 4 11 4980 68 120\nEND\n")


def remediation_environment():
    env = conditional_environment()
    env.pop(FOCUSED_ENV, None)
    env.pop("GENGINE_P20_CONDITIONAL_ATTENDED_REQUEST", None)
    return env


def remediation_machine():
    import ctypes
    import winreg
    cpu = []
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor") as root:
        for i in range(winreg.QueryInfoKey(root)[0]):
            name = winreg.EnumKey(root, i)
            with winreg.OpenKey(root, name) as key:
                cpu.append({"logical_processor": name, **{field: winreg.QueryValueEx(key, field)[0]
                    for field in ("ProcessorNameString", "Identifier", "VendorIdentifier", "~MHz")}})
    power = subprocess.run(["powercfg", "/getactivescheme"], capture_output=True, text=True, check=True).stdout.strip()
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    size = ctypes.c_uint32()
    kernel.GetLogicalProcessorInformationEx(0, None, ctypes.byref(size))
    assert size.value, "CPU core topology unavailable"
    topology = ctypes.create_string_buffer(size.value)
    assert kernel.GetLogicalProcessorInformationEx(0, topology, ctypes.byref(size)), "CPU core topology unavailable"
    idle = ctypes.c_uint64(); system = ctypes.c_uint64(); user = ctypes.c_uint64()
    assert kernel.GetSystemTimes(ctypes.byref(idle), ctypes.byref(system), ctypes.byref(user))
    return {"cpu_registry": cpu, "processor_core_topology_hex": topology.raw[:size.value].hex(),
        "power_scheme": power, "logical_processors": os.cpu_count(),
        "system_time_100ns": {"idle": idle.value, "kernel_including_idle": system.value, "user": user.value},
        "recorded_utc": now(), "query_method": "Windows registry/kernel32; CIM access unavailable in supported sandbox"}


def remediation_machine_identity(record):
    return {key: value for key, value in record.items() if key not in ("recorded_utc", "system_time_100ns", "query_method")}


def remediation_query_inputs(directory):
    expected = json.loads((CONDITIONAL / "focused-query-inputs.json").read_text())["expected_csv_rows"]
    mismatches = []
    try:
        with (directory / "focused-inputs.csv").open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        if len(rows) != len(expected): mismatches.append({"row_count": len(rows), "expected": len(expected)})
        for index, (old, actual) in enumerate(zip(expected, rows)):
            if set(old) != set(actual): mismatches.append({"row": index, "columns": "MISMATCH"})
            for field, value in old.items():
                if field not in actual or int(actual[field]) != value:
                    mismatches.append({"row": index, "field": field, "expected": value, "actual": actual.get(field)})
        return {"pass": not mismatches, "rows": len(rows), "expected_rows": len(expected), "mismatches": mismatches,
            "basis": "Exact native words compared with prebuild source-model freeze; no normalization or replacement"}
    except (ValueError, OSError) as error:
        return {"pass": False, "error": str(error), "mismatches": mismatches}


def remediation_process_identity(process):
    import ctypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetPriorityClass.argtypes = [ctypes.c_void_p]
    kernel.GetPriorityClass.restype = ctypes.c_uint32
    kernel.GetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
    own = ctypes.c_size_t(); system = ctypes.c_size_t(); handle = ctypes.c_void_p(int(process._handle))
    priority = kernel.GetPriorityClass(handle)
    assert priority == 0x20, "native process is not normal priority"
    assert kernel.GetProcessAffinityMask(handle, ctypes.byref(own), ctypes.byref(system)), "native affinity query failed"
    return {"priority_class": priority, "priority_name": "NORMAL_PRIORITY_CLASS", "process_affinity": own.value,
        "system_affinity": system.value}


def remediation_inputs(stage):
    inputs = conditional_inputs()
    for path in (FOCUSED_ADMISSION, FOCUSED_LATTICE, CONTROL_CONTINUATION,
                 ROOT / remediation_continuation()["proposal"]["path"], CONDITIONAL / "focused-protocol.json",
                 CONDITIONAL / "focused-build-protocol.json", CONDITIONAL / "focused-query-inputs.json"):
        inputs["workspace_inputs"][path.relative_to(ROOT).as_posix()] = sha(path)
    control_handoff = FOCUSED_CONTROL_ANALYSIS / "handoff.json"
    if stage == "candidate":
        assert control_handoff.exists()
        inputs["workspace_inputs"][control_handoff.relative_to(ROOT).as_posix()] = sha(control_handoff)
        continuation = remediation_stack_continuation()
        for path in (STACK_CONTINUATION, ROOT / continuation["proposal"]["path"],
                     ROOT / continuation["failed_candidate"]["path"]):
            inputs["workspace_inputs"][path.relative_to(ROOT).as_posix()] = sha(path)
        autonomy = remediation_autonomous_closure()
        for path in (AUTONOMOUS_CLOSURE, ROOT / autonomy["predecessor"]["path"]):
            inputs["workspace_inputs"][path.relative_to(ROOT).as_posix()] = sha(path)
    if POST_ACTIVE:
        a = post_authority()
        for path in (POST_ACCEPTANCE, ROOT / a["proposal"]["path"], ROOT / a["predecessor"]["path"],
                     FINAL_PREFLIGHT_ACCEPTANCE,
                     ROOT / "logs/pre_editor/phase-20/post-candidate-remediation-01/implementation-plan-01.json"):
            inputs["workspace_inputs"][path.relative_to(ROOT).as_posix()] = sha(path)
    return inputs


def remediation_compilation_inputs(inputs):
    # These classifications permit runner/review-only correction without claiming
    # that the earlier build executed the changed runner. All native compilation,
    # asset, discovery and toolchain inputs stay bound byte-for-byte.
    return {**inputs, "workspace_inputs": {p: digest for p, digest in inputs["workspace_inputs"].items()
        if not p.startswith(("docs/pre_editor/", "logs/pre_editor/")) and
        p != "tools/pre_editor_resting_contact_diagnosis.py" and p != FOCUSED_LATTICE.relative_to(ROOT).as_posix()}}


def remediation_freeze(stage):
    remediation_admission()
    assert stage in ("control", "candidate")
    review_paths = list(CONDITIONAL.glob(stage + "-source-review*.json")); assert review_paths
    review_path = max(review_paths, key=lambda p: json.loads(p.read_text())["recorded_utc"])
    review = json.loads(review_path.read_text())
    assert review["status"] == "PASS_STATIC_REVIEW"
    source_hashes = {p: sha(ROOT / p) for p in conditional_authority()["source_ownership"]}
    assert review["source_hashes"] == source_hashes, "review must bind the actual five files"
    assert review["stage"] == stage
    usage = conditional_usage()
    assert usage["native_launches"] == remediation_slot(stage)-1
    if stage == "candidate":
        control = json.loads((FOCUSED_CONTROL_ANALYSIS / "handoff.json").read_text())
        assert control["status"] == "PASS_MATCHED_CONTROL_INTEGRITY_BASELINE_FAILURES_PRESERVED"
    output = target_fresh("candidates", stage, CONDITIONAL)
    for path in source_hashes:
        dest = contained(output / "source" / path); dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / path, dest)
    (output / "changes.patch").write_bytes(git("diff", "--no-ext-diff", "--", *source_hashes))
    (output / "focused-request.txt").write_text(remediation_request(stage), encoding="ascii", newline="\n")
    write_new(output / "frozen.json", {"status": "PASS_STATIC_FROZEN_CONDITIONAL_FOCUSED", "stage": stage,
        "recorded_utc": now(), "authority_sha256": sha(CONDITIONAL_ACCEPTANCE), "admission_sha256": sha(FOCUSED_ADMISSION),
        "review": {"path": review_path.relative_to(ROOT).as_posix(), "sha256": sha(review_path)},
        "source_hashes": source_hashes, "inputs": remediation_inputs(stage), "usage": usage,
        "patch_sha256": sha(output / "changes.patch"), "request_sha256": sha(output / "focused-request.txt")})
    print(json.dumps({"status": "PASS_STATIC_FROZEN_CONDITIONAL_FOCUSED", "frozen": str(output / "frozen.json")}, indent=2))


def remediation_frozen(stage):
    remediation_admission()
    paths = sorted((CONDITIONAL / "candidates").glob(stage + "-*/frozen.json")); assert paths
    path = paths[-1]; data = json.loads(path.read_text())
    assert data["status"] == "PASS_STATIC_FROZEN_CONDITIONAL_FOCUSED" and data["stage"] == stage
    assert data["authority_sha256"] == sha(CONDITIONAL_ACCEPTANCE) and data["admission_sha256"] == sha(FOCUSED_ADMISSION)
    assert data["inputs"] == remediation_inputs(stage), "focused input closure changed"
    assert all(sha(ROOT / p) == digest for p, digest in data["source_hashes"].items())
    assert sha(path.with_name("changes.patch")) == data["patch_sha256"]
    assert sha(path.with_name("focused-request.txt")) == data["request_sha256"]
    assert path.with_name("focused-request.txt").read_text() == remediation_request(stage)
    assert sha(ROOT / data["review"]["path"]) == data["review"]["sha256"]
    return path, data


def remediation_latest_build(configuration, inputs):
    expected = remediation_compilation_inputs(inputs)
    mode = {"profiling": configuration == "Release", "CL": "/DGE_ENABLE_PHYSICS_PROFILING" if configuration == "Release" else None}
    paths = sorted((CONDITIONAL / "builds").glob(configuration + "-*/result.json"),
        key=lambda p: json.loads(p.read_text())["completed_utc"], reverse=True)
    for path in paths:
        result = json.loads(path.read_text()); invocation = json.loads(path.with_name("invocation.json").read_text())
        if (result["status"] == "PASS" and invocation.get("compile_mode") == mode and
            remediation_compilation_inputs(invocation["inputs"]) == expected and
            len(result["products"]) == 3 and all(sha(ROOT / p) == digest for p, digest in result["products"].items())):
            return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path), "products": result["products"],
                "reuse_basis": "Exact native compilation/assets/discovery/toolchain inputs and current product identities; runner/metadata separately frozen"}
    return None


class _RemediationBuildOwner:
    """Own the suspended child and its non-breakaway descendant job by handles."""
    def __init__(self, audit):
        import ctypes
        from ctypes import wintypes as W
        import _winapi
        self.ctypes, self.winapi, self.audit = ctypes, _winapi, audit
        self.job = self.process = self.thread = None
        self.assigned = False
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        class BasicLimits(ctypes.Structure):
            _fields_ = [("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64),
                ("flags", W.DWORD), ("min_working", ctypes.c_size_t), ("max_working", ctypes.c_size_t),
                ("active_limit", W.DWORD), ("affinity", ctypes.c_size_t),
                ("priority", W.DWORD), ("scheduling", W.DWORD)]
        class ExtendedLimits(ctypes.Structure):
            _fields_ = [("basic", BasicLimits), ("io", ctypes.c_uint64 * 6),
                ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
                ("peak_process_memory", ctypes.c_size_t), ("peak_job_memory", ctypes.c_size_t)]
        class Accounting(ctypes.Structure):
            _fields_ = [("times", ctypes.c_int64 * 4), ("page_faults", W.DWORD),
                ("total", W.DWORD), ("active", W.DWORD), ("terminated", W.DWORD)]
        self.Limits, self.Accounting = ExtendedLimits, Accounting
        signatures = {
            "CreateJobObjectW": ([ctypes.c_void_p, W.LPCWSTR], W.HANDLE),
            "SetInformationJobObject": ([W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD], W.BOOL),
            "AssignProcessToJobObject": ([W.HANDLE, W.HANDLE], W.BOOL),
            "QueryInformationJobObject": ([W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD, ctypes.c_void_p], W.BOOL),
            "TerminateJobObject": ([W.HANDLE, W.UINT], W.BOOL),
            "ResumeThread": ([W.HANDLE], W.DWORD)}
        for name, (args, result) in signatures.items():
            function = getattr(self.kernel, name); function.argtypes = args; function.restype = result

    def checked(self, result):
        if not result: raise self.ctypes.WinError(self.ctypes.get_last_error())
        return result

    def create(self):
        self.job = self.checked(self.kernel.CreateJobObjectW(None, None))
        limits = self.Limits(); limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE only.
        self.checked(self.kernel.SetInformationJobObject(self.job, 9,
            self.ctypes.byref(limits), self.ctypes.sizeof(limits)))
        self.audit["kill_on_job_close"] = True
        self.audit["breakaway_allowed"] = False

    def start(self, command, env, log):
        import msvcrt
        api = self.winapi; duplicates = []
        try:
            with open(os.devnull, "rb") as stdin:
                own = api.GetCurrentProcess()
                for stream in (stdin, log):
                    duplicates.append(api.DuplicateHandle(own, msvcrt.get_osfhandle(stream.fileno()),
                        own, 0, True, api.DUPLICATE_SAME_ACCESS))
                startup = subprocess.STARTUPINFO(dwFlags=api.STARTF_USESTDHANDLES,
                    hStdInput=duplicates[0], hStdOutput=duplicates[1], hStdError=duplicates[1],
                    lpAttributeList={"handle_list": duplicates.copy()})
                # Popen discards the primary-thread handle; retain CreateProcess's
                # original handles instead. Nothing executes before job assignment.
                self.process, self.thread, pid, tid = api.CreateProcess(command[0],
                    subprocess.list2cmdline(command), None, None, True,
                    0x4 | api.CREATE_NO_WINDOW, env, str(ROOT), startup)
                self.audit.update({"pid": pid, "thread_id": tid, "created_suspended": True})
        finally:
            for handle in duplicates:
                api.CloseHandle(handle)
        self.checked(self.kernel.AssignProcessToJobObject(self.job, self.process))
        self.assigned = True; self.audit["assigned_before_resume"] = True

    def resume(self):
        previous = self.kernel.ResumeThread(self.thread)
        if previous == 0xffffffff: raise self.ctypes.WinError(self.ctypes.get_last_error())
        if previous != 1: raise RuntimeError("unexpected primary-thread suspension count")
        self.audit["resumed"] = True

    def poll(self):
        if self.process is None: return None, 0
        api = self.winapi
        state = api.WaitForSingleObject(self.process, 0)
        if state not in (api.WAIT_OBJECT_0, api.WAIT_TIMEOUT):
            raise RuntimeError("unexpected owned process wait result: " + str(state))
        code = api.GetExitCodeProcess(self.process) if state == api.WAIT_OBJECT_0 else None
        if not self.assigned: return code, int(code is None)
        accounting = self.Accounting()
        self.checked(self.kernel.QueryInformationJobObject(self.job, 1,
            self.ctypes.byref(accounting), self.ctypes.sizeof(accounting), None))
        self.audit["job_total_processes"] = accounting.total
        self.audit["job_active_processes"] = accounting.active
        return code, accounting.active

    def terminate(self):
        if self.process is None: return
        if self.assigned:
            self.checked(self.kernel.TerminateJobObject(self.job, 1))
            self.audit["terminate_job_succeeded"] = True
        else:
            # Assignment failed while the sole child was still suspended.
            self.winapi.TerminateProcess(self.process, 1)
            self.audit["terminate_suspended_process_succeeded"] = True

    def close(self):
        errors = []
        # Closing our sole noninherited job handle is the final kill fallback.
        for name in ("job", "thread", "process"):
            handle = getattr(self, name)
            if handle is not None:
                try: self.winapi.CloseHandle(handle)
                except BaseException as error: errors.append(name + ": " + repr(error))
                else: setattr(self, name, None)
        return errors


def remediation_owned_build(command, env, log, limit, *, _factory=None, _clock=None, _sleep=None):
    """Return failure evidence even when assignment, termination or waiting fails."""
    clock = _clock or time.monotonic; sleep = _sleep or time.sleep
    started = clock(); deadline = started + limit
    cleanup_reserve = min(5., limit / 5)
    work_deadline = deadline - cleanup_reserve
    audit = {"method": "suspended CreateProcess; owned job; held process/thread handles",
        "cleanup_reserve_seconds": cleanup_reserve, "quiescent_before_handle_close": False}
    owner = None; code = None; timed_out = False; quiet = False; errors = []
    try:
        if limit <= 0: raise ValueError("insufficient owned build deadline")
        owner = (_factory or _RemediationBuildOwner)(audit)
        owner.create(); owner.start(command, env, log)
        if clock() >= work_deadline:
            timed_out = True; raise TimeoutError("owned build setup exhausted execution deadline before resume")
        owner.resume()
        while True:
            code, active = owner.poll()
            remaining = work_deadline - clock()
            if remaining <= 0: timed_out = True; break
            if code is not None:
                audit.update(root_exit_code=code, root_exit_elapsed_seconds=clock()-started,
                    active_descendants_at_root_exit=active)
                # MSBuild's completed result is distinct from child lifetime.
                # Retire all remaining owned descendants immediately below;
                # their continued existence is not itself a work timeout.
                quiet = active == 0
                break
            sleep(min(.05, remaining))
    except BaseException as error:
        errors.append("execution: " + repr(error))
    finally:
        if owner is not None:
            if not quiet:
                try: owner.terminate()
                except BaseException as error: errors.append("termination: " + repr(error))
                while True:
                    try:
                        code, active = owner.poll()
                        if active == 0 and (code is not None or owner.process is None):
                            quiet = True; break
                        remaining = deadline - clock()
                        if remaining <= 0: timed_out = True; break
                        sleep(min(.05, remaining))
                    except BaseException as error:
                        errors.append("cleanup observation: " + repr(error)); break
            audit["quiescent_before_handle_close"] = quiet
            try: errors.extend("handle close: " + error for error in owner.close())
            except BaseException as error: errors.append("handle cleanup: " + repr(error))
        if not quiet and owner is not None:
            errors.append("owned descendant termination not confirmed; no successful build claim")
    duration = clock() - started
    if duration > limit: timed_out = True
    return {"exit_code": code, "timed_out": timed_out, "error": "; ".join(errors) or None,
        "wall_seconds": duration, "ownership": audit}


def remediation_build(stage, configuration):
    frozen_path, frozen = remediation_frozen(stage)
    previous = remediation_latest_build(configuration, frozen["inputs"])
    if previous:
        print(json.dumps({"status": "REUSED_MATCHING_BUILD_PASS", "configuration": configuration, "evidence": previous}, indent=2))
        return
    authority = remediation_continuation()
    remaining = authority["budgets"]["build_aggregate_seconds"] - conditional_usage()["build_seconds"]
    spent = 0.
    for invocation_path in (CONDITIONAL / "builds").glob(configuration + "-" + stage + "-*/invocation.json"):
        invocation = json.loads(invocation_path.read_text())
        if invocation["started_utc"] >= authority["recorded_utc"]:
            spent += json.loads(invocation_path.with_name("result.json").read_text())["wall_seconds"]
    limits = post_authority() if POST_ACTIVE else authority
    stage_remaining = limits["budgets"]["build_stage_seconds"][stage][configuration] - spent
    assert remaining > 0 and stage_remaining > 0, "accepted build budget exhausted"
    output = target_fresh("builds", configuration + "-" + stage, CONDITIONAL)
    command = [str(MSBUILD), str(ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"), "/t:Build",
        f"/p:Configuration={configuration}", "/p:Platform=x64", "/m:2", "/nr:false", "/v:minimal"]
    mode = {"profiling": configuration == "Release", "CL": "/DGE_ENABLE_PHYSICS_PROFILING" if configuration == "Release" else None}
    # Changing a compiler environment define must not reuse unprofiled PCH/objects.
    # Force only the required RBS dependency closure at this first mode transition.
    profile_builds = [p for p in (CONDITIONAL / "builds").glob("Release-*/result.json")
        if json.loads(p.read_text())["status"] == "PASS"]
    matching_mode = False
    if profile_builds:
        latest = max(profile_builds, key=lambda p: json.loads(p.read_text())["completed_utc"])
        last_result = json.loads(latest.read_text()); last_invocation = json.loads(latest.with_name("invocation.json").read_text())
        matching_mode = last_invocation.get("compile_mode") == mode and all(
            (ROOT / p).is_file() and sha(ROOT / p) == digest for p, digest in last_result.get("mode_artifacts", {}).items()) and bool(last_result.get("mode_artifacts"))
    force_mode_transition = configuration == "Release" and not matching_mode
    if force_mode_transition: command.append("/p:ForceRebuild=true")
    env = remediation_environment()
    if mode["CL"]: env["CL"] = mode["CL"]
    limit = min(1200, remaining, stage_remaining)
    write_new(output / "invocation.json", {"started_utc": now(), "command": command, "timeout_seconds": limit,
        "inputs": frozen["inputs"], "frozen": {"path": frozen_path.relative_to(ROOT).as_posix(), "sha256": sha(frozen_path)},
        "remaining_aggregate_seconds": remaining, "remaining_stage_seconds": stage_remaining,
        "continuation_sha256": sha(CONTROL_CONTINUATION), "stage": stage, "compile_mode": mode,
        "final_preflight_sha256": sha(FINAL_PREFLIGHT_ACCEPTANCE) if POST_ACTIVE else None,
        "force_compiler_mode_transition": force_mode_transition})
    started = time.monotonic()
    try:
        with (output / "build.log").open("wb") as log:
            execution = remediation_owned_build(command, env, log, limit)
    except BaseException as error:
        execution = {"exit_code": None, "timed_out": False, "error": "log/setup: " + repr(error),
            "wall_seconds": time.monotonic()-started, "ownership": {"quiescent_before_handle_close": False}}
    code = execution["exit_code"]; timed_out = execution["timed_out"]
    duration = execution["wall_seconds"]; launch_error = execution["error"]
    evidence_errors = []
    try: stable = remediation_inputs(stage) == frozen["inputs"]
    except BaseException as error:
        stable = False; evidence_errors.append("input comparison: " + repr(error))
    products = [f"bin/{configuration}/RigidBodySimulation/RigidBodySimulation.exe", f"bin/{configuration}/GEngine/GEngine.lib",
        f"external/glad/bin/{configuration}/glad/glad.lib"]
    mode_paths = [f"bin-int/{configuration}/GEngine/{name}" for name in
        ("gepch.obj", "PhysicsProfile.obj", "PhysicsSystem.obj", "GJK.obj", "ConstraintPenetration.obj")]
    def available_hashes(paths):
        hashes = {}
        for path in paths:
            try:
                if (ROOT / path).is_file(): hashes[path] = sha(ROOT / path)
            except BaseException as error: evidence_errors.append(path + ": " + repr(error))
        return hashes
    product_hashes = available_hashes(products); mode_hashes = available_hashes(mode_paths)
    log_hashes = available_hashes([(output / "build.log").relative_to(ROOT).as_posix()])
    result = {"status": "PASS" if code == 0 and stable and not timed_out and duration <= limit and
            not launch_error and not evidence_errors and execution["ownership"]["quiescent_before_handle_close"] and
            len(product_hashes) == 3 and len(mode_hashes) == 5 and len(log_hashes) == 1 else "FAIL",
        "configuration": configuration, "exit_code": code, "launch_error": launch_error, "timed_out": timed_out,
        "stable_inputs": stable, "wall_seconds": duration, "completed_utc": now(),
        "log_sha256": next(iter(log_hashes.values()), None), "ownership": execution["ownership"],
        "evidence_errors": evidence_errors, "products": product_hashes, "mode_artifacts": mode_hashes}
    write_new(output / "result.json", result)
    print(json.dumps({**result, "evidence": str(output), "usage": conditional_usage()}, indent=2))
    if result["status"] != "PASS": raise SystemExit(1)


def remediation_prelaunch_proof(frozen_path, frozen):
    path = frozen_path.with_name("prelaunch-proof.json")
    proof = json.loads(path.read_text())
    assert proof["status"] == "PASS_MANDATORY_CANDIDATE_PRELAUNCH_GATES"
    assert proof["frozen_sha256"] == sha(frozen_path)
    assert proof["source_hashes"] == frozen["source_hashes"]
    assert proof["stack_continuation_sha256"] == sha(STACK_CONTINUATION)
    if POST_ACTIVE:
        assert proof["post_candidate_acceptance_sha256"] == sha(POST_ACCEPTANCE)
        assert proof["final_preflight_acceptance_sha256"] == sha(FINAL_PREFLIGHT_ACCEPTANCE)
        assert proof["exact_grid_equivalence"] and proof["complete_friction_policy"] and proof["light_path_unchanged"]
    assert all(sha(ROOT / p) == digest for p, digest in proof["artifacts"].items())
    assert all(sha(ROOT / p) == digest for p, digest in proof["evidence"].items())
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path)}


def remediation_prepare(stage):
    frozen_path, frozen = remediation_frozen(stage); usage = conditional_usage()
    assert usage["native_launches"] == remediation_slot(stage)-1
    assert usage["native_seconds"] + 120 <= 1200
    output_run = remediation_run_path(stage)
    assert not output_run.exists(), "unique focused native slot already reserved/consumed"
    builds = {c: remediation_latest_build(c, frozen["inputs"]) for c in ("Debug", "Release")}
    assert all(builds.values()), "affected Debug and Release builds required"
    profile_path = (ROOT / builds["Release"]["path"]).with_name("profiling-proof.json")
    profile = json.loads(profile_path.read_text())
    assert profile["status"] == "PASS_COMPILED_EXISTING_PROFILER_ENABLED"
    assert profile["build_result_sha256"] == builds["Release"]["sha256"]
    assert all(sha(ROOT / p) == digest for p, digest in profile["artifacts"].items())
    assert all(sha(ROOT / p) == digest for p, digest in profile["evidence"].items())
    prelaunch_proof = remediation_prelaunch_proof(frozen_path, frozen) if stage == "candidate" else None
    output = target_fresh("prelaunch", stage + "-ready", CONDITIONAL)
    write_new(output / "readiness.json", {"status": "READY_FOR_AUTHORIZED_CONDITIONAL_FOCUSED", "stage": stage,
        "recorded_utc": now(), "authority_sha256": sha(CONDITIONAL_ACCEPTANCE), "inputs": frozen["inputs"], "builds": builds,
        "frozen": {"path": frozen_path.relative_to(ROOT).as_posix(), "sha256": sha(frozen_path)},
        "profiling_proof": {"path": profile_path.relative_to(ROOT).as_posix(), "sha256": sha(profile_path)},
        "mandatory_prelaunch_proof": prelaunch_proof,
        "request_sha256": frozen["request_sha256"], "usage": usage,
        "command": [str(ROOT / "bin/Release/RigidBodySimulation/RigidBodySimulation.exe"), "--resting-contact-diagnostic",
            "on", "--lattice", str(FOCUSED_LATTICE), "--output", str(output_run / "data")],
        "cwd": str(ROOT / "runtime/RigidBodySimulation/Release"), "owner_visible_validation": False,
        "desktop_requirement": "Accepted automated focused local session; no owner-visible assessment",
        "limits": {"wall_seconds": 120, "simulated_seconds": 68, "physics_ticks": 4980,
            "queries": 50, "direct_gjk": 16, "ca_helpers": 12, "projection_helpers": 4, "automatic_retries": 0}})
    print(json.dumps({"status": "READY_FOR_AUTHORIZED_CONDITIONAL_FOCUSED", "readiness": str(output / "readiness.json")}, indent=2))


def remediation_run(stage):
    import platform
    frozen_path, frozen = remediation_frozen(stage)
    paths = sorted((CONDITIONAL / "prelaunch").glob(stage + "-ready-*/readiness.json")); assert paths
    readiness = paths[-1]; ready = json.loads(readiness.read_text())
    assert ready["status"] == "READY_FOR_AUTHORIZED_CONDITIONAL_FOCUSED" and ready["stage"] == stage
    assert ready["authority_sha256"] == sha(CONDITIONAL_ACCEPTANCE) and ready["inputs"] == frozen["inputs"]
    assert ready["request_sha256"] == frozen["request_sha256"]
    assert ready["frozen"] == {"path": frozen_path.relative_to(ROOT).as_posix(), "sha256": sha(frozen_path)}
    if stage == "candidate":
        assert ready["mandatory_prelaunch_proof"] == remediation_prelaunch_proof(frozen_path, frozen)
    expected_output = remediation_run_path(stage)
    assert ready["cwd"] == str(ROOT / "runtime/RigidBodySimulation/Release")
    assert ready["command"] == [str(ROOT / "bin/Release/RigidBodySimulation/RigidBodySimulation.exe"),
        "--resting-contact-diagnostic", "on", "--lattice", str(FOCUSED_LATTICE), "--output", str(expected_output / "data")]
    assert ready["limits"] == {"wall_seconds": 120, "simulated_seconds": 68, "physics_ticks": 4980,
        "queries": 50, "direct_gjk": 16, "ca_helpers": 12, "projection_helpers": 4, "automatic_retries": 0}
    for c in ("Debug", "Release"):
        assert remediation_latest_build(c, frozen["inputs"]) == ready["builds"][c]
    profile_ref = ready["profiling_proof"]; assert sha(ROOT / profile_ref["path"]) == profile_ref["sha256"]
    profile = json.loads((ROOT / profile_ref["path"]).read_text())
    assert profile["status"] == "PASS_COMPILED_EXISTING_PROFILER_ENABLED" and profile["build_result_sha256"] == ready["builds"]["Release"]["sha256"]
    assert all(sha(ROOT / p) == digest for p, digest in profile["artifacts"].items())
    assert all(sha(ROOT / p) == digest for p, digest in profile["evidence"].items())
    usage = conditional_usage(); slot = remediation_slot(stage)
    assert usage["native_launches"] == slot-1 and usage["native_seconds"]+120 <= 1200
    if stage == "candidate":
        prior = json.loads((FOCUSED_CONTROL_ANALYSIS / "handoff.json").read_text())
        assert prior["status"] == "PASS_MATCHED_CONTROL_INTEGRITY_BASELINE_FAILURES_PRESERVED"
    assert os.name == "nt"
    probe = subprocess.run(["powershell", "-NoProfile", "-Command",
        "@(Get-Process -Name RigidBodySimulation -ErrorAction SilentlyContinue).Count"], capture_output=True, text=True, check=True)
    assert int(probe.stdout.strip()) == 0, "another RBS is active; no intervention"
    output = contained(remediation_run_path(stage))
    assert not output.exists(), "unique native attempt consumed/reserved; no retry"
    environment_record = {"platform": platform.platform(), "launcher_pid": os.getpid(), "owner_visible_validation": False,
        "machine_before": remediation_machine()}
    if stage == "candidate":
        control_result = json.loads((remediation_run_path("control") / "result.json").read_text())
        assert remediation_machine_identity(environment_record["machine_before"]) == remediation_machine_identity(
            control_result["environment"]["machine_before"]), "matched CPU/power environment differs before unique launch"
    try: environment_record["desktop"] = attended_desktop()
    except OSError as error: environment_record["desktop_query_error"] = str(error)
    env = remediation_environment(); env["GENGINE_ASSET_ROOT"] = str(ROOT / "bin/Release/assets")
    output.mkdir(parents=True); (output / "data").mkdir()
    request = output / "focused-request.txt"; shutil.copyfile(frozen_path.with_name("focused-request.txt"), request)
    assert sha(request) == ready["request_sha256"] and request.read_text() == remediation_request(stage)
    env[FOCUSED_ENV] = str(request)
    write_new(output / "invocation.json", {"command": ready["command"], "cwd": ready["cwd"], "started_utc": now(),
        "native_launch_number": slot, "limit_seconds": 120, "usage_before": usage, "stage": stage,
        "readiness_sha256": sha(readiness), "request_sha256": sha(request), "environment": environment_record, "automatic_retry": False})
    started = time.monotonic(); timed_out = False; code = None; launch_error = None; samples = []; sample_at = .25
    process_identity = None; process_identity_error = None
    with (output / "console.log").open("wb") as log:
        try:
            process = subprocess.Popen(ready["command"], cwd=ready["cwd"], env=env, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.NORMAL_PRIORITY_CLASS)
            try: process_identity = remediation_process_identity(process)
            except (OSError, AssertionError) as error: process_identity_error = str(error)
            write_new(output / "process.json", {"pid": process.pid, "created_utc": now(), "native_launch_number": slot,
                "identity": process_identity, "identity_error": process_identity_error})
            while process.poll() is None:
                elapsed = time.monotonic()-started
                if elapsed >= 115:
                    timed_out = True
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=log, stderr=log, check=False)
                    break
                if elapsed >= sample_at and len(samples) < 2:
                    try: samples.append(attended_windows(process.pid))
                    except OSError as error: samples.append({"error": str(error)})
                    sample_at += 1
                time.sleep(.1)
            code = process.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired) as error: launch_error = str(error)
    duration = time.monotonic()-started
    try:
        environment_record["machine_after"] = remediation_machine()
        environment_record["stable_machine_identity"] = remediation_machine_identity(environment_record["machine_before"]) == remediation_machine_identity(environment_record["machine_after"])
    except (OSError, AssertionError, subprocess.SubprocessError) as error:
        environment_record["machine_after_error"] = str(error); environment_record["stable_machine_identity"] = False
    environment_record["native_process"] = process_identity
    environment_record["native_process_error"] = process_identity_error
    for name in ("GEngine.log", "imgui.ini"):
        source = Path(ready["cwd"]) / name
        if source.exists(): shutil.copyfile(source, output / ("runtime-"+name))
    stable = remediation_inputs(stage) == frozen["inputs"]
    native_path = output / "data/result.json"
    native = json.loads(native_path.read_text()) if native_path.exists() else None
    export_files = [contained(p) for p in (output / "data").rglob("*") if p.is_file()]
    exports = sum(p.stat().st_size for p in export_files)
    input_comparison = remediation_query_inputs(output / "data")
    # Native checks and independent offline checks have separate dispositions.
    # A baseline physical failure must stay recorded and is never renamed PASS.
    integrity = bool(code == 0 and not timed_out and not launch_error and duration <= 120 and stable and process_identity and
        environment_record["stable_machine_identity"] and input_comparison["pass"] and
        sha(request) == ready["request_sha256"] and native and native.get("complete") and native.get("shutdown") and
        native.get("conditional") and native.get("candidate") == (stage == "candidate") and native.get("episodes") == 11 and native.get("ticks") == 4980 and
        native.get("collision_calls") == 50 and native.get("direct_gjk_calls") == 16 and native.get("ca_helper_calls") == 12 and
        native.get("projection_helper_calls") == 4 and not native.get("overflow", True) and native.get("profiling_enabled") and
        native.get("profiled_ticks") == 4980 and native.get("physics_world_ns", 0) > 0 and exports <= 8388608)
    if POST_ACTIVE:
        integrity = bool(integrity and native.get("post_candidate_policy") and native.get("post_ticks") == 1680 and
            native.get("post_stage_records") == 288 and 0 < native.get("post_event_records", 0) <= 1024 and
            0 < native.get("post_storage_bytes", 0) <= 1048576 and native.get("post_sliding", 0) > 0)
    result = {"status": "PASS_FOCUSED_NATIVE_INTEGRITY_OFFLINE_VALIDATION_PENDING" if integrity else "FAIL_OR_INCONCLUSIVE_NO_RETRY",
        "stage": stage, "exit_code": code, "launch_error": launch_error, "timed_out": timed_out, "wall_seconds": duration,
        "completed_utc": now(), "stable_inputs": stable, "native": native, "exports_bytes": exports,
        "frozen_query_input_comparison": input_comparison,
        "environment": environment_record, "window_samples": samples, "owner_visible_validation": False,
        "evidence": {p.relative_to(output).as_posix(): sha(p) for p in output.rglob("*") if p.is_file()}}
    write_new(output / "result.json", result)
    print(json.dumps({"status": result["status"], "native": native, "wall_seconds": duration,
        "evidence": str(output), "usage": conditional_usage()}, indent=2))
    if not integrity: raise SystemExit(1)


def remediation_preserve_control_products():
    run = remediation_run_path("control") / "result.json"
    result = json.loads(run.read_text())
    assert result["status"] == "PASS_FOCUSED_NATIVE_INTEGRITY_OFFLINE_VALIDATION_PENDING"
    ready_paths = sorted((CONDITIONAL / "prelaunch").glob("control-ready-*/readiness.json")); assert ready_paths
    ready = json.loads(ready_paths[-1].read_text()); build = json.loads((ROOT / ready["builds"]["Release"]["path"]).read_text())
    paths = {**build["products"], **build["mode_artifacts"]}
    debug_build = json.loads((ROOT / ready["builds"]["Debug"]["path"]).read_text())
    paths.update(debug_build["products"]); paths.update(debug_build["mode_artifacts"])
    for name in ("main.obj", "RbsFrame.obj", "RbsScene.obj"):
        rbs_object = "bin-int/Release/RigidBodySimulation/" + name
        assert (ROOT / rbs_object).is_file(); paths[rbs_object] = sha(ROOT / rbs_object)
    output = contained(CONDITIONAL / "products/control-02"); assert not output.exists()
    for path, digest in paths.items():
        assert sha(ROOT / path) == digest
        dest = contained(output / path); dest.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(ROOT / path, dest)
        assert sha(dest) == digest
    write_new(output / "manifest.json", {"status": "PRESERVED_TESTED_CONTROL_PRODUCTS_AND_STACK_OBJECTS",
        "recorded_utc": now(), "run_sha256": sha(run), "build": ready["builds"]["Release"], "products": paths})
    print(json.dumps({"preserved": str(output / "manifest.json")}, indent=2))


def remediation_csv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def remediation_cost(directory):
    rows = remediation_csv(directory / "focused-cpu.csv")
    intervals = {}
    timed = ("physicsWorldTimeNs", "narrowphaseTimeNs", "gjkTimeNs", "epaTimeNs", "manifoldTimeNs",
             "solverTimeNs", "contactResolutionTimeNs", "broadphaseTimeNs", "gravityTimeNs", "integrationTimeNs")
    def stats(values):
        assert values and all(math.isfinite(v) and v >= 0 for v in values)
        return {"mean": statistics.fmean(values), "p95": sorted(values)[math.ceil(.95 * len(values))-1]}
    for episode, count, block in ((0, 1800, 348), (8, 1200, 228), (10, 300, 48)):
        selected = [r for r in rows if int(r["episode"]) == episode]
        assert [int(r["tick"]) for r in selected] == list(range(1, count+1))
        assert all(int(r["stepCount"]) == 1 and int(r["physicsWorldTimeNs"]) > 0 for r in selected)
        measured = selected[60:]; assert len(measured) == 5*block
        boundaries = {}
        for field in timed:
            values = [int(r[field]) for r in measured]
            boundaries[field] = {"aggregate": stats(values), "blocks": [stats(values[i*block:(i+1)*block]) for i in range(5)],
                "excluded_first60": stats([int(r[field]) for r in selected[:60]])}
        intervals[str(episode)] = {"samples": count, "excluded": 60, "block_length": block, "boundaries": boundaries,
            "work_counts": {field: sum(int(r[field]) for r in measured) for field in selected[0]
                if field.endswith("Count") and field not in ("manifoldCount", "manifoldContactCount", "solverConstraintCount")},
            "end_gauges": {field: int(selected[-1][field]) for field in ("manifoldCount", "manifoldContactCount", "solverConstraintCount")},
            "observer_ns": sum(int(r["observer_ns"]) for r in measured)}
    assert len(rows) == 3300
    return {"status": "COMPLETE_FIXED_SAMPLES", "estimators": "raw-ns arithmetic mean; nearest-rank p95",
        "intervals": intervals, "interpretation": "One bounded matched pair; nested timers are not added; changed physical work must be reported."}


def remediation_compare_cost(control, candidate):
    rows = []; exceedance = False; inconclusive = False
    fields = ("physicsWorldTimeNs", "narrowphaseTimeNs", "gjkTimeNs", "epaTimeNs", "manifoldTimeNs", "solverTimeNs")
    for episode in ("0", "8", "10"):
        for field in fields:
            old = control["intervals"][episode]["boundaries"][field]
            new = candidate["intervals"][episode]["boundaries"][field]
            for statistic in ("mean", "p95"):
                baseline = old["aggregate"][statistic]; value = new["aggregate"][statistic]
                allowance = max(100000 if field == "physicsWorldTimeNs" else 50000, .1 * baseline)
                differences = [b[statistic]-a[statistic] for a, b in zip(old["blocks"], new["blocks"])]
                spread = max(differences)-min(differences)
                numeric_pass = value <= baseline+allowance; comparable = spread <= allowance
                exceedance |= not numeric_pass; inconclusive |= not comparable
                rows.append({"episode": int(episode), "boundary": field, "statistic": statistic,
                    "control_ns": baseline, "candidate_ns": value, "allowance_ns": allowance,
                    "numeric_pass": numeric_pass, "block_difference_ns": differences,
                    "block_difference_range_ns": spread, "comparability_pass": comparable})
    return {"status": "INCONCLUSIVE" if inconclusive else "FAIL_NUMERIC_EXCEEDANCE" if exceedance else "PASS_BOUNDED_MATCHED_COST",
        "any_numeric_exceedance": exceedance, "any_inconclusive_spread": inconclusive, "rows": rows,
        "work": {episode: {"control": control["intervals"][episode]["work_counts"],
            "candidate": candidate["intervals"][episode]["work_counts"]} for episode in ("0", "8", "10")}}


def post_analyze_friction(data):
    """Coverage and independent work checks; original metric remains visible."""
    native = json.loads((data / "result.json").read_text())
    rows = remediation_csv(data / "post-ticks.csv")
    events = remediation_csv(data / "post-events.csv")
    stages = remediation_csv(data / "post-stages.csv")
    gates = remediation_csv(data / "focused-gates.csv")
    qualification = {(int(r["episode"]), int(r["tick"])): r for r in remediation_csv(data / "focused-qualification.csv")}
    initial = {(int(r["episode"]), int(r["slot"])): r for r in remediation_csv(data / "initial.csv")}
    expected = [(ep, tick) for ep, count in enumerate(FOCUSED_DURATIONS) if ep not in (0, 8, 10) for tick in range(1, count+1)]
    assert [(int(r["episode"]), int(r["tick"])) for r in rows] == expected
    assert all(math.isfinite(float(value)) for row in rows for value in row.values())
    assert len(rows) == native["post_ticks"] == 1680
    assert len(stages) == native["post_stage_records"] == 288 and len(events) == native["post_event_records"] <= 1024
    assert [(int(r["episode"]), int(r["tick"]), int(r["stage"]), int(r["slot"])) for r in stages] == [
        (ep, tick, stage, slot) for ep in (2, 9) for tick in range(1, 7) for stage in range(12) for slot in (1, 2)]
    assert [int(r["sequence"]) for r in events] == sorted(set(int(r["sequence"]) for r in events))
    assert sum(int(r["sliding"]) for r in rows) == native["post_sliding"]
    assert sum(int(r["legacy_positive"]) for r in rows) == native["legacy_positive_sliding"]
    assert sum(int(r["explained"]) for r in rows) == native["explained_infeasible_old"]
    assert all(int(gates[i]["checks"]) == native["post_sliding"] for i in (15, 16, 20))
    assert all(int(gates[i]["checks"]) == 1680 for i in (18, 19))
    for r in events:
        for role in ("a", "b"):
            body = initial[int(r["episode"]), int(r[role+"_slot"])]
            assert int(r[role+"_generation"]) == int(body["generation"])
        assert int(r["episode"]) in (2, 9) and 1 <= int(r["tick"]) <= 6
    for r in stages:
        assert int(r["generation"]) == int(initial[int(r["episode"]), int(r["slot"])]["generation"])

    def dot(a, b): return sum(x*y for x, y in zip(a, b))
    def vector(row, prefix): return [float(row[prefix+"_"+axis]) for axis in "xyz"]
    def kinetic(row, prefix, body):
        inv = float(body["inverse_mass"])
        if inv == 0: return 0.
        # Both admitted causal windows have isotropic inertia. This independent
        # calculation is compared at the original allowance, not a new tolerance.
        assert float(body["ixx"]) == float(body["iyy"]) == float(body["izz"])
        v = [float(row[prefix+"_v"+axis]) for axis in "xyz"]
        w = [float(row[prefix+"_w"+axis]) for axis in "xyz"]
        return .5/inv*(dot(v, v)+float(body["ixx"])*dot(w, w))

    windows = []; max_balance_error = 0.
    for ep in (2, 9):
        for tick in range(1, 7):
            selected = [r for r in events if int(r["episode"]) == ep and int(r["tick"]) == tick]
            assert len(selected) == int(qualification[ep, tick]["events"]), "causal window omitted events"
            assert selected, "causal window empty"
            by_stage = {(int(r["stage"]), int(r["slot"])): r for r in stages if int(r["episode"]) == ep and int(r["tick"]) == tick}
            before = sum(kinetic(by_stage[4, slot], "state", initial[ep, slot]) for slot in (1, 2))
            after = sum(kinetic(by_stage[7, slot], "state", initial[ep, slot]) for slot in (1, 2))
            work = 0.; operations = {}; sliding = []
            warm_normal = {}; block_contacts = 0; block_max_error = 0.
            for r in selected:
                op = int(r["operation"]); operations[str(op)] = operations.get(str(op), 0)+1
                key = tuple(int(r[name]) for name in (
                    "a_slot", "a_generation", "b_slot", "b_generation", "slot"))
                anchors = tuple(vector(r, "local_a") + vector(r, "local_b"))
                if op == 0:
                    normal_row = [float(r[f"jn_{i}"]) for i in range(12)]
                    assert all(math.isfinite(x) for x in normal_row)
                    warm_normal[key] = normal_row, anchors
                if op == 5:
                    # BlockNormal's unfilled row fields are not observations.
                    # Use its preceding warm row at the same fixed pose. This
                    # supplements the all-event source linkage and native Gate4.
                    assert r["impulses_known"] == "1" and key in warm_normal
                    normal_row, warm_anchors = warm_normal[key]
                    assert anchors == warm_anchors, "block contact anchor continuity failed"
                    dn = struct.unpack("<f", struct.pack("<f", float(r["after_0"])-float(r["before_0"])))[0]
                    expected_impulse = [component*dn for component in normal_row]
                    actual_impulse = (vector(r, "linear_a") + vector(r, "angular_a") +
                        vector(r, "linear_b") + vector(r, "angular_b"))
                    for observed, wanted in zip(actual_impulse, expected_impulse):
                        assert math.isfinite(observed) and math.isfinite(wanted)
                        error = abs(observed-wanted)
                        assert error <= 1e-5*max(1., abs(wanted)), "block cache delta / signed impulse mismatch"
                        block_max_error = max(block_max_error, error)
                    block_contacts += 1
                if op in (0, 1, 2, 3, 5):
                    assert r["impulses_known"] == "1"
                    for role in ("a", "b"):
                        body = initial[ep, int(r[role+"_slot"])]; inv = float(body["inverse_mass"])
                        if inv == 0 or (role == "b" and r["a_slot"] == r["b_slot"]): continue
                        inertia = float(body["ixx"])/inv
                        j, angular = vector(r, "linear_"+role), vector(r, "angular_"+role)
                        v = [float(r[role+"_before_v"+axis]) for axis in "xyz"]
                        w = [float(r[role+"_before_w"+axis]) for axis in "xyz"]
                        work += dot(v, j)+dot(w, angular)+.5*(inv*dot(j, j)+dot(angular, angular)/inertia)
                if op == 1:
                    old = [float(r["before_"+str(i)]) for i in range(3)]
                    new = [float(r["after_"+str(i)]) for i in range(3)]
                    sliding.append({"sequence": int(r["sequence"]), "slot": int(r["slot"]), "token": r["token"],
                        "before": old, "after": new, "normal_increment": new[0]-old[0],
                        "old_tangent_norm": math.hypot(*old[1:]), "updated_radius": .25*new[0],
                        "old_feasible_under_existing_allowance": math.hypot(*old[1:]) <= .25*new[0]+1e-5*max(1., abs(new[0]))})
            error = abs(after-before-work); max_balance_error = max(max_balance_error, error)
            assert error <= 1e-5*max(1., before), "independent window response balance failed"
            assert after-before <= 1e-5*max(1., before), "independent window response energy failed"
            windows.append({"episode": ep, "tick": tick, "event_count": len(selected), "operations": operations,
                "block_contacts_checked": block_contacts, "block_impulse_components_checked": 12*block_contacts,
                "block_max_component_error": block_max_error,
                "response_before_k": before, "response_after_k": after, "signed_response_work": work,
                "response_balance_error": error, "sliding": sliding})
    return {"pass": all(int(r["failures"]) == 0 for r in gates), "policy": "P20_POST_CANDIDATE_FRICTION_V1",
        "detailed_ticks": len(rows), "window_events": len(events), "window_stages": len(stages),
        "storage_bytes": native["post_storage_bytes"], "sliding_checks": native["post_sliding"],
        "legacy_positive_increment_count": native["legacy_positive_sliding"],
        "explained_infeasible_count": native["explained_infeasible_old"],
        "max_objective_upper": max(float(r["max_f_upper"]) for r in rows),
        "max_optimality_gap_upper": max(float(r["max_gap_upper"]) for r in rows),
        "max_intermediate_cone_excess": max(float(r["max_intermediate_cone"]) for r in rows),
        "independent_window_max_balance_error": max_balance_error, "windows": windows,
        "scope": "Prospective policy only; all historical failures remain unchanged; no whole-tick substitute"}


def remediation_analyze(stage):
    remediation_admission()
    run = remediation_run_path(stage)
    result = json.loads((run / "result.json").read_text())
    assert all(sha(run / p) == digest for p, digest in result["evidence"].items()), "captured evidence drift"
    output = remediation_analysis_path(stage)
    assert not output.exists(), "preserve prior analysis; no overwrite"
    output.mkdir(parents=True)
    data = run / "data"; report = {"stage": stage, "recorded_utc": now(), "run": {
        "path": (run / "result.json").relative_to(ROOT).as_posix(), "sha256": sha(run / "result.json")},
        "native_status": result["status"], "usage": conditional_usage(), "owner_visible_validation": False}
    try:
        assert result["status"] == "PASS_FOCUSED_NATIVE_INTEGRITY_OFFLINE_VALIDATION_PENDING", "native integrity incomplete"
        queries = remediation_csv(data / "focused-queries.csv")
        assert [(int(r["case"]), int(r["mode"])) for r in queries] == [(i, j) for i in range(25) for j in range(2)]
        for off, on in zip(queries[::2], queries[1::2]):
            assert all(off[k] == on[k] for k in off if k not in ("mode", "elapsed_ns", "observer_ns")), "query off/on mismatch"
            assert all(r["pointers_ok"] == r["unchanged"] == "1" for r in (off, on))
        helpers = [json.loads(line) for line in (data / "focused-helpers.jsonl").read_text().splitlines()]
        assert [(r["kind"], r["case"], r["mode"]) for r in helpers] == [
            (kind, i, j) for kind, count in (("gjk", 8), ("ca", 6), ("projection", 2)) for i in range(count) for j in range(2)]
        for off, on in zip(helpers[::2], helpers[1::2]):
            assert {k: v for k, v in off.items() if k != "mode"} == {k: v for k, v in on.items() if k != "mode"}
        assert all(r["pass"] for r in helpers if r["kind"] == "gjk")
        assert all(r["expected_build_behavior"] and r["unchanged"] for r in helpers if r["kind"] == "ca")
        qualifications = remediation_csv(data / "focused-qualification.csv")
        assert [(int(r["episode"]), int(r["tick"])) for r in qualifications] == [
            (episode, tick) for episode, count in enumerate(FOCUSED_DURATIONS) for tick in range(1, count+1)]
        assert all(math.isfinite(float(v)) for row in qualifications for v in row.values())
        assert all(int(r["bodies"]) == FOCUSED_COUNTS[int(r["episode"])] for r in qualifications)
        gates = remediation_csv(data / "focused-gates.csv"); assert [int(r["gate"]) for r in gates] == list(range(21 if POST_ACTIVE else 15))
        frames = remediation_csv(data / "frames.csv")
        episode_report = {}
        for episode in range(11):
            selected = [r for r in qualifications if int(r["episode"]) == episode]
            frame_rows = [r for r in frames if int(r["episode"]) == episode]
            assert frame_rows and int(frame_rows[-1]["tick"]) == FOCUSED_DURATIONS[episode]
            episode_report[str(episode)] = {"ticks": len(selected), "peak_energy": max(float(r["energy"]) for r in selected),
                "final_energy": float(selected[-1]["energy"]), "max_penetration": max(float(r["penetration"]) for r in selected),
                "missed_awake_ground": sum(int(r["missed_awake_ground"]) for r in selected),
                "maximum_capacity_bytes": max(int(r["capacity_bytes"]) for r in selected),
                "capacity_growth": int(selected[-1]["capacity_growth"]), "final_active": int(selected[-1]["active"]),
                "final_sleeping": int(selected[-1]["sleeping"]), "observer_ns": sum(int(r["observer_ns"]) for r in selected),
                "discarded_seconds": float(frame_rows[-1]["discarded"]), "pending_seconds": float(frame_rows[-1]["pending"])}
        cost = remediation_cost(data); write_new(output / "cost.json", cost)
        report.update({"status": "COMPLETE_OFFLINE_COUNTS_PARITY_AND_METRICS_PENDING_INDEPENDENT_CAUSAL_REVIEW",
            "query_geometry_failures": [int(r["case"]) for r in queries if r["mode"] == "0" and r["geometry_ok"] != "1"],
            "helper_requirement_failures": [{"kind": r["kind"], "case": r["case"]} for r in helpers if r["mode"] == 0 and not r["pass"]],
            "physical_gates": gates, "episodes": episode_report,
            "cost": {"path": (output / "cost.json").relative_to(ROOT).as_posix(), "sha256": sha(output / "cost.json")}})
        if stage == "candidate":
            old_data = remediation_run_path("control") / "data"
            old_queries = remediation_csv(old_data / "focused-queries.csv")
            for old, new in zip(old_queries, queries):
                if old["hit"] == "1":
                    assert old["hit"] == new["hit"] and all(old[k] == new[k] for k in old if k.startswith("contact_bits_")), "previous success bits changed"
            assert (old_data / "focused-inputs.csv").read_bytes() == (data / "focused-inputs.csv").read_bytes(), "matched query input bits changed"
            if POST_ACTIVE:
                previous_data = CONDITIONAL / "runs/Release-candidate-01/data"
                previous_queries = remediation_csv(previous_data / "focused-queries.csv")
                assert len(previous_queries) == len(queries)
                assert all({k: v for k, v in old.items() if k not in ("elapsed_ns", "observer_ns")} ==
                    {k: v for k, v in new.items() if k not in ("elapsed_ns", "observer_ns")}
                    for old, new in zip(previous_queries, queries)), "exact representation changed a prior candidate query"
                previous_helpers = [json.loads(line) for line in (previous_data / "focused-helpers.jsonl").read_text().splitlines()]
                assert previous_helpers == helpers, "exact representation changed prior helper behavior"
                report["post_candidate_friction"] = post_analyze_friction(data)
            comparison = remediation_compare_cost(remediation_cost(old_data), cost)
            old_result = json.loads(old_data.parent.joinpath("result.json").read_text())
            comparison["native_priority_affinity_equal"] = old_result["environment"]["native_process"] == result["environment"]["native_process"]
            if not comparison["native_priority_affinity_equal"]: comparison["status"] = "INCONCLUSIVE"
            write_new(output / "cost-comparison.json", comparison)
            report["cost_comparison"] = {"path": (output / "cost-comparison.json").relative_to(ROOT).as_posix(), "sha256": sha(output / "cost-comparison.json"), "status": comparison["status"]}
            report["physical_numeric_pass"] = not report["query_geometry_failures"] and not report["helper_requirement_failures"] and all(int(r["failures"]) == 0 for r in gates)
            if POST_ACTIVE: report["physical_numeric_pass"] &= report["post_candidate_friction"]["pass"]
    except (AssertionError, ValueError, KeyError, OSError) as error:
        report["status"] = "FAIL_OR_INCOMPLETE_PRESERVED_NO_RETRY"; report["failure"] = str(error)
    write_new(output / "analysis.json", report)
    print(json.dumps({"analysis": str(output / "analysis.json"), "status": report["status"]}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("static", "build", "run", "analyze", "target-static", "target-build", "target-prepare", "target-run", "attended-coverage", "attended-static", "attended-build", "attended-prepare", "attended-run", "replay-static", "replay-build", "replay-prepare", "replay-run", "conditional-static", "conditional-build", "conditional-prepare", "conditional-run", "remediation-static", "remediation-build", "remediation-prepare", "remediation-run", "remediation-analyze", "post-static", "post-build", "post-prepare", "post-run", "post-analyze"))
    parser.add_argument("--stage", choices=("control", "candidate"))
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    parser.add_argument("--observation", choices=("off", "on"))
    parser.add_argument("--owner-ready-record")
    args = parser.parse_args()
    if args.action.startswith("post-"):
        assert args.stage == "candidate", "no new Control, diagnosis or owner launch in this action"
        POST_ACTIVE = True; post_authority()
        args.action = "remediation-" + args.action.removeprefix("post-")
    if args.action.startswith("remediation-"):
        assert args.stage
        if args.action == "remediation-static": remediation_freeze(args.stage)
        elif args.action == "remediation-build":
            assert args.configuration
            remediation_build(args.stage, args.configuration)
        elif args.action == "remediation-prepare": remediation_prepare(args.stage)
        elif args.action == "remediation-run": remediation_run(args.stage)
        elif args.action == "remediation-analyze": remediation_analyze(args.stage)
    elif args.action == "conditional-static":
        conditional_static()
    elif args.action == "conditional-build":
        assert args.configuration
        conditional_build(args.configuration)
    elif args.action == "conditional-prepare":
        conditional_prepare()
    elif args.action == "conditional-run":
        conditional_run()
    elif args.action == "replay-static":
        replay_static()
    elif args.action == "replay-build":
        assert args.configuration
        replay_build(args.configuration)
    elif args.action == "replay-prepare":
        replay_prepare()
    elif args.action == "replay-run":
        replay_run()
    elif args.action == "attended-coverage":
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
