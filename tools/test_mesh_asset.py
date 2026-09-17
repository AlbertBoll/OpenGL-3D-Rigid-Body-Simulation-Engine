"""Build maintained consumers and validate the context-independent CPU mesh model."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import winreg

from rendering_validation import ROOT, toolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--no-build', action='store_true', help='Reuse matching consumer/library builds; compile and run the CPU probe')
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / 'logs/rendering/phase31/final' / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'configuration': config, 'steps': [], 'build_reused': args.no_build}
    env = dict(os.environ)
    env['SDL_VIDEODRIVER'] = 'phase31-cpu-must-not-initialize-video'

    def invoke(name, command, timeout=120, marker=None):
        command = [str(x) for x in command]
        log = out / (name + '.log')
        step = {'name': name, 'command': command, 'cwd': str(ROOT), 'log': str(log),
                'timeout_seconds': timeout, 'required_output': marker}
        report['steps'].append(step)
        try:
            process = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                     timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            data = process.stdout + process.stderr
            log.write_bytes(data)
            step.update(exit=process.returncode,
                        result='PASS' if process.returncode == 0 and
                        (marker is None or marker.encode() in data) else 'FAIL')
        except subprocess.TimeoutExpired as error:
            log.write_bytes((error.stdout or b'') + (error.stderr or b''))
            step.update(exit=None, result='FAIL', reason='Timeout; owned child terminated')
        print(f"[{step['result']}] {name}: exit={step['exit']}", flush=True)
        return step['result'] == 'PASS'

    passed = False
    try:
        msbuild, vc = toolchain()
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows Kits\Installed Roots') as key:
            sdk = Path(winreg.QueryValueEx(key, 'KitsRoot10')[0])
        sdk_version = '10.0.26100.0'
        if vc.name != '14.44.35207' or not (sdk / 'Lib' / sdk_version / 'um/x64/kernel32.lib').is_file():
            report['reason'] = 'Recorded Phase 26 toolset/SDK unavailable; no toolchain migration authorized'
            return 1
        report['toolchain'] = {'msbuild': str(msbuild), 'msvc': str(vc), 'sdk': str(sdk), 'sdk_version': sdk_version}
        if not args.no_build and not invoke('generate', [ROOT / 'vendor/bin/premake/premake5.exe', 'vs2022']):
            return 1
        if not args.no_build and not invoke('build', [msbuild, ROOT / 'GEngine.sln',
                      '/t:GEngineEditor;Breakout;RayTracing;RigidBodySimulation;PhysicsTests;PhysicsBenchmark',
                      '/m:1', '/nr:false', '/nologo', '/v:normal', '/p:Configuration=' + config,
                      '/p:Platform=x64', '/p:VCToolsVersion=' + vc.name, '/p:WindowsTargetPlatformVersion=' + sdk_version,
                      '/bl:' + str(out / 'build.binlog')], 1200):
            return 1
        includes = [vc / 'include', *(sdk / 'Include' / sdk_version / part for part in ('ucrt', 'shared', 'um')),
                    ROOT / 'GEngine/include/GEngine']
        libraries = [vc / 'lib/x64', sdk / 'Lib' / sdk_version / 'ucrt/x64', sdk / 'Lib' / sdk_version / 'um/x64']
        executable = out / 'mesh-asset-probe.exe'
        # No external include roots, PCH, backend libraries, or application DLL paths.
        # showIncludes records the real transitive closure of the normal API consumer.
        command = [vc / 'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W4', '/WX', '/showIncludes',
                   '/MTd' if config == 'Debug' else '/MT', '/Od' if config == 'Debug' else '/O2',
                   *['/I' + str(p) for p in includes], ROOT / 'tools/mesh_asset_probe.cpp',
                   '/Fo' + str(out / 'mesh-asset-probe.obj'), '/Fe' + str(executable), '/link',
                   '/SUBSYSTEM:CONSOLE', *['/LIBPATH:' + str(p) for p in libraries],
                   ROOT / 'bin' / config / 'GEngine/GEngine.lib']
        if not invoke('compile-consumer-probe', command):
            return 1
        dependencies = (out / 'compile-consumer-probe.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in dependencies for token in ('/glad/', '/sdl2/', '/opengl/', '/imgui/', '/assimp/', '/gepch.h')):
            report['reason'] = 'Backend dependency leaked into MeshAsset consumer'
            return 1
        production = [ROOT / 'GEngine/include/GEngine/Mesh/MeshAsset.h', ROOT / 'GEngine/src/Mesh/MeshAsset.cpp']
        for path in production:
            text = path.read_text(encoding='utf-8')
            # Remove comments for the syntax gate; manually review declarations too.
            text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
            if re.search(r'\b(throw|try|catch|enable_if)\b', text):
                report['reason'] = 'Exception/SFINAE syntax in new production file: ' + str(path)
                return 1
        report['architecture'] = {'consumer_boundary': 'PASS', 'no_new_exception': 'PASS',
                                  'record_constraints': 'standard-layout and trivially-copyable concept',
                                  'backend_roots_available_to_consumer': False}
        if not invoke('cpu', [executable], marker='[PASS] mesh-asset-CPU'):
            return 1
        report['inputs'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                           [*production, ROOT / 'tools/mesh_asset_probe.cpp', executable,
                            ROOT / 'bin' / config / 'GEngine/GEngine.lib']}
        passed = True
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report['reason'] = str(error)
        return 1
    finally:
        report.update(result='PASS' if passed else 'FAIL', exit=0 if passed else 1)
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f"[{report['result']}] results: {out / 'results.json'}", flush=True)


if __name__ == '__main__':
    sys.exit(main())
