"""Build and validate semantic framebuffer ownership and correctness."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import time
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import winreg

from rendering_validation import ROOT, toolchain


OUT = ROOT / "logs/rendering/phase28/final/smoke"

def environment():
    env = dict(os.environ)
    env["Path"] = env.pop("PATH", env.get("Path", ""))
    env.pop("GENGINE_SHADOW_RESOLUTION", None)
    env.pop("GENGINE_ASSET_ROOT", None)
    return env


def framebuffer_smoke(config, only=None, label_prefix=""):
    user = c.WinDLL("user32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.IsHungAppWindow.argtypes = [w.HWND]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]

    def windows(pid):
        found = []

        @callback_type
        def visit(hwnd, unused):
            owner = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(owner))
            if owner.value == pid and user.IsWindowVisible(hwnd):
                cls = c.create_unicode_buffer(256)
                user.GetClassNameW(hwnd, cls, 256)
                found.append(dict(hwnd=hwnd, cls=cls.value, hung=bool(user.IsHungAppWindow(hwnd))))
            return True

        user.EnumWindows(visit, 0)
        return found

    results = []
    cases = [(app, None) for app in ["GEngineEditor", "RigidBodySimulation", "Breakout", "RayTracing"]]
    cases += [(app, "4096") for app in ["GEngineEditor", "RigidBodySimulation"]]
    cases += [(app, "invalid") for app in ["GEngineEditor", "RigidBodySimulation", "Breakout", "RayTracing"]]
    cases += [("GEngineEditor", value) for value in ["0", "8193", "-1", "2048junk", "999999999999999999999"]]
    if only:
        cases = [(app, value) for app, value in cases if app + ":" + (value or "default") == only]
        if not cases:
            raise ValueError("Unknown smoke case: " + only)
    for app, override in cases:
        label = label_prefix + config.lower() + "-" + app.lower() + "-" + (override or "default")
        executable = ROOT / "bin" / config / app / (app + ".exe")
        cwd = OUT / "runtime" / label
        cwd.mkdir(parents=True, exist_ok=True)
        env = environment()
        if override is not None:
            env["GENGINE_SHADOW_RESOLUTION"] = override
        negative = override not in [None, "4096"]
        record = dict(config=config, app=app, override=override, cwd=str(cwd), command=[str(executable)],
                      binary_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
        # Keep application settings exact; only disposable fixture files may be written.
        guards = []
        before = {}
        for path in [cwd / "imgui.ini", executable.parent / "imgui.ini"]:
            if path.exists():
                before[path] = path.read_bytes()
                handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0, None)
                assert handle != c.c_void_p(-1).value
                guards.append(handle)
        process = None
        try:
            with (OUT / (label + ".log")).open("wb") as log:
                process = subprocess.Popen([str(executable)], cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                record["pid"] = process.pid
                ready = 0
                observations = []
                for second in range(90):
                    time.sleep(1)
                    current = windows(process.pid)
                    observations.append(dict(second=second + 1, windows=current))
                    responsive = any(x["cls"] == "SDL_app" and not x["hung"] for x in current)
                    responsive &= not any(x["cls"] == "#32770" for x in current)
                    ready = ready + 1 if responsive and second >= 10 else 0
                    if process.poll() is not None or (not negative and ready >= 5):
                        break
                record["responsive"] = ready >= 5
                record["observations"] = observations
                if process.poll() is None:
                    for window in windows(process.pid):
                        if window["cls"] == "SDL_app":
                            user.PostMessageW(window["hwnd"], 0x10, 0, 0)
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    record["forced_termination"] = True
                    process.kill()  # Only the fixture-owned process.
                    process.wait(timeout=10)
                record["exit"] = process.returncode
            text = (OUT / (label + ".log")).read_text(errors="replace")
            if negative:
                passed = process.returncode == 1 and "GENGINE_SHADOW_RESOLUTION must be an integer from 1 to 8192" in text
                passed &= "[Shadows] cascade" not in text and "[Shadows] point" not in text
            else:
                size = override or "4096"
                source = "GENGINE_SHADOW_RESOLUTION (explicit)" if override else "safe default"
                expected = ["resolution=" + size + "x" + size + " source=" + source, "Runtime assets [" + app + "]:"]
                if config == "Debug": expected.append("Initialize Scene...")
                # EntryPoint returns nonzero for a typed startup failure. A responsive
                # application with a clean native-close exit has completed all checked
                # target factories; GL target correctness is tested by the focused probe.
                record["target_completeness_contract"] = "Successful application initialization requires every framebuffer factory to complete"
                record["high_gl_diagnostic_count"] = text.count("severity=high")
                record["diagnostics_match"] = all(token in text for token in expected)
                passed = ready >= 5 and process.returncode == 0 and record["diagnostics_match"]
                passed &= "allocation failed" not in text and "resolution=8192" not in text
            record["result"] = "PASS" if passed and not record.get("forced_termination") else "FAIL"
        finally:
            if process and process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            for handle in guards:
                kernel.CloseHandle(handle)
            assert all(path.read_bytes() == data for path, data in before.items())
        (OUT / (label + ".json")).write_text(json.dumps(record, indent=2) + "\n")
        print(json.dumps({k: v for k, v in record.items() if k != "observations"}), flush=True)
        results.append(record)
    suffix = "-" + only.replace(":", "-") if only else ""
    (OUT / (label_prefix + "smoke-" + config.lower() + suffix + ".json")).write_text(json.dumps(results, indent=2) + "\n")
    return all(r["result"] == "PASS" for r in results)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--smoke', action='store_true', help='Run four default application startup/native-close checks')
    parser.add_argument('--regression', action='append', help='Run only the named regression probe stem(s)')
    parser.add_argument('--mode', choices=['all','gl','reject-worker','reject-context','startup-failure','regressions'], default='all')
    parser.add_argument("--no-build", action="store_true", help="Use already-built matching candidate libraries/consumers; still compile and run the focused probes")
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / 'logs/rendering/phase28/final' / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if args.smoke:
        global OUT
        OUT = out
        outcomes = [framebuffer_smoke(config, app + ':default') for app in
                    ('GEngineEditor', 'RigidBodySimulation', 'Breakout', 'RayTracing')]
        passed = all(outcomes)
        (out / 'results.json').write_text(json.dumps({'configuration': config, 'mode': 'smoke',
            'result': 'PASS' if passed else 'FAIL', 'exit': 0 if passed else 1,
            'evidence': 'Per-application JSON records native exits, responsiveness, diagnostics and exact binary identity'}, indent=2) + '\n')
        return 0 if passed else 1
    report = {'configuration': config, 'steps': [], 'build_reused': args.no_build,
              'mode': args.mode, 'regression_selection': args.regression, 'build_only': args.build_only}
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
        header_probe = out / 'framebuffer-consumer.cpp'
        header_probe.write_text('#include "Core/FrameBuffer.h"\n#include "Core/RenderTarget.h"\n#include "UI/FramebufferImage.h"\n'
            '#if defined(GL_TEXTURE_2D) || defined(SDL_MAJOR_VERSION) || defined(GLAD_GL_H_)\n#error Native backend leaked\n#endif\n'
            'static_assert(!std::is_copy_constructible_v<GEngine::FrameBuffer>);\n'
            'static_assert(std::is_nothrow_move_constructible_v<GEngine::FrameBuffer>);\n'
            'static_assert(std::is_same_v<decltype(GEngine::FrameBuffer::Create({})), std::expected<GEngine::FrameBuffer, GEngine::FramebufferError>>);\n'
            'static_assert(std::is_trivially_copyable_v<GEngine::RenderTargetDesc>);\n'
            'static_assert(std::is_same_v<decltype(GEngine::RenderTarget::Create(GEngine::RenderTargetDesc{})), std::expected<GEngine::RenderTarget, GEngine::FramebufferError>>);\n'
            'auto reconfigure(GEngine::RenderTarget& t, GEngine::RenderTargetDesc d) { d.Usage = GEngine::RenderTargetUsage::Attachment | GEngine::RenderTargetUsage::Sampled; return t.Reconfigure(d); }\n'
            'auto consume(const GEngine::RenderTarget& t) { return t.ColorView(); }\n'
            'GEngine::ApplicationInitializationResult startupFailure() { return std::unexpected(GEngine::FramebufferError{GEngine::FramebufferErrorCode::Allocation, "failure"}); }\n')
        if not invoke('framebuffer-consumer-boundary', [*common, '/showIncludes', '/c',
                      '/I' + str(ROOT / 'GEngine/include/external'), header_probe,
                      '/Fo' + str(out / 'framebuffer-consumer.obj')], 120):
            return 1
        dependencies = (out / 'framebuffer-consumer-boundary.log').read_text(errors='replace').lower().replace('\\', '/')
        if any(token in dependencies for token in ('/glad/', '/sdl2/', '/opengl/', '/imgui/')):
            report['reason'] = 'Native dependency leaked into the normal framebuffer consumer probe'
            return 1
        source = ROOT / 'tools/framebuffer_probe.cpp'
        if not args.no_build and not invoke('generate', [ROOT / 'vendor/bin/premake/premake5.exe', 'vs2022'], 120):
            return 1
        if not args.no_build and not invoke('build', [msbuild, ROOT / 'GEngine.sln',
                      '/t:GEngineEditor;Breakout;RayTracing;RigidBodySimulation;PhysicsTests;PhysicsBenchmark',
                      '/m:1', '/nr:false', '/nologo', '/v:normal', '/p:Configuration=' + config,
                      '/p:Platform=x64', '/p:VCToolsVersion=' + vc.name, '/p:WindowsTargetPlatformVersion=' + sdk_version,
                      '/bl:' + str(out / 'build.binlog')], 1200):
            return 1
        if args.build_only:
            passed = True
            return 0
        extra_includes = [ROOT / 'GEngine/include', ROOT / 'GEngine/include/external',
                          *(ROOT / 'external' / part / 'include' for part in ('sdl2', 'spdlog', 'glad', 'assimp', 'entt', 'tbb'))]
        extra_libraries = [ROOT / 'external' / part / 'lib' for part in ('sdl2', 'tbb', 'assimp', 'fmod')]
        gl = out / 'framebuffer-probe.exe'
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
        for mode, marker, expected in (('--gl', '[PASS] framebuffers cycles=2', 0),
                                       ('--reject-worker', '[EXPECTED] framebuffer ownership invariant', 86),
                                       ('--reject-context', '[EXPECTED] framebuffer ownership invariant', 86),
                                       ('--startup-failure', '[PASS] typed startup failures and partial ownership cleanup', 0)):
            if args.mode != 'all' and mode[2:] != args.mode:
                continue
            runtime = out / mode[2:]
            runtime.mkdir(exist_ok=True)
            if not invoke(mode[2:], ([gl] if mode == '--gl' else [gl, mode]), 240, expected=expected, marker=marker, cwd=runtime):
                return 1
        if args.mode in ('all', 'regressions'):
            for filename, defines, cases in (
                ('input_control_probe.cpp', [], []),
                ('interpolation_probe.cpp', [], []),
                ('uniform_renderbuffer_probe.cpp', [], [([], '[PASS] uniform-renderbuffer-RAII cycles=2')]),
                ('shadow_framebuffer_probe.cpp', [], [([], 'PASS: dimensions, completeness, clear, cleanup')]),
                ('asset_registry_probe.cpp', ['/DGENGINE_REGISTRY_GL'], [(['--gl'], '[PASS] asset-registry-GL cycles=2')]),
                ('shutdown_probe.cpp', [], [( [mode], '[PASS] shutdown ' + mode) for mode in
                    ('--lifetimes', '--application-failure', '--resource-moves', '--cache-ownership', '--imgui-context-failure', '--imgui-platform-failure', '--empty-windows')])):
                if args.regression and Path(filename).stem not in args.regression:
                    continue
                executable = out / (Path(filename).stem + '.exe')
                command = [*defines, *gl_command]
                command[command.index(source)] = ROOT / 'tools' / filename
                command[command.index('/Fo' + str(out / 'gl.obj'))] = '/Fo' + str(executable.with_suffix('.obj'))
                command[command.index('/Fe' + str(gl))] = '/Fe' + str(executable)
                # Keep the compiler executable first, followed by additional probe definitions.
                if defines:
                    command = [command[len(defines)], *defines, *command[len(defines)+1:]]
                if not invoke('compile-' + executable.stem, command, 120):
                    return 1
                report['inputs'][str(executable)] = hashlib.sha256(executable.read_bytes()).hexdigest()
                for arguments, marker in cases:
                    case = executable.stem + ('-' + arguments[0][2:] if arguments else '')
                    runtime = out / case
                    runtime.mkdir(exist_ok=True)
                    if not invoke(case, [executable, *arguments], 240, marker=marker, cwd=runtime):
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
