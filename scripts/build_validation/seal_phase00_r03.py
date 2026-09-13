"""Seal the Revision 03 candidate while preserving every earlier evidence lineage."""

import datetime
import csv
import gzip
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys

os.environ["GIT_OPTIONAL_LOCKS"] = "0"
import seal_phase00_r02 as shared
from revise_phase00_r03 import ROOT, WORK, EVIDENCE, ENTRY, APPS, allowed_paths, write_json, sha
from pe_runtime import resolve_closure

REVIEW = ROOT / "docs/build/reviews/BUILD_PHASE_00_REVIEW.md"
OLD = ROOT / ".codex/build/phase-00-revision-02-snapshot.json"
ACTIVE = ROOT / ".codex/build/phase-00-snapshot.json"
NEW_MODIFIED = ["GEngineEditor/src/SceneApp.cpp", "GEngine/src/Core/BaseApp.cpp"]
ACCEPTED = ["Breakout/src/BreakoutApp.cpp", "GEngine/src/Managers/AssetsManager.cpp", "PhysicsBenchmark/src/main.cpp", "PhysicsTests/src/main.cpp"]
MODIFIED = sorted(NEW_MODIFIED + ACCEPTED)
GIT = ["git", "-c", "safe.directory=C:/dev/GEngine-build"]
for key, value in dict(EVIDENCE=EVIDENCE, ENTRY=ENTRY, WORK=WORK, OLD=OLD, ACTIVE=ACTIVE, MODIFIED=MODIFIED, allowed_paths=allowed_paths).items():
    setattr(shared, key, value)
read, git, relative, digest = shared.read, shared.git, shared.relative, shared.digest
file_table, candidate_paths, captures = shared.file_table, shared.candidate_paths, shared.captures


def verify_history():
    entry, old = read(ENTRY), read(OLD)
    assert sha(OLD) == entry["previous_snapshot_sha256"]
    assert sha(EVIDENCE / "revision-02-snapshot.json") == sha(OLD)
    assert sha(EVIDENCE / "revision-02-review.md") == entry["previous_review_sha256"]
    assert sha(ROOT / ".codex/build/phase-00-entry.json") == entry["original_entry_sha256"]
    assert git("rev-parse", "HEAD").strip() == entry["head"]
    assert git("branch", "--show-current").strip() == entry["branch"]
    assert git("rev-parse", entry["previous_tag"] + "^{}").strip() == entry["previous_peeled"]
    assert git("ls-files", "--stage").strip() == entry["index_entries"] == old["git"]["index_entries"].strip()
    assert sha(ROOT / git("rev-parse", "--git-path", "index").strip()) == entry["index_sha256"]
    protected = []
    for row in entry["protected"]:
        if row["path"] in NEW_MODIFIED + [relative(REVIEW)]:
            continue
        assert digest(ROOT / row["path"]) == row["sha256"], row["path"]
        protected.append(row)
    for row in entry["historical_generated_evidence"]:
        assert digest(ROOT / row["path"]) == row["sha256"], row["path"]
    for record in read(ROOT / "docs/build/evidence/phase-00/commands.json"):
        assert digest(ROOT / record["log"]) == record["log_sha256"]
    for revision in ["01", "02"]:
        for path in (EVIDENCE.parent / ("revision-" + revision) / "commands").glob("*.json"):
            row = read(path)
            assert digest(ROOT / row["raw_log"]) == row["raw_sha256"]
            assert digest(ROOT / row["compressed_log"]) == row["compressed_sha256"]
    assert sorted(git("diff", "--name-only").splitlines()) == MODIFIED
    before = read(WORK / "pre-camera-edit-binaries.json")
    transitions = [r["path"] for r in before if digest(ROOT / r["path"]) != r["sha256"]]
    assert sorted(transitions) == ["bin/Debug/GEngineEditor/GEngineEditor.exe", "bin/Release/GEngineEditor/GEngineEditor.exe"]
    return entry, old, protected


