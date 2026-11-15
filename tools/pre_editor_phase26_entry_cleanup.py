"""Bounded Phase26 entry/warning cleanup evidence; no approval or qualification."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("preservation", ROOT / "tools/pre_editor_phase26_chain_wake.py")
preserve = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preserve)
BASE = ROOT / "logs/pre_editor/phase-26/jolt-integration-01/main-entry-warning-cleanup-01"
ENTRY = BASE / "entry-and-scope.json"
INPUTS = "01"
sha, read, write = preserve.sha, preserve.read, preserve.write
require, create, copy = preserve.require, preserve.create, preserve.copy_new


def admission():
    entry = read(ENTRY)
    require(preserve.git("rev-parse", "HEAD").strip() == entry["head"] and
            preserve.git("branch", "--show-current").strip() == "", "HEAD/branch drift")
    require(preserve.git("ls-files", "--stage").strip() == entry["index"].strip(), "Index drift")
    preserve.check_identities(entry["accepted_review"])
    preserve.check_identities(entry["contracts"])
    for name, expected in entry["previous_inputs"]["source_inputs"].items():
        if name not in entry["allowlist"]:
            require(sha(ROOT / name) == expected, "Unrelated source drift: " + name)
    require(preserve.git("rev-parse", "HEAD", cwd=ROOT / "external/jolt").strip() == preserve.SDK_COMMIT and
            not preserve.git("status", "--porcelain", "--untracked-files=all", cwd=ROOT / "external/jolt").strip(), "SDK drift")
    return entry


def freeze(attempt):
    entry = admission()
    names = set(entry["previous_inputs"]["source_inputs"]) | set(entry["allowlist"])
    raw = create(BASE / ("source-" + attempt))
    for name in entry["allowlist"]:
        if (ROOT / name).is_file():
            copy(ROOT / name, raw / name)
    names = sorted(name for name in names if (ROOT / name).is_file())
    preserve.PROFILE = "mass-inertia"
    deps = preserve.identities(preserve.dependency_files())
    write(BASE / f"inputs-{attempt}.json", dict(utc=preserve.utc(), entry_sha256=sha(ENTRY),
        source_inputs={n: sha(ROOT / n) for n in names}, dependencies=deps,
        runtime=preserve.identities(preserve.runtime_files()), sdk_commit=preserve.SDK_COMMIT,
        policy="Existing ordinary C++23preview/W3/MTd-MT; no test/profiling macros; canonical dependencies reused"))


def verify(attempt):
    admission()
    path = BASE / f"inputs-{attempt}.json"
    frozen = read(path)
    require(sha(ENTRY) == frozen["entry_sha256"], "Entry drift")
    for name, expected in frozen["source_inputs"].items():
        require(sha(ROOT / name) == expected, "Source drift: " + name)
    preserve.check_identities(frozen["dependencies"])
    preserve.check_identities(frozen["runtime"])
    return path, frozen


def environment(extra=None):
    env = os.environ.copy()
    for name in list(env):
        if name.startswith(("GENGINE_", "OWNER_REVIEW_")):
            env.pop(name)
    for name in ("CL", "_CL_", "LINK", "_LINK_"):
        require(not env.get(name), "Unfrozen build override: " + name)
    env.update(extra or {})
    return env


def execute(directory, command, timeout, extra=None):
    write(directory / "command.json", dict(utc=preserve.utc(), command=command, cwd=str(directory), timeout_seconds=timeout, environment=extra or {}))
    start = time.monotonic()
    with (directory / "stdout.log").open("xb") as out, (directory / "stderr.log").open("xb") as err:
        proc = subprocess.Popen(command, cwd=directory, env=environment(extra), stdout=out, stderr=err)
        timed_out = False
        try:
            code = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], stdout=err, stderr=err,
                           timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
            # Preserve the primary failed result even if the sandbox cannot
            # terminate a native modal process. No successful exit is invented.
            try:
                code = proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                code = None
    result = dict(pid=proc.pid, exit_code=code, timed_out=timed_out, seconds=time.monotonic()-start)
    write(directory / "process-result.json", result)
    return result


def warnings(log):
    # MSBuild repeats diagnostics in its final summary. Count primary emissions
    # before the summary, excluding the repeated template-substitution text.
    text = log.split("Build succeeded.")[0].split("Build FAILED.")[0]
    records = []
    for line in text.splitlines():
        match = re.search(r"(?P<where>(?:[A-Za-z]:\\[^\r\n]*?|LINK|cl|MSBUILD))(?::|\((?P<line>\d+)(?:,\d+)?\):)\s*(?:fatal )?warning (?P<id>(?:C|LNK|MSB|D)\d+): (?P<message>.*)", line)
        if not match:
            continue
        value = match.groupdict()
        if not value["message"].strip() or value["message"].startswith(("        ", "    with", "    [", "    ]")):
            continue
        where = value["where"].lower()
        if "\\external\\" in where or "\\include\\external\\" in where:
            owner = "third-party"
        elif where.startswith(str(ROOT).lower()):
            owner = "GEngine" if "\\gengine\\" in where else "RigidBodySimulation"
        elif "\\microsoft visual studio\\" in where or "\\windows kits\\" in where:
            owner = "vendor-instantiated-system-header" if "STL4043" in value["message"] else "system-header-review-required"
        else:
            owner = "first-party-build-integration"
        value["owner"] = owner
        records.append(value)
    return dict(records=records, counts={owner: sum(r["owner"] == owner for r in records) for owner in sorted({r["owner"] for r in records})},
                first_party=sum(r["owner"] in ("GEngine", "RigidBodySimulation", "first-party-build-integration", "system-header-review-required") for r in records))


def output(config, attempt):
    return ROOT / f"bin/{config}/Phase26EntryCleanup/{attempt}"


def build(config, attempt):
    path, _ = verify(INPUTS)
    directory = create(BASE / f"build-{config}-{attempt}")
    out = create(output(config, attempt))
    intermediate = create(ROOT / f"bin-int/{config}/Phase26EntryCleanup/{attempt}")
    condition = "'$(MSBuildProjectName)'=='GEngine' Or '$(MSBuildProjectName)'=='RigidBodySimulation'"
    props = directory / "isolated.props"
    props.write_text(f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup Condition="{condition}">
<OutDir>{escape(out.as_posix())}/$(MSBuildProjectName)/</OutDir>
<IntDir>{escape(intermediate.as_posix())}/$(MSBuildProjectName)/</IntDir>
<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild>
</PropertyGroup></Project>''', encoding="utf-8")
    project = directory / "affected.proj"
    project.write_text(f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003" DefaultTargets="Build">
<ItemGroup><Affected Include="{escape(str(ROOT / 'GEngine/GEngine.vcxproj'))}" />
<Affected Include="{escape(str(ROOT / 'RigidBodySimulation/RigidBodySimulation.vcxproj'))}" /></ItemGroup>
<Target Name="Build"><MSBuild Projects="@(Affected)" Targets="Build" BuildInParallel="false" StopOnFirstFailure="true" /></Target></Project>''', encoding="utf-8")
    command = [str(preserve.MSBUILD), str(project), "/t:Build", f"/p:Configuration={config}", "/p:Platform=x64",
        "/p:VCToolsVersion=14.44.35207", "/p:WindowsTargetPlatformVersion=10.0.26100.0", "/p:BuildProjectReferences=false",
        f"/p:ForceImportBeforeCppTargets={props}", "/nr:false", "/m:2", "/nologo", "/v:normal"]
    result = execute(directory, command, 1200)
    log = (directory / "stdout.log").read_text(errors="replace")
    result.update(inputs_sha256=sha(path), warning_classification=warnings(log), passed=result["exit_code"] == 0 and not result["timed_out"])
    result["tlogs"] = preserve.identities(p for p in intermediate.rglob("*.tlog") if p.is_file())
    if result["passed"]:
        result["products"] = preserve.identities([out / "GEngine/GEngine.lib", out / "RigidBodySimulation/RigidBodySimulation.exe"])
        preserve.stage_runtime(config, out)
        result["runtime_dlls"] = preserve.identities((out / "RigidBodySimulation").glob("*.dll"))
        commands = "\n".join(p.read_text(encoding="utf-16") for p in intermediate.rglob("CL.command.*.tlog"))
        result["ordinary_policy"] = "/std:c++23preview" in commands and "/W3" in commands and not any(x in commands for x in ("GENGINE_CHAIN_WAKE_CHECKS", "GENGINE_MASS_INERTIA_CHECKS", "GENGINE_JOLT_FINAL_AUDIT"))
        links = "\n".join(p.read_text(encoding="utf-16") for p in (intermediate / "RigidBodySimulation").rglob("link.read.*.tlog")).upper()
        consumed = [out / "GEngine/GEngine.lib", ROOT / f"bin/{config}/Jolt/Jolt.lib", ROOT / f"external/glad/bin/{config}/glad/glad.lib"]
        result["correct_link_inputs"] = all(str(p).upper() in links for p in consumed)
        result["passed"] &= result["ordinary_policy"] and result["correct_link_inputs"] and result["warning_classification"]["first_party"] == 0
    try:
        verify(INPUTS)
        result["preservation"] = "PASS"
    except RuntimeError as error:
        result["preservation"] = str(error)
        result["passed"] = False
    write(directory / "result.json", result)
    print(json.dumps({k: v for k, v in result.items() if k not in ("tlogs", "runtime_dlls", "warning_classification")}, indent=2))
    print("WARNINGS", result["warning_classification"]["counts"])
    return 0 if result["passed"] else 1


def executable(config, attempt, baseline=False):
    if baseline:
        entry = admission()
        path = ROOT / "bin/Release/RigidBodySimulation/RigidBodySimulation.exe"
        require(sha(path) == sha(BASE / "products-before/Release/RigidBodySimulation.exe"), "Owner baseline binary drift")
        return path
    inputs, _ = verify(INPUTS)
    admission_path = BASE / f"build-admission-{config}-{attempt}.json"
    built = read(admission_path if admission_path.is_file() else BASE / f"build-{config}-{attempt}/result.json")
    require(built["passed"] and built["inputs_sha256"] == sha(inputs), "No matching clean build")
    preserve.check_identities(built["products"])
    preserve.check_identities(built["runtime_dlls"])
    return output(config, attempt) / "RigidBodySimulation/RigidBodySimulation.exe"


CLI_CASES = {
    "missing": ["--scene"], "unknown": ["--scene", "missing"],
    "links": ["--scene", "distance-chain", "--links", "7"],
    "invalid-backend": ["--backend", "unknown"], "duplicate": ["--scene", "box-stack", "--scene", "box-stack"],
    "diagnostic-extra": ["--backend-checks", "extra"],
}


def run(config, attempt, case, baseline=False):
    exe = executable(config, attempt, baseline)
    prefix = "baseline" if baseline else config
    directory = create(BASE / f"run-{prefix}-{attempt}-{case}")
    extra = {}
    if case in CLI_CASES:
        args, timeout, expected, token = CLI_CASES[case], 30, 1, "RBS startup: "
    elif case == "backend":
        args, timeout, expected, token = ["--backend-checks"], 180, 0, "BACKEND_CHECKS_PASS"
    elif case == "diagnostic":
        args, timeout, expected, token = ["--transform-shortcut-checks"], 30, 0, "TRANSFORM_SHORTCUT_PASS"
    elif case == "lifecycle":
        args, timeout, expected, token = ["--scene", "distance-chain", "--links", "8", "--backend", "native", "--physics-smoke-ticks", "120", "--backend-switch-checks", "true"], 180, 0, "BACKEND_SWITCH_CHECKS_PASS"
    elif case in ("default", "jolt", "box"):
        args = ["--physics-smoke-ticks", "20"]
        if case == "jolt": args += ["--backend", "jolt", "--scene", "distance-chain", "--links", "4"]
        if case == "box": args += ["--scene", "box-stack", "--backend", "native"]
        timeout, expected, token = 180, 0, "BACKEND_SMOKE_PASS"
    elif case in ("init-error", "asset-error"):
        args, timeout, expected, token = ["--physics-smoke-ticks", "1"], 30, 1, ""
        extra["SDL_VIDEODRIVER"] = "dummy"
        if case == "asset-error":
            # An isolated package missing assets exercises the real pre-logger
            # failure. No canonical resource is renamed, deleted or modified.
            original = exe
            target = create(directory / "missing-package/RigidBodySimulation")
            copy(original, target / "RigidBodySimulation.exe")
            for dll in original.parent.glob("*.dll"):
                copy(dll, target / dll.name)
            exe = target / "RigidBodySimulation.exe"
            token = "Runtime asset failure code="
    else:
        raise RuntimeError("Unknown bounded case")
    result = execute(directory, [str(exe), *args], timeout, extra)
    stdout = (directory / "stdout.log").read_bytes()
    stderr = (directory / "stderr.log").read_bytes()
    result.update(executable=str(exe), executable_sha256=sha(exe), case=case,
        passed=result["exit_code"] == expected and not result["timed_out"] and token.encode() in stdout+stderr)
    if case in CLI_CASES:
        require(not stdout, "Invalid CLI unexpectedly wrote stdout")
        if not baseline:
            prior = BASE / f"run-baseline-01-{case}"
            result["byte_equivalent"] = stdout == (prior / "stdout.log").read_bytes() and stderr == (prior / "stderr.log").read_bytes()
            result["passed"] &= result["byte_equivalent"]
    if case == "lifecycle":
        result["passed"] &= len(re.findall(rb"BACKEND_SWITCH_CHECKS_PASS[^\r\n]*total_ticks=60\b", stdout)) == 1
    executable(config, attempt, baseline)
    write(directory / "result.json", result)
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


def main():
    global INPUTS
    p = argparse.ArgumentParser()
    p.add_argument("action", choices=("freeze", "build", "run"))
    p.add_argument("--config", choices=("Debug", "Release"), default="Release")
    p.add_argument("--attempt", type=preserve.label, default="01")
    p.add_argument("--inputs", type=preserve.label, default="01")
    p.add_argument("--case", default="missing")
    p.add_argument("--baseline", action="store_true")
    a = p.parse_args()
    INPUTS = a.inputs
    if a.action == "freeze": freeze(a.attempt); return 0
    if a.action == "build": return build(a.config, a.attempt)
    return run(a.config, a.attempt, a.case, a.baseline)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print("ENTRY_CLEANUP_STOP:", error)
        raise SystemExit(1)
