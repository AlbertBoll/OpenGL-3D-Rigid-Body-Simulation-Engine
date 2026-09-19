"""Validate Phase 50 worker texture decoding, semantic parity and real consumer publication."""
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
    parser.add_argument('--no-build', action='store_true', help='Reuse an already validated matching library')
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / 'logs/rendering/phase50/final' / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'configuration': config, 'steps': []}
    env = {k: v for k, v in os.environ.items() if k.lower() != 'path'}
    env['Path'] = os.environ.get('PATH', os.environ.get('Path', ''))
    env.pop('SDL_VIDEODRIVER', None)
    env['CL'] = env.get('CL', '') + ' /we4715 /we4716'

    def invoke(name, command, timeout=120, cwd=ROOT, expected=0, marker=None):
        step = {'name': name, 'command': [str(p) for p in command], 'cwd': str(cwd),
                'expected_exit': expected, 'marker': marker, 'log': str(out / (name + '.log'))}
        report['steps'].append(step)
        try:
            result = subprocess.run(step['command'], cwd=cwd, env=env, capture_output=True,
                                    timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            data = result.stdout + result.stderr
            Path(step['log']).write_bytes(data)
            step['exit'] = result.returncode
            okay = result.returncode != 0 if expected == 'nonzero' else result.returncode == expected
            step['result'] = 'PASS' if okay and (marker is None or marker in data.decode(errors='replace')) else 'FAIL'
        except (OSError, subprocess.TimeoutExpired) as error:
            step.update(result='FAIL', error=str(error))
        print(f"[{step['result']}] {name}: exit={step.get('exit')}", flush=True)
        return step['result'] == 'PASS'

    passed = False
    try:
        # Shared scheduler/public interface: maintained consumers, existing scheduler
        # regression checks and graphical shutdowns once per final configuration.
        if not args.no_build and not invoke('affected-consumers', [sys.executable,
                ROOT / 'tools/test_frame_submission.py', '--configuration', config,
                '--smoke', '--output', out / 'consumers'], timeout=1800):
            return 1
        msbuild, vc = toolchain()
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows Kits\Installed Roots') as key:
            sdk = Path(winreg.QueryValueEx(key, 'KitsRoot10')[0])
        sdk_version = '10.0.26100.0'
        report['toolchain'] = {'msbuild': str(msbuild), 'msvc': str(vc), 'sdk': str(sdk), 'sdk_version': sdk_version}
        if vc.name != '14.44.35207' or not (sdk / 'Lib' / sdk_version / 'um/x64/kernel32.lib').is_file():
            report['reason'] = 'Recorded compiler/SDK unavailable'
            return 1
        includes = [vc / 'include', *(sdk / 'Include' / sdk_version / p for p in ('ucrt', 'shared', 'um')),
                    ROOT / 'GEngine/include', ROOT / 'GEngine/include/GEngine', ROOT / 'GEngine/include/external',
                    *(ROOT / 'external' / p / 'include' for p in ('sdl2', 'spdlog', 'glad', 'assimp', 'entt', 'tbb', 'fmod'))]
        libraries = [vc / 'lib/x64', sdk / 'Lib' / sdk_version / 'ucrt/x64', sdk / 'Lib' / sdk_version / 'um/x64',
                     *(ROOT / 'external' / p / 'lib' for p in ('sdl2', 'tbb', 'assimp', 'fmod'))]
        flags = [vc / 'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W3',
                 '/MTd' if config == 'Debug' else '/MT', '/Od' if config == 'Debug' else '/O2']
        probe = ROOT / 'tools/async_texture_probe.cpp'
        native_free = [p for p in includes if not any(part in p.parts for part in ('sdl2', 'glad', 'assimp', 'tbb'))]
        boundary = [*flags, '/c', '/showIncludes', '/DTEXTURE_SCHEMA_ONLY', *['/I' + str(p) for p in native_free],
                    probe, '/Fo' + str(out / 'consumer.obj')]
        if not invoke('consumer-boundary', boundary):
            return 1
        dependencies = (out / 'consumer-boundary.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in dependencies for token in ('/glad/', '/sdl2/', '/opengl/', '/imgui/', '/assimp/', '/gepch.h', 'backend.h')):
            report['reason'] = 'Native declaration in normal consumer dependency closure'
            return 1
        changed = ['GEngine/include/GEngine/Assets/Textures/AsyncTexture.h', 'GEngine/src/Assets/AsyncTexture.cpp',
                   'GEngine/include/GEngine/Managers/AssetsManager.h', 'GEngine/src/Managers/AssetsManager.cpp',
                   'RigidBodySimulation/include/RigidBodySimulation.h', 'RigidBodySimulation/src/RigidBodySimulation.cpp']
        for path in changed:
            source = re.sub(r'//[^\n]*|/\*.*?\*/', '', (ROOT / path).read_text(), flags=re.S)
            if re.search(r'\b(throw|try|catch|exception_ptr|enable_if)\b', source):
                report['reason'] = 'New exception/SFINAE boundary: ' + path
                return 1
        report['architecture'] = {'native_free_consumer': 'PASS', 'no_new_exceptions': 'PASS'}
        executable = out / 'async-texture-probe.exe'
        if not invoke('compile-probe', [*flags, '/DSDL_MAIN_HANDLED', '/DGENGINE_PLATFORM_WINDOWS',
                '/DGENGINE_CONFIG_' + config.upper(), *['/I' + str(p) for p in includes],
                probe, '/Fo' + str(out) + os.sep, '/Fe' + str(executable), '/link', '/SUBSYSTEM:CONSOLE',
                *['/LIBPATH:' + str(p) for p in libraries], ROOT / 'bin' / config / 'GEngine/GEngine.lib',
                ROOT / 'external/glad/bin' / config / 'glad/glad.lib', 'SDL2.lib', 'SDL2_ttf.lib',
                'tbb12.lib', 'tbb12_debug.lib', 'tbb.lib', 'tbb_debug.lib', 'assimp.lib',
                'fmod64_vc.lib', 'fmodL64_vc.lib', 'fmodstudio64_vc.lib', 'fmodstudioL64_vc.lib']):
            return 1
        sdl = ROOT / 'bin' / config / 'GEngineEditor/SDL2.dll'
        report['inputs'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                           (probe, executable, ROOT / 'bin' / config / 'GEngine/GEngine.lib', sdl)}
        env['Path'] = str(sdl.parent) + os.pathsep + env['Path']
        env['GENGINE_ASSET_ROOT'] = str(ROOT / 'bin' / config / 'assets')
        env['GENGINE_SHADOW_RESOLUTION'] = '256'
        if not invoke('async-texture', [executable], cwd=out, marker='[PASS] async-texture-retirement'):
            return 1
        if not args.no_build:
            smoke = out / 'consumers/smoke/RigidBodySimulation/application.log'
            if 'Async wood texture published: 2048x2048' not in smoke.read_text(errors='replace'):
                report['reason'] = 'Selected application did not publish the asynchronous material texture'
                return 1
            report['selected_consumer_publication'] = 'PASS'
        passed = True
        return 0
    finally:
        report['result'] = 'PASS' if passed else 'FAIL'
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