def closure(old):
    rows, transitions = {}, []
    for row in old["behavior_build_input_closure"]["files"]:
        name = relative(ROOT / row["path"])
        current = digest(ROOT / name)
        if current != row["sha256"]:
            generated = name.startswith(("bin-int/Debug/", "bin-int/Release/", "external/glad/bin-int/", "external/glad/bin/"))
            generated |= name.startswith("bin/") and Path(name).suffix in [".exe", ".lib", ".pdb", ".ilk"]
            assert name in NEW_MODIFIED or generated, "Unexpected input transition: " + name
            transitions.append({"path": name, "before": row["sha256"], "after": current, "reason": "Authorized Editor correctness fix" if name in NEW_MODIFIED else "Generated normal/profile build output"})
        rows[name.casefold()] = {"path": name, "sha256": current, "reason": "Inherited source/resource/dependency/tool/SDK/runtime input closure"}
    data = {"transitions": transitions, "toolchain_policy": "C++20; Debug /MTd; Release /MT; GLAD C /TC; MSVC14.44.35207 HostX86/x64; Windows SDK10.0.26100.0; fixed Premake graph/dependencies"}
    additions = [ROOT / "scripts/build_validation" / n for n in ["revise_phase00_r03.py", "audit_phase00_r03.py", "seal_phase00_r03.py"]]
    additions += list((ROOT / ".codex/build").glob("phase-00-revision-03-*.json"))
    additions += [OLD] + [p for p in WORK.rglob("*") if p.is_file() and p.name != "compiler-audit.json"]
    for base in [ROOT / "bin-int/Debug", ROOT / "bin-int/Release", ROOT / "external/glad/bin-int"]:
        for tlog in base.rglob("*.tlog"):
            additions.append(tlog)
            if ".read." in tlog.name:
                for line in tlog.read_text(encoding="utf-16", errors="replace").splitlines():
                    additions += [Path(n) for n in line.lstrip("^").split("|") if re.match(r"^[A-Z]:\\", n, re.I)]
    for path in (EVIDENCE / "commands").glob("startup-*.txt.gz"):
        for line in gzip.decompress(path.read_bytes()).decode(errors="replace").splitlines():
            try:
                obj = json.loads(line)
            except ValueError:
                continue
            additions += [Path(m["path"]) for m in obj.get("modules", [])]
    for path in additions:
        rows[relative(path).casefold()] = {"path": relative(path), "sha256": digest(path), "reason": "Revision 03 execution/validation input and frozen authorization receipt"}
    data.update(files=sorted(rows.values(), key=lambda r: r["path"].casefold()),
                generator_relationship="Unchanged Premake option generates profile mode, then restores normal mode. Actual profile GEngine/PhysicsBenchmark commands are audited. Full normal eight-target Rebuild captures 133 target/TU pairs per config. Final SceneApp-only correction is rebuilt in both configs; all other executable/archive hashes remain identical to pre-camera-edit capture. Their fresh tests/benchmarks and startup captures remain applicable.",
                limits="Conservative actual input closure; acceptance status is separately recorded. OS/driver modules and protected source/resources/runtime are hashed. Generated diagnostic logs/dumps/layouts are evidence, excluded from checkpoint unless explicitly versionable evidence. No CMake/Conan or dependency/toolchain/CRT/graph changes.")
    return data


