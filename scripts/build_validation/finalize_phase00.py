"""Audit captured Phase 00 logs and seal a BLOCKED, non-approvable receipt."""

import collections
import datetime
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

from capture_phase00 import EVIDENCE, ENTRY, GIT, ROOT, VS, git, sha, write_json

REVIEW = ROOT / "docs/build/reviews/BUILD_PHASE_00_REVIEW.md"
NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


def relative(path):
    try:
        return Path(path).relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def audit_compilers(entry):
    projects = {}
    for row in entry["files"]:
        path = ROOT / row["path"]
        if path.suffix == ".vcxproj":
            tree = ET.parse(path)
            projects[path.stem] = {"path": row["path"], "sources": [
                relative((path.parent / node.attrib["Include"]).resolve())
                for node in tree.findall(".//m:ClCompile", NS) if "Include" in node.attrib]}
    results = {}
    for config in ["Debug", "Release"]:
        log = EVIDENCE / ("build-" + config + ".txt")
        contents = log.read_text(encoding="utf-8", errors="replace")
        lines = contents.splitlines()
        calls = []
        for number, line in enumerate(lines, 1):
            if not re.match(r"^\s*C:.*\\CL\.exe /c ", line, re.I):
                continue
            command = re.sub(r" \(TaskId:\d+\)$", "", line.strip())
            target = re.search(r"bin-int[\\/]" + config + r"[\\/]([^\\/\"]+)", command).group(1)
            project = ROOT / projects[target]["path"]
            sources = re.findall(r'(?<!\S)([^\s\"]+\.(?:cpp|c))(?=\s|$)', command, re.I)
            crt = re.findall(r"(?<!\S)/(MTd|MT|MDd|MD)(?=\s|$)", command)
            languages = re.findall(r"(?<!\S)/std:([^\s]+)", command)
            compiler = command.split(" /c ", 1)[0]
            calls.append({"log_line": number, "target": target, "command": command,
                          "compiler": compiler, "compiler_sha256": sha(compiler),
                          "sources": [relative((project.parent / p).resolve()) for p in sources],
                          "crt_options": crt, "language_options": languages,
                          "crt_pass": crt == (["MTd"] if config == "Debug" else ["MT"]),
                          "language_pass": languages == (["c++20"] if target != "glad" else [])
                          and (target != "glad" or " /TC " in command)})
        coverage = []
        for target, data in projects.items():
            expected = set(data["sources"])
            observed = {s for call in calls if call["target"] == target for s in call["sources"]}
            coverage.append({"target": target, "expected_count": len(expected), "observed_count": len(observed),
                             "missing": sorted(expected - observed), "unexpected": sorted(observed - expected)})
        before_summary = contents.split("Build succeeded.", 1)[0].split("Build FAILED.", 1)[0].splitlines()
        starts = [(i, match) for i, line in enumerate(before_summary)
                  if (match := re.search(r": warning ([A-Z]+\d+): (\S.*)", line))]
        warnings = []
        for j, (i, match) in enumerate(starts):
            end = starts[j + 1][0] if j + 1 < len(starts) else len(before_summary)
            block = before_summary[i:min(end, i + 150)]
            # Stop at the end of the emitted diagnostic, before unrelated MSBuild tasks.
            for k, line in enumerate(block[1:], 1):
                if re.match(r"^\s*\(TaskId:\d+\)\s*$", line):
                    block = block[:k + 1]
                    break
            context = "\n".join(block)
            locations = re.findall(r"C:\\dev\\GEngine-build\\([^\n]+?)\(\d+(?:,\d+)?\):", context, re.I)
            first_party = sorted(set(p for p in locations if not p.lower().startswith(
                ("external\\", "gengine\\include\\external\\"))))
            code = match.group(1)
            category = "linker" if code.startswith("LNK") else "first_party" if first_party else "vendor_external"
            warnings.append({"log_line": i + 1, "code": code, "category": category,
                             "first_party_instantiation_locations": first_party, "diagnostic_context": block})
        counts = collections.Counter(w["category"] for w in warnings)
        reported = [int(n) for n in re.findall(r"^\s*(\d+) Warning\(s\)\s*$", contents, re.M)]
        assert reported == [len(warnings)], (config, reported, len(warnings))
        link_commands = [{"log_line": i + 1, "command": line.strip()} for i, line in enumerate(lines)
                         if re.match(r"^\s*C:.*\\(?:link|lib)\.exe ", line, re.I)]
        results[config] = {"log": relative(log), "log_sha256": sha(log), "compiler_calls": calls,
                           "translation_units": sum(len(c["sources"]) for c in calls),
                           "coverage": coverage, "link_and_archive_commands": link_commands,
                           "compiler_version": sorted(set(re.findall(r"Compiler Version ([\d.]+)", contents))),
                           "evaluated_vctools_versions": sorted(set(re.findall(r"^\s*VCToolsVersion = (.+)$", contents, re.M))),
                           "evaluated_sdk_versions": sorted(set(re.findall(r"^\s*WindowsSDKVersion = (.+)$", contents, re.M))),
                           "resolved_sdk_evidence": "10.0.26100.0 headers/import libraries in CL/link read tlogs and evaluated diagnostic log",
                           "warning_counts": {k: counts[k] for k in ["first_party", "linker", "vendor_external"]},
                           "warnings": warnings, "msbuild_reported_warning_count": reported[0],
                           "flag_gate": "PASS" if all(c["crt_pass"] and c["language_pass"] for c in calls) else "FAIL",
                           "coverage_gate": "PASS" if all(not c["missing"] and not c["unexpected"] for c in coverage) else "FAIL"}
    result = {"projects": projects, "configurations": results,
              "warning_method": "Count primary diagnostics before MSBuild summary, excluding indented template substitutions; reconcile exact MSBuild count. Preserve reported template context. Any repository first-party location makes a warning first-party even when primary location is external. Vendor-only diagnostic blocks do not establish first-party warning ownership merely because the compiling TU is first-party.",
              "warning_signature": "All Debug warnings are C4996 STL4043 checked_array_iterator deprecations in spdlog/fmt and MSVC STL; all reported instantiation locations are vendor/STL. No suppression added."}
    write_json(EVIDENCE / "compiler-audit.json", result)
    return result


