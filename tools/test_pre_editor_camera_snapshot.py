"""Phase 16 bounded RBS camera validation; create-only attempts, exact input guards."""
import argparse
import ast
import csv
import ctypes
from ctypes import wintypes
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import test_pre_editor_rbs_modularity as closure

ROOT = closure.ROOT
BASE = ROOT / "logs/pre_editor/phase-16"
ACCEPTANCE = ROOT / "docs/pre_editor/active/PHASE_16_OD05_SCOPE_ACCEPTANCE.json"
AMENDMENT = ROOT / "docs/pre_editor/active/PHASE_16_BACKGROUND_SAMPLE_AMENDMENT_01_ACCEPTANCE.json"
REVISION = BASE / "revision-01/entry.json"
SCOPE_AMENDMENT = ROOT / "docs/pre_editor/active/PHASE_16_REVISION_01_SCOPE_AMENDMENT_ACCEPTANCE.json"
CURSOR_AMENDMENT = ROOT / "docs/pre_editor/active/PHASE_16_REVISION_02_SCOPE_AMENDMENT_ACCEPTANCE.json"
PRIOR = ROOT / "logs/pre_editor/phase-15/candidate-24/scenario-manifest.json"
local, digest, identity, save, utc = closure.local, closure.digest, closure.identity, closure.save, closure.utc
PROJECTS, MSBUILD, environment = closure.PROJECTS, closure.MSBUILD, closure.environment


def diagnose():
    """Scalar reference diagnosis, separate from the native acceptance campaign."""
    out = local(BASE / "matrix-diagnosis-01")
    admission = json.loads((out / "admission.json").read_text())
    for name, expected in admission["inputs"].items():
        if name != "tools/test_pre_editor_camera_snapshot.py":
            assert identity(ROOT / name) == expected, name
    def f32(x):
        return struct.unpack("f", struct.pack("f", x))[0]
    def multiply(a, b):
        return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
    def transform(a, v):
        return [sum(a[i][j]*v[j] for j in range(4)) for i in range(4)]
    cases = [("P1",False,45,.1,1000,1280,720,False),
        ("P2",False,60,.5,100,1000,750,True),("P3",False,30,.01,100,3840,2160,False),
        ("O1",True,10,.1,1000,1280,720,False),("O2",True,20,-.5,100,1200,900,True),
        ("O3",True,4,1,20,800,720,True),("F1",False,45,.1,1000,1600,900,True)]
    records = []
    for name, ortho, field, n, f, pw, ph, pose_b in cases:
        yaw, pitch = (math.pi/6,-math.pi/12) if pose_b else (0,0)
        cy,sy,cx,sx = math.cos(yaw),math.sin(yaw),math.cos(pitch),math.sin(pitch)
        rotation = [[cy,sy*sx,sy*cx],[0,cx,-sx],[-sy,cy*sx,cy*cx]]
        position = [3,2,10] if pose_b else [0,0,10]
        view = [[rotation[j][i] for j in range(3)]+[-sum(rotation[j][i]*position[j] for j in range(3))] for i in range(3)]+[[0,0,0,1]]
        view = [[f32(x) for x in row] for row in view]
        reference = [[0.0]*4 for unused in range(4)]
        if ortho:
            reference[0][0]=2/(field*pw/ph); reference[1][1]=2/field
            reference[2][2]=-2/(f-n); reference[2][3]=-(f+n)/(f-n);reference[3][3]=1
        else:
            reference[0][0]=1/(pw/ph*math.tan(math.radians(field)/2)); reference[1][1]=1/math.tan(math.radians(field)/2)
            reference[2][2]=-(f+n)/(f-n); reference[2][3]=-2*f*n/(f-n); reference[3][2]=-1
        record = {"case":name,"projection":"orthographic" if ortho else "perspective","near":n,"far":f,"lanes":{}}
        for lane in ("independently_rounded", "far_endpoint_compensated"):
            projection = [[f32(x) for x in row] for row in reference]
            if not ortho and lane=="far_endpoint_compensated":
                projection[2][3]=f32((projection[2][2]+1)*f)
            clip=multiply(projection,view)
            planes=[]
            for axis in range(3):
                for side in (1,-1):
                    plane=[clip[3][k]+side*clip[axis][k] for k in range(4)]
                    length=math.sqrt(sum(x*x for x in plane[:3]))
                    planes.append([x/length for x in plane])
            lane_result={"projection_row_major":projection,"coefficient_gate":all(abs(projection[i][j]-reference[i][j])<=1e-5+2e-5*abs(reference[i][j]) for i in range(4) for j in range(4)),"boundaries":[],"clip_plane_disagreements":0}
            samples=[(x,y,z) for x in (-1,1) for y in (-1,1) for z in (n,f)]
            samples += [(x,y,z) for x,y in ((0,0),(-.8,.8),(1.01,0),(0,1.01)) for z in (n+(f-n)*.1,(n+f)/2,f+f*.01)]
            for index,(x,y,depth) in enumerate(samples):
                half=field/2 if ortho else depth*math.tan(math.radians(field)/2)
                local_position=[x*half*pw/ph,y*half,-depth]
                world=[position[i]+sum(rotation[i][j]*local_position[j] for j in range(3)) for i in range(3)]+[1]
                homogeneous=transform(clip,world)
                distances=[sum(plane[i]*world[i] for i in range(4)) for plane in planes]
                tolerances=[32*2**-23*(1+abs(plane[3])+sum(abs(plane[i]*world[i]) for i in range(3))) for plane in planes]
                # Same six inequalities without plane normalization: valid for either projection.
                inequalities=[homogeneous[3]+sign*homogeneous[axis] for axis in range(3) for sign in (1,-1)]
                if any(abs(v)>1e-8 and abs(d)>1e-8 and (v<0)!=(d<0) for v,d in zip(inequalities,distances)):
                    lane_result["clip_plane_disagreements"]+=1
                if index<8:
                    lane_result["boundaries"].append({"ndc_xy":[x,y],"view_depth":depth,"world":world[:3],"far_plane_distance":distances[5],"far_plane_tolerance":tolerances[5],"outside":any(d < -t for d,t in zip(distances,tolerances))})
            lane_result["rejected_boundaries"]=sum(v["outside"] for v in lane_result["boundaries"])
            record["lanes"][lane]=lane_result
        records.append(record)
    result={"recorded_utc":utc(),"kind":"SCALAR_DIAGNOSIS_NOT_NATIVE_ACCEPTANCE","cases":records,
        "extraction":"row3 +/- row0/1/2 implements -w <= x,y,z <= w independently of perspective or orthographic projection. No snapshot presence participates in this function.",
        "cache":"SameCamera compares the exact float view/projection matrices and entity/viewport/visibleLayers/worldPosition consumed by visibility. Identical fields produce identical frustum and layer decisions. No demonstrated requirement for snapshot presence or precise hidden VP in this cache.",
        "scope":"No RenderVisibility change. Prospective compensated float projection must pass original native gates before acceptance.",
        "runner":identity(Path(__file__))}
    save(out/"result.json",result)
    for row in records:
        print(row["case"],{lane:{k:v[k] for k in ("coefficient_gate","rejected_boundaries","clip_plane_disagreements")} for lane,v in row["lanes"].items()})


def git(*args):
    return subprocess.check_output(["git", "--no-optional-locks", *args], cwd=ROOT)


def guards():
    entry = json.loads((BASE / "entry-01/entry.json").read_text())
    accepted = json.loads(ACCEPTANCE.read_text())
    owned = accepted["production_files"] + accepted["check_files"]
    amendment = json.loads(SCOPE_AMENDMENT.read_text())
    assert amendment["status"] == "ACCEPTED"
    owned += amendment["added_production_files"]
    cursor_amendment = json.loads(CURSOR_AMENDMENT.read_text())
    assert cursor_amendment["status"] == "ACCEPTED"
    assert cursor_amendment["added_production_files"] == ["GEngine/src/Managers/InputManager.cpp"]
    assert set(cursor_amendment["production_files"]) == set(accepted["production_files"] + amendment["added_production_files"] + cursor_amendment["added_production_files"])
    assert cursor_amendment["production_file_total"] == 11
    owned += cursor_amendment["added_production_files"]
    revision = json.loads(REVISION.read_text())
    owner_inputs = revision["behavior_input_drift"]
    for name, expected in owner_inputs.items():
        assert digest(ROOT / name) == expected["current"], name
    assert git("rev-parse", "HEAD").decode().strip() == entry["baseline"]
    assert git("branch", "--show-current").decode().strip() == "pre-editor/refactor"
    assert digest(entry["index"]["path"]) == entry["index"]["sha256"]
    assert hashlib.sha256(git("ls-files", "--stage", "-v")).hexdigest() == entry["index"]["logical_stage_flags_sha256"]
    changed = set(git("diff", "--name-only", "HEAD").decode().splitlines())
    untracked = set(git("ls-files", "--others", "--exclude-standard").decode().splitlines())
    generated = json.loads((BASE / "revision-01/known-generated-untracked-01-through-05.json").read_text())["files"]
    for name in untracked & generated.keys():
        assert identity(ROOT / name) == {k:generated[name][k] for k in ("sha256", "size")}, name
    changed.update(untracked - generated.keys())
    assert changed <= set(owned) | set(owner_inputs), sorted(changed - set(owned) - set(owner_inputs))
    for name, expected in entry["protected_files"].items():
        assert identity(ROOT / name) == expected, name
    assert identity(ROOT / entry["canonical_rbs"]["path"]) == entry["canonical_rbs"]["working"]
    return entry, owned


