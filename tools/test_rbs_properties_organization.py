"""Focused existing RBS checks for the Properties-only candidate; preserve old runs."""
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
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'logs/ui/properties-organization-01'):
        raise ValueError('Use the current candidate evidence root')
    out.mkdir(parents=True, exist_ok=False)
    exe = ROOT / f'bin/{args.config}/RigidBodySimulation/RigidBodySimulation.exe'
    sha = hashlib.sha256(exe.read_bytes()).hexdigest()
    cases = [('numeric-input', 'NUMERIC_INPUT_CHECKS_PASS'),
             ('owner05', 'OWNER05_CHECK_PASS'),
             ('editor-correction', 'EDITOR_CORRECTION_CHECKS_PASS'),
             ('selection-batch', 'SELECTION_BATCH_PASS'),
             ('impulse-boundary', 'IMPULSE_BOUNDARY_PASS')]
    results = []
    for name, marker in cases:
        command = [str(exe), '--' + name + '-checks']
        start = time.monotonic()
        with (out / (name + '.log')).open('wb') as log:
            try:
                code = subprocess.run(command, cwd=ROOT, stdout=log,
                                      stderr=subprocess.STDOUT, timeout=240).returncode
            except subprocess.TimeoutExpired:
                code = None
        text = (out / (name + '.log')).read_text(errors='replace')
        passed = code == 0 and marker in text
        results.append(dict(case=name, command=command, binary_sha256=sha,
                            exit_code=code, seconds=time.monotonic()-start,
                            result='PASS' if passed else 'FAIL',
                            markers=[s for s in text.splitlines() if '_PASS' in s or '_FAIL' in s]))
        (out / 'results.json').write_text(json.dumps(results, indent=2)+'\n')
        print(args.config, name, results[-1]['result'], flush=True)
        if not passed:
            print(text[-3000:])
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
