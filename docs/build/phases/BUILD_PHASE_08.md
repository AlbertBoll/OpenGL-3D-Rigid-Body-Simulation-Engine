# BUILD PHASE 08: Integrate the oneTBB package

## Phase Identity

- Track: Build System Migration; phase 08; status: NOT STARTED.
- Kind: **DEPENDENCY INTEGRATION**.
- Branch: build/cmake-conan-migration; source: render-refactor-phase-00-approved.
- Immediate predecessor / rejection checkpoint: `build-phase-07-approved`.
- Technical prerequisites: 01, 04. These do not permit skipping the serial predecessor.

## Goal

Replace ambiguous TBB debug/release library selection with one approved oneTBB package target and verified per-configuration runtime behavior.

## Why This Phase Exists

Current Premake linkage lists multiple TBB variants while required runtime DLLs are
not reliably present. TBB runtime/configuration validation is independent from Assimp
import semantics and deserves its own checkpoint.

## Preconditions

Phase 07 is fully approved/published; HEAD equals build-phase-07-approved peeled commit; explicit START BUILD PHASE 08.
No other active phase or pending approval transaction. Load AGENTS, execution skill,
BUILD_STATE and this contract first. Verify status/index, remote, tag and protected
work; freeze exact ownership before changes. Read only relevant audit/code-map/source
sections and record concrete reasons for context expansion.

## Allowed Scope

Conan oneTBB requirement/options/lock data; CMake package probes; TBB include/namespace
compatibility and dependency provenance documentation; narrowly required compatibility
adapter if proven necessary.
This phase's review/evidence and local state/receipts may be updated. Scope families
must be expanded into exact paths before editing.

## Explicit Non-Goals

No Assimp changes, ray-tracing algorithm rewrite, performance optimization, broad
Render refactor, C++23 modernization, commit, tag or push during execution.
Do not hide wrong-config runtime selection with PATH or stale bin contents.

## Expected Changes

Provide exact oneTBB config targets/import metadata, approved version/options and
documented compatibility for current `parallel_for`/TBB usage.
Produce one understandable diff and `../reviews/BUILD_PHASE_08_REVIEW.md` with
the exact checkpoint candidate and Final Validation Snapshot.

## Implementation Constraints

Windows x64/MSVC; preserve C++20, Debug /MTd and Release /MT. Use target-scoped
dependencies and correct propagation. No global library directories, duplicated
debug+release linkage, uncontrolled Conan fallback or stale runtime dependence.
Keep Premake/runtime protections and unrelated work intact. Local governance remains
outside Git checkpoints according to policy.

## Mandatory Validation

Fresh locked install/configure/build probes in Debug/Release; compile and execute a
small representative oneTBB `parallel_for` workload and compare deterministic results
with the reference behavior. Inspect actual selected import/runtime libraries,
architecture/configuration and CRT closure. Validate execution from a restricted
runtime environment that cannot succeed solely because of stale bin/PATH contents.
Verify missing/wrong-config package/runtime cases fail deterministically.
Record exact commands/cwd/tool/environment/config/exit/results and evidence hashes.
Require zero first-party compiler and zero linker warnings; count vendors separately.
Finish with git status --short, new/ignored inventory, git diff --check and protected
/index hash comparison. Seal the final review per the execution skill.

## Premake Reference Comparison

Compare observed TBB workload behavior and actual runtime/configuration closure, not
library filename similarity. Record corrections to the ambiguous Premake linkage as
intentional migration differences while preserving functional behavior.

## Human Review Checklist

- [ ] oneTBB package/version/options are explicit.
- [ ] Debug/Release choose coherent import/runtime binaries.
- [ ] Representative parallel behavior matches reference.
- [ ] Restricted-runtime test proves no stale DLL/PATH dependency.
- [ ] No Assimp or unrelated subsystem work entered the phase.
- [ ] Snapshot covers the final exact candidate.

## Approval Boundary

Execution ends AWAITING HUMAN REVIEW or BLOCKED and never commits/tags/pushes.
APPROVE BUILD PHASE 08 uses cheap snapshot/hash/Git checks only. Only explicit
WITH REVALIDATION repeats full mandatory validation after verifying unchanged hashes.
Stage only exact PHASE_OWNED_TRACKED/PHASE_OWNED_NEW_TRACKED files.
Record PRE_APPROVAL_HEAD, require a real new commit, and annotate
`build-phase-08-approved` peeling exactly to POST_APPROVAL_HEAD. Push branch then tag;
persist final local approved state only after all gates succeed.

## Rejection Boundary

Verify `build-phase-07-approved` and restore only exact phase-owned changes from that
checkpoint. Remove only verified new phase-created files absent at entry/checkpoint.
Preserve local governance and unrelated work. No reset-hard, clean-fd, broad restore,
wildcard or recursive deletion. If isolation is unsafe, STOP.

## Stop/Block Conditions

Unresolved oneTBB package/API compatibility, missing/wrong architecture runtime,
configuration ambiguity, behavior mismatch, reliance on stale runtime files, required
broad algorithm changes, wrong branch/HEAD/predecessor, changed sealed candidate or
unsafe ownership overlap.
