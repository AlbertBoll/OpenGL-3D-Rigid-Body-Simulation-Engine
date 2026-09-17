"""Phase 34 declaration behavior and actual native-free consumer include closure."""
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
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / 'logs/rendering/phase34/final' / config / 'declarations').resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'configuration': config, 'steps': []}
    env = {k: v for k, v in os.environ.items() if k.lower() != 'path'}
    env['Path'] = os.environ.get('PATH', os.environ.get('Path', ''))
    env['SDL_VIDEODRIVER'] = 'phase34-cpu-must-not-initialize-video'

    def invoke(name, command, expected=0):
        command = list(map(str, command))
        log = out / (name + '.log')
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                timeout=180, creationflags=subprocess.CREATE_NO_WINDOW)
        log.write_bytes(result.stdout + result.stderr)
        ok = result.returncode == expected
        report['steps'].append({'name': name, 'command': command, 'exit': result.returncode,
                                'expected_exit': expected, 'result': 'PASS' if ok else 'FAIL', 'evidence': str(log)})
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: exit={result.returncode}", flush=True)
        return ok

    passed = False
    try:
        msbuild, vc = toolchain()
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows Kits\Installed Roots') as key:
            sdk = Path(winreg.QueryValueEx(key, 'KitsRoot10')[0])
        sdk_version = '10.0.26100.0'
        if vc.name != '14.44.35207':
            report['reason'] = 'Recorded compiler unavailable; no toolchain migration authorized'
            return 1
        report['toolchain'] = {'msvc': str(vc), 'sdk': str(sdk), 'sdk_version': sdk_version}
        includes = [vc / 'include', *(sdk / 'Include' / sdk_version / part for part in ('ucrt', 'shared', 'um')),
                    ROOT / 'GEngine/include/GEngine', ROOT / 'GEngine/include/external']
        libraries = [vc / 'lib/x64', sdk / 'Lib' / sdk_version / 'ucrt/x64', sdk / 'Lib' / sdk_version / 'um/x64']
        executable = out / 'material-pipeline-probe.exe'
        command = [vc / 'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W4', '/showIncludes',
                   '/MTd' if config == 'Debug' else '/MT', '/Od' if config == 'Debug' else '/O2',
                   *['/I' + str(p) for p in includes], '/external:W0', '/external:I' + str(ROOT / 'GEngine/include/external'),
                   ROOT / 'tools/material_pipeline_probe.cpp', '/Fo' + str(out / 'probe.obj'), '/Fe' + str(executable),
                   '/link', '/SUBSYSTEM:CONSOLE', *['/LIBPATH:' + str(p) for p in libraries],
                   ROOT / 'bin' / config / 'GEngine/GEngine.lib']
        if not invoke('compile-neutral-consumer', command):
            return 1
        dependencies = (out / 'compile-neutral-consumer.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in dependencies for token in ('/glad/', '/sdl2/', '/imgui/', '/assimp/', '/gepch.h', 'shaderbackend.h')):
            report['reason'] = 'Native dependency in actual compiler include closure'
            return 1
        production = ['GEngine/include/GEngine/Assets/Shaders/Shader.h', 'GEngine/src/Assets/Shader.cpp',
                      'GEngine/src/Assets/ShaderBackend.h', 'GEngine/src/Assets/ShaderCompilation.cpp', 'GEngine/include/GEngine/Managers/ShaderManager.h',
                      'GEngine/src/Managers/ShaderManager.cpp', 'GEngine/include/GEngine/Material/Pipeline.h',
                      'GEngine/include/GEngine/Material/MaterialTemplate.h', 'GEngine/src/Material/MaterialTemplate.cpp']
        for rel in production:
            text = (ROOT / rel).read_text(encoding='utf-8')
            text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
            if re.search(r'\b(throw|try|catch|enable_if|ShaderCreationException|ShaderException)\b', text):
                report['reason'] = 'Forbidden shader/declaration error or constraint syntax: ' + rel
                return 1
        report['architecture'] = {'native_free_include_closure': 'PASS', 'shader_and_declaration_error_model': 'PASS'}
        if not invoke('declaration-behavior', [executable]):
            return 1
        if '[PASS] material-pipeline declarations' not in (out / 'declaration-behavior.log').read_text():
            report['reason'] = 'Missing behavioral completion marker'
            return 1
        report['inputs'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                           [*(ROOT / p for p in production), ROOT / 'tools/material_pipeline_probe.cpp',
                            ROOT / 'tools/test_material_pipeline.py', executable, ROOT / 'bin' / config / 'GEngine/GEngine.lib']}
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