def prepare():
    entry, old, protected = verify_history()
    assert read(ACTIVE)["workflow"] == "IN_PROGRESS"
    records = captures()
    for r in records:
        assert digest(r["retained_harness"]) == r["harness_sha256"]
    runtime = [resolve_closure(ROOT / "bin" / c / a / (a + ".exe")) for c in ["Debug", "Release"] for a in APPS]
    assert all(not a["missing"] and all(n["machine"] == "0x8664" for n in a["nodes"]) for a in runtime)
    write_json(EVIDENCE / "runtime-audit.json", {"result": "PASS", "audits": runtime, "TBB": "Accepted Revision 01 source/import/runtime identity and staging helper preserved. Fresh check and disposable fixture validation passed; no dependency work repeated or changed."})
    missing = sorted(Path(p).stem for p in allowed_paths() if "/commands/" in p and p.endswith(".json") and not (ROOT / p).exists())
    matrix = {"executed": records, "reserved_but_not_run": missing,
              "counts_note": "Diagnostic capture success is not application acceptance. Initial sandbox launch denial, heap-state minidump limitation, first corrected Editor AV and debugger exit/parser attempts are retained with original metadata. Earlier Release profile sampling overlapped final link; separately named verified repeats are authoritative. No failed capture is overwritten or silently relabeled."}
    counter_checks = []
    for config in ["Debug", "Release"]:
        for mode in ["normal", "profile"]:
            name = f"benchmark-{config}-{mode}" + ("-verified" if config == "Release" and mode == "profile" else "")
            text = (WORK / (name + ".txt")).read_text()
            values = list(csv.DictReader(io.StringIO("\n".join(l for l in text.splitlines() if not l.startswith("#")))))
            assert [int(r["body_count"]) for r in values] == [50, 100, 200, 500, 1000, 2000]
            assert "# physics_profiling=" + ("enabled" if mode == "profile" else "disabled") in text
            for row in values:
                numbers = [float(row[k]) for k in ["contacts", "solver_constraints", "physics_world_ms"]]
                assert all(n > 0 if mode == "profile" else n == 0 for n in numbers)
            counter_checks.append({"capture": name, "result": "PASS", "rows": values})
    matrix["profiling_counter_validation"] = counter_checks
    by_name = {r["name"]: r for r in records}
    required = ["premake-profile", "premake-normal", "runtime-check", "safeguards"]
    for config in ["Debug", "Release"]:
        required += [f"build-{mode}-{config}" for mode in ["normal", "profile"]]
        required += [f"build-editor-{config}-final", f"imports-{config}-GEngineEditor-final"]
        required += [f"tests-{config}-{case}" for case in ["full", "solver-storage", "runtime-transform", "scene-runtime-lifecycle", "fixed-scheduling", "absolute-scaling", "box-manifolds"]]
        required += [f"benchmark-{config}-{mode}" + ("-verified" if config == "Release" and mode.startswith("profile") else "") for mode in ["normal", "profile", "normal-steady", "profile-steady"]]
        required += [f"tests-profile-{config}-box-manifolds" + ("-verified" if config == "Release" else "")]
        required += [f"editor-fixed-{timing}-{config}" + ("-final" if config == "Debug" and timing == "early" else "") for timing in ["early", "late"]]
        for app in APPS:
            required += [f"startup-{config}-{app}", f"resources-{config}-{app}" + ("-final" if config == "Debug" and app == "Breakout" else "")]
            if app != "GEngineEditor":
                required += [f"imports-{config}-{app}"]
    assert all(n in by_name for n in required), [n for n in required if n not in by_name]
    failed = [n for n in required if by_name[n]["result"] != "PASS"]
    assert failed == ["startup-Release-RayTracing"], failed
    matrix["required_commands"] = [{"name": n, "result": by_name[n]["result"]} for n in required]
    matrix["failed_acceptance_commands"] = failed
    matrix["final_acceptance"] = "BLOCKED"
    write_json(EVIDENCE / "validation-matrix.json", matrix)
    write_json(EVIDENCE / "baseline-registry.json", {
        "initial": {"B00-RUNTIME-001": "Historical missing TBB; resolved by accepted unchanged Revision 01", "B00-VALIDATION-002": "Original 120-second Debug timeout remains INCOMPLETE evidence, never a Physics failure"},
        "revision_01_blockers_resolved_by_accepted_revision_02": {"B00-VALIDATION-003": "Conditional telemetry test contract preserved; fresh normal tests and enabled-counter checks PASS", "B00-RUNTIME-004": "Explicit Breakout relative filename correction preserved; both configurations resource opens and startup PASS", "B00-RUNTIME-005": "Base-owned Breakout shared cleanup preserved; both configurations normal close PASS", "B00-VALIDATION-006": "Ordinary benchmark logger initialization preserved; normal/profile benchmarks PASS"},
        "revision_03_corrections": {"B00-RUNTIME-007": "Independent baseline late-close stack demonstrates repeated shared manager cleanup; remove only three redundant derived calls, preserve BaseApp cleanup; final early/late Debug/Release close PASS", "B00-RUNTIME-008": "Independent RenderTarget.cpp:266 assertion has live m_Running=false and zero windows after input destroys GL context. Break frame after shutdown input; final early/late Debug/Release close PASS", "B00-RUNTIME-009": "Same SceneApp destructor manually deletes scene-owned camera. Live pre-delete camera pointer exactly matches later failing unique_ptr delete. Remove redundant manual deletion; existing scene/rig owner preserved, final close PASS"},
        "new_blocker": {"id": "B00-RUNTIME-010", "target": "RayTracing Release", "result": "BLOCKED", "signature": "Fresh ordinary source-CWD startup exits 3221226505 / 0xc0000409 in 5.704 seconds before any close request; no diagnostic dialog and no forced termination", "evidence": "commands/startup-Release-RayTracing.json", "repeats": "Three independent ordinary 60-second repeats pass; resource-traced run and normal-heap debugger run exit 0. These do not explain or erase the original fast-fail.", "root_cause": "UNRESOLVED; no failing stack obtained in debugger repeats. No evidence proves an Editor correction or accepted TBB distribution is incorrect. No RayTracing, driver, dependency, rendering or threading behavior was changed.", "diagnostic_limits": "The first diagnostic stopped at process termination and its harness timed out; no failure stack or dump was captured. The normal-heap diagnostic exits 0 but capture metadata FAIL means the sought crash dump was absent. Original raw outcomes remain unchanged.", "smallest_required_new_contract": "Scoped Release RayTracing startup fast-fail investigation: reproduce with a failure dump/stack under the documented source CWD and protected inputs, distinguish application/runtime/driver or probe-concurrency cause, authorize only a separately reviewed minimum correction if required. Preserve all accepted revisions, dependencies/toolchain/CRT/graph and resources. No broad architecture or migration work."},
        "final_acceptance": "BLOCKED", "approval_exception": "NONE; no failed or missing gate waived"})
    write_json(EVIDENCE / "lineage.json", {"revision": "03", "entry_sha256": sha(ENTRY), "previous_seal_sha256": sha(OLD), "previous_review_sha256": entry["previous_review_sha256"], "initial_seal_sha256": old["initial_snapshot_sha256"], "revision_01_seal_sha256": old["revision_01_snapshot_sha256"], "authorization": entry["authorization"], "accepted_revision_02_sources_unchanged": ACCEPTED, "new_source_scope": NEW_MODIFIED, "ownership_receipts": [{"path": relative(p), "sha256": sha(p)} for p in sorted((ROOT / ".codex/build").glob("phase-00-revision-03-ownership-*.json"))]})
    inputs = closure(old)
    write_json(EVIDENCE / "integrity.json", {"result": "PASS", "protected": protected, "protected_count": len(protected), "historical_generated_evidence": entry["historical_generated_evidence"], "tracked_runtime_count": sum(r["path"].startswith("bin/") and r.get("previous_blob", "ABSENT") != "ABSENT" for r in protected), "index_sha256": entry["index_sha256"], "index_entries_identical_to_revision_02": True, "source_changes": MODIFIED, "revision_03_source_changes": NEW_MODIFIED, "all_prior_raw_captures": "Verified against immutable recorded hashes", "behavior_build_input_closure": inputs})
    write_json(EVIDENCE / "git-final.json", {"status": "Pending finalized review; completed before local seal"})
    deferred = [relative(REVIEW), relative(EVIDENCE / "candidate-inventory.json"), relative(EVIDENCE / "git-final.json")]
    candidates = sorted(set(candidate_paths(old)) | set(deferred))
    write_json(EVIDENCE / "candidate-inventory.json", {"exact_checkpoint_manifest": candidates, "files": file_table(set(candidates) - set(deferred), candidates), "deferred_final_hashes": {p: "Exact finalized raw SHA-256 and expected Git blob in local seal; deferred to avoid recursive hashing" for p in deferred}})
    print(json.dumps({"candidates": len(candidates), "protected": len(protected), "inputs": len(inputs["files"]), "captures": len(records)}))


