"""Focused Hierarchy model and existing backend transaction checks; no qualification."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / 'logs/ui/hierarchy-01'):
        raise ValueError('Use new Hierarchy evidence destination')
    output.mkdir(parents=True, exist_ok=False)
    exe = ROOT / f'bin/{args.config}/RigidBodySimulation/RigidBodySimulation.exe'
    sha = hashlib.sha256(exe.read_bytes()).hexdigest()
    # Debug Diamond construction alone takes ~47s in retained candidate-04
    # evidence. This transaction constructs five worlds; allow that bounded work.
    timeout = 360 if args.config == 'Debug' else 180
    rows = []
    cases = [('model', ['--hierarchy-checks'], 'HIERARCHY_CHECK_PASS')]
    cases += [(scene, ['--scene', scene, '--backend', 'native', '--backend-switch-checks', 'true',
                       '--physics-smoke-ticks', '120'], 'BACKEND_SWITCH_CHECKS_PASS')
              for scene in ('distance-chain', 'sphere-diamond', 'sphere-lattice', 'box-stack')]
    for name, flags, marker in cases:
        folder = output / name
        folder.mkdir()
        command = [str(exe), *flags]
        start = time.monotonic()
        timed_out = False
        with (folder / 'stdout.log').open('wb') as stream:
            try:
                run = subprocess.run(command, cwd=folder, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
                exit_code = run.returncode
            except subprocess.TimeoutExpired:
                timed_out, exit_code = True, None
        text = (folder / 'stdout.log').read_text(errors='replace')
        passed = exit_code == 0 and marker in text
        if name != 'model':
            passed = passed and 'BACKEND_SWITCH_ROLLBACK_PASS' in text and 'backend=Jolt' in text
        rows.append(dict(case=name, config=args.config, command=command, exit=exit_code,
                         timeout_seconds=timeout,
                         seconds=time.monotonic()-start, binary_sha256=sha, result='PASS' if passed else 'TIMEOUT' if timed_out else 'FAIL',
                         summary=[line for line in text.splitlines() if 'HIERARCHY_CHECK' in line or 'BACKEND_SWITCH' in line]))
        (output / 'results.json').write_text(json.dumps(rows, indent=2)+'\n')
        print(args.config, name, rows[-1]['result'], flush=True)
        if not passed:
            print(text[-4000:])
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
