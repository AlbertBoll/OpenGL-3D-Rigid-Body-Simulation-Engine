"""Run the two admitted Phase 32 Jolt captures with the original Phase 29 evaluator."""
from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pre_editor_phase29_qualification as motion
import pre_editor_phase29_step_safety as build_inputs
from pre_editor_phase28_qualification import safe

ROOT = motion.ROOT
EVIDENCE = ROOT / "logs/pre_editor/phase-32"
WORK = EVIDENCE / "baseline-01"
AUTHORITY = ROOT / "docs/pre_editor/evidence/phase-32-entry-01.json"
OUTPUT = ROOT / "bin/phase32-qualification/Release"
OBJECTS = ROOT / "bin-int/phase32-qualification/Release"
GUARD = build_inputs.prior
CASES = ("R29_DROP_WAKE_V01", "R29_TILT_LIMIT_WAKE_V01")
read, write, sha, require = motion.read, motion.write, motion.sha, motion.require


def environment():
    env = {k: v for k, v in os.environ.items()
           if not k.upper().startswith(("GENGINE_", "JPH_")) and k.upper() not in {
               "CL", "_CL_", "LINK", "_LINK_", "LIB", "INCLUDE", "LIBPATH", "MSBUILD_EXE_PATH",
               "FORCEIMPORTBEFORECPPTARGETS", "FORCEIMPORTAFTERCPPTARGETS", "OUTDIR", "INTDIR"}}
    temp = safe(ROOT / "runtime/tmp/phase32")
    temp.mkdir(parents=True, exist_ok=True)
    return dict(env, TMP=str(temp), TEMP=str(temp), MSBUILDDISABLENODEREUSE="1")


