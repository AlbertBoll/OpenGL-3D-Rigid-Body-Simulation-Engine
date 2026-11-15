"""Bounded admission/lifecycle checks, not Phase 26 qualification."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PRESETS = ('geometry-gallery', 'sphere-diamond', 'sphere-lattice', 'box-stack',
           'sphere-box-stack', 'distance-pendulum', 'distance-chain', 'distance-chain-contact')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--completed-from', type=Path, help='Reuse completed matching-binary rows after an interrupted runner; never overwrite prior evidence')
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / 'logs/ui/owner-corrections-04'):
        raise ValueError('Use new candidate evidence destination')
    output.mkdir(parents=True, exist_ok=False)
    exe = ROOT / f'bin/{args.config}/RigidBodySimulation/RigidBodySimulation.exe'
    sha = hashlib.sha256(exe.read_bytes()).hexdigest()
    results = []
    membership = {}
    completed = {}
    if args.completed_from:
        for row in json.loads((args.completed_from/'results.json').read_text()):
            if row['config'] != args.config or row['binary_sha256'] != sha or row['result'] != 'PASS':
                raise ValueError('Interrupted-run reuse requires identical binary/config and completed PASS')
            completed[row['backend'],row['scene']] = row
    for backend in ('native', 'jolt'):
        for scene in PRESETS:
            if (backend,scene) in completed:
                row=dict(completed[backend,scene],reused_from=str(args.completed_from))
                results.append(row)
                if backend=='native':
                    counts=re.search(r'bodies=(\d+) joints=(\d+)',row['summary'])
                    membership[scene]=tuple(map(int,counts.groups()))
                continue
            folder = output / f'{backend}-{scene}'
            folder.mkdir()
            command = [str(exe), '--scene', scene, '--backend', backend, '--physics-smoke-ticks', '12']
            start = time.monotonic()
            with (folder / 'stdout.log').open('wb') as stream:
                run = subprocess.run(command, cwd=folder, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
            text = (folder / 'stdout.log').read_text(errors='replace')
            match = re.search(r'BACKEND_SMOKE_PASS backend=(\w+) scene=([\w-]+) ticks=(\d+) bodies=(\d+) joints=(\d+)', text)
            passed = run.returncode == 0 and match and match[1].lower() == backend and match[2] == scene and match[3] == '12'
            if match:
                counts = tuple(map(int, match.group(4, 5)))
                if backend == 'native': membership[scene] = counts
                else: passed = passed and membership.get(scene) == counts
            results.append(dict(config=args.config, backend=backend, scene=scene, command=command,
                exit=run.returncode, seconds=time.monotonic()-start, binary_sha256=sha,
                result='PASS' if passed else 'FAIL', summary=match[0] if match else None))
            (output / 'results.json').write_text(json.dumps(results, indent=2)+'\n')
            print(f'{args.config} {backend} {scene}: {results[-1]["result"]}', flush=True)
    return int(any(r['result'] != 'PASS' for r in results))


if __name__ == '__main__':
    raise SystemExit(main())