def capture_closure(entry):
    paths = {}
    def add(path, reason):
        path = Path(path).resolve()
        key = str(path).casefold()
        paths.setdefault(key, {"path": path, "reasons": set()})["reasons"].add(reason)
    for row in entry["files"]:
        if row["previous_blob"] != "ABSENT":
            add(ROOT / row["path"], "conservative tracked source/resource/library closure and protected bytes")
        elif row["entry_exists"] and row["classification"] != "PHASE_OWNED_NEW_TRACKED" and row["path"] != "docs/build/BUILD_STATE.md":
            add(ROOT / row["path"], "entry ignored input/governance or protected helper")
    for base in [ROOT / "bin-int", ROOT / "external/glad/bin-int"]:
        for tlog in base.rglob("*.tlog"):
            add(tlog, "MSBuild generated compiler/linker tracking evidence")
            if ".read." in tlog.name:
                for line in tlog.read_text(encoding="utf-16", errors="replace").splitlines():
                    for name in line.lstrip("^").split("|"):
                        if re.match(r"^[A-Z]:\\", name, re.I):
                            add(name, "actual compiler/linker read tlog: " + relative(tlog))
    # Capture selected toolchain implementation and build rules, not just XML declarations.
    for base in [VS / "VC/Tools/MSVC/14.44.35207/bin/Hostx86/x64",
                 VS / "MSBuild/Current/Bin", VS / "MSBuild/Microsoft/VC/v170"]:
        for path in base.rglob("*"):
            if path.is_file():
                add(path, "selected VS2022 compiler/MSBuild implementation and imported build rules")
    environment = json.loads((EVIDENCE / "environment.json").read_text())
    for tool in environment["tools"]:
        add(tool["path"], "environment tool identity")
    for name in ["capture_phase00.py", "finalize_phase00.py"]:
        add(ROOT / "scripts/build_validation" / name, "phase evidence orchestration; no production graph changes")
    for path in (ROOT / "bin").rglob("*.exe"):
        add(path, "fresh build output; runtime/test executable identity")
    missing = [ROOT / "bin/Debug/RayTracing/tbb12.dll", ROOT / "RayTracing/tbb12.dll",
               Path("C:/Windows/System32/tbb12.dll"), Path("C:/Windows/System/tbb12.dll"),
               Path("C:/Windows/tbb12.dll"), ROOT / "external/tbb/bin/tbb12.dll"]
    for path in missing:
        add(path, "missing imported runtime candidate; launch uses only Windows system PATH")
    rows = [{"path": relative(item["path"]), "sha256": sha(item["path"]) if item["path"].is_file() else "ABSENT",
             "reasons": sorted(item["reasons"])} for _, item in sorted(paths.items())]
    closure = {"timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "files": rows,
               "generator_relationship": "Unmodified premake5.lua + external/glad/premake5.lua + bundled Premake vs2022 -> eight ignored .vcxproj files and GEngine.sln -> explicit VS2022 MSBuild -> captured CL/link/lib commands and read tlogs -> fresh Debug/Release binaries.",
               "package_profile_lock_toolchain": "Premake reference only; no Conan profiles/locks/packages or CMake toolchains generated. Vendored headers/import libraries and FMOD/SDL/Assimp runtime bytes are covered as tracked files. SDK/CRT consumed headers and libraries are covered by read tlogs.",
               "limits": "BLOCKED capture. Runtime closure is incomplete: required TBB runtime absent, no successful module-load/resource evidence. Release tests, focused cases, profiling and other app launches are not validated. This closure cannot serve as a passing reference or approval authorization."}
    write_json(EVIDENCE / "input-closure.json", closure)
    return closure


