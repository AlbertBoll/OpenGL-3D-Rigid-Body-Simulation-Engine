"""Validate deterministic, CPU-only mesh import and its normal consumer boundary."""
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


def make_fixtures(out):
    material = out / 'materials.mtl'
    material.write_text('newmtl Red\nKd 1 0 0\nnewmtl Blue\nKd 0 0 1\n')
    obj = out / 'multi.obj'
    obj.write_text('mtllib materials.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n'
                   'v 2 0 0\nv 3 0 0\nv 2 1 0\n'
                   'o first\nusemtl Blue\nf 1 2 3\no second\nusemtl Red\nf 4 5 6\n')
    dae = out / 'instances.dae'
    dae.write_text('''<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
<asset><created>2026-01-01T00:00:00Z</created><modified>2026-01-01T00:00:00Z</modified><unit meter="1"/><up_axis>Y_UP</up_axis></asset>
<library_geometries><geometry id="triangle"><mesh>
<source id="positions"><float_array id="positions-data" count="9">0 0 0 1 0 0 0 1 0</float_array>
<technique_common><accessor source="#positions-data" count="3" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>
<vertices id="vertices"><input semantic="POSITION" source="#positions"/></vertices>
<triangles count="1"><input semantic="VERTEX" source="#vertices" offset="0"/><p>0 1 2</p></triangles>
</mesh></geometry></library_geometries>
<library_visual_scenes><visual_scene id="scene"><node id="parent"><translate>2 3 4</translate>
<node id="mirrored"><scale>-2 3 1</scale><instance_geometry url="#triangle"/></node>
<node id="translated"><translate>8 0 0</translate><instance_geometry url="#triangle"/></node>
</node></visual_scene></library_visual_scenes><scene><instance_visual_scene url="#scene"/></scene>
</COLLADA>''', encoding='utf-8')
    return obj, dae


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--no-build', action='store_true', help='Reuse matching consumer/library builds; compile and run the CPU import probe')
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / 'logs/rendering/phase33/final' / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'configuration': config, 'steps': [], 'build_reused': args.no_build}
    env = {k: v for k, v in os.environ.items() if k.lower() != 'path'}
    env['Path'] = str(ROOT / 'bin' / config / 'GEngineEditor') + os.pathsep + os.environ.get('PATH', os.environ.get('Path', ''))
    env['SDL_VIDEODRIVER'] = 'phase33-cpu-must-not-initialize-video'

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
        fixtures = make_fixtures(out)
        executable = out / 'mesh-import-probe.exe'
        # No external include roots or PCH; only the private adapter links Assimp.
        # showIncludes records the real transitive closure of the normal API consumer.
        command = [vc / 'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W4', '/WX', '/showIncludes',
                   '/MTd' if config == 'Debug' else '/MT', '/Od' if config == 'Debug' else '/O2',
                   *['/I' + str(p) for p in includes], ROOT / 'tools/mesh_import_probe.cpp',
                   '/Fo' + str(out / 'mesh-import-probe.obj'), '/Fe' + str(executable), '/link',
                   '/SUBSYSTEM:CONSOLE', *['/LIBPATH:' + str(p) for p in libraries],
                   ROOT / 'bin' / config / 'GEngine/GEngine.lib', ROOT / 'external/assimp/lib/assimp.lib']
        if not invoke('compile-consumer-probe', command):
            return 1
        dependencies = (out / 'compile-consumer-probe.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in dependencies for token in ('/glad/', '/sdl2/', '/opengl/', '/imgui/', '/assimp/', '/gepch.h')):
            report['reason'] = 'Backend dependency leaked into importer consumer'
            return 1
        production = [ROOT / 'GEngine/include/GEngine/Mesh/MeshImporter.h', ROOT / 'GEngine/src/Mesh/MeshImporter.cpp',
                      ROOT / 'GEngine/src/Mesh/MeshImportNormalization.cpp']
        for path in production:
            text = path.read_text(encoding='utf-8')
            # Remove comments for the syntax gate; manually review declarations too.
            text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
            if re.search(r'\b(throw|try|catch|enable_if)\b', text):
                report['reason'] = 'Exception/SFINAE syntax in new production file: ' + str(path)
                return 1
        report['architecture'] = {'consumer_boundary': 'PASS', 'no_new_exception': 'PASS',
                                  'assimp_boundary': 'private C import API; no public native declarations',
                                  'backend_roots_available_to_consumer': False}
        if not invoke('cpu', [executable, *fixtures], marker='[PASS] mesh-import-CPU'):
            return 1
        report['inputs'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                           [*production, ROOT / 'tools/mesh_import_probe.cpp', ROOT / 'tools/test_mesh_import.py', *fixtures, executable,
                            ROOT / 'bin' / config / 'GEngine/GEngine.lib', ROOT / 'external/assimp/lib/assimp.lib']}
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
