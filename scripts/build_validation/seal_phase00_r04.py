"""Verify immutable lineage and seal the exact Revision 04 review candidate."""

import datetime
import gzip
import json
import os
import re
from pathlib import Path
import subprocess
import sys

from revise_phase00_r04 import ROOT, EVIDENCE, WORK, ENTRY, sha, write

os.environ["GIT_OPTIONAL_LOCKS"] = "0"
GIT = ["git", "-c", "safe.directory=C:/dev/GEngine-build"]
REVIEW = "docs/build/reviews/BUILD_PHASE_00_REVIEW.md"
CONTROLS = ["docs/build/BUILD_STATE.md", ".codex/build/phase-00-snapshot.json"]


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def git(*args):
    return subprocess.check_output(GIT + list(args), cwd=ROOT, text=True).strip()


def digest(path):
    return sha(path) if path.is_file() else "ABSENT"


def rel(path):
    return path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)


def verify():
    entry = read(ENTRY)
    old = read(EVIDENCE / "revision-03-snapshot.json")
    assert sha(EVIDENCE / "revision-03-snapshot.json") == entry["previous_snapshot_sha256"]
    assert sha(ROOT / ".codex/build/phase-00-revision-03-snapshot.json") == entry["previous_snapshot_sha256"]
    assert sha(EVIDENCE / "revision-03-review.md") == entry["previous_review_sha256"]
    assert git("rev-parse", "HEAD") == entry["head"]
    assert git("branch", "--show-current") == entry["branch"]
    assert git("rev-parse", entry["previous_tag"] + "^{commit}") == entry["previous_peeled"]
    assert git("ls-files", "--stage") == entry["index_entries"]
    # Git read-only inspection may refresh index stat-cache bytes. Checkpoint entries
    # must remain exact; report both file identities without rewriting the frozen entry.
    assert git("diff", "--cached", "--raw") == ""
    changes, protected = [], []
    for row in entry["entry_files"]:
        name = row["path"]
        if name in CONTROLS + [REVIEW]:
            continue
        current = digest(ROOT / name)
        if current != row["sha256"]:
            # Ignored per-app log outputs are captured by each probe. Never exempt layouts.
            assert name in [a + "/GEngine.log" for a in ["RayTracing", "Breakout", "RigidBodySimulation", "GEngineEditor"]], name
            changes.append({**row, "after": current, "reason": "Ignored runtime log output; raw launch capture retains contents"})
        else:
            protected.append(row)
    for row in old["behavior_build_input_closure"]["files"]:
        assert digest(ROOT / row["path"]) == row["sha256"], "Changed prior input: " + row["path"]
    return entry, old, protected, changes


def records():
    result = []
    for p in sorted((EVIDENCE / "commands").glob("*.json")):
        row = read(p)
        assert digest(ROOT / row["raw_log"]) == row["raw_sha256"]
        assert digest(ROOT / row["compressed_log"]) == row["compressed_sha256"]
        assert gzip.decompress((ROOT / row["compressed_log"]).read_bytes()) == (ROOT / row["raw_log"]).read_bytes()
        result.append(row)
    return result


def candidate_paths(entry, old):
    paths = set(old["exact_checkpoint_manifest"])
    paths.update(r["path"] for r in entry["new_owned_files"])
    paths.update(r["path"] for r in read(EVIDENCE / "lineage.json")["additional_exact_ownership"] if (ROOT / r["path"]).is_file())
    return sorted(paths)


def table(paths, candidates, entry, old, deferred=()):
    original = {r["path"]: r for r in old["files"]}
    at_entry = {r["path"]: r for r in entry["entry_files"]}
    rows = []
    for name in sorted(paths):
        inherited = original.get(name, {})
        classification = inherited.get("classification", "PROTECTED_UNRELATED")
        if name.startswith(".codex/") or name in ["AGENTS.md", "MANIFEST.md", "docs/build/BUILD_STATE.md"]:
            classification = "LOCAL_GOVERNANCE"
        elif name in candidates and classification != "PHASE_OWNED_TRACKED":
            classification = "PHASE_OWNED_NEW_TRACKED"
        row = {"path": name, "classification": classification,
               "entry_exists": inherited.get("entry_exists", False),
               "revision_04_entry_exists": name in at_entry,
               "revision_04_entry_sha256": at_entry.get(name, {}).get("sha256", "ABSENT"),
               "previous_blob": inherited.get("previous_blob", "ABSENT"), "checkpoint_member": name in candidates}
        if name in CONTROLS:
            row["mutable_control"] = True
        elif name in deferred:
            row["final_hashes"] = "Recorded in local seal after review finalization; avoids recursive hashes"
        else:
            current = digest(ROOT / name)
            row.update(raw_sha256=current, expected_git_blob=git("hash-object", "--path=" + name, name) if current != "ABSENT" else "ABSENT",
                       operation="delete" if current == "ABSENT" else "modify" if classification == "PHASE_OWNED_TRACKED" or name == REVIEW else "add" if not inherited.get("entry_exists", False) else "unchanged")
        rows.append(row)
    return rows


