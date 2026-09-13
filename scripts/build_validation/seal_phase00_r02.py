"""Audit and seal the scoped Revision 02 candidate, preserving both prior reviews."""

import datetime
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import sys

from pe_runtime import ROOT, sha, resolve_closure
from revise_phase00_r02 import EVIDENCE, ENTRY, WORK, APPS, allowed_paths, write_json

GIT = ["git", "-c", "safe.directory=C:/dev/GEngine-build"]
REVIEW = ROOT / "docs/build/reviews/BUILD_PHASE_00_REVIEW.md"
OLD = ROOT / ".codex/build/phase-00-revision-01-snapshot.json"
ACTIVE = ROOT / ".codex/build/phase-00-snapshot.json"
MODIFIED = ["Breakout/src/BreakoutApp.cpp", "GEngine/src/Managers/AssetsManager.cpp",
            "PhysicsBenchmark/src/main.cpp", "PhysicsTests/src/main.cpp"]


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def git(*args):
    return subprocess.run(GIT + list(args), cwd=ROOT, capture_output=True, text=True, check=True).stdout


def relative(path):
    path = Path(path).resolve()
    return path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)


def digest(path):
    return sha(path) if Path(path).is_file() else "ABSENT"


def verify_history():
    entry, old = read(ENTRY), read(OLD)
    assert sha(OLD) == entry["previous_snapshot_sha256"]
    assert sha(EVIDENCE / "revision-01-snapshot.json") == sha(OLD)
    assert sha(EVIDENCE / "revision-01-review.md") == entry["previous_review_sha256"]
    assert sha(ROOT / ".codex/build/phase-00-entry.json") == entry["original_entry_sha256"]
    assert git("rev-parse", "HEAD").strip() == entry["head"]
    assert git("branch", "--show-current").strip() == entry["branch"]
    assert git("rev-parse", entry["previous_tag"] + "^{}").strip() == entry["previous_peeled"]
    assert git("ls-files", "--stage").strip() == entry["index_entries"]
    assert sha(ROOT / git("rev-parse", "--git-path", "index").strip()) == entry["index_sha256"]
    protected = []
    for row in entry["protected"]:
        if row["path"] in MODIFIED + [relative(REVIEW)]:
            continue
        assert digest(ROOT / row["path"]) == row["sha256"], row["path"]
        protected.append(row)
    original_commands = read(ROOT / "docs/build/evidence/phase-00/commands.json")
    for record in original_commands:
        assert digest(ROOT / record["log"]) == record["log_sha256"]
    revision_commands = list((ROOT / "docs/build/evidence/phase-00/revision-01/commands").glob("*.json"))
    for path in revision_commands:
        record = read(path)
        assert digest(ROOT / record["raw_log"]) == record["raw_sha256"]
        assert digest(ROOT / record["compressed_log"]) == record["compressed_sha256"]
    changed = git("diff", "--name-only").splitlines()
    assert sorted(changed) == sorted(MODIFIED), changed
    return entry, old, protected


def captures():
    records = [read(p) for p in sorted((EVIDENCE / "commands").glob("*.json"))]
    for record in records:
        assert digest(ROOT / record["raw_log"]) == record["raw_sha256"]
        assert digest(ROOT / record["compressed_log"]) == record["compressed_sha256"]
        assert gzip.decompress((ROOT / record["compressed_log"]).read_bytes()) == (ROOT / record["raw_log"]).read_bytes()
    return records