def prepare(out, candidate, reuse_engine=None, reuse_build=None):
    entry, owned = guards()
    prior = json.loads(PRIOR.read_text())
    out.mkdir(parents=True, exist_ok=False)
    for cfg in ("Debug", "Release"):
        for base in ("bin", "bin-int"):
            # Earlier revision observers have separately isolated children here.
            # The actual candidate project outputs must still be create-only.
            for project in ("glad", "GEngine", "RigidBodySimulation"):
                assert not local(f"{base}/{cfg}/Phase16Candidate{candidate}/{project}").exists()
    props = out / "isolated.props"
    # Retain the owner's scene selection in the ordinary application. The original
    # camera acceptance workload remains the exact historical GeometryGallery.
    source = (ROOT / "RigidBodySimulation/src/main.cpp").read_text()
    before = "constexpr auto scenePreset = Rbs::RbsScenePreset::SphereBoxStack;"
    assert source.count(before) == 1
    fixture_main = out / "main-camera-fixture.cpp"
    fixture_main.write_text(source.replace(before, "constexpr auto scenePreset = Rbs::RbsScenePreset::GeometryGallery;"))
    assert fixture_main.read_text() == (BASE / "revision-01/main.index.cpp").read_text()
    props.write_text('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        '<PropertyGroup>'
        f'<OutDir>{ROOT.as_posix()}/bin/$(Configuration)/Phase16Candidate{candidate}/$(MSBuildProjectName)/</OutDir>'
        f'<IntDir>{ROOT.as_posix()}/bin-int/$(Configuration)/Phase16Candidate{candidate}/$(MSBuildProjectName)/$(CameraLane)/</IntDir>'
        '<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>\n'
        '<ItemDefinitionGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\' and \'$(CameraLane)\'==\'diagnostic\'">'
        '<ClCompile><PreprocessorDefinitions>GENGINE_CAMERA_VALIDATION;%(PreprocessorDefinitions)</PreprocessorDefinitions>'
        f'<AdditionalIncludeDirectories>{ROOT.as_posix()}/external/sdl2/include;{ROOT.as_posix()}/external/glad/include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>'
        '</ClCompile></ItemDefinitionGroup>\n'
        '<ItemGroup Condition="\'$(MSBuildProjectName)\'==\'RigidBodySimulation\' and \'$(CameraLane)\'==\'diagnostic\'">'
        '<ClCompile Remove="src\\main.cpp"/>'
        f'<ClCompile Include="{fixture_main.as_posix()}"/>'
        '</ItemGroup>\n</Project>\n', encoding="utf-8")
    names = set(PROJECTS + owned + ["premake5.lua", "external/glad/premake5.lua", "tools/postbuild.py",
        "tools/runtime_assets.json", "tools/test_pre_editor_rbs_modularity.py", "tools/test_pre_editor_input_routing.py",
        "CODING_STYLE.md", ".clang-format", "docs/pre_editor/phases/PHASE_16.md",
        "docs/pre_editor/VALIDATION_CONTRACTS.md", "docs/pre_editor/active/OUTPUT_PATHS.json",
        "docs/pre_editor/active/PHASE_16_OD05_SCOPE_PROPOSAL.md", ACCEPTANCE.relative_to(ROOT).as_posix(),
        props.relative_to(ROOT).as_posix()])
    names.update([SCOPE_AMENDMENT.relative_to(ROOT).as_posix(), CURSOR_AMENDMENT.relative_to(ROOT).as_posix(), REVISION.relative_to(ROOT).as_posix(),
        fixture_main.relative_to(ROOT).as_posix(), "RigidBodySimulation/src/main.cpp"])
    names.update(prior["asset_members"].values())
    names.update(p for p in prior["input_hashes"] if p.endswith((".lib", ".dll", ".inl")))
    absent = set()
    closure.include_closure(names, absent)
    names = {p for p in names if (ROOT / p).is_file()}
    absent = {p for p in absent if not (ROOT / p).exists()}
    for folder in ("external/sdl2/include", "external/glad/include"):
        names.update(p.relative_to(ROOT).as_posix() for p in (ROOT / folder).rglob("*.h"))
    for name in owned:
        destination = local(out / "source-snapshot" / name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, destination)
    machine = subprocess.check_output(["powershell", "-NoProfile", "-Command",
        "@{cpu=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors);memory=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;os=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber);gpu=(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion);power=(powercfg /getactivescheme)} | ConvertTo-Json -Depth 4"])
    manifest = {"phase": 16, "candidate": candidate, "recorded_utc": utc(), "baseline": entry["baseline"],
        "owned_files": owned, "input_hashes": {p: digest(ROOT / p) for p in sorted(names)},
        "external_inputs": {p: identity(p) for p in prior["external_inputs"]},
        "compile_members": closure.compile_members(), "compile_discovery": closure.discovery(),
        "include_preceding_absent": sorted(absent), "asset_members": prior["asset_members"],
        "expected_modules": prior["expected_modules"], "layout": prior["layout"],
        "toolchain": prior["toolchain"], "machine": json.loads(machine),
        "protocol": {"fixture": "Unmodified GeometryGallery membership, assets, lights, quality, Physics settings; deterministic, no seed",
            "cpu_cases": "P1 P2 P3 O1 O2 O3 F1 exactly per accepted proposal; qYaw30*qPitchMinus15",
            "pose": {"focal": [0, 7, 6], "distance": 28, "pitch": .12, "yaw": 0},
            "segments": ["perspective 1x", "orthographic height20 1x", "perspective yaw.12 1x",
                "perspective controlled1.25x", "orthographic controlled1.5x", "perspective resize960x640 panel560x580 1x",
                "perspective native restore1280x720 panel880x660 1x"],
            "picking": [{"name": "Phase08 Source", "center": [-6,2,6], "dimensions": [4,6,8], "point": [-6,2,10]},
                {"name": "Phase08 Regenerated", "center": [6,2,6], "dimensions": [4,3,2], "point": [6,2,7]}],
            "occlusion_proof": "RbsScene CreateGeometryExamples fixes front faces at z10/z7. Camera z>33; the wall at z49 is behind camera; other walls at +/-49 bound the room. Gallery instances are z<=6, distinct x ranges for unique bake. Render-only targets are static. Each projected front-face sample must have >=4 pixel interior margin. No scene reduction, object substitution or tolerance fallback.",
            "background": "Owner-accepted Amendment01: fixed pixel(floor(W/4),8). Actual integer framebuffer GPU readback must equal InvalidPixel=-1. CPU ray geometry is only an independent oracle; existing production GPU MousePicker shader/pass unchanged. Scene,other samples,thresholds,workloads and deadlines unchanged. Original failed center/top evidence retained.",
            "bounds": {"stable_frames": 2, "observed_frames": 8, "segments": 7, "runtime_seconds": 60,
                "initial_runtime_pair_seconds": 120, "build_configuration_seconds": 900, "initial_build_pair_seconds": 1800},
            "pacing": "normal VSync and default manual cap; no performance assertion",
            "observer": "CPU CSV at startup; 3 picking readbacks per observed frame; 7 color captures per process; native routed click each segment; external process-owned native minimize/restore observation only",
            "desktop": "Before launch, save/minimize only known mininews overlays from360huabao.exe and restore their placements in finally. RBS temporary topmost/foreground lease, actual focus and point visibility gates unchanged; flags/lease released before minimize and close.",
            "criteria": "Accepted proposal gates unchanged; no favorable-result retry; corrected attempts use new destinations",
            "manual": "Real desktop DPI and canonical human VS Release rebuild/RBS inspection before separate SEAL and APPROVE"},
        "writers": [str(out), f"bin/{{Debug,Release}}/Phase16Candidate{candidate}",
            f"bin-int/{{Debug,Release}}/Phase16Candidate{candidate}",
            f"runtime/RigidBodySimulation/{{Debug,Release}}/phase-16-candidate-{candidate}", "runtime/tmp"],
        "product_roles": "Ordinary RBS archived before diagnostic build; tested binaries/inputs historical immutable; normal owner rebuild products derived"}
    manifest["background_sample_amendment"] = {"path":AMENDMENT.relative_to(ROOT).as_posix(),**identity(AMENDMENT)}
    manifest["revision_scope_amendment"] = {"path":SCOPE_AMENDMENT.relative_to(ROOT).as_posix(),**identity(SCOPE_AMENDMENT)}
    manifest["cursor_scope_amendment"] = {"path":CURSOR_AMENDMENT.relative_to(ROOT).as_posix(),**identity(CURSOR_AMENDMENT)}
    manifest["owner_scene_preservation"] = {"path":"RigidBodySimulation/src/main.cpp",**identity(ROOT/"RigidBodySimulation/src/main.cpp"),
        "ordinary":"Owner SphereBoxStack source unchanged", "diagnostic":"Frozen generated main-camera-fixture.cpp equals the historical index main after the single scene selector substitution; original GeometryGallery workload unchanged"}
    assert not (reuse_engine and reuse_build)
    if reuse_build:
        previous = local(BASE / ("candidate-" + reuse_build))
        original = json.loads((previous / "scenario-manifest.json").read_text())
        def compile_inputs(data):
            return {p:v for p,v in data["input_hashes"].items()
                if p != "tools/test_pre_editor_camera_snapshot.py" and not p.endswith("/isolated.props")}
        assert compile_inputs(original) == compile_inputs(manifest), "Exact compilation inputs required"
        for key in ("external_inputs", "compile_members", "compile_discovery", "include_preceding_absent", "expected_modules", "asset_members"):
            assert original[key] == manifest[key], key
        assert (previous / "isolated.props").read_text().replace("Phase16Candidate"+reuse_build,"Phase16Candidate"+candidate) == props.read_text()
        records = {}
        for cfg in ("Debug", "Release"):
            path = previous / f"build-{cfg}/result.json"
            result = json.loads(path.read_text())
            assert result["result"] == "PASS"
            for path_name, expected in result["products"].items():
                assert identity(path_name) == expected
            ordinary = result["ordinary_product"]
            assert identity(ordinary["path"]) == {k:ordinary[k] for k in ("sha256","size")}
            records[cfg] = {"record":str(path), **identity(path)}
        manifest["reuse_build"] = {"candidate":reuse_build,"manifest":identity(previous/"scenario-manifest.json"),
            "configurations":records,"reason":"Only Python desktop preparation/evidence plumbing changed; exact compiled source, dependencies, tools and normalized options match. CPU evidence retained; inseparable CPU/native session checks still execute."}
    if reuse_engine:
        previous = local(BASE / ("candidate-" + reuse_engine))
        original = json.loads((previous / "scenario-manifest.json").read_text())
        changed_allowed = {"RigidBodySimulation/src/RbsFrame.cpp", "RigidBodySimulation/src/RbsValidation.cpp",
            "RigidBodySimulation/tests/CameraSnapshotChecks.h", "tools/test_pre_editor_camera_snapshot.py"}
        old_inputs = {p:v for p,v in original["input_hashes"].items() if p not in changed_allowed and not p.endswith("/isolated.props")}
        new_inputs = {p:v for p,v in manifest["input_hashes"].items() if p not in changed_allowed and not p.endswith("/isolated.props")}
        assert old_inputs == new_inputs, "Unchanged engine closure required for reuse"
        assert original["external_inputs"] == manifest["external_inputs"]
        assert original["compile_members"] == manifest["compile_members"]
        assert (previous / "isolated.props").read_text().replace("Phase16Candidate"+reuse_engine,"Phase16Candidate"+candidate) == props.read_text()
        reuse = {}
        for cfg in ("Debug","Release"):
            result = previous / f"build-{cfg}/result.json"
            if result.exists() and json.loads(result.read_text())["exits"][:2] == [0,0]:
                reuse[cfg] = {"build":identity(result),"libraries":{p:{"path":str(local(f"bin/{cfg}/Phase16Candidate{reuse_engine}/{p}/{p}.lib")),**identity(local(f"bin/{cfg}/Phase16Candidate{reuse_engine}/{p}/{p}.lib"))} for p in ("glad","GEngine")}}
        assert reuse, "No successfully built unchanged libraries to reuse"
        manifest["reuse_engine"] = {"candidate":reuse_engine,"manifest":identity(previous/"scenario-manifest.json"),"configurations":reuse,
            "reason":"Only admitted RBS consumer/test/runner bytes changed; exact engine/dependency source, include discovery, tools and normalized options match"}
    save(out / "scenario-manifest.json", manifest)
    fingerprint = subprocess.check_output(["python", "-B", ".codex/tools/rendering_checkpoint.py", "--workflow", "pre-editor",
        "fingerprint", "--paths", *owned], cwd=ROOT, env=environment())
    with (out / "checkpoint-fingerprints.json").open("xb") as stream:
        stream.write(fingerprint)
    verify(out)
    print("Frozen",len(names),"inputs",flush=True)


def verify(out):
    guards()
    manifest = json.loads((out / "scenario-manifest.json").read_text())
    amendment = manifest.get("background_sample_amendment")
    if amendment:
        assert identity(ROOT / amendment["path"]) == {k:amendment[k] for k in ("sha256","size")}
    for name, expected in manifest["input_hashes"].items():
        assert digest(ROOT / name) == expected, name
    for name, expected in manifest["external_inputs"].items():
        assert identity(name) == expected, name
    assert manifest["compile_members"] == closure.compile_members()
    assert manifest["compile_discovery"] == closure.discovery()
    assert all(not (ROOT / p).exists() for p in manifest["include_preceding_absent"])
    return manifest


def build(out, cfg):
    manifest = verify(out)
    candidate = manifest["candidate"]
    folder = local(out / ("build-" + cfg))
    folder.mkdir()
    report = {"result": "FAIL", "recorded_utc": utc(), "commands": [], "exits": []}
    common = [f"/p:Configuration={cfg}", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
        "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:ForceImportBeforeCppTargets=" + str(out / "isolated.props")]
    start = time.monotonic()
    try:
        reuse_build = manifest.get("reuse_build",{}).get("configurations",{}).get(cfg)
        if reuse_build:
            previous = Path(reuse_build["record"])
            assert identity(previous) == {k:reuse_build[k] for k in ("sha256","size")}
            original = json.loads(previous.read_text())
            products = {}
            for source, expected in original["products"].items():
                assert identity(source) == expected
                target = local(source.replace("Phase16Candidate"+manifest["reuse_build"]["candidate"],"Phase16Candidate"+candidate))
                target.parent.mkdir(parents=True,exist_ok=False)
                shutil.copyfile(source,target)
                products[str(target)] = identity(target)
            source = original["ordinary_product"]["path"]
            target = local(source.replace("Phase16Candidate"+manifest["reuse_build"]["candidate"],"Phase16Candidate"+candidate))
            target.parent.mkdir()
            shutil.copyfile(source,target)
            report.update(result="PASS",reused_build=reuse_build,exits=original["exits"],
                products=products,ordinary_product={"path":str(target),**identity(target)},seconds=time.monotonic()-start)
            verify(out)
            save(folder/"result.json",report)
            print(json.dumps(report),flush=True)
            return
        projects = [(PROJECTS[1],"ordinary"),(PROJECTS[0],"ordinary"),(PROJECTS[2],"ordinary"),(PROJECTS[2],"diagnostic")]
        reuse = manifest.get("reuse_engine",{}).get("configurations",{}).get(cfg)
        if reuse:
            for name,data in reuse["libraries"].items():
                source=Path(data["path"])
                assert identity(source)=={k:data[k] for k in ("sha256","size")}
                target=local(f"bin/{cfg}/Phase16Candidate{candidate}/{name}/{name}.lib")
                target.parent.mkdir(parents=True,exist_ok=False)
                shutil.copyfile(source,target)
            report["reused_engine"]=reuse
            report["exits"]=[0,0]
            projects=projects[2:]
        for project, lane in projects:
            stem = Path(project).stem + "-" + lane
            args = [str(MSBUILD), project, *common, "/p:CameraLane="+lane]
            evaluated = subprocess.run(args + ["/getProperty:OutDir,IntDir,TargetPath,PostBuildEventUseInBuild,VCToolsVersion,WindowsTargetPlatformVersion", "/nologo"],cwd=ROOT,env=environment(),capture_output=True,timeout=30)
            save(folder / (stem+"-evaluation.json"),{"exit":evaluated.returncode,"stdout":evaluated.stdout.decode(errors="replace"),"stderr":evaluated.stderr.decode(errors="replace")})
            assert evaluated.returncode == 0
            properties = json.loads(evaluated.stdout)["Properties"]
            for key in ("OutDir","IntDir","TargetPath"):
                assert f"Phase16Candidate{candidate}" in str(local(properties[key]))
            assert properties["PostBuildEventUseInBuild"] == "false"
            assert properties["VCToolsVersion"] == "14.44.35207" and properties["WindowsTargetPlatformVersion"] == "10.0.26100.0"
            command = args + ["/t:Build","/p:BuildProjectReferences=false","/m:2","/nr:false","/nologo","/v:normal","/bl:"+str(folder/(stem+".binlog"))]
            report["commands"].append(command)
            with (folder/(stem+".log")).open("xb") as stream:
                result = subprocess.run(command,cwd=ROOT,env=environment(),stdout=stream,stderr=subprocess.STDOUT,
                    timeout=max(1,900-(time.monotonic()-start)),creationflags=subprocess.CREATE_NO_WINDOW)
            report["exits"].append(result.returncode)
            assert result.returncode == 0, stem+" compilation failed"
            if project == PROJECTS[2] and lane == "ordinary":
                target = local(properties["TargetPath"])
                ordinary = target.parent / "ordinary" / target.name
                ordinary.parent.mkdir()
                shutil.copyfile(target,ordinary)
                report["ordinary_product"] = {"path":str(ordinary),**identity(ordinary)}
        verify(out)
        report.update(result="PASS",products={str(p):identity(p) for p in [local(f"bin/{cfg}/Phase16Candidate{candidate}/{p}/{p}.{ext}") for p,ext in [("glad","lib"),("GEngine","lib"),("RigidBodySimulation","exe")]]})
    except Exception as error:
        report["reason"] = str(error)
    report["seconds"] = time.monotonic()-start
    save(folder/"result.json",report)
    print(json.dumps(report),flush=True)
    if report["result"]!="PASS": raise SystemExit(1)