def prepare():
    entry, old, protected, changes = verify()
    captures = records()
    inputs = {r["path"].casefold(): r for r in old["behavior_build_input_closure"]["files"]}
    additions = [p for p in WORK.rglob("*") if p.is_file()]
    additions += [ROOT / "scripts/build_validation/revise_phase00_r04.py", ROOT / "scripts/build_validation/seal_phase00_r04.py", ENTRY,
                  ROOT / ".codex/build/phase-00-revision-03-snapshot.json", Path("C:/Windows/System32/nvidia-smi.exe")]
    additions += [ROOT / r["path"] for r in read(EVIDENCE / "lineage.json")["additional_exact_ownership"] if r["path"].endswith(".py")]
    additions += [Path("C:/Program Files (x86)/Windows Kits/10/Debuggers/x64") / n for n in ["dbghelp.dll", "dbgcore.dll", "cdb.exe"]]
    additions += [Path("C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um") / n for n in ["minwinbase.h", "winnt.h", "minidumpapiset.h"]]
    for path in WORK.glob("*.txt"):
        for line in path.read_text(errors="replace").splitlines():
            try:
                row = json.loads(line)
                additions += [Path(m["path"]) for key in ["module_identities", "modules"] for m in row.get(key, [])]
            except ValueError:
                pass
        additions += [Path(n.strip()) for n in re.findall(r"Image path:\s*(.+)", path.read_text(errors="replace")) if Path(n.strip()).is_file()]
    for p in additions:
        inputs[rel(p).casefold()] = {"path": rel(p), "sha256": digest(p), "reason": "Revision 04 read-only investigation helper/tool/raw evidence"}
    closure = {**old["behavior_build_input_closure"], "files": sorted(inputs.values(), key=lambda r:r["path"].casefold()),
               "revision_04_prior_input_changes": [], "revision_04_generator_relationship": "No generation, compilation, package staging or production input edits. Prior 4296-path closure verified unchanged."}
    index_now = sha(ROOT / git("rev-parse", "--git-path", "index"))
    write(EVIDENCE / "integrity.json", {"prior_candidate_and_input_hashes": "PASS", "index_entry_sha256": entry["index_sha256"], "index_final_sha256": index_now,
          "index_entries_unchanged": True, "index_file_identity_note": "Index file hash changed during investigation and again during sealing; all path/mode/stage/blob entries remain exact and cached diff is empty. Consistent with index cache refresh; exact writer and original cache bytes are unavailable. Raw identities are observations; logical checkpoint entries are the invariant. No index rewrite or staging performed by Revision 04 helpers.",
          "protected_unchanged": protected, "ignored_runtime_output_changes": changes, "behavior_build_input_closure": closure})
    candidates = candidate_paths(entry, old)
    deferred = [REVIEW, rel(EVIDENCE / "candidate-inventory.json"), rel(EVIDENCE / "git-final.json")]
    write(EVIDENCE / "candidate-inventory.json", {"exact_checkpoint_manifest": candidates, "files": table(candidates, candidates, entry, old, deferred)})
    print(json.dumps({"protected": len(protected), "candidate_files": len(candidates), "input_closure": len(inputs), "captures": len(captures)}))


