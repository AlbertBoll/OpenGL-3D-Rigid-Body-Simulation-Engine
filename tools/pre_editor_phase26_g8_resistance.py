"""Exact Owner-admitted G8 resistance evidence; no qualification or publication."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import time
import math
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("entry_helpers", ROOT / "tools/pre_editor_phase26_entry_cleanup.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
p = m.preserve
BASE = ROOT / "logs/pre_editor/phase-26/jolt-integration-01/g8-contact-resistance-01"
ENTRY = BASE / "owner-authorization-and-entry.json"
RECOVERY = BASE / "validation-recovery-01"
CPU = BASE / "cpu-attribution-01"
require, sha, read, write, create, copy = p.require, p.sha, p.read, p.write, p.create, p.copy_new


def admission(pristine=False):
    entry = read(ENTRY)
    require(p.git("rev-parse", "HEAD").strip() == entry["head"] and not p.git("branch", "--show-current").strip(), "HEAD/branch drift")
    require(p.git("ls-files", "--stage").strip() == entry["index"], "Logical Git index drift")
    require(sha(Path(p.git("rev-parse", "--git-path", "index").strip())) == entry["raw_index_sha256"], "Raw Git index drift")
    owned = set(entry["scope"]["pre_edit"])
    for name, expected in entry["source_inputs"].items():
        if name not in owned:
            require(sha(ROOT / name) == expected, "Frozen source changed: " + name)
    sdk = ROOT / "external/jolt"
    require(p.git("rev-parse", "HEAD", cwd=sdk).strip() == entry["sdk_commit"], "SDK upstream commit drift")
    require(sha(Path(p.git("rev-parse", "--path-format=absolute", "--git-path", "index", cwd=sdk).strip())) == entry["sdk_index_sha256"], "SDK index drift")
    sdk_owned = {n.removeprefix("external/jolt/") for n in entry["scope"]["sdk_production"]}
    original = read(BASE / "pristine-sdk-membership.json")
    for name, expected in original["present"].items():
        if pristine or name not in sdk_owned:
            require(sha(sdk / name) == expected, "Unexpected SDK change: " + name)
    changes = set(p.git("diff", "--name-only", "HEAD", cwd=sdk).splitlines()) | set(p.git("ls-files", "--others", "--exclude-standard", cwd=sdk).splitlines())
    require(not changes if pristine else changes <= sdk_owned, "SDK source scope mismatch")
    require(sha(ROOT / "docs/pre_editor/active/PHASE_26_JOLT_INTEGRATION_01/G8_RESISTANCE_FEASIBILITY_CLOSURE_01.md") == entry["review_sha256"], "Accepted proposal changed")
    return entry


def output(config, stage):
    return ROOT / f"bin/{config}/Phase26G8Resistance/{stage}"


def snapshot(directory, entry):
    names = set(entry["source_inputs"]) | set(entry["scope"]["pre_edit"])
    inputs = {n: sha(ROOT/n) for n in sorted(names) if (ROOT/n).is_file()}
    record = dict(utc=p.utc(), entry_sha256=sha(ENTRY), source_inputs=inputs,
                  sdk_upstream=entry["sdk_commit"], sdk_membership_sha256=sha(BASE/"pristine-sdk-membership.json"))
    write(directory/"inputs.json", record)
    for name in entry["scope"]["pre_edit"]:
        if (ROOT/name).is_file():
            copy(ROOT/name, directory/"source"/name)
    return record


def verify_snapshot(directory, runner_delta=None):
    frozen = read(directory/"inputs.json")
    require(sha(ENTRY) == frozen["entry_sha256"], "Admission changed")
    for name, expected in frozen["source_inputs"].items():
        if runner_delta and name == "tools/pre_editor_phase26_g8_resistance.py":
            require(expected == runner_delta["before_sha256"] and sha(ROOT/name) == runner_delta["after_sha256"], "Unmatched evidence-runner correction")
            continue
        require(sha(ROOT/name) == expected, "Build input changed: " + name)


def baseline_build():
    entry = admission(True)
    directory = create(BASE/"build-pristine-Release-01")
    out = create(output("Release", "pristine-01"))
    intermediate = create(ROOT/"bin-int/Release/Phase26G8Resistance/pristine-01")
    snapshot(directory, entry)
    prior = read(ROOT/"logs/pre_editor/phase-26/jolt-integration-01/main-entry-cpp23-refinement-01/build-Release-01/result.json")
    engine = ROOT/"bin/Release/Phase26EntryCleanup/01/GEngine/GEngine.lib"
    require(sha(engine) == prior["reused_engine_sha256"], "Accepted pristine GEngine identity mismatch")
    libraries = {"GEngine/GEngine.lib":engine, "Jolt/Jolt.lib":ROOT/"bin/Release/Jolt/Jolt.lib", "glad/glad.lib":ROOT/"external/glad/bin/Release/glad/glad.lib"}
    for target, source in libraries.items(): copy(source, out/target)
    write(directory/"pristine-library-inputs.json", p.identities(libraries.values()))
    props = directory/"isolated.props"
    with props.open("x", encoding="utf-8") as stream:
        stream.write(f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup><OutDir>{escape(out.as_posix())}/$(MSBuildProjectName)/</OutDir><IntDir>{escape(intermediate.as_posix())}/$(MSBuildProjectName)/</IntDir><PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>
<ItemDefinitionGroup Condition="'$(MSBuildProjectName)'=='RigidBodySimulation'"><ClCompile><PreprocessorDefinitions>GENGINE_G8_RESISTANCE_CHECKS;GENGINE_G8_RESISTANCE_PRISTINE;JPH_CROSS_PLATFORM_DETERMINISTIC;JPH_OBJECT_LAYER_BITS=16;JPH_NO_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalIncludeDirectories>{escape((ROOT/'external/jolt').as_posix())};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile></ItemDefinitionGroup>
</Project>''')
    command = [str(p.MSBUILD), str(ROOT/"RigidBodySimulation/RigidBodySimulation.vcxproj"), "/t:Build", "/p:Configuration=Release", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207", "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:BuildProjectReferences=false", f"/p:ForceImportBeforeCppTargets={props}", "/nr:false", "/m:2", "/nologo", "/v:normal"]
    result = p.execute(directory, command, "Release", 600, ROOT)
    result.update(passed=result["exit_code"]==0 and not result["timed_out"], inputs_sha256=sha(directory/"inputs.json"), warning_classification=m.warnings((directory/"stdout.log").read_text(errors="replace")))
    result["tlogs"] = p.identities(f for f in intermediate.rglob("*.tlog") if f.is_file())
    if result["passed"]:
        result["products"] = p.identities([out/"RigidBodySimulation/RigidBodySimulation.exe"]+[out/n for n in libraries])
        link_reads="\n".join(f.read_text(encoding="utf-16",errors="replace") for f in intermediate.rglob("link.read.*.tlog")).upper()
        result["exact_link_inputs"] = all(str(out/n).upper() in link_reads for n in libraries)
        result["passed"] &= result["exact_link_inputs"] and result["warning_classification"]["first_party"] == 0
        p.stage_runtime("Release",out)
        result["runtime_dlls"] = p.identities((out/"RigidBodySimulation").glob("*.dll"))
    verify_snapshot(directory);admission(True)
    result["preservation"]="PASS"
    write(directory/"result.json",result)
    print(json.dumps({k:result[k] for k in ("passed","exit_code","seconds","warning_classification")},indent=2))
    return 0 if result["passed"] else 1


def collect_baseline():
    admission(True)
    directory=BASE/"build-pristine-Release-01"
    recovery=BASE/"baseline-evidence-recovery-01"
    classification=read(recovery/"classification-before-edit.json")
    require(classification["result"]=="ADMITTED EVIDENCE-ONLY CORRECTION", "Missing recovery admission")
    require(sha(directory/"stdout.log")==classification["raw_build_log"], "Original build log changed")
    delta=dict(before_sha256=sha(recovery/"runner-before.py"),after_sha256=sha(Path(__file__)),
               classification_sha256=sha(recovery/"classification-before-edit.json"),scope="Evidence runner only; tlog file filter and prospective collection; no rebuild")
    verify_snapshot(directory,delta)
    write(recovery/"runner-delta.json",delta)
    log=(directory/"stdout.log").read_text(errors="replace")
    require("Build succeeded." in log and "0 Warning(s)" in log and "0 Error(s)" in log, "No completed successful textual MSBuild report")
    out=output("Release","pristine-01")
    intermediate=ROOT/"bin-int/Release/Phase26G8Resistance/pristine-01"
    libraries=[out/"GEngine/GEngine.lib",out/"Jolt/Jolt.lib",out/"glad/glad.lib"]
    before=read(directory/"pristine-library-inputs.json")
    p.check_identities(before)
    for product in libraries:
        require(product.is_file(),"Missing linked library")
        matches=[v for n,v in before.items() if Path(n).name==product.name]
        require(len(matches)==1 and sha(product)==matches[0]["sha256"],"Copied pristine library mismatch")
    link_reads="\n".join(f.read_text(encoding="utf-16",errors="replace") for f in intermediate.rglob("link.read.*.tlog")).upper()
    require(all(str(library).upper() in link_reads for library in libraries),"Actual link inputs not identified")
    warnings=m.warnings(log)
    require(warnings["first_party"]==0,"First-party warning gate")
    p.stage_runtime("Release",out)
    result=dict(utc=p.utc(),passed=True,result="PROSPECTIVE EXACT ARTIFACT ADMISSION; original collector FAIL retained",exit_code=None,
        exit_status="Original MSBuild process status was not persisted; textual success and exact artifacts verified separately",
        build_report="Build succeeded; 0 warnings; 0 errors; 175.22 seconds",new_builds=0,new_simulation_processes=0,
        warning_classification=warnings,products=p.identities([out/"RigidBodySimulation/RigidBodySimulation.exe"]+libraries),
        runtime_dlls=p.identities((out/"RigidBodySimulation").glob("*.dll")),tlogs=p.identities(f for f in intermediate.rglob("*.tlog") if f.is_file()),
        exact_link_inputs=True,inputs_sha256=sha(directory/"inputs.json"),runner_delta_sha256=sha(recovery/"runner-delta.json"),
        original_failure_sha256=sha(directory/"collector-failure.json"),original_log_sha256=sha(directory/"stdout.log"))
    verify_snapshot(directory,delta);admission(True)
    write(recovery/"result.json",result)
    print(json.dumps({k:result[k] for k in ("passed","result","exit_status","new_builds")},indent=2));return 0


def baseline_run():
    admission(True)
    result_path=BASE/"baseline-evidence-recovery-01/result.json"
    runner_delta=read(BASE/"baseline-evidence-recovery-01/runner-delta.json") if result_path.exists() else None
    if not result_path.exists():result_path=BASE/"build-pristine-Release-01/result.json"
    built = read(result_path)
    require(built["passed"],"Pristine build not admitted")
    p.check_identities(built["products"]);p.check_identities(built["runtime_dlls"])
    verify_snapshot(BASE/"build-pristine-Release-01",runner_delta)
    directory=create(BASE/"test-pristine-Release-01")
    exe=output("Release","pristine-01")/"RigidBodySimulation/RigidBodySimulation.exe"
    result=p.execute(directory,[str(exe),"--backend-checks"],"Release",180,directory)
    log=(directory/"stdout.log").read_text(errors="replace")
    ticks=re.findall(r"RESISTANCE_PRISTINE_PASS ticks=(\d+)",log)
    traces=[line for line in log.splitlines() if line.startswith("RESISTANCE_ZERO ")]
    result.update(passed=result["exit_code"]==0 and not result["timed_out"] and ticks==["168"] and len(traces)==121*181+49*2,
                  ticks=int(ticks[0]) if len(ticks)==1 else None,physical_trace_rows=len(traces),physical_trace_sha256=hashlib.sha256("\n".join(traces).encode()).hexdigest(),executable_sha256=sha(exe),build_result_sha256=sha(result_path),layout=re.findall(r"RESISTANCE_LAYOUT max_constraint_bytes=(\d+)",log))
    admission(True);verify_snapshot(BASE/"build-pristine-Release-01",runner_delta)
    write(directory/"result.json",result)
    print(json.dumps(result,indent=2));return 0 if result["passed"] else 1


def warning_classification(log):
    value=m.warnings(log)
    owned={str((ROOT/n).resolve()).lower() for n in read(ENTRY)["scope"]["sdk_production"]}
    for record in value["records"]:
        if record["where"].lower() in owned:
            record["owner"]="first-party-sdk-extension"
    value["counts"]={owner:sum(r["owner"]==owner for r in value["records"]) for owner in sorted({r["owner"] for r in value["records"]})}
    value["first_party"]=sum(r["owner"] not in ("third-party","vendor-instantiated-system-header") for r in value["records"])
    return value


def recovery_admission():
    entry=admission(False)
    record=read(RECOVERY/"authorization-preflight.json")
    delta=read(RECOVERY/"runner-delta.json")
    require(sha(RECOVERY/"authorization-preflight.json")==delta["authorization_sha256"],"Recovery authorization drift")
    require(sha(Path(__file__))==delta["after_sha256"] and record["runner_before_sha256"]==delta["before_sha256"],"Recovery runner drift")
    for name,expected in record["source_inputs"].items():
        if name!="tools/pre_editor_phase26_g8_resistance.py":
            require(sha(ROOT/name)==expected,"Recovery source drift: "+name)
    require(sha(ENTRY)==record["original_entry_sha256"],"Original entry drift")
    old=BASE/"build-patched-Debug-01"
    for name,key in (("inputs.json","original_build_inputs_sha256"),("result.json","original_build_result_sha256"),("stdout.log","original_build_log_sha256")):
        require(sha(old/name)==record[key],"Original failed build evidence changed: "+name)
    require(sha(BASE/"test-pristine-Release-01/result.json")==record["pristine_result_sha256"] and sha(BASE/"test-pristine-Release-01/stdout.log")==record["pristine_trace_sha256"],"Pristine baseline drift")
    p.check_identities(record["libraries"]);p.check_identities(record["dependencies"])
    return entry,record


def recover_debug_build():
    entry,record=recovery_admission()
    directory=create(BASE/"build-patched-Debug-recovery-01")
    out=create(output("Debug","patched-recovery-01"))
    intermediate=create(ROOT/"bin-int/Debug/Phase26G8Resistance/patched-recovery-01")
    snapshot(directory,entry)
    write(directory/"reused-libraries.json",record["libraries"])
    libraries=[Path(n) for n in record["libraries"]]
    props=directory/"isolated.props"
    with props.open("x",encoding="utf-8") as stream:
        stream.write(f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup><OutDir>{escape(out.as_posix())}/$(MSBuildProjectName)/</OutDir><IntDir>{escape(intermediate.as_posix())}/$(MSBuildProjectName)/</IntDir><PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>
<ItemGroup><ProjectReference Remove="@(ProjectReference)" /></ItemGroup>
<ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>GENGINE_G8_RESISTANCE_CHECKS;GENGINE_CHAIN_WAKE_CHECKS;GENGINE_MASS_INERTIA_CHECKS;JPH_CROSS_PLATFORM_DETERMINISTIC;JPH_OBJECT_LAYER_BITS=16;%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalIncludeDirectories>{escape((ROOT/'external/jolt').as_posix())};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile><Link><AdditionalDependencies>{';'.join(escape(v.as_posix()) for v in libraries)};%(AdditionalDependencies)</AdditionalDependencies></Link></ItemDefinitionGroup>
<Target Name="RbsRecoveryScopeGuard" BeforeTargets="PrepareForBuild"><Error Condition="'$(MSBuildProjectName)'!='RigidBodySimulation'" Text="Recovery may build only RBS." /><Error Condition="'@(ProjectReference)'!=''" Text="Recovery project references must be empty." /></Target>
</Project>''')
    command=[str(p.MSBUILD),str(ROOT/"RigidBodySimulation/RigidBodySimulation.vcxproj"),"/t:Build","/p:Configuration=Debug","/p:Platform=x64","/p:VCToolsVersion=14.44.35207","/p:WindowsTargetPlatformVersion=10.0.26100.0","/p:BuildProjectReferences=false",f"/p:ForceImportBeforeCppTargets={props}","/nr:false","/m:2","/nologo","/v:normal"]
    result=p.execute(directory,command,"Debug",1200,ROOT)
    write(directory/"process-result.json",result)
    log=(directory/"stdout.log").read_text(errors="replace")
    result.update(passed=result["exit_code"]==0 and not result["timed_out"],inputs_sha256=sha(directory/"inputs.json"),warning_classification=warning_classification(log))
    result["tlogs"]=p.identities(f for f in intermediate.rglob("*.tlog") if f.is_file())
    result["dependency_projects_built"]=re.findall(r'Project "[^"\r\n]*(?:GEngine|Jolt|glad)\.vcxproj"',log,re.IGNORECASE)
    result["compile_policy_match"]=all(flag in log for flag in ("/MTd","/W3","/std:c++23preview","GENGINE_G8_RESISTANCE_CHECKS","GENGINE_CHAIN_WAKE_CHECKS","GENGINE_MASS_INERTIA_CHECKS","JPH_CROSS_PLATFORM_DETERMINISTIC","JPH_OBJECT_LAYER_BITS=16"))
    result["passed"] &= not result["dependency_projects_built"] and result["compile_policy_match"] and result["warning_classification"]["first_party"]==0
    if result["passed"]:
        normalize=lambda s:s.replace("/","\\").upper()
        expected={normalize(str(v)) for v in libraries}
        names={v.name.upper() for v in libraries}
        reads="\n".join(f.read_text(encoding="utf-16",errors="replace") for f in intermediate.rglob("link.read.*.tlog"))
        actual={normalize(line.strip().strip('"')) for line in reads.splitlines() if Path(line.strip().strip('"')).name.upper() in names}
        commands="\n".join(f.read_text(encoding="utf-16",errors="replace") for f in intermediate.rglob("link.command.*.tlog"))
        result["actual_reused_link_inputs"]=sorted(actual)
        result["exact_link_inputs"]=actual==expected and all(v in normalize(commands) for v in expected)
        result["passed"] &= result["exact_link_inputs"]
        exe=out/"RigidBodySimulation/RigidBodySimulation.exe"
        result["executable"]=str(exe)
        result["products"]=p.identities([exe]+libraries)
        p.stage_runtime("Debug",out);result["runtime_dlls"]=p.identities((out/"RigidBodySimulation").glob("*.dll"))
    try:
        verify_snapshot(directory);recovery_admission()
        result["preservation"]="PASS"
    except (RuntimeError,OSError) as error:
        result["preservation"]=str(error);result["passed"]=False
    write(directory/"result.json",result)
    print(json.dumps(dict(passed=result["passed"],exit_code=result["exit_code"],seconds=result["seconds"],warnings=result["warning_classification"]["counts"],first_party_warnings=result["warning_classification"]["first_party"],preservation=result["preservation"],exact_link_inputs=result.get("exact_link_inputs")),indent=2))
    return 0 if result["passed"] else 1


def patched_build(config):
    entry,_=recovery_admission()
    require(config=="Release","Only the unused Release closure build is admitted; use recover-debug for the one RBS-only recovery")
    require(read(BASE/"test-pristine-Release-01/result.json")["passed"],"Pristine comparison baseline missing")
    if config=="Release":require(read(BASE/"test-patched-Debug-01/result.json")["passed"],"Stop after failed or incomplete Debug process")
    directory=create(BASE/f"build-patched-{config}-01")
    out=create(output(config,"patched-01"))
    intermediate=create(ROOT/f"bin-int/{config}/Phase26G8Resistance/patched-01")
    snapshot(directory,entry)
    p.PROFILE="mass-inertia"
    dependencies=p.identities(p.dependency_files())
    write(directory/"dependencies-before.json",dependencies)
    # GLAD is unchanged; copy the exact existing library and prevent its rebuild.
    glad=ROOT/f"external/glad/bin/{config}/glad/glad.lib";copy(glad,out/"glad/glad.lib")
    props=directory/"isolated.props"
    with props.open("x",encoding="utf-8") as stream:
        stream.write(f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup><OutDir>{escape(out.as_posix())}/$(MSBuildProjectName)/</OutDir><IntDir>{escape(intermediate.as_posix())}/$(MSBuildProjectName)/</IntDir><PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>
<ItemDefinitionGroup Condition="'$(MSBuildProjectName)'=='RigidBodySimulation' Or '$(MSBuildProjectName)'=='GEngine'"><ClCompile><PreprocessorDefinitions>GENGINE_G8_RESISTANCE_CHECKS;GENGINE_CHAIN_WAKE_CHECKS;GENGINE_MASS_INERTIA_CHECKS;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>
<ItemDefinitionGroup Condition="'$(MSBuildProjectName)'=='RigidBodySimulation'"><ClCompile><PreprocessorDefinitions>JPH_CROSS_PLATFORM_DETERMINISTIC;JPH_OBJECT_LAYER_BITS=16;{'JPH_NO_DEBUG;' if config=='Release' else ''}%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalIncludeDirectories>{escape((ROOT/'external/jolt').as_posix())};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile><Link><AdditionalDependencies>{escape((out/'glad/glad.lib').as_posix())};%(AdditionalDependencies)</AdditionalDependencies></Link></ItemDefinitionGroup>
<ItemDefinitionGroup Condition="'$(MSBuildProjectName)'=='Jolt'"><ClCompile><ExternalWarningLevel>Level3</ExternalWarningLevel></ClCompile></ItemDefinitionGroup>
<ItemGroup Condition="'$(MSBuildProjectName)'=='RigidBodySimulation'"><ProjectReference Update="../external/glad/glad.vcxproj"><BuildReference>false</BuildReference></ProjectReference></ItemGroup>
</Project>''')
    command=[str(p.MSBUILD),str(ROOT/"RigidBodySimulation/RigidBodySimulation.vcxproj"),"/t:Build",f"/p:Configuration={config}","/p:Platform=x64","/p:VCToolsVersion=14.44.35207","/p:WindowsTargetPlatformVersion=10.0.26100.0",f"/p:ForceImportBeforeCppTargets={props}","/nr:false","/m:2","/nologo","/v:normal"]
    result=p.execute(directory,command,config,1200,ROOT)
    result.update(passed=result["exit_code"]==0 and not result["timed_out"],inputs_sha256=sha(directory/"inputs.json"),warning_classification=warning_classification((directory/"stdout.log").read_text(errors="replace")))
    result["tlogs"]=p.identities(f for f in intermediate.rglob("*.tlog") if f.is_file())
    if result["passed"]:
        result["products"]=p.identities([out/"RigidBodySimulation/RigidBodySimulation.exe",out/"GEngine/GEngine.lib",out/"Jolt/Jolt.lib",out/"glad/glad.lib"])
        link_reads="\n".join(f.read_text(encoding="utf-16",errors="replace") for f in intermediate.rglob("link.read.*.tlog")).upper()
        result["exact_link_inputs"]=all(str(out/n).upper() in link_reads for n in ("GEngine/GEngine.lib","Jolt/Jolt.lib","glad/glad.lib"))
        result["passed"] &= result["exact_link_inputs"] and result["warning_classification"]["first_party"]==0
        p.stage_runtime(config,out);result["runtime_dlls"]=p.identities((out/"RigidBodySimulation").glob("*.dll"))
    try:
        verify_snapshot(directory);admission(False);p.check_identities(dependencies)
        result["preservation"]="PASS"
    except (RuntimeError,OSError) as error:
        result["preservation"]=str(error);result["passed"]=False
    write(directory/"result.json",result)
    print(json.dumps({k:result[k] for k in ("passed","exit_code","seconds","warning_classification","preservation")},indent=2))
    return 0 if result["passed"] else 1


def stats(values):
    ordered=sorted(values)
    if not ordered:return {}
    n=len(ordered)
    return dict(n=n,median=(ordered[(n-1)//2]+ordered[n//2])/2,p95=ordered[math.ceil(.95*n)-1],p99=ordered[math.ceil(.99*n)-1],maximum=ordered[-1])


def cpu_admission():
    entry=admission(False)
    auth=read(CPU/"authorization-preflight.json");frozen=read(CPU/"source-freeze.json")
    require(sha(CPU/"authorization-preflight.json")==frozen["authorization_sha256"],"CPU authority drift")
    require(sha(ENTRY)==auth["original_entry_sha256"],"Original extension authority drift")
    for name,expected in auth["source_inputs"].items():
        target=frozen["support_sources"][name] if name in auth["support_allowlist"] else expected
        require(sha(ROOT/name)==target,"CPU source drift: "+name)
    require(sha(ROOT/"docs/pre_editor/active/PHASE_26_JOLT_INTEGRATION_01/G8_CONTACT_RESISTANCE_VALIDATION_RECOVERY_REVIEW_01.md")==auth["accepted_review_sha256"],"Accepted review drift")
    require(sha(BASE/"build-patched-Release-01/result.json")==auth["prior_build_result_sha256"] and sha(RECOVERY/"manifest.json")==auth["prior_evidence_manifest_sha256"],"Preserved evidence drift")
    p.check_identities(auth["prior_release_products"]);p.check_identities(auth["dependencies"])
    return entry,auth


def cpu_build():
    entry,auth=cpu_admission()
    directory=create(CPU/"build-Release-01")
    out=create(output("Release","cpu-attribution-01"))
    intermediate=create(ROOT/"bin-int/Release/Phase26G8Resistance/cpu-attribution-01")
    snapshot(directory,entry);write(directory/"reused-libraries.json",auth["libraries"])
    libraries=[Path(n) for n in auth["libraries"]]
    props=directory/"isolated.props"
    with props.open("x",encoding="utf-8") as stream:
        stream.write(f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup><OutDir>{escape(out.as_posix())}/$(MSBuildProjectName)/</OutDir><IntDir>{escape(intermediate.as_posix())}/$(MSBuildProjectName)/</IntDir><PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>
<ItemGroup><ProjectReference Remove="@(ProjectReference)" /></ItemGroup>
<ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>GENGINE_G8_CPU_ATTRIBUTION_CHECKS;GENGINE_G8_RESISTANCE_CHECKS;GENGINE_CHAIN_WAKE_CHECKS;GENGINE_MASS_INERTIA_CHECKS;JPH_CROSS_PLATFORM_DETERMINISTIC;JPH_OBJECT_LAYER_BITS=16;JPH_NO_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalIncludeDirectories>{escape((ROOT/'external/jolt').as_posix())};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile><Link><AdditionalDependencies>{';'.join(escape(v.as_posix()) for v in libraries)};%(AdditionalDependencies)</AdditionalDependencies></Link></ItemDefinitionGroup>
<Target Name="RbsCpuScopeGuard" BeforeTargets="PrepareForBuild"><Error Condition="'$(MSBuildProjectName)'!='RigidBodySimulation'" Text="CPU attribution may build only RBS." /><Error Condition="'@(ProjectReference)'!=''" Text="CPU attribution project references must be empty." /></Target>
</Project>''')
    command=[str(p.MSBUILD),str(ROOT/"RigidBodySimulation/RigidBodySimulation.vcxproj"),"/t:Build","/p:Configuration=Release","/p:Platform=x64","/p:VCToolsVersion=14.44.35207","/p:WindowsTargetPlatformVersion=10.0.26100.0","/p:BuildProjectReferences=false",f"/p:ForceImportBeforeCppTargets={props}","/nr:false","/m:2","/nologo","/v:normal"]
    result=p.execute(directory,command,"Release",1200,ROOT);write(directory/"process-result.json",result)
    log=(directory/"stdout.log").read_text(errors="replace")
    result.update(passed=result["exit_code"]==0 and not result["timed_out"],inputs_sha256=sha(directory/"inputs.json"),warning_classification=warning_classification(log))
    result["tlogs"]=p.identities(f for f in intermediate.rglob("*.tlog") if f.is_file())
    result["dependency_projects_built"]=re.findall(r'Project "[^"\r\n]*(?:GEngine|Jolt|glad)\.vcxproj"',log,re.IGNORECASE)
    result["compile_policy_match"]=all(flag in log for flag in ("/MT ","/W3","/std:c++23preview","GENGINE_G8_CPU_ATTRIBUTION_CHECKS","JPH_CROSS_PLATFORM_DETERMINISTIC","JPH_OBJECT_LAYER_BITS=16","JPH_NO_DEBUG")) and "/MTd" not in log
    result["passed"] &= not result["dependency_projects_built"] and result["compile_policy_match"] and result["warning_classification"]["first_party"]==0
    if result["passed"]:
        normalize=lambda s:s.replace("/","\\").upper()
        expected={normalize(str(v)) for v in libraries};names={v.name.upper() for v in libraries}
        reads="\n".join(f.read_text(encoding="utf-16",errors="replace") for f in intermediate.rglob("link.read.*.tlog"))
        actual={normalize(line.strip().strip('"')) for line in reads.splitlines() if Path(line.strip().strip('"')).name.upper() in names}
        commands="\n".join(f.read_text(encoding="utf-16",errors="replace") for f in intermediate.rglob("link.command.*.tlog"))
        result["actual_reused_link_inputs"]=sorted(actual)
        result["exact_link_inputs"]=actual==expected and all(v in normalize(commands) for v in expected)
        result["passed"] &= result["exact_link_inputs"]
        exe=out/"RigidBodySimulation/RigidBodySimulation.exe";result["executable"]=str(exe)
        result["products"]=p.identities([exe]+libraries)
        p.stage_runtime("Release",out);result["runtime_dlls"]=p.identities((out/"RigidBodySimulation").glob("*.dll"))
    try:
        verify_snapshot(directory);cpu_admission();result["preservation"]="PASS"
    except (RuntimeError,OSError) as error:
        result["preservation"]=str(error);result["passed"]=False
    write(directory/"result.json",result)
    print(json.dumps(dict(passed=result["passed"],exit_code=result["exit_code"],seconds=result["seconds"],warnings=result["warning_classification"]["counts"],first_party_warnings=result["warning_classification"]["first_party"],exact_link_inputs=result.get("exact_link_inputs"),preservation=result["preservation"]),indent=2))
    return 0 if result["passed"] else 1


def cpu_run():
    cpu_admission();build=CPU/"build-Release-01";built=read(build/"result.json")
    require(built["passed"],"CPU executable not admitted");verify_snapshot(build)
    p.check_identities(built["products"]);p.check_identities(built["runtime_dlls"])
    directory=create(CPU/"run-Release-01");exe=Path(built["executable"])
    result=p.execute(directory,[str(exe),"--backend-checks"],"Release",180,directory)
    write(directory/"process-result.json",result)
    log=(directory/"stdout.log").read_text(errors="replace")
    samples=[dict(re.findall(r"(\w+)=([^ ]+)",line)) for line in log.splitlines() if line.startswith("CPU_ATTRIBUTION_SAMPLE ")]
    result["sample_rows"]=len(samples)
    result["reported_updates"]=re.findall(r"CPU_ATTRIBUTION_PROCESS_END updates=(\d+)",log)
    result["measurement_complete"]=re.findall(r"CPU_ATTRIBUTION_PASS updates=(\d+)",log)==["1824"] and result["reported_updates"]==["1824"] and len(samples)==1824
    required_fields={"substeps","arm","warmup","round","order","position","ns"}
    orders=("ABC","BCA","CAB","ACB","CBA","BAC")
    valid_samples=all(required_fields<=s.keys() and s["substeps"] in ("1","2","16") and s["arm"] in ("A","B","C") and s["warmup"] in ("0","1") and s["order"] in orders and all(re.fullmatch(r"\d+",s[k]) for k in ("round","position","ns")) and int(s["position"])<3 and s["order"][int(s["position"])]==s["arm"] for s in samples)
    if not valid_samples:
        result.update(passed=False,measurement_complete=False,data_integrity_error="Invalid sample fields, clock value or arm/order",executable_sha256=sha(exe))
        verify_snapshot(build);cpu_admission();write(directory/"result.json",result)
        print(json.dumps(result,indent=2));return 1
    groups={};paired={}
    for substeps,expected in ((1,128),(2,256),(16,128)):
        for arm in "ABC":
            values=[s for s in samples if s.get("substeps")==str(substeps) and s.get("arm")==arm]
            warm=[s for s in values if s.get("warmup")=="1"];measured=[s for s in values if s.get("warmup")=="0"]
            result["measurement_complete"] &= len(warm)==32 and {int(s["round"]) for s in warm}==set(range(32)) and len(measured)==expected and {int(s["round"]) for s in measured}==set(range(expected))
            times=[int(s["ns"]) for s in measured]
            summary=stats(times)
            if times:summary.update(minimum=min(times),p05=sorted(times)[math.ceil(.05*len(times))-1],p25=sorted(times)[math.ceil(.25*len(times))-1],p75=sorted(times)[math.ceil(.75*len(times))-1])
            groups[f"{substeps}:{arm}"]=dict(warmups=len(warm),measured=len(measured),elapsed_update_ns=summary)
        rounds={}
        for s in samples:
            if s.get("substeps")==str(substeps) and s.get("warmup")=="0":rounds.setdefault(int(s["round"]),{})[s["arm"]]=int(s["ns"])
        result["measurement_complete"] &= set(rounds)==set(range(expected)) and all(set(v)==set("ABC") for v in rounds.values())
        for warm,round_count in (("1",32),("0",expected)):
            for round_number in range(round_count):
                triplet=[s for s in samples if s["substeps"]==str(substeps) and s["warmup"]==warm and int(s["round"])==round_number]
                expected_order=orders[(round_number+(0 if warm=="1" else 32))%6]
                result["measurement_complete"] &= len(triplet)==3 and {s["arm"] for s in triplet}==set("ABC") and {int(s["position"]) for s in triplet}=={0,1,2} and all(s["order"]==expected_order for s in triplet)
        for left,right in (("B","A"),("C","B")):
            differences=[v[left]-v[right] for _,v in sorted(rounds.items()) if left in v and right in v]
            result["measurement_complete"] &= len(differences)==expected
            blocks=[stats(differences[i:i+expected//8])["median"] for i in range(0,len(differences),expected//8)] if differences else []
            paired[f"{substeps}:{left}-{right}"]=dict(elapsed_difference_ns=stats(differences),ordered_eight_block_medians_ns=blocks)
    result.update(passed=result["exit_code"]==0 and not result["timed_out"] and result["measurement_complete"],groups=groups,paired_differences=paired,executable_sha256=sha(exe),build_result_sha256=sha(build/"result.json"),performance_disposition="Requires evidence review; program success is not an automatic performance PASS",full_qualification="NOT RUN")
    verify_snapshot(build);cpu_admission();write(directory/"result.json",result)
    print(json.dumps({k:result[k] for k in ("passed","exit_code","seconds","sample_rows","reported_updates","groups","paired_differences","performance_disposition")},indent=2))
    return 0 if result["passed"] else 1


def patched_run(config,lifecycle=False):
    recovery_admission()
    if config=="Release":require(read(BASE/"test-patched-Debug-01/result.json")["passed"],"Debug gates not passed")
    if lifecycle:require(config=="Release" and read(BASE/"test-patched-Release-01/result.json")["passed"],"Release focused gates not passed")
    build_dir=BASE/("build-patched-Debug-recovery-01" if config=="Debug" else "build-patched-Release-01");built=read(build_dir/"result.json")
    require(built["passed"],"Patched build not admitted");p.check_identities(built["products"]);p.check_identities(built["runtime_dlls"]);verify_snapshot(build_dir)
    kind="lifecycle" if lifecycle else "test"
    directory=create(BASE/f"{kind}-patched-{config}-01")
    exe=Path(built["executable"]) if config=="Debug" else output(config,"patched-01")/"RigidBodySimulation/RigidBodySimulation.exe"
    command=[str(exe)]
    if lifecycle:command += ["--scene","distance-chain","--links","8","--backend","native","--physics-smoke-ticks","120","--backend-switch-checks","true"]
    else:command += ["--backend-checks"]
    env,removed=p.environment(config)
    env["GENGINE_G8_BASELINE_TRACE"]=str(BASE/"test-pristine-Release-01/stdout.log")
    write(directory/"command.json",dict(utc=p.utc(),command=command,cwd=str(directory),deadline_seconds=180,max_ticks=60 if lifecycle else 1600,baseline_trace=env["GENGINE_G8_BASELINE_TRACE"],removed_environment=removed))
    start=time.monotonic();result=dict(exit_code=None,timed_out=False)
    with (directory/"stdout.log").open("xb") as stream:
        proc=subprocess.Popen(command,cwd=directory,env=env,stdout=stream,stderr=subprocess.STDOUT);result["pid"]=proc.pid
        try:result["exit_code"]=proc.wait(timeout=180)
        except subprocess.TimeoutExpired:
            result["timed_out"]=True
            subprocess.run(["taskkill","/PID",str(proc.pid),"/T","/F"],stdout=stream,stderr=subprocess.STDOUT,timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
            result["exit_code"]=proc.wait(timeout=30)
    result["seconds"]=time.monotonic()-start
    log=(directory/"stdout.log").read_text(errors="replace")
    if lifecycle:
        ticks=re.findall(r"BACKEND_SWITCH_CHECKS_PASS[^\r\n]*total_ticks=(\d+)",log)
        passed=ticks==["60"]
        result["ticks"]=int(ticks[0]) if len(ticks)==1 else None
    else:
        completed=re.findall(r"RESISTANCE_PROCESS_END ticks=(\d+)",log)
        tokens=re.findall(r"RESISTANCE_CHECKS_PASS ticks=(\d+)",log)
        result["ticks"]=int(completed[0]) if len(completed)==1 else None
        passed=len(tokens)==1 and completed==tokens and 0<int(tokens[0])<=1600
        required=("RESISTANCE_NUMERICAL_PASS","RESISTANCE_ZERO_PASS","RESISTANCE_DIRECT_PASS","MASS_INERTIA_CHECKS_PASS","CHAIN_WAKE_CAPACITY_PASS","RESISTANCE_IMPULSE_PASS","RESISTANCE_G8_PREFIX_PASS")
        result["required_markers"]={token:token in log for token in required};passed &= all(result["required_markers"].values())
        result["wake_cases"]=re.findall(r"CHAIN_WAKE_301_PASS case=(C4|C8|C16|G8)\b",log);passed &= result["wake_cases"]==["C4","C8","C16","G8"]
        traces=[line for line in log.splitlines() if line.startswith("RESISTANCE_ZERO ")]
        result["physical_trace_sha256"]=hashlib.sha256("\n".join(traces).encode()).hexdigest()
        result["zero_comparison"]=result["physical_trace_sha256"]==read(BASE/"test-pristine-Release-01/result.json")["physical_trace_sha256"] if config=="Release" else "No matched pristine Debug binary; coverage only"
        if config=="Release":passed &= result["zero_comparison"]
        timing={}
        for case,ns in re.findall(r"RESISTANCE_CPU case=(\S+) tick=\d+ ns=(\d+)",log):timing.setdefault(case,[]).append(int(ns))
        result["step_cpu_ns"]={case:stats(values) for case,values in timing.items()}
        result["full_qualification"]="NOT RUN"
    result.update(passed=result["exit_code"]==0 and not result["timed_out"] and passed,executable_sha256=sha(exe),build_result_sha256=sha(build_dir/"result.json"))
    verify_snapshot(build_dir);recovery_admission()
    write(directory/"result.json",result)
    print(json.dumps(result,indent=2));return 0 if result["passed"] else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action",choices=("baseline-build","collect-baseline","baseline-test","recover-debug","build","test","lifecycle","cpu-build","cpu-run"))
    parser.add_argument("--config",choices=("Debug","Release"),default="Debug")
    args=parser.parse_args()
    if args.action in ("cpu-build","cpu-run"):
        require(args.config=="Release","CPU attribution is Release only")
        return cpu_build() if args.action=="cpu-build" else cpu_run()
    if args.action=="recover-debug":
        require(args.config=="Debug","Recovery is Debug only")
        return recover_debug_build()
    if args.action=="collect-baseline":return collect_baseline()
    if args.action=="build":return patched_build(args.config)
    if args.action in ("test","lifecycle"):return patched_run(args.config,args.action=="lifecycle")
    return baseline_build() if args.action=="baseline-build" else baseline_run()


if __name__=="__main__":
    try: raise SystemExit(main())
    except (RuntimeError,OSError,subprocess.SubprocessError) as error:
        print("G8_RESISTANCE_RUNNER_STOP:",error)
        raise SystemExit(1)
