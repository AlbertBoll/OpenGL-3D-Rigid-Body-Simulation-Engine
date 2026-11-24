"""Run the ordinary Book Conformance smoke and evaluate its single shared-world capture."""
import argparse
import math
import re
import shutil
import sys

import pre_editor_phase32_conformance as prior


def run(destination, build):
    root = prior.ROOT
    product = prior.read(build / "product.json")
    exe = root / product["path"]
    prior.require(prior.sha(exe) == product["sha256"], "Release product changed")
    assets = prior.safe(root / "bin/Release/assets")
    prior.require((assets / "startup/RigidBodySimulation.txt").is_file(), "Missing staged RBS assets")
    prior.census()
    destination.mkdir()
    cwd = prior.safe(root / "runtime/RigidBodySimulation/Release" / ("phase32-" + destination.name))
    cwd.mkdir(exist_ok=False)
    env = dict(prior.environment(), GENGINE_ASSET_ROOT=str(assets), GENGINE_PHASE32_BOOK_CAPTURE="1")
    args = [str(exe), "--scene", "book-conformance", "--backend", "jolt", "--physics-smoke-ticks", "120"]
    prior.write(destination / "reservation.json", dict(binary=product, command=args, cwd=str(cwd),
                seconds=180, ticks=120, starts=1, purpose="construction, appearance, independent controls, finite steps, cost, reset, teardown"))
    sys.path.insert(0, str(root / "logs/pre_editor/phase-29/step-safety-job-01"))
    import owned_job_02 as jobs
    job = jobs.OwnedJob({})
    try:
        job.configure()
        jobs.expect_counts(job.state(), 0, 0, [])
        with (destination / "stdout.log").open("xb") as log:
            process = jobs.run_owned(job, args, cwd, env, log, 180, prior.sha, product["sha256"],
                                     lambda name, data: prior.write(destination / name, data))
    finally:
        cleanup = job.close()
    prior.write(destination / "job-process.json", process)
    image = cwd / "integrated-viewport.png"
    if image.is_file():
        shutil.copyfile(image, destination / image.name)
    log = (destination / "stdout.log").read_text(errors="replace")
    rows = [line.split("BOOK_STEP ", 1)[1].split() for line in log.splitlines() if "BOOK_STEP " in line]
    times = sorted(float(row[1]) for row in rows)
    def percentile(fraction):
        return times[math.ceil(len(times) * fraction) - 1] if times else None
    checks = {
        "owned_clean_exit": process.get("exit") == 0 and not process["errors"] and not cleanup and not process["timeout"],
        "same_binary": prior.sha(exe) == product["sha256"],
        "ticks": [int(row[0]) for row in rows] == list(range(1, 121)),
        "finite": all(math.isfinite(float(row[i])) for row in rows for i in (1, 2, 7)),
        "motor_commands": bool(rows) and all((r[3] == "true") == (not 31 <= int(r[0]) <= 40) for r in rows),
        "motor_turned": any(abs(float(r[2])) > .1 for r in rows),
        "mover_commands": bool(rows) and all((r[4] == "true") == (not 61 <= int(r[0]) <= 75) and
                                         (r[5] == "true") == (int(r[0]) >= 76) for r in rows),
        "rider_contact": any(r[8] == "true" for r in rows),
        "world_and_skin": image.is_file() and "wood=settled rust_maps=5" in log,
        "reset": bool(re.search(r"BOOK_RESET_PASS old_world=(\d+) new_world=(\d+) ticks=0 bodies=48 joints=15", log)),
        "finite_smoke": "BACKEND_SMOKE_PASS backend=Jolt scene=book-conformance ticks=120 bodies=48 joints=15" in log,
    }
    p95 = percentile(.95)
    result = dict(status="PASS" if all(checks.values()) else "FAIL", checks=checks, samples=len(times),
                  timing_boundary="ordinary Scene::Update at one fixed tick per frame; readback and file writes excluded",
                  scene_update_ms={"p50": percentile(.5), "p95": p95, "max": max(times, default=None)},
                  fixed_step_ms=1000. / 60., source_log=prior.motion.identity(destination / "stdout.log"), shutdown=process.get("exit"))
    prior.write(destination / "assessment.json", result)
    print(result, flush=True)
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("attempt", type=int, choices=range(1, 100))
    parser.add_argument("build", type=int, choices=range(1, 100))
    args = parser.parse_args()
    base = prior.EVIDENCE / "integrated-01"
    sys.exit(run(base / f"integrated-runtime-{args.attempt:02d}", base / f"build-Release-{args.build:02d}"))
