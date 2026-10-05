"""Phase 10 bounded GEngine/glad/RBS campaign. Explicit steps, immutable evidence, no retries."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "logs/pre_editor/phase-10/candidate-01"
PROJECTS = ["GEngine/GEngine.vcxproj", "external/glad/glad.vcxproj", "RigidBodySimulation/RigidBodySimulation.vcxproj"]
MSBUILD = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/amd64/MSBuild.exe")


def local(value):
    path = Path(value)
    if not path.is_absolute():
        path = ROOT / path
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError("Output escapes PRE_EDITOR: " + str(path))
    for ancestor in (path, *path.parents):
        if ancestor == ROOT.parent:
            break
        if ancestor.exists() and ancestor.lstat().st_file_attributes & 0x400:
            raise ValueError("Reparse ancestor: " + str(ancestor))
    return path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def identity(path):
    return {"sha256": digest(path), "size": Path(path).stat().st_size}


def save(path, value):
    path = local(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def environment():
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("GENGINE_", "SDL_", "ASAN_"))}
    temporary = local("runtime/tmp")
    temporary.mkdir(parents=True, exist_ok=True)
    env.update(TMP=str(temporary), TEMP=str(temporary), PYTHONDONTWRITEBYTECODE="1",
               GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="safe.directory", GIT_CONFIG_VALUE_0=ROOT.as_posix())
    return env


def compile_members():
    result = []
    for project in PROJECTS:
        for node in ET.parse(ROOT / project).findall(".//{http://schemas.microsoft.com/developer/msbuild/2003}ClCompile"):
            if node.get("Include"):
                path = local((ROOT / project).parent / node.get("Include")).resolve()
                if path.is_file():
                    result.append(path.relative_to(ROOT).as_posix())
    return sorted(result)


def discovery():
    return {scope: sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / scope).glob(pattern) if p.is_file())
            for scope, pattern in [("GEngine/src", "**/*.cpp"), ("RigidBodySimulation/src", "**/*.cpp"), ("external/glad/src", "**/*.c")]}


def verify(manifest):
    for name, expected in manifest["input_hashes"].items():
        if digest(local(name)) != expected:
            raise RuntimeError("Changed relevant input: " + name)
    for name, expected in manifest["external_inputs"].items():
        if identity(name) != expected:
            raise RuntimeError("Changed external input: " + name)
    if compile_members() != manifest["compile_members"] or discovery() != manifest["compile_discovery"]:
        raise RuntimeError("Changed build membership")
    for path in manifest["include_preceding_absent"]:
        if (ROOT / path).exists():
            raise RuntimeError("New include resolution candidate: " + path)


def prepare():
    # Reuse the immediate predecessor's actual build/read dependency membership,
    # not its test results or its source fingerprints. New dependencies are explicit.
    prior_path = ROOT / "logs/pre_editor/phase-09/candidate-12/scenario-manifest.json"
    prior = json.loads(prior_path.read_text())
    closure_path = ROOT / "logs/pre_editor/phase-09/candidate-06/relevant-inputs.json"
    closure = json.loads(closure_path.read_text())
    scope = json.loads((ROOT / "docs/pre_editor/active/PHASE_10_SCOPE.json").read_text())
    amendment = json.loads((ROOT / "docs/pre_editor/active/PHASE_10_SCOPE_AMENDMENT_01.json").read_text())
    owned = scope["production_files"] + amendment["additional_production_files"] + scope["validation_files"]
    for cfg in ("Debug", "Release"):
        for prefix in ("bin", "bin-int"):
            path = local(f"{prefix}/{cfg}/Phase10Candidate01")
            if path.exists():
                raise RuntimeError("Preserve preexisting candidate output: " + str(path))
        local(f"runtime/RigidBodySimulation/{cfg}/phase10-candidate01")
    props = OUT / "isolated.props"
    props_text = ('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n<PropertyGroup>\n'
                     f'<OutDir>{ROOT}\\bin\\$(Configuration)\\Phase10Candidate01\\$(MSBuildProjectName)\\</OutDir>\n'
                     f'<IntDir>{ROOT}\\bin-int\\$(Configuration)\\Phase10Candidate01\\$(MSBuildProjectName)\\</IntDir>\n'
                     '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild>\n</PropertyGroup>\n</Project>\n')
    if local(props).exists():
        assert props.read_text() == props_text, "Preserve differing existing output declaration"
    else:
        with props.open("x", encoding="utf-8") as stream:
            stream.write(props_text)
    names = {p.casefold(): p for p in prior["input_hashes"] if not p.lower().startswith(("logs/", "runtime/", "bin-int/", "docs/pre_editor/active/"))}
    # Built outputs are evidence, not behavior inputs. Prebuilt runtime dependencies remain inputs.
    names = {k: p for k, p in names.items() if not (p.lower().startswith("bin/") and p.lower().endswith((".exe", "/gengine.lib", "/glad.lib")))}
    extra = owned + PROJECTS + [
        "docs/pre_editor/phases/PHASE_10.md", "docs/pre_editor/VALIDATION_CONTRACTS.md",
        "docs/pre_editor/active/PHASE_10_OD03.json", "docs/pre_editor/active/PHASE_10_OD03_PROPOSAL.md",
        "docs/pre_editor/active/PHASE_10_SCOPE.json", "docs/pre_editor/active/PHASE_10_SCOPE_AMENDMENT_01.json",
        "docs/pre_editor/active/PHASE_10_SCOPE_AMENDMENT_01.md", "docs/pre_editor/active/OUTPUT_PATHS.json",
        "runtime/tmp/gpu-capture-tools/RenderDoc_1.46_64/RenderDoc_1.46_64/renderdoc_app.h",
        "tools/test_pre_editor_geometry_templates.py", "tools/test_pre_editor_geometry_authoring.py",
        "tools/test_pre_editor_material_authoring.py", "tools/test_frame_submission.py",
        "tools/rendering_baseline.py", "tools/rendering_validation.py", "tools/postbuild.py",
        props.relative_to(ROOT).as_posix()]
    for p in extra:
        names[p.casefold()] = p
    names = sorted(names.values())
    assets = json.loads((ROOT / "tools/runtime_assets.json").read_text())
    for p in assets["files"].values():
        if p.casefold() not in {n.casefold() for n in names}:
            names.append(p)
    inputs = {p: digest(local(p)) for p in names}
    external = {p: identity(p) for p in prior["external_inputs"]}
    external[str(MSBUILD)] = identity(MSBUILD)
    preceding = sorted({p for row in closure["include_resolutions"] for p in row["preceding_absent"] if not (ROOT / p).exists()})
    for p in owned:
        destination = local(OUT / "source-snapshot-final" / p)
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("xb") as stream:
            stream.write((ROOT / p).read_bytes())
    cases = {}
    glad_reuse = {}
    for cfg in ("Debug", "Release"):
        previous = prior["cases"][cfg]
        runtime_path = ROOT / f"logs/pre_editor/phase-09/candidate-11/runtime-admission-{cfg}.json"
        previous_runtime = json.loads(runtime_path.read_text())["cases"][cfg]
        library_path = f"bin/{cfg}/Phase09Candidate12/glad/glad.lib"
        old_build = ROOT / f"logs/pre_editor/phase-09/candidate-12/build-{cfg}/result.json"
        build_record = json.loads(old_build.read_text())
        assert build_record["result"] == "PASS" and digest(ROOT / library_path) == build_record["products"][library_path]
        glad_sources = {p: expected for p, expected in prior["input_hashes"].items() if p.lower().startswith("external/glad/")}
        assert all(digest(ROOT / p) == expected for p, expected in glad_sources.items())
        assert all(identity(p) == {k: expected[k] for k in ("sha256", "size")} for p, expected in prior["external_inputs"].items())
        glad_reuse[cfg] = {"source": library_path, "identity": identity(ROOT / library_path), "input_hashes": glad_sources,
                           "build_evidence": old_build.relative_to(ROOT).as_posix(), "build_evidence_identity": identity(old_build),
                           "basis": "Unchanged glad sources/project/build flags and external toolchain inputs; exact approved static library bytes reused."}
        cases[cfg] = {
            "executable": f"bin/{cfg}/Phase10Candidate01/RigidBodySimulation/RigidBodySimulation.exe",
            "cwd": f"runtime/RigidBodySimulation/{cfg}/phase10-candidate01",
            "asset_root": f"bin/{cfg}/Phase10Candidate01/assets", "layout": previous["layout"],
            "expected_modules": previous_runtime["modules"],
            "runtime_dependency_basis": {runtime_path.relative_to(ROOT).as_posix(): identity(runtime_path)},
            "markers": ["PRE_EDITOR_PHASE_10_CACHE_LOCATIONS_PASS", "PRE_EDITOR_PHASE_10_WARM_PASS",
                        "PRE_EDITOR_PHASE_10_COLD_PASS", "PRE_EDITOR_PHASE_10_API_PASS", "PRE_EDITOR_PHASE_10_STEADY_PASS"]}
    manifest = {"phase": "10", "candidate": "01", "baseline": scope["baseline"], "recorded_utc": utc(),
                "input_hashes": inputs, "external_inputs": external, "owned_files": owned,
                "compile_members": compile_members(), "compile_discovery": discovery(), "include_preceding_absent": preceding,
                "dependency_membership_basis": {str(prior_path.relative_to(ROOT)): identity(prior_path), str(closure_path.relative_to(ROOT)): identity(closure_path)},
                "cases": cases, "glad_reuse": glad_reuse,
                "toolchain": "MSVC 14.44.35207 / v143 / SDK 10.0.26100.0 / C++23 / Debug MTd / Release MT",
                "protocol": {"processes": 2, "repeats": 0, "runtime_seconds_each": 120, "runtime_seconds_total": 240,
                             "build_seconds_each_configuration": 1800, "warmup_frames": 5, "measured_frames": 30,
                             "performance_claim": False, "gpu_timing_query_drain": "not applicable; no timing campaign",
                             "pass": "All typed/schema/variant/cache/retained-frame assertions, actual warm driver counts zero, responsive RBS and exit 0",
                             "stop": "First unexpected build/runtime/assertion/GL/identity error or timeout; preserve failure; no retries"},
                "fixture": "Phase09 scene retained plus opaque unlit non-shadow custom cube at (0,5,-22), existing Cube mesh; tint (1,.35,.08,1), gain .8; explicit variants 0/1; no random inputs",
                "camera": "Focal (0,3,-22), pitch .35, yaw 0, distance38, FOV pi/4, near .1, far1000",
                "reference_viewport": prior["viewport"], "lighting": prior["lighting"], "quality": prior["quality"],
                "physics": prior["physics"], "pacing": "Existing VSync/frame cap unchanged; functional counts only",
                "observer": "Creation-only driver hooks plus 30 warmed frames; no timing/throughput inference. Hooks restore after observation.",
                "human_acceptance": "Inspect custom cube, diagnostics, ordinary/advanced API boundary and unchanged Phase09 materials; pending",
                "asset_members": assets["files"], "evidence_reuse": "Historical Phase09 material equations/Clone/mapping/shadow quality remain approved; changed compilation/admission/submission receives fresh Phase10 evidence. No old diagnostic rerun."}
    verify(manifest)
    save(OUT / "scenario-manifest.json", manifest)
    print(f"Prepared Phase10 M0: {len(inputs)} relevant repository inputs, {len(external)} external inputs, two bounded RBS processes.", flush=True)


def build(cfg):
    manifest = json.loads((OUT / "scenario-manifest.json").read_text()); verify(manifest)
    folder = local(OUT / f"build-{cfg}"); folder.mkdir()
    common = [f"/p:Configuration={cfg}", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
              "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:ForceImportBeforeCppTargets=" + str(OUT / "isolated.props")]
    for project in PROJECTS:
        command = [str(MSBUILD), project, *common, "/getProperty:OutDir,IntDir,TargetPath,PostBuildEventUseInBuild,VCToolsVersion,WindowsTargetPlatformVersion", "/nologo"]
        result = subprocess.run(command, cwd=ROOT, env=environment(), capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
        save(folder / (Path(project).stem + "-evaluation.json"), {"command": command, "exit": result.returncode, "stdout": result.stdout.decode(errors="replace"), "stderr": result.stderr.decode(errors="replace")})
        assert result.returncode == 0
        properties = json.loads(result.stdout)["Properties"]
        assert Path(properties["OutDir"]).resolve() == ROOT / f"bin/{cfg}/Phase10Candidate01/{Path(project).stem}"
        assert Path(properties["IntDir"]).resolve() == ROOT / f"bin-int/{cfg}/Phase10Candidate01/{Path(project).stem}"
        assert properties["PostBuildEventUseInBuild"] == "false"
        assert properties["VCToolsVersion"] == "14.44.35207" and properties["WindowsTargetPlatformVersion"] == "10.0.26100.0"
    reuse = manifest["glad_reuse"][cfg]
    assert identity(ROOT / reuse["source"]) == reuse["identity"]
    library = local(f"bin/{cfg}/Phase10Candidate01/glad/glad.lib")
    library.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / reuse["source"], library)
    commands = [[str(MSBUILD), project, "/t:Build", *common, "/p:BuildProjectReferences=false", "/m:2", "/nr:false", "/nologo", "/v:normal",
                 "/bl:" + str(folder / (Path(project).stem + ".binlog"))] for project in (PROJECTS[0], PROJECTS[-1])]
    report = {"commands": commands, "reused_glad": reuse, "cwd": str(ROOT), "recorded_utc": utc(), "result": "FAIL", "exits": []}
    started = time.monotonic()
    try:
        with (folder / "build.log").open("xb") as stream:
            for command in commands:
                result = subprocess.run(command, cwd=ROOT, env=environment(), stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=max(1, 1800 - (time.monotonic() - started)), creationflags=subprocess.CREATE_NO_WINDOW)
                report["exits"].append(result.returncode)
                assert result.returncode == 0, "Required affected build failed"
        verify(manifest)
        products = [f"bin/{cfg}/Phase10Candidate01/{name}/{name}.{extension}" for name, extension in [("GEngine", "lib"), ("glad", "lib"), ("RigidBodySimulation", "exe")]]
        report.update(result="PASS", products={p: identity(ROOT / p) for p in products})
    except Exception as error:
        report["reason"] = str(error)
    finally:
        report["seconds"] = time.monotonic() - started
        save(folder / "result.json", report)
    print(json.dumps(report), flush=True)
    if report["result"] != "PASS":
        raise SystemExit(1)


def stage(cfg):
    import postbuild
    manifest = json.loads((OUT / "scenario-manifest.json").read_text()); verify(manifest)
    assert json.loads((OUT / f"build-{cfg}/result.json").read_text())["result"] == "PASS"
    case = manifest["cases"][cfg]
    executable = local(case["executable"]); destination = executable.parent; assets = local(case["asset_root"])
    assert not assets.exists()
    for name in postbuild.runtime_dlls(cfg, "RigidBodySimulation"):
        source = ROOT / f"bin/{cfg}/RigidBodySimulation" / name
        if name == "assimp-vc140-mt.dll":
            source = ROOT / postbuild.ASSIMP_RUNTIME_SOURCE
        target = local(destination / name)
        assert not target.exists()
        shutil.copy2(source, target)
        assert digest(source) == digest(target)
    # TBB runtimes are loaded from the pinned external directory, as in the predecessor.
    postbuild.stage_runtime_assets(ROOT, assets)
    postbuild.stage_assimp_runtime(ROOT, destination)
    directory = local(case["cwd"]); directory.mkdir(parents=True, exist_ok=False)
    (directory / "imgui.ini").write_text(case["layout"], encoding="utf-8")
    modules = {}
    for name, old in case["expected_modules"].items():
        source = Path(old["path"])
        selected = destination / source.name
        if not selected.exists():
            selected = source if source.is_absolute() else ROOT / source
        assert digest(selected) == old["sha256"], str(selected)
        modules[name] = {"path": str(selected.resolve()), "sha256": digest(selected)}
    bundle = {p.relative_to(ROOT).as_posix(): identity(p) for base in (destination, assets) for p in base.rglob("*") if p.is_file() and p.suffix != ".pdb"}
    for path, source in manifest["asset_members"].items():
        assert digest(assets / path) == digest(ROOT / source)
    save(OUT / f"runtime-admission-{cfg}.json", {"recorded_utc": utc(), "binary": identity(executable), "modules": modules,
         "bundle": bundle, "initial_layout": identity(directory / "imgui.ini"), "manifest": identity(OUT / "scenario-manifest.json")})
    print(cfg + " runtime closure staged and verified", flush=True)


def run(cfg):
    from test_pre_editor_geometry_templates import application_smoke
    from test_pre_editor_geometry_authoring import observe_window
    manifest = json.loads((OUT / "scenario-manifest.json").read_text()); verify(manifest)
    admitted = json.loads((OUT / f"runtime-admission-{cfg}.json").read_text())
    for path, expected in admitted["bundle"].items():
        assert identity(ROOT / path) == expected, path
    case = manifest["cases"][cfg]
    directory = local(case["cwd"]); folder = local(OUT / f"runtime-{cfg}"); folder.mkdir()
    assert identity(directory / "imgui.ini") == admitted["initial_layout"]
    env = environment()
    env.update(GENGINE_ASSET_ROOT=str(local(case["asset_root"])), GENGINE_PRE_EDITOR_SHADER_DESCRIPTIONS="1")
    path_key = next((k for k in env if k.lower() == "path"), "PATH")
    env[path_key] = str(ROOT / "external/tbb/bin") + os.pathsep + env.get(path_key, "")
    stopped = threading.Event()
    observations = {}
    worker = threading.Thread(target=observe_window, args=(local(case["executable"]), stopped, observations)); worker.start()
    start = time.monotonic()
    try:
        report = application_smoke(local(case["executable"]), directory, env, admitted["modules"], case["markers"])
    finally:
        stopped.set(); worker.join()
    for name in ("application.log", "GEngine.log", "imgui.ini", "smoke.json"):
        if (directory / name).is_file():
            shutil.copy2(directory / name, folder / name)
    text = "\n".join((folder / name).read_text(encoding="utf-8", errors="replace") for name in ("application.log", "GEngine.log") if (folder / name).exists())
    report["markers"] = {marker: marker in text for marker in case["markers"]}
    report["unexpected_phase_failure"] = bool(re.search(r"PRE_EDITOR_PHASE_10_(FAIL|STEADY_FAIL)", text))
    report["seconds"] = time.monotonic() - start
    report["window_observations"] = observations
    if not all(report["markers"].values()) or report["unexpected_phase_failure"] or report["seconds"] > 120 or not observations:
        report["result"] = "FAIL"
    verify(manifest)
    save(folder / "result.json", report)
    print(json.dumps({k: v for k, v in report.items() if k not in ("loaded_modules", "required_modules")}), flush=True)
    if report["result"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("step", choices=["prepare", "build", "stage", "run"])
    parser.add_argument("--configuration", choices=["Debug", "Release"])
    args = parser.parse_args()
    if args.step == "prepare":
        prepare()
    else:
        if not args.configuration:
            parser.error("Configuration is required")
        globals()[args.step](args.configuration)
