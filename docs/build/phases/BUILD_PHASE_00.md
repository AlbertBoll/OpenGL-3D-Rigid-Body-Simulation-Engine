# BUILD PHASE 00: Capture the current Premake reference

## Phase Identity

- Track: Build System Migration; phase 00; status: NOT STARTED.
- Kind: **REFERENCE PREPARATION**.
- Branch: build/cmake-conan-migration; source: render-refactor-phase-00-approved.
- Immediate predecessor / rejection checkpoint: `render-refactor-phase-00-approved`.
- Technical prerequisites: Source tag and bootstrap review. These do not permit skipping the serial predecessor.

## Goal

Establish reproducible current Debug/Release compiler, warning, test and runtime evidence before production migration.

## Why This Phase Exists

Bootstrap inspected generated XML and binaries; it did not validate compiler commands or application outcomes. Old Physics reviews describe a different CRT baseline.

## Preconditions

Human has reviewed bootstrap; explicit START BUILD PHASE 00; HEAD is the source-tag commit.
No other active phase or pending approval transaction. Load AGENTS, execution skill,
BUILD_STATE and this contract first. Verify status/index, remote, tag and protected
work; freeze exact ownership before changes. Read only relevant audit/code-map/source
sections and record concrete reasons for context expansion. REVISE applies only to
this active phase and invalidates its prior seal before edits.

## Allowed Scope

Versionable bootstrap docs/build files except BUILD_STATE.md; new reference evidence under docs/build/evidence/phase-00/; narrowly scoped reference-capture helpers under scripts/build_validation/ if needed. Existing build/source/runtime files are read-only.
This phase's review/evidence and local state/receipts may be updated. Scope families
must be expanded into exact paths before editing; they are not staging wildcards.

## Explicit Non-Goals

No other phase, next-phase implementation, unlisted production change, general
Rendering/Physics refactor, C++23 modernization, commit, tag or push during execution.
No production CMake/Conan, engine/application edits, dependency upgrades or reference build fixes. Preserve tracked DLLs and source resources. A source-tag discrepancy blocks.

## Expected Changes

Explicitly adopt the exact bootstrap audit/plan/contracts/template/evidence/review files into this phase inventory; capture VS2022 tool/SDK versions, both configuration logs, imports, tests, app startup and a baseline-failure registry.
Produce one understandable diff and `../reviews/BUILD_PHASE_00_REVIEW.md` with
the exact checkpoint candidate and Final Validation Snapshot.

## Implementation Constraints

Windows x64/MSVC; preserve C++20, Debug /MTd and Release /MT. Use target-scoped
dependencies and correct PUBLIC/PRIVATE/INTERFACE propagation, STATIC for compiled
modules and INTERFACE only for genuine headers/policy. No circular link tricks,
duplicate source workarounds, broad PUBLIC dependencies or uncontrolled Conan fallback.
Keep applicable Premake/runtime protections and unrelated work intact. Local state,
AGENTS/MANIFEST/skills/receipts are LOCAL_GOVERNANCE and excluded from checkpoints.

## Mandatory Validation

Regenerate with vendor/bin/premake/premake5.exe vs2022. Build all eight targets in Debug and Release with the verified VS2022 MSBuild /p:Platform=x64 and diagnostic logs. Inspect every cl command for C++20 and /MTd or /MT (GLAD remains C), compiler/linker warning counts and resolved SDK/options. Run full PhysicsTests and documented focused scene/CRT cases in both configs; run fixed-input PhysicsBenchmark normal and profiling-enabled, restoring normal generation afterward. Launch all four apps from their declared source CWD in both configs and record DLL imports/load results/resource failures. Verify runtime safeguard behavior in a disposable copied output, never by deleting tracked bin DLLs.
Record exact commands/cwd/tool/environment/config/exit/results and evidence hashes.
For compilation, require zero first-party compiler and zero linker warnings; count
vendors separately. Never infer actual flags from CMake variables/XML alone.
For documentary checks, explicitly mark compile/runtime not applicable rather than
claiming a build pass. Finish with git status --short, expanded new/ignored inventory,
git diff --check plus checks on new text, and protected/index hash comparison.
Seal the finalized review and complete behavior/build input closure as specified by
the execution skill. A later behavior-affecting edit requires affected validation
again; no hash-only refresh.

## Premake Reference Comparison

This phase creates the authoritative current reference. Compare historical results only to explain differences, never to assert fresh success. Build/CRT/warning gates must pass; reproducible existing functional defects can be recorded with exact signatures and human review.
Use source-tag/Phase 00 evidence plus the previous approved checkpoint. Distinguish
PASS, unchanged baseline-known failure, NOT RUN and BLOCKED. Never grandfather a
new failure or use historical results as fresh measurements.

## Human Review Checklist

- [ ] Goal and expected changes above are complete and limited to allowed paths.
- [ ] The specific dependency/boundary reason above is resolved with evidence.
- [ ] Ownership and any public/private/runtime effects match the candidate graph.
- [ ] Every mandatory check and reference comparison has an accurate result.
- [ ] Language/CRT/warning gates and known-failure classifications are supported.
- [ ] Snapshot covers the final exact candidate and preserves unrelated work.
- [ ] Rejection actions are isolated and no successor has started.

## Approval Boundary

Execution ends AWAITING HUMAN REVIEW or BLOCKED and never commits/tags/pushes.
APPROVE BUILD PHASE 00 uses cheap snapshot/hash/Git checks only. Only explicit
WITH REVALIDATION repeats full mandatory validation, after verifying unchanged
hashes; changed behavior/build hashes STOP even in that mode. Stage only exact
PHASE_OWNED_TRACKED/PHASE_OWNED_NEW_TRACKED files via git add -- exact-files.
Record PRE_APPROVAL_HEAD, create exactly one new reviewed checkpoint, require
POST_APPROVAL_HEAD != PRE_APPROVAL_HEAD, and annotate `build-phase-00-approved`
peeling exactly to POST. Push branch then tag; persist final local approved state
only after all gates succeed. No new commit means NOT APPROVED. Never start the
next phase automatically. Follow the approval skill for interrupted transactions.

## Rejection Boundary

Verify `render-refactor-phase-00-approved` and its recorded peeled commit. Restore only the exact owned
tracked files from that checkpoint and remove only verified new phase-created files
absent at entry/checkpoint. Preserve bootstrap entry files, local governance/review
history, and all unrelated bytes/index entries. Use the rejection skill; no reset-hard,
clean-fd, broad restore, wildcard or recursive deletion. A partially published
approval is transaction recovery, not ordinary rejection. If isolation is unsafe, STOP.

## Stop/Block Conditions

Missing tool/SDK/DLL/driver, inability to validate both configs, new/unclassified failures, nonzero first-party/linker warnings, or baseline commands needing source fixes: stop and propose the smallest reference-preparation phase.
Also block on wrong branch/HEAD/predecessor, unsafe ownership/index overlap, changed
sealed candidate, missing mandatory evidence or required work outside this contract.
Recommend the smallest additional preparation phase, with dependency and validation
boundary; do not implement it implicitly. End BLOCKED when a required gate is unmet.