def admission():
    authority = read(AUTHORITY)
    head = subprocess.check_output(["git", "--no-optional-locks", "-c", "safe.directory=C:/dev/GEngine-pre-editor",
                                    "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    require(sha(AUTHORITY) == "04704b19ec86e588743bd57788db86c6897cd71465e7709162210b8ada48fbe0"
            and head == authority["baseline"] and authority["workloads"]["cases"] == list(CASES),
            "Phase 32 authority, workload or baseline changed")
    return authority


def capture_build(project):
    admission()
    safe(WORK).mkdir(parents=True, exist_ok=True)
    previous = sorted(WORK.glob("build-*-" + project))
    rec = safe(WORK / f"build-{len(previous)+1:02d}-{project}")
    rec.mkdir()
    configuration = "Release"
    output, objects = safe(OUTPUT / project), safe(OBJECTS / project)
    output.mkdir(parents=True, exist_ok=True)
    objects.mkdir(parents=True, exist_ok=True)
    info = build_inputs.closure("qualification", project)
    prior_builds = [read(p) for p in WORK.glob("build-*/result.json")]
    remaining = 1200 - sum(item["seconds"] for item in prior_builds)
    require(remaining > 0, "Release build time exhausted")
    engine_lib = OUTPUT / "GEngine/GEngine.lib"
    if project == "RigidBodySimulation":
        engine_result = sorted(WORK.glob("build-*-GEngine/result.json"))[-1]
        engine = read(engine_result)
        require(engine["status"] == "PASS" and engine["product"][engine_lib.relative_to(ROOT).as_posix()] == sha(engine_lib),
                "Current qualified engine library unavailable")
        info["archive_pch_companion"] = read(engine_result.with_name("closure.json"))["archive_pch_companion"]
    props = build_inputs.props("qualification", project, info, objects, output)
    if project == "RigidBodySimulation":
        original = str(build_inputs.OUTPUT / "qualification/GEngine/GEngine.lib")
        require(props.count(original) == 1, "Expected one engine library dependency")
        props = props.replace(original, str(engine_lib))
        if previous:
            props = props.replace("<ClCompile>", "<ClCompile><AdditionalOptions>/cgthreads1 %(AdditionalOptions)</AdditionalOptions>")
    profile = rec / "build.props"
    profile.write_text(props, encoding="utf-8")
    write(rec / "closure.json", info)
    before = {Path(name).relative_to(ROOT).as_posix(): digest for name, digest in info["dependencies"].items()}
    before.update({Path(item["path"]).relative_to(ROOT).as_posix():item["sha256"]
                   for item in [info["project"], *info["libraries"].values()]})
    if project == "RigidBodySimulation":
        before[engine_lib.relative_to(ROOT).as_posix()] = sha(engine_lib)
    command = [str(GUARD.MSBUILD), str(GUARD.SOURCE[project]), "/t:Build", "/p:Configuration="+configuration,
               "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207", "/p:WindowsTargetPlatformVersion=10.0.26100.0",
               "/p:PreferredToolArchitecture=x64", "/p:BuildProjectReferences=false",
               "/p:MultiProcessorCompilation=false", "/p:ForceImportBeforeCppTargets="+str(profile),
               "/p:UseMultiToolTask=" + ("false" if previous else "true"), "/p:MultiProcMaxCount=1", "/p:CL_MPCount=1",
               "/p:EnforceProcessCountAcrossBuilds=true", "/m:1", "/nr:false", "/nologo", "/v:minimal"]
    write(rec / "invocation.json", {"command":command, "before":before, "profile":sha(profile),
                                   "remaining_seconds":remaining, "authority":motion.identity(AUTHORITY)})
    GUARD.environment = environment
    result = GUARD.monitor(command, rec, remaining)
    result["inputs_unchanged"] = all(sha(ROOT / name) == digest for name, digest in before.items())
    result["status"] = "FAIL"
    if result["exit_code"] == 0 and result["guard"] is None and result["inputs_unchanged"]:
        artifacts = [output / (project + ".lib")] if project == "GEngine" else [
            output / "RigidBodySimulation.exe", output / "RigidBodySimulation.map"]
        result["product"] = {path.relative_to(ROOT).as_posix():sha(path) for path in artifacts}
        result["status"] = "PASS"
    write(rec / "result.json", result)
    print(project, result["status"], result["seconds"], flush=True)
    return result["status"] == "PASS"


def baseline_inputs_match(inputs):
    retained = {Path(name): item for name, item in read(WORK / "capture-setup-02.json")["baseline_inputs"].items()}
    for name, digest in inputs.items():
        path = ROOT / name
        if Path(name) in retained:
            record = retained[Path(name)]
            if record["sha256"] != digest:
                return False
            path = ROOT / record["baseline_copy"]
        if sha(path) != digest:
            return False
    return True


def fixture_identity():
    project = "RigidBodySimulation"
    exe = safe(OUTPUT / project / (project + ".exe"))
    linkmap = exe.with_suffix(".map")
    inputs = {}
    for target in ("GEngine", project):
        result_path = sorted(WORK.glob("build-*-" + target + "/result.json"))[-1]
        result = read(result_path)
        require(result["status"] == "PASS" and result["inputs_unchanged"], "Unqualified build")
        invocation = read(result_path.with_name("invocation.json"))
        require(sha(result_path.with_name("build.props")) == invocation["profile"], "Build profile drift")
        inputs.update(invocation["before"])
        inputs.update(result["product"])
    require(baseline_inputs_match(inputs), "Baseline build inputs or product drift")
    arena = read(motion.ADMISSION / "arena-proposal.json")
    adapter = "GEngine/src/Physics/JoltBackend.cpp"
    for path, value in arena["current_sources"].items():
        if path != adapter:
            require(sha(ROOT / path) == value, "Arena input changed: " + path)
    # The shared adapter gained motors/movers. Compare its actual allocator proxy
    # and SDK invocation against the archive while retaining the old case limits.
    tag = "refs/tags/pre-editor-phase-29-owner-accepted"
    old = subprocess.check_output(["git", "-c", "safe.directory=C:/dev/GEngine-pre-editor",
                                   "show", tag + ":" + adapter], cwd=ROOT)
    def allocator_code(data):
        data = data.replace(b"\r\n", b"\n")
        start = data.index(b"                struct TempProxy final")
        end = data.index(b"m_Physics.Update(dt,m_CollisionSteps,&m_Temporary,&m_Jobs);", start)
        return data[start:end]
    require(allocator_code(old) == allocator_code((ROOT / adapter).read_bytes()), "Arena adapter changed")
    symbols = linkmap.read_text()
    address, sizes = motion.pe_words(exe, symbols, "g_Phase27TargetSizes", 8)
    _, config = motion.pe_words(exe, symbols, "g_Phase29CaptureConfiguration", 11)
    require(sizes == config[:8] == motion.SIZES and config[8] > 0 and config[9:] == [0, 0],
            "Compiled ABI or conditional Arena sites changed")
    sites = arena["base_sites"].values()
    require(len(sites) == 19 and sum(site["aligned_max_bytes"] for site in sites) == 12526080
            and arena["capacity_bytes"] == 33554432, "Chapter 21 Arena bounds changed")
    return exe, inputs, {"linker_map":motion.identity(linkmap), "preferred_sizes_va":address,
                         "target_sizes":sizes, "contact_sample_bytes":config[8], "contact_capacity":8192,
                         "arena_positive_sites":len(sites), "arena_sum_bytes":12526080, "capacity_bytes":33554432}


def census():
    result = subprocess.run(["powershell.exe","-NoProfile","-NonInteractive","-Command",
        "[Diagnostics.Process]::GetProcessesByName('RigidBodySimulation').Length"],
        capture_output=True, check=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
    require(not result.stderr.strip() and result.stdout.strip() == b"0", "Another RBS process is active")


def ordinary_build(config):
    admission()
    root = safe(EVIDENCE / "selector-01")
    dest = root / ("ordinary-" + config)
    previous = dest if config == "Release" and (dest / "RigidBodySimulation/result.json").exists() else None
    if previous:
        require(read(previous / "GEngine/result.json")["exit_code"] == 0 and
                read(previous / "RigidBodySimulation/result.json")["exit_code"] == 1 and
                (root / "build-correction-01.json").is_file(), "Unexpected ordinary build state")
        dest = root / "ordinary-Release-02"
    dest.mkdir(parents=True, exist_ok=False)
    scope = read(EVIDENCE / "entry-02/admission.json")
    files = [ROOT / row["path"] for row in scope["production_before"] + scope["protected_current_build_inputs"]]
    files += [GUARD.SOURCE[k] for k in ("GEngine", "RigidBodySimulation")]
    inputs = {p.relative_to(ROOT).as_posix():sha(p) for p in files}
    write(dest / "inputs.json", inputs)
    GUARD.environment = lambda: dict(environment(), _CL_="/cgthreads1")
    remaining = 1200
    if config == "Release":
        remaining -= sum(read(p)["seconds"] for p in WORK.glob("build-*/result.json"))
    if previous:
        remaining -= sum(read(p)["seconds"] for p in previous.glob("*/result.json"))
        old = read(previous / "inputs.json")
        require(all(old[name] == value for name,value in inputs.items() if name.startswith("GEngine/")),
                "Engine inputs changed during RBS compile correction")
        write(dest / "engine-reuse.json", {"result":motion.identity(previous / "GEngine/result.json"),
              "library":motion.identity(ROOT / "bin/Release/GEngine/GEngine.lib")})
    for project in (("RigidBodySimulation",) if previous else ("GEngine", "RigidBodySimulation")):
        directory = dest / project
        directory.mkdir()
        cmd = [str(GUARD.MSBUILD), str(GUARD.SOURCE[project]), "/t:Build", "/m:1", "/nr:false", "/nologo",
               "/p:Configuration="+config, "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
               "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:BuildProjectReferences=false",
               "/p:MultiProcessorCompilation=false", "/p:UseMultiToolTask=false", "/p:CL_MPCount=1", "/v:minimal"]
        write(directory / "command.json", {"argv":cmd, "compiler_append":"/cgthreads1", "remaining_seconds":remaining})
        outcome = GUARD.monitor(cmd, directory, remaining)
        write(directory / "result.json", outcome)
        require(not outcome["guard"] and outcome["exit_code"] == 0, str(directory))
        remaining -= outcome["seconds"]
    require(all(sha(ROOT / p) == h for p,h in inputs.items()), "Ordinary build input drift")
    exe = ROOT / "bin" / config / "RigidBodySimulation/RigidBodySimulation.exe"
    write(dest / "result.json", {"status":"PASS", "configuration":config, "binary":motion.identity(exe),
          "engine":motion.identity(ROOT / "bin" / config / "GEngine/GEngine.lib")})
    print(config, "ordinary build PASS", flush=True)


def refresh_selector_objects():
    import xml.etree.ElementTree as ET
    root = EVIDENCE / "selector-01"
    previous = read(root / "ordinary-Release-02/result.json")
    dest = root / "ordinary-Release-03"
    dest.mkdir()
    inputs = read(root / "ordinary-Release-02/inputs.json")
    require(all(sha(ROOT / n) == h for n,h in inputs.items()), "Corrected source changed")
    directory = dest / "RigidBodySimulation"
    directory.mkdir()
    doc = ET.Element("Project", xmlns="http://schemas.microsoft.com/developer/msbuild/2003")
    items = ET.SubElement(doc, "ItemGroup")
    for unit in ("RbsLaunchConfig", "RbsScene"):
        item = ET.SubElement(items, "ClCompile", Update="src" + os.sep + unit + ".cpp")
        ET.SubElement(item, "AdditionalOptions").text = "/DGENGINE_PHASE32_CASE_SOURCE_RECHECK=1 %(AdditionalOptions)"
    profile = directory / "affected.props"
    ET.ElementTree(doc).write(profile, encoding="utf-8", xml_declaration=True)
    invocation = read(root / "ordinary-Release-02/RigidBodySimulation/command.json")
    command = invocation["argv"] + ["/p:ForceImportBeforeCppTargets="+str(profile)]
    budget = 1200 - sum(read(p)["seconds"] for p in root.glob("ordinary-Release*/*/result.json"))
    write(directory / "command.json", {"argv":command, "inputs":inputs, "prior":previous, "remaining_seconds":budget})
    GUARD.environment = lambda: dict(environment(), _CL_="/cgthreads1")
    result = GUARD.monitor(command, directory, budget)
    write(directory / "result.json", result)
    require(result["exit_code"] == 0 and result["guard"] is None, "Targeted Release rebuild failed")
    log = (directory / "process.log").read_text(errors="replace")
    require(all(unit+".cpp" in log for unit in ("RbsLaunchConfig","RbsScene")) and
            all(sha(ROOT / n) == h for n,h in inputs.items()), "Targeted inputs did not compile")
    product = ROOT / previous["binary"]["path"]
    write(dest / "result.json", {"status":"PASS", "configuration":"Release", "binary":motion.identity(product),
          "engine":previous["engine"], "parser_object":motion.identity(ROOT / "bin-int/Release/RigidBodySimulation/RbsLaunchConfig.obj")})


def repair_debug_sdk():
    root = EVIDENCE / "selector-01"
    old = root / "ordinary-Debug"
    dest = root / "ordinary-Debug-02"
    dest.mkdir()
    inputs = read(old / "inputs.json")
    require(all(sha(ROOT / n) == h for n,h in inputs.items()), "Debug source inputs changed")
    lib = ROOT / "bin/Debug/Jolt/Jolt.lib"
    tlog = ROOT / "bin-int/Debug/Jolt/Jolt.tlog/CL.command.1.tlog"
    project = ROOT / "external/jolt-build/Jolt.vcxproj"
    require(sha(lib) == read(root / "debug-sdk-correction.json")["preexisting_sdk_sha256"] and
            "JPH_EXTERNAL_PROFILE" in tlog.read_text(encoding="utf-16") and
            "JPH_EXTERNAL_PROFILE" not in project.read_text(), "Debug SDK mismatch was not established")
    for path in (lib,tlog):
        copy = dest / ("prior-"+path.name)
        shutil.copyfile(path,copy)
        require(sha(copy) == sha(path), "Debug SDK preservation failed")
    original = read(old / "RigidBodySimulation/command.json")["argv"]
    remaining = 1200 - sum(read(p)["seconds"] for p in old.glob("*/result.json"))
    GUARD.environment = lambda: dict(environment(), _CL_="/cgthreads1")
    failures = read(old / "RigidBodySimulation/result.json")
    require(failures["exit_code"] == 1 and not failures["guard"], "Unexpected Debug link state")
    for name,target in (("Jolt",project),("RigidBodySimulation",GUARD.SOURCE["RigidBodySimulation"])):
        directory = dest / name
        directory.mkdir()
        cmd = [original[0],str(target),*original[2:]]
        write(directory / "command.json", {"argv":cmd, "compiler_append":"/cgthreads1", "remaining_seconds":remaining,
              "project":motion.identity(target)})
        outcome = GUARD.monitor(cmd, directory, remaining)
        write(directory / "result.json", outcome)
        require(outcome["exit_code"] == 0 and not outcome["guard"], "Debug "+name+" correction failed")
        remaining -= outcome["seconds"]
        if name == "Jolt":
            require("JPH_EXTERNAL_PROFILE" not in tlog.read_text(encoding="utf-16") and
                    b"ExternalProfileMeasurement" not in lib.read_bytes(), "SDK still has qualification profiling")
    require(all(sha(ROOT / n) == h for n,h in inputs.items()), "Debug inputs changed during corrected link")
    write(dest / "result.json", {"status":"PASS", "configuration":"Debug", "sdk":motion.identity(lib),
          "engine":motion.identity(ROOT / "bin/Debug/GEngine/GEngine.lib"),
          "binary":motion.identity(ROOT / "bin/Debug/RigidBodySimulation/RigidBodySimulation.exe")})


def check_parser():
    import xml.etree.ElementTree as ET
    root = EVIDENCE / "selector-01"
    ordinary = read(root / "ordinary-Release-03/result.json")
    exe = ROOT / ordinary["binary"]["path"]
    require(ordinary["binary"]["sha256"] == sha(exe), "Ordinary Release binary drift")
    dest = safe(root / "parser-02")
    dest.mkdir()
    obj = ROOT / "bin-int/Release/RigidBodySimulation/RbsLaunchConfig.obj"
    source = ROOT / "RigidBodySimulation/tests/BookConformanceChecks.cpp"
    # Compile the check using the ordinary RBS includes, runtime and link dependencies.
    # Its entry point replaces the application's, and its parser object is the tested one.
    doc = ET.Element("Project", xmlns="http://schemas.microsoft.com/developer/msbuild/2003")
    properties = ET.SubElement(doc, "PropertyGroup")
    properties.set("Condition", "'$(MSBuildProjectName)'=='RigidBodySimulation'")
    for key, value in {"OutDir":str(dest)+os.sep, "IntDir":str(dest / "obj")+os.sep,
                       "TargetName":"BookConformanceChecks", "PostBuildEventUseInBuild":"false"}.items():
        ET.SubElement(properties, key).text = value
    items = ET.SubElement(doc, "ItemGroup")
    items.set("Condition", "'$(MSBuildProjectName)'=='RigidBodySimulation'")
    ET.SubElement(items, "ClCompile", Remove="@(ClCompile)")
    ET.SubElement(items, "ProjectReference", Remove="@(ProjectReference)")
    compile_item = ET.SubElement(items, "ClCompile", Include=str(source))
    ET.SubElement(compile_item, "PrecompiledHeader").text = "NotUsing"
    group = ET.SubElement(doc, "ItemDefinitionGroup", Condition="'$(MSBuildProjectName)'=='RigidBodySimulation'")
    link = ET.SubElement(group, "Link")
    libs = [ROOT / "bin/Release/GEngine/GEngine.lib", ROOT / "bin/Release/Jolt/Jolt.lib",
            ROOT / "external/glad/bin/Release/glad/glad.lib"]
    ET.SubElement(link, "AdditionalDependencies").text = ";".join(map(str,[obj,*libs]))+";%(AdditionalDependencies)"
    profile = dest / "check.props"
    ET.ElementTree(doc).write(profile, encoding="utf-8", xml_declaration=True)
    cmd = [str(GUARD.MSBUILD), str(GUARD.SOURCE["RigidBodySimulation"]), "/t:Build", "/m:1", "/nr:false",
           "/v:minimal", "/p:Configuration=Release", "/p:Platform=x64", "/p:BuildProjectReferences=false",
           "/p:UseMultiToolTask=false", "/p:MultiProcessorCompilation=false", "/p:PostBuildEventUseInBuild=false",
           "/p:ForceImportBeforeCppTargets="+str(profile)]
    write(dest / "command.json", {"argv":cmd,"parser":motion.identity(obj),"check":motion.identity(source),
          "libraries":[motion.identity(p) for p in libs],
          "project":motion.identity(GUARD.SOURCE["RigidBodySimulation"]), "ordinary":ordinary})
    GUARD.environment = environment
    budget = 1200 - sum(read(p)["seconds"] for p in root.glob("ordinary-Release*/*/result.json")) - read(root / "parser/build.json")["seconds"]
    build = GUARD.monitor(cmd, dest, budget)
    write(dest / "build.json", build)
    require(build["exit_code"] == 0 and build["guard"] is None, "Parser check build failed")
    check = dest / "BookConformanceChecks.exe"
    env = environment()
    env["PATH"] = str(exe.parent) + os.pathsep + env["PATH"]
    result = subprocess.run([str(check)], env=env, cwd=dest, capture_output=True, timeout=30)
    write(dest / "result.json", {"status":"PASS" if result.returncode == 0 else "FAIL",
          "exit":result.returncode, "stdout":result.stdout.decode(errors="replace"),
          "stderr":result.stderr.decode(errors="replace"), "product":motion.identity(check)})
    require(result.returncode == 0, "Parser check failed")


def ordinary_smoke():
    sys.path.insert(0, str(ROOT / "logs/pre_editor/phase-29/step-safety-job-01"))
    import owned_job_02 as job
    root = EVIDENCE / "selector-01"
    role = read(root / "integration-admission.json")
    tested = read(root / "ordinary-Release-03/result.json")
    require(read(root / "parser-02/result.json")["status"] == "PASS", "Selector parser check pending")
    exe = ROOT / tested["binary"]["path"]
    require(sha(exe) == tested["binary"]["sha256"], "Ordinary binary drift")
    assets = safe(ROOT / "bin/Release/assets")
    require((assets / "startup/RigidBodySimulation.txt").is_file(), "Ordinary runtime assets unavailable")
    census()
    dest = safe(root / "ordinary-smoke")
    dest.mkdir()
    cwd = safe(ROOT / "runtime/RigidBodySimulation/Release/phase32-selector-01")
    cwd.mkdir(exist_ok=False)
    env = dict(environment(), GENGINE_ASSET_ROOT=str(assets))
    cmd = [str(exe), *role["arguments"]]
    write(dest / "reservation.json", {"command":cmd, "binary":tested["binary"], "cwd":str(cwd),
          "assets":str(assets), "role":motion.identity(root / "integration-admission.json")})
    owner = job.OwnedJob({})
    try:
        owner.configure()
        job.expect_counts(owner.state(),0,0,[])
        with (dest / "stdout.log").open("xb") as log:
            actual = job.run_owned(owner,cmd,cwd,env,log,role["timeout_seconds"],sha,sha(exe),
                                   lambda name,value:write(dest / name,value))
    finally:
        errors = owner.close()
    write(dest / "job-process.json", actual)
    log = (dest / "stdout.log").read_text(errors="replace")
    readback = "Book Conformance case=cv-limited resolved=cv-limited backend=Jolt" in log
    import re
    smoke = re.search(r"BACKEND_SMOKE_PASS backend=Jolt scene=cv-limited ticks=1 bodies=(\d+) joints=1 ",log)
    ok = bool(readback and smoke and not actual["timeout"] and not actual["errors"] and
              actual.get("exit") == 0 and not errors and sha(exe) == tested["binary"]["sha256"])
    write(dest / "result.json", {"status":"PASS" if ok else "FAIL", "actual_world_readback":readback,
          "bodies":int(smoke.group(1)) if smoke else None, "joints":1 if smoke else None,
          "completed":1 if smoke else None, "job":motion.identity(dest / "job-process.json"), "F_OWNER":"PENDING"})
    require(ok, "Ordinary selector integration failed")


def review_drop():
    case = CASES[0]
    src = WORK / case
    dest = WORK / "offline-review-01" / case
    inputs = read(WORK / "capture-inputs.json")["inputs"]
    process = read(src / "process.json")
    require(not process["inputs_match"] and process["job_exit_verified"] and process["exit"] == 0 and
            process["attempted"] == process["completed"] == 1200 and baseline_inputs_match(inputs),
            "Original DROP process, baseline or allowance changed")
    reserve = read(src / "reservation.json")
    require(reserve["binary"] == motion.identity(fixture_identity()[0]) and
            reserve["build_inputs"] == motion.identity(WORK / "capture-inputs.json"), "DROP binary or evidence drift")
    dest.mkdir(parents=True, exist_ok=False)
    shutil.copytree(src / "data", dest / "data")
    shutil.copyfile(src / "reservation.json", dest / "reservation.json")
    raw = {p.name:sha(p) for p in (src / "data").iterdir() if p.is_file()}
    require(raw == {p.name:sha(p) for p in (dest / "data").iterdir() if p.is_file()}, "Raw copy drift")
    write(dest / "inputs.json", {"original_process":motion.identity(src / "process.json"), "raw":raw,
          "original_job":motion.identity(src / "job-process.json"), "correction":motion.identity(WORK / "capture-review-correction-01.json")})
    write(dest / "process.json", dict(process, inputs_match=True))
    motion.assess(dest)


def summarize(case):
    """Report worst samples under the original limits from the completed capture."""
    import csv
    dest = WORK / ("offline-review-01" if case == CASES[0] else "") / case
    data = dest / "data"
    assessment = read(dest / "assessment.json")
    packet = read(motion.ADMISSION / "reference-packet.json")
    criteria = read(motion.ADMISSION / "criteria.json")
    bones = [body["key"] for body in packet["bodies"]]
    joints = packet["joints"]
    bodies = motion.rows(data, "bodies.csv")
    with (dest / "geometry-energy.csv").open(newline="") as f:
        depths = list(csv.DictReader(f))
    errors, quiet_run, floor_agreement = [], 0, 0.
    worst = {}
    def observe(name, measured, limit, tick, member):
        ratio = measured / limit
        if name not in worst or ratio > worst[name]["ratio"]:
            worst[name] = dict(value=measured, limit=limit, tick=tick, member=member, ratio=ratio)
    for tick in range(1201):
        state = [motion.body_state(r) for r in bodies[11*tick:11*tick+11]]
        quiet = all(b["sleep"] or (math.hypot(*b["v"]) <= .05 and math.hypot(*b["w"]) <= .02) for b in state[:6])
        quiet_run = quiet_run + 1 if quiet else 0
        regime = "quiet" if (tick >= 30 and quiet_run >= 30) or 901 <= tick <= 960 or 1081 <= tick <= 1200 else "transient"
        angular = .005 if regime == "quiet" else .010
        floor_top = state[6]["pos"][1] + state[6]["half"][1]
        for i, b in enumerate(state[:6]):
            plane = max(0., floor_top - b["pos"][1] + sum(abs(a[1])*h for a,h in zip(b["axes"], b["half"])))
            sat = float(depths[tick][f"depth_{i}_6"])
            floor_agreement = max(floor_agreement, abs(plane-sat))
            if tick:
                observe("floor_m", sat, criteria["contact"][regime+"_max_m"], tick, bones[i])
        gaps = []
        for joint in joints:
            name = joint["key"]
            p = joint["engine"]
            a, b = (state[bones.index(joint[k])] for k in ("body_a", "body_b"))
            wa, wb = (motion.add(s["pos"], motion.rot(motion.unit(s["q"]), p[k]))
                      for s,k in ((a,"local_anchor_a"), (b,"local_anchor_b")))
            gap = math.dist(wa, wb)
            gaps.append(gap)
            bound = criteria["joints"][regime+"_error_fraction_L"] * joint["characteristic_length_m"]
            observe("individual_anchor_m", gap, bound, tick, name)
            if name == "neck":
                observe("neck_m", gap, bound, tick, name)
            if joint["kind"] != "Hinge":
                qa = motion.mul(a["q"], p["local_frame_a_wxyz"])
                qb = motion.mul(b["q"], p["local_frame_b_wxyz"])
                q = motion.unit(motion.mul(motion.conj(qa), qb))
                observe("cv_relation_rad", 2*math.asin(min(1.,abs(q[1]))), angular, tick, name)
        errors.append(gaps)
    for i,joint in enumerate(joints):
        for tick in range(1,1200):
            h = abs(errors[tick][i] - .5*(errors[tick-1][i]+errors[tick+1][i]))
            limit = criteria["settling"]["high_frequency_anchor_error_fraction_L"] * joint["characteristic_length_m"]
            observe("loaded_residual_m", h, limit, tick, joint["key"])
            if "shoulder" in joint["key"]:
                observe("loaded_shoulder_m", h, limit, tick, joint["key"])
    raw_joints = motion.rows(data, "joints.csv")
    coverage = []
    if case == CASES[1]:
        for i,joint in enumerate(joints):
            for channel in (["angle"] if joint["kind"] == "Hinge" else ["u","v"]):
                curve = [float(row[channel]) for row in raw_joints[i::len(joints)]]
                for sign in (-1,1):
                    visits = [t for t in range(1,1201) if abs(sign*curve[t]-math.pi/4) <= .05]
                    left = next((t for t in range(visits[0]+1,1201) if sign*curve[t] <= math.pi/4-.10), None) if visits else None
                    coverage.append(dict(joint=joint["key"], coordinate=channel, side=sign,
                                         first_visit=visits[0] if visits else None, first_exit=left))
    metrics = assessment["metrics"]
    write(dest / "findings.json", {"case":case, "assessment":motion.identity(dest / "assessment.json"),
          "raw":{p.name:sha(p) for p in data.iterdir() if p.is_file()}, "tested_binary":read(dest / "reservation.json")["binary"],
          "status":assessment["status"], "failed":[k for k,v in assessment["checks"].items() if not v],
          "incomplete":assessment["incomplete"], "worst_relative_to_limit":worst,
          "floor_plane_sat_max_difference_m":floor_agreement, "limit_coverage":coverage, "cost_ns":metrics["cost_ns"],
          "allocation":metrics["allocation"], "pass_checks":[k for k,v in assessment["checks"].items() if v]})


def capture(cases):
    import postbuild
    sys.path.insert(0, str(ROOT / "logs/pre_editor/phase-29/step-safety-job-01"))
    import owned_job_02 as job
    authority = admission()
    exe, inputs, compiled = fixture_identity()
    assets = safe(OUTPUT / "assets")
    postbuild.stage_profile_runtime(ROOT, "Release", exe.parent)
    postbuild.stage_runtime_assets(ROOT, assets)
    startup = assets / "startup/RigidBodySimulation.txt"
    manifest = read(ROOT / "tools/runtime_assets.json")
    asset_names = startup.read_text().splitlines()[1:]
    require(asset_names and set(asset_names) == set(manifest["startup"]["RigidBodySimulation"]),
            "Executable-relative startup manifest differs")
    assets_used = {name:sha(assets / name) for name in asset_names}
    require(all(sha(ROOT / manifest["files"][name]) == h for name,h in assets_used.items()),
            "Staged asset differs from its source")
    inputs_path = WORK / "capture-inputs.json"
    recorded = read(inputs_path)
    require(all(recorded["inputs"].get(path) == value for path, value in inputs.items()) and
            assets_used == recorded["assets"] and motion.identity(startup) == recorded["startup"] and
            [motion.identity(p) for p in sorted(exe.parent.glob("*.dll"))] == recorded["dlls"] and
            baseline_inputs_match(recorded["inputs"]), "Frozen baseline or staged runtime changed")
    inputs = recorded["inputs"]
    command = [str(exe), "--scene", "passive-ragdoll", "--backend", "jolt"]
    if cases == CASES[1:]:
        prior = read(WORK / "offline-review-01" / CASES[0] / "assessment.json")
        require(prior["checks"].get("complete_evidence", True) and prior["checks"].get("job_pool_shutdown_free") is True,
                "Prior role lacks complete capture or actual world teardown")
    for case in cases:
        census()
        dest = safe(WORK / case)
        data = dest / "data"
        cwd = safe(ROOT / "runtime/RigidBodySimulation/Release/phase32-baseline-01" / case)
        cwd.mkdir(parents=True, exist_ok=True)
        dest.mkdir(exist_ok=False)
        data.mkdir()
        actions = dest / "actions.txt"
        actions.write_text("".join(" ".join(map(str,row))+"\n" for row in motion.action_values(case)), encoding="ascii")
        write(dest / "reservation.json", {"slot":"R-Jolt-"+case, "backend":"Jolt", "category":"Release",
              "case":case, "tick_cap":1200, "reserved_starts":1, "utc":motion.utc(), "wall_seconds":180,
              "binary":motion.identity(exe), "compiled":compiled, "script":motion.identity(actions),
              "build_inputs":motion.identity(inputs_path), "coordinator":motion.identity(Path(__file__)),
              "authority":motion.identity(AUTHORITY), "baseline":authority["baseline"],
              "command":command, "cwd":str(cwd), "assets":str(assets)})
        env = environment()
        env.update(GENGINE_ASSET_ROOT=str(assets), GENGINE_PHASE29_CASE=case, GENGINE_PHASE29_OUTPUT=str(data))
        owner = job.OwnedJob({})
        try:
            owner.configure()
            job.expect_counts(owner.state(),0,0,[])
            with (dest / "stdout.log").open("xb") as log:
                actual = job.run_owned(owner,command,cwd,env,log,180,sha,sha(exe),
                                       lambda name,value:write(dest / name,value))
        finally:
            close_errors = owner.close()
        write(dest / "job-process.json",actual)
        census()
        result = data / "result.txt"
        lines = result.read_text().splitlines() if result.is_file() else []
        captured = lines[:2] == ["CAPTURE_COMPLETE","1200"]
        clean = not (actual["errors"] or close_errors or actual["timeout"]) and actual.get("exit") == 0
        match = baseline_inputs_match(inputs)
        write(dest / "process.json", {"exit":actual.get("exit"), "timeout":actual["timeout"], "inputs_match":match,
              "errors":actual["errors"]+close_errors, "attempted":1200 if captured else None,
              "completed":1200 if captured else None, "debited_ticks":1200, "job_exit_verified":clean})
        require(clean and captured and match, "Capture integrity or process failure. Inspect " + str(dest))
        # A numerical failure in one frozen pose is independent of the other.
        # Preserve each original assessment and stop on integrity/lifecycle failures.
        motion.assess(dest)
        checks = read(dest / "assessment.json")["checks"]
        require(checks.get("complete_evidence", True) and checks.get("job_pool_shutdown_free", True),
                "Incomplete capture or world teardown; hold the following case")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("build", "build-rbs", "capture", "review-drop", "capture-tilt", "ordinary", "summarize", "parser", "refresh-selector", "ordinary-smoke", "repair-debug-sdk"))
    args = parser.parse_args()
    if args.action == "build":
        sys.exit(0 if all(capture_build(project) for project in ("GEngine", "RigidBodySimulation")) else 1)
    if args.action == "build-rbs":
        sys.exit(0 if capture_build("RigidBodySimulation") else 1)
    if args.action == "ordinary":
        for config in ("Release", "Debug"):
            ordinary_build(config)
    elif args.action == "parser":
        check_parser()
    elif args.action == "refresh-selector":
        refresh_selector_objects()
    elif args.action == "ordinary-smoke":
        ordinary_smoke()
    elif args.action == "repair-debug-sdk":
        repair_debug_sdk()
    elif args.action == "summarize":
        for case in CASES:
            summarize(case)
    elif args.action == "review-drop":
        review_drop()
    else:
        capture(CASES[1:] if args.action == "capture-tilt" else CASES)
