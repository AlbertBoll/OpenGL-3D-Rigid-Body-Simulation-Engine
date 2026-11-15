"""Bounded shared impulse validation; no qualification/default changes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PRESETS = ('geometry-gallery', 'sphere-diamond', 'sphere-lattice', 'box-stack',
           'sphere-box-stack', 'distance-pendulum', 'distance-chain', 'distance-chain-contact')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', required=True, choices=['Debug', 'Release'])
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--checks', nargs='*', default=['impulse-editor'])
    parser.add_argument('--presets', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'logs/ui/impulse-editor-01'):
        raise ValueError('Use this campaign evidence directory')
    out.mkdir(parents=True, exist_ok=False)
    exe = ROOT / f'bin/{args.config}/RigidBodySimulation/RigidBodySimulation.exe'
    identity = hashlib.sha256(exe.read_bytes()).hexdigest()
    results = []

    def run(name, flags, env, marker, cwd):
        start = time.monotonic()
        command = [str(exe), *flags]
        with (out / (name + '.log')).open('w') as log:
            process = subprocess.run(command, cwd=cwd, env=env, stdout=log,
                                     stderr=subprocess.STDOUT, timeout=240)
        text = (out / (name + '.log')).read_text(errors='replace')
        passed = process.returncode == 0 and (not marker or marker in text)
        row = dict(name=name, config=args.config, binary_sha256=identity, command=command,
                   exit_code=process.returncode, seconds=time.monotonic()-start,
                   result='PASS' if passed else 'FAIL')
        results.append(row)
        (out / 'results.json').write_text(json.dumps(results, indent=2)+'\n')
        print(json.dumps(row), flush=True)
        return passed

    for check in args.checks:
        if not run(check, ['--'+check+'-checks'], os.environ.copy(), '', ROOT):
            return 1
    if args.presets:
        for backend in ('native', 'jolt'):
            for preset in PRESETS:
                name = backend+'-'+preset
                runtime = out / name
                runtime.mkdir()
                env = os.environ.copy()
                env['GENGINE_IMPULSE_PRESET_CHECK'] = '1'
                if not run(name, ['--scene', preset, '--backend', backend,
                                  '--physics-smoke-ticks', '12'], env,
                           'IMPULSE_PRESET_PASS', runtime):
                    return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
