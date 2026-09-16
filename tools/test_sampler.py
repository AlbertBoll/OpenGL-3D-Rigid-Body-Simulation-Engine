"""Build and validate reusable samplers, typed cache identity and material bindings."""
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
    parser.add_argument('--output', type=Path)
    parser.add_argument("--no-build", action="store_true", help="Use already-built matching candidate libraries/consumers; still compile and run the focused probes")
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / 'logs/rendering/phase27/final' / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'configuration': config, 'steps': [], 'build_reused': args.no_build}
    env = {k: v for k, v in os.environ.items() if k.lower() != 'path'}
    env['Path'] = os.environ.get('PATH', os.environ.get('Path', ''))
    env.pop('SDL_VIDEODRIVER', None)

    def invoke(name, command, timeout=60, expected=0, marker=None, cwd=ROOT):
        command = [str(value) for value in command]
        log = out / (name + '.log')
        step = {'name': name, 'command': command, 'cwd': str(cwd), 'log': str(log),
                'expected_exit': expected, 'required_output': marker, 'timeout_seconds': timeout}
        report['steps'].append(step)
        try:
            result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                                    timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            data = result.stdout + result.stderr
            log.write_bytes(data)
            exit_ok = result.returncode != 0 if expected == 'nonzero' else result.returncode == expected
            step.update(exit=result.returncode,
                        result='PASS' if exit_ok and (marker is None or marker.encode() in data) else 'FAIL')
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
        sdk_version = max((p.name for p in (sdk / 'Lib').iterdir() if (p / 'um/x64/kernel32.lib').is_file()),
                          key=lambda name: tuple(map(int, name.split('.'))))
        report['toolchain'] = {'msbuild': str(msbuild), 'msvc': str(vc), 'sdk': str(sdk), 'sdk_version': sdk_version}
        includes = [vc / 'include', *(sdk / 'Include' / sdk_version / part for part in ('ucrt', 'shared', 'um')),
                    ROOT / 'GEngine/include/GEngine']
        libraries = [vc / 'lib/x64', sdk / 'Lib' / sdk_version / 'ucrt/x64', sdk / 'Lib' / sdk_version / 'um/x64']
        common = [vc / 'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W4',
                  '/MTd' if config == 'Debug' else '/MT', '/Od' if config == 'Debug' else '/O2',
                  '/DGENGINE_CONFIG_' + config.upper(), *['/I' + str(p) for p in includes]]
        header_probe = out / 'texture-consumer.cpp'
        header_probe.write_text('#include "Assets/Samplers/Sampler.h"\n#include "Managers/AssetsManager.h"\n'
            '#if defined(GL_TEXTURE_2D) || defined(SDL_MAJOR_VERSION) || defined(GLAD_GL_H_)\n#error Native backend leaked\n#endif\n'
            'static_assert(!std::is_copy_constructible_v<GEngine::Asset::GpuSampler>);\n'
            'auto consume(GEngine::Asset::MaterialTextureBindings& material, const GEngine::Asset::SampledTextureBinding& binding) { return material.SetSampledTextureBinding("image", binding, 0); }\n'
            'static_assert(!std::is_assignable_v<GEngine::Asset::SamplerHandle&, GEngine::Asset::TextureHandle>);\n'
            'int main() { GEngine::Asset::SampledTextureBinding binding; return bool(binding.SamplerIdentity()); }\n')
        if not invoke('texture-consumer-boundary', [*common, '/showIncludes', '/c',
                      '/I' + str(ROOT / 'GEngine/include/external'), header_probe,
                      '/Fo' + str(out / 'texture-consumer.obj')], 120):
            return 1
        dependencies = (out / 'texture-consumer-boundary.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in dependencies for token in ('/glad/', '/sdl2/', '/opengl/', '/imgui/')):
            report['reason'] = 'Native dependency leaked into the normal texture consumer probe'
            return 1
        source = ROOT / 'tools/sampler_probe.cpp'
        if not args.no_build and not invoke('generate', [ROOT / 'vendor/bin/premake/premake5.exe', 'vs2022'], 120):
            return 1
        if not args.no_build and not invoke('build', [msbuild, ROOT / 'GEngine.sln',
                      '/t:GEngineEditor;Breakout;RayTracing;RigidBodySimulation;PhysicsTests;PhysicsBenchmark',
                      '/m:1', '/nr:false', '/nologo', '/v:normal', '/p:Configuration=' + config,
                      '/p:Platform=x64', '/p:VCToolsVersion=' + vc.name, '/p:WindowsTargetPlatformVersion=' + sdk_version,
                      '/bl:' + str(out / 'build.binlog')], 1200):
            return 1
        extra_includes = [ROOT / 'GEngine/include', ROOT / 'GEngine/include/external',
                          *(ROOT / 'external' / part / 'include' for part in ('sdl2', 'spdlog', 'glad', 'assimp', 'entt', 'tbb'))]
        extra_libraries = [ROOT / 'external' / part / 'lib' for part in ('sdl2', 'tbb', 'assimp', 'fmod')]
        gl = out / 'sampler-probe.exe'
        gl_command = [*common, '/DSDL_MAIN_HANDLED', '/DGENGINE_PLATFORM_WINDOWS',
                      *['/I' + str(p) for p in extra_includes], '/external:W0', '/external:templates-',
                      *['/external:I' + str(p) for p in (ROOT / 'external', ROOT / 'GEngine/include/external', vc / 'include')],
                      source, '/Fo' + str(out / 'gl.obj'), '/Fe' + str(gl), '/link', '/SUBSYSTEM:CONSOLE',
                      *(['/OPT:NOREF', '/OPT:NOICF'] if config == 'Debug' else []),
                      *['/LIBPATH:' + str(p) for p in libraries + extra_libraries],
                      ROOT / 'bin' / config / 'GEngine/GEngine.lib', ROOT / 'external/glad/bin' / config / 'glad/glad.lib',
                      'SDL2.lib', 'SDL2_ttf.lib', 'tbb12.lib', 'tbb12_debug.lib', 'tbb.lib', 'tbb_debug.lib',
                      'assimp.lib', 'fmod64_vc.lib', 'fmodL64_vc.lib', 'fmodstudio64_vc.lib', 'fmodstudioL64_vc.lib']
        if not invoke('compile-gl', gl_command, 120):
            return 1
        sdl = ROOT / 'bin' / config / 'GEngineEditor/SDL2.dll'
        report['inputs'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                            (gl, ROOT / 'bin' / config / 'GEngine/GEngine.lib', sdl)}
        env['Path'] = str(sdl.parent) + os.pathsep + env['Path']
        env['GENGINE_ASSET_ROOT'] = str(ROOT / 'bin' / config / 'assets')
        env['GENGINE_SHADOW_RESOLUTION'] = '32'
        for mode, marker, expected in (('--gl', '[PASS] samplers cycles=2', 0),
                                       ('--reject-worker', '[EXPECTED] sampler ownership invariant', 86),
                                       ('--reject-live-lease', '[EXPECTED] sampler ownership invariant', 86)):
            runtime = out / mode[2:]
            runtime.mkdir(exist_ok=True)
            if not invoke(mode[2:], ([gl] if mode == '--gl' else [gl, mode]), 180, expected=expected, marker=marker, cwd=runtime):
                return 1
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
