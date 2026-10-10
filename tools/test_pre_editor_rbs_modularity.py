"""Phase 14 isolated Packet A checks and separately admitted Packet B Transform checks."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "logs/pre_editor/phase-14/packet-a/entry-01/entry.json"
PRIOR = ROOT / "logs/pre_editor/phase-13/candidate-04/scenario-manifest.json"
PROJECTS = ["GEngine/GEngine.vcxproj", "external/glad/glad.vcxproj",
            "RigidBodySimulation/RigidBodySimulation.vcxproj"]
MSBUILD = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/amd64/MSBuild.exe")
OUT = None
CANDIDATE = None
VALIDATION = "docking"
PACKET = "A"
LEGACY_RBS_ENVIRONMENT_KEYS = [
    "GENGINE_PRE_EDITOR_SCENE_AUTHORING", "GENGINE_PRE_EDITOR_RESOURCE_OWNERSHIP",
    "GENGINE_PRE_EDITOR_GEOMETRY_TEMPLATES", "GENGINE_PRE_EDITOR_PARAMETRIC_GEOMETRY",
    "GENGINE_PRE_EDITOR_GEOMETRY_AUTHORING", "GENGINE_PRE_EDITOR_MATERIAL_AUTHORING",
    "GENGINE_PRE_EDITOR_SHADER_DESCRIPTIONS", "GENGINE_PRE_EDITOR_SHADER_RELOAD",
    "GENGINE_PRE_EDITOR_ASYNC_RESOURCES", "GENGINE_PRE_EDITOR_SUBSCRIPTIONS",
    "GENGINE_ASYNC_MESH_SMOKE", "GENGINE_PRE_EDITOR_ASYNC_PERFORMANCE",
    "GENGINE_PRE_EDITOR_ASYNC_ADMISSION_ONLY", "GENGINE_PRE_EDITOR_MATERIAL_ALPHA_FIXTURE",
    "GENGINE_PRE_EDITOR_MATERIAL_FLOOR_COMPARISON", "GENGINE_PRE_EDITOR_SUBSCRIPTION_OUTPUT",
    "GENGINE_ASSET_ROOT", "GENGINE_SHADOW_QUALITY", "GENGINE_SHADOW_RESOLUTION",
    "GENGINE_PASS_TIMING", "GENGINE_PASS_TIMING_OUTPUT", "GENGINE_RENDER_COUNTERS_LOG",
    "GENGINE_BASELINE_OUTPUT", "GENGINE_BASELINE_SCENE_TARGET",
    "GENGINE_PRE_EDITOR_POINT_SHADOW_DIAGNOSTIC", "GENGINE_PRE_EDITOR_POINT_GPU_CAPTURE",
]


def local(value):
    path = Path(value)
    if not path.is_absolute():
        path = ROOT / path
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError("Output escapes PRE_EDITOR: " + str(path))
    for parent in (path, *path.parents):
        if parent == ROOT.parent:
            break
        if parent.exists() and parent.lstat().st_file_attributes & 0x400:
            raise ValueError("Reparse output ancestor: " + str(parent))
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
    env = {k: v for k, v in os.environ.items()
           if not k.upper().startswith(("GENGINE_", "SDL_", "ASAN_"))}
    temporary = local("runtime/tmp")
    temporary.mkdir(parents=True, exist_ok=True)
    env.update(TMP=str(temporary), TEMP=str(temporary), PYTHONDONTWRITEBYTECODE="1")
    return env


def git(*args):
    return subprocess.run(["git", "-c", "safe.directory=" + ROOT.as_posix(), *args],
                          cwd=ROOT, capture_output=True, check=True).stdout


def compile_members():
    result = {}
    ns = "{http://schemas.microsoft.com/developer/msbuild/2003}"
    for project in PROJECTS:
        result[project] = sorted({local((ROOT / project).parent / node.get("Include")).resolve()
                                 .relative_to(ROOT).as_posix()
                                 for node in ET.parse(ROOT / project).findall(".//" + ns + "ClCompile")
                                 if node.get("Include")})
    return result


def discovery():
    return {scope: sorted(p.relative_to(ROOT).as_posix()
                          for p in (ROOT / scope).glob(pattern) if p.is_file())
            for scope, pattern in [("GEngine/src", "**/*.cpp"),
                                   ("RigidBodySimulation/src", "**/*.cpp"),
                                   ("external/glad/src", "**/*.c")]}


def guards():
    entry = json.loads(ENTRY.read_text())
    assert git("rev-parse", "HEAD").decode().strip() == entry["baseline"]
    assert git("branch", "--show-current").decode().strip() == "pre-editor/refactor"
    assert hashlib.sha256(git("ls-files", "--stage", "-z")).hexdigest() == entry["index_identity"]
    for path, expected in entry["protected"].items():
        if expected.get("absent"):
            assert not (ROOT / path).exists(), path
        else:
            assert digest(ROOT / path) == expected["sha256"], path
    changed = set(git("diff", "--name-only", "HEAD").decode().splitlines())
    changed.update(git("ls-files", "--others", "--exclude-standard").decode().splitlines())
    assert changed <= set(entry["owned_files"]), "Unclassified paths: " + repr(changed - set(entry["owned_files"]))
    return entry


def static():
    entry = guards()
    checks = []
    for path in entry["owned_files"]:
        if not (ROOT / path).exists():
            raise RuntimeError("Missing owned file: " + path)
        tracked = bool(git("ls-files", "--", path).strip())
        command = ["git", "-c", "safe.directory=" + ROOT.as_posix(), "diff"]
        command += ["--check", "HEAD", "--", path] if tracked else ["--no-index", "--check", "--", "NUL", path]
        result = subprocess.run(command, cwd=ROOT, capture_output=True)
        assert result.returncode in (0, 1) and not result.stdout, result.stdout.decode(errors="replace")
        checks.append({"path": path, "new": not tracked, "exit": result.returncode})
    for folder in ["RigidBodySimulation/src", "RigidBodySimulation/tests", "RigidBodySimulation/include"]:
        for path in (ROOT / folder).glob("*.*"):
            assert not re.search(r"\b(?:getenv|SDL_getenv|_dupenv_s|GetEnvironmentVariable\w*|CaptureEngineLaunchConfig)\s*\(", path.read_text()), path
    for name in ["RbsLaunchConfig.h", "RbsState.h", "RigidBodySimulation.h"]:
        assert "using namespace" not in (ROOT / "RigidBodySimulation/include" / name).read_text()
    for name in ["Core/LaunchConfig.h", "UI/Dockspace.h"]:
        source = (ROOT / "GEngine/include/GEngine" / name).read_text()
        assert not re.search(r"\b(?:ImGui\w*|ImVec\w*|SDL_\w+|GLuint)\b", source)
    assert "EntryPoint.h" not in (ROOT / "RigidBodySimulation/src/RigidBodySimulation.cpp").read_text()
    assert sum(len(re.findall(r"\bint\s+main\s*\(", p.read_text()))
               for p in (ROOT / "RigidBodySimulation/src").glob("*.cpp")) == 1
    ui = (ROOT / "RigidBodySimulation/src/RbsUi.cpp").read_text()
    assert ui.index("BeginDockspaceHost") < ui.index("DrawShaderPanel();") < ui.index("DrawViewport();")
    scene = (ROOT / "RigidBodySimulation/src/RbsScene.cpp").read_text()
    assert not re.search(r"\bactivate_(?:boxes_stacking|sphere_\w+)\b", scene)
    assert "switch (m_Launch.scene)" in scene
    assert "GENGINE_RBS_GEOMETRY_TEMPLATE_GALLERY" not in scene
    config = (ROOT / "RigidBodySimulation/include/RbsLaunchConfig.h").read_text()
    assert "enum class RbsScenePreset" in config and "PhysicsScenePreset" not in config
    assert "constexpr auto scenePreset = Rbs::RbsScenePreset::GeometryGallery;" in (ROOT / "RigidBodySimulation/src/main.cpp").read_text()
    save(OUT / "static-result.json", {"result": "PASS", "recorded_utc": utc(),
         "whitespace": checks, "scope": "Packet " + PACKET + "; declared protection and index unchanged"})
    print("Packet " + PACKET + " static ownership/configuration/whitespace checks PASS", flush=True)


def include_closure(names, absent):
    # Extend the predecessor's compiler-observed closure for the newly split TUs.
    # The ordered project include search, including missing earlier candidates,
    # detects a newly added header shadowing a current input at approval.
    ns = "{http://schemas.microsoft.com/developer/msbuild/2003}"
    for project in PROJECTS:
        dirs = []
        tree = ET.parse(ROOT / project)
        nodes = tree.findall(".//" + ns + "AdditionalIncludeDirectories") + tree.findall(".//" + ns + "IncludePath")
        for node in nodes:
            for value in (node.text or "").split(";"):
                if value and "%" not in value and "$" not in value:
                    path = ((ROOT / project).parent / value).resolve()
                    if path not in dirs:
                        dirs.append(path)
        queue = [ROOT / name for name in compile_members()[project]]
        seen = set()
        while queue:
            path = queue.pop()
            if path in seen:
                continue
            seen.add(path)
            names.add(path.relative_to(ROOT).as_posix())
            for quote, header in re.findall(r'^\s*#\s*include\s*([<"])([^>"\r\n]+)[>"]',
                                            path.read_text(errors="replace"), re.M):
                candidates = ([path.parent] if quote == '"' else []) + dirs
                for base in candidates:
                    candidate = Path(os.path.normpath(base / header))
                    if not candidate.is_relative_to(ROOT):
                        continue
                    name = candidate.relative_to(ROOT).as_posix()
                    if candidate.is_file():
                        queue.append(candidate)
                        break
                    absent.add(name)


def prepare():
    entry = guards()
    prior = json.loads(PRIOR.read_text())
    OUT.mkdir(parents=True, exist_ok=False)
    props = OUT / "isolated.props"
    probe_directory = "Scenes" if VALIDATION == "scenes" else "Docking"
    probe_macro = "GENGINE_RBS_MODULARITY_VALIDATION"
    if VALIDATION == "scenes":
        probe_macro += ";GENGINE_RBS_SCENE_VALIDATION"
    if VALIDATION == "transform":
        probe_directory = "Transform"
        probe_macro += ";GENGINE_RBS_TRANSFORM_VALIDATION"
    props.write_text('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        '<PropertyGroup>\n'
        f'<OutDir>{ROOT}/bin/$(Configuration)/Phase14Candidate{CANDIDATE}/$(MSBuildProjectName)/</OutDir>\n'
        f'<IntDir>{ROOT}/bin-int/$(Configuration)/Phase14Candidate{CANDIDATE}/$(MSBuildProjectName)/</IntDir>\n'
        '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild>\n</PropertyGroup>\n'
        '<PropertyGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\' and \'$(PacketAProbe)\'==\'true\'">\n'
        f'<OutDir>{ROOT}/bin/$(Configuration)/Phase14Candidate{CANDIDATE}/{probe_directory}/RigidBodySimulation/</OutDir>\n'
        f'<IntDir>{ROOT}/bin-int/$(Configuration)/Phase14Candidate{CANDIDATE}/{probe_directory}/RigidBodySimulation/</IntDir>\n'
        '</PropertyGroup>\n'
        '<ItemDefinitionGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\' and \'$(PacketAProbe)\'==\'true\'">\n'
        f'<ClCompile><PreprocessorDefinitions>{probe_macro};%(PreprocessorDefinitions)</PreprocessorDefinitions>'
        '</ClCompile></ItemDefinitionGroup>\n</Project>\n', encoding="utf-8")
    names = {p for p in prior["input_hashes"] if not p.startswith(
        ("logs/", "runtime/", "bin-int/", "docs/pre_editor/active/", "docs/pre_editor/phases/", "tools/test_pre_editor_"))}
    names = {p for p in names if not (p.lower().startswith("bin/") and
             p.lower().endswith((".exe", "/gengine.lib", "/glad.lib")))}
    names.update(entry["owned_files"] + PROJECTS + ["CODING_STYLE.md", ".clang-format",
        "docs/pre_editor/phases/PHASE_14.md", "docs/pre_editor/VALIDATION_CONTRACTS.md",
        "docs/pre_editor/active/OUTPUT_PATHS.json", "docs/pre_editor/active/PHASE_14_SCOPE_AMENDMENT_02.md",
        "tools/postbuild.py", "tools/runtime_assets.json",
        props.relative_to(ROOT).as_posix()])
    if PACKET == "B":
        names.update([
            "docs/pre_editor/active/PHASE_14_PACKET_B_MUTATION_POLICY_PROPOSAL_01.md",
            "docs/pre_editor/active/PHASE_14_PACKET_B_MUTATION_POLICY_ACCEPTANCE_01.json",
            "docs/pre_editor/active/PHASE_14_PACKET_A_OWNER_ACCEPTANCE_CANDIDATE_08.json",
            "logs/pre_editor/phase-14/packet-b/entry-01/validation-environment.json",
            "docs/pre_editor/active/PHASE_14_PACKET_B_VALIDATION_AMENDMENT_01_PROPOSAL.md",
            "docs/pre_editor/active/PHASE_14_PACKET_B_VALIDATION_AMENDMENT_01_ACCEPTANCE.json",
            "docs/pre_editor/active/PHASE_14_EXECUTION_AUTONOMY_01.json",
        ])
    absent = {p for p in prior["include_preceding_absent"] if not (ROOT / p).exists()}
    include_closure(names, absent)
    absent = {p for p in absent if not (ROOT / p).exists()}
    for path in entry["owned_files"]:
        target = local(OUT / "source-snapshot" / path)
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write((ROOT / path).read_bytes())
    external = {p: identity(p) for p in prior["external_inputs"]}
    external[str(MSBUILD)] = identity(MSBUILD)
    for cfg, reuse in prior["glad_reuse"].items():
        assert identity(ROOT / reuse["source"]) == reuse["identity"]
        assert all(digest(ROOT / p) == value for p, value in reuse["input_hashes"].items())
        assert identity(ROOT / reuse["build_evidence"]) == reuse["build_evidence_identity"]
        for prefix in ("bin", "bin-int"):
            assert not local(f"{prefix}/{cfg}/Phase14Candidate{CANDIDATE}").exists()
    manifest = {"phase": 14, "packet": PACKET, "candidate": CANDIDATE, "recorded_utc": utc(),
        "baseline": entry["baseline"], "owned_files": entry["owned_files"],
        "input_hashes": {p: digest(ROOT / p) for p in sorted(names)}, "external_inputs": external,
        "include_preceding_absent": sorted(absent), "compile_members": compile_members(),
        "compile_discovery": discovery(), "predecessor_manifest": identity(PRIOR),
        "glad_reuse": prior["glad_reuse"], "expected_modules": {c: prior["cases"][c]["expected_modules"]
        for c in ("Debug", "Release")}, "layout": prior["cases"]["Debug"]["layout"],
        "asset_members": json.loads((ROOT / "tools/runtime_assets.json").read_text())["files"],
        "protocol": {"build_seconds_each_configuration": 1800, "runtime_seconds_each": 180,
            "processes_each_configuration": 2, "repeats": 0, "frames_each": 240,
            "pass": "Static guards, both affected builds, captured-config cases, all three panel operations retained across frames, compatible/mixed layout restore, viewport check, clean exit and shutdown; owner visual/gesture review remains pending",
            "stop": "Any failed check or identity drift. Preserve attempt; only proved infrastructure corrections can continue under the execution skill.",
            "metrics": "Functional state, dimensions and identities only; no performance comparison",
            "fixture": "Maintained source scene, camera, lights, materials, Physics and quality defaults. Only test dock/window layout changes. No Phase04-13 diagnostic/performance flags.",
            "human": "Native dock gestures, readable/usable controls, picking/input capture, minimize/restore and supported DPI inspection"},
        "toolchain": "MSVC14.44.35207/v143/SDK10.0.26100.0/C++23; Debug MTd / Release MT",
        "derived_outputs": "Candidate-specific bin/bin-int; isolated runtime settings/logs; original tested product identities are recorded separately",
        "packet_b": "EXCLUDED"}
    manifest["validation_kind"] = VALIDATION
    manifest["probe_directory"] = probe_directory
    if VALIDATION == "transform":
        manifest["packet_b"] = "ACCEPTED_TRANSFORM_ONLY_SUCCESS_POLICY"
        manifest["environment"] = json.loads((ROOT / "logs/pre_editor/phase-14/packet-b/entry-01/validation-environment.json").read_text())
        manifest["protocol"] = {
            "build_seconds_each_configuration": 1800,
            "runtime_seconds_by_configuration": {"Debug": 360, "Release": 180},
            "processes_each_configuration": 1, "repeats": 0, "frames_each": 240,
            "runtime_cases": ["GeometryGalleryTransform"],
            "fixture": "GeometryGallery default unchanged; isolated deterministic Transform notification/hierarchy/collider/kinematic fixtures in RBS startup; existing 240-frame docking/resource/action/viewport/shutdown observer exercises the newly linked Scene closure.",
            "pass": "Every Transform check and affected scale/lifetime check passes; rejected/no-op mutation silent; committed result explicitly Transform-only; collider rejection independent; hierarchy and interpolation checks; current RBS integration markers, native minimize/restore, positive DPI and clean shutdown.",
            "tolerance": "Kinematic interpolation position absolute error <= 0.0001 world units; exact identities, state/event counts, hierarchy translations, shape bounds/revisions otherwise.",
            "metrics": "Functional assertions only; no timing/cost/performance claims or historical campaigns.",
            "reuse": "Frozen Packet A scene/config/no-target/docking policy and existing evidence retained separately. Shared Scene integration checked on fresh binary; no claim old tested product identity applies to new product. Unchanged glad reused by identity; Engine and normal/probe RBS rebuilt.",
            "stop": "Any failure or input drift. Preserve every attempt; only demonstrated in-scope infrastructure corrections may continue.",
            "human": "Final owner Rebuild Solution and inspection of human-built RBS before SEAL."
        }
    if VALIDATION == "scenes":
        manifest["protocol"] = {
            "build_seconds_each_configuration": 1800, "runtime_seconds_each": 180,
            "processes_each_configuration": 6, "repeats": 0, "simulation_steps": 0, "frames_each": 240,
            "cases": ["GeometryGallery", "SphereDiamond", "SphereLattice", "BoxStack", "SphereBoxStack"],
            "runtime_cases": ["GeometryGallery", "SphereDiamond", "SphereLattice", "BoxStack", "SphereBoxStack", "GeometryGalleryRestore"],
            "dynamic_bodies": [0, 2, 180, 16, 17], "common_static_bodies": 5,
            "geometry_gallery": [15, 0, 0, 0, 0],
            "pass": "Both affected RBS builds; 127 typed configuration assertions; complete closed entity name/category inventory and retained ECS identities across 240 UI/resource frames; exact initial fixture/body properties; no-target Material/Shader/Async UI text; dock/undock/redock/resize all panels in all presets; saved mixed-layout restoration; viewport/actions/shutdown; poisoned process configuration ignored.",
            "fixture": "Exact Phase13 bodies, owner-directed complete scene exclusivity. Normal camera/light/quality/assets/window settings; GeometryGallery uses main.cpp default. Diagnostic pauses Physics after construction; Physics cases request startup Barrel to prove demo import is unavailable. All 26 former environment keys poisoned. UI/menu opening and layout controls are test-only.",
            "stop": "Any failure or input drift. Preserve every attempt; execution-skill correction boundaries remain binding.",
            "metrics": "Construction and functional UI correctness only; no solver stability, performance or visual acceptance claim",
            "reuse": "Fresh docking evidence because no-target UI changed; unchanged Engine/glad and historical provider evidence only"
        }
    correction = OUT.parent / f"correction-{CANDIDATE}.json"
    if correction.exists():
        record = json.loads(correction.read_text())
        manifest["correction"] = {"path": correction.relative_to(ROOT).as_posix(),
                                  "identity": identity(correction)}
        manifest["engine_reuse"] = record.get("engine_reuse", {})
        manifest["normal_reuse"] = record.get("normal_reuse", {})
        manifest["prior_build_seconds"] = record.get("prior_build_seconds", {})
    save(OUT / "scenario-manifest.json", manifest)
    static()
    verify(manifest)
    print(f"Frozen Packet {PACKET} candidate {CANDIDATE}: {len(names)} repository inputs", flush=True)


def verify(manifest):
    guards()
    for path, expected in manifest["input_hashes"].items():
        assert digest(ROOT / path) == expected, "Changed relevant input: " + path
    for path, expected in manifest["external_inputs"].items():
        assert identity(path) == expected, "Changed toolchain input: " + path
    assert compile_members() == manifest["compile_members"]
    assert discovery() == manifest["compile_discovery"]
    assert all(not (ROOT / p).exists() for p in manifest["include_preceding_absent"])


def build(cfg):
    manifest = json.loads((OUT / "scenario-manifest.json").read_text())
    verify(manifest)
    folder = local(OUT / ("build-" + cfg))
    folder.mkdir()
    common = [f"/p:Configuration={cfg}", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
              "/p:WindowsTargetPlatformVersion=10.0.26100.0",
              "/p:ForceImportBeforeCppTargets=" + str(OUT / "isolated.props")]
    reuse = manifest["glad_reuse"][cfg]
    target = local(f"bin/{cfg}/Phase14Candidate{CANDIDATE}/glad/glad.lib")
    target.parent.mkdir(parents=True, exist_ok=False)
    shutil.copy2(ROOT / reuse["source"], target)
    commands = []
    report = {"result": "FAIL", "recorded_utc": utc(), "commands": commands, "exits": []}
    started = time.monotonic()
    prior_seconds = manifest.get("prior_build_seconds", {}).get(cfg, 0)
    report["prior_seconds"] = prior_seconds
    try:
        engine = manifest.get("engine_reuse", {}).get(cfg)
        if engine:
            assert engine["exit"] == 0
            assert identity(ROOT / engine["source"]) == engine["identity"]
            assert identity(ROOT / engine["build_evidence"]) == engine["build_evidence_identity"]
            assert all(digest(ROOT / p) == value for p, value in engine["input_hashes"].items())
            library = local(f"bin/{cfg}/Phase14Candidate{CANDIDATE}/GEngine/GEngine.lib")
            library.parent.mkdir(parents=True, exist_ok=False)
            shutil.copy2(ROOT / engine["source"], library)
            report["reused_engine"] = engine["source"]
        normal = manifest.get("normal_reuse", {}).get(cfg)
        if normal:
            assert normal["exit"] == 0
            assert identity(ROOT / normal["source"]) == normal["identity"]
            assert identity(ROOT / normal["build_evidence"]) == normal["build_evidence_identity"]
            assert all(digest(ROOT / p) == value for p, value in normal["input_hashes"].items())
            assert identity(ROOT / normal["validation_source"]) == normal["validation_source_identity"]
            marker = b"#ifdef GENGINE_RBS_MODULARITY_VALIDATION\nnamespace"
            previous = (ROOT / normal["validation_source"]).read_bytes()
            current = (ROOT / "RigidBodySimulation/src/RbsValidation.cpp").read_bytes()
            assert marker in previous and marker in current
            assert previous.split(marker)[0] == current.split(marker)[0]
            for source, preserved in normal.get("diagnostic_only_sources", {}).items():
                assert identity(ROOT / preserved["snapshot"]) == preserved["identity"]
                def normal_text(path):
                    text = path.read_text()
                    def exclude(match):
                        assert not re.search(r"^\s*#", match[1], re.M), "Nested diagnostic directive"
                        return ""
                    return re.sub(r"^#ifdef GENGINE_RBS_SCENE_VALIDATION\n(.*?)^#endif\n",
                                  exclude, text, flags=re.M | re.S)
                assert normal_text(ROOT / source) == normal_text(ROOT / preserved["snapshot"]), source
            target = local(f"bin/{cfg}/Phase14Candidate{CANDIDATE}/RigidBodySimulation/RigidBodySimulation.exe")
            target.parent.mkdir(parents=True, exist_ok=False)
            shutil.copy2(ROOT / normal["source"], target)
            report["reused_normal"] = normal["source"]
        projects = ([] if engine else [(PROJECTS[0], False)])
        projects += ([] if normal else [(PROJECTS[2], False)]) + [(PROJECTS[2], True)]
        for project, probe in projects:
            label = Path(project).stem + ("-" + manifest.get("validation_kind", "docking") if probe else "")
            args = [str(MSBUILD), project, *common, "/p:PacketAProbe=" + str(probe).lower()]
            evaluated = subprocess.run(args + ["/getProperty:OutDir,IntDir,TargetPath,PostBuildEventUseInBuild", "/nologo"],
                                       cwd=ROOT, env=environment(), capture_output=True)
            save(folder / (label + "-evaluation.json"), {"command": args, "exit": evaluated.returncode,
                 "stdout": evaluated.stdout.decode(errors="replace"), "stderr": evaluated.stderr.decode(errors="replace")})
            assert evaluated.returncode == 0
            properties = json.loads(evaluated.stdout)["Properties"]
            for key in ("OutDir", "IntDir", "TargetPath"):
                assert f"Phase14Candidate{CANDIDATE}" in str(local(properties[key]))
            assert properties["PostBuildEventUseInBuild"] == "false"
            command = args + ["/t:Build", "/p:BuildProjectReferences=false", "/m:2", "/nr:false",
                              "/nologo", "/v:normal", "/bl:" + str(folder / (label + ".binlog"))]
            commands.append(command)
            with (folder / (label + ".log")).open("xb") as stream:
                result = subprocess.run(command, cwd=ROOT, env=environment(), stdout=stream,
                    stderr=subprocess.STDOUT, timeout=max(1, 1800 - prior_seconds - (time.monotonic() - started)),
                    creationflags=subprocess.CREATE_NO_WINDOW)
            report["exits"].append(result.returncode)
            assert result.returncode == 0, label + " failed"
        verify(manifest)
        products = [f"bin/{cfg}/Phase14Candidate{CANDIDATE}/" + p for p in
            ["glad/glad.lib", "GEngine/GEngine.lib", "RigidBodySimulation/RigidBodySimulation.exe",
             manifest.get("probe_directory", "Docking") + "/RigidBodySimulation/RigidBodySimulation.exe"]]
        report.update(result="PASS", products={p: identity(ROOT / p) for p in products})
    except Exception as error:
        report["reason"] = str(error)
    report["seconds"] = time.monotonic() - started
    save(folder / "result.json", report)
    print(json.dumps(report), flush=True)
    if report["result"] != "PASS":
        raise SystemExit(1)


def stage(cfg):
    import postbuild
    manifest = json.loads((OUT / "scenario-manifest.json").read_text())
    verify(manifest)
    built = json.loads((OUT / f"build-{cfg}/result.json").read_text())
    assert built["result"] == "PASS"
    base = local(f"bin/{cfg}/Phase14Candidate{CANDIDATE}")
    assert not (base / "assets").exists()
    postbuild.stage_runtime_assets(ROOT, base / "assets")
    for variant in ("RigidBodySimulation", manifest.get("probe_directory", "Docking") + "/RigidBodySimulation"):
        destination = base / variant
        for old in manifest["expected_modules"][cfg].values():
            source = Path(old["path"])
            assert digest(source) == old["sha256"]
            target = local(destination / source.name)
            assert not target.exists()
            shutil.copy2(source, target)
    for asset, source in manifest["asset_members"].items():
        assert digest(base / "assets" / asset) == digest(ROOT / source)
    save(OUT / f"stage-{cfg}.json", {"recorded_utc": utc(), "result": "PASS",
         "bundle": {p.relative_to(ROOT).as_posix(): identity(p)
                    for p in base.rglob("*") if p.is_file() and p.suffix != ".pdb"}})


def run(cfg):
    manifest = json.loads((OUT / "scenario-manifest.json").read_text())
    if manifest.get("validation_kind") == "scenes":
        return run_scenes(cfg, manifest)
    verify(manifest)
    for result in OUT.glob("**/result.json"):
        assert json.loads(result.read_text())["result"] == "PASS", "Preserved failure blocks further validation"
    staged = json.loads((OUT / f"stage-{cfg}.json").read_text())
    for path, expected in staged["bundle"].items():
        assert identity(ROOT / path) == expected
    folder = local(OUT / f"runtime-{cfg}")
    folder.mkdir()
    probe_directory = manifest.get("probe_directory", "Docking")
    executable = local(f"bin/{cfg}/Phase14Candidate{CANDIDATE}/{probe_directory}/RigidBodySimulation/RigidBodySimulation.exe")
    env = environment()
    key = next((k for k in env if k.lower() == "path"), "PATH")
    env[key] = str(ROOT / "external/tbb/bin") + os.pathsep + env.get(key, "")
    deadline = manifest["protocol"].get("runtime_seconds_by_configuration", {}).get(
        cfg, manifest["protocol"].get("runtime_seconds_each", 180))
    for ordinal in ((1,) if manifest.get("validation_kind") == "transform" else (1, 2)):
        directory = local(f"runtime/RigidBodySimulation/{cfg}/phase14-packet-{PACKET.lower()}-{CANDIDATE}-{ordinal}")
        directory.mkdir(parents=True, exist_ok=False)
        if ordinal == 1:
            (directory / "imgui.ini").write_text(manifest["layout"], encoding="utf-8")
            expected = []
            for name in ("Viewport", "Material authoring", "Custom shader reload"):
                section = re.search(r"\[Window\]\[" + re.escape(name) +
                                    r"\]\s+Pos=(\d+),(\d+)\s+Size=(\d+),(\d+)", manifest["layout"])
                assert section, "Missing compatible layout entry: " + name
                expected.append("0 " + " ".join(section.groups()))
            (directory / "packet-a-restore.expected").write_text("\n".join(expected) + "\n", encoding="utf-8")
        else:
            previous = local(f"runtime/RigidBodySimulation/{cfg}/phase14-packet-a-{CANDIDATE}-1")
            shutil.copy2(previous / "imgui.ini", directory / "imgui.ini")
            shutil.copy2(previous / "packet-a-layout.expected", directory / "packet-a-restore.expected")
        attempt = folder / str(ordinal)
        attempt.mkdir()
        report = {"result": "FAIL", "recorded_utc": utc(), "command": [str(executable)],
                  "cwd": str(directory), "binary": identity(executable), "limit_seconds": deadline}
        save(attempt / "admission.json", report)
        started = time.monotonic()
        try:
            with (directory / "application.log").open("xb") as stream:
                report["exit"], report["window"] = observe_process(
                    executable, directory, env, stream, limit_seconds=deadline)
            assert report["exit"] == 0, "RBS did not exit successfully"
            assert report["window"].get("minimized") and report["window"].get("restored"), "Minimize/restore was not observed"
            assert report["window"].get("dpi", 0) > 0, "No live-window DPI observation"
            text = "\n".join(p.read_text(errors="replace") for p in
                             [directory / "application.log", directory / "GEngine.log"] if p.exists())
            markers = ["PRE_EDITOR_PACKET_A_CONFIG_PASS", "PRE_EDITOR_PACKET_A_LAYOUT_RESTORE_PASS",
                       "PRE_EDITOR_PACKET_A_VIEWPORT_PASS", "PRE_EDITOR_PACKET_A_UI_PASS",
                       "PRE_EDITOR_PACKET_A_SHUTDOWN_PASS", "PRE_EDITOR_PACKET_A_ACTIONS_PASS"]
            markers += ["PRE_EDITOR_PACKET_A_DOCK_PASS panel=" + name
                        for name in ("Viewport", "Material authoring", "Custom shader reload")]
            if manifest.get("validation_kind") == "transform":
                markers += ["PRE_EDITOR_PACKET_B_" + name + "_PASS" for name in
                            ("NOTIFICATION", "HIERARCHY", "COLLIDER_POLICY", "INTERPOLATION", "SCALE_LIFETIME")]
                assert "PRE_EDITOR_PACKET_B_FAIL" not in text
            assert all(marker in text for marker in markers), "Missing Packet A acceptance marker"
            assert "PRE_EDITOR_PACKET_A_FAIL" not in text
            assert not re.search(r"GL_INVALID|GL_OUT_OF_MEMORY|context thread violation", text, re.I)
            report.update(result="PASS", markers=markers)
            verify(manifest)
        except Exception as error:
            report["reason"] = str(error)
        finally:
            report["seconds"] = time.monotonic() - started
            for path in directory.iterdir():
                if path.is_file():
                    shutil.copy2(path, attempt / path.name)
            save(attempt / "result.json", report)
        print(json.dumps(report), flush=True)
        if report["result"] != "PASS":
            raise SystemExit(1)


def run_scenes(cfg, manifest):
    verify(manifest)
    for result in OUT.glob("**/result.json"):
        assert json.loads(result.read_text())["result"] == "PASS", "Preserved failure blocks validation"
    staged = json.loads((OUT / f"stage-{cfg}.json").read_text())
    for path, expected in staged["bundle"].items():
        assert identity(ROOT / path) == expected
    folder = local(OUT / f"runtime-{cfg}")
    folder.mkdir()
    executable = local(f"bin/{cfg}/Phase14Candidate{CANDIDATE}/Scenes/RigidBodySimulation/RigidBodySimulation.exe")
    env = environment()
    # Negative transport check: none of these legacy selectors may affect RBS.
    for name in LEGACY_RBS_ENVIRONMENT_KEYS:
        env[name] = "INVALID_PROCESS_CONFIG_MUST_BE_IGNORED"
    key = next((k for k in env if k.lower() == "path"), "PATH")
    env[key] = str(ROOT / "external/tbb/bin") + os.pathsep + env.get(key, "")
    for case in manifest["protocol"]["runtime_cases"]:
        preset = "GeometryGallery" if case == "GeometryGalleryRestore" else case
        ordinal = manifest["protocol"]["cases"].index(preset)
        count = manifest["protocol"]["dynamic_bodies"][ordinal]
        directory = local(f"runtime/RigidBodySimulation/{cfg}/phase14-scenes-{CANDIDATE}-{case}")
        directory.mkdir(parents=True, exist_ok=False)
        if case == "GeometryGalleryRestore":
            previous = local(f"runtime/RigidBodySimulation/{cfg}/phase14-scenes-{CANDIDATE}-GeometryGallery")
            shutil.copy2(previous / "imgui.ini", directory / "imgui.ini")
            shutil.copy2(previous / "packet-a-layout.expected", directory / "packet-a-restore.expected")
        else:
            (directory / "imgui.ini").write_text(manifest["layout"], encoding="utf-8")
            expected = []
            for name in ("Viewport", "Material authoring", "Custom shader reload"):
                section = re.search(r"\[Window\]\[" + re.escape(name) +
                                    r"\]\s+Pos=(\d+),(\d+)\s+Size=(\d+),(\d+)", manifest["layout"])
                assert section, "Missing compatible layout entry: " + name
                expected.append("0 " + " ".join(section.groups()))
            (directory / "packet-a-restore.expected").write_text("\n".join(expected) + "\n", encoding="utf-8")
        attempt = folder / case
        attempt.mkdir()
        command = [str(executable)] + ([] if preset == "GeometryGallery" else [preset])
        report = {"result": "FAIL", "recorded_utc": utc(), "command": command,
                  "cwd": str(directory), "binary": identity(executable), "limit_seconds": 180,
                  "preset": preset, "case": case, "simulation_steps": 0, "frames": 240,
                  "poisoned_keys": LEGACY_RBS_ENVIRONMENT_KEYS}
        save(attempt / "admission.json", report)
        started = time.monotonic()
        try:
            with (directory / "application.log").open("xb") as stream:
                report["exit"], report["window"] = observe_process(executable, directory, env, stream, command[1:])
            assert report["exit"] == 0, "RBS construction/UI/teardown failed"
            assert report["window"].get("minimized") and report["window"].get("restored"), "Minimize/restore not observed"
            assert report["window"].get("dpi", 0) > 0, "No live-window DPI observation"
            text = "\n".join(p.read_text(errors="replace") for p in
                             [directory / "application.log", directory / "GEngine.log"] if p.exists())
            gallery = manifest["protocol"]["geometry_gallery"][ordinal]
            markers = ["PRE_EDITOR_PACKET_A_CONFIG_PASS checks=127",
                       f"PRE_EDITOR_PACKET_A_SCENE_PASS preset={ordinal} dynamic={count} static=5 gallery={gallery} before_step=true",
                       f"PRE_EDITOR_PACKET_A_MEMBERSHIP_PASS preset={ordinal} identities=true categories=true retained_frames=240 physics_paused=true",
                       "PRE_EDITOR_PACKET_A_LAYOUT_RESTORE_PASS", "PRE_EDITOR_PACKET_A_VIEWPORT_PASS",
                       "PRE_EDITOR_PACKET_A_UI_PASS", "PRE_EDITOR_PACKET_A_SHUTDOWN_PASS",
                       "PRE_EDITOR_PACKET_A_ACTIONS_PASS"]
            markers += ["PRE_EDITOR_PACKET_A_DOCK_PASS panel=" + name
                        for name in ("Viewport", "Material authoring", "Custom shader reload")]
            if preset != "GeometryGallery":
                markers.append("PRE_EDITOR_PACKET_A_NO_TARGET_PASS panels=3")
            assert not re.search(r"PRE_EDITOR_PHASE_(?:0[4-9]|1[0-3])_", text), "Process environment activated historical validation"
            assert all(marker in text for marker in markers), "Missing construction acceptance marker"
            assert not re.search(r"GL_INVALID|GL_OUT_OF_MEMORY|context thread violation", text, re.I)
            verify(manifest)
            report.update(result="PASS", markers=markers)
        except Exception as error:
            report["reason"] = str(error)
        finally:
            report["seconds"] = time.monotonic() - started
            for path in directory.iterdir():
                if path.is_file():
                    shutil.copy2(path, attempt / path.name)
            save(attempt / "result.json", report)
        print(json.dumps(report), flush=True)
        if report["result"] != "PASS":
            raise SystemExit(1)


def observe_process(executable, directory, env, stream, arguments=(), *, limit_seconds=180):
    import ctypes
    from ctypes import wintypes
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = user.IsIconic.argtypes = [wintypes.HWND]
    user.ShowWindowAsync.argtypes = [wintypes.HWND, ctypes.c_int]
    user.GetDpiForWindow.argtypes = [wintypes.HWND]
    windows = []
    child = subprocess.Popen([str(executable), *arguments], cwd=directory, env=env, stdout=stream,
                             stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    @callback
    def find(hwnd, _):
        pid = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        name = ctypes.create_unicode_buffer(256)
        user.GetClassNameW(hwnd, name, 256)
        if pid.value == child.pid and name.value == "SDL_app" and user.IsWindowVisible(hwnd):
            windows.append(hwnd)
        return True
    started = time.monotonic()
    minimized_at = None
    observation = {"pid": child.pid}
    try:
        while child.poll() is None:
            assert time.monotonic() - started < limit_seconds, "Runtime deadline exceeded"
            if not windows:
                user.EnumWindows(find, 0)
            if windows:
                hwnd = windows[0]
                dpi = user.GetDpiForWindow(hwnd)
                if dpi:
                    observation["dpi"] = dpi
                log = directory / "GEngine.log"
                text = log.read_text(errors="replace") if log.exists() else ""
                if minimized_at is None and "PRE_EDITOR_PACKET_A_DOCK_PASS" in text:
                    user.ShowWindowAsync(hwnd, 6)
                    minimized_at = time.monotonic()
                elif minimized_at is not None and not observation.get("restored"):
                    if user.IsIconic(hwnd):
                        observation["minimized"] = True
                    if time.monotonic() - minimized_at >= .5:
                        user.ShowWindowAsync(hwnd, 9)
                        if observation.get("minimized") and not user.IsIconic(hwnd):
                            observation["restored"] = True
            time.sleep(.02)
        return child.returncode, observation
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("step", choices=["prepare", "build", "stage", "run", "verify"])
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--configuration", choices=["Debug", "Release"])
    parser.add_argument("--validation", choices=["docking", "scenes", "transform"], default="docking")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]{2}", args.candidate):
        parser.error("Use a two-digit candidate identity")
    CANDIDATE = args.candidate
    VALIDATION = args.validation
    PACKET = "B" if VALIDATION == "transform" else "A"
    if PACKET == "B":
        ENTRY = ROOT / "logs/pre_editor/phase-14/packet-b/entry-01/entry.json"
    OUT = local(f"logs/pre_editor/phase-14/packet-{PACKET.lower()}/candidate-{CANDIDATE}")
    if args.step == "prepare":
        prepare()
    elif args.step == "verify":
        verify(json.loads((OUT / "scenario-manifest.json").read_text()))
        print("Packet " + PACKET + " inputs, membership, index and scope match", flush=True)
    elif args.configuration:
        globals()[args.step](args.configuration)
    else:
        parser.error("Configuration required")
