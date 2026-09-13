"""Finalize the explicitly scoped revision without changing historical evidence."""

import collections
import datetime
import gzip
import json
from pathlib import Path
import subprocess
import sys

from pe_runtime import ROOT, DUMPBIN, sha
from revise_phase00 import EVIDENCE, ENTRY, GIT, write_json

REVIEW = ROOT / "docs/build/reviews/BUILD_PHASE_00_REVIEW.md"
OLD_SEAL = ROOT / ".codex/build/phase-00-initial-blocked-snapshot.json"
ACTIVE = ROOT / ".codex/build/phase-00-snapshot.json"


def git(*args):
    return subprocess.run(GIT + list(args), cwd=ROOT, capture_output=True, text=True, check=True).stdout


def relative(path):
    path = Path(path)
    return path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)


def json_read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def verify_history():
    revision = json_read(ENTRY)
    old = json_read(OLD_SEAL)
    assert sha(OLD_SEAL) == revision["initial_snapshot_sha256"]
    assert sha(EVIDENCE / "initial-blocked-snapshot.json") == revision["initial_snapshot_sha256"]
    assert sha(EVIDENCE / "initial-blocked-review.md") == revision["initial_review_sha256"]
    assert sha(ROOT / ".codex/build/phase-00-entry.json") == revision["original_entry_sha256"]
    assert git("rev-parse", "HEAD").strip() == revision["head"]
    assert git("branch", "--show-current").strip() == revision["branch"]
    assert git("ls-files", "--stage") == revision["index_entries"]
    assert sha(ROOT / git("rev-parse", "--git-path", "index").strip()) == revision["index_sha256"]
    protected = []
    for row in old["files"]:
        if row.get("mutable_control"):
            continue
        path = row["path"]
        original_candidate = row.get("checkpoint_member") and path != relative(REVIEW)
        tracked = row.get("previous_blob", "ABSENT") != "ABSENT"
        if original_candidate or tracked:
            current = sha(ROOT / path) if (ROOT / path).is_file() else "ABSENT"
            assert current == row["raw_sha256"], "Historical/tracked bytes changed: " + path
            protected.append({"path": path, "sha256": current, "reason": "immutable initial candidate" if original_candidate else "protected tracked file"})
    return revision, old, protected


def capture_records():
    records = [json_read(p) for p in sorted((EVIDENCE / "commands").glob("*.json"))]
    for record in records:
        archive = ROOT / record["compressed_log"]
        raw = ROOT / record["raw_log"]
        assert sha(archive) == record["compressed_sha256"] and sha(raw) == record["raw_sha256"]
        assert gzip.decompress(archive.read_bytes()) == raw.read_bytes()
    return records


def candidate_paths(revision, old):
    return sorted(set(old["exact_checkpoint_manifest"]) | {r["path"] for r in revision["new_owned_files"] if (ROOT / r["path"]).is_file()})


def table_row(path, old_files, revision):
    file = ROOT / path
    old = old_files.get(path, {})
    if not hasattr(table_row, "context"):
        phase_entry = json_read(ROOT / ".codex/build/phase-00-entry.json")
        table_row.context = ({r["path"]: r for r in phase_entry["files"]}, set(candidate_paths(revision, json_read(OLD_SEAL))))
    entries, candidates = table_row.context
    entry = entries.get(path, {})
    candidate = path in candidates
    local = path.startswith(".codex/") or path in ["AGENTS.md", "MANIFEST.md", "docs/build/BUILD_STATE.md"]
    classification = "LOCAL_GOVERNANCE" if local else "PHASE_OWNED_NEW_TRACKED" if candidate else "PROTECTED_UNRELATED"
    exists = file.is_file()
    raw = sha(file) if exists else "ABSENT"
    return {"path": path, "classification": classification, "entry_exists": entry.get("entry_exists", False),
            "revision_entry_exists": old.get("raw_sha256", "ABSENT") != "ABSENT",
            "entry_sha256": entry.get("entry_sha256", "ABSENT"), "revision_entry_sha256": old.get("raw_sha256", "ABSENT"),
            "raw_sha256": raw, "expected_git_blob": git("hash-object", "--path=" + path, path).strip() if exists else "ABSENT",
            "previous_blob": entry.get("previous_blob", "ABSENT"), "checkpoint_member": candidate,
            "operation": "unchanged" if raw == old.get("raw_sha256") else "modify" if old.get("raw_sha256", "ABSENT") != "ABSENT" else "add" if exists else "absent"}


