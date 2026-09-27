"""Phase 68 minimum compilation and RigidBodySimulation-only functional validation.
No standalone executable probes, historical timing workflow, or performance acceptance.
"""
import argparse
import csv
import json
import os
import hashlib
from pathlib import Path
import subprocess
import sys
from test_runtime_failure import Validation, ROOT, VC


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--action', choices=['objects', 'build', 'runtime', 'native-close', 'startup'], required=True)
    p.add_argument('--case', choices=['success', 'relative', 'missing', 'frames-open', 'metadata-open',
        'targets-open', 'image-write', 'scene-dimensions', 'asset-root', 'asset-manifest', 'terrain-decode', 'audio-bank'], default='success')
    a = p.parse_args()
    v = Validation(a.configuration, a.output)
    v.env['Path'] = str(VC / 'bin/Hostx64/x64') + os.pathsep + str(ROOT / 'bin' / a.configuration / 'RigidBodySimulation') + os.pathsep + os.environ.get('PATH', '')
    # Retain the application's normal shadow settings; this is not a new baseline.
    v.env.pop('GENGINE_SHADOW_RESOLUTION', None)
    v.report.update(policy='PHASE_68_OWNER_VALIDATION_AMENDMENT', workload='RigidBodySimulation only',
        performance_acceptance='NOT RUN; timings in collector output are not compared or accepted')
    try:
        if a.action == 'objects':
            v.compile('baseline-header', [ROOT / 'tools/baseline_error_probe.cpp'], boundary=True)
            for name in ('RenderBaseline', 'BaseApp'):
                directory = v.out / name; directory.mkdir(exist_ok=True)
                v.invoke('object-' + name, [VC / 'bin/Hostx64/x64/cl.exe', *v.flags,
                    *('/I' + str(q) for q in v.includes), '/c', ROOT / ('GEngine/src/Core/' + name + '.cpp'),
                    '/Fo' + str(directory) + os.sep], timeout=300)
        elif a.action == 'build':
            v.build(['RigidBodySimulation'])
        elif a.action == 'native-close':
            from test_frame_submission import application_smoke
            result = application_smoke(ROOT / 'bin' / a.configuration / 'RigidBodySimulation/RigidBodySimulation.exe', v.out / 'rbs-native-close', v.env)
            v.report['steps'].append(result)
            if result['result'] != 'PASS': raise RuntimeError('RBS native-close failed')
        elif a.action == 'startup':
            staged = Path(v.env['GENGINE_ASSET_ROOT'])
            fixture = v.out / 'asset-fixture'; fixture.mkdir(exist_ok=True)
            markers = []
            if a.case == 'asset-root':
                v.env['GENGINE_ASSET_ROOT'] = 'relative-root'
                markers = ['Runtime asset failure code=', 'GENGINE_ASSET_ROOT must be a nonempty absolute path']
            elif a.case == 'asset-manifest':
                v.env['GENGINE_ASSET_ROOT'] = str(fixture)
                (fixture / 'startup').mkdir(exist_ok=True)
                (fixture / 'startup/RigidBodySimulation.txt').write_text('invalid header\n')
                markers = ['Runtime asset failure code=', 'invalid startup dependency list header']
            elif a.case in ('terrain-decode', 'audio-bank'):
                manifest = staged / 'startup/RigidBodySimulation.txt'
                replaced = 'Images/heightmap.png' if a.case == 'terrain-decode' else 'Audio/Bank/Master Bank.bank'
                sources = manifest.read_text().splitlines()[1:] + ['startup/RigidBodySimulation.txt', 'Fonts/Carlito-Regular.ttf']
                identities = {}
                for relative in sources:
                    source = staged / relative; target = (fixture / relative).resolve()
                    if not target.is_relative_to(fixture.resolve()): raise RuntimeError('Fixture escaped its directory')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    identities[relative] = hashlib.sha256(source.read_bytes()).hexdigest()
                    # Never alter linked bytes: the deliberate bad asset gets its own new file.
                    if relative == replaced: target.write_bytes(b'invalid nonempty asset for typed startup failure')
                    elif not target.exists(): os.link(source, target)
                v.env['GENGINE_ASSET_ROOT'] = str(fixture)
                v.report['fixture'] = {'replacement': replaced, 'source_hashes': identities}
                markers = ['Cannot decode terrain heightmap'] if a.case == 'terrain-decode' else ['audio bank load', 'audio-code=']
            else: raise RuntimeError('Choose an explicit startup failure case')
            exe = ROOT / 'bin' / a.configuration / 'RigidBodySimulation/RigidBodySimulation.exe'
            v.invoke('rbs-' + a.case, [exe], expected=1, markers=markers, timeout=240, cwd=v.out)
            if a.case in ('terrain-decode', 'audio-bank'):
                for relative, identity in identities.items():
                    if hashlib.sha256((staged / relative).read_bytes()).hexdigest() != identity:
                        raise RuntimeError('Staged source asset changed: ' + relative)
                v.report['staged_assets_preserved'] = True
        else:
            if a.case in ('asset-root', 'asset-manifest', 'terrain-decode', 'audio-bank'):
                raise RuntimeError('Startup case requires --action startup')
            directory = v.out / 'collection'; directory.mkdir(exist_ok=True)
            block = {'frames-open': 'frames.csv', 'metadata-open': 'runtime.txt', 'targets-open': 'targets.txt',
                'image-write': 'diagnostic.bmp'}.get(a.case)
            if block: (directory / block).mkdir(exist_ok=True)
            v.env['GENGINE_BASELINE_OUTPUT'] = ('relative' if a.case == 'relative' else
                str(directory / 'does-not-exist') if a.case == 'missing' else str(directory))
            if a.case == 'scene-dimensions': v.env['GENGINE_BASELINE_SCENE_TARGET'] = '1'
            exe = ROOT / 'bin' / a.configuration / 'RigidBodySimulation/RigidBodySimulation.exe'
            expected = 0 if a.case == 'success' else 1
            codes = {'relative': 1, 'missing': 1, 'frames-open': 2, 'metadata-open': 2,
                'targets-open': 2, 'image-write': 10, 'scene-dimensions': 12}
            marker = [] if expected == 0 else ['Application runtime failure', 'subsystem=RenderBaseline code=' + str(codes[a.case]) + ' ']
            log = v.invoke('rbs-' + a.case, [exe], expected=expected, markers=marker, timeout=600, cwd=v.out)
            if 'GEngine application failed:' in log: raise RuntimeError('Measurement failure reached legacy catch')
            if expected == 0:
                metadata = (directory / 'runtime.txt').read_text()
                for line in ('protocol=phase13-frozen-v1', 'width=1280', 'height=720', 'vsync=0', 'warmup=120', 'samples=240', 'completed_samples=240'):
                    if line not in metadata.splitlines(): raise RuntimeError('Protocol mismatch: ' + line)
                with (directory / 'frames.csv').open(newline='') as f: rows = list(csv.DictReader(f))
                if len(rows) != 240 or [int(row['sample']) for row in rows] != list(range(240)):
                    raise RuntimeError('Sampling schedule changed')
                if any(int(row['physics_steps']) for row in rows): raise RuntimeError('Frozen workload stepped physics')
                for name in ('diagnostic.bmp', 'targets.txt'):
                    if not (directory / name).is_file(): raise RuntimeError('Missing output: ' + name)
                v.report['protocol'] = 'PASS; 1280x720 VSYNC=0 warmup=120 samples=240 zero physics steps'
            # Process exited normally; verify collector output handles have been released.
            for item in directory.iterdir():
                if item.is_file():
                    moved = item.with_suffix(item.suffix + '.closed'); item.rename(moved); moved.rename(item)
            v.report['closed_outputs'] = 'PASS; all existing output files renameable after process exit'
        v.report['result'] = 'PASS'
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        v.report.update(result='FAIL', reason=str(error)); print(error, flush=True); return 1
    finally: v.save()

if __name__ == '__main__': sys.exit(main())