def seal():
    entry, old, protected, changes = verify()
    candidates = candidate_paths(entry, old)
    assert candidates == read(EVIDENCE / "candidate-inventory.json")["exact_checkpoint_manifest"]
    assert all((ROOT / p).is_file() for p in candidates if p != rel(EVIDENCE / "git-final.json"))
    check = subprocess.run(GIT + ["diff", "--check"], cwd=ROOT, capture_output=True, text=True)
    assert check.returncode == 0 and not check.stdout
    whitespace = []
    for p in candidates:
        if p in old["exact_checkpoint_manifest"] and p != REVIEW or p == rel(EVIDENCE / "git-final.json"):
            continue
        if Path(p).suffix not in [".md", ".py", ".json", ".txt"]:
            continue
        result = subprocess.run(GIT + ["diff", "--no-index", "--check", "--", "NUL", p], cwd=ROOT, capture_output=True, text=True)
        assert result.returncode in [0, 1] and not result.stdout, (p, result.stdout)
        whitespace.append({"path": p, "exit_code": result.returncode, "stdout": result.stdout})
    status = subprocess.check_output(GIT + ["status", "--short"], cwd=ROOT, text=True)
    index_now = sha(ROOT / git("rev-parse", "--git-path", "index"))
    assert git("ls-files", "--stage") == entry["index_entries"] and git("diff", "--cached", "--raw") == ""
    report = {"status_short": status, "diff_check": {"exit_code": check.returncode, "stdout": check.stdout, "stderr": check.stderr},
              "index_entries": git("ls-files", "--stage"), "index_sha256": index_now, "index_entry_sha256": entry["index_sha256"],
              "index_prepare_sha256": read(EVIDENCE / "integrity.json")["index_final_sha256"],
              "index_comparison_policy": "Exact path/mode/stage/blob comparison; cached diff empty. Raw cache-file hashes are recorded observations, not proof of staging or unchanged cache bytes.",
              "remote": git("remote", "-v"), "untracked": git("ls-files", "--others", "--exclude-standard").splitlines(),
              "ignored": git("ls-files", "--others", "--ignored", "--exclude-standard").splitlines()}
    write(EVIDENCE / "git-final.json", report)
    result = subprocess.run(GIT + ["diff", "--no-index", "--check", "--", "NUL", rel(EVIDENCE / "git-final.json")], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode in [0, 1] and not result.stdout
    paths = set(git("ls-files").splitlines()) | set(report["untracked"]) | set(report["ignored"]) | set(candidates)
    files = table(paths, candidates, entry, old)
    closure = read(EVIDENCE / "integrity.json")["behavior_build_input_closure"]
    for row in closure["files"]:
        assert digest(ROOT / row["path"]) == row["sha256"], row["path"]
    registry = read(EVIDENCE / "baseline-registry.json")
    workflow = "AWAITING HUMAN REVIEW" if registry["final_acceptance"] == "PASS" else "BLOCKED"
    snapshot = {**old, "revision": "04", "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "workflow": workflow, "approval_eligible": workflow == "AWAITING HUMAN REVIEW", "review_sha256": sha(ROOT / REVIEW),
                "revision_entry_sha256": sha(ENTRY), "revision_03_snapshot_sha256": entry["previous_snapshot_sha256"],
                "files": files, "exact_checkpoint_manifest": candidates, "protected_history_and_tracked_hashes": protected,
                "behavior_build_input_closure": closure, "validation_commands": records(),
                "validation_summary": read(EVIDENCE / "validation-matrix.json"), "baseline_failures": registry,
                "git": report, "new_text_whitespace": whitespace, "context_expansion": read(EVIDENCE / "investigation.json")["context_expansion"],
                "limits": read(EVIDENCE / "investigation.json")["limits"], "mutable_control_exclusions": CONTROLS + ["future local transaction receipts"]}
    active = ROOT / ".codex/build/phase-00-snapshot.json"
    write(active, snapshot)
    state = f"# Build System Migration state\n\nActive Build phase: 00\nActive revision: 04\nNext expected Build phase: 00\nCurrent workflow state: {workflow}\nCurrent approved Build phase/tag/commit: NONE\nPending approval transaction: NONE\nBranch: {entry['branch']}\nSource baseline: {entry['previous_tag']}\nVerified HEAD: {entry['head']}\nSnapshot: .codex/build/phase-00-snapshot.json\nSnapshot SHA-256: {sha(active)}\nReview: {REVIEW}\nRevision evidence: docs/build/evidence/phase-00/revision-04/\n\nInitial and Revisions 01, 02 and 03 reviews, seals, registries, dumps and raw captures remain immutable. All accepted corrections and TBB safeguards are preserved. Revision 04 disposition is recorded in its review and registry.\n\nHuman review required. No checkpoint staging, commit, tag, push, approval or Phase 01. BUILD_STATE is LOCAL_GOVERNANCE, excluded from checkpoint membership.\n"
    (ROOT / "docs/build/BUILD_STATE.md").write_text(state, encoding="utf-8")
    print(json.dumps({"workflow": workflow, "seal_sha256": sha(active), "review_sha256": sha(ROOT / REVIEW), "candidates": len(candidates)}))


if __name__ == "__main__":
    {"prepare": prepare, "seal": seal}[sys.argv[1]]()
