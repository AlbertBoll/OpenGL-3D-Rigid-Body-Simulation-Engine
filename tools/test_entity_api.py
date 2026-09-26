"""Build ECS consumers and validate entity lifetime and the transform hierarchy on Windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import winreg

from rendering_validation import ROOT, toolchain


def phase66_children_main():
    from test_runtime_failure import Validation
    parser=argparse.ArgumentParser(description='Bounded Phase66 mutable child-list validation; no timing.')
    parser.add_argument('--phase66-children',action='store_true')
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--reuse-build',type=Path)
    args=parser.parse_args();v=Validation(args.configuration,args.output)
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    path=ROOT/'logs/rendering/phase66/entity-children/validation-inputs.json';inputs=json.loads(path.read_text())
    build_inputs={p:r['sha256'] for p,r in inputs.items() if not p.startswith('tools/')}
    try:
        if any(sha(ROOT/p)!=r['sha256'] for p,r in inputs.items()):raise RuntimeError('Input drift before validation')
        v.report['build_inputs']=build_inputs
        if args.reuse_build:
            prior=json.loads(args.reuse_build.read_text());step=next(s for s in prior['steps'] if s['name']=='build')
            if step['result']!='PASS' or prior['configuration']!=args.configuration or prior['build_inputs']!=build_inputs:
                raise RuntimeError('Build reuse input mismatch')
            if prior['engine_library_sha256']!=sha(ROOT/'bin'/args.configuration/'GEngine/GEngine.lib'):raise RuntimeError('Library mismatch')
            v.report['steps'].append(dict(step,reuse=str(args.reuse_build.resolve())))
        else:v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation','PhysicsTests','PhysicsBenchmark'])
        v.report['engine_library_sha256']=sha(ROOT/'bin'/args.configuration/'GEngine/GEngine.lib');v.report['benchmark']='COMPILED ONLY';v.save()
        source=ROOT/'tools/entity_api_probe.cpp'
        v.compile('children-header',[source],['/DENTITY_CHILDREN_SCHEMA_ONLY'],boundary=True)
        probe=v.compile('entity-api',[source,ROOT/'PhysicsTests/src/ProgramGroupingTests.cpp'])
        v.invoke('entity-api',[probe],markers=['[PASS] typed children diagnostics/borrow/copy/move/retirement cycles=3','[PASS] entity-api ','[PASS] hierarchy depth=8192'])
        v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        if any(sha(ROOT/p)!=r['sha256'] for p,r in inputs.items()):raise RuntimeError('Input drift during validation')
        v.report.update(result='PASS',validation_input_sha256=sha(path));return 0
    except (OSError,RuntimeError) as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=["Debug", "Release"], required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--no-build", action="store_true", help="Reuse matching affected-consumer builds")
    args = parser.parse_args()
    config = args.configuration
    out = (args.output or ROOT / "logs/rendering/phase10" / config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {"configuration": config, "steps": []}
    env = {k: v for k, v in os.environ.items() if k.lower() != "path"}
    env["Path"] = os.environ.get("PATH", os.environ.get("Path", ""))
    env.pop("SDL_VIDEODRIVER", None)
    env["CL"] = env.get("CL", "") + " /we4715 /we4716"

    def invoke(name, command, timeout=60, cwd=ROOT, child_env=None, marker=None):
        command = [str(value) for value in command]
        log = out / (name + ".log")
        step = {"name": name, "command": command, "cwd": str(cwd), "log": str(log), "required_output": marker}
        report["steps"].append(step)
        try:
            result = subprocess.run(command, cwd=cwd, env=child_env or env, capture_output=True,
                                    timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            log.write_bytes(result.stdout + result.stderr)
            step["exit"] = result.returncode
            step["result"] = "PASS" if result.returncode == 0 else "FAIL"
            if marker and marker.encode() not in result.stdout:
                step["result"] = "FAIL"
        except subprocess.TimeoutExpired as error:
            log.write_bytes((error.stdout or b"") + (error.stderr or b""))
            step.update(exit=None, result="FAIL", reason="Timeout; owned child terminated")
        print(f"[{step['result']}] {name}: exit={step['exit']}", flush=True)
        return step["result"] == "PASS"

    passed = False
    try:
        msbuild, vc = toolchain()  # Existing VS2022/v143 discovery and static CRT policy.
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots") as key:
            sdk = Path(winreg.QueryValueEx(key, "KitsRoot10")[0])
        sdk_version = max((p.name for p in (sdk / "Lib").iterdir()
                           if (p / "um/x64/kernel32.lib").is_file()), key=lambda name: tuple(map(int, name.split("."))))
        report["toolchain"] = {"msbuild": str(msbuild), "msvc": str(vc), "sdk": str(sdk), "sdk_version": sdk_version}
        if vc.name != "14.44.35207" or sdk_version != "10.0.26100.0":
            report["reason"] = "Recorded compiler/SDK unavailable; toolchain migration is not authorized"
            return 1
        if not args.no_build and not invoke("generate", [ROOT / "vendor/bin/premake/premake5.exe", "vs2022"], 120):
            return 1
        report["compiler_return_guards"] = env["CL"]
        build = [msbuild, ROOT / "GEngine.sln", "/t:GEngineEditor;Breakout;RayTracing;RigidBodySimulation;PhysicsTests;PhysicsBenchmark",
                 "/m:1", "/nr:false", "/nologo", "/v:normal",
                 "/p:Configuration=" + config, "/p:Platform=x64", "/p:VCToolsVersion=" + vc.name,
                 "/p:WindowsTargetPlatformVersion=" + sdk_version, "/bl:" + str(out / "build.binlog")]
        if not args.no_build and not invoke("build", build, 1200):
            return 1
        includes = [vc / "include", *(sdk / "Include" / sdk_version / part for part in ("ucrt", "shared", "um")),
                    ROOT / "GEngine/include", ROOT / "GEngine/include/GEngine", ROOT / "GEngine/include/external",
                    *(ROOT / "external" / part / "include" for part in ("sdl2", "spdlog", "glad", "assimp", "entt", "tbb"))]
        libraries = [vc / "lib/x64", sdk / "Lib" / sdk_version / "ucrt/x64",
                     sdk / "Lib" / sdk_version / "um/x64",
                     *(ROOT / "external" / part / "lib" for part in ("sdl2", "tbb", "assimp", "fmod"))]
        consumer = out / "hierarchy-consumer.cpp"
        consumer.write_text('#include <Scene/_Entity.h>\n'
                            'std::expected<GEngine::WorldTransformUpdate, GEngine::TransformError> '
                            'Evaluate(GEngine::_Scene& scene) { return scene.UpdateWorldTransforms(); }\n'
                            'std::expected<void, GEngine::TransformError> '
                            'Parent(GEngine::_Entity child, GEngine::_Entity parent) { return child.SetParent(parent); }\n')
        native_free = [path for path in includes if not any(part in path.parts for part in ("sdl2", "glad", "assimp", "tbb"))]
        if not invoke("hierarchy-consumer-boundary", [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++23preview",
                      "/EHsc", "/W3", "/MTd" if config == "Debug" else "/MT", "/c", "/showIncludes",
                      "/D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING",
                      *["/I" + str(p) for p in native_free], consumer, "/Fo" + str(out / "hierarchy-consumer.obj")], 120):
            return 1
        dependencies = (out / "hierarchy-consumer-boundary.log").read_text(errors="replace").lower().replace("\\", "/")
        if any(token in dependencies for token in ("/glad/", "/sdl2/", "/opengl/", "/imgui/", "/assimp/", "/gepch.h", "backend.h")):
            report["reason"] = "Concrete backend declaration leaked into the transform consumer"
            return 1
        report["consumer_boundary"] = "PASS"
        executable = out / "entity-api-probe.exe"
        command = [vc / "bin/Hostx64/x64/cl.exe", "/nologo", "/std:c++23preview", "/EHsc", "/W3",
                   "/MTd" if config == "Debug" else "/MT", "/Od" if config == "Debug" else "/O2",
                   "/DSDL_MAIN_HANDLED", "/DGENGINE_PLATFORM_WINDOWS", "/DGENGINE_CONFIG_" + config.upper(),
                   *["/I" + str(p) for p in includes], "/external:W0", "/external:templates-",
                   *["/external:I" + str(p) for p in (ROOT / "external", ROOT / "GEngine/include/external", vc / "include")],
                   ROOT / "tools/entity_api_probe.cpp", ROOT / "PhysicsTests/src/ProgramGroupingTests.cpp",
                   "/Fo" + str(out) + os.sep,
                   "/Fe" + str(executable), "/link", "/SUBSYSTEM:CONSOLE",
                   *(["/OPT:NOREF", "/OPT:NOICF"] if config == "Debug" else []),
                   *["/LIBPATH:" + str(p) for p in libraries], ROOT / "bin" / config / "GEngine/GEngine.lib",
                   ROOT / "external/glad/bin" / config / "glad/glad.lib", "SDL2.lib", "SDL2_ttf.lib",
                   "tbb12.lib", "tbb12_debug.lib", "tbb.lib", "tbb_debug.lib", "assimp.lib",
                   "fmod64_vc.lib", "fmodL64_vc.lib", "fmodstudio64_vc.lib", "fmodstudioL64_vc.lib"]
        if not invoke("compile-probe", command, 120):
            return 1
        sdl = ROOT / "bin" / config / "GEngineEditor/SDL2.dll"
        report["inputs"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                            (executable, ROOT / "bin" / config / "GEngine/GEngine.lib", sdl)}
        env["Path"] = str(sdl.parent) + os.pathsep + env["Path"]
        passed = invoke("entity-api", [executable], cwd=out,
                        marker="[PASS] entity-api ")
        return 0 if passed else 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report["reason"] = str(error)
        return 1
    finally:
        report.update(result="PASS" if passed else "FAIL", exit=0 if passed else 1)
        (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"[{report['result']}] results: {out / 'results.json'}", flush=True)


if __name__ == "__main__":
    sys.exit(phase66_children_main() if '--phase66-children' in sys.argv else main())