def prepare():
    revision, old, protected = verify_history()
    assert json_read(ACTIVE)["workflow"] == "IN_PROGRESS", "Already sealed"
    records = capture_records()
    registry = json_read(EVIDENCE / "baseline-registry.json")
    original_closure = old["behavior_build_input_closure"]
    closure_rows = {}
    authorized_runtime = {r["path"]: r["expected_sha256"] for r in json_read(EVIDENCE / "runtime-plan.json")["destinations"]}
    transitions = []
    for row in original_closure["files"]:
        file = ROOT / row["path"]
        current = sha(file) if file.is_file() else "ABSENT"
        if current != row["sha256"]:
            assert authorized_runtime.get(row["path"]) == current and row["sha256"] == "ABSENT", "Unexpected old input change: " + row["path"]
            transitions.append({"path": row["path"], "before": row["sha256"], "after": current, "reason": "Authorized previously absent runtime output"})
        closure_rows[row["path"].casefold()] = {"path": row["path"], "sha256": current, "reason": "Original reference input; unchanged except explicit runtime transition"}
    additions = list((ROOT / "bin-int/build-phase-00-revision-01/tbb-origin").rglob("*"))
    additions += [ROOT / r["path"] for r in revision["new_owned_files"] if r["path"].endswith(".py")]
    additions += [ROOT / app / "imgui.ini" for app in ["Breakout", "RayTracing", "GEngineEditor", "RigidBodySimulation"]]
    additions += [ROOT / p for p in authorized_runtime]
    for file in additions:
        if file.is_file():
            path = relative(file)
            closure_rows[path.casefold()] = {"path": path, "sha256": sha(file), "reason": "Revision helper, verified runtime distribution or generated runtime configuration"}
    for audit in json_read(EVIDENCE / "runtime-audit-after.json"):
        for edge in audit["edges"]:
            if "system_sha256" in edge:
                path = edge["resolved"]
                assert sha(path) == edge["system_sha256"]
                closure_rows[path.casefold()] = {"path": path, "sha256": edge["system_sha256"], "reason": "Declared Windows/CRT import dependency"}
    for record in records:
        if not record["name"].startswith("startup-"):
            continue
        for line in (ROOT / record["raw_log"]).read_text(errors="replace").splitlines():
            if not line.startswith("{"):
                continue
            sample = json.loads(line)
            modules = sample.get("modules", [])
            if isinstance(modules, list):
                for module in modules:
                    path = module["path"]
                    assert sha(path) == module["sha256"], "Loaded runtime changed: " + path
                    closure_rows[path.casefold()] = {"path": path, "sha256": module["sha256"], "reason": "Actually observed loaded module, including graphics driver/OS runtime"}
    for resource in json_read(EVIDENCE / "resource-audit.json")["Breakout_Debug"]["failed_texture_requests"]:
        path = resource["resolved_path"]
        closure_rows[path.casefold()] = {"path": path, "sha256": "ABSENT", "reason": "Observed missing resource request; no source-tree alias added"}
    closure = {"revision": "01", "files": sorted(closure_rows.values(), key=lambda r: r["path"].casefold()),
               "original_closure_sha256": sha(ROOT / "docs/build/evidence/phase-00/input-closure.json"),
               "authorized_transitions": transitions, "compilation_inputs_changed": False,
               "generator_relationship": "Original normal Premake graph/builds retained. No revision configure/build/profile generation performed before the new scope stop. New helper supplies only exact missing runtime output bytes; application/test executables unchanged.",
               "system_runtime_closure": "runtime-audit-after.json records OS/CRT paths, architecture and hashes; startup records capture actual module paths/hashes, including NVIDIA driver modules and staged TBB.",
               "limits": "Not an accepted complete reference: full suites fail telemetry assertions, Debug normal benchmark fails, Breakout resource requests fail and shutdown faults, remaining app/profiling checks not run. Original compile evidence retained, not misreported as a new build."}
    write_json(EVIDENCE / "closure.json", closure)
    initial_environment = ROOT / "docs/build/evidence/phase-00/environment.json"
    write_json(EVIDENCE / "environment.json", {"initial_environment": relative(initial_environment), "initial_sha256": sha(initial_environment),
               "python": {"path": sys.executable, "version": sys.version, "sha256": sha(sys.executable)},
               "dumpbin": {"path": str(DUMPBIN), "sha256": sha(DUMPBIN)},
               "runtime_PATH": "C:\\Windows\\System32;C:\\Windows", "runtime_CWD": "C:/dev/GEngine-build/<application>",
               "selection": "Same explicit VS2022/MSVC/SDK identities as initial evidence; no installation, replacement or policy change. Official same-version TBB archive separately hash-verified."})
    checks = [
        ["Initial all-eight-target Debug/Release compiler, CRT and warning evidence", "INITIAL PASS; original inputs/executables unchanged; no new build claimed"],
        ["TBB import-library provenance and exact matching runtime", "PASS; byte-identical upstream x64 import libraries, publisher-provided archive checksum, PE x64 and runtime API 2021.5/interface 12050"],
        ["All eight direct and relevant transitive import inventories", "PASS; only two missing runtime destinations supplied"],
        ["Output-only staging and disposable safeguard", "PASS; missing/wrong DLL and protected-destination rejection; idempotence; original safeguards preserved"],
        ["Full Debug PhysicsTests (1800-second limit)", "COMPLETED FAIL: 80/18726; 185.766 seconds"],
        ["Full Release PhysicsTests (1800-second limit)", "COMPLETED FAIL: 80/18724; 13.094 seconds"],
        ["Five focused scene/CRT cases per configuration", "PASS: solver-storage, runtime-transform, scene-runtime-lifecycle, fixed-scheduling, absolute-scaling"],
        ["Fixed-input normal Debug benchmark", "FAIL: 0xc0000005; 32.313 seconds; no stdout"],
        ["Fixed-input normal Release benchmark", "PASS: six body counts, fixed warmup/samples/dt; 0.578 seconds"],
        ["Profiling benchmarks and associated builds", "NOT RUN after source/resource scope stop; no profiling generation to restore"],
        ["Debug RayTracing source-CWD startup", "PASS corrected capture: exact TBB loaded, SDL/OpenGL initialized, clean WM_CLOSE exit"],
        ["Debug Breakout source-CWD startup/resources", "FAIL: five malformed texture paths/11 fallback warnings; 0xc0000005 after WM_CLOSE"],
        ["Other Debug apps and all four Release app startups/resources", "NOT RUN after new out-of-scope resource blocker"],
        ["Final acceptance", "BLOCKED; no failing gate waived"]]
    write_json(EVIDENCE / "validation-summary.json", {"workflow": "BLOCKED", "approval_eligible": False, "checks": checks, "commands": records})
    for path in [EVIDENCE / "candidate-files.json", EVIDENCE / "git-final.json"]:
        if not path.exists():
            path.write_text("{}\n", encoding="utf-8")
    deferred = {relative(REVIEW), relative(EVIDENCE / "candidate-files.json"), relative(EVIDENCE / "git-final.json")}
    old_files = {r["path"]: r for r in old["files"]}
    candidates = candidate_paths(revision, old)
    rows = [table_row(path, old_files, revision) for path in candidates if path not in deferred]
    write_json(EVIDENCE / "candidate-files.json", {"files": rows, "deferred_to_local_seal": sorted(deferred),
               "immutable_history_and_tracked_hashes": protected, "exact_checkpoint_manifest": candidates,
               "excluded_runtime_outputs": list(authorized_runtime), "recursion_policy": "Final review/inventory/Git evidence hashes live only in the local seal."})
    diff = subprocess.run(GIT + ["diff", "--check"], cwd=ROOT, capture_output=True, text=True)
    assert diff.returncode == 0
    git_data = {"status_short": git("status", "--short"), "expanded_status": git("status", "--short", "--untracked-files=all"),
                "ignored_paths": git("ls-files", "--others", "--ignored", "--exclude-standard").splitlines(),
                "index_entries": git("ls-files", "--stage"), "index_sha256": revision["index_sha256"],
                "diff_check": {"argv": GIT + ["diff", "--check"], "cwd": str(ROOT), "exit_code": diff.returncode, "stdout": diff.stdout, "stderr": diff.stderr}}
    write_json(EVIDENCE / "git-final.json", git_data)
    write_review(revision, old, checks, records, rows, candidates, git_data, closure)
    print(json.dumps({"prepared": relative(REVIEW), "candidates": len(candidates), "closure_paths": len(closure["files"]), "workflow": "BLOCKED"}), flush=True)


