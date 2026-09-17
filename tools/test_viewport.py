"""Phase 30: neutral platform headers, maintained consumers, real viewport/target checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import winreg

from rendering_validation import ROOT, toolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--rebuild-library', action='store_true', help='Rebuild the affected library before consumer builds')
    parser.add_argument('--headers-only', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--smoke', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if k.lower() != 'path'}
    env['Path'] = os.environ.get('PATH', os.environ.get('Path', ''))
    env.pop('SDL_VIDEODRIVER', None)
    report = {'configuration': args.configuration, 'steps': [], 'build_reused': args.no_build}
    passed = False

    def invoke(name, command, timeout=120, expected=0, marker=None, cwd=ROOT):
        command = [str(x) for x in command]
        step = dict(name=name, command=command, cwd=str(cwd), expected_exit=expected,
                    required_output=marker, log=str(out / (name + '.log')))
        report['steps'].append(step)
        try:
            result = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=timeout,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            data = result.stdout
            step['exit'] = result.returncode
            step['result'] = 'PASS' if result.returncode == expected and (not marker or marker.encode() in data) else 'FAIL'
        except subprocess.TimeoutExpired as error:
            data = error.stdout or b''
            step.update(exit=None, result='FAIL', reason='Owned child timed out')
        Path(step['log']).write_bytes(data)
        print(f"[{step['result']}] {name}: exit={step['exit']}", flush=True)
        return step['result'] == 'PASS'

    try:
        msbuild, vc = toolchain()
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows Kits\Installed Roots') as key:
            sdk = Path(winreg.QueryValueEx(key, 'KitsRoot10')[0])
        sdk_version = '10.0.26100.0'
        if vc.name != '14.44.35207':
            raise RuntimeError('Recorded toolset changed; no implicit toolchain migration')
        report['toolchain'] = dict(msbuild=str(msbuild), msvc=str(vc), sdk_version=sdk_version)
        includes = [vc / 'include', *(sdk / 'Include' / sdk_version / x for x in ('ucrt', 'shared', 'um')),
                    ROOT / 'GEngine/include/GEngine', ROOT / 'GEngine/include/external']
        common = [vc / 'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W4',
                  '/DNOMINMAX', '/MTd' if args.configuration == 'Debug' else '/MT',
                  '/Od' if args.configuration == 'Debug' else '/O2',
                  '/DGENGINE_CONFIG_' + args.configuration.upper(), *['/I' + str(x) for x in includes]]
        headers = ['Core/Platform.h', 'Core/Window.h', 'Managers/WindowManager.h', 'Managers/InputManager.h',
                   'Managers/EventManager.h', 'Events/Event.h', 'Events/ApplicationEvent.h', 'Events/MouseEvent.h',
                   'Events/KeyBoardEvent.h', 'Inputs/KeyCodes.h', 'Inputs/MouseCodes.h', 'Inputs/ControllerCodes.h',
                   'Core/GEngine.h', 'Core/BaseApp.h', 'Core/RenderTarget.h', 'Core/RuntimeAssets.h']
        for index, header in enumerate(headers):
            source = out / f'consumer-{index}.cpp'
            source.write_text('#include "' + header + '"\n'
                              '#if defined(SDL_MAJOR_VERSION) || defined(GLAD_GL_H_) || defined(IMGUI_VERSION)\n'
                              '#error Backend declaration leaked\n#endif\n', encoding='utf-8')
            name = f'consumer-{index}'
            if not invoke(name, [*common, '/showIncludes', '/c', source, '/Fo' + str(source.with_suffix('.obj'))]):
                return 1
            trace = (out / (name + '.log')).read_text(errors='replace').lower().replace('\\', '/')
            if any(token in trace for token in ('/glad/', '/sdl2/', '/imgui/', '/assimp/')):
                raise RuntimeError('Backend dependency in ' + header)
        report['neutral_headers'] = headers
        if args.headers_only:
            passed = True
            return 0
        if not args.no_build:
            if not invoke('generate', [ROOT / 'vendor/bin/premake/premake5.exe', 'vs2022']):
                return 1
            if args.rebuild_library and not invoke('rebuild-library', [msbuild, ROOT / 'GEngine/GEngine.vcxproj',
                          '/t:Rebuild', '/m:1', '/nr:false', '/nologo', '/v:normal', '/p:Configuration=' + args.configuration,
                          '/p:Platform=x64', '/p:VCToolsVersion=' + vc.name, '/p:WindowsTargetPlatformVersion=' + sdk_version,
                          '/bl:' + str(out / 'rebuild-library.binlog')], timeout=1200):
                return 1
            if not invoke('build', [msbuild, ROOT / 'GEngine.sln',
                          '/t:GEngineEditor;Breakout;RayTracing;RigidBodySimulation;PhysicsTests;PhysicsBenchmark',
                          '/m:1', '/nr:false', '/nologo', '/v:normal', '/p:Configuration=' + args.configuration,
                          '/p:Platform=x64', '/p:VCToolsVersion=' + vc.name, '/p:WindowsTargetPlatformVersion=' + sdk_version,
                          '/bl:' + str(out / 'build.binlog')], timeout=1200):
                return 1
        if args.build_only:
            passed = True
            return 0
        extra = [ROOT / 'GEngine/include', ROOT / 'GEngine/include/GEngine/Core',
                 *(ROOT / 'external' / x / 'include' for x in ('sdl2', 'spdlog', 'glad', 'assimp', 'entt', 'tbb'))]
        libraries = [vc / 'lib/x64', sdk / 'Lib' / sdk_version / 'ucrt/x64', sdk / 'Lib' / sdk_version / 'um/x64',
                     *(ROOT / 'external' / x / 'lib' for x in ('sdl2', 'tbb', 'assimp', 'fmod'))]
        exe = out / 'viewport-probe.exe'
        command = [*common, '/DSDL_MAIN_HANDLED', '/DGENGINE_PLATFORM_WINDOWS',
                   *['/I' + str(x) for x in extra], '/external:W0', '/external:templates-',
                   *['/external:I' + str(x) for x in (ROOT / 'external', ROOT / 'GEngine/include/external', vc / 'include')],
                   ROOT / 'tools/viewport_probe.cpp', '/Fo' + str(out / 'viewport-probe.obj'), '/Fe' + str(exe),
                   '/link', '/SUBSYSTEM:CONSOLE', *(['/OPT:NOREF', '/OPT:NOICF'] if args.configuration == 'Debug' else []), *['/LIBPATH:' + str(x) for x in libraries],
                   ROOT / 'bin' / args.configuration / 'GEngine/GEngine.lib',
                   ROOT / 'external/glad/bin' / args.configuration / 'glad/glad.lib',
                   'SDL2.lib', 'SDL2_ttf.lib', 'tbb12.lib', 'tbb12_debug.lib', 'tbb.lib', 'tbb_debug.lib',
                   'assimp.lib', 'fmod64_vc.lib', 'fmodL64_vc.lib', 'fmodstudio64_vc.lib', 'fmodstudioL64_vc.lib']
        if not invoke('compile-viewport', command):
            return 1
        env['Path'] = str(ROOT / 'bin' / args.configuration / 'GEngineEditor') + os.pathsep + env['Path']
        env['GENGINE_ASSET_ROOT'] = str(ROOT / 'bin' / args.configuration / 'assets')
        env['GENGINE_SHADOW_RESOLUTION'] = '16'
        report['binaries'] = {str(x): hashlib.sha256(x.read_bytes()).hexdigest() for x in
                              (exe, ROOT / 'bin' / args.configuration / 'GEngine/GEngine.lib')}
        for mode, expected in [('viewport', 0), ('startup-errors', 0), ('asset-root-error', 0), ('reject-worker', 86), ('reject-root-worker', 86)]:
            cwd = out / mode
            cwd.mkdir(exist_ok=True)
            if not invoke(mode, [exe, '--' + mode], expected=expected, marker='[PASS] ' + ('reject-worker' if mode == 'reject-root-worker' else mode), cwd=cwd):
                return 1
        if args.smoke:
            import test_framebuffer
            test_framebuffer.OUT = out / 'smoke'
            test_framebuffer.OUT.mkdir(exist_ok=True)
            outcomes = [test_framebuffer.framebuffer_smoke(args.configuration, app + ':default')
                        for app in ('GEngineEditor', 'RigidBodySimulation', 'Breakout', 'RayTracing')]
            if not all(outcomes):
                return 1
        passed = True
        return 0
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        report['reason'] = str(error)
        print('[FAIL]', error, flush=True)
        return 1
    finally:
        report.update(result='PASS' if passed else 'FAIL', exit=0 if passed else 1)
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    sys.exit(main())
