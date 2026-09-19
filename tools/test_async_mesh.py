"""Validate Phase 51 worker mesh import, bounded engine preparation and scheduler publication."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import winreg

from rendering_validation import ROOT, toolchain


def standalone_smoke(config, directory):
    """Validate the built output without fixing it or borrowing probe DLL paths."""
    from postbuild import ASSIMP_RUNTIME_SOURCE, ASSIMP_RUNTIME_SHA256, ASSIMP_IMPORT_LIBRARY_SHA256
    from test_frame_submission import application_smoke
    executable = ROOT / 'bin' / config / 'RigidBodySimulation/RigidBodySimulation.exe'
    source = ROOT / ASSIMP_RUNTIME_SOURCE
    target = executable.parent / source.name
    library = ROOT / 'external/assimp/lib/assimp.lib'
    directory.mkdir(parents=True, exist_ok=True)
    provenance = {'source': str(source), 'target': str(target), 'sha256': ASSIMP_RUNTIME_SHA256,
                  'import_library': str(library), 'import_library_sha256': ASSIMP_IMPORT_LIBRARY_SHA256}
    for path, expected in ((source, ASSIMP_RUNTIME_SHA256), (target, ASSIMP_RUNTIME_SHA256),
                           (library, ASSIMP_IMPORT_LIBRARY_SHA256)):
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            record = {'result': 'FAIL', 'reason': 'Missing or mismatched bundled Assimp: ' + str(path),
                      'assimp_provenance': provenance}
            (directory / 'smoke.json').write_text(json.dumps(record, indent=2) + '\n')
            return record
    # Start from a small OS environment; inherited VS/test-runner settings and
    # repository DLL directories must not satisfy standalone runtime closure.
    allowed = {'SYSTEMROOT', 'WINDIR', 'SYSTEMDRIVE', 'TEMP', 'TMP', 'USERPROFILE', 'APPDATA',
               'LOCALAPPDATA', 'PROGRAMDATA', 'ALLUSERSPROFILE', 'PROGRAMFILES', 'PROGRAMFILES(X86)',
               'PROGRAMW6432', 'COMMONPROGRAMFILES', 'COMMONPROGRAMFILES(X86)', 'COMMONPROGRAMW6432',
               'COMPUTERNAME', 'COMSPEC', 'HOMEDRIVE', 'HOMEPATH', 'LOGONSERVER', 'NUMBER_OF_PROCESSORS',
               'OS', 'PATHEXT', 'PROCESSOR_ARCHITECTURE', 'PROCESSOR_IDENTIFIER', 'PROCESSOR_LEVEL',
               'PROCESSOR_REVISION', 'PUBLIC', 'SESSIONNAME', 'USERDOMAIN', 'USERDOMAIN_ROAMINGPROFILE',
               'USERNAME', 'DRIVERDATA', 'LANG', 'LC_ALL', 'LC_CTYPE'}
    env = {k: v for k, v in os.environ.items() if k.upper() in allowed}
    system = Path(os.environ['SystemRoot'])
    env.update(Path=str(system / 'System32') + os.pathsep + str(system),
               GENGINE_ASYNC_MESH_SMOKE='1', GENGINE_SHADOW_RESOLUTION='256')
    # The application resolves its normal adjacent asset package itself.
    previous_mode = ctypes.windll.kernel32.SetErrorMode(0x8003)
    try:
        record = application_smoke(executable, directory, env, required_modules={
            source.name: {'path': str(target), 'sha256': ASSIMP_RUNTIME_SHA256}})
    finally:
        ctypes.windll.kernel32.SetErrorMode(previous_mode)
    record['assimp_provenance'] = provenance
    if record['result'] == 'PASS':
        published = 'Async barrel mesh published: 1 submeshes' in Path(record['log']).read_text(errors='replace')
        record['barrel_publication'] = 'PASS' if published else 'FAIL'
        if not published:
            record.update(result='FAIL', reason='Selected application mesh import did not publish')
    (directory / 'smoke.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--no-build', action='store_true', help='Reuse matching built libraries/application; still run standalone smoke')
    parser.add_argument('--smoke-only', action='store_true', help='Only validate standalone built output, DLL provenance and barrel publication')
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / 'logs/rendering/phase51/final' / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'configuration': config, 'steps': []}
    env = {k: v for k, v in os.environ.items() if k.lower() != 'path'}
    env['Path'] = os.environ.get('PATH', os.environ.get('Path', ''))
    env.pop('SDL_VIDEODRIVER', None)
    env.pop('GENGINE_ASYNC_MESH_SMOKE', None)
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
        if args.smoke_only:
            smoke = standalone_smoke(config, out / 'smoke/RigidBodySimulation')
            report['application_smoke'] = smoke
            passed = smoke['result'] == 'PASS'
            report['selected_consumer_publication'] = 'PASS' if passed else 'FAIL'
            return 0 if passed else 1
        msbuild, vc = toolchain()
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows Kits\Installed Roots') as key:
            sdk = Path(winreg.QueryValueEx(key, 'KitsRoot10')[0])
        sdk_version = '10.0.26100.0'
        report['toolchain'] = {'msbuild': str(msbuild), 'msvc': str(vc), 'sdk': str(sdk), 'sdk_version': sdk_version}
        if vc.name != '14.44.35207' or not (sdk / 'Lib' / sdk_version / 'um/x64/kernel32.lib').is_file():
            report['reason'] = 'Recorded compiler/SDK unavailable'
            return 1
        if not args.no_build:
            if not invoke('generate', [ROOT / 'vendor/bin/premake/premake5.exe', 'vs2022']):
                return 1
            if not invoke('build', [msbuild, ROOT / 'GEngine.sln',
                    '/t:GEngineEditor;Breakout;RayTracing;RigidBodySimulation;PhysicsTests;PhysicsBenchmark',
                    '/m:1', '/nr:false', '/nologo', '/v:normal', '/p:Configuration=' + config,
                    '/p:Platform=x64', '/p:VCToolsVersion=' + vc.name,
                    '/p:WindowsTargetPlatformVersion=' + sdk_version, '/bl:' + str(out / 'build.binlog')], timeout=1800):
                return 1
        includes = [vc / 'include', *(sdk / 'Include' / sdk_version / p for p in ('ucrt', 'shared', 'um')),
                    ROOT / 'GEngine/include', ROOT / 'GEngine/include/GEngine', ROOT / 'GEngine/include/external',
                    *(ROOT / 'external' / p / 'include' for p in ('sdl2', 'spdlog', 'glad', 'assimp', 'entt', 'tbb', 'fmod'))]
        libraries = [vc / 'lib/x64', sdk / 'Lib' / sdk_version / 'ucrt/x64', sdk / 'Lib' / sdk_version / 'um/x64',
                     *(ROOT / 'external' / p / 'lib' for p in ('sdl2', 'tbb', 'assimp', 'fmod'))]
        flags = [vc / 'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W3',
                 '/MTd' if config == 'Debug' else '/MT', '/Od' if config == 'Debug' else '/O2']
        probe = ROOT / 'tools/async_mesh_probe.cpp'
        native_free = [p for p in includes if not any(part in p.parts for part in ('sdl2', 'glad', 'assimp', 'tbb'))]
        boundary = [*flags, '/c', '/showIncludes', '/DMESH_SCHEMA_ONLY', *['/I' + str(p) for p in native_free],
                    probe, '/Fo' + str(out / 'consumer.obj')]
        if not invoke('consumer-boundary', boundary):
            return 1
        dependencies = (out / 'consumer-boundary.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in dependencies for token in ('/glad/', '/sdl2/', '/opengl/', '/imgui/', '/assimp/', '/gepch.h', 'backend.h')):
            report['reason'] = 'Native declaration in normal consumer dependency closure'
            return 1
        app_includes = [p for p in includes if not any(part in p.parts for part in ('sdl2', 'glad', 'assimp'))]
        if not invoke('application-boundary', [*flags, '/c', '/showIncludes', '/DGENGINE_PLATFORM_WINDOWS',
                '/DGENGINE_CONFIG_' + config.upper(), *['/I' + str(p) for p in app_includes],
                '/I' + str(ROOT / 'RigidBodySimulation/include'), ROOT / 'RigidBodySimulation/src/RigidBodySimulation.cpp',
                '/Fo' + str(out / 'application-consumer.obj')]):
            return 1
        app_deps = (out / 'application-boundary.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in app_deps for token in ('/glad/', '/sdl2/', '/opengl/', '/assimp/', '/gepch.h', 'backend.h', 'imgui_impl_')):
            report['reason'] = 'Backend declaration in selected application dependency closure'
            return 1
        changed = ['GEngine/include/GEngine/Mesh/AsyncMesh.h', 'GEngine/src/Mesh/AsyncMesh.cpp',
                   'GEngine/include/GEngine/Mesh/MeshImporter.h', 'GEngine/src/Mesh/MeshImporter.cpp',
                   'GEngine/src/Mesh/MeshImportNormalization.cpp',
                   'RigidBodySimulation/include/RigidBodySimulation.h', 'RigidBodySimulation/src/RigidBodySimulation.cpp']
        for path in changed:
            source = re.sub(r'//[^\n]*|/\*.*?\*/', '', (ROOT / path).read_text(), flags=re.S)
            if re.search(r'\b(throw|try|catch|exception_ptr|enable_if)\b', source):
                report['reason'] = 'New exception/SFINAE boundary: ' + path
                return 1
        report['architecture'] = {'native_free_consumer': 'PASS', 'no_new_exceptions': 'PASS'}
        executable = out / 'async-mesh-probe.exe'
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
        from test_mesh_import import make_fixtures
        fixtures = out / 'fixtures'
        fixtures.mkdir(exist_ok=True)
        make_fixtures(fixtures)
        single = fixtures / 'single.obj'
        single.write_text('o triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n')
        (fixtures / 'failure.obj').write_bytes(single.read_bytes())
        (fixtures / 'corrupt.obj').write_text('not a valid mesh file\n')
        alias = fixtures / 'alias.obj'
        if not alias.exists():
            os.link(single, alias)
        if not invoke('async-mesh', [executable, fixtures], cwd=out, marker='[PASS] async-mesh-retirement'):
            return 1
        if not invoke('import-regression', [sys.executable, ROOT / 'tools/test_mesh_import.py',
                '--configuration', config, '--no-build', '--output', out / 'import-regression'], timeout=180):
            return 1
        # Canonical owner source has pre-existing scene-selection changes. Verify
        # the maintained consumer's actual startup/teardown with those exact bytes.
        smoke = standalone_smoke(config, out / 'smoke/RigidBodySimulation')
        report['application_smoke'] = smoke
        if smoke['result'] != 'PASS':
            return 1
        report['selected_consumer_publication'] = 'PASS'
        passed = True
        return 0
    finally:
        report['result'] = 'PASS' if passed else 'FAIL'
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
