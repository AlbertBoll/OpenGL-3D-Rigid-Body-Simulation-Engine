"""Validate the bounded runtime failure channel, without timing or baseline runs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VS = Path('C:/Program Files/Microsoft Visual Studio/2022/Community')
VC = VS / 'VC/Tools/MSVC/14.44.35207'
SDK = Path('C:/Program Files (x86)/Windows Kits/10')
SDK_VERSION = '10.0.26100.0'

class Validation:
    def __init__(self, config, out):
        self.config, self.out = config, out.resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.env = {k: v for k, v in os.environ.items() if k.lower() != 'path' and not k.startswith('GENGINE_')}
        self.env['Path'] = str(VC / 'bin/Hostx64/x64') + os.pathsep + str(ROOT / 'bin' / config / 'GEngineEditor') + os.pathsep + os.environ.get('PATH', '')
        self.env['CL'] = '/we4715 /we4716'
        self.env.pop('_CL_', None)
        self.env.pop('SDL_VIDEODRIVER', None)
        self.env['GENGINE_ASSET_ROOT'] = str(ROOT / 'bin' / config / 'assets')
        self.env['GENGINE_SHADOW_RESOLUTION'] = '256'
        self.report = {'configuration': config, 'steps': [], 'toolchain': {'msvc': str(VC), 'sdk': SDK_VERSION, 'language': 'c++23preview', 'crt': 'MTd' if config == 'Debug' else 'MT'}, 'timing': 'NOT RUN'}
        self.includes = [VC / 'include', *(SDK / 'Include' / SDK_VERSION / p for p in ('ucrt', 'shared', 'um')),
            ROOT / 'GEngine/include', ROOT / 'GEngine/include/GEngine', ROOT / 'GEngine/include/external',
            *(ROOT / 'external' / p / 'include' for p in ('sdl2', 'spdlog', 'glad', 'assimp', 'entt', 'tbb', 'fmod'))]
        self.libpaths = [VC / 'lib/x64', SDK / 'Lib' / SDK_VERSION / 'ucrt/x64', SDK / 'Lib' / SDK_VERSION / 'um/x64',
            *(ROOT / 'external' / p / 'lib' for p in ('sdl2', 'tbb', 'assimp', 'fmod'))]
        self.flags = ['/nologo', '/std:c++23preview', '/EHsc', '/W3', '/MTd' if config == 'Debug' else '/MT', '/Od' if config == 'Debug' else '/O2',
            '/DSDL_MAIN_HANDLED', '/DGENGINE_PLATFORM_WINDOWS', '/DGENGINE_CONFIG_' + config.upper(),
            '/DGENGINE_RENDER_BASELINE', '/DGENGINE_RENDER_COUNTERS=1', '/DGE_ENABLE_PHYSICS_PROFILING',
            '/D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING', '/external:W0', '/external:templates-',
            *('/external:I' + str(p) for p in (ROOT / 'external', ROOT / 'GEngine/include/external', VC / 'include'))]
        self.libs = [ROOT / 'bin' / config / 'GEngine/GEngine.lib', ROOT / 'external/glad/bin' / config / 'glad/glad.lib',
            'SDL2.lib', 'SDL2_ttf.lib', 'tbb12.lib', 'tbb12_debug.lib', 'tbb.lib', 'tbb_debug.lib', 'assimp.lib',
            'fmod64_vc.lib', 'fmodL64_vc.lib', 'fmodstudio64_vc.lib', 'fmodstudioL64_vc.lib']

    def invoke(self, name, command, expected=0, markers=(), timeout=180, cwd=None):
        cwd = (cwd or self.out).resolve(); cwd.mkdir(parents=True, exist_ok=True)
        step = {'name': name, 'command': list(map(str, command)), 'cwd': str(cwd), 'expected_exit': expected,
            'required_markers': list(markers), 'log': str(self.out / (name + '.log'))}
        self.report['steps'].append(step)
        try:
            result = subprocess.run(step['command'], cwd=cwd, env=self.env, capture_output=True, timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            data = result.stdout + result.stderr
            Path(step['log']).write_bytes(data)
            step.update(exit=result.returncode, result='PASS' if result.returncode == expected and all(m.encode() in data for m in markers) else 'FAIL')
        except subprocess.TimeoutExpired as error:
            Path(step['log']).write_bytes((error.stdout or b'') + (error.stderr or b''))
            step.update(exit=None, result='FAIL', reason='Owned child timed out and was terminated')
        self.save()
        print(f"[{step['result']}] {name}: exit={step['exit']}", flush=True)
        if step['result'] != 'PASS': raise RuntimeError(name + ' failed: ' + step['log'])
        return Path(step['log']).read_text(errors='replace')

    def save(self):
        (self.out / 'results.json').write_text(json.dumps(self.report, indent=2) + '\n')

    def build(self, targets):
        self.invoke('build', [VS / 'MSBuild/Current/Bin/MSBuild.exe', ROOT / 'GEngine.sln', '/t:' + ';'.join(targets),
            '/m:1', '/nr:false', '/nologo', '/v:minimal', '/p:Configuration=' + self.config, '/p:Platform=x64',
            '/p:VCToolsVersion=14.44.35207', '/p:WindowsTargetPlatformVersion=' + SDK_VERSION,
            '/bl:' + str(self.out / 'build.binlog')], timeout=1800, cwd=ROOT)

    def compile(self, name, sources, extra=(), boundary=False):
        directory = self.out / name; directory.mkdir(exist_ok=True)
        includes = self.includes
        if boundary: includes = [p for p in includes if not any(x in p.parts for x in ('sdl2', 'glad', 'assimp', 'fmod'))]
        command = [VC / 'bin/Hostx64/x64/cl.exe', *self.flags, *('/I' + str(p) for p in includes), *extra,
            *sources, '/Fo' + str(directory) + os.sep]
        if boundary: command += ['/c', '/showIncludes']
        else: command += ['/Fe' + str(directory / (name + '.exe')), '/link', '/SUBSYSTEM:CONSOLE',
            *('/LIBPATH:' + str(p) for p in self.libpaths), *self.libs]
        log = self.invoke('compile-' + name, command, timeout=300)
        if boundary:
            normalized = log.lower().replace('\\', '/')
            forbidden = ('/glad/', '/sdl2/', '/opengl/', '/fmod/', '/imgui/', '/assimp/', '/gepch.h', 'backend.h')
            if any(x in normalized for x in forbidden): raise RuntimeError('Native declaration leaked into consumer boundary')
        return directory / (name + '.exe')

    def smoke(self, apps):
        from test_frame_submission import application_smoke
        for app in apps:
            result = application_smoke(ROOT / 'bin' / self.config / app / (app + '.exe'), self.out / ('smoke-' + app), self.env)
            result['name'] = 'smoke-' + app; self.report['steps'].append(result); self.save()
            print('[' + result['result'] + '] ' + result['name'], flush=True)
            if result['result'] != 'PASS': raise RuntimeError(result['name'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reuse-build', type=Path, help='Completed results.json; caller must verify unchanged build inputs')
    args = parser.parse_args()
    v = Validation(args.configuration, args.output)
    try:
        if args.reuse_build:
            prior = json.loads(args.reuse_build.read_text())
            build = next(s for s in prior['steps'] if s['name'] == 'build')
            if build['result'] != 'PASS' or prior['configuration'] != args.configuration: raise RuntimeError('Invalid build reuse')
            v.report['steps'].append(dict(build, reuse=str(args.reuse_build.resolve())))
        else:
            v.build(['GEngineEditor', 'Breakout', 'RayTracing', 'RigidBodySimulation'])
        v.compile('runtime-header', [ROOT / 'tools/runtime_failure_probe.cpp'], ['/DRUNTIME_SCHEMA_ONLY'], boundary=True)
        exe = v.compile('GEngineEditor', [ROOT / 'tools/runtime_failure_probe.cpp'])
        for mode in ('success', 'close', 'input-failure', 'update-failure', 'render-failure'):
            markers = ['[PASS] runtime ' + mode]
            if mode.endswith('failure'): markers += ['subsystem=fixture-subsystem code=fixture-code operation=fixture-operation context=event=37; resource=music: full diagnostic: native=81; detail=unavailable']
            log = v.invoke(mode, [exe, mode], expected=1 if mode.endswith('failure') else 0, markers=markers, cwd=v.out / mode)
            if 'GEngine application failed:' in log: raise RuntimeError('New failure used a legacy catch')
        for probe in ('interpolation', 'input_control', 'frame_submission', 'shutdown'):
            sources = [ROOT / ('tools/' + probe + '_probe.cpp')]
            extra = []
            if probe == 'frame_submission':
                sources += [ROOT / 'RigidBodySimulation/src/main.cpp']; extra += ['/I' + str(ROOT / 'RigidBodySimulation/include')]
            old = v.compile(probe, sources, extra)
            if probe == 'shutdown':
                for mode in ('--lifetimes', '--minimized'):
                    v.invoke('shutdown-' + mode[2:], [old, mode], markers=['[PASS] shutdown ' + mode], cwd=v.out / ('shutdown-' + mode[2:]))
        v.smoke(['GEngineEditor', 'Breakout', 'RayTracing', 'RigidBodySimulation'])
        v.report['result'] = 'PASS'
        v.report['engine_library_sha256'] = hashlib.sha256((ROOT / 'bin' / args.configuration / 'GEngine/GEngine.lib').read_bytes()).hexdigest()
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        v.report.update(result='FAIL', reason=str(error)); print(str(error), flush=True); return 1
    finally: v.save()

if __name__ == '__main__': sys.exit(main())
