# BUILD PHASE 44: Retire Premake after the authoritative switch

## Phase Identity

- Track: Build System Migration; phase 44; status: NOT STARTED.
- Kind: **PREMAKE RETIREMENT**.
- Branch: build/cmake-conan-migration; source: render-refactor-phase-00-approved.
- Immediate predecessor / rejection checkpoint: `build-phase-43-approved`.
- Technical prerequisites: 43. These do not permit skipping the serial predecessor.

## Goal

Remove obsolete Premake build orchestration only after all replacement contracts are approved.

## Why This Phase Exists

Early deletion would remove the trusted comparison and runtime safeguards; retirement is a separate final checkpoint.

## Preconditions

Phase 43 is fully approved/published; HEAD equals build-phase-43-approved peeled commit; explicit START BUILD PHASE 44.
No other active phase or pending approval transaction. Load AGENTS, execution skill,
BUILD_STATE and this contract first. Verify status/index, remote, tag and protected
work; freeze exact ownership before changes. Read only relevant audit/code-map/source
sections and record concrete reasons for context expansion. REVISE applies only to
this active phase and invalidates its prior seal before edits.

## Allowed Scope

premake5.lua, external/glad/premake5.lua, bundled Premake executables/license only as actually exclusive; Premake-only tools/CLI routes and related documentation/ignore rules; final retention inventory.
This phase's review/evidence and local state/receipts may be updated. Scope families
must be expanded into exact paths before editing; they are not staging wildcards.

## Explicit Non-Goals

No other phase, next-phase implementation, unlisted production change, general
Rendering/Physics refactor, C++23 modernization, commit, tag or push during execution.
No broad bin/vendor deletion, SDK/asset cleanup, history rewrite or source refactor. Tracked DLLs/resources and historical objects require separate cleanup authorization, not incidental retirement.

## Expected Changes

Delete only exact proven Premake-exclusive inputs/entrypoints. Preserve shared postbuild/runtime code if any supported caller still uses it; remove it only when all equivalent CMake protections and callers are proven. Retain immutable reference evidence and source-tag reconstruction instructions.
Produce one understandable diff and `../reviews/BUILD_PHASE_44_REVIEW.md` with
the exact checkpoint candidate and Final Validation Snapshot.

## Implementation Constraints

Windows x64/MSVC; preserve C++20, Debug /MTd and Release /MT. Use target-scoped
dependencies and correct PUBLIC/PRIVATE/INTERFACE propagation, STATIC for compiled
modules and INTERFACE only for genuine headers/policy. No circular link tricks,
duplicate source workarounds, broad PUBLIC dependencies or uncontrolled Conan fallback.
Keep applicable Premake/runtime protections and unrelated work intact. Local state,
AGENTS/MANIFEST/skills/receipts are LOCAL_GOVERNANCE and excluded from checkpoints.

## Mandatory Validation

Search all callers/entrypoints to prove deleted files are unused. From a fresh checkout/build/staging root run full Debug/Release build, full tests, benchmark smoke and all app startup using canonical driver/direct workflows. Verify actual flags/warnings/runtime/resources and failure safeguards still satisfy Phase 42. Confirm rebuilding the old source tag remains documented and protected inputs unchanged.
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

Premake is reconstructed only at its preserved historical tag after this phase; final CMake results must match the approved equivalence evidence. Archive exact removal/retention inventory.
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
APPROVE BUILD PHASE 44 uses cheap snapshot/hash/Git checks only. Only explicit
WITH REVALIDATION repeats full mandatory validation, after verifying unchanged
hashes; changed behavior/build hashes STOP even in that mode. Stage only exact
PHASE_OWNED_TRACKED/PHASE_OWNED_NEW_TRACKED files via git add -- exact-files.
Record PRE_APPROVAL_HEAD, create exactly one new reviewed checkpoint, require
POST_APPROVAL_HEAD != PRE_APPROVAL_HEAD, and annotate `build-phase-44-approved`
peeling exactly to POST. Push branch then tag; persist final local approved state
only after all gates succeed. No new commit means NOT APPROVED. Never start the
next phase automatically. Follow the approval skill for interrupted transactions.

## Rejection Boundary

Verify `build-phase-43-approved` and its recorded peeled commit. Restore only the exact owned
tracked files from that checkpoint and remove only verified new phase-created files
absent at entry/checkpoint. Preserve bootstrap entry files, local governance/review
history, and all unrelated bytes/index entries. Use the rejection skill; no reset-hard,
clean-fd, broad restore, wildcard or recursive deletion. A partially published
approval is transaction recovery, not ordinary rejection. If isolation is unsafe, STOP.

## Stop/Block Conditions

Any deleted script still has a caller, runtime protection regresses, clean-room validation fails or removing vendor bytes would affect a surviving dependency.
Also block on wrong branch/HEAD/predecessor, unsafe ownership/index overlap, changed
sealed candidate, missing mandatory evidence or required work outside this contract.
Recommend the smallest additional preparation phase, with dependency and validation
boundary; do not implement it implicitly. End BLOCKED when a required gate is unmet.