def seal():
    entry, old, protected = verify_history()
    assert read(ACTIVE)["workflow"] == "IN_PROGRESS"
    candidates = read(EVIDENCE / "candidate-inventory.json")["exact_checkpoint_manifest"]
    assert set(candidates) == set(candidate_paths(old))
    text_checks = []
    for p in candidates:
        if p == relative(EVIDENCE / "git-final.json") or (p in old["exact_checkpoint_manifest"] and p != relative(REVIEW)):
            continue
        if Path(p).suffix not in [".md", ".json", ".py", ".txt"] or p in MODIFIED:
            continue
        result = subprocess.run(GIT + ["diff", "--no-index", "--check", "--", "NUL", p], cwd=ROOT, capture_output=True, text=True)
        assert result.returncode in [0, 1] and not result.stdout, (p, result.stdout)
        text_checks.append({"path": p, "exit_code": result.returncode, "stdout": result.stdout, "sha256": sha(ROOT / p)})
    check = subprocess.run(GIT + ["diff", "--check"], cwd=ROOT, capture_output=True, text=True)
    assert check.returncode == 0 and not check.stdout
    report = {"head": git("rev-parse", "HEAD").strip(), "branch": git("branch", "--show-current").strip(), "remote": git("remote", "-v"), "status_short": git("status", "--short"), "diff_check": {"exit_code": check.returncode, "stdout": check.stdout, "stderr": check.stderr}, "index_entries": git("ls-files", "--stage"), "index_sha256": entry["index_sha256"], "text_checks": text_checks}
    write_json(EVIDENCE / "git-final.json", report)
    p = relative(EVIDENCE / "git-final.json")
    check = subprocess.run(GIT + ["diff", "--no-index", "--check", "--", "NUL", p], cwd=ROOT, capture_output=True, text=True)
    assert check.returncode in [0, 1] and not check.stdout
    text_checks.append({"path": p, "exit_code": check.returncode, "stdout": check.stdout, "sha256": sha(ROOT / p)})
    paths = set(git("ls-files").splitlines()) | set(git("ls-files", "--others", "--exclude-standard").splitlines()) | set(git("ls-files", "--others", "--ignored", "--exclude-standard").splitlines()) | set(candidates)
    rows = file_table(paths, candidates)
    inputs = read(EVIDENCE / "integrity.json")["behavior_build_input_closure"]
    for row in inputs["files"]:
        assert digest(ROOT / row["path"]) == row["sha256"], row["path"]
    registry = read(EVIDENCE / "baseline-registry.json")
    workflow = "AWAITING HUMAN REVIEW" if registry["final_acceptance"] == "PASS" else "BLOCKED"
    result = {"phase": "00", "revision": "03", "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "workflow": workflow, "approval_eligible": workflow == "AWAITING HUMAN REVIEW", "validated_head": entry["head"], "branch": entry["branch"], "remote": report["remote"], "source_baseline": entry["previous_tag"], "previous_approved_tag": entry["previous_tag"], "previous_approved_peeled_commit": entry["previous_peeled"], "remote_verification": old["remote_verification"], "approved_reference_identity": "NONE; human review pending", "revision_entry_sha256": sha(ENTRY), "original_entry_sha256": entry["original_entry_sha256"], "revision_02_snapshot_sha256": sha(OLD), "revision_01_snapshot_sha256": old["revision_01_snapshot_sha256"], "initial_snapshot_sha256": old["initial_snapshot_sha256"], "review_sha256": sha(REVIEW), "files": rows, "exact_checkpoint_manifest": candidates, "protected_history_and_tracked_hashes": protected, "historical_generated_evidence": entry["historical_generated_evidence"], "behavior_build_input_closure": inputs, "validation_commands": captures(), "validation_summary": read(EVIDENCE / "validation-matrix.json"), "baseline_failures": registry, "runtime_plan": old["runtime_plan"], "git": report, "new_text_whitespace": text_checks, "warning_counts": {mode: {c: a["warning_counts"] for c, a in data["configurations"].items()} for mode, data in read(EVIDENCE / "compiler-audit.json").items()}, "mutable_control_exclusions": ["docs/build/BUILD_STATE.md", ".codex/build/phase-00-snapshot.json", "future local approval/rejection transaction receipts"], "context_expansion": ["SceneApp/BaseApp destruction and manager cleanup for 007", "Event/window/SDL context destruction and RenderTarget assertion for 008", "Same derived destructor camera pointer and existing Actor/CameraRig/Scene unique_ptr owner for 009; no ownership design changes", "Fresh compiler/link/read, profiling/normal test and benchmark, app runtime/resource evidence and protected-input checks", "Independent Release RayTracing fast-fail diagnostics only; no unrelated source/dependency repair"], "limits": "No failing or missing acceptance gate waived. Initial and all prior revision evidence is immutable. No checkpoint stage, commit, tag, push, approval or Phase 01."}
    write_json(ACTIVE, result)
    state = "# Build System Migration state\n\nActive Build phase: 00\nActive revision: 03\nNext expected Build phase: 00\nCurrent workflow state: " + workflow + "\nCurrent approved Build phase/tag/commit: NONE\nPending approval transaction: NONE\nBranch: " + entry["branch"] + "\nSource baseline: " + entry["previous_tag"] + "\nVerified HEAD: " + entry["head"] + "\nSnapshot: .codex/build/phase-00-snapshot.json\nSnapshot SHA-256: " + sha(ACTIVE) + "\nReview: docs/build/reviews/BUILD_PHASE_00_REVIEW.md\nRevision evidence: docs/build/evidence/phase-00/revision-03/\n\nInitial and Revisions 01/02 reviews, seals, registries and raw evidence are preserved. B00-RUNTIME-001 remains historically recorded and resolved by accepted Revision 01. B00-VALIDATION-002 remains incomplete timeout evidence, not a Physics failure. Accepted Revision 02 corrections unchanged. Revision 03 scope/results and remaining blockers are in the review and registry.\n\nHuman review required. No staging, commit, tag, push, approval or Phase 01. This file is LOCAL_GOVERNANCE and excluded from checkpoint membership.\n"
    (ROOT / "docs/build/BUILD_STATE.md").write_text(state, encoding="utf-8")
    print(json.dumps({"workflow": workflow, "seal_sha256": sha(ACTIVE), "review_sha256": sha(REVIEW), "candidates": len(candidates), "inventory": len(rows), "input_closure": len(inputs["files"])}))


if __name__ == "__main__":
    {"prepare": prepare, "seal": seal}[sys.argv[1]]()