def compiler_audit():
    import finalize_phase00 as prior
    copied = WORK / "compiler-normal"
    copied.mkdir(exist_ok=True)
    for config in ["Debug", "Release"]:
        destination = copied / ("build-" + config + ".txt")
        source = WORK / ("build-normal-" + config + ".txt")
        if not destination.exists():
            shutil.copyfile(source, destination)
        assert sha(destination) == sha(source)
    prior.EVIDENCE = copied
    result = prior.audit_compilers(read(ROOT / ".codex/build/phase-00-entry.json"))
    for config, value in result["configurations"].items():
        value["canonical_capture"] = relative(EVIDENCE / "commands" / ("build-normal-" + config + ".json"))
        assert value["flag_gate"] == value["coverage_gate"] == "PASS"
        assert value["warning_counts"]["first_party"] == value["warning_counts"]["linker"] == 0
    result["revision"] = "02"
    result["mode"] = "normal; explicit Rebuild of all eight targets; tracked runtime preservation verified"
    write_json(EVIDENCE / "compiler-audit.json", result)
    return result


def input_closure(old):
    rows, changes = {}, []
    for row in old["behavior_build_input_closure"]["files"]:
        name = relative(ROOT / row["path"])
        current = digest(ROOT / name)
        if current != row["sha256"]:
            generated = name.startswith(("bin-int/Debug/", "bin-int/Release/", "external/glad/bin-int/", "external/glad/bin/"))
            generated |= name.startswith("bin/") and Path(name).suffix in [".exe", ".lib"]
            generated |= name == "GEngineEditor/imgui.ini"
            assert name in MODIFIED or generated, "Unexpected behavior/input transition: " + name
            changes.append({"path": name, "before": row["sha256"], "after": current,
                            "reason": "Authorized root-cause correction" if name in MODIFIED else "Generated rebuild output or application layout output"})
        rows[name.casefold()] = {"path": name, "sha256": current, "reason": "Inherited verified source/tool/SDK/resource/runtime closure"}
    additions = [ROOT / n for n in ["scripts/build_validation/revise_phase00_r02.py", "scripts/build_validation/seal_phase00_r02.py"]]
    additions += list((ROOT / ".codex/build").glob("phase-00-revision-02-*.json"))
    additions += [OLD]
    additions += [p for p in WORK.rglob("*") if p.is_file() and p.name != "compiler-audit.json"]
    for config in ["Debug", "Release"]:
        for app in APPS + ["PhysicsTests", "PhysicsBenchmark"]:
            additions += list((ROOT / "bin" / config / app).glob("*.pdb"))
    for path in sorted((EVIDENCE / "commands").glob("startup-*.txt.gz")):
        for line in gzip.decompress(path.read_bytes()).decode(errors="replace").splitlines():
            try:
                sample = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(sample.get("modules"), list):
                additions += [Path(m["path"]) for m in sample["modules"]]
    for file in additions:
        name = relative(file)
        rows[name.casefold()] = {"path": name, "sha256": digest(file), "reason": "Revision investigation/build/observed runtime input or retained raw evidence"}
    return {"files": sorted(rows.values(), key=lambda r: r["path"].casefold()), "transitions": changes,
            "generator_relationship": "Unchanged Premake scripts + bundled Premake vs2022 -> same eight projects -> pinned VS2022 MSBuild normal Debug/Release rebuild -> audited actual CL/link commands and tlogs -> new binaries. No profiling generation occurred in Revision 02.",
            "toolchain_policy": "C++20; Debug /MTd; Release /MT; GLAD C /TC; MSVC 14.44.35207 HostX86/x64, SDK 10.0.26100.0. No dependency/version/target-ownership changes.",
            "limits": "Complete conservative captured-input closure for the blocked candidate; remaining mandatory runtime/resource/profiling acceptance is explicitly NOT RUN, so this is not an approved reference."}


def candidate_paths(old):
    return sorted(set(old["exact_checkpoint_manifest"]) | set(MODIFIED) |
                  {p for p in allowed_paths() if (ROOT / p).is_file()})


