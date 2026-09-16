"""Build the maintained consumers and prove C++23 facilities with the selected VS2022 toolset."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import winreg
import xml.etree.ElementTree as ET

from rendering_validation import ROOT, toolchain


TARGETS = ('GEngine', 'GEngineEditor', 'Breakout', 'RayTracing', 'RigidBodySimulation',
           'PhysicsTests', 'PhysicsBenchmark', 'RenderingValidation')
NS = {'ms': 'http://schemas.microsoft.com/developer/msbuild/2003'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=('Debug', 'Release'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'configuration': args.configuration, 'steps': [], 'result': 'FAIL'}
    env = {k: v for k, v in os.environ.items() if k.lower() != 'path'}
    env['Path'] = os.environ.get('PATH', os.environ.get('Path', ''))

    def invoke(name, command, timeout=120, marker=None):
        step = {'name': name, 'command': [str(x) for x in command], 'cwd': str(ROOT),
                'log': str(out / (name + '.log')), 'expected_exit': 0, 'required_output': marker}
        report['steps'].append(step)
        try:
            completed = subprocess.run(step['command'], cwd=ROOT, env=env, capture_output=True,
                                       timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            data = completed.stdout + completed.stderr
            Path(step['log']).write_bytes(data)
            step.update(exit=completed.returncode,
                        result='PASS' if completed.returncode == 0 and
                        (marker is None or marker.encode() in data) else 'FAIL')
        except subprocess.TimeoutExpired as error:
            Path(step['log']).write_bytes((error.stdout or b'') + (error.stderr or b''))
            step.update(exit=None, result='FAIL', reason='Timed out; owned process terminated')
        print(f"[{step['result']}] {name}: exit={step['exit']}", flush=True)
        return step['result'] == 'PASS'

    try:
        msbuild, vc = toolchain()
        if vc.name != '14.44.35207':
            raise ValueError('Phase 26 must preserve the approved 14.44.35207 toolset')
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows Kits\Installed Roots') as key:
            sdk = Path(winreg.QueryValueEx(key, 'KitsRoot10')[0])
        sdk_version = '10.0.26100.0'
        if not (sdk / 'Lib' / sdk_version / 'um/x64/kernel32.lib').is_file():
            raise ValueError('The approved Windows SDK 10.0.26100.0 is unavailable')
        report['toolchain'] = {'msbuild': str(msbuild), 'msvc': str(vc), 'sdk': str(sdk),
                               'sdk_version': sdk_version, 'language': '/std:c++23preview',
                               'crt': '/MTd' if args.configuration == 'Debug' else '/MT'}
        toolchain_inputs = [vc / 'bin/Hostx64/x64/cl.exe', vc / 'bin/Hostx64/x64/c1xx.dll',
                            vc / 'include/expected', vc / 'include/print', vc / 'include/yvals_core.h',
                            ROOT / 'vendor/bin/premake/premake5.exe']
        report['toolchain_hashes'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                     for p in toolchain_inputs}
        if not invoke('generate', [ROOT / 'vendor/bin/premake/premake5.exe', 'vs2022']):
            return 1
        generated = []
        for target in TARGETS:
            project = ROOT / target / (target + '.vcxproj')
            tree = ET.parse(project)
            configs = tree.findall('ms:ItemDefinitionGroup', NS)
            checked = 0
            for group in configs:
                compile_settings = group.find('ms:ClCompile', NS)
                if compile_settings is None:
                    continue
                condition = group.attrib.get('Condition', '')
                standard = compile_settings.findtext('ms:LanguageStandard', namespaces=NS)
                crt = compile_settings.findtext('ms:RuntimeLibrary', namespaces=NS)
                expected_crt = 'MultiThreadedDebug' if 'Debug' in condition else 'MultiThreaded'
                if standard != 'stdcpp23' or crt != expected_crt:
                    raise ValueError(f'{target} {condition}: incorrect language/CRT: {standard}/{crt}')
                generated.append({'target': target, 'configuration': condition, 'language': standard, 'crt': crt})
                checked += 1
            toolsets = tree.findall('.//ms:PlatformToolset', NS)
            if checked != 2 or not toolsets or any(value.text != 'v143' for value in toolsets):
                raise ValueError(f'{target}: unexpected configuration count or toolset')
        report['generated_configuration_checks'] = generated
        print('[PASS] generated C++23/static CRT/v143: 16 target/configuration pairs', flush=True)
        if not invoke('build', [msbuild, ROOT / 'GEngine.sln',
                      '/t:' + ';'.join(TARGETS[1:]), '/m:1', '/nr:false', '/nologo', '/v:normal',
                      '/p:Configuration=' + args.configuration, '/p:Platform=x64',
                      '/p:VCToolsVersion=' + vc.name, '/p:WindowsTargetPlatformVersion=' + sdk_version,
                      '/bl:' + str(out / 'build.binlog')], timeout=1800):
            return 1
        binary = ROOT / 'bin' / args.configuration / 'RenderingValidation/RenderingValidation.exe'
        # This proof uses direct console output and no SDL/GL initialization.
        env['SDL_VIDEODRIVER'] = 'phase26-cpp23-must-not-initialize-video'
        if not invoke('cpp23', [binary, '--cpp23'], marker='[PASS] cpp23-capability success/failure/void/move-only'):
            return 1
        report['outputs'] = {}
        for target in TARGETS:
            suffix = '.lib' if target == 'GEngine' else '.exe'
            path = ROOT / 'bin' / args.configuration / target / (target + suffix)
            report['outputs'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        report['result'] = 'PASS'
        return 0
    except (OSError, ValueError, subprocess.SubprocessError, ET.ParseError) as error:
        report['reason'] = str(error)
        print('[FAIL]', error, flush=True)
        return 1
    finally:
        report['exit'] = 0 if report['result'] == 'PASS' else 1
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f"[{report['result']}] {out / 'results.json'}", flush=True)


if __name__ == '__main__':
    sys.exit(main())
