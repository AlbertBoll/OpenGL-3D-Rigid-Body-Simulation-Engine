"""Validate Phase 32 GPU mesh upload, publication, updates and legacy owner compatibility."""
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
    out = (args.output or ROOT / 'logs/rendering/phase32/final' / config).resolve()
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
                    ROOT / 'GEngine/include/GEngine']
        libraries = [vc / 'lib/x64', sdk / 'Lib' / sdk_version / 'ucrt/x64', sdk / 'Lib' / sdk_version / 'um/x64']
        common = [vc / 'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W4',
                  '/MTd' if config == 'Debug' else '/MT', '/Od' if config == 'Debug' else '/O2',
                  '/DGENGINE_CONFIG_' + config.upper(), *['/I' + str(p) for p in includes]]
        consumer = out / 'consumer.cpp'
        consumer.write_text('#include "Mesh/GpuMesh.h"\n'
            '#if defined(GLAD_GL_H_) || defined(GL_VERSION_1_0) || defined(SDL_MAJOR_VERSION)\n#error Backend leak\n#endif\n'
            'static_assert(!std::is_copy_constructible_v<GEngine::GpuMesh>);\n'
            'static_assert(std::is_nothrow_move_constructible_v<GEngine::GpuMesh>);\n'
            'auto create(const GEngine::MeshAsset& a){return GEngine::GpuMesh::Create(a);}\n'
            'auto publish(GEngine::MeshRegistry& r, const GEngine::Asset::AssetPublication::Publication& p, const GEngine::MeshAsset& a){return GEngine::PublishMesh(r,p,a);}\n'
            'auto update(GEngine::MeshRegistry& r, const GEngine::Asset::AssetPublication::Publication& p, GEngine::Asset::MeshHandle h, const GEngine::VertexRecordUpdate& u){return GEngine::UpdateMesh(r,p,h,u);}\n'
            'auto draw(const GEngine::MeshView& v){return v->DrawSubmesh(0);}\n')
        if not invoke('consumer-boundary', [*common, '/WX', '/showIncludes', '/c', consumer, '/Fo' + str(out / 'consumer.obj')]):
            return 1
        dependencies = (out / 'consumer-boundary.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in dependencies for token in ('/glad/', '/sdl2/', '/opengl/', '/imgui/', '/assimp/', '/gepch.h')):
            report['reason'] = 'Backend dependency leaked into normal consumer'
            return 1
        for relative in ('GEngine/include/GEngine/Mesh/GpuMesh.h', 'GEngine/src/Mesh/GpuMesh.cpp',
                         'GEngine/include/GEngine/Assets/AssetRegistry.h'):
            text = (ROOT / relative).read_text(encoding='utf-8')
            text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
            if re.search(r'\b(throw|try|catch|enable_if)\b', text):
                report['reason'] = 'Exception/SFINAE syntax in new or migrated production file: ' + relative
                return 1
        report['architecture'] = {'consumer_boundary': 'PASS', 'no_new_exception': 'PASS',
                                  'registry_update_constraint': 'invocable returning expected<void, Error>'}
        extra_includes = [ROOT / 'GEngine/include/external', *(ROOT / 'external' / part / 'include'
                          for part in ('sdl2', 'spdlog', 'glad', 'assimp', 'entt', 'tbb'))]
        extra_libraries = [ROOT / 'external' / part / 'lib' for part in ('sdl2', 'tbb', 'assimp', 'fmod')]
        gl_common = [*common, '/DSDL_MAIN_HANDLED', '/DGENGINE_PLATFORM_WINDOWS', *['/I' + str(p) for p in extra_includes],
                     '/external:W0', '/external:templates-', *['/external:I' + str(p) for p in
                     (ROOT / 'external', ROOT / 'GEngine/include/external', vc / 'include')]]
        link = ['/link', '/SUBSYSTEM:CONSOLE', *['/LIBPATH:' + str(p) for p in libraries + extra_libraries],
                ROOT / 'bin' / config / 'GEngine/GEngine.lib', ROOT / 'external/glad/bin' / config / 'glad/glad.lib',
                'SDL2.lib', 'SDL2_ttf.lib', 'tbb12.lib', 'tbb12_debug.lib', 'tbb.lib', 'tbb_debug.lib',
                'assimp.lib', 'fmod64_vc.lib', 'fmodL64_vc.lib', 'fmodstudio64_vc.lib', 'fmodstudioL64_vc.lib']
        gpu = out / 'gpu-mesh-probe.exe'
        if not invoke('compile-gpu', [*gl_common, ROOT / 'tools/gpu_mesh_probe.cpp', '/Fo' + str(out / 'gpu.obj'), '/Fe' + str(gpu), *link]):
            return 1
        env['Path'] = str(ROOT / 'bin' / config / 'GEngineEditor') + os.pathsep + env['Path']
        if not invoke('gpu', [gpu], marker='[PASS] gpu-mesh cycles=2'):
            return 1
        for mode in ('--reject-worker', '--reject-worker-create', '--reject-context'):
            if not invoke(mode[2:], [gpu, mode], expected=86, marker='[EXPECTED] gpu-mesh ownership invariant'):
                return 1
        legacy = out / 'legacy-owners.exe'
        if not invoke('compile-legacy', [*gl_common, ROOT / 'tools/vertex_buffer_probe.cpp', ROOT / 'tools/geometry_resource_probe.cpp',
                      '/Fo' + str(out) + os.sep, '/Fe' + str(legacy), *link]):
            return 1
        if not invoke('legacy-owners', [legacy], marker='[PASS] vertex-resource-RAII cycles=2'):
            return 1
        cpu = out / 'asset-registry-cpu.exe'
        if not invoke('compile-registry', [*common, ROOT / 'tools/asset_registry_probe.cpp', '/Fo' + str(out / 'registry.obj'),
                      '/Fe' + str(cpu), '/link', '/SUBSYSTEM:CONSOLE', *['/LIBPATH:' + str(p) for p in libraries]]):
            return 1
        if not invoke('registry-cpu', [cpu], marker='[PASS] asset-registry-CPU'):
            return 1
        report['inputs'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                           (gpu, legacy, cpu, ROOT / 'bin' / config / 'GEngine/GEngine.lib')}
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