def file_table(paths, candidates):
    entry = {r["path"]: r for r in read(ROOT / ".codex/build/phase-00-entry.json")["files"]}
    revision = {r["path"]: r for r in read(ENTRY)["protected"]}
    rows = []
    for name in sorted(paths):
        old = entry.get(name, {})
        current = digest(ROOT / name)
        local = name.startswith(".codex/") or name in ["AGENTS.md", "MANIFEST.md", "docs/build/BUILD_STATE.md"]
        candidate = name in candidates
        kind = "LOCAL_GOVERNANCE" if local else "PHASE_OWNED_TRACKED" if name in MODIFIED else "PHASE_OWNED_NEW_TRACKED" if candidate else "PROTECTED_UNRELATED"
        mutable = name in [".codex/build/phase-00-snapshot.json", "docs/build/BUILD_STATE.md"]
        row = {"path": name, "classification": kind, "entry_exists": old.get("entry_exists", False),
               "entry_sha256": old.get("entry_sha256", "ABSENT"), "revision_entry_sha256": revision.get(name, {}).get("sha256", "ABSENT"),
               "previous_blob": old.get("previous_blob", "ABSENT"), "checkpoint_member": candidate}
        if mutable:
            row["mutable_control"] = True
        else:
            row.update({"raw_sha256": current, "expected_git_blob": git("hash-object", "--path=" + name, name).strip() if current != "ABSENT" else "ABSENT",
                        "operation": "modify" if name in MODIFIED or name == relative(REVIEW) else "unchanged" if current == old.get("entry_sha256") else "add" if current != "ABSENT" else "absent"})
        rows.append(row)
    return rows