def write_review(revision, old, checks, records, rows, candidates, git_data, closure):
    def link(name):
        return f"[{name}](../evidence/phase-00/revision-01/{name})"
    text = ["# BUILD PHASE 00 review: revision 01", "", "Workflow: **BLOCKED: acceptance criteria have not passed**", "",
            "Contract: [Capture the current Premake reference](../phases/BUILD_PHASE_00.md). Human command: `REVISE BUILD PHASE 00`, with explicit authorization for minimum runtime closure and resumed validation. Active phase remains 00; no successor, staging for checkpoint, commit, tag, push or approval.", "",
            "## Scope and behavior", "", "### Initial BLOCKED evidence (immutable)", "",
            "The initial review and snapshot are preserved byte for byte in " + link("initial-blocked-review.md") + " and " + link("initial-blocked-snapshot.json") + ". Original evidence files under `docs/build/evidence/phase-00/`, including the failed RayTracing launch, the 120-second PhysicsTests timeout, both build logs and the original failure registry, were not rewritten. Initial snapshot SHA-256: `86621b4bcbcbd46a79eaacc35de270b0b833b6eda9b1138a503b2b1eeb1f2a70`.", "",
            "`B00-RUNTIME-001` retains its historical missing-runtime failure. `B00-VALIDATION-002` retains its historical **incomplete** timeout classification. The new completed failures are separate records; they do not retroactively turn the initial timeout into a Physics failure.", "",
            "### Authorized revision work", "",
            "Added `stage_premake_runtime.py` as a narrowly scoped supplement after the existing Premake build. It writes only `bin/Debug/RayTracing/tbb12.dll` and `bin/Release/RayTracing/tbb12.dll`, both absent before this revision. It verifies the upstream archive, exact import-library match, DLL hash and x64 machine type; refuses tracked/unlisted destinations and mismatched pre-existing outputs; and performs no write for an already matching output. The original Premake graph and postbuild safeguards remain byte-unchanged. No source, resource, tracked runtime, compiler policy or dependency version changed.", "",
            "Replay the authorized output staging with `python scripts/build_validation/stage_premake_runtime.py stage`, then `python scripts/build_validation/stage_premake_runtime.py check`. The helper requires the exact verified upstream distribution retained in ignored `bin-int/build-phase-00-revision-01/tbb-origin`; its origin/hash is documented below. It has no machine-install or alternate-version fallback. The two DLL outputs are excluded from the checkpoint even though Git shows them as untracked.", "",
            "### New validation evidence and stop", "",
            "Runtime import closure is established, and Debug RayTracing loads the exact staged DLL, initializes SDL/OpenGL and exits normally in the corrected probe. Resumed validation then established failing normal-suite telemetry assertions, a Debug benchmark access violation, and Breakout resource-path failures plus an access violation after the normal close request.", "",
            "The specific scope stop is Breakout resource handling: the application passes paths already ending in `.png`; AssetsManager prepends the shared image directory and appends another `.png`. Logs show five malformed paths and 11 checkerboard-fallback warnings. Correcting this needs application/engine path-handling changes or source-tree resource aliases. Your revision explicitly prohibits both behavioral changes and staging into the source resource tree, so no repair or later profiling/app execution was attempted. Already running normal captures were allowed to finish; the isolated staging safeguard and preservation checks were completed.", "",
            "## Target and dependency impact", "",
            "The exact library read by both RayTracing link operations is `C:/dev/GEngine-build/external/tbb/lib/tbb12.lib`, SHA-256 `bb72fee5dfebcd6d17448751647a5027027ca4d9309fdae0d4b099c72873ba72`. Link read tlogs also name the original `tbb12_debug.lib`, `tbb.lib` and `tbb_debug.lib`; the latter aliases are byte-identical copies. No library list/order or target changed. Git records the TBB addition at commit `0706a009749651a1b92b57ab1b377a6cba90a979` (2022-05-19).",
            "",
            "Provenance is established by byte identity with the [official oneTBB 2021.5.0 Windows distribution](https://github.com/uxlfoundation/oneTBB/releases/tag/v2021.5.0), not a same-named DLL search. Its published archive SHA-256 is `096c004c7079af89fe990bb259d58983b0ee272afa3a7ef0733875bfe09fcd8e`, verified before use. Both desktop and UWP import libraries are byte-identical in that archive; the existing desktop ConsoleApp graph selects the desktop `redist/intel64/vc14/tbb12.dll` from the same distribution.", "",
            "Selected DLL SHA-256: `3bda7c5458d7f43bcd49f0dead5114eac1ea14f573aafc736f833089b1d2cd79`; PE machine `0x8664`; runtime API version `2021.5`, interface `12050`, matching the repository 2021.5.0/interface-12050 headers. See " + link("tbb-release.json") + ", " + link("tbb-package.json") + ", and " + link("tbb-import-libraries.json") + ".",
            "",
            "All four apps in both configurations have direct import captures and relevant non-system transitive inventories: " + link("runtime-audit-before.json") + " and " + link("runtime-audit-after.json") + ". Windows/CRT files are declared by actual paths, machine types and hashes. App launches use only `C:/Windows/System32;C:/Windows` as PATH and each application's actual source directory as CWD. Release Breakout/RigidBodySimulation/RayTracing do not import Assimp after optimization; no extra Assimp copies were needed. The only supplied files are the two TBB outputs.", "",
            "## Validation and Premake reference comparison", "",
            "Initial source-tag compiler evidence remains immutable: eight targets, 133 target/TU pairs per configuration, actual C++20 commands, Debug `/MTd`, Release `/MT`, GLAD `/TC`, VS2022 v143 14.44.35207 and SDK 10.0.26100.0. All compilation inputs, generated projects and executables match the initial capture. No new compilation or profiling generation is claimed. Runtime changes were validated by new import/loader/test captures.", "",
            "| Mandatory check | Revision result |", "| --- | --- |"]
    text += ["| " + name + " | " + result + " |" for name, result in checks]
    text += ["", "Full tests used an explicitly recorded 1,800-second limit. Debug completed in 185.766 seconds and Release in 13.094 seconds. Both fail exactly 80 instances of `contact telemetry counts all four generated face contacts and solver constraints`; all other checks pass. Normal `GetPhysicsProfileSnapshot()` returns an empty snapshot when `GE_ENABLE_PHYSICS_PROFILING` is absent, while this test unconditionally expects counters equal to four. No test, counter, macro or normal-generation policy was changed.", "",
             "Focused cases run in **both** configurations: `--solver-storage`, `--runtime-transform`, `--scene-runtime-lifecycle`, `--fixed-scheduling`, `--absolute-scaling`; all ten pass. The fixed normal benchmark inputs were `--body-counts=50,100,200,500,1000,2000 --warmup=2 --samples=5 --dt=0.008333333`. Release completes; Debug exits `0xc0000005` with empty stdout. Profiling was never generated, so normal generation remains intact and no restoration mutation is required.", "",
             "The first revision RayTracing probe deliberately hid the SDL window but incorrectly required visibility and failed to send WM_CLOSE to hidden windows. That raw capture is retained as a harness limitation. The separately named corrected capture identifies the SDL window by class, verifies loaded module hashes, and closes normally. Later probes also capture the application's generated GEngine.log inside their compressed raw evidence. No silent overwrite or relabeling of raw captures occurred.", "",
             "Every exact argv/CWD/config/start/duration/timeout/exit and raw/compressed log hash is retained in " + link("validation-summary.json") + " and individual `commands/*.json` entries listed in the snapshot. Gzip stores verbatim new captures, including whitespace; decompression is checked against retained raw output. Runtime timings are observational, not performance acceptance thresholds.", "",
             "| Capture | Result / exit | Raw SHA-256 |", "| --- | --- | --- |"]
    text += [f"| [{r['name']}](../evidence/phase-00/revision-01/commands/{r['name']}.json) | {r['result']} / {r['exit_code']} | `{r['raw_sha256']}` |" for r in records]
    text += ["", "## Warning status and baseline-known failures", "", "| Evidence | First-party compiler warnings | Linker warnings | Vendor warnings |", "| --- | --- | --- | --- |",
             "| Initial Debug compilation, unchanged inputs | 0 | 0 | 986 C4996/STL4043 |", "| Initial Release compilation, unchanged inputs | 0 | 0 | 0 |",
             "| Revision runtime-only code/evidence | Not applicable: no compilation run | Not applicable | Not applicable |", "",
             "Original warning classification and external instantiation contexts remain in the original compiler audit. No suppressions or language/CRT changes were added. Runtime texture warnings are separate from compiler warning gates.", "",
             "The full lineage registry is " + link("baseline-registry.json") + ". `B00-RUNTIME-001` is resolved for the demonstrated missing dependency; the original failure remains recorded. `B00-VALIDATION-002` remains historical incomplete evidence. New records: `B00-VALIDATION-003` (80 telemetry assertions/config), `B00-RUNTIME-004` (Breakout texture paths), `B00-RUNTIME-005` (Breakout close access violation), and `B00-VALIDATION-006` (Debug normal benchmark access violation). Crash causes are unproven; no failing gate is grandfathered or approved.", "",
             "Resource evidence and limits are in " + link("resource-audit.json") + ". Debug RayTracing log establishes SDL 2.0.20, NVIDIA OpenGL 4.6, font/scene/renderer/camera initialization. Breakout's intended five source images exist and remain unchanged, but its actual malformed requests do not resolve. Basic font/level/bank/model existence checks for other apps are availability evidence only, not claims of successful unrun launches.", "",
             "## Human review checklist", "", "- [x] Preserve initial review, seal, registry and raw failure captures.", "- [x] Prove same-distribution TBB import/runtime identity and all eight static import closures.", "- [x] Stage only two previously absent runtime outputs; preserve all tracked bytes and the index.", "- [x] Validate disposable missing/corrupt-runtime and protected-destination guards.", "- [x] Complete both full suites, ten focused cases and normal benchmark captures with accurate outcomes.", "- [ ] Full suites, Debug benchmark and Breakout resource/shutdown acceptance pass.", "- [ ] Complete profiling and remaining app startup/resource matrix.", "- [x] Seal a BLOCKED revision; no checkpoint/publication or next phase.", "",
             "## Final Validation Snapshot", "", "Phase 00, revision 01; timestamp " + datetime.datetime.now(datetime.timezone.utc).isoformat() + ". Validated source/build HEAD: `" + revision["head"] + "`; branch `" + revision["branch"] + "`.",
             "Source/predecessor tag `render-refactor-phase-00-approved` peels to that same commit. Initial published tag object `f1a174b35ea0a07b64dafc6eb697ce83f94420c4` and remote verification are retained in the historical seal. Configured origin remains `https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git`. There is no approved Build checkpoint or passing reference.", "",
             "Revision entry receipt `.codex/build/phase-00-revision-01-entry.json` SHA-256: `" + sha(ENTRY) + "`. It retains the original phase-entry protection and freezes new filenames before writes. " + link("revision-entry.json") + " is the versionable mirror. No broad ownership transfer occurred.", "",
             "Complete recorded behavior/build input closure: " + link("closure.json") + " (`" + sha(EVIDENCE / "closure.json") + "`, " + str(len(closure["files"])) + " exact paths). Initial compiler/toolchain/source/SDK inputs are unchanged; the explicit transition is two ABSENT TBB paths becoming the verified runtime. The closure adds the distribution, revision helpers, generated runtime configuration and absent malformed resource requests. Input presence/deletion is part of validation identity. Runtime closure limits are explicit; this is not a complete passing reference.", "",
             "Exact four-class file inventory, entry/revision existence, raw and expected Git blob hashes, protected hashes and checkpoint membership: " + link("candidate-files.json") + ". The final local seal adds its finalized review/inventory/Git-document hashes to avoid self-hash recursion, plus exact ignored/generated output classification. All candidate files are PHASE_OWNED_NEW_TRACKED because no tracked production file changed; PHASE_OWNED_TRACKED is empty.", "",
             "| Exact current candidate path | Raw SHA-256 | Expected Git blob |", "| --- | --- | --- |"]
    lookup = {r["path"]: r for r in rows}
    for path in candidates:
        row = lookup.get(path, {})
        text.append(f"| `{path}` | {row.get('raw_sha256', 'Final local seal')} | {row.get('expected_git_blob', 'Final local seal')} |")
    text += ["", "LOCAL_GOVERNANCE excludes AGENTS, MANIFEST, skills, state and local receipts from checkpoints. Protected unrelated tracked files and original candidate evidence retain their entry hashes. New validation logs, copies, downloaded distribution, .ini files and two TBB outputs are generated artifacts. GEngine.log is the program's generated log (Log.cpp opens it in truncate mode); its updates are output effects, not overwrites of historical captures.", "",
             "Literal `git status --short`:", "", "```text", git_data["status_short"].rstrip(), "```", "",
             "Literal `git diff --check`: empty stdout/stderr, exit 0. Index entries/hash are unchanged: `" + revision["index_sha256"] + "`. Expanded status and every ignored path are in " + link("git-final.json") + ". New-text whitespace results are recorded after finalization in the local seal. Original verbatim MSBuild logs retain their already documented whitespace diagnostics; they are immutable historical evidence. Gzip new captures are compared as bytes and not falsely reported as whitespace-checked text.", "",
             "Context expansion: actual Debug/Release TBB link read tlogs and Git history for provenance; official versioned archive for byte matching; PE direct/transitive imports and actual loaded modules for runtime closure; current source-CWD startup/Log/SDL/ImGui code for capture semantics; PhysicsTests:2682 and PhysicsProfile source for the completed telemetry failure; BreakoutApp/GameLevel, AssetsManager and Texture loader for the observed resource-path scope stop. Original broad audit/roadmap/history was not reloaded. No Physics/rendering behavior repairs were attempted.", "",
             "Local seal: `.codex/build/phase-00-snapshot.json`, workflow BLOCKED and approval_eligible=false. Its SHA-256 is stored in local BUILD_STATE; it does not hash itself. Mutable control exclusions are BUILD_STATE, the active seal and later approval/rejection receipts; all actual behavior/build inputs are hashed. Any subsequent candidate edit invalidates this receipt; any behavior/input edit invalidates affected validation and requires authorized revision.", "",
             "## Approval and rejection boundary", "",
             "The exact potential checkpoint manifest is in candidate-files.json, but approval is blocked and no file has been staged. The two runtime DLL outputs are excluded. No commit/tag/push/approval/Phase 01 action occurred.", "",
             "For a later authorized rejection, use the rejection skill and original source checkpoint. Preserve all 55 bootstrap files that existed at original entry, historical original/revision evidence and unrelated bytes. No tracked source needs restoring. Only verified phase-created files/output paths can be considered for isolated removal under that separate command; no broad delete/restore is authorized here.", "",
             "Next decision: the user must explicitly authorize any additional preparation contract for the demonstrated source/test behavior issues before they can be repaired. Current runtime-only authorization does not cover changing texture path semantics, telemetry expectations/profiling policy, or application/Physics code to repair access violations. **Final acceptance remains BLOCKED.**", ""]
    REVIEW.write_text("\n".join(text), encoding="utf-8")