def file_record(path, old):
    absolute = ROOT / path
    exists = absolute.is_file()
    raw = sha(absolute) if exists else "ABSENT"
    blob = git("hash-object", "--path=" + path, path).strip() if exists else "ABSENT"
    return {"path": path, "classification": old["classification"],
            "entry_exists": old["entry_exists"], "entry_sha256": old["entry_sha256"],
            "operation": "unchanged" if raw == old["entry_sha256"] else "add" if not old["entry_exists"] else "modify" if exists else "delete",
            "raw_sha256": raw, "expected_git_blob": blob, "previous_blob": old["previous_blob"],
            "checkpoint_member": exists and old["classification"].startswith("PHASE_OWNED_")}


def prepare():
    assert not (ROOT / ".codex/build/phase-00-snapshot.json").exists(), "Sealed candidate requires explicit revision"
    entry = json.loads(ENTRY.read_text())
    audit = audit_compilers(entry)
    print("Compiler audit:", {c: (v["translation_units"], v["flag_gate"], v["coverage_gate"], v["warning_counts"])
                              for c, v in audit["configurations"].items()}, flush=True)
    closure = capture_closure(entry)
    failures = {
        "approved_reference": "NONE; this is fresh source-tag evidence, not an accepted baseline exception",
        "failures": [{"id": "B00-RUNTIME-001", "gate": "required runtime DLL / application startup", "status": "BLOCKED",
                      "signature": "Debug RayTracing imports tbb12.dll; DLL absent in captured loader search locations; fresh launch exits 3221225781 / 0xc0000135 within 0.015 seconds before application startup.",
                      "evidence": ["imports-Debug-RayTracing.txt", "startup-Debug-RayTracing.txt", "input-closure.json"],
                      "attribution_limit": "The observed loader exit reports a missing module, not its filename. The separately established missing TBB import is a concrete unresolved dependency; other transitive dependencies are not ruled out.",
                      "source_unchanged": True, "approved_exception": False},
                     {"id": "B00-VALIDATION-002", "gate": "full Debug PhysicsTests", "status": "INCOMPLETE",
                      "signature": "Capture terminated this process after its 120-second limit; exit 1 is harness termination, not a reported assertion failure. Output has no final check summary.",
                      "evidence": ["tests-Debug-full.txt", "commands.json"],
                      "attribution_limit": "A slow run, hang and interactive CRT diagnostic are not distinguished. No Physics defect is classified from this timeout.",
                      "approved_exception": False}],
        "proposal": {"title": "Reference runtime preparation for the existing Premake graph",
                     "dependency": "A verified x64 runtime matching the existing external/tbb import libraries, plus an audit of transitive imports of all freshly linked applications.",
                     "scope": "Authorize narrowly scoped runtime provenance/staging/safeguard work for the current Premake reference. Supply the matching TBB DLL from an explicit verified origin; permit only runtime closure repairs demonstrated by fresh import evidence. Keep engine/application/Physics behavior, compiler flags and dependency versions fixed.",
                     "validation": "Verify DLL architecture/hash/import-library compatibility; test safeguards in a disposable copied output; launch four apps in Debug/Release from source CWD with documented DLL search paths; resume the incomplete Phase 00 test and benchmark matrix with an adequate declared test time limit.",
                     "boundary": "Requires a separate human roadmap decision and explicit preparation contract; not executed here. No successor started."}}
    write_json(EVIDENCE / "baseline-failures.json", failures)
    validations = [
        {"check": "entry branch/HEAD/published source tag/index", "result": "PASS"},
        {"check": "normal Premake vs2022 regeneration", "result": "PASS"},
        *[{"check": c + " all eight targets; 133 TUs; actual C++20/CRT/coverage/warning audit", "result": "PASS"}
          for c in ["Debug", "Release"]],
        {"check": "Debug full PhysicsTests", "result": "INCOMPLETE: 120-second capture limit; no completion summary"},
        {"check": "Release full PhysicsTests; focused scene/CRT cases both configs", "result": "NOT RUN after runtime stop gate"},
        {"check": "fixed-input benchmark normal/profiling both configs", "result": "NOT RUN after runtime stop gate"},
        {"check": "restore normal generation", "result": "NOT APPLICABLE: profiling generation never performed; normal generation retained"},
        {"check": "four Debug app import inventories", "result": "PASS: dumpbin capture, not successful runtime closure"},
        {"check": "Debug RayTracing source-CWD startup", "result": "FAIL: missing-module loader exit 0xc0000135"},
        {"check": "other three Debug app launches; four Release imports/launches; resource checks", "result": "NOT RUN after runtime stop gate"},
        {"check": "runtime safeguard in disposable copied output", "result": "NOT RUN after runtime stop gate"}]
    write_json(EVIDENCE / "validation-summary.json", {"workflow": "BLOCKED", "approval_eligible": False, "checks": validations})
    old = {r["path"]: r for r in entry["files"]}
    # Do not recursively hash inventory, review, or Git-final evidence inside themselves.
    deferred = {relative(REVIEW), relative(EVIDENCE / "final-inventory.json"), relative(EVIDENCE / "git-final.json")}
    rows = [file_record(p, old[p]) for p in sorted(entry["exact_owned_paths"]) if p not in deferred]
    protected = []
    for row in entry["files"]:
        if row["classification"] != "PROTECTED_UNRELATED":
            continue
        path = ROOT / row["path"]
        current = sha(path) if path.is_file() else "ABSENT"
        protected.append({"path": row["path"], "entry_sha256": row["entry_sha256"], "final_sha256": current,
                          "unchanged": current == row["entry_sha256"]})
    assert all(r["unchanged"] for r in protected), "Protected work changed; stop before sealing"
    index = git("ls-files", "--stage")
    index_hash = sha(ROOT / git("rev-parse", "--git-path", "index").strip())
    assert index == entry["index_entries"] and index_hash == entry["index_sha256"], "Index changed"
    write_json(EVIDENCE / "final-inventory.json", {"files": rows, "protected_unrelated": protected,
               "deferred_final_hashes": sorted(deferred), "deferred_reason": "Final local seal stores these exact bytes after finalization to avoid recursive self-hashing.",
               "entry_sha256": sha(ENTRY), "closure_sha256": sha(EVIDENCE / "input-closure.json")})
    for path in [REVIEW, EVIDENCE / "git-final.json"]:
        path.write_text("", encoding="utf-8")
    status = git("status", "--short")
    expanded = git("status", "--short", "--untracked-files=all")
    ignored = git("ls-files", "--others", "--ignored", "--exclude-standard").splitlines()
    untracked = git("ls-files", "--others", "--exclude-standard").splitlines()
    diff = subprocess.run(GIT + ["diff", "--check"], cwd=ROOT, capture_output=True, text=True)
    assert diff.returncode == 0
    git_final = {"timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "status_short": status,
                 "expanded_untracked_status": expanded, "ignored_paths": ignored, "untracked_paths": untracked,
                 "index_entries": index, "index_sha256": index_hash, "index_preserved": True,
                 "diff_check": {"command": subprocess.list2cmdline(GIT + ["diff", "--check"]), "exit_code": diff.returncode,
                                "stdout": diff.stdout, "stderr": diff.stderr},
                 "new_text_whitespace": "Verified on finalized candidate with git diff --no-index --check -- NUL <exact-file>; exact results stored in local seal to avoid self-hash recursion.",
                 "output_policy": "New ignored binaries/objects/PCH/tlogs are generated reference outputs, excluded. No disposable safeguard copy created. No tracked DLL modified."}
    write_json(EVIDENCE / "git-final.json", git_final)
    commands = json.loads((EVIDENCE / "commands.json").read_text())
    for command in commands:
        assert sha(ROOT / command["log"]) == command["log_sha256"]
    present = [p for p in entry["exact_owned_paths"] if (ROOT / p).is_file()]
    def evidence_link(name):
        return "[" + name + "](../evidence/phase-00/" + name + ")"
    lines = ["# BUILD PHASE 00 review", "", "Workflow: **BLOCKED**", "",
             "Phase title / contract: [Capture the current Premake reference](../phases/BUILD_PHASE_00.md).",
             "Human instruction: `START BUILD PHASE 00`. Previous checkpoint: `render-refactor-phase-00-approved`, peeled to `" + entry["head"] + "`.", "",
             "## Scope and behavior", "",
             "Both normal configurations built all eight targets at the unchanged source tag. The fresh Debug RayTracing executable imports `tbb12.dll`, absent from the recorded loader search locations, and fails before startup with `0xc0000135`. The runtime stop condition prevents a complete or approvable Phase 00 reference.", "",
             "Adopted 55 existing versionable bootstrap files without editing their bytes. Added capture/audit helpers and the evidence enumerated below. The bootstrap phase contracts remain historical NOT STARTED documents; active workflow truth is the local state and this review. No engine, application, dependency, Premake or runtime source changed; no CMake/Conan migration, staging, commit, tag or push occurred.", "",
             "Smallest proposed preparation: **reference runtime closure for the current Premake graph**. Establish an explicit verified origin for the x64 TBB runtime matching the existing import libraries, then authorize narrowly scoped staging/safeguard changes. Audit other freshly linked imports as part of that boundary; do not assume TBB is the only possible missing transitive DLL. Validate copied-output safeguards and all app/config source-CWD launches. This requires a separate human roadmap decision and explicit contract; no repair or successor has been started.", "",
             "## Target and dependency impact", "",
             "The graph remains GEngine, glad, PhysicsTests, PhysicsBenchmark, Breakout, GEngineEditor, RayTracing and RigidBodySimulation. No PUBLIC/PRIVATE/INTERFACE, TU ownership, package, SDK selection or dependency changes. Normal ignored VS projects were regenerated; all entry project bytes were preserved by deterministic regeneration. All 1,568 tracked files, including 85 DLLs and source resources, are protected and unchanged.", "",
             "## Validation and Premake reference comparison", "",
             "Explicit VS2022 MSBuild 17.14 / v143 14.44.35207 selected; actual compiler is HostX86/x64, with Windows SDK 10.0.26100.0 resolved in the read tlogs. The host-x64 inspection tools are recorded separately. Each configuration has 10 actual CL invocations covering all 133 target/TU pairs (including duplicated intentional PhysicsTests ShapeConvex ownership); C++ TUs use `/std:c++20`, Debug `/MTd`, Release `/MT`, and GLAD remains `/TC`. This conclusion is based on actual commands, cross-checked against the eight generated source inventories.", "",
             "Both build commands used `/t:Build /m:2 /nr:false /p:Platform=x64 /v:diag` from the repository root. Entry contained no compiled outputs; no Clean/Rebuild operation was used. Complete argv, CWD, selected environment, UTC start, elapsed time, exit codes and log hashes are in " + evidence_link("commands.json") + "; tool identities are in " + evidence_link("environment.json") + ".", "",
             "| Exact command / capture | CWD / configuration | Result / exit | Evidence SHA-256 |", "| --- | --- | --- | --- |"]
    for command in commands:
        label = command["command"] if "\n" not in command["command"] else "Python loader probe; exact multiline argv in commands.json"
        lines.append("| `" + label.replace("|", "\\|") + "` | `" + command["cwd"] + "` / " + str(command["configuration"]) + " | " + command["result"] + " / " + str(command["exit_code"]) + " | " + evidence_link(Path(command["log"]).name) + " `" + command["log_sha256"] + "` |")
    lines += ["", "| Mandatory check | Status |", "| --- | --- |"]
    lines += ["| " + v["check"] + " | " + v["result"] + " |" for v in validations]
    lines += ["", "The full Debug PhysicsTests process was terminated by the capture at 120 seconds. No completion summary or assertion failure was reported; exit 1 was caused by termination. This is incomplete evidence, not an established Physics defect or accepted historical failure. Repeat with an adequate declared time limit when the phase can resume. Release/focused cases and benchmarks have no fresh outcomes. Profiling was never generated, so no restoration action is needed.", "",
              "## Warning status and baseline-known failures", "", "| Configuration | First-party compiler | Linker | Vendor/external | Evidence |", "| --- | --- | --- | --- | --- |"]
    for config, data in audit["configurations"].items():
        c = data["warning_counts"]
        lines.append(f"| {config} | {c['first_party']} | {c['linker']} | {c['vendor_external']} | " + evidence_link("compiler-audit.json") + " |")
    lines += ["", "All 986 Debug diagnostics are C4996/STL4043 deprecated checked-array-iterator diagnostics in bundled spdlog/fmt and MSVC STL. Counts exclude template substitution continuation lines and the repeated MSBuild summary, and reconcile to MSBuild's 986. Complete reported instantiation contexts are retained; none reports a first-party location. No blanket suppression or warning-policy change was made.", "",
              "`B00-RUNTIME-001`: fresh RayTracing Debug imports TBB and fails with missing-module status. The status alone does not identify the failing DLL; the absent imported `tbb12.dll` is independently established. Other transitive imports are unverified. `B00-VALIDATION-002`: Debug suite completion unknown after capture timeout. Neither is an approved baseline exception. See " + evidence_link("baseline-failures.json") + ". No historical Physics review was used as fresh evidence.", "",
              "## Human review checklist", "",
              "- [x] Candidate edits fit Phase 00 documentation/capture scope.",
              "- [x] All eight normal target compilations and actual language/CRT flags are evidenced.",
              "- [x] Vendor warnings are separately counted with instantiation contexts retained.",
              "- [ ] Full tests, focused scene/CRT cases and normal/profiling benchmarks complete.",
              "- [ ] Runtime dependency closure, all app starts and disposable safeguard checks pass.",
              "- [x] Protected bytes and index match entry; rejection ownership is explicit.",
              "- [x] Blocked receipt is sealed; approval is disabled and no successor started.", "",
              "## Final Validation Snapshot", "",
              "Snapshot time: " + git_final["timestamp_utc"] + ". Branch: `" + entry["branch"] + "`. Validated build HEAD/source/predecessor peeled commit: `" + entry["head"] + "`.",
              "Configured origin: `https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git`. Published source tag object: `f1a174b35ea0a07b64dafc6eb697ce83f94420c4`, verified peeled commit above. Remote migration branch was absent at entry; Phase 00 approval would publish it. No approved Build reference exists.", "",
              "Entry receipt `.codex/build/phase-00-entry.json` SHA-256: `" + sha(ENTRY) + "`; versionable mirror: " + evidence_link("entry-inventory.json") + ".", "",
              "Complete tracked/resource/library and observed CL/link read closure: " + evidence_link("input-closure.json") + " (`" + sha(EVIDENCE / "input-closure.json") + "`, " + str(len(closure["files"])) + " exact paths). Includes ignored generator outputs, compiler/toolchain implementation, consumed SDK/CRT headers/import libraries, FMOD/runtime files, helpers and missing runtime paths. Full runtime load/resource closure is explicitly incomplete. No generated Conan/CMake/package state exists.", "",
              "Exact file inventory, four classes, entry existence, raw SHA-256, previous/expected Git blobs and checkpoint membership: " + evidence_link("final-inventory.json") + ". The review, final-inventory and git-final hashes are deferred to the external local seal to prevent recursive hashing. The seal contains every final candidate hash including this finalized review. Absent reserved outputs remain absent and are not checkpoint members.", "",
              "| Exact candidate path | Class | Existed at entry | Operation | Raw SHA-256 | Expected Git blob |", "| --- | --- | --- | --- | --- | --- |"]
    final_rows = {r["path"]: r for r in rows}
    for path in sorted(present):
        row = final_rows.get(path)
        lines.append("| `" + path + "` | PHASE_OWNED_NEW_TRACKED | " + str(old[path]["entry_exists"]) + " | " + (row["operation"] if row else "add") + " | " + (row["raw_sha256"] if row else "Final local seal") + " | " + (row["expected_git_blob"] if row else "Final local seal") + " |")
    lines += ["", "All candidate paths above were untracked at entry; `PHASE_OWNED_TRACKED` is empty. Entry-present bootstrap files are explicitly adopted, not phase-created. `LOCAL_GOVERNANCE` includes AGENTS, MANIFEST, the three skills, BUILD_STATE and local entry/seal/transaction receipts. Everything else is `PROTECTED_UNRELATED`; generated build outputs are excluded artifacts. Exact expanded untracked/ignored paths and index entries are in " + evidence_link("git-final.json") + "; the seal supplies per-file hashes and classifications for all current ignored files too.", "",
              "Literal `git status --short`:", "", "```text", status.rstrip(), "```", "",
              "Literal `git diff --check`: empty stdout/stderr, exit 0. Final new-text checks use `git -c safe.directory=C:/dev/GEngine-build diff --no-index --check -- NUL <exact-file>` for each versionable candidate. The 75 non-build-log files have no whitespace diagnostics (no-index exit 1 denotes differing file contents). Both verbatim MSBuild logs report original trailing whitespace (exit 3); their bytes are preserved as raw evidence, not normalized or represented as a clean whitespace result. Exact commands, stdout/stderr and outcomes are recorded in the local blocked seal after finalization. No passing approval exception is granted for these raw-log diagnostics.", "",
              "Index SHA-256 at entry/final: `" + index_hash + "`; exact entries unchanged, staged diff empty. Protected file hashes match entry, including ignored bootstrap helper/project files. Expanded ignored build outputs are generated artifacts and never checkpoint members.", "",
              "Context expansion log:", ""]
    lines += ["- " + reason for reason in entry["context_expansion"]]
    lines += ["- Actual diagnostic CL commands, instantiation chains and read tlogs: required to prove per-TU flags, warning provenance and consumed SDK inputs.",
              "- Debug PE imports and loader search candidates: scope the concrete missing-module runtime blocker. No unrelated source/history expansion.", "",
              "Seal: `.codex/build/phase-00-snapshot.json`, with `workflow=BLOCKED` and `approval_eligible=false`; its SHA-256 is recorded only in local BUILD_STATE. It preserves failed/incomplete evidence and is **not an approval seal for a passing candidate**. Mutable control exclusions are BUILD_STATE, this local seal and future local approval/rejection receipts; none supplies build inputs. Entry and execution-governance hashes are protected. Any behavior/build input edit invalidates validation; any candidate edit invalidates this receipt and must be handled through authorized revision.", "",
              "## Approval and rejection boundary", "",
              "Intended checkpoint membership is exactly the existing PHASE_OWNED_NEW_TRACKED candidate paths above, but checkpointing is blocked. Exclude all LOCAL_GOVERNANCE and PROTECTED_UNRELATED files. No staging, commit, tag or push occurred; PRE_APPROVAL_HEAD/POST_APPROVAL_HEAD and publication receipts do not exist.", "",
              "If rejected under the rejection skill, preserve all 55 bootstrap files because they existed at entry; no tracked file needs restoration. Remove only verified phase-created files listed with entry_exists=false and present in the final seal. Preserve local history/control and unrelated generated output unless separately authorized. Previous source checkpoint is verified. No recursive or broad rollback is authorized.", "",
              "Final workflow: **BLOCKED** on missing runtime dependency/startup and incomplete mandatory reference validation.", ""]
    REVIEW.write_text("\n".join(lines), encoding="utf-8")
    print("Prepared BLOCKED review", relative(REVIEW), "candidate files", len(present), flush=True)