def prepare():
    entry, old, protected = verify_history()
    assert read(ACTIVE)["workflow"] == "IN_PROGRESS"
    records = captures()
    audit = compiler_audit()
    runtime = [resolve_closure(ROOT / "bin" / c / a / (a + ".exe")) for c in ["Debug", "Release"] for a in APPS]
    assert all(not r["missing"] for r in runtime)
    write_json(EVIDENCE / "runtime-audit.json", {"result": "PASS", "audits": runtime, "TBB": "Accepted Revision 01 exact distribution/runtime and staging helper preserved; no repeated dependency work or staging writes"})
    startups = {}
    for p in sorted((EVIDENCE / "commands").glob("startup-*.txt.gz")):
        lines = gzip.decompress(p.read_bytes()).decode(errors="replace").splitlines()
        startups[p.name] = json.loads(lines[-1])
    resources = []
    for app in APPS:
        paths = [ROOT / app / "../GEngine/include/GEngine/Assets/Fonts/OpenSans-Regular.ttf",
                 ROOT / app / "../GEngine/include/GEngine/Assets/Fonts/OpenSans-Bold.ttf"]
        if app == "Breakout":
            paths += [ROOT / app / ("../Breakout/include/images/" + n + ".png") for n in ["background", "paddle", "awesomeface_r", "block", "block_solid"]]
            paths += list((ROOT / "Breakout/include/levels").glob("*.lvl"))
        resources.append({"app": app, "cwd": str(ROOT / app), "checked_paths": [{"path": str(p), "resolved": str(p.resolve()), "sha256": digest(p)} for p in paths]})
    write_json(EVIDENCE / "resource-audit.json", {"status": "PARTIAL / BLOCKED", "startup_results": startups,
               "source_cwd_file_checks": resources, "Breakout_Debug": "PASS: no malformed paths, no fallback warnings, initialized 2D renderer and normal exit 0; intended images and levels unchanged",
               "limits": "Release app/resource launches and other Debug applications NOT RUN after new Editor blocker; file availability alone is not a resource-load pass."})
    write_json(EVIDENCE / "safeguard-results.json", {"revision_02_disposable_rerun": "NOT RUN after new Editor scope blocker", "historical_revision_01_result": "PASS; preserved, not a fresh run", "historical_evidence": "../revision-01/safeguard-results.json", "historical_sha256": sha(EVIDENCE.parent / "revision-01/safeguard-results.json"), "current_import_closure": "PASS for all eight outputs", "runtime_staging_changes": "NONE; accepted source and output DLL identities unchanged"})
    registry = {"initial": {"B00-RUNTIME-001": "Historical missing TBB; resolved by accepted Revision 01, unchanged", "B00-VALIDATION-002": "Historical 120-second timeout remains INCOMPLETE, never a Physics failure"},
                "revision_01": {"B00-VALIDATION-003": "Corrected conditional telemetry expectation; full normal Debug/Release tests PASS; profiling validation remains pending", "B00-RUNTIME-004": "Corrected explicit relative path contract; Debug Breakout no fallback warnings; Release validation pending", "B00-RUNTIME-005": "Corrected duplicate Breakout shared-manager cleanup after live stack capture; Debug normal close PASS; Release pending", "B00-VALIDATION-006": "Corrected benchmark logger initialization after live null-logger stack capture; Debug/Release normal fixed-input benchmark PASS"},
                "new_blocker": {"id": "B00-RUNTIME-007", "target": "GEngineEditor Debug", "result": "BLOCKED / additional source scope required", "signature": "Late normal close causes 0xc0000005, freed-heap pointer, Texture destructor -> AssetsManager -> BaseApp destructor -> SceneApp destructor", "evidence": "commands/new-blocker-Editor-Debug-cdb.txt.gz", "cause": "SceneApp.cpp:42-44 duplicates shared manager cleanup already performed by BaseApp; Editor is an additional unchanged caller outside the four authorized fixes", "related_cause": "Same deletion pattern as Breakout B00-RUNTIME-005; separate caller and acceptance failure, not silently adopted into its repair", "additional_observation": "Earlier 45-second close probe ended 0x80000003 with Framebuffer status error. Later no-close startup stayed alive until a separately captured close at ~153 seconds, then produced the independent shutdown AV. The early-close framebuffer assertion location/root is not established or waived.", "smallest_proposed_scope": "Explicitly authorize Editor shutdown correctness for duplicate shared cleanup plus investigation of the early-close framebuffer assertion, preserve all rendering behavior/resources/dependencies/toolchain/graph, then resume the complete acceptance matrix."},
                "final_acceptance": "BLOCKED; no failed or missing mandatory gate waived"}
    write_json(EVIDENCE / "baseline-registry.json", registry)
    not_run = [p.name[:-5] for p in [ROOT / r["path"] for r in entry["new_owned_files"]]
               if p.parent.name == "commands" and p.suffix == ".json" and not p.exists()]
    write_json(EVIDENCE / "validation-matrix.json", {"executed": records, "reserved_but_not_run": sorted(not_run), "stop": registry["new_blocker"], "counts_note": "CDB capture PASS means diagnostics/dump captured; it is an application failure. Original two retry exits FAIL include optional dump-path failures despite successful live stacks. No capture metadata is relabeled."})
    write_json(EVIDENCE / "lineage.json", {"revision": "02", "entry_sha256": sha(ENTRY), "previous_seal_sha256": sha(OLD), "previous_review_sha256": entry["previous_review_sha256"], "initial_seal_sha256": old["initial_snapshot_sha256"], "authorization": entry["authorization"], "ownership_receipts": [{"path": relative(p), "sha256": sha(p)} for p in sorted((ROOT / ".codex/build").glob("phase-00-revision-02-ownership-*.json"))]})
    closure = input_closure(old)
    write_json(EVIDENCE / "integrity.json", {"result": "PASS", "protected": protected, "protected_count": len(protected), "tracked_runtime_count": sum(r["path"].startswith("bin/") for r in protected), "index_sha256": entry["index_sha256"], "source_changes": MODIFIED, "all_prior_raw_captures": "Verified against their immutable recorded hashes", "behavior_build_input_closure": closure})
    write_json(EVIDENCE / "git-final.json", {"status": "Pending final review whitespace and expanded Git inventory; finalized before the local seal"})
    candidates = candidate_paths(old)
    deferred = [relative(REVIEW), relative(EVIDENCE / "candidate-inventory.json"), relative(EVIDENCE / "git-final.json")]
    candidates = sorted(set(candidates) | set(deferred))
    write_json(EVIDENCE / "candidate-inventory.json", {"exact_checkpoint_manifest": candidates, "files": file_table(set(candidates) - set(deferred), candidates), "deferred_final_hashes": {p: "Finalized after this inventory; exact SHA-256 and expected Git blob are in the local final seal" for p in deferred}})
    print(json.dumps({"candidates": len(candidates), "protected": len(protected), "input_closure": len(closure["files"]), "records": len(records), "warnings": {c: a["warning_counts"] for c, a in audit["configurations"].items()}}))