def seal():
    revision, old, protected = verify_history()
    assert json_read(ACTIVE)["workflow"] == "IN_PROGRESS", "Do not replace a finalized seal"
    assert "Workflow: **BLOCKED" in REVIEW.read_text()
    closure = json_read(EVIDENCE / "closure.json")
    for row in closure["files"]:
        path = ROOT / row["path"]
        assert (sha(path) if path.is_file() else "ABSENT") == row["sha256"], "Closure changed since preparation: " + row["path"]
    records = capture_records()
    old_files = {r["path"]: r for r in old["files"]}
    paths = set(git("ls-files").splitlines()) | set(git("ls-files", "--others", "--exclude-standard").splitlines()) | set(git("ls-files", "--others", "--ignored", "--exclude-standard").splitlines())
    paths |= {r["path"] for r in revision["new_owned_files"]}
    paths |= {r["path"] for r in old["files"]}
    mutable = {"docs/build/BUILD_STATE.md", ".codex/build/phase-00-snapshot.json"}
    files = []
    whitespace = []
    for path in sorted(paths):
        if path in mutable:
            files.append({"path": path, "classification": "LOCAL_GOVERNANCE", "mutable_control": True, "checkpoint_member": False})
            continue
        row = table_row(path, old_files, revision)
        files.append(row)
        if row["checkpoint_member"] and not path.endswith(".gz"):
            args = GIT + ["diff", "--no-index", "--check", "--", "NUL", path]
            check = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
            historical_raw = path in ["docs/build/evidence/phase-00/build-Debug.txt", "docs/build/evidence/phase-00/build-Release.txt"]
            clean = check.returncode in (0, 1) and not check.stdout
            assert clean or historical_raw and check.returncode == 3, "Unexpected whitespace finding: " + path
            whitespace.append({"argv": args, "cwd": str(ROOT), "exit_code": check.returncode, "stdout": check.stdout, "stderr": check.stderr,
                               "result": "PASS" if clean else "IMMUTABLE_INITIAL_RAW_EVIDENCE_WHITESPACE; no passing-approval exception"})
    git_data = json_read(EVIDENCE / "git-final.json")
    assert git("status", "--short") == git_data["status_short"]
    assert git("ls-files", "--stage") == revision["index_entries"]
    candidate = [r["path"] for r in files if r["checkpoint_member"]]
    assert candidate == json_read(EVIDENCE / "candidate-files.json")["exact_checkpoint_manifest"]
    snapshot = {"phase": "00", "revision": "01", "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "workflow": "BLOCKED", "approval_eligible": False, "validated_head": revision["head"], "branch": revision["branch"],
                "remote": old["remote"], "source_baseline": old["source_baseline"], "previous_approved_tag": old["previous_approved_tag"],
                "previous_approved_peeled_commit": old["previous_approved_peeled_commit"], "remote_verification": old["remote_verification"],
                "approved_reference_identity": "NONE; failed criteria not waived", "revision_entry_sha256": sha(ENTRY),
                "original_entry_sha256": revision["original_entry_sha256"], "initial_snapshot_sha256": sha(OLD_SEAL),
                "initial_review_sha256": revision["initial_review_sha256"], "review_sha256": sha(REVIEW),
                "files": files, "exact_checkpoint_manifest": candidate, "protected_history_and_tracked_hashes": protected,
                "behavior_build_input_closure": closure, "validation_commands": records,
                "validation_summary": json_read(EVIDENCE / "validation-summary.json"), "baseline_failures": json_read(EVIDENCE / "baseline-registry.json"),
                "runtime_plan": json_read(EVIDENCE / "runtime-plan.json"), "git": git_data, "new_text_whitespace": whitespace,
                "initial_warning_counts": old["warning_counts"], "revision_compilation": "NOT RUN; original compilation evidence retained with unchanged compilation input hashes",
                "mutable_control_exclusions": {"docs/build/BUILD_STATE.md": "local workflow metadata", ".codex/build/phase-00-snapshot.json": "active nonrecursive seal", "future_local_transactions": "approval/rejection receipts without build input"},
                "context_expansion": revision["context_expansion"] + ["Startup/Log/SDL/ImGui sources for source-CWD capture and hidden-window semantics", "PhysicsTests and PhysicsProfile telemetry sections for completed failures", "BreakoutApp, GameLevel, AssetsManager and Texture load sections for observed out-of-scope resource requests"],
                "limits": closure["limits"]}
    write_json(ACTIVE, snapshot)
    state = ["# Build System Migration state", "", "Track: Build System Migration", "Worktree: C:\\dev\\GEngine-build", "Branch: " + revision["branch"],
             "Configured remote: origin", "Remote fetch/push URL: https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git",
             "Source baseline: render-refactor-phase-00-approved", "Source baseline / verified HEAD: " + revision["head"], "",
             "Current approved Build phase: NONE", "Current approved Build tag: NONE", "Current approved Build commit: NONE",
             "Active Build phase: 00", "Active revision: 01", "Next expected Build phase: 00", "Current workflow state: BLOCKED",
             "Pending approval transaction: NONE", "Final phase validation snapshot: .codex/build/phase-00-snapshot.json", "Snapshot SHA-256: " + sha(ACTIVE), "Snapshot eligibility: BLOCKED; not approvable", "",
             "Initial BLOCKED snapshot: .codex/build/phase-00-initial-blocked-snapshot.json", "Initial snapshot SHA-256: " + sha(OLD_SEAL),
             "Original phase entry remains immutable: .codex/build/phase-00-entry.json", "Revision entry: .codex/build/phase-00-revision-01-entry.json",
             "Review: docs/build/reviews/BUILD_PHASE_00_REVIEW.md", "Revision evidence: docs/build/evidence/phase-00/revision-01/", "",
             "Revision authorization: minimum existing-Premake runtime closure and resumed validation; no behavior/version/toolchain/graph changes.",
             "B00-RUNTIME-001: historical missing-TBB failure preserved; matching verified runtime now staged in two previously absent RayTracing outputs, Debug loader/startup proof passes.",
             "B00-VALIDATION-002: historical 120-second timeout remains INCOMPLETE; new full runs completed independently.",
             "New blockers: B00-VALIDATION-003 (80 telemetry assertions/config), B00-RUNTIME-004 (Breakout resource path/fallbacks), B00-RUNTIME-005 (Breakout close access violation), B00-VALIDATION-006 (Debug normal benchmark access violation).",
             "Focused scene/CRT tests: 10/10 PASS. All eight static runtime import closures and disposable copied-output safeguards: PASS.",
             "Profiling and remaining app startup/resource checks: NOT RUN after out-of-scope resource behavior blocker; normal generation unchanged.",
             "Next action: a separate explicit human decision is needed for any additional source/test preparation contract. Do not start Phase 01.", "",
             "Tracking: LOCAL_GOVERNANCE, deliberately local-only and unignored; never stage this file. AGENTS/MANIFEST/.codex remain ignored.",
             "Preserve all initial raw captures and historical review/registry identities. No approved exception for failed gates.",
             "Commit / tag / push / approval / Phase 01: NONE. Production CMake/Conan migration: NOT PERFORMED.",
             "Approval persistence: no approved fields change until a separately authorized exact checkpoint commit, annotated tag, branch push and tag push all succeed and verify.", ""]
    (ROOT / "docs/build/BUILD_STATE.md").write_text("\n".join(state), encoding="utf-8")
    print(json.dumps({"workflow": "BLOCKED", "seal_sha256": sha(ACTIVE), "review_sha256": sha(REVIEW), "candidate_files": len(candidate),
                      "history_and_tracked_files_verified": len(protected), "whitespace_checks": len(whitespace)}, indent=2), flush=True)


if __name__ == "__main__":
    {"prepare": prepare, "seal": seal}[sys.argv[1]]()