def stage(out,cfg):
    import postbuild
    manifest=verify(out)
    assert json.loads((out/f"build-{cfg}/result.json").read_text())["result"]=="PASS"
    target=local(f"bin/{cfg}/Phase16Candidate{manifest['candidate']}")
    assert not (target/"assets").exists()
    postbuild.stage_runtime_assets(ROOT,target/"assets")
    for data in manifest["expected_modules"][cfg].values():
        source=Path(data["path"])
        assert digest(source)==data["sha256"]
        destination=local(target/"RigidBodySimulation"/source.name)
        assert not destination.exists()
        shutil.copyfile(source,destination)
    for asset,source in manifest["asset_members"].items():
        assert digest(target/"assets"/asset)==digest(ROOT/source)
    save(out/f"stage-{cfg}.json",{"result":"PASS","recorded_utc":utc(),"products":{str(p):identity(p) for p in target.rglob("*") if p.is_file() and p.suffix!=".pdb"}})


def restore_native(process,directory,report):
    """Observe only the test PID's minimized SDL window, then restore it once."""
    user=ctypes.WinDLL("user32",use_last_error=True)
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.IsIconic.argtypes=[wintypes.HWND]
    user.ShowWindowAsync.argtypes=[wintypes.HWND,ctypes.c_int]
    handles=[]
    @ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    def visit(handle,unused):
        pid=wintypes.DWORD()
        user.GetWindowThreadProcessId(handle,ctypes.byref(pid))
        if pid.value==process.pid and user.IsIconic(handle): handles.append(handle)
        return True
    assert user.EnumWindows(visit,0)
    if handles:
        assert len(handles)==1
        # A bounded interval verifies application frame output does not advance while suspended.
        before=(directory/"camera-frames.csv").read_bytes()
        time.sleep(.25)
        after=(directory/"camera-frames.csv").read_bytes()
        assert before==after,"Application rendered while minimized"
        record={"pid":process.pid,"handle":handles[0],"minimized_observed":True,"frame_rows_stable":True,"recorded_utc":utc()}
        save(directory/"native-restored.json",record)
        user.ShowWindowAsync(handles[0],9)
        report["native_restore"]=record


