# BUILD PHASE 41: Complete Python run, build-run, test and safe clean

## Phase Identity

- Track: Build System Migration; phase 41; status: NOT STARTED.
- Kind: **DRIVER INTEGRATION**.
- Branch: build/cmake-conan-migration; source: render-refactor-phase-00-approved.
- Immediate predecessor / rejection checkpoint: `build-phase-40-approved`.
- Technical prerequisites: 40. These do not permit skipping the serial predecessor.

## Goal

Expose the remaining thin orchestration commands using CMake-owned runtime/test/output metadata.

## Why This Phase Exists

Correct CWD/staging/failure handling and safe cleaning are required to make the new build usable without hidden machine state.

## Preconditions

Phase 40 is fully approved/published; HEAD equals build-phase-40-approved peeled commit; explicit START BUILD PHASE 41.
No other active phase or pending approval transaction. Load AGENTS, execution skill,
BUILD_STATE and this contract first. Verify status/index, remote, tag and protected
work; freeze exact ownership before changes. Read only relevant audit/code-map/source
sections and record concrete reasons for context expansion. REVISE applies only to
this active phase and invalidates its prior seal before edits.

## Allowed Scope

scripts/gengine.py/helpers; CMake-generated consumer metadata interface; driver behavior fixtures/docs.
This phase's review/evidence and local state/receipts may be updated. Scope families
must be expanded into exact paths before editing; they are not staging wildcards.

## Explicit Non-Goals

No other phase, next-phase implementation, unlisted production change, general
Rendering/Physics refactor, C++23 modernization, commit, tag or push during execution.
run never silently builds; build-run stops on build/staging failure. No duplicated dependency/resource graph. Never clean tracked Premake bin, model OBJ, SDK, source, protected work or unchecked/reparse paths.

## Expected Changes

Add run/build-run/test/clean with target/config selection; resolve runtime/CWD/test metadata from CMake; restrict clean to verified owned generated outputs.
Produce one understandable diff and `../reviews/BUILD_PHASE_41_REVIEW.md` with
the exact checkpoint candidate and Final Validation Snapshot.

## Implementation Constraints

Windows x64/MSVC; preserve C++20, Debug /MTd and Release /MT. Use target-scoped
dependencies and correct PUBLIC/PRIVATE/INTERFACE propagation, STATIC for compiled
modules and INTERFACE only for genuine headers/policy. No circular link tricks,
duplicate source workarounds, broad PUBLIC dependencies or uncontrolled Conan fallback.
Keep applicable Premake/runtime protections and unrelated work intact. Local state,
AGENTS/MANIFEST/skills/receipts are LOCAL_GOVERNANCE and excluded from checkpoints.

## Mandatory Validation

Exercise each command for kernel tests and representative app in both configs. Check failure propagation, explicit CWD, missing/stale runtime, target/config selection and cancellation. Test clean dry-run/actual deletion in disposable output fixtures including traversal/space/reparse/protected-path rejection; verify tracked and unrelated hashes unchanged.
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

Match reference app startup/test exit semantics while using deterministic new staging. Existing Premake run/postbuild safeguards still work.
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
APPROVE BUILD PHASE 41 uses cheap snapshot/hash/Git checks only. Only explicit
WITH REVALIDATION repeats full mandatory validation, after verifying unchanged
hashes; changed behavior/build hashes STOP even in that mode. Stage only exact
PHASE_OWNED_TRACKED/PHASE_OWNED_NEW_TRACKED files via git add -- exact-files.
Record PRE_APPROVAL_HEAD, create exactly one new reviewed checkpoint, require
POST_APPROVAL_HEAD != PRE_APPROVAL_HEAD, and annotate `build-phase-41-approved`
peeling exactly to POST. Push branch then tag; persist final local approved state
only after all gates succeed. No new commit means NOT APPROVED. Never start the
next phase automatically. Follow the approval skill for interrupted transactions.

## Rejection Boundary

Verify `build-phase-40-approved` and its recorded peeled commit. Restore only the exact owned
tracked files from that checkpoint and remove only verified new phase-created files
absent at entry/checkpoint. Preserve bootstrap entry files, local governance/review
history, and all unrelated bytes/index entries. Use the rejection skill; no reset-hard,
clean-fd, broad restore, wildcard or recursive deletion. A partially published
approval is transaction recovery, not ordinary rejection. If isolation is unsafe, STOP.

## Stop/Block Conditions

Unsafe cleanup target, graph duplication, hidden build on run, swallowed test failure or stale runtime can satisfy run validation.
Also block on wrong branch/HEAD/predecessor, unsafe ownership/index overlap, changed
sealed candidate, missing mandatory evidence or required work outside this contract.
Recommend the smallest additional preparation phase, with dependency and validation
boundary; do not implement it implicitly. End BLOCKED when a required gate is unmet.
