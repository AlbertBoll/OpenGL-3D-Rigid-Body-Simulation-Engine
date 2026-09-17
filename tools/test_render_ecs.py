"""Validate Phase 36 render-facing ECS, serial mutation boundaries and picking identity."""
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
    parser.add_argument('--no-build', action='store_true', help='Reuse matching library/consumer builds')
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / 'logs/rendering/phase36/final' / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'configuration': config, 'steps': [], 'build_reused': args.no_build}
    env = {k: v for k, v in os.environ.items() if k.lower() != 'path'}
    env['Path'] = os.environ.get('PATH', os.environ.get('Path', ''))
    env.pop('SDL_VIDEODRIVER', None)

    def invoke(name, command, timeout=120, marker=None, expected=0, cwd=ROOT):
        command = [str(x) for x in command]
        log = out / (name + '.log')
        step = {'name': name, 'command': command, 'cwd': str(cwd), 'log': str(log),
                'timeout_seconds': timeout, 'required_output': marker, 'expected_exit': expected}
        report['steps'].append(step)
        try:
            process = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                                     timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            data = process.stdout + process.stderr
            log.write_bytes(data)
            step.update(exit=process.returncode, result='PASS' if process.returncode == expected and
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
            report['reason'] = 'Recorded toolchain unavailable; no migration authorized'
            return 1
        report['toolchain'] = {'msbuild': str(msbuild), 'msvc': str(vc), 'sdk': str(sdk), 'sdk_version': sdk_version}
        if not args.no_build:
            if not invoke('generate', [ROOT / 'vendor/bin/premake/premake5.exe', 'vs2022']):
                return 1
            if not invoke('build', [msbuild, ROOT / 'GEngine.sln',
                          '/t:GEngineEditor;Breakout;RayTracing;RigidBodySimulation;PhysicsTests;PhysicsBenchmark',
                          '/m:1', '/nr:false', '/nologo', '/v:normal', '/p:Configuration=' + config,
                          '/p:Platform=x64', '/p:VCToolsVersion=' + vc.name, '/p:WindowsTargetPlatformVersion=' + sdk_version,
                          '/bl:' + str(out / 'build.binlog')], 1200):
                return 1
        includes = [vc / 'include', *(sdk / 'Include' / sdk_version / part for part in ('ucrt', 'shared', 'um')),
                    ROOT / 'GEngine/include/GEngine', ROOT / 'GEngine/include/external', ROOT / 'external/entt/include']
        libraries = [vc / 'lib/x64', sdk / 'Lib' / sdk_version / 'ucrt/x64', sdk / 'Lib' / sdk_version / 'um/x64']
        common = [vc / 'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W4',
                  '/MTd' if config == 'Debug' else '/MT', '/Od' if config == 'Debug' else '/O2',
                  '/DGENGINE_CONFIG_' + config.upper(), *['/I' + str(p) for p in includes],
                  '/external:W0', '/external:I' + str(ROOT / 'GEngine/include/external'), '/external:I' + str(ROOT / 'external/entt/include')]
        cpu = out / 'render-ecs-cpu.exe'
        cpu_link = ['/link', '/SUBSYSTEM:CONSOLE', *['/LIBPATH:' + str(p) for p in libraries],
                    ROOT / 'bin' / config / 'GEngine/GEngine.lib']
        if not invoke('consumer-boundary', [*common, '/WX', '/showIncludes', ROOT / 'tools/render_ecs_probe.cpp',
                      '/Fo' + str(out / 'cpu.obj'), '/Fe' + str(cpu), *cpu_link]):
            return 1
        dependencies = (out / 'consumer-boundary.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in dependencies for token in ('/glad/', '/sdl2/', '/opengl/', '/imgui/', '/assimp/', '/gepch.h', 'backend.h')):
            report['reason'] = 'Backend dependency leaked into normal consumer'
            return 1
        scene_header = out / 'scene-header.cpp'
        scene_header.write_text('#include "Scene/_Entity.h"\n'
                                'GEngine::RenderEcs& Use(GEngine::_Scene& scene) { return scene.RenderData(); }\n')
        # Existing scene headers retain the maintained /W3 and aligned-storage
        # compatibility policy. New render-only headers above stay /W4 /WX.
        scene_common = [p if p != '/W4' else '/W3' for p in common]
        scene_common += ['/D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING']
        if not invoke('scene-header-boundary', [*scene_common, '/showIncludes', '/c',
                      '/I' + str(ROOT / 'external/spdlog/include'),
                      '/external:I' + str(ROOT / 'external/spdlog/include'), scene_header,
                      '/Fo' + str(out / 'scene-header.obj')]):
            return 1
        dependencies = (out / 'scene-header-boundary.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in dependencies for token in ('/glad/', '/sdl2/', '/opengl/', '/imgui/', '/assimp/', '/gepch.h', 'backend.h')):
            report['reason'] = 'Backend dependency leaked through scene integration headers'
            return 1
        production = ['GEngine/include/GEngine/Component/RenderComponents.h', 'GEngine/include/GEngine/Scene/RenderEcs.h']
        for relative in production:
            content = (ROOT / relative).read_text(encoding='utf-8')
            content = re.sub(r'//[^\n]*|/\*.*?\*/', '', content, flags=re.S)
            if re.search(r'\b(throw|try|catch|enable_if|GLuint|GLenum|SDL_Window|BindTextures)\b', content):
                report['reason'] = 'Exception/SFINAE/native syntax in migrated production file: ' + relative
                return 1
        report['architecture'] = {'consumer_boundary': 'PASS', 'no_new_exception': 'PASS'}
        env['SDL_VIDEODRIVER'] = 'phase36-cpu-must-not-initialize-video'
        if not invoke('authoring-cpu', [cpu], marker='[PASS] render-ecs'):
            return 1
        for mode in ('wrong-thread', 'legacy-mutation', 'duplicate-facade'):
            if not invoke(mode, [cpu, '--' + mode], expected=86, marker='[EXPECTED] render ECS invariant'):
                return 1
        extra_includes = [ROOT / 'external' / part / 'include' for part in ('sdl2', 'spdlog', 'glad', 'assimp', 'entt', 'tbb')]
        extra_libraries = [ROOT / 'external' / part / 'lib' for part in ('sdl2', 'tbb', 'assimp', 'fmod')]
        gl_common = [*scene_common, '/DSDL_MAIN_HANDLED', '/DGENGINE_PLATFORM_WINDOWS', '/DRENDER_ECS_SCENE_PROBE',
                     *['/I' + str(p) for p in extra_includes], '/external:templates-',
                     *['/external:I' + str(p) for p in (ROOT / 'external', vc / 'include')]]
        link = ['/link', '/SUBSYSTEM:CONSOLE', *['/LIBPATH:' + str(p) for p in libraries + extra_libraries],
                ROOT / 'bin' / config / 'GEngine/GEngine.lib', ROOT / 'external/glad/bin' / config / 'glad/glad.lib',
                'SDL2.lib', 'SDL2_ttf.lib', 'tbb12.lib', 'tbb12_debug.lib', 'tbb.lib', 'tbb_debug.lib',
                'assimp.lib', 'fmod64_vc.lib', 'fmodL64_vc.lib', 'fmodstudio64_vc.lib', 'fmodstudioL64_vc.lib']
        gpu = out / 'render-ecs-gl.exe'
        if not invoke('compile-resource-probe', [*gl_common, ROOT / 'tools/render_ecs_probe.cpp',
                      '/Fo' + str(out / 'gpu.obj'), '/Fe' + str(gpu), *link]):
            return 1
        env['Path'] = str(ROOT / 'bin' / config / 'GEngineEditor') + os.pathsep + env['Path']
        if not invoke('scene-integration', [gpu], marker='[PASS] render-ecs'):
            return 1
        for mode in ('scene-create', 'scene-destroy', 'entity-add'):
            if not invoke(mode, [gpu, '--' + mode], expected=86, marker='[EXPECTED] render ECS invariant'):
                return 1
        env.pop('SDL_VIDEODRIVER', None)
        env['GENGINE_ASSET_ROOT'] = str(ROOT / 'bin' / config / 'assets')
        if not invoke('picking-attachment', [gpu, '--picking'], marker='[PASS] render-ecs picking'):
            return 1
        if not invoke('entity-regression', [sys.executable, ROOT / 'tools/test_entity_api.py',
                      '--configuration', config, '--no-build', '--output', out / 'entity-regression'],
                      240, marker='[PASS] results:'):
            return 1
        report['inputs'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                           [*(ROOT / p for p in production), ROOT / 'tools/render_ecs_probe.cpp',
                            ROOT / 'tools/test_render_ecs.py', gpu, cpu, ROOT / 'bin' / config / 'GEngine/GEngine.lib']}
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
