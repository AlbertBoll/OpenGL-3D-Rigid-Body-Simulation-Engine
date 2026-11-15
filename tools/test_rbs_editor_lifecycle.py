"""Bounded reuse of the existing Native/Jolt switch driver for an RBS UI candidate."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--config", choices=["Debug", "Release"], required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "logs/ui"):
        raise ValueError("Evidence must remain under logs/ui")
    output.mkdir(parents=True, exist_ok=False)
    exe = ROOT / f"bin/{args.config}/RigidBodySimulation/RigidBodySimulation.exe"
    results = []
    for scene in ("distance-chain", "box-stack"):
        folder = output / scene
        folder.mkdir()
        command = [str(exe), "--scene", scene, "--backend", "native", "--backend-switch-checks", "true",
                   "--physics-smoke-ticks", "120"]
        started = time.monotonic()
        result = subprocess.run(command, cwd=folder, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        (folder / "stdout.log").write_bytes(result.stdout)
        text = result.stdout.decode(errors="replace")
        passed = result.returncode == 0 and "BACKEND_SWITCH_CHECKS_PASS" in text and "BACKEND_SWITCH_ROLLBACK_PASS" in text
        results.append(dict(scene=scene, command=command, exit_code=result.returncode, seconds=time.monotonic()-started,
                            result="PASS" if passed else "FAIL", binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                            summary=[line for line in text.splitlines() if "BACKEND_SWITCH" in line]))
        (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(f"{args.config} {scene}: {'PASS' if passed else 'FAIL'}", flush=True)
        if not passed:
            print(text[-4000:])
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
