"""Owner-admitted CHAIN-WAKE checks only; never qualifies, seals or publishes.

Every action has an explicit, create-only destination. Build attempts have their
own output/intermediate trees; canonical outputs and earlier evidence are inputs.
No matrix/performance/retry action is provided. Run snapshot after source edits.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time
from datetime import datetime, timezone
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "logs/pre_editor/phase-26/jolt-integration-01/p1-remediation-01/chain-wake-candidate-01"
ENTRY = BASE / "owner-authorization-and-entry.json"
AUDIT = ROOT / "logs/pre_editor/phase-26/jolt-integration-01/final-audit-01/final-inputs.json"
PROPOSAL = ROOT / "docs/pre_editor/active/PHASE_26_JOLT_INTEGRATION_01/CHAIN_WAKE_SCOPE_PROPOSAL_01.md"
VS = Path("C:/Program Files/Microsoft Visual Studio/2022/Community")
MSBUILD = VS / "MSBuild/Current/Bin/MSBuild.exe"
VC = VS / "VC/Tools/MSVC/14.44.35207"
SDK_COMMIT = "e77f175595e64cb44218cc9d9d56fc365ad0e36a"
CONFIGS = ("Debug", "Release")
SOURCE_SCOPES = ("GEngine/include/GEngine/Physics", "GEngine/src/Physics",
                 "RigidBodySimulation/include", "RigidBodySimulation/src", "RigidBodySimulation/tests")
ASSET_SCOPES = ("Shaders", "Fonts", "Images", "startup")
PROFILE = "chain-wake"
RECOVERY = False
PRODUCT_PREFIX = "Phase26ChainWake"
DESIGN = ROOT / "logs/pre_editor/phase-26/jolt-integration-01/p1-remediation-01/mass-inertia-design-01"
MASS_CANDIDATE = DESIGN.parent / "mass-inertia-candidate-01"


def select_profile(profile):
    global PROFILE, PRODUCT_PREFIX, BASE, ENTRY, PROPOSAL, RECOVERY
    RECOVERY = profile == "mass-inertia-recovery"
    PROFILE = "mass-inertia" if RECOVERY else profile
    if PROFILE == "mass-inertia":
        PRODUCT_PREFIX = "Phase26MassInertia"
        BASE = ROOT / "logs/pre_editor/phase-26/jolt-integration-01/p1-remediation-01/mass-inertia-candidate-01"
        ENTRY = BASE / "owner-authorization-and-entry.json"
        PROPOSAL = ROOT / "docs/pre_editor/active/PHASE_26_JOLT_INTEGRATION_01/MASS_INERTIA_P0_CORRECTION_PROPOSAL_01.md"
    if RECOVERY:
        PRODUCT_PREFIX = "Phase26MassInertiaRecovery"
        BASE = DESIGN.parent / "mass-inertia-validation-recovery-01"
        ENTRY = BASE / "owner-authorization-and-entry.json"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


def git(*args, cwd=ROOT):
    # Per-command exact-worktree trust only; never change global Git configuration.
    return subprocess.check_output(["git", "-c", "safe.directory=" + Path(cwd).as_posix(), *args],
                                   cwd=cwd, text=True, encoding="utf-8")


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def safe_write_path(path):
    path = Path(os.path.abspath(path))
    require(path.is_relative_to(ROOT), f"Write outside integration root: {path}")
    require(path.resolve().is_relative_to(ROOT), f"Resolved write outside integration root: {path}")
    for parent in (path, *path.parents):
        if parent == ROOT:
            break
        if parent.exists() or parent.is_symlink():
            require(not (parent.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT),
                    f"Unexpected reparse point in write path: {parent}")
    return path


def create(path):
    path = safe_write_path(path)
    path.mkdir(parents=True, exist_ok=False)
    return path


def write(path, value):
    with safe_write_path(path).open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def copy_new(source, target):
    target = safe_write_path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    with Path(source).open("rb") as src, target.open("xb") as dst:
        shutil.copyfileobj(src, dst, 1024 * 1024)
    require(sha(source) == sha(target), f"Copy mismatch: {source}")


def identities(paths):
    return {str(p): {"sha256": sha(p), "bytes": p.stat().st_size} for p in sorted(set(paths))}


def check_identities(records):
    for name, expected in records.items():
        path = Path(name)
        require(path.is_file() and sha(path) == expected["sha256"], f"Input/product drift: {name}")


def admission():
    require(ROOT == Path("C:/dev/GEngine-jolt-integration").resolve(), "Wrong integration workspace")
    entry = read(ENTRY)
    require(git("rev-parse", "HEAD").strip() == entry["head"], "HEAD changed")
    require(git("branch", "--show-current").strip() == entry["branch"] == "", "Detached HEAD changed")
    require(git("ls-files", "--stage").strip() == entry["index"].strip(), "Git index changed")
    require(sha(PROPOSAL) == entry["proposal_sha256"], "Accepted proposal changed")
    if RECOVERY:
        check_identities(entry["baseline_files"])
        check_identities(entry["owner_products_at_recovery"])
        require(entry["recovery_budget"] == dict(additional_debug_builds=1, release_builds=0,
            debug_build_seconds=1200, backend_checks_each_config=1, backend_check_seconds=180,
            backend_check_ticks_max=2353, release_lifecycle_processes=1, lifecycle_seconds=180,
            lifecycle_ticks=60), "Recovery budget changed")
        original = MASS_CANDIDATE / "build-inputs-02.json"
        require(sha(original) == entry["source02_sha256"], "Corrected Release source baseline changed")
        # The only authorized new source change is this evidence runner. No C++
        # source, fixture value, test dimension or production input may drift.
        for name, expected in read(original)["source_inputs"].items():
            if name != "tools/pre_editor_phase26_chain_wake.py":
                require(sha(ROOT / name) == expected, f"Recovery source drift: {name}")
    if PROFILE == "mass-inertia":
        design = read(DESIGN / "proposal-inputs.json")
        require(sha(DESIGN / "proposal-inputs.json") == entry["proposal_inputs_sha256"], "Mass design inputs changed")
        frozen = {}
        for group in ("protected_current_hashes", "controlling_documents", "contracts", "read_only_references"):
            frozen.update(design[group])
        for name, expected in frozen.items():
            require(sha(ROOT / name) == expected, f"Protected mass input changed: {name}")
        for config, expected in entry["canonical_products"].items():
            require(sha(ROOT / f"bin/{config}/RigidBodySimulation/RigidBodySimulation.exe") == expected["sha256"],
                    f"Owner executable changed: {config}")
        fragments = read(DESIGN / "protected-test-fragments.json")
        tests = (ROOT / "RigidBodySimulation/tests/BackendChecks.h").read_text(encoding="utf-8")
        for name, end in (("ChainAt301", "ActivationTopology"), ("IndependentCapacity", "ChainWake")):
            fragment = tests[tests.index("    inline bool " + name):tests.index("    inline bool " + end)]
            require(hashlib.sha256(fragment.encode()).hexdigest() == fragments[name]["sha256_lf_text"] and
                    len(fragment.encode()) == fragments[name]["bytes_lf"], f"Frozen test fragment changed: {name}")
        require(entry["budgets"] == dict(builds_each_config=1, build_timeout_s=1200,
            backend_checks_each_config=1, check_timeout_s=180, existing_ticks=2289,
            additional_ticks_max=64, per_check_ticks_max=2353, release_lifecycle_processes=1,
            lifecycle_ticks=60, aggregate_ticks_max=4766, jacobi_sweeps_max=32,
            extra_persistent_sync_bytes_max=1048576), "Mass admission budgets changed")
        entry = dict(entry, production=design["production_allowlist_proposed"],
                     support=design["support_allowlist_proposed"], frozen_input_hashes=frozen,
                     build_seconds_each=1200, backend_check_seconds_each=180,
                     backend_check_ticks_each=4096, release_lifecycle_seconds=180, release_lifecycle_ticks=60)
    owned = set(entry["production"] + entry["support"])
    for name, expected in entry["frozen_input_hashes"].items():
        if name not in owned:
            require(sha(ROOT / name) == expected, f"Frozen non-owned input changed: {name}")
    pin = read(ROOT / "external/jolt-pin.json")
    require(pin["version"] == "5.6.0" and pin["commit"] == SDK_COMMIT, "Jolt pin changed")
    require(git("rev-parse", "HEAD", cwd=ROOT / "external/jolt").strip() == SDK_COMMIT,
            "Jolt checkout differs from pin")
    require(not git("status", "--porcelain", "--untracked-files=all", cwd=ROOT / "external/jolt").strip(),
            "Jolt SDK checkout is not clean")
    require((entry["build_seconds_each"], entry["backend_check_seconds_each"],
             entry["backend_check_ticks_each"], entry["release_lifecycle_seconds"],
             entry["release_lifecycle_ticks"]) == (1200, 180, 4096, 180, 60), "Admitted budget mismatch")
    return entry


def source_files(entry):
    audit = read(AUDIT)
    names = set(audit["source_inputs"]) | set(audit["contract_files"])
    names.update(entry["frozen_input_hashes"])
    names.update(entry["production"] + entry["support"])
    if PROFILE == "mass-inertia":
        names.update(p.relative_to(ROOT).as_posix() for p in
                     (PROPOSAL, DESIGN / "proposal-inputs.json", DESIGN / "protected-test-fragments.json"))
    names.update(("GEngine/src/gepch.cpp", "GEngine/include/GEngine/gepch.h", "tools/postbuild.py",
                  "RigidBodySimulation/postbuild.py", "external/jolt-build/Jolt.vcxproj"))
    if RECOVERY:
        # These three consumed sources were omitted from the historical manifest.
        # Bind them prospectively; do not rewrite the old manifest or its FAIL.
        names.update(("external/glad/src/glad.c", "external/glad/include/glad/glad.h",
                      "external/glad/include/KHR/khrplatform.h"))
    # Additional providers omitted from the earlier audit's declared closure are
    # bounded to the affected Physics module and existing RBS integration/tests.
    for scope in SOURCE_SCOPES:
        names.update(p.relative_to(ROOT).as_posix() for p in (ROOT / scope).rglob("*")
                     if p.is_file() and p.suffix.lower() in (".cpp", ".h", ".hpp", ".inl"))
    return sorted(names)


def runtime_files():
    paths = []
    for config in CONFIGS:
        paths.extend((ROOT / f"bin/{config}/RigidBodySimulation").glob("*.dll"))
        for scope in ASSET_SCOPES:
            root = ROOT / f"bin/{config}/assets/{scope}"
            require(root.is_dir(), f"Runtime asset scope missing: {root}")
            paths.extend(p for p in root.rglob("*") if p.is_file())
    return paths


def dependency_files():
    paths = [MSBUILD, VC / "bin/Hostx64/x64/cl.exe", VC / "bin/Hostx64/x64/link.exe"]
    if PROFILE == "mass-inertia":
        # This MSBuild invocation selects HostX86/x64 in the preserved build log.
        # Bind the actual tools as well as the already recorded alternate host.
        paths.extend(VC / "bin/HostX86/x64" / name for name in ("cl.exe", "link.exe", "lib.exe"))
        paths.extend(VC / "lib/x64" / name for name in
                     ("libcmt.lib", "libcmtd.lib", "libvcruntime.lib", "libvcruntimed.lib",
                      "libcpmt.lib", "libcpmtd.lib", "oldnames.lib"))
    for name in ("Microsoft.Cpp.Default.props", "Microsoft.Cpp.props", "Microsoft.Cpp.targets"):
        paths.append(VS / "MSBuild/Microsoft/VC/v170" / name)
    paths.append(Path("C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe"))
    for config in CONFIGS:
        paths.extend((ROOT / f"bin/{config}/Jolt/Jolt.lib", ROOT / f"bin/{config}/GEngine/GEngine.lib",
                      ROOT / f"bin/{config}/RigidBodySimulation/RigidBodySimulation.exe",
                      ROOT / f"external/glad/bin/{config}/glad/glad.lib"))
    # These are exactly the library search directories in the existing RBS project.
    # Record existing files; actual linker choices are also retained in build tlogs.
    for name in ("sdl2", "tbb", "rttr", "assimp", "fmod"):
        paths.extend((ROOT / f"external/{name}/lib").glob("*.lib"))
    user_props = Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft/MSBuild/v4.0/Microsoft.Cpp.x64.user.props"
    if user_props.is_file():
        paths.append(user_props)
    if RECOVERY:
        # Referenced projects may rebuild these derived Debug libraries. Their
        # sources/tools remain frozen; exact produced bytes and actual link inputs
        # are checked and recorded by build(), then immutable for runtime checks.
        paths = [p for p in paths if p not in recovery_debug_libraries()]
    return paths


def recovery_debug_libraries():
    return (ROOT / "bin/Debug/Jolt/Jolt.lib", ROOT / "external/glad/bin/Debug/glad/glad.lib")


def recovery_dependency_evidence(directory, intermediate):
    paths = []
    actual = {}
    for project, folder in (("Jolt", ROOT / "bin-int/Debug/Jolt"),
                            ("glad", ROOT / "external/glad/bin-int/Debug/glad")):
        commands = list(folder.rglob("CL.command.*.tlog"))
        require(commands, f"Missing actual {project} compiler commands")
        text = "\n".join(p.read_text(encoding="utf-16") for p in commands)
        require("/MTD" in text.upper() and "GENGINE_MASS_INERTIA_CHECKS" not in text and
                "GENGINE_CHAIN_WAKE_CHECKS" not in text, f"Unexpected {project} compiler flags")
        actual[project] = dict(debug_static_crt=True, editor_test_macros=False)
        paths.extend(p for p in folder.rglob("*.tlog") if p.is_file())
    reads = list((intermediate / "RigidBodySimulation").rglob("link.read.*.tlog"))
    linked = "\n".join(p.read_text(encoding="utf-16") for p in reads).upper()
    for library in recovery_debug_libraries():
        require(str(library).upper() in linked, f"Missing actual linked library: {library}")
    copies = []
    for path in paths:
        target = directory / "dependency-tlogs" / path.relative_to(ROOT)
        copy_new(path, target)
        copies.append(target)
    return dict(libraries=identities(recovery_debug_libraries()),
                tlogs=identities(copies), compiler_policy=actual)


def snapshot(attempt):
    if RECOVERY:
        require(attempt == "01", "Only the admitted recovery snapshot is supported")
    entry = admission()
    destination = BASE / f"build-inputs-{attempt}.json"
    require(not destination.exists(), f"Snapshot exists: {destination}")
    names = source_files(entry)
    sources = {name: sha(ROOT / name) for name in names}
    runtime = identities(runtime_files())
    deps = identities(dependency_files())
    raw = create(BASE / f"source-candidate-{attempt}")
    for name in entry["production"] + entry["support"] + ["RigidBodySimulation/src/RigidBodySimulation.cpp"]:
        copy_new(ROOT / name, raw / name)
    # Preserve owner executables separately from the affected candidate, never replace them.
    for config in CONFIGS:
        copy_new(ROOT / f"bin/{config}/RigidBodySimulation/RigidBodySimulation.exe",
                 raw / f"canonical-{config}/RigidBodySimulation.exe")
    write(destination, dict(utc=utc(), profile=PROFILE, head=entry["head"], branch=entry["branch"],
                           entry_sha256=sha(ENTRY), proposal_sha256=sha(PROPOSAL),
                           release_admission_sha256=sha(BASE / "release-artifact-admission.json") if RECOVERY else None,
                           audit_baseline_sha256=sha(AUDIT), source_inputs=sources,
                           source_discovery_scopes=list(SOURCE_SCOPES), runtime_inputs=runtime,
                           runtime_asset_scopes=list(ASSET_SCOPES), dependencies=deps,
                           sdk_commit=SDK_COMMIT, toolset="14.44.35207", windows_sdk="10.0.26100.0",
                           configuration_policy="Existing project CRT/options; GENGINE_CHAIN_WAKE_CHECKS" +
                           (" and GENGINE_MASS_INERTIA_CHECKS" if PROFILE == "mass-inertia" else " only"),
                           qualification="No performance campaign or dependent qualification authorized"))
    print(destination)
    return 0


def verify_snapshot(attempt):
    entry = admission()
    path = BASE / f"build-inputs-{attempt}.json"
    frozen = read(path)
    require(sha(ENTRY) == frozen["entry_sha256"] and sha(AUDIT) == frozen["audit_baseline_sha256"],
            "Evidence baseline identity changed")
    if RECOVERY:
        require(sha(BASE / "release-artifact-admission.json") == frozen["release_admission_sha256"],
                "Prospective Release artifact admission changed")
    require(source_files(entry) == sorted(frozen["source_inputs"]), "Relevant source membership changed")
    for name, expected in frozen["source_inputs"].items():
        require(sha(ROOT / name) == expected, f"Source changed since snapshot: {name}")
    require(set(map(str, runtime_files())) == set(frozen["runtime_inputs"]), "Runtime membership changed")
    check_identities(frozen["runtime_inputs"])
    check_identities(frozen["dependencies"])
    return path, frozen


def environment(config):
    env = os.environ.copy()
    removed = sorted(key for key in env if key.startswith("GENGINE_"))
    for key in removed:
        env.pop(key)
    for key in ("CL", "_CL_", "LINK", "_LINK_"):
        require(not env.get(key), f"Unfrozen compiler/linker environment override: {key}")
    env["GENGINE_ASSET_ROOT"] = str(ROOT / f"bin/{config}/assets")
    return env, removed


def execute(directory, command, config, timeout, cwd):
    env, removed = environment(config)
    write(directory / "command.json", dict(utc=utc(), command=command, cwd=str(cwd),
          deadline_seconds=timeout, removed_gengine_environment_keys=removed,
          asset_root=env["GENGINE_ASSET_ROOT"]))
    start = time.monotonic()
    result = dict(exit_code=None, timed_out=False)
    with safe_write_path(directory / "stdout.log").open("xb") as log:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT)
        result["pid"] = process.pid
        try:
            result["exit_code"] = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            result["timed_out"] = True
            # Kill only this explicitly launched process and its descendants.
            cleanup = subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                     stdout=log, stderr=subprocess.STDOUT, timeout=30,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            result["timeout_cleanup_exit_code"] = cleanup.returncode
            result["exit_code"] = process.wait(timeout=30)
    result["seconds"] = time.monotonic() - start
    return result


def output_root(config, attempt):
    return ROOT / f"bin/{config}/{PRODUCT_PREFIX}/{attempt}"


def stage_runtime(config, output):
    source = ROOT / f"bin/{config}/assets"
    destination = output / "assets"
    safe_write_path(destination)
    require(source.resolve().is_relative_to(ROOT) and source.is_dir(), "Invalid asset target")
    require(not destination.exists(), "Asset junction already exists")
    # Known contained paths; native PowerShell creates one junction without copying
    # or changing the existing runtime resources. Nothing writes through it.
    quote = lambda value: "'" + str(value).replace("'", "''") + "'"
    subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-Command",
                    "New-Item -ItemType Junction -Path " + quote(destination) + " -Target " + quote(source) + " | Out-Null"],
                   check=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
    require(destination.resolve() == source.resolve(), "Asset junction target mismatch")
    for dll in (ROOT / f"bin/{config}/RigidBodySimulation").glob("*.dll"):
        copy_new(dll, output / "RigidBodySimulation" / dll.name)


def build(config, attempt, inputs):
    if RECOVERY:
        require(config == "Debug" and attempt == "01" and inputs == "01",
                "Recovery authorizes one Debug build only; no Release build or retry")
    snapshot_path, _ = verify_snapshot(inputs)
    directory = create(BASE / f"build-{config}-{attempt}")
    output = create(output_root(config, attempt))
    intermediate = create(ROOT / f"bin-int/{config}/{PRODUCT_PREFIX}/{attempt}")
    props = directory / "isolated.props"
    condition = "'$(MSBuildProjectName)'=='GEngine' Or '$(MSBuildProjectName)'=='RigidBodySimulation'"
    with props.open("x", encoding="utf-8") as stream:
        macros = "GENGINE_CHAIN_WAKE_CHECKS;" + ("GENGINE_MASS_INERTIA_CHECKS;" if PROFILE == "mass-inertia" else "")
        stream.write(f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup Condition="{condition}">
<OutDir>{escape(output.as_posix())}/$(MSBuildProjectName)/</OutDir>
<IntDir>{escape(intermediate.as_posix())}/$(MSBuildProjectName)/</IntDir>
<PostBuildEventUseInBuild>false</PostBuildEventUseInBuild>
</PropertyGroup>
<ItemDefinitionGroup Condition="{condition}"><ClCompile>
<PreprocessorDefinitions>{macros}%(PreprocessorDefinitions)</PreprocessorDefinitions>
</ClCompile></ItemDefinitionGroup>
</Project>\n''')
    command = [str(MSBUILD), str(ROOT / "RigidBodySimulation/RigidBodySimulation.vcxproj"),
               "/t:Build", f"/p:Configuration={config}", "/p:Platform=x64",
               "/p:VCToolsVersion=14.44.35207", "/p:WindowsTargetPlatformVersion=10.0.26100.0",
               f"/p:ForceImportBeforeCppTargets={props}", "/nr:false", "/m:2", "/nologo", "/v:normal"]
    result = execute(directory, command, config, 1200, ROOT)
    result.update(inputs=str(snapshot_path), inputs_sha256=sha(snapshot_path), props_sha256=sha(props))
    result["passed"] = result["exit_code"] == 0 and not result["timed_out"]
    if result["passed"]:
        try:
            products = [output / "RigidBodySimulation/RigidBodySimulation.exe", output / "GEngine/GEngine.lib"]
            result["products"] = identities(products)
            stage_runtime(config, output)
            result["runtime_dlls"] = identities((output / "RigidBodySimulation").glob("*.dll"))
        except (RuntimeError, OSError, subprocess.SubprocessError) as error:
            result["runtime_staging_error"] = str(error)
            result["passed"] = False
    # Compiler/linker command and dependency tlogs retain the actual options and
    # inputs, including inherited project settings, independently of this script.
    tlogs = [p for p in intermediate.rglob("*.tlog") if p.is_file()]
    result["build_tlogs"] = identities(tlogs)
    if result["passed"]:
        commands = {}
        for project in ("GEngine", "RigidBodySimulation"):
            files = list((intermediate / project).rglob("CL.command.*.tlog"))
            text = "\n".join(p.read_text(encoding="utf-16", errors="replace") for p in files)
            commands[project] = dict(chain_wake_macro="GENGINE_CHAIN_WAKE_CHECKS" in text,
                                     mass_inertia_macro="GENGINE_MASS_INERTIA_CHECKS" in text,
                                     audit_macro="GENGINE_JOLT_FINAL_AUDIT" in text)
        result["actual_compile_macro_checks"] = commands
        if not all(value["chain_wake_macro"] and not value["audit_macro"] and
                   value["mass_inertia_macro"] == (PROFILE == "mass-inertia") for value in commands.values()):
            result["passed"] = False
    if RECOVERY and result["passed"]:
        try:
            result["derived_dependency_evidence"] = recovery_dependency_evidence(directory, intermediate)
        except (RuntimeError, OSError) as error:
            result["dependency_evidence_error"] = str(error)
            result["passed"] = False
    try:
        verify_snapshot(inputs)
        result["canonical_and_input_preservation"] = "PASS"
    except RuntimeError as error:
        result["canonical_and_input_preservation"] = str(error)
        result["passed"] = False
    write(directory / "result.json", result)
    print(json.dumps({"evidence": str(directory), **result}, indent=2))
    return 0 if result["passed"] else 1


def validated_build(config, build_attempt, inputs):
    snapshot_path, _ = verify_snapshot(inputs)
    if RECOVERY and config == "Release":
        # This is a new prospective artifact admission, never a relabeling of the
        # old build preservation FAIL. Static library copies bind the candidate's
        # actual link event independently of later Owner canonical rebuilds.
        path = BASE / "release-artifact-admission.json"
        result = read(path)
        require(result["passed"] and result["compiled_inputs_sha256"] == read(ENTRY)["source02_sha256"],
                "No prospective Release artifact admission")
        for key in ("products", "runtime_dlls", "provenance"):
            check_identities(result[key])
        exe = ROOT / "bin/Release/Phase26MassInertia/01/RigidBodySimulation/RigidBodySimulation.exe"
        require(str(exe) in result["products"], "Release executable not bound")
        return exe, dict(result, inputs_sha256=sha(snapshot_path), admission_sha256=sha(path))
    result = read(BASE / f"build-{config}-{build_attempt}/result.json")
    require(result["passed"] and result["inputs_sha256"] == sha(snapshot_path), "No matching successful build")
    check_identities(result["products"])
    check_identities(result["runtime_dlls"])
    if RECOVERY:
        check_identities(result["derived_dependency_evidence"]["libraries"])
        check_identities(result["derived_dependency_evidence"]["tlogs"])
    return output_root(config, build_attempt) / "RigidBodySimulation/RigidBodySimulation.exe", result


def run_check(config, attempt, inputs, build_attempt, lifecycle=False):
    require(not lifecycle or config == "Release", "Only the admitted Release lifecycle run is supported")
    if RECOVERY:
        require(attempt == build_attempt == inputs == "01", "No recovery retry/process expansion admitted")
        validated_build("Release", build_attempt, inputs)
        if config == "Release":
            require(read(BASE / "test-Debug-01/result.json")["passed"], "Stop after failed Debug check")
        if lifecycle:
            require(read(BASE / "test-Release-01/result.json")["passed"], "Stop after failed Release check")
    exe, build_result = validated_build(config, build_attempt, inputs)
    name = "lifecycle" if lifecycle else "test"
    directory = create(BASE / f"{name}-{config}-{attempt}")
    command = [str(exe)]
    if lifecycle:
        command += ["--scene", "distance-chain", "--links", "8", "--backend", "native",
                    "--physics-smoke-ticks", "120", "--backend-switch-checks", "true"]
    else:
        command += ["--backend-checks"]
    result = execute(directory, command, config, 180, directory)
    log = (directory / "stdout.log").read_text(encoding="utf-8", errors="replace")
    if lifecycle:
        tokens = re.findall(r"BACKEND_SWITCH_CHECKS_PASS[^\r\n]*total_ticks=(\d+)", log)
        token_ok = tokens == ["60"]
        result["completed_ticks"] = int(tokens[0]) if len(tokens) == 1 else None
    else:
        tokens = re.findall(r"CHAIN_WAKE_CHECKS_PASS\s+ticks=(\d+)\b", log)
        token_ok = len(tokens) == 1 and 0 < int(tokens[0]) <= 4096 and "BACKEND_CHECKS_PASS" in log
        result["completed_ticks"] = int(tokens[0]) if len(tokens) == 1 else None
        if PROFILE == "mass-inertia":
            mass_tokens = re.findall(r"MASS_INERTIA_CHECKS_PASS\s+ticks=(\d+)\b", log)
            token_ok = token_ok and len(mass_tokens) == 1 and int(mass_tokens[0]) <= 64 and \
                result["completed_ticks"] == 2289 + int(mass_tokens[0]) <= 2353
            result["mass_inertia_ticks"] = int(mass_tokens[0]) if len(mass_tokens) == 1 else None
    result.update(executable=str(exe), executable_sha256=sha(exe),
                  build_attempt=build_attempt, inputs_sha256=build_result["inputs_sha256"],
                  passed=result["exit_code"] == 0 and not result["timed_out"] and token_ok,
                  token_contract="Release lifecycle exactly 60 ticks" if lifecycle else
                  ("CHAIN_WAKE_CHECKS_PASS ticks=N and MASS_INERTIA_CHECKS_PASS ticks=M; M<=64, N=2289+M<=2353; BACKEND_CHECKS_PASS"
                   if PROFILE == "mass-inertia" else
                   "CHAIN_WAKE_CHECKS_PASS ticks=N (all existing/new ticks, 1..4096) and BACKEND_CHECKS_PASS"))
    try:
        validated_build(config, build_attempt, inputs)
        result["canonical_and_input_preservation"] = "PASS"
    except RuntimeError as error:
        result["canonical_and_input_preservation"] = str(error)
        result["passed"] = False
    write(directory / "result.json", result)
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


def final(attempt, inputs, build_attempt, test_attempt, lifecycle_attempt):
    snapshot_path, frozen = verify_snapshot(inputs)
    results = {}
    for config in CONFIGS:
        exe, built = validated_build(config, build_attempt, inputs)
        results[f"build-{config}"] = built
        tested_path = BASE / f"test-{config}-{test_attempt}/result.json"
        tested = read(tested_path)
        require(tested["passed"] and tested["executable_sha256"] == sha(exe) and
                tested["inputs_sha256"] == sha(snapshot_path), f"Unmatched/failed {config} checks")
        results[f"test-{config}"] = dict(path=str(tested_path), sha256=sha(tested_path), **tested)
    lifecycle_path = BASE / f"lifecycle-Release-{lifecycle_attempt}/result.json"
    lifecycle = read(lifecycle_path)
    release, _ = validated_build("Release", build_attempt, inputs)
    require(lifecycle["passed"] and lifecycle["completed_ticks"] == 60 and
            lifecycle["executable_sha256"] == sha(release) and
            lifecycle["inputs_sha256"] == sha(snapshot_path), "Unmatched/failed Release lifecycle check")
    results["lifecycle-Release"] = dict(path=str(lifecycle_path), sha256=sha(lifecycle_path), **lifecycle)
    path = BASE / f"final-{attempt}.json"
    write(path, dict(utc=utc(), head=frozen["head"], inputs=str(snapshot_path),
                     inputs_sha256=sha(snapshot_path), results=results,
                     status="FOCUSED AUTOMATED CHECKS PASS; OWNER RELEASE|x64 REBUILD AND VISUAL REVIEW REQUIRED",
                     qualification="Dependent qualification not run; G8/SphereLattice blockers unchanged",
                     governance="No approval, seal, commit, publication or canonical reconciliation"))
    print(path)
    return 0


def label(value):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,47}", value):
        raise argparse.ArgumentTypeError("Use a short alphanumeric/hyphen/underscore attempt label")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("snapshot", "build", "test", "lifecycle", "final"))
    parser.add_argument("--profile", choices=("chain-wake", "mass-inertia", "mass-inertia-recovery"), default="chain-wake")
    parser.add_argument("--config", choices=CONFIGS, default="Release")
    parser.add_argument("--attempt", type=label, default="01", help="New output evidence attempt")
    parser.add_argument("--inputs", type=label, default="01", help="Existing build-inputs snapshot")
    parser.add_argument("--build-attempt", type=label, default="01")
    parser.add_argument("--test-attempt", type=label, default="01")
    parser.add_argument("--lifecycle-attempt", type=label, default="01")
    args = parser.parse_args()
    select_profile(args.profile)
    if args.action == "snapshot":
        return snapshot(args.attempt)
    if args.action == "build":
        return build(args.config, args.attempt, args.inputs)
    if args.action in ("test", "lifecycle"):
        return run_check(args.config, args.attempt, args.inputs, args.build_attempt, args.action == "lifecycle")
    return final(args.attempt, args.inputs, args.build_attempt, args.test_attempt, args.lifecycle_attempt)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f"CHAIN_WAKE_RUNNER_STOP: {error}", file=sys.stderr)
        raise SystemExit(1)