def seal():
    entry = json.loads(ENTRY.read_text())
    assert "Workflow: **BLOCKED**" in REVIEW.read_text()
    assert git("rev-parse", "HEAD").strip() == entry["head"]
    assert git("branch", "--show-current").strip() == entry["branch"]
    assert git("ls-files", "--stage") == entry["index_entries"]
    assert sha(ROOT / git("rev-parse", "--git-path", "index").strip()) == entry["index_sha256"]
    closure = json.loads((EVIDENCE / "input-closure.json").read_text())
    for row in closure["files"]:
        path = ROOT / row["path"]
        assert (sha(path) if path.is_file() else "ABSENT") == row["sha256"], "Closure changed: " + row["path"]
    old = {r["path"]: r for r in entry["files"]}
    current = set(git("ls-files").splitlines()) | set(git("ls-files", "--others", "--exclude-standard").splitlines()) | set(git("ls-files", "--others", "--ignored", "--exclude-standard").splitlines())
    current |= set(entry["exact_owned_paths"])
    records = []
    whitespace = []
    mutable = {"docs/build/BUILD_STATE.md", ".codex/build/phase-00-snapshot.json"}
    for path in sorted(current):
        if path in mutable:
            records.append({"path": path, "classification": "LOCAL_GOVERNANCE", "mutable_control": True, "checkpoint_member": False})
            continue
        previous = old.get(path, {"classification": "LOCAL_GOVERNANCE" if path.startswith(".codex/") else "PROTECTED_UNRELATED",
                                 "entry_exists": False, "entry_sha256": "ABSENT", "previous_blob": "ABSENT"})
        row = file_record(path, previous)
        records.append(row)
        if previous["classification"] == "PROTECTED_UNRELATED" and previous["entry_exists"]:
            assert row["raw_sha256"] == previous["entry_sha256"], "Protected work changed: " + path
        if row["checkpoint_member"]:
            argv = GIT + ["diff", "--no-index", "--check", "--", "NUL", path]
            check = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
            raw_build_log = path in ["docs/build/evidence/phase-00/build-Debug.txt", "docs/build/evidence/phase-00/build-Release.txt"]
            whitespace.append({"argv": argv, "cwd": str(ROOT), "exit_code": check.returncode,
                               "stdout": check.stdout, "stderr": check.stderr,
                               "result": "RAW_EVIDENCE_WHITESPACE_RETAINED; BLOCKED RECEIPT ONLY" if check.stdout and raw_build_log else "PASS" if check.returncode in (0, 1) and not check.stdout else "FAIL"})
            assert (check.returncode in (0, 1) and not check.stdout) or (raw_build_log and check.returncode == 3), "Unexpected new text whitespace result: " + path
    commands = json.loads((EVIDENCE / "commands.json").read_text())
    for record in commands:
        assert sha(ROOT / record["log"]) == record["log_sha256"]
    final_git = json.loads((EVIDENCE / "git-final.json").read_text())
    assert git("status", "--short") == final_git["status_short"]
    snapshot = {"phase": "00", "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "workflow": "BLOCKED", "approval_eligible": False,
                "unmet_gates": ["Missing imported runtime / RayTracing startup", "Incomplete full tests, focused cases, benchmarks, full runtime/safeguard matrix"],
                "branch": entry["branch"], "remote": entry["remote"], "validated_head": entry["head"],
                "source_baseline": entry["previous_tag"], "previous_approved_tag": entry["previous_tag"],
                "previous_approved_peeled_commit": entry["previous_peeled_commit"],
                "remote_verification": entry["remote_verification"], "approved_reference_identity": "NONE",
                "entry_sha256": sha(ENTRY), "review_sha256": sha(REVIEW), "files": records,
                "exact_checkpoint_manifest": [r["path"] for r in records if r["checkpoint_member"]],
                "behavior_build_input_closure": closure, "validation_commands": commands,
                "validation_summary": json.loads((EVIDENCE / "validation-summary.json").read_text()),
                "warning_counts": {c: v["warning_counts"] for c, v in json.loads((EVIDENCE / "compiler-audit.json").read_text())["configurations"].items()},
                "baseline_failures": json.loads((EVIDENCE / "baseline-failures.json").read_text()),
                "git": final_git, "new_text_whitespace": whitespace,
                "mutable_control_exclusions": {"docs/build/BUILD_STATE.md": "local workflow only", ".codex/build/phase-00-snapshot.json": "nonrecursive receipt", "future_local_transaction_receipts": "approval/rejection control only, no candidate/build inputs"},
                "context_expansion": entry["context_expansion"], "limits": closure["limits"]}
    target = ROOT / ".codex/build/phase-00-snapshot.json"
    assert not target.exists(), "Do not replace an existing seal"
    write_json(target, snapshot)
    state = ROOT / "docs/build/BUILD_STATE.md"
    text = state.read_text().replace("Current workflow state: IN_PROGRESS", "Current workflow state: BLOCKED")
    text = text.replace("Final phase validation snapshot: NONE (bootstrap is not phase execution)",
                        "Final phase validation snapshot: .codex/build/phase-00-snapshot.json\nSnapshot SHA-256: " + sha(target) + "\nSnapshot eligibility: BLOCKED; not approvable")
    text = text.replace("Build Phase 00: IN_PROGRESS (START BUILD PHASE 00)", "Build Phase 00: BLOCKED (START BUILD PHASE 00; runtime dependency/startup and incomplete validation)")
    text += "\nPhase 00 review: docs/build/reviews/BUILD_PHASE_00_REVIEW.md\nRuntime blocker: fresh Debug RayTracing imports absent tbb12.dll; source-CWD loader exit 0xc0000135.\nBoth normal configurations built all eight targets; Debug vendor warnings 986, Release 0; first-party/linker warnings 0.\nDebug full tests reached the 120-second capture limit without a final summary; remaining runtime/test/benchmark matrix NOT RUN after stop gate.\nNext action requires a separate human roadmap decision for a narrowly scoped Premake reference runtime-preparation contract. No successor, commit, tag or push.\n"
    state.write_text(text, encoding="utf-8")
    print(json.dumps({"workflow": "BLOCKED", "seal": relative(target), "sha256": sha(target),
                      "candidate_files": len(snapshot["exact_checkpoint_manifest"]), "whitespace_checks": len(whitespace)}, indent=2))


if __name__ == "__main__":
    {"prepare": prepare, "seal": seal}[sys.argv[1]]()
