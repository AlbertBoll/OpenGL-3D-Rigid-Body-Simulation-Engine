# BUILD PHASE 13: Remove Physics header and PCH dependence on scene components

## Phase Identity

- Track: Build System Migration; phase 13; status: NOT STARTED.
- Kind: **CYCLE / BOUNDARY PREPARATION**.
- Branch: build/cmake-conan-migration; source: render-refactor-phase-00-approved.
- Immediate predecessor / rejection checkpoint: `build-phase-12-approved`.
- Technical prerequisites: 11, 12. These do not permit skipping the serial predecessor.

## Goal

Prove Physics kernel headers and TUs can compile without Scene, GPU or UI includes.

## Why This Phase Exists

Shape.h and PhysicsBody.h include the mixed Component.h, although direct component usage is largely absent; gepch injects SDL/GLAD everywhere.

## Preconditions

Phase 12 is fully approved/published; HEAD equals build-phase-12-approved peeled commit; explicit START BUILD PHASE 13.
No other active phase or pending approval transaction. Load AGENTS, execution skill,
BUILD_STATE and this contract first. Verify status/index, remote, tag and protected
work; freeze exact ownership before changes. Read only relevant audit/code-map/source
sections and record concrete reasons for context expansion. REVISE applies only to
this active phase and invalidates its prior seal before edits.

## Allowed Scope

Exact Physics public/implementation prerequisite includes, related neutral compatibility headers and Premake per-file PCH settings; ownership/compile evidence.
This phase's review/evidence and local state/receipts may be updated. Scope families
must be expanded into exact paths before editing; they are not staging wildcards.

## Explicit Non-Goals

No other phase, next-phase implementation, unlisted production change, general
Rendering/Physics refactor, C++23 modernization, commit, tag or push during execution.
No physics refactor for aesthetics, algorithm changes, registry relocation or Scene runtime lifecycle rewrite. Any genuinely required component type needs the smallest separately scoped value boundary.

## Expected Changes

Remove unused Component reach and replace implicit PCH prerequisites with explicit headers or a private narrow PCH; use the Math-owned Bounds. Preserve all types/layouts and solver code.
Produce one understandable diff and `../reviews/BUILD_PHASE_13_REVIEW.md` with
the exact checkpoint candidate and Final Validation Snapshot.

## Implementation Constraints

Windows x64/MSVC; preserve C++20, Debug /MTd and Release /MT. Use target-scoped
dependencies and correct PUBLIC/PRIVATE/INTERFACE propagation, STATIC for compiled
modules and INTERFACE only for genuine headers/policy. No circular link tricks,
duplicate source workarounds, broad PUBLIC dependencies or uncontrolled Conan fallback.
Keep applicable Premake/runtime protections and unrelated work intact. Local state,
AGENTS/MANIFEST/skills/receipts are LOCAL_GOVERNANCE and excluded from checkpoints.

## Mandatory Validation

Compile each Physics public header without PCH, SDL, GLAD, Scene or ImGui include roots. Rebuild current PhysicsTests/Benchmark in both configs and run full regression plus a fixed-input benchmark sample. Compare actual compiler flags, types and deterministic outputs; inspect dependency scans for remaining high-level headers.
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

Premake numerical/solver results and profiling semantics must match reference. Record only include/PCH differences.
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
APPROVE BUILD PHASE 13 uses cheap snapshot/hash/Git checks only. Only explicit
WITH REVALIDATION repeats full mandatory validation, after verifying unchanged
hashes; changed behavior/build hashes STOP even in that mode. Stage only exact
PHASE_OWNED_TRACKED/PHASE_OWNED_NEW_TRACKED files via git add -- exact-files.
Record PRE_APPROVAL_HEAD, create exactly one new reviewed checkpoint, require
POST_APPROVAL_HEAD != PRE_APPROVAL_HEAD, and annotate `build-phase-13-approved`
peeling exactly to POST. Push branch then tag; persist final local approved state
only after all gates succeed. No new commit means NOT APPROVED. Never start the
next phase automatically. Follow the approval skill for interrupted transactions.

## Rejection Boundary

Verify `build-phase-12-approved` and its recorded peeled commit. Restore only the exact owned
tracked files from that checkpoint and remove only verified new phase-created files
absent at entry/checkpoint. Preserve bootstrap entry files, local governance/review
history, and all unrelated bytes/index entries. Use the rejection skill; no reset-hard,
clean-fd, broad restore, wildcard or recursive deletion. A partially published
approval is transaction recovery, not ordinary rejection. If isolation is unsafe, STOP.

## Stop/Block Conditions

A real kernel component dependency requires wider redesign, numerical fingerprints drift, or any high-level header still leaks into the proposed kernel.
Also block on wrong branch/HEAD/predecessor, unsafe ownership/index overlap, changed
sealed candidate, missing mandatory evidence or required work outside this contract.
Recommend the smallest additional preparation phase, with dependency and validation
boundary; do not implement it implicitly. End BLOCKED when a required gate is unmet.