def seal():
    entry, old, protected = verify_history()
    assert read(ACTIVE)["workflow"] == "IN_PROGRESS"
    candidates = read(EVIDENCE / "candidate-inventory.json")["exact_checkpoint_manifest"]
    assert set(candidates) == set(candidate_paths(old))
    text_checks = []
    for p in candidates:
        if p == relative(EVIDENCE / "git-final.json"):
            continue
        if p in old["exact_checkpoint_manifest"] and p != relative(REVIEW):
            continue
        if Path(p).suffix not in [".md", ".json", ".py", ".txt"]:
            continue
        result = subprocess.run(GIT + ["diff", "--no-index", "--check", "--", "NUL", p], cwd=ROOT, capture_output=True, text=True)
        assert result.returncode in [0, 1] and not result.stdout, (p, result.stdout)
        text_checks.append({"path": p, "exit_code": result.returncode, "stdout": result.stdout, "sha256": sha(ROOT / p)})
    check = subprocess.run(GIT + ["diff", "--check"], cwd=ROOT, capture_output=True, text=True)
    assert check.returncode == 0 and not check.stdout
    report = {"head": git("rev-parse", "HEAD").strip(), "branch": git("branch", "--show-current").strip(), "remote": git("remote", "-v"), "status_short": git("status", "--short"),
              "diff_check": {"exit_code": check.returncode, "stdout": check.stdout, "stderr": check.stderr}, "index_entries": git("ls-files", "--stage"), "index_sha256": entry["index_sha256"], "text_checks": [r for r in text_checks if r["path"] != relative(EVIDENCE / "git-final.json")]}
    write_json(EVIDENCE / "git-final.json", report)
    final_git_path = relative(EVIDENCE / "git-final.json")
    final_git_check = subprocess.run(GIT + ["diff", "--no-index", "--check", "--", "NUL", final_git_path], cwd=ROOT, capture_output=True, text=True)
    assert final_git_check.returncode in [0, 1] and not final_git_check.stdout
    text_checks.append({"path": final_git_path, "exit_code": final_git_check.returncode, "stdout": final_git_check.stdout, "sha256": sha(ROOT / final_git_path)})
    paths = set(git("ls-files").splitlines()) | set(git("ls-files", "--others", "--exclude-standard").splitlines()) | set(git("ls-files", "--others", "--ignored", "--exclude-standard").splitlines()) | set(candidates)
    rows = file_table(paths, candidates)
    closure = read(EVIDENCE / "integrity.json")["behavior_build_input_closure"]
    assert all(digest(ROOT / r["path"]) == r["sha256"] for r in closure["files"])
    warnings = read(EVIDENCE / "compiler-audit.json")
    result = {"phase": "00", "revision": "02", "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "workflow": "BLOCKED", "approval_eligible": False,
              "validated_head": entry["head"], "branch": entry["branch"], "remote": report["remote"], "source_baseline": entry["previous_tag"], "previous_approved_tag": entry["previous_tag"], "previous_approved_peeled_commit": entry["previous_peeled"], "remote_verification": old["remote_verification"],
              "approved_reference_identity": "NONE; acceptance blocked", "revision_entry_sha256": sha(ENTRY), "original_entry_sha256": entry["original_entry_sha256"], "revision_01_snapshot_sha256": sha(OLD), "initial_snapshot_sha256": old["initial_snapshot_sha256"],
              "review_sha256": sha(REVIEW), "files": rows, "exact_checkpoint_manifest": candidates, "protected_history_and_tracked_hashes": protected, "behavior_build_input_closure": closure, "validation_commands": captures(), "validation_summary": read(EVIDENCE / "validation-matrix.json"),
              "baseline_failures": read(EVIDENCE / "baseline-registry.json"), "runtime_plan": old["runtime_plan"], "git": report, "new_text_whitespace": text_checks, "warning_counts": {c: a["warning_counts"] for c, a in warnings["configurations"].items()},
              "mutable_control_exclusions": ["docs/build/BUILD_STATE.md", ".codex/build/phase-00-snapshot.json", "future local approval/rejection transaction receipts"],
              "context_expansion": ["Physics profiling history and test/snapshot/macro contract for 003", "Breakout caller/AssetsManager/Texture history for 004", "CDB live source-resolved crash stacks and baseline destructor/logger code for 005/006", "Editor source and independent CDB shutdown stack to classify additional blocker 007; no Editor/rendering edit", "Actual CL/link commands, SDK read closure, PE inventories and preservation checks for Phase 00"],
              "limits": "BLOCKED; focused cases, profiling benchmarks, remaining app starts/resources and disposable safeguard rerun remain NOT RUN after additional Editor scope blocker. No source change, hash refresh, or historical result is claimed as passing those gates. No checkpoint stage/commit/tag/push/approval/Phase 01."}
    write_json(ACTIVE, result)
    state = "# Build System Migration state\n\nTrack: Build System Migration\nWorktree: C:\\dev\\GEngine-build\nBranch: " + entry["branch"] + "\nSource baseline: " + entry["previous_tag"] + "\nVerified HEAD: " + entry["head"] + "\n\nCurrent approved Build phase: NONE\nCurrent approved Build tag: NONE\nCurrent approved Build commit: NONE\nActive Build phase: 00\nActive revision: 02\nNext expected Build phase: 00\nCurrent workflow state: BLOCKED\nPending approval transaction: NONE\nSnapshot: .codex/build/phase-00-snapshot.json\nSnapshot SHA-256: " + sha(ACTIVE) + "\nSnapshot eligibility: BLOCKED; not approvable\nReview: docs/build/reviews/BUILD_PHASE_00_REVIEW.md\nRevision evidence: docs/build/evidence/phase-00/revision-02/\n\nInitial and Revision 01 evidence/entries/seals are immutable and preserved. B00-RUNTIME-001 remains historically recorded and resolved by accepted unchanged Revision 01 TBB work. B00-VALIDATION-002 remains an incomplete timeout. Four authorized source corrections implemented; both full normal tests and normal benchmarks PASS; Debug Breakout startup/resources/close PASS. New B00-RUNTIME-007: Editor duplicates shared cleanup and crashes after later normal close; earlier framebuffer close-time assertion also retained. Remaining acceptance is NOT RUN/BLOCKED; no waiver.\n\nNext action: human review and a separate scope decision for Editor shutdown/early-close investigation, then resume Phase 00; do not start Phase 01. No stage/commit/tag/push/approval performed. LOCAL_GOVERNANCE, unignored and excluded from checkpoints.\n"
    (ROOT / "docs/build/BUILD_STATE.md").write_text(state, encoding="utf-8")
    print(json.dumps({"workflow": "BLOCKED", "seal_sha256": sha(ACTIVE), "review_sha256": sha(REVIEW), "checkpoint_files": len(candidates), "file_inventory": len(rows), "input_closure": len(closure["files"])}))


if __name__ == "__main__":
    {"prepare": prepare, "seal": seal}[sys.argv[1]]()
