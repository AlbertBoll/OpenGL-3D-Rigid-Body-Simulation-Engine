"""Validate the Phase 66 native-free segment conversion and retained helper ownership."""
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
    out = args.output or ROOT / 'logs/rendering/phase66/point-light-segments' / args.configuration
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
        source = ROOT / 'tools/point_light_segments_probe.cpp'
        v.compile('segment-header', [source], ['/DPOINT_LIGHT_SEGMENTS_SCHEMA_ONLY'], boundary=True)
        exe = v.compile('point-light-segments-probe', [source])
        v.invoke('probe', [exe], markers=[
            '[PASS] segment-conversion/defaults/truncation/int-edges/diagnostics/no-context',
            '[PASS] helper-payload/unique-points/defaults/ownership cycles=2'], cwd=v.out / 'probe-run')
        v.smoke(['GEngineEditor', 'Breakout', 'RayTracing', 'RigidBodySimulation'])
        v.report['result'] = 'PASS'
        v.report['engine_library_sha256'] = hashlib.sha256((ROOT / 'bin' / args.configuration / 'GEngine/GEngine.lib').read_bytes()).hexdigest()
        return 0
    except (OSError, RuntimeError) as error:
        v.report.update(result='FAIL', reason=str(error)); print(str(error), flush=True); return 1
    finally: v.save()

if __name__ == '__main__': sys.exit(main())
