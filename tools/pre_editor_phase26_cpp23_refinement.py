"""Bounded RBS startup refinement checks; preserve the previous cleanup evidence."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("entry_cleanup", ROOT / "tools/pre_editor_phase26_entry_cleanup.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
PRIOR = m.BASE
m.BASE = ROOT / "logs/pre_editor/phase-26/jolt-integration-01/main-entry-cpp23-refinement-01"
m.ENTRY = m.BASE / "entry-and-scope.json"
BASE = m.BASE
original_admission = m.admission


def admission():
    entry = original_admission()
    index = Path(m.preserve.git("rev-parse", "--git-path", "index").strip())
    m.require(m.sha(index) == entry["raw_index_sha256"], "Raw Git index drift")
    return entry


m.admission = admission
m.output = lambda config, attempt: ROOT / f"bin/{config}/Phase26Cpp23Refinement/{attempt}"


def build(config):
    inputs, frozen = m.verify("01")
    directory = m.create(BASE / f"build-{config}-01")
    out = m.create(m.output(config, "01"))
    intermediate = m.create(ROOT / f"bin-int/{config}/Phase26Cpp23Refinement/01")
    # GEngine and its exact source/dependency closure are unchanged. Reuse the
    # prior tested ordinary library rather than recompiling Physics or the SDK.
    old_result = m.read(PRIOR / ("build-admission-Debug-01.json" if config == "Debug" else "build-Release-01/result.json"))
    engine = ROOT / f"bin/{config}/Phase26EntryCleanup/01/GEngine/GEngine.lib"
    m.require(m.sha(engine) == old_result["products"][str(engine)]["sha256"], "Reused GEngine drift")
    m.copy(engine, out / "GEngine/GEngine.lib")
    props = directory / "isolated.props"
    props.write_text(f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup Condition="'$(MSBuildProjectName)'=='GEngine' Or '$(MSBuildProjectName)'=='RigidBodySimulation'">
<OutDir>{escape(out.as_posix())}/$(MSBuildProjectName)/</OutDir>
<IntDir>{escape(intermediate.as_posix())}/$(MSBuildProjectName)/</IntDir>
<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild>
</PropertyGroup></Project>''', encoding="utf-8")
    command = [str(m.preserve.MSBUILD), str(ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"),
        "/t:Build", f"/p:Configuration={config}", "/p:Platform=x64", "/p:VCToolsVersion=14.44.35207",
        "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:BuildProjectReferences=false",
        f"/p:ForceImportBeforeCppTargets={props}", "/nr:false", "/m:2", "/nologo", "/v:normal"]
    result = m.execute(directory, command, 1200)
    log = (directory / "stdout.log").read_text(errors="replace")
    result.update(inputs_sha256=m.sha(inputs), warning_classification=m.warnings(log),
        passed=result["exit_code"] == 0 and not result["timed_out"], reused_engine_sha256=m.sha(engine))
    result["tlogs"] = m.preserve.identities(p for p in intermediate.rglob("*.tlog") if p.is_file())
    if result["passed"]:
        result["products"] = m.preserve.identities([out / "GEngine/GEngine.lib", out / "RigidBodySimulation/RigidBodySimulation.exe"])
        m.preserve.stage_runtime(config, out)
        result["runtime_dlls"] = m.preserve.identities((out / "RigidBodySimulation").glob("*.dll"))
        commands = "\n".join(p.read_text(encoding="utf-16") for p in intermediate.rglob("CL.command.*.tlog"))
        result["ordinary_policy"] = "/std:c++23preview" in commands and "/W3" in commands and not any(
            flag in commands for flag in ("GENGINE_OWNER_REVIEW_PROBE", "GENGINE_JOLT_FINAL_AUDIT", "GENGINE_PHASE26_QUALIFICATION", "GENGINE_PHASE26_SANDBOX_CHECKS", "GENGINE_CHAIN_WAKE_CHECKS", "GENGINE_MASS_INERTIA_CHECKS"))
        links = "\n".join(p.read_text(encoding="utf-16") for p in intermediate.rglob("link.read.*.tlog")).upper()
        result["correct_link_inputs"] = all(str(p).upper() in links for p in [out / "GEngine/GEngine.lib", ROOT / f"bin/{config}/Jolt/Jolt.lib", ROOT / f"external/glad/bin/{config}/glad/glad.lib"])
        result["passed"] &= result["ordinary_policy"] and result["correct_link_inputs"] and result["warning_classification"]["first_party"] == 0
    m.verify("01")
    result["preservation"] = "PASS"
    m.write(directory / "result.json", result)
    print(json.dumps({k: result[k] for k in ("passed", "exit_code", "seconds", "preservation")}))
    print("WARNINGS", result["warning_classification"]["counts"])
    return 0 if result["passed"] else 1


def compiler(config):
    vc = m.preserve.VC
    sdk = Path("C:/Program Files (x86)/Windows Kits/10")
    include = [vc / "include"] + [sdk / "Include/10.0.26100.0" / n for n in ("ucrt", "shared", "um", "winrt")]
    # Same first-party include discovery and existing external classification as
    # the actual project. No new warning exclusions or diagnostic suppression.
    project = (ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj").read_text()
    paths = re.search(r"<IncludePath>(.*?)</IncludePath>", project).group(1).split(";")
    include += [(ROOT / "RigidBodySimulation" / n).resolve() for n in paths if "$" not in n]
    flags = [str(vc / "bin/HostX86/x64/cl.exe"), "/nologo", "/std:c++23preview", "/W3", "/EHsc",
        "/MTd" if config == "Debug" else "/MT", "/bigobj", "/DGENGINE_PLATFORM_WINDOWS",
        "/DGENGINE_CONFIG_" + config.upper(), "/D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING",
        "/external:W0", "/external:templates-", "/external:I" + str(ROOT / "external"),
        "/external:I" + str(ROOT / "GEngine/include/external"), "/external:I" + str(vc / "include")]
    flags += ["/I" + str(p) for p in include]
    libs = ["/link", "/LIBPATH:" + str(vc / "lib/x64")]
    libs += ["/LIBPATH:" + str(sdk / "Lib/10.0.26100.0" / n / "x64") for n in ("ucrt", "um")]
    return flags, libs


def probe(config):
    m.verify("01")
    directory = m.create(BASE / f"probe-{config}-01")
    flags, libs = compiler(config)
    command = flags + [str(BASE / "probe.cpp"), "/Fo" + str(directory / "probe.obj"), "/Fe" + str(directory / "probe.exe")] + libs
    result = m.execute(directory, command, 180)
    result["passed"] = result["exit_code"] == 0 and not result["timed_out"]
    result["source_sha256"] = m.sha(BASE / "probe.cpp")
    if result["passed"]:
        run = m.create(directory / "run")
        executed = m.execute(run, [str(directory / "probe.exe")], 15)
        result["runtime"] = executed
        result["passed"] &= executed["exit_code"] == 0 and not executed["timed_out"]
        result["products"] = m.preserve.identities([directory / "probe.exe", directory / "probe.obj"])
    m.write(directory / "result.json", result)
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


def output_error(config):
    exe = m.executable(config, "01")
    directory = m.create(BASE / f"run-{config}-01-output-error")
    target = directory / "readonly.txt"
    target.write_bytes(b"")
    import subprocess
    with target.open("rb") as err, (directory / "stdout.log").open("xb") as out:
        result = subprocess.run([str(exe), "--scene"], cwd=directory, env=m.environment(), stdout=out, stderr=err, timeout=30)
    record = dict(passed=result.returncode == 1 and not (directory / "stdout.log").read_bytes() and not target.read_bytes(),
        exit_code=result.returncode, executable_sha256=m.sha(exe), stderr="inherited read-only file", simulation_steps=0)
    m.write(directory / "result.json", record)
    print(json.dumps(record))
    return 0 if record["passed"] else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("freeze", "build", "run", "probe", "output-error"))
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--case", default="backend")
    args = parser.parse_args()
    if args.action == "freeze": m.freeze("01"); return 0
    if args.action == "build": return build(args.config)
    if args.action == "probe": return probe(args.config)
    if args.action == "output-error": return output_error(args.config)
    return m.run(args.config, "01", args.case)


if __name__ == "__main__":
    raise SystemExit(main())
