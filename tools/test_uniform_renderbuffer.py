"""Validate the Phase 66 typed renderbuffer ownership and existing target/UBO regressions."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from test_runtime_failure import ROOT, Validation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--reuse-build', type=Path, help='Completed results after verification of unchanged production/build inputs')
    args = parser.parse_args()
    out = args.output or ROOT / 'logs/rendering/phase66/renderbuffer' / args.configuration
    v = Validation(args.configuration, out)
    try:
        if args.reuse_build:
            prior = json.loads(args.reuse_build.read_text())
            build = next(s for s in prior['steps'] if s['name'] == 'build')
            if build['result'] != 'PASS' or prior['configuration'] != args.configuration:
                raise RuntimeError('Invalid completed build evidence')
            v.report['steps'].append(dict(build, reuse=str(args.reuse_build.resolve())))
        else:
            v.build(['GEngineEditor', 'Breakout', 'RayTracing', 'RigidBodySimulation'])
        source = ROOT / 'tools/uniform_renderbuffer_probe.cpp'
        v.compile('renderbuffer-header', [source], ['/DRENDERBUFFER_SCHEMA_ONLY'], boundary=True)
        exe = v.compile('renderbuffer-probe', [source])
        v.invoke('probe', [exe], markers=['[PASS] uniform-renderbuffer-RAII cycles=2',
            '[PASS] RBO typed-errors/diagnostics/binding/partial-cleanup/moves/recovery'], cwd=v.out / 'probe-run')
        if args.configuration == 'Debug':
            for mode in ('--reject-ubo-delete', '--reject-rbo-delete'):
                v.invoke(mode[2:], [exe, mode], expected=86, markers=['[GLThread] assertion failed:'])
        v.smoke(['GEngineEditor', 'Breakout', 'RayTracing', 'RigidBodySimulation'])
        v.report['result'] = 'PASS'
        v.report['engine_library_sha256'] = hashlib.sha256((ROOT / 'bin' / args.configuration / 'GEngine/GEngine.lib').read_bytes()).hexdigest()
        return 0
    except (OSError, RuntimeError) as error:
        v.report.update(result='FAIL', reason=str(error)); print(str(error), flush=True); return 1
    finally: v.save()

if __name__ == '__main__': sys.exit(main())
