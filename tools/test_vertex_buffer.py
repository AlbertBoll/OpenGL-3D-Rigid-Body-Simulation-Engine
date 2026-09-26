"""Validate the Phase 66 vertex-buffer result contract and retained ownership on real GL."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from test_runtime_failure import ROOT, VC, Validation


def startup_lifetimes(parent):
    v = Validation(parent.config, parent.out / 'startup')
    try:
        obj = v.out / 'glad-renamed.obj'
        v.invoke('compile-test-loader', [VC / 'bin/Hostx64/x64/cl.exe', '/nologo', '/c', '/TC',
            '/MTd' if v.config == 'Debug' else '/MT', '/Od' if v.config == 'Debug' else '/O2',
            '/DgladLoadGL=Phase66RealGladLoadGL', *('/I' + str(p) for p in v.includes),
            ROOT / 'external/glad/src/glad.c', '/Fo' + str(obj)])
        exe = v.compile('GEngineEditor', [ROOT / 'tools/uniform_renderbuffer_probe.cpp', obj], ['/DUBO_STARTUP_PROBE'])
        for mode in ('failure', 'success'):
            markers = ['[PASS] UBO startup ' + mode, '[OBSERVE] buffers=0 engine=0 SDL=0 TTF=0']
            if mode == 'failure':
                markers += ['Application uniform buffer operation=UniformBufferObject::Create code=4 count=16 binding=0 stride=64: Uniform buffer storage allocation failed']
            log = v.invoke('startup-' + mode, [exe, mode], expected=1 if mode == 'failure' else 0,
                markers=markers, cwd=v.out / mode)
            if 'GEngine application failed:' in log:
                raise RuntimeError('Startup failure used legacy exception transport')
        v.report['result'] = 'PASS'
    except (OSError, RuntimeError) as error:
        v.report.update(result='FAIL', reason=str(error))
        raise
    finally:
        v.save()
        parent.report['steps'].append({'name': 'startup-lifetimes', 'result': v.report.get('result', 'FAIL'),
            'evidence': str(v.out / 'results.json')})
        parent.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--reuse-build', type=Path, help='Completed results after verification of unchanged production/build inputs')
    parser.add_argument('--startup-lifetimes', action='store_true', help='Run real BaseApp/EntryPoint registration and UBO success/failure teardown gates')
    args = parser.parse_args()
    out = args.output or ROOT / 'logs/rendering/phase66/vertex-buffer' / args.configuration
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
        source = ROOT / 'tools/vertex_buffer_probe.cpp'
        v.compile('vertex-header', [source], ['/DVERTEX_BUFFER_SCHEMA_ONLY'], boundary=True)
        exe = v.compile('vertex-buffer-probe', [source, ROOT / 'tools/geometry_resource_probe.cpp'])
        v.invoke('probe', [exe], markers=[
            '[PASS] vertex-buffer-upload-safety cycles=2', '[PASS] vertex-resource-RAII cycles=2',
            '[PASS] typed-driver-failures/diagnostics/binding/partial-owner-cleanup/recovery',
            '[PASS] Geometry transform position/normal CPU+GPU payload/layout/preserved-owner exact-buffers=4',
            '[PASS] Geometry temporary capped-shape retirement caps=6 exact-buffers=40'],
            cwd=v.out / 'probe-run')
        if args.startup_lifetimes:
            startup_lifetimes(v)
        v.smoke(['GEngineEditor', 'Breakout', 'RayTracing', 'RigidBodySimulation'])
        v.report['result'] = 'PASS'
        v.report['engine_library_sha256'] = hashlib.sha256((ROOT / 'bin' / args.configuration / 'GEngine/GEngine.lib').read_bytes()).hexdigest()
        return 0
    except (OSError, RuntimeError) as error:
        v.report.update(result='FAIL', reason=str(error)); print(str(error), flush=True); return 1
    finally: v.save()

if __name__ == '__main__': sys.exit(main())