@contextmanager
def desktop_envelope(directory):
    """Bounded reversible preparation of the known advertising overlays only."""
    user=ctypes.WinDLL("user32",use_last_error=True)
    kernel=ctypes.WinDLL("kernel32",use_last_error=True)
    class Placement(ctypes.Structure):
        _fields_=[("length",wintypes.UINT),("flags",wintypes.UINT),("showCmd",wintypes.UINT),
            ("ptMinPosition",wintypes.POINT),("ptMaxPosition",wintypes.POINT),("rcNormalPosition",wintypes.RECT)]
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
    user.IsWindowVisible.argtypes=user.IsWindow.argtypes=user.IsIconic.argtypes=[wintypes.HWND]
    user.GetWindowPlacement.argtypes=user.SetWindowPlacement.argtypes=[wintypes.HWND,ctypes.POINTER(Placement)]
    user.ShowWindowAsync.argtypes=[wintypes.HWND,ctypes.c_int]
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
    kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes=[wintypes.HANDLE,wintypes.DWORD,wintypes.LPWSTR,ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    def identify(handle):
        pid=wintypes.DWORD()
        user.GetWindowThreadProcessId(handle,ctypes.byref(pid))
        name=ctypes.create_unicode_buffer(256)
        user.GetClassNameW(handle,name,len(name))
        path=ctypes.create_unicode_buffer(32768)
        length=wintypes.DWORD(len(path))
        process=kernel.OpenProcess(0x1000,False,pid.value)
        if process:
            try: kernel.QueryFullProcessImageNameW(process,0,path,ctypes.byref(length))
            finally: kernel.CloseHandle(process)
        return {"pid":pid.value,"class":name.value,"process":path.value}
    targets=[]
    @ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    def visit(handle,unused):
        if user.IsWindowVisible(handle) and not user.IsIconic(handle):
            item=identify(handle)
            if item["class"]=="mininews" and Path(item["process"]).name.lower()=="360huabao.exe":
                placement=Placement()
                placement.length=ctypes.sizeof(placement)
                if user.GetWindowPlacement(handle,ctypes.byref(placement)):
                    targets.append((handle,item,placement))
        return True
    assert user.EnumWindows(visit,0)
    report={"recorded_utc":utc(),"operation":"Minimize known overlays before launch; restore native placement after exit","targets":[],"restored":False}
    touched=[]
    try:
        for handle,item,placement in targets:
            row={"handle":handle,**item,"placement_hex":bytes(placement).hex()}
            report["targets"].append(row)
            assert identify(handle)==item
            ctypes.set_last_error(0)
            row["minimize_requested"]=bool(user.ShowWindowAsync(handle,11))
            row["win32_error"]=ctypes.get_last_error()
            assert row["minimize_requested"],"Overlay minimize request failed: "+str(row["win32_error"])
            touched.append((handle,item,placement))
        end=time.monotonic()+2
        while any(not user.IsIconic(h) for h,_,_ in targets) and time.monotonic()<end:
            time.sleep(.02)
        assert all(user.IsIconic(h) for h,_,_ in targets),"Known overlay did not remain minimized"
        report["setup_passed"]=True
        yield
    except Exception as error:
        report["error"]=str(error)
        raise
    finally:
        restored=True
        report["restorations"]=[]
        for handle,item,placement in reversed(touched):
            if user.IsWindow(handle):
                match=identify(handle)==item
                ctypes.set_last_error(0)
                ok=bool(match and user.SetWindowPlacement(handle,ctypes.byref(placement)))
                report["restorations"].append({"handle":handle,"identity_match":match,"success":ok,"win32_error":ctypes.get_last_error()})
                restored=ok and restored
        report["restored"]=restored
        report["finished_utc"]=utc()
        save(directory/"desktop-envelope.json",report)
        assert restored,"Native overlay placement restoration failed"


def run(out,cfg):
    manifest=verify(out)
    candidate=manifest["candidate"]
    assert json.loads((out/f"stage-{cfg}.json").read_text())["result"]=="PASS"
    directory=local(f"runtime/RigidBodySimulation/{cfg}/phase-16-candidate-{candidate}")
    directory.mkdir(parents=True,exist_ok=False)
    (directory/"imgui.ini").write_text(manifest["layout"],encoding="utf-8")
    executable=local(f"bin/{cfg}/Phase16Candidate{candidate}/RigidBodySimulation/RigidBodySimulation.exe")
    report={"result":"INCONCLUSIVE","recorded_utc":utc(),"binary":identity(executable),"cwd":str(directory)}
    start=time.monotonic()
    process=None
    try:
        with desktop_envelope(directory), (directory/"process-output.log").open("xb") as stream:
            process=subprocess.Popen([str(executable)],cwd=directory,env=environment(),stdout=stream,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
            report["pid"]=process.pid
            while process.poll() is None:
                if time.monotonic()-start>=60: raise TimeoutError("60 second native runtime deadline")
                if (directory/"native-minimize.request").exists() and "native_restore" not in report:
                    restore_native(process,directory,report)
                time.sleep(.05)
            report["exit"]=process.returncode
            log=(directory/"GEngine.log").read_text(errors="replace")
            rows=list(csv.DictReader((directory/"camera-frames.csv").open()))
            math=list(csv.DictReader((directory/"camera-math.csv").open()))
            report["observed_frames"]=len(rows)
            report["cpu_checks"]=len(math)
            passed=process.returncode==0 and "PRE_EDITOR_PHASE_16_PASS" in log and len(rows)==56 and all(r["pass"]=="1" for r in math)
            passed &= log.count("PRE_EDITOR_PHASE_16_ROUTED_PICK")==7 and "native_restore" in report
            report["result"]="PASS" if passed else "FAIL"
    except Exception as error:
        report["reason"]=str(error)
        report["result"]="INCONCLUSIVE"
        if process is not None:
            if process.poll() is None: process.kill()
            process.wait()
    report["seconds"]=time.monotonic()-start
    archive=local(out/("runtime-"+cfg))
    archive.mkdir()
    for p in directory.iterdir():
        if p.is_file(): shutil.copyfile(p,archive/p.name)
    report["evidence"]={p.name:identity(p) for p in archive.iterdir() if p.is_file()}
    save(out/f"run-{cfg}.json",report)
    verify(out)
    print(json.dumps(report),flush=True)
    if report["result"]!="PASS": raise SystemExit(1)


# Phase 17 Amendment 04 is opt-in. The Phase 16 envelope and CLI above retain
# their original behavior. A same-thread bootstrap debug connection establishes
# DebugSetProcessKillOnExit(FALSE) before ANY external attachment. Initial debug
# events stay pending until detach. No target event is continued or rewritten.
PHASE17_ISOLATION_AUTH = ROOT / "docs/pre_editor/active/PHASE_17_EXTERNAL_DESKTOP_ISOLATION_AMENDMENT_04_ACCEPTANCE.json"
PHASE17_ISOLATION_PROOF = ROOT / "logs/pre_editor/phase-17/corrections/14/isolation-selftest/result.json"
PHASE17_EMPTY_AUTH = ROOT / "docs/pre_editor/active/PHASE_17_FINAL_OWNER_DIRECTIVE_ACCEPTANCE.json"
PHASE17_EMPTY_ADMISSION = ROOT / "logs/pre_editor/phase-17/corrections/16/admission.json"
PHASE17_REVISION_AUTH = ROOT / "docs/pre_editor/active/PHASE_17_REVISION_01_AUTHORIZATION.json"
PHASE17_REVISION_ADMISSION = ROOT / "logs/pre_editor/phase-17/revision-01/isolation-correction-02/admission.json"
PHASE17_CURRENT_ISOLATION_PROOF = ROOT / "logs/pre_editor/phase-17/revision-01/isolation-selftest-02/result.json"
PHASE17_EMPTY_INVENTORY_PROOF = ROOT / "logs/pre_editor/phase-17/revision-01/empty-inventory-proof-02/result.json"


def _phase17_require(condition, message):
    if not condition:
        raise RuntimeError(message)


class _Phase17NativeError(RuntimeError):
    def __init__(self, api, code, **context):
        self.record = {"api": api, "last_error": int(code), **context}
        super().__init__("Native API failure: " + json.dumps(self.record, sort_keys=True))


def _phase17_error_details(error):
    return {"exception": repr(error), "native": getattr(error, "record", None)}


def _phase17_proof_inputs():
    return {name: identity(ROOT / name) for name in (
        "tools/test_pre_editor_rbs_modularity.py", "docs/pre_editor/active/OUTPUT_PATHS.json",
        "logs/pre_editor/phase-17/candidate-08/runtime-Debug/desktop-envelope.json",
        "logs/pre_editor/phase-17/revision-01/isolation-correction-02/case-evidence-admission.json",
        PHASE17_REVISION_ADMISSION.relative_to(ROOT).as_posix())}


def _phase17_authorization(path):
    path = local(path)
    _phase17_require(path.resolve() == PHASE17_ISOLATION_AUTH.resolve(), "Unexpected isolation authorization")
    record = json.loads(path.read_text(encoding="utf-8"))
    admission = json.loads((ROOT / "logs/pre_editor/phase-17/corrections/14/admission.json").read_text())
    _phase17_require(identity(path) == {k: admission["acceptance"][k] for k in ("sha256", "size")},
                     "Isolation authorization changed after admission")
    _phase17_require(record["status"] == "ACCEPTED" and record["phase"] == 17 and
                     "tools/test_pre_editor_camera_snapshot.py" in record["additional_check_files"],
                     "Isolation is not authorized")
    revision = json.loads((ROOT / "logs/pre_editor/phase-17/corrections/14/implementation-admission-01.json").read_text())
    _phase17_require(identity(path) == {k: revision["authority"][k] for k in ("sha256", "size")} and
                     identity(ROOT / revision["new_design"]["path"]) ==
                     {k: revision["new_design"][k] for k in ("sha256", "size")},
                     "Isolation implementation admission changed")
    return {"path": str(path), **identity(path)}


def _phase17_platform():
    _phase17_require(os.name == "nt" and ctypes.sizeof(ctypes.c_void_p) == 8,
                     "Isolation requires native 64-bit Windows Python")
    version = sys.getwindowsversion()
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetModuleFileNameW.argtypes = [wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
    kernel.GetModuleFileNameW.restype = wintypes.DWORD
    module_path = ctypes.create_unicode_buffer(32768)
    length = kernel.GetModuleFileNameW(ctypes.pythonapi._handle, module_path, len(module_path))
    _phase17_require(0 < length < len(module_path), "Cannot identify the loaded Python runtime DLL")
    system = Path(os.environ["SystemRoot"]) / "System32"
    return {"windows": list(version), "python": sys.version,
            "python_image": identity(sys.executable),
            "python_runtime": {"path": module_path.value, **identity(module_path.value)},
            "api_providers": {name: identity(system / name)
                              for name in ("ntdll.dll", "kernel32.dll", "KernelBase.dll", "user32.dll")}}


class _Phase17Native:
    def __init__(self):
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.user = ctypes.WinDLL("user32", use_last_error=True)
        self.handle_context = {}
        class DebugEvent(ctypes.Structure):
            # DEBUG_EVENT's union is 160 bytes, aligned to 8 on native x64.
            _fields_ = [("code", wintypes.DWORD), ("pid", wintypes.DWORD),
                        ("tid", wintypes.DWORD), ("data", ctypes.c_uint64 * 20)]
        self.DebugEvent = DebugEvent
        _phase17_require(ctypes.sizeof(DebugEvent) == 176 and DebugEvent.data.offset == 16,
                         "Unexpected native DEBUG_EVENT layout")
        bindings = [
            (self.kernel, "GetCurrentThreadId", [], wintypes.DWORD),
            (self.kernel, "OpenProcess", [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            (self.kernel, "CloseHandle", [wintypes.HANDLE], wintypes.BOOL),
            (self.kernel, "WaitForSingleObject", [wintypes.HANDLE, wintypes.DWORD], wintypes.DWORD),
            (self.kernel, "GetProcessId", [wintypes.HANDLE], wintypes.DWORD),
            (self.kernel, "GetProcessTimes", [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4, wintypes.BOOL),
            (self.kernel, "QueryFullProcessImageNameW", [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
            (self.kernel, "GetHandleInformation", [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
            (self.user, "GetWindowThreadProcessId", [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)], wintypes.DWORD),
            (self.user, "GetClassNameW", [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int], ctypes.c_int),
            (self.kernel, "CheckRemoteDebuggerPresent", [wintypes.HANDLE, ctypes.POINTER(wintypes.BOOL)], wintypes.BOOL),
            (self.kernel, "DebugSetProcessKillOnExit", [wintypes.BOOL], wintypes.BOOL),
            (self.kernel, "DebugActiveProcess", [wintypes.DWORD], wintypes.BOOL),
            (self.kernel, "DebugActiveProcessStop", [wintypes.DWORD], wintypes.BOOL),
            (self.kernel, "WaitForDebugEvent", [ctypes.POINTER(DebugEvent), wintypes.DWORD], wintypes.BOOL),
            (self.user, "SendMessageTimeoutW", [wintypes.HWND, wintypes.UINT, wintypes.WPARAM,
                                               wintypes.LPARAM, wintypes.UINT, wintypes.UINT,
                                               ctypes.POINTER(ctypes.c_size_t)], ctypes.c_ssize_t),
        ]
        for library, name, arguments, result in bindings:
            function = getattr(library, name)  # Missing native exports are fatal.
            function.argtypes, function.restype = arguments, result
        self.debug_owner_tid = int(self.kernel.GetCurrentThreadId())
        self.debug_calls, self.initial_debug_events = [], []

    def open(self, pid):
        access = 0x100000 | 0x0400 | 0x1000  # SYNCHRONIZE, QUERY_INFORMATION, QUERY_LIMITED_INFORMATION
        ctypes.set_last_error(0)
        handle = self.kernel.OpenProcess(access, False, pid)
        if not handle:
            raise _Phase17NativeError("OpenProcess", ctypes.get_last_error(), pid=int(pid), access_mask=access)
        self.handle_context[int(handle)] = {"pid": int(pid), "access_mask": access}
        return handle

    def failure(self, api, handle, code=None, **context):
        code = ctypes.get_last_error() if code is None else code
        raise _Phase17NativeError(api, code, handle=int(handle),
                                  **{**self.handle_context.get(int(handle), {}), **context})

    def close(self, handle):
        if handle:
            ctypes.set_last_error(0)
            if not self.kernel.CloseHandle(handle):
                self.failure("CloseHandle", handle)
            self.handle_context.pop(int(handle), None)

    def alive(self, handle):
        ctypes.set_last_error(0)
        result = self.kernel.WaitForSingleObject(handle, 0)
        if result not in (0, 258):
            self.failure("WaitForSingleObject", handle, native_return=int(result))
        return result == 258

    def process(self, handle, expected_identity=None):
        context = {"expected_identity": expected_identity or self.handle_context.get(int(handle), {}),
                   "actual_identity": {}}
        _phase17_require(self.alive(handle), "Pinned process exited: " + json.dumps(context, sort_keys=True))
        times = [wintypes.FILETIME() for unused in range(4)]
        ctypes.set_last_error(0)
        if not self.kernel.GetProcessTimes(handle, *(ctypes.byref(value) for value in times)):
            self.failure("GetProcessTimes", handle, **context)
        context["actual_identity"]["creation_filetime"] = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        path = ctypes.create_unicode_buffer(32768)
        length = wintypes.DWORD(len(path))
        ctypes.set_last_error(0)
        if not self.kernel.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(length)):
            self.failure("QueryFullProcessImageNameW", handle, **context)
        context["actual_identity"]["image"] = os.path.normcase(os.path.abspath(path.value))
        ctypes.set_last_error(0)
        pid = self.kernel.GetProcessId(handle)
        if not pid:
            self.failure("GetProcessId", handle, **context)
        return {"pid": int(pid), **context["actual_identity"]}

    def debugged(self, handle):
        present = wintypes.BOOL()
        ctypes.set_last_error(0)
        if not self.kernel.CheckRemoteDebuggerPresent(handle, ctypes.byref(present)):
            self.failure("CheckRemoteDebuggerPresent", handle)
        return bool(present.value)

    def cpu_time(self, handle):
        times = [wintypes.FILETIME() for unused in range(4)]
        ctypes.set_last_error(0)
        if not self.kernel.GetProcessTimes(handle, *(ctypes.byref(value) for value in times)):
            self.failure("GetProcessTimes", handle)
        return sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in times[2:])

    def debug_call(self, name, value):
        native_tid = int(self.kernel.GetCurrentThreadId())
        _phase17_require(native_tid == self.debug_owner_tid, "Debugger call moved off its owning native thread")
        ctypes.set_last_error(0)
        succeeded = bool(getattr(self.kernel, name)(value))
        code = ctypes.get_last_error()
        self.debug_calls.append({"api": name, "argument": int(value), "native_tid": native_tid,
                                 "succeeded": succeeded, "last_error": code if not succeeded else 0})
        if not succeeded:
            raise _Phase17NativeError(name, code, argument=int(value), native_tid=native_tid,
                                      debug_owner_tid=self.debug_owner_tid)

    def initial_events(self, expected_pids, deadline, guard):
        held = self.initial_debug_events
        while set(row["pid"] for row in held) != set(expected_pids):
            _phase17_require(time.monotonic() < deadline, "Initial debug-event deadline elapsed")
            _phase17_require(guard(), "Parent/release guard failed while awaiting initial debug events")
            event = self.DebugEvent()
            native_tid = int(self.kernel.GetCurrentThreadId())
            _phase17_require(native_tid == self.debug_owner_tid, "Debug-event wait moved off its owning native thread")
            ctypes.set_last_error(0)
            if not self.kernel.WaitForDebugEvent(ctypes.byref(event), 50):
                code = ctypes.get_last_error()
                if code not in (0, 121):
                    raise _Phase17NativeError("WaitForDebugEvent", code, expected_pids=expected_pids)
                continue
            # Only initial CREATE_PROCESS events may be observed. We never call
            # ContinueDebugEvent, so each process remains stopped at this event.
            _phase17_require(event.code == 3 and event.pid in expected_pids and
                             event.pid not in {row["pid"] for row in held},
                             "Unexpected debugger event; refuse to continue it")
            # WaitForDebugEvent assigns hFile closure to the debugger; hProcess
            # and hThread participate in native debug-event lifecycle cleanup.
            # Keep those handles valid while events are pending. Never manually
            # close their numeric values after detach (they may already be closed
            # and reused); guardian process teardown reclaims any remaining handles.
            row = {"pid": event.pid, "tid": event.tid, "event": "CREATE_PROCESS_DEBUG_EVENT",
                   "native_tid": native_tid,
                   "event_handles": dict(zip(("image_file", "process", "thread"), map(int, event.data[:3]))),
                   "image_file_handle_closed": False, "process_thread_handles_manually_closed": False}
            held.append(row)
            if event.data[0]:
                self.close(event.data[0])
                row["image_file_handle_closed"] = True
        return held

    def responsive(self, item):
        for window in item.get("windows", []):
            pid = wintypes.DWORD()
            name = ctypes.create_unicode_buffer(256)
            context = {"hwnd": window["hwnd"], "expected_identity": item}
            ctypes.set_last_error(0)
            if not self.user.GetWindowThreadProcessId(window["hwnd"], ctypes.byref(pid)):
                raise _Phase17NativeError("GetWindowThreadProcessId", ctypes.get_last_error(), **context)
            ctypes.set_last_error(0)
            if not self.user.GetClassNameW(window["hwnd"], name, len(name)):
                raise _Phase17NativeError("GetClassNameW", ctypes.get_last_error(), pid=int(pid.value), **context)
            _phase17_require(pid.value == item["pid"] and name.value == window["class"],
                             "Pinned window identity changed: " + json.dumps({**context,
                                 "actual_identity": {"pid": int(pid.value), "class": name.value}}, sort_keys=True))
            response = ctypes.c_size_t()
            ctypes.set_last_error(0)
            if not self.user.SendMessageTimeoutW(window["hwnd"], 0, 0, 0, 0x23, 300, ctypes.byref(response)):
                raise _Phase17NativeError("SendMessageTimeoutW", ctypes.get_last_error(), pid=int(pid.value), **context)
        return True

    def windows(self, expected_image):
        # Qualification and absence monitoring share the same strict observer.
        # Failure to query a class/owner is unknown inventory, never absence.
        return _phase17_absence_inventory(self, expected_image)


def _phase17_same_process(native, handle, item):
    expected = {key: item[key] for key in ("pid", "creation_filetime", "image")}
    return native.process(handle, expected) == expected


def _phase17_publish(path, record):
    # Only this guardian writes its replaceable heartbeat. Evidence records use save/x.
    path = local(path)
    temporary = path.with_name(path.name + ".new")
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(record, stream)
    os.replace(temporary, path)


def _phase17_launch(mode, request, stream, debugging=False):
    return subprocess.Popen([sys.executable, "-B", str(Path(__file__).resolve()), mode, str(request)],
                            cwd=ROOT, env=environment(), stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NO_WINDOW | (0x00000002 if debugging else 0),  # DEBUG_ONLY_THIS_PROCESS
                            close_fds=True)


def _phase17_guardian(request_path):
    request = json.loads(local(request_path).read_text())
    directory = local(request_path).parent
    native = _Phase17Native()
    handles, attached, parent, bootstrap = [], [], None, None
    report = {"result": "INCONCLUSIVE", "recorded_utc": utc(), "guardian_pid": os.getpid(),
              "restored": False, "restorations": [], "targets": request["targets"],
              "native_debug_owner_tid": native.debug_owner_tid,
              "debug_calls": native.debug_calls, "observed_initial_events": native.initial_debug_events}
    failed = None
    reason = "setup_failure"
    try:
        _phase17_require(identity(__file__) == request["helper"], "Guardian helper identity changed")
        _phase17_require(_phase17_proof_inputs() == request["source_inputs"], "Guardian source inputs changed")
        _phase17_require(_phase17_authorization(request["authorization"]["path"]) == request["authorization"],
                         "Guardian authorization mismatch")
        _phase17_require(_phase17_platform() == request["platform"], "Guardian OS identity mismatch")
        parent = native.open(request["parent"]["pid"])
        _phase17_require(_phase17_same_process(native, parent, request["parent"]), "Guardian parent identity mismatch")
        _phase17_require(time.monotonic() < request["deadline"], "Guardian deadline already elapsed")
        # Pin every process, baseline and executable before changing any state.
        for item in request["targets"]:
            handle = native.open(item["pid"])
            handles.append((handle, item))
            _phase17_require(_phase17_same_process(native, handle, item), "Target identity changed")
            _phase17_require(identity(item["image"]) == item["file"], "Target image bytes changed")
            _phase17_require(not native.debugged(handle) and item["baseline_debugged"] is False,
                             "Target was already debugged; no debug ownership assumption allowed")
            native.responsive(item)
        if not request.get("surrogate"):
            _phase17_require(native.windows(request["image"]) == request["inventory"],
                             "Target window inventory changed before isolation")
        # Only our new disposable bootstrap is exposed to the default kill policy.
        # The successful FALSE call occurs on this same thread before any target
        # attach, and the bootstrap preserves the debugging connection throughout.
        report["bootstrap_launch_native_tid"] = int(native.kernel.GetCurrentThreadId())
        _phase17_require(report["bootstrap_launch_native_tid"] == native.debug_owner_tid,
                         "Bootstrap launch moved off debugger's owning native thread")
        bootstrap = _phase17_launch("_phase17-bootstrap", request_path, subprocess.DEVNULL, debugging=True)
        native.debug_call("DebugSetProcessKillOnExit", False)
        report["no_kill_policy_armed"] = True
        for index, (handle, item) in enumerate(handles):
            _phase17_require(native.alive(parent) and time.monotonic() < request["deadline"],
                             "Parent/deadline guard failed during setup")
            _phase17_require(not native.debugged(handle), "Target acquired another debugger before attachment")
            native.debug_call("DebugActiveProcess", item["pid"])
            attached.append(item["pid"])
            if request.get("surrogate_fail_after_attach") == index + 1:
                raise RuntimeError("Deliberate surrogate partial-setup failure")
        report["held_initial_events"] = native.initial_events(
            [bootstrap.pid, *attached], request.get("release_deadline", request["deadline"]),
            lambda: native.alive(parent) and not (directory / "release.request").exists())
        cpu_baseline = {item["pid"]: native.cpu_time(handle) for handle, item in handles}
        report["ready_utc"] = utc()
        save(directory / "ready.json", {"guardian_pid": os.getpid(), "targets": request["targets"],
                                       "deadline": request["deadline"], "initial_events_held": True,
                                       "no_kill_policy_armed": True,
                                        "native_debug_owner_tid": native.debug_owner_tid,
                                        "bootstrap_launch_native_tid": report["bootstrap_launch_native_tid"],
                                        "debug_calls": native.debug_calls,
                                       "held_initial_events": report["held_initial_events"]})
        last_check = 0.0
        while True:
            now = time.monotonic()
            if not native.alive(parent):
                reason = "parent_exit"
                break
            if now >= request.get("release_deadline", request["deadline"]):
                reason = "deadline"
                break
            if (directory / "release.request").exists():
                reason = "release_requested"
                break
            if now - last_check >= .25:
                try:
                    for handle, item in handles:
                        _phase17_require(_phase17_same_process(native, handle, item) and native.debugged(handle) and
                                         native.cpu_time(handle) == cpu_baseline[item["pid"]],
                                         "Pinned target exited, lost debugger lease, or executed while held")
                    if not request.get("surrogate"):
                        current = native.windows(request["image"])
                        _phase17_require(current == request["inventory"], "Window/process inventory drift during isolation")
                except BaseException as error:
                    if failed is None:
                        failed = repr(error)
                        save(directory / "failure.json", {"error": failed, "recorded_utc": utc()})
                _phase17_publish(directory / "heartbeat.json", {"monotonic": now, "failed": failed})
                last_check = now
            time.sleep(.025)
        report["result"] = "PASS" if reason == "release_requested" and failed is None else "INCONCLUSIVE"
    except BaseException as error:
        failed = repr(error)
        report["error"] = failed
        if not (directory / "failure.json").exists():
            save(directory / "failure.json", {"error": failed, "recorded_utc": utc()})
    finally:
        # Explicit detach removes our pending initial events. If this path fails,
        # thread/process exit invokes the already-armed no-kill detach policy.
        close_errors = []
        for pid in reversed(attached):
            try:
                native.debug_call("DebugActiveProcessStop", pid)
            except BaseException as error:
                close_errors.append(repr(error))
        for handle, item in handles:
            row = {"pid": item["pid"], "restored": False}
            try:
                row["identity_match"] = _phase17_same_process(native, handle, item)
                row["debugged"] = native.debugged(handle)
                row["responsive"] = native.responsive(item)
                row["restored"] = row["identity_match"] and row["debugged"] == item["baseline_debugged"] and row["responsive"]
            except BaseException as error:
                row["error"] = repr(error)
            report["restorations"].append(row)
            try:
                native.close(handle)
            except BaseException as error:
                close_errors.append(repr(error))
        if parent:
            try:
                native.close(parent)
            except BaseException as error:
                close_errors.append(repr(error))
        if bootstrap is not None:
            try:
                native.debug_call("DebugActiveProcessStop", bootstrap.pid)
                bootstrap.wait(timeout=max(0, request["deadline"] - time.monotonic()))
            except BaseException as error:
                close_errors.append("Bootstrap: " + repr(error))
                # It is ours; never use this cleanup on a pinned external target.
                if bootstrap.poll() is None:
                    bootstrap.kill()
        report.update({"reason": reason, "failure": failed, "close_errors": close_errors,
                       "finished_utc": utc(), "finished_monotonic": time.monotonic()})
        report["restored"] = not close_errors and len(handles) == len(request["targets"]) and all(
            row["restored"] for row in report["restorations"])
        if not report["restored"]:
            report["result"] = "INCONCLUSIVE"
        save(directory / "exit.json", report)
    return 0 if report["result"] == "PASS" else 1


class _Phase17Lease:
    def __init__(self, directory, process, request):
        self.directory, self.process, self.request = directory, process, request
        self.report = None
        self.runtime = None
        self.last_heartbeat = time.monotonic()
        self.heartbeat_mtime = None

    def assert_healthy(self):
        _phase17_require(self.process.poll() is None, "Isolation guardian exited unexpectedly")
        _phase17_require(not (self.directory / "failure.json").exists(), "Isolation guardian reported failure")
        _phase17_require(time.monotonic() < self.request["deadline"] - 30,
                         "Isolation close reserve reached; stop owned RBS before release")
        heartbeat = self.directory / "heartbeat.json"
        if heartbeat.exists():
            modified = heartbeat.stat().st_mtime_ns
            if modified != self.heartbeat_mtime:
                self.heartbeat_mtime = modified
                self.last_heartbeat = time.monotonic()
        _phase17_require(time.monotonic() - self.last_heartbeat < 3,
                         "Isolation guardian heartbeat stalled")

    def bind_runtime(self, process):
        self.assert_healthy()
        _phase17_require(self.runtime is None, "Isolation lease already bound to a runtime")
        self.runtime = process
        save(self.directory / "runtime.json", {"pid": process.pid, "recorded_utc": utc()})


def _phase17_wait_ready(directory, process, deadline):
    while not (directory / "ready.json").exists():
        _phase17_require(process.poll() is None and not (directory / "failure.json").exists(),
                         "Isolation guardian failed before readiness; inspect guardian evidence")
        _phase17_require(time.monotonic() < deadline, "Isolation setup deadline elapsed")
        time.sleep(.025)


def _phase17_request(directory, targets, parent, deadline, authorization, platform, **extra):
    request = {"helper": identity(__file__), "authorization": authorization, "platform": platform,
               "source_inputs": _phase17_proof_inputs(),
               "parent": parent, "deadline": deadline, "targets": targets, **extra}
    save(directory / "request.json", request)
    return request


def _phase17_empty_authorization():
    admission = json.loads(PHASE17_REVISION_ADMISSION.read_text())
    authority = admission["authority"]
    _phase17_require(ROOT / authority["path"] == PHASE17_REVISION_AUTH and
                     identity(PHASE17_REVISION_AUTH) == {k: authority[k] for k in ("sha256", "size")},
                     "Revision isolation authority changed after admission")
    record = json.loads(PHASE17_REVISION_AUTH.read_text())
    _phase17_require(record["status"] == "AUTHORIZED" and record["phase"] == 17 and
                     record["revision"] == "01" and
                     "tools/test_pre_editor_camera_snapshot.py" in record["check_files"],
                     "Revision isolation checks are not authorized")
    previous = record["previous_authority"]
    _phase17_require(ROOT / previous["path"] == PHASE17_EMPTY_AUTH and
                     identity(PHASE17_EMPTY_AUTH) == {k: previous[k] for k in ("sha256", "size")},
                     "Revision authority predecessor changed")
    return {"path": str(PHASE17_REVISION_AUTH), **identity(PHASE17_REVISION_AUTH)}


def _phase17_nonempty_reuse(authorization, platform):
    """Reuse only the unchanged backend exercised by the original five cases."""
    historical = json.loads(PHASE17_ISOLATION_PROOF.read_text())
    before = PHASE17_EMPTY_ADMISSION.parent / "before/tools/test_pre_editor_camera_snapshot.py"
    admitted = json.loads(PHASE17_EMPTY_ADMISSION.read_text())["before"]["tools/test_pre_editor_camera_snapshot.py"]
    _phase17_require(identity(before) == admitted == historical["helper"] and
                     historical["result"] == "PASS" and historical["authorization"] == authorization and
                     historical["platform"] == platform, "Original nonempty backend proof is not reusable")
    _phase17_require([(row["mode"], row["result"]) for row in historical["cases"]] == [
        (name, "PASS") for name in ("explicit_close", "holder_crash", "parent_exit", "deadline", "partial_setup")],
        "Original nonempty proof lacks required cases")
    def nodes(path):
        # Preserve exact source text inside each original node. File-level newline
        # normalization is recorded separately from behavior-affecting node text.
        source = Path(path).read_text(encoding="utf-8")
        rows = {}
        for node in ast.parse(source).body:
            if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
                if node.name == "phase17_desktop_isolation":
                    continue  # New routing/proof plumbing is reviewed and checked separately.
                key = type(node).__name__ + ":" + node.name
            elif isinstance(node, (ast.Import, ast.ImportFrom, ast.Assign)):
                key = ast.dump(node, include_attributes=False)
            else:
                continue  # The CLI dispatch is the other admitted routing change.
            start = min([node.lineno] + [d.lineno for d in getattr(node, "decorator_list", [])])
            text = "\n".join(source.splitlines()[start - 1:node.end_lineno])
            _phase17_require(key not in rows, "Duplicate nonempty closure node")
            rows[key] = text.encode("utf-8")
        return rows
    baseline, current = nodes(before), nodes(__file__)
    _phase17_require(all(current.get(name) == value for name, value in baseline.items()),
                     "Historical nonempty source closure changed")
    closure_rows = {name: {"sha256": hashlib.sha256(value).hexdigest(), "size": len(value)}
                    for name, value in sorted(baseline.items())}
    return {"result": "REUSED_UNCHANGED_BACKEND", "historical_proof": {"path": str(PHASE17_ISOLATION_PROOF),
            **identity(PHASE17_ISOLATION_PROOF)}, "historical_helper": {"path": str(before), **identity(before)},
            "source_text_encoding": "UTF-8 with LF, exact original top-level node text including decorators",
            "excluded_changed_routing": ["phase17_desktop_isolation", "__main__ dispatch"],
            "nodes": closure_rows}


def _phase17_current_nonempty_proof(authorization, platform):
    proof = json.loads(PHASE17_CURRENT_ISOLATION_PROOF.read_text())
    _phase17_require(proof["result"] == "PASS" and proof["helper"] == identity(__file__) and
                     proof["platform"] == platform and proof["authorization"] == authorization and
                     proof["final_authorization"] == _phase17_empty_authorization() and
                     proof["source_inputs"] == _phase17_proof_inputs(),
                     "Current nonempty backend requires its own exact surrogate proof")
    _phase17_require([(row["mode"], row["result"]) for row in proof["cases"]] == [
        (name, "PASS") for name in ("explicit_close", "holder_crash", "parent_exit", "deadline", "partial_setup")],
        "Current nonempty proof lacks required cases")
    return {"path": str(PHASE17_CURRENT_ISOLATION_PROOF), **identity(PHASE17_CURRENT_ISOLATION_PROOF)}


def _phase17_isolation_proof(authorization, platform):
    proof = json.loads(PHASE17_EMPTY_INVENTORY_PROOF.read_text())
    _phase17_require(proof["result"] == "PASS" and proof["helper"] == identity(__file__) and
                     proof["platform"] == platform and proof["authorization"] == authorization and
                     proof["final_authorization"] == _phase17_empty_authorization() and
                     proof["source_inputs"] == _phase17_proof_inputs() and
                     proof["nonempty_backend_proof"] == _phase17_current_nonempty_proof(authorization, platform),
                     "Current helper/OS/revision authority requires its exact focused absence proof")
    _phase17_require([(row["mode"], row["result"]) for row in proof["cases"]] == [
        (name, "PASS") for name in ("explicit_close", "appearance", "query_failure", "guardian_loss", "parent_exit", "deadline")],
        "Focused absence proof lacks required cases")
    _phase17_require(proof["fixture"]["role"] == "TEST_ONLY_EXPECTED_IMAGE" and
                     proof["fixture"]["expected_image"] == _phase17_empty_fixture_image() and
                     proof["fixture"]["claims_real_C08_absence"] is False and
                     [(row["api"], row["result"]) for row in proof["error_attribution_cases"]] == [
                         (name, "PASS") for name in ("GetClassNameW", "GetWindowThreadProcessId", "OpenProcess",
                         "WaitForSingleObject", "GetProcessTimes", "QueryFullProcessImageNameW", "GetProcessId",
                         "CloseHandle", "EnumWindows")],
                     "Focused absence fixture/error-attribution proof is incomplete")
    return proof


def _phase17_absence_inventory(native, expected_image, injected_class_failure=False):
    """Strict read-only enumeration: inability to query any class is not absence."""
    result, errors, injected = {}, [], False
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    native.user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    native.user.EnumWindows.restype = wintypes.BOOL
    @callback_type
    def visit(window, unused):
        nonlocal injected
        context = {"hwnd": int(window), "expected_image": expected_image, "actual_identity": None}
        try:
            name = ctypes.create_unicode_buffer(256)
            ctypes.set_last_error(0)
            length = native.user.GetClassNameW(window, name, len(name))
            code = ctypes.get_last_error()
            if injected_class_failure and not injected:
                _phase17_require(length > 0, "Real class query failed before focused injection")
                length, code, injected = 0, 5, True  # Exact native failure boundary, proof-only.
            if length <= 0:
                raise _Phase17NativeError("GetClassNameW", code, hwnd=int(window),
                                          deliberate_focused_fault=injected)
            context["class"] = name.value
            if name.value == "mininews":
                pid = wintypes.DWORD()
                ctypes.set_last_error(0)
                if not native.user.GetWindowThreadProcessId(window, ctypes.byref(pid)):
                    raise _Phase17NativeError("GetWindowThreadProcessId", ctypes.get_last_error(), hwnd=int(window))
                context["pid"] = int(pid.value)
                handle = native.open(pid.value)
                primary = None
                try:
                    item = native.process(handle, {"pid": int(pid.value), "image": expected_image})
                    context["actual_identity"] = item
                    if item["image"] == expected_image:
                        result.setdefault(pid.value, {**item, "windows": []})["windows"].append(
                            {"hwnd": int(window), "class": name.value})
                except BaseException as error:
                    primary = error
                finally:
                    try:
                        native.close(handle)
                    except BaseException as closing:
                        if primary is not None:
                            raise RuntimeError("Inventory query and handle cleanup failed: " + json.dumps({
                                "primary": _phase17_error_details(primary),
                                "cleanup": _phase17_error_details(closing)}, sort_keys=True)) from primary
                        raise
                if primary is not None:
                    raise primary
        except BaseException as error:
            errors.append({**context, **_phase17_error_details(error)})
            return False
        return True
    ctypes.set_last_error(0)
    ok = native.user.EnumWindows(visit, 0)
    code = ctypes.get_last_error()
    if not ok and not errors:
        errors.append(_phase17_error_details(_Phase17NativeError("EnumWindows", code)))
    _phase17_require(ok and not errors, "Absence inventory query failed: " + json.dumps(errors, sort_keys=True))
    _phase17_require(not injected_class_failure or injected, "Focused query-failure injection was not exercised")
    for item in result.values():
        item["windows"].sort(key=lambda window: window["hwnd"])
    return sorted(result.values(), key=lambda item: item["pid"])


def _phase17_empty_fixture_image():
    path = local(PHASE17_EMPTY_INVENTORY_PROOF.parent / "TEST_ONLY_NO_PROCESS_IMAGE.exe")
    _phase17_require(not path.exists(), "Focused image sentinel unexpectedly exists")
    return os.path.normcase(os.path.abspath(path))


def _phase17_inventory_error_selftest():
    """Exercise actual callback/error paths with in-memory Win32 return fixtures."""
    cases = []
    for failing_api in ("GetClassNameW", "GetWindowThreadProcessId", "OpenProcess", "WaitForSingleObject",
                        "GetProcessTimes", "QueryFullProcessImageNameW", "GetProcessId", "CloseHandle", "EnumWindows"):
        native = _Phase17Native()
        def answer(api, value):
            if failing_api == api:
                ctypes.set_last_error(5)
                return 0xFFFFFFFF if api == "WaitForSingleObject" else 0
            return value
        def get_class(hwnd, value, capacity):
            value.value = "mininews"
            return answer("GetClassNameW", 8)
        def get_owner(hwnd, value):
            ctypes.cast(value, ctypes.POINTER(wintypes.DWORD))[0] = 4242
            return answer("GetWindowThreadProcessId", 4343)
        def get_times(handle, created, exited, kernel, user):
            ctypes.cast(created, ctypes.POINTER(wintypes.FILETIME))[0].dwLowDateTime = 12345
            return answer("GetProcessTimes", 1)
        def get_image(handle, flags, value, capacity):
            value.value = _phase17_empty_fixture_image()
            return answer("QueryFullProcessImageNameW", 1)
        def enumerate_windows(callback, value):
            return answer("EnumWindows", 0) if failing_api == "EnumWindows" else callback(4545, value)
        native.user.GetClassNameW = get_class
        native.user.GetWindowThreadProcessId = get_owner
        native.user.EnumWindows = enumerate_windows
        native.kernel.OpenProcess = lambda access, inherit, pid: answer("OpenProcess", 4646)
        native.kernel.WaitForSingleObject = lambda handle, timeout: answer("WaitForSingleObject", 258)
        native.kernel.GetProcessTimes = get_times
        native.kernel.QueryFullProcessImageNameW = get_image
        native.kernel.GetProcessId = lambda handle: answer("GetProcessId", 4242)
        native.kernel.CloseHandle = lambda handle: answer("CloseHandle", 1)
        error = None
        try:
            _phase17_absence_inventory(native, _phase17_empty_fixture_image())
        except RuntimeError as observed:
            error = str(observed)
        _phase17_require(error is not None and error.startswith("Absence inventory query failed: "),
                         "A failed native inventory query was accepted")
        records = json.loads(error.split(": ", 1)[1])
        _phase17_require(len(records) == 1 and records[0]["native"]["api"] == failing_api and
                         records[0]["native"]["last_error"] == 5,
                         "Exact native API/error attribution was lost")
        if failing_api != "EnumWindows":
            _phase17_require(records[0]["hwnd"] == 4545 and
                             records[0]["expected_image"] == _phase17_empty_fixture_image(),
                             "Inventory window/expected-identity context was lost")
        if failing_api in ("OpenProcess", "WaitForSingleObject", "GetProcessTimes",
                           "QueryFullProcessImageNameW", "GetProcessId", "CloseHandle"):
            _phase17_require(records[0]["pid"] == records[0]["native"]["pid"] == 4242 and
                             records[0]["native"]["access_mask"] == 0x101400,
                             "Inventory PID/access-mask context was lost")
        cases.append({"api": failing_api, "result": "PASS", "observed": records[0]})
    return cases


def _phase17_absence_sample(native, request, directory):
    injection = None
    if (directory / "inject.request").exists():
        _phase17_require(request.get("focused_check") in ("appearance", "query_failure") and
                         directory == PHASE17_EMPTY_INVENTORY_PROOF.parent / request["focused_check"] and
                         not PHASE17_EMPTY_INVENTORY_PROOF.exists(), "Unadmitted absence fault injection")
        injection = json.loads((directory / "inject.request").read_text())["mode"]
        _phase17_require(injection == request["focused_check"], "Absence fault injection mismatch")
    current = _phase17_absence_inventory(native, request["image"], injection == "query_failure")
    _phase17_require(not current, "Unexpected real identity-matching window appeared during absence monitoring")
    if injection == "appearance":
        # Deliberate returned-inventory injection; no target/window is created.
        current = [{"focused_injected_matching_window": True}]
    _phase17_require(current == request["inventory"] == [],
                     "Identity-matching window appeared during absence monitoring")


def _phase17_empty_guardian(request_path):
    request_path = local(request_path)
    request = json.loads(request_path.read_text())
    directory, native, parent = request_path.parent, _Phase17Native(), None
    report = {"result": "INCONCLUSIVE", "recorded_utc": utc(), "guardian_pid": os.getpid(),
              "lease_mode": "ABSENCE_MONITORED", "targets": [], "external_attachments": 0,
              "restored": None, "restorations": [], "restoration": "NOT_APPLICABLE_NO_EXTERNAL_STATE_CHANGED",
              "absence_verified": False, "inventory_samples": 0,
              "sampling_limit": "Periodic samples do not prove absence between samples"}
    failed, reason = None, "setup_failure"
    try:
        _phase17_require(request["targets"] == request["inventory"] == [] and
                         request["lease_mode"] == "ABSENCE_MONITORED", "Absence request contains a target")
        _phase17_require(identity(__file__) == request["helper"], "Absence guardian helper identity changed")
        _phase17_require(_phase17_proof_inputs() == request["source_inputs"], "Absence source inputs changed")
        _phase17_require(_phase17_authorization(request["authorization"]["path"]) == request["authorization"] and
                         _phase17_empty_authorization() == request["final_authorization"],
                         "Absence guardian authorization mismatch")
        _phase17_require(_phase17_platform() == request["platform"], "Absence guardian OS identity mismatch")
        if request.get("focused_check"):
            _phase17_require(directory == PHASE17_EMPTY_INVENTORY_PROOF.parent / request["focused_check"] and
                             not PHASE17_EMPTY_INVENTORY_PROOF.exists() and
                             request["image"] == _phase17_empty_fixture_image() and
                             request["expected_image_role"] == "TEST_ONLY_EXPECTED_IMAGE",
                             "Unadmitted focused guardian request")
            report["expected_image_role"] = "TEST_ONLY_EXPECTED_IMAGE"
            report["claims_real_C08_absence"] = False
        else:
            _phase17_isolation_proof(request["authorization"], request["platform"])
        parent = native.open(request["parent"]["pid"])
        _phase17_require(_phase17_same_process(native, parent, request["parent"]), "Absence parent identity mismatch")
        _phase17_require(time.monotonic() < request["release_deadline"] <= request["deadline"],
                         "Absence guardian deadline already elapsed")
        _phase17_absence_sample(native, request, directory)
        report["inventory_samples"] += 1
        report["first_sample_utc"] = utc()
        save(directory / "ready.json", {"guardian_pid": os.getpid(), "targets": [],
             "lease_mode": "ABSENCE_MONITORED", "external_attachments": 0,
             "deadline": request["deadline"], "initial_inventory_empty": True,
             "initial_events_held": False, "no_kill_policy_armed": False})
        last_check = 0.0
        while True:
            now = time.monotonic()
            if not native.alive(parent):
                reason = "parent_exit"
                break
            if now >= request["release_deadline"]:
                reason = "deadline"
                break
            if (directory / "release.request").exists():
                reason = "release_requested"
                _phase17_absence_sample(native, request, directory)
                report["inventory_samples"] += 1
                report["last_sample_utc"] = utc()
                report["absence_verified"] = True
                report["result"] = "PASS"
                break
            if now - last_check >= .25:
                reason = "inventory_failure"
                _phase17_absence_sample(native, request, directory)
                report["inventory_samples"] += 1
                report["last_sample_utc"] = utc()
                _phase17_publish(directory / "heartbeat.json", {"monotonic": now, "failed": None,
                                 "lease_mode": "ABSENCE_MONITORED", "inventory_samples": report["inventory_samples"]})
                last_check = now
            time.sleep(.025)
    except BaseException as error:
        failed = repr(error)
        report["failure_details"] = {"message": str(error), **_phase17_error_details(error)}
        save(directory / "failure.json", {"error": failed, "details": report["failure_details"], "recorded_utc": utc()})
    finally:
        close_errors = []
        if parent:
            try:
                native.close(parent)
            except BaseException as error:
                close_errors.append(repr(error))
        report.update({"reason": reason, "failure": failed, "close_errors": close_errors,
                       "finished_utc": utc(), "finished_monotonic": time.monotonic()})
        if failed or close_errors or time.monotonic() > request["deadline"]:
            report["result"] = "INCONCLUSIVE"
            report["absence_verified"] = False
        save(directory / "exit.json", report)
    return 0 if report["result"] == "PASS" else 1


def _phase17_empty_completion(report, returncode):
    _phase17_require(returncode == 0 and report.get("result") == "PASS" and
                     report.get("lease_mode") == "ABSENCE_MONITORED" and
                     report.get("reason") == "release_requested" and report.get("absence_verified") is True and
                     report.get("failure") is None and report.get("close_errors") == [] and
                     report.get("external_attachments") == 0 and report.get("targets") == [] and
                     report.get("restored") is None and report.get("restorations") == [],
                     "Absence monitoring did not complete with a verified PASS")


def phase17_empty_inventory_selftest(directory, authorization):
    """One focused suite: only read-only inventory and self-owned helper processes."""
    directory = local(directory)
    _phase17_require(directory / "result.json" == PHASE17_EMPTY_INVENTORY_PROOF,
                     "Use the admitted create-only empty-inventory proof destination")
    directory.mkdir(parents=True, exist_ok=False)
    suite_started = time.monotonic()
    suite_deadline = suite_started + 55
    report = {"result": "INCONCLUSIVE", "recorded_utc": utc(), "helper": identity(__file__), "cases": [],
              "source_inputs": _phase17_proof_inputs(), "suite_seconds_cap": 55,
              "scope": "Focused absence backend and routing guards; no RBS or external process mutation"}
    try:
        authorization = _phase17_authorization(authorization)
        platform, final_authorization = _phase17_platform(), _phase17_empty_authorization()
        report.update({"authorization": authorization, "platform": platform,
                       "final_authorization": final_authorization,
                       "nonempty_backend_proof": _phase17_current_nonempty_proof(authorization, platform)})
        historical = ROOT / "logs/pre_editor/phase-17/candidate-08/runtime-Debug/desktop-envelope.json"
        paths = {os.path.normcase(os.path.abspath(row["process"]))
                 for row in json.loads(historical.read_text())["targets"] if row["class"] == "mininews"}
        _phase17_require(len(paths) == 1, "C08 does not identify exactly one interfering image path")
        historical_image, native = paths.pop(), _Phase17Native()
        image = _phase17_empty_fixture_image()
        report["historical_path_evidence"] = {"path": str(historical), **identity(historical)}
        report["real_C08_inventory_observation"] = _phase17_absence_inventory(native, historical_image)
        report["fixture"] = {"role": "TEST_ONLY_EXPECTED_IMAGE", "expected_image": image,
                             "file_created": False, "claims_real_C08_absence": False,
                             "meaning": "Exercise the exact path filter and monitored-empty branch without requiring the real desktop to be empty"}
        report["error_attribution_cases"] = _phase17_inventory_error_selftest()
        _phase17_require(_phase17_absence_inventory(native, image) == [], "Focused sentinel image unexpectedly matched a window")
        for mode in ("explicit_close", "appearance", "query_failure", "guardian_loss", "parent_exit", "deadline"):
            _phase17_require(time.monotonic() < suite_deadline - 5, "Focused suite work deadline elapsed")
            case = directory / mode
            case.mkdir()
            guardian = owner = parent = None
            with (case / "helpers.log").open("xb") as stream:
                try:
                    parent_pid = os.getpid()
                    if mode == "parent_exit":
                        owner_dir = case / "owner"
                        owner_dir.mkdir()
                        save(owner_dir / "request.json", {"deadline": min(time.monotonic() + 30, suite_deadline - 5)})
                        owner = _phase17_launch("_phase17-heartbeat", owner_dir / "request.json", stream)
                        parent_pid = owner.pid
                    parent = native.open(parent_pid)
                    started = time.monotonic()
                    request = _phase17_request(case, [], native.process(parent), min(started + 45, suite_deadline - 5), authorization, platform,
                        release_deadline=min(started + (3 if mode == "deadline" else 12), suite_deadline - 5), image=image, inventory=[],
                        lease_mode="ABSENCE_MONITORED", final_authorization=final_authorization, focused_check=mode,
                        expected_image_role="TEST_ONLY_EXPECTED_IMAGE")
                    guardian = _phase17_launch("_phase17-empty-guardian", case / "request.json", stream)
                    lease = _Phase17Lease(case, guardian, request)
                    _phase17_wait_ready(case, guardian, min(started + 8, request["release_deadline"]))
                    ready = json.loads((case / "ready.json").read_text())
                    _phase17_require(ready["lease_mode"] == "ABSENCE_MONITORED" and ready["targets"] == [] and
                                     ready["external_attachments"] == 0 and ready["initial_inventory_empty"] is True and
                                     ready["initial_events_held"] is False and ready["no_kill_policy_armed"] is False,
                                     "Absence readiness falsely claims target isolation")
                    time.sleep(.35)
                    lease.assert_healthy()
                    heartbeat = json.loads((case / "heartbeat.json").read_text())
                    _phase17_require(heartbeat["inventory_samples"] >= 2, "Absence monitoring did not sample after readiness")
                    if mode in ("appearance", "query_failure"):
                        save(case / "inject.request", {"mode": mode, "recorded_utc": utc(), "deliberate_focused_fault": True})
                    elif mode == "guardian_loss":
                        guardian.kill()  # Only this case's own disposable guardian.
                    elif mode == "parent_exit":
                        owner.kill()  # Only this case's own surrogate parent.
                        owner.wait(timeout=max(0, min(3, suite_deadline - time.monotonic())))
                    elif mode == "explicit_close":
                        save(case / "release.request", {"recorded_utc": utc()})
                    guardian.wait(timeout=max(0, min(request["release_deadline"] - time.monotonic() + 4,
                                                    suite_deadline - time.monotonic())))
                    if mode == "guardian_loss":
                        _phase17_require(guardian.returncode != 0 and not (case / "exit.json").exists() and
                                         not (case / "failure.json").exists(), "Guardian-loss injection did not reach its expected boundary")
                        exited = {"result": "INCONCLUSIVE", "reason": "Guardian exited without restoration evidence"}
                    else:
                        exited = json.loads((case / "exit.json").read_text())
                        _phase17_require(exited["lease_mode"] == "ABSENCE_MONITORED" and exited["targets"] == [] and
                                         exited["external_attachments"] == 0 and exited["restored"] is None and
                                         exited["restorations"] == [] and exited["close_errors"] == [],
                                         "Absence exit claims external target restoration or reports cleanup failure")
                        expected_reason = {"explicit_close": "release_requested", "appearance": "inventory_failure",
                            "query_failure": "inventory_failure", "parent_exit": "parent_exit", "deadline": "deadline"}[mode]
                        _phase17_require(exited["reason"] == expected_reason, "Unexpected focused guardian exit reason")
                        if mode in ("appearance", "query_failure"):
                            failure_record = json.loads((case / "failure.json").read_text())
                            _phase17_require(failure_record["error"] == exited["failure"] and
                                             failure_record["details"] == exited["failure_details"],
                                             "Focused failure evidence differs from guardian result")
                            if mode == "appearance":
                                deliberate = exited["failure"] == "RuntimeError('Identity-matching window appeared during absence monitoring')"
                            else:
                                message = exited["failure_details"]["message"]
                                prefix = "Absence inventory query failed: "
                                _phase17_require(message.startswith(prefix), "Focused query error lost inventory context")
                                failures = json.loads(message[len(prefix):])
                                deliberate = (len(failures) == 1 and failures[0]["native"]["api"] == "GetClassNameW" and
                                              failures[0]["native"]["last_error"] == 5 and
                                              failures[0]["native"]["deliberate_focused_fault"] is True)
                            _phase17_require(deliberate,
                                             "Focused inventory failure was not the exact deliberate fault")
                        else:
                            _phase17_require(exited["failure"] is None and not (case / "failure.json").exists(),
                                             "Unexpected focused absence failure")
                    if mode == "explicit_close":
                        _phase17_empty_completion(exited, guardian.returncode)
                    else:
                        _phase17_require(exited["result"] == "INCONCLUSIVE" and guardian.returncode != 0,
                                         "A failed absence lease was accepted")
                        for check in (lease.assert_healthy, lambda: _phase17_empty_completion(exited, guardian.returncode)):
                            rejected = False
                            try:
                                check()
                            except RuntimeError:
                                rejected = True
                            _phase17_require(rejected, "Lost/failed absence guardian was accepted by a lease guard")
                    _phase17_require(_phase17_absence_inventory(native, image) == [],
                                     "Focused sentinel image acquired an unexpected window")
                    report["cases"].append({"mode": mode, "result": "PASS", "guardian_pid": guardian.pid,
                        "surrogate_parent_pid": owner.pid if owner else None, "guardian_exit": guardian.returncode,
                        "observed_result": exited["result"], "observed_reason": exited["reason"],
                        "external_attachments": 0, "request": identity(case / "request.json"),
                        "exit_record": identity(case / "exit.json") if (case / "exit.json").exists() else None})
                finally:
                    for process in (guardian, owner):
                        if process is not None and process.poll() is None:
                            process.kill()
                            process.wait(timeout=max(0, min(3, suite_deadline - time.monotonic())))
                    if parent:
                        native.close(parent)
        _phase17_require(identity(__file__) == report["helper"] and _phase17_platform() == platform and
                         report["source_inputs"] == _phase17_proof_inputs() and time.monotonic() <= suite_deadline,
                         "Focused suite inputs changed while running")
        report["result"] = "PASS"
    except BaseException as error:
        report["error"] = repr(error)
        raise
    finally:
        report["finished_utc"] = utc()
        report["seconds"] = time.monotonic() - suite_started
        save(directory / "result.json", report)
    return report


@contextmanager
def phase17_desktop_isolation(directory, seconds, authorization):
    """Amendment04 target lease or revision-authorized monitored absence."""
    started = time.monotonic()
    _phase17_require(35 < seconds <= 180, "Isolation needs its 30-second close reserve within the 180-second cap")
    authorization = _phase17_authorization(authorization)
    platform = _phase17_platform()
    proof = _phase17_isolation_proof(authorization, platform)
    directory = local(Path(directory) / "phase17-isolation")
    directory.mkdir(parents=True, exist_ok=False)
    native = _Phase17Native()
    historical = ROOT / "logs/pre_editor/phase-17/candidate-08/runtime-Debug/desktop-envelope.json"
    historical_rows = json.loads(historical.read_text())["targets"]
    paths = {os.path.normcase(os.path.abspath(row["process"])) for row in historical_rows if row["class"] == "mininews"}
    _phase17_require(len(paths) == 1, "C08 does not identify exactly one interfering image path")
    expected_image = paths.pop()
    inventory = native.windows(expected_image)
    absence = not inventory
    if absence:
        _phase17_require(_phase17_absence_inventory(native, expected_image) == [],
                         "Inventory changed before absence monitoring")
    pins, targets, guardian, lease, stream = [], [], None, None, None
    deadline = started + seconds
    try:
        for item in inventory:
            handle = native.open(item["pid"])
            pins.append((handle, item))
            _phase17_require(_phase17_same_process(native, handle, item), "Inventory process identity changed")
            _phase17_require(not native.debugged(handle), "Target already has a debugger")
            native.responsive(item)
            targets.append({**item, "file": identity(item["image"]), "baseline_debugged": False})
        parent = native.open(os.getpid())
        try:
            parent_identity = native.process(parent)
        finally:
            native.close(parent)
        request = _phase17_request(directory, targets, parent_identity, deadline, authorization, platform,
                                   release_deadline=deadline - 3,
                                   image=expected_image, inventory=inventory,
                                   historical_path_evidence={"path": str(historical), **identity(historical)},
                                   image_hash_provenance="Freshly pinned bytes; C08 did not record an image hash",
                                   surrogate_proof={"path": str(PHASE17_CURRENT_ISOLATION_PROOF), **identity(PHASE17_CURRENT_ISOLATION_PROOF)},
                                   focused_proof={"path": str(PHASE17_EMPTY_INVENTORY_PROOF), **identity(PHASE17_EMPTY_INVENTORY_PROOF)},
                                   final_authorization=proof["final_authorization"],
                                   lease_mode="ABSENCE_MONITORED" if absence else "DEBUGGER_ISOLATION")
        stream = (directory / "guardian.log").open("xb")
        guardian = _phase17_launch("_phase17-empty-guardian" if absence else "_phase17-guardian", directory / "request.json", stream)
        lease = _Phase17Lease(directory, guardian, request)
        _phase17_wait_ready(directory, guardian, min(deadline - 30, time.monotonic() + 15))
        for handle, item in pins:
            _phase17_require(_phase17_same_process(native, handle, item) and native.debugged(handle),
                             "Independent debug-lease verification failed")
        if absence:
            _phase17_require(_phase17_absence_inventory(native, expected_image) == [],
                             "Inventory changed before absence lease handoff")
        lease.assert_healthy()
        yield lease
        lease.assert_healthy()
    finally:
        final_error = None
        if guardian is not None:
            if lease.runtime is not None and lease.runtime.poll() is None:
                final_error = "Owned RBS remains alive when isolation context exits"
            try:
                save(directory / "release.request", {"recorded_utc": utc()})
                remaining = max(0, deadline - time.monotonic() - 5)
                guardian.wait(timeout=remaining)
            except BaseException as error:
                # This is our disposable guardian, never an external target. Its
                # thread exit invokes its primed no-kill detach policy if stalled.
                if guardian.poll() is None:
                    guardian.kill()
                    try:
                        guardian.wait(timeout=max(0, deadline - time.monotonic()))
                    except subprocess.TimeoutExpired:
                        final_error = "Guardian did not exit inside isolation allowance"
                final_error = "Guardian release failed: " + repr(error)
            if (directory / "exit.json").exists():
                lease.report = json.loads((directory / "exit.json").read_text())
            else:
                lease.report = {"result": "INCONCLUSIVE", "restored": False,
                                "reason": "Guardian exited without restoration evidence"}
            if absence:
                try:
                    _phase17_require(not (directory / "failure.json").exists(), "Absence guardian recorded failure")
                    _phase17_empty_completion(lease.report, guardian.returncode)
                    _phase17_require(_phase17_absence_inventory(native, expected_image) == [],
                                     "Inventory changed at independent absence completion")
                except BaseException as error:
                    final_error = final_error or repr(error)
            elif guardian.returncode != 0 or lease.report["result"] != "PASS" or not lease.report["restored"]:
                final_error = final_error or "Isolation restoration was not a verified PASS"
        independent = []
        for handle, item in pins:
            try:
                restored = _phase17_same_process(native, handle, item) and not native.debugged(handle) and native.responsive(item)
                independent.append({"pid": item["pid"], "restored": restored})
                if not restored:
                    final_error = "Independent target restoration verification failed"
            except BaseException as error:
                independent.append({"pid": item["pid"], "restored": False, "error": repr(error)})
                final_error = "Independent target restoration verification failed"
            finally:
                native.close(handle)
        if stream:
            stream.close()
        if lease is not None:
            if time.monotonic() > deadline:
                final_error = final_error or "Isolation restoration exceeded its absolute allowance"
            lease.report = {**(lease.report or {}), "independent_restorations": independent,
                            "context_seconds": time.monotonic() - started, "context_error": final_error}
            if final_error:
                lease.report["result"] = "INCONCLUSIVE"
            if absence:
                lease.report.update({"lease_mode": "ABSENCE_MONITORED", "restored": None,
                                     "restoration": "NOT_APPLICABLE_NO_EXTERNAL_STATE_CHANGED",
                                     "independent_absence_verified": final_error is None})
            else:
                lease.report["restored"] = bool(lease.report.get("restored")) and all(
                    row["restored"] for row in independent)
            save(directory / "restoration.json", lease.report)
        _phase17_require(final_error is None, final_error or "Isolation restoration failed")


def _phase17_heartbeat(request_path):
    directory = local(request_path).parent
    request = json.loads(local(request_path).read_text())
    with (directory / "progress.bin").open("xb", buffering=0) as stream:
        while time.monotonic() < request["deadline"] and not (directory / "stop.request").exists():
            stream.write(b".")
            time.sleep(.02)
    return 0


def phase17_isolation_selftest(directory, authorization):
    """Only disposable self-owned Python helpers; no RBS or external target."""
    directory = local(directory)
    _phase17_require(directory / "result.json" == PHASE17_CURRENT_ISOLATION_PROOF,
                     "Use the admitted create-only selftest evidence destination")
    directory.mkdir(parents=True, exist_ok=False)
    suite_started = time.monotonic()
    suite_deadline = suite_started + 55
    authorization = _phase17_authorization(authorization)
    platform = _phase17_platform()
    native = _Phase17Native()
    report = {"result": "INCONCLUSIVE", "recorded_utc": utc(), "helper": identity(__file__),
              "authorization": authorization, "platform": platform, "cases": [],
              "final_authorization": _phase17_empty_authorization(), "source_inputs": _phase17_proof_inputs(),
              "suite_seconds_cap": 55,
              "historical_proof": {"path": str(PHASE17_ISOLATION_PROOF), **identity(PHASE17_ISOLATION_PROOF)},
              "historical_proof_reused_for_current_helper": False}
    try:
        for mode in ("explicit_close", "holder_crash", "parent_exit", "deadline", "partial_setup"):
            _phase17_require(time.monotonic() < suite_deadline - 5, "Focused suite work deadline elapsed")
            case_row = {"mode": mode, "result": "INCONCLUSIVE"}
            report["cases"].append(case_row)
            case = directory / mode
            case.mkdir()
            heartbeat_dir = case / "heartbeat"
            heartbeat_dir.mkdir()
            save(heartbeat_dir / "request.json", {"deadline": min(time.monotonic() + 40, suite_deadline - 5)})
            heartbeat = second = owner = guardian = target_handle = second_handle = parent_handle = None
            with (case / "helpers.log").open("xb") as stream:
                try:
                    heartbeat = _phase17_launch("_phase17-heartbeat", heartbeat_dir / "request.json", stream)
                    progress = heartbeat_dir / "progress.bin"
                    end = min(time.monotonic() + 10, suite_deadline - 5)
                    while not progress.exists() or progress.stat().st_size < 3:
                        _phase17_require(heartbeat.poll() is None and time.monotonic() < end, "Surrogate heartbeat failed")
                        time.sleep(.025)
                    target_handle = native.open(heartbeat.pid)
                    target = native.process(target_handle)
                    _phase17_require(not native.debugged(target_handle), "Surrogate initially debugged")
                    target.update({"file": identity(target["image"]), "baseline_debugged": False})
                    targets = [target]
                    second_progress = None
                    if mode in ("explicit_close", "partial_setup"):
                        second_dir = case / "second-heartbeat"
                        second_dir.mkdir()
                        save(second_dir / "request.json", {"deadline": min(time.monotonic() + 30, suite_deadline - 5)})
                        second = _phase17_launch("_phase17-heartbeat", second_dir / "request.json", stream)
                        second_progress = second_dir / "progress.bin"
                        end = min(time.monotonic() + 10, suite_deadline - 5)
                        while not second_progress.exists() or second_progress.stat().st_size < 3:
                            _phase17_require(second.poll() is None and time.monotonic() < end, "Second surrogate heartbeat failed")
                            time.sleep(.025)
                        second_handle = native.open(second.pid)
                        second_target = native.process(second_handle)
                        second_target.update({"file": identity(second_target["image"]), "baseline_debugged": False})
                        targets.append(second_target)
                    case_row["targets"] = targets
                    parent_pid = os.getpid()
                    if mode == "parent_exit":
                        owner_dir = case / "owner"
                        owner_dir.mkdir()
                        save(owner_dir / "request.json", {"deadline": min(time.monotonic() + 30, suite_deadline - 5)})
                        owner = _phase17_launch("_phase17-heartbeat", owner_dir / "request.json", stream)
                        parent_pid = owner.pid
                    parent_handle = native.open(parent_pid)
                    case_started = time.monotonic()
                    request = _phase17_request(case, targets, native.process(parent_handle),
                                               min(case_started + (8 if mode == "deadline" else 15), suite_deadline - 5),
                                               authorization, platform, surrogate=True,
                                               release_deadline=min(case_started + (3 if mode == "deadline" else 12), suite_deadline - 5),
                                               surrogate_fail_after_attach=1 if mode == "partial_setup" else None)
                    guardian = _phase17_launch("_phase17-guardian", case / "request.json", stream)
                    if mode != "partial_setup":
                        _phase17_wait_ready(case, guardian, request["deadline"])
                        _phase17_require(native.debugged(target_handle), "Surrogate debug lease did not assert")
                        before = progress.stat().st_size
                        if second_handle:
                            _phase17_require(native.debugged(second_handle), "Second surrogate debug lease did not assert")
                            second_held = second_progress.stat().st_size
                        time.sleep(.25)
                        _phase17_require(progress.stat().st_size == before, "Held surrogate continued progressing")
                        if second_handle:
                            _phase17_require(second_progress.stat().st_size == second_held,
                                             "Held second surrogate continued progressing")
                        _phase17_require(guardian.poll() is None and not (case / "failure.json").exists(),
                                         "Surrogate guardian failed before the deliberate release/crash action")
                    else:
                        before = progress.stat().st_size
                    if mode == "holder_crash":
                        guardian.kill()  # Deliberate crash of our own primed debugger guardian.
                    elif mode == "parent_exit":
                        owner.kill()  # Deliberate loss of our own surrogate parent.
                        owner.wait(timeout=max(0, min(3, suite_deadline - time.monotonic())))
                    elif mode == "explicit_close":
                        save(case / "release.request", {"recorded_utc": utc()})
                    guardian.wait(timeout=max(0, min(request["deadline"] + 2 - time.monotonic(),
                                                    suite_deadline - time.monotonic())))
                    case_row["guardian_exit"] = guardian.returncode
                    end = min(time.monotonic() + 2, suite_deadline - 5)
                    while progress.stat().st_size <= before and time.monotonic() < end:
                        time.sleep(.025)
                    restored = _phase17_same_process(native, target_handle, target) and not native.debugged(target_handle)
                    case_row["initial_restoration"] = {"same_identity_and_not_debugged": restored,
                                                        "progress_before": before, "progress_after": progress.stat().st_size}
                    _phase17_require(restored and case_row["initial_restoration"]["progress_after"] > before,
                                     "Debugger detach failed to restore live surrogate progress")
                    # Observe continued operation beyond the first resumed tick,
                    # so a subsequently delivered attach breakpoint cannot pass.
                    resumed = progress.stat().st_size
                    second_resumed = second_progress.stat().st_size if second_handle else None
                    _phase17_require(time.monotonic() + 1 <= suite_deadline - 5, "Focused sustained-restoration deadline elapsed")
                    time.sleep(1)
                    sustained = native.alive(target_handle) and not native.debugged(target_handle) and progress.stat().st_size >= resumed + 10
                    case_row["sustained_restoration"] = {"passed": sustained, "progress_before": resumed,
                                                         "progress_after": progress.stat().st_size, "required_increase": 10}
                    _phase17_require(sustained, "Surrogate failed after initial detach progress")
                    if second_handle:
                        second_sustained = _phase17_same_process(native, second_handle, second_target) and not native.debugged(second_handle) and second_progress.stat().st_size >= second_resumed + 10
                        case_row["second_sustained_restoration"] = {"passed": second_sustained,
                                "progress_before": second_resumed, "progress_after": second_progress.stat().st_size, "required_increase": 10}
                        _phase17_require(second_sustained, "Second surrogate failed sustained progress/restoration")
                    if mode != "holder_crash":
                        exited = json.loads((case / "exit.json").read_text())
                        expected = {"explicit_close": "release_requested", "parent_exit": "parent_exit",
                                    "deadline": "deadline", "partial_setup": "setup_failure"}[mode]
                        case_row["guardian_report"] = {"path": str(case / "exit.json"), **identity(case / "exit.json"),
                                "expected_reason": expected, "reason": exited["reason"], "restored": exited["restored"],
                                "result": exited["result"], "failure": exited.get("failure")}
                        _phase17_require(exited["reason"] == expected and exited["restored"], "Unexpected guardian release path")
                        if mode == "partial_setup":
                            _phase17_require(exited.get("failure") == "RuntimeError('Deliberate surrogate partial-setup failure')" and
                                             exited["result"] == "INCONCLUSIVE" and guardian.returncode == 1,
                                             "Partial setup did not reach the admitted injection point")
                        else:
                            _phase17_require(exited.get("failure") is None and not (case / "failure.json").exists(),
                                             "Surrogate guardian detected an unexpected isolation failure")
                            expected_result = "PASS" if mode == "explicit_close" else "INCONCLUSIVE"
                            expected_exit = 0 if mode == "explicit_close" else 1
                            _phase17_require(exited["result"] == expected_result and guardian.returncode == expected_exit,
                                             "Unexpected surrogate guardian result/exit code")
                    else:
                        _phase17_require(not (case / "failure.json").exists(),
                                         "Crash surrogate guardian detected an unexpected isolation failure")
                    case_row.update({"result": "PASS", "restored": restored,
                                     "guardian_exit": guardian.returncode, "progress_before": before,
                                     "progress_after": progress.stat().st_size, "target_count": len(targets),
                                     "second_progress_before": second_resumed,
                                     "second_progress_after": second_progress.stat().st_size if second_handle else None})
                except BaseException as error:
                    case_row["error"] = repr(error)
                    raise
                finally:
                    # Only Popen objects created in this exact surrogate case can
                    # be terminated here. External isolation never enters this code.
                    for process in (guardian, owner, heartbeat, second):
                        if process is not None and process.poll() is None:
                            process.kill()
                            process.wait(timeout=max(0, min(3, suite_deadline - time.monotonic())))
                    for handle in (target_handle, second_handle, parent_handle):
                        if handle:
                            native.close(handle)
        _phase17_require(identity(__file__) == report["helper"] and _phase17_platform() == platform and
                         report["source_inputs"] == _phase17_proof_inputs() and time.monotonic() <= suite_deadline,
                         "Focused nonempty suite inputs changed or time limit elapsed")
        report["result"] = "PASS"
    except BaseException as error:
        report["error"] = repr(error)
        raise
    finally:
        report["finished_utc"] = utc()
        report["seconds"] = time.monotonic() - suite_started
        save(directory / "result.json", report)
    return report


if __name__=="__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "_phase17-empty-guardian":
        raise SystemExit(_phase17_empty_guardian(sys.argv[2]))
    if len(sys.argv) > 1 and sys.argv[1] == "phase17-empty-inventory-selftest":
        empty_parser = argparse.ArgumentParser()
        empty_parser.add_argument("operation", choices=["phase17-empty-inventory-selftest"])
        empty_parser.add_argument("--directory", required=True)
        empty_parser.add_argument("--authorization", required=True)
        empty_args = empty_parser.parse_args()
        print(json.dumps(phase17_empty_inventory_selftest(empty_args.directory, empty_args.authorization)), flush=True)
        raise SystemExit(0)
    if len(sys.argv) == 3 and sys.argv[1] == "_phase17-bootstrap":
        raise SystemExit(0)
    if len(sys.argv) == 3 and sys.argv[1] in ("_phase17-guardian", "_phase17-heartbeat"):
        operation = _phase17_guardian if sys.argv[1] == "_phase17-guardian" else _phase17_heartbeat
        raise SystemExit(operation(sys.argv[2]))
    if len(sys.argv) > 1 and sys.argv[1] == "phase17-isolation-selftest":
        isolation_parser = argparse.ArgumentParser()
        isolation_parser.add_argument("operation", choices=["phase17-isolation-selftest"])
        isolation_parser.add_argument("--directory", required=True)
        isolation_parser.add_argument("--authorization", required=True)
        isolation_args = isolation_parser.parse_args()
        print(json.dumps(phase17_isolation_selftest(isolation_args.directory, isolation_args.authorization)), flush=True)
        raise SystemExit(0)
    parser=argparse.ArgumentParser()
    parser.add_argument("operation",choices=["prepare","verify","build","stage","run","diagnose"])
    parser.add_argument("--candidate")
    parser.add_argument("--reuse-engine")
    parser.add_argument("--reuse-build")
    parser.add_argument("--configuration",choices=["Debug","Release"],default="Release")
    args=parser.parse_args()
    if args.operation=="diagnose":
        diagnose()
        raise SystemExit(0)
    assert args.candidate, "Candidate is required for campaign operations"
    assert len(args.candidate)==2 and args.candidate.isdigit()
    output=local(BASE/("candidate-"+args.candidate))
    if args.operation=="prepare": prepare(output,args.candidate,args.reuse_engine,args.reuse_build)
    elif args.operation=="verify": verify(output)
    else: globals()[args.operation](output,args.configuration)
