"""Phase 16 bounded RBS camera validation; create-only attempts, exact input guards."""
import argparse
import csv
import ctypes
from ctypes import wintypes
from contextlib import contextmanager
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
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


if __name__=="__main__":
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
