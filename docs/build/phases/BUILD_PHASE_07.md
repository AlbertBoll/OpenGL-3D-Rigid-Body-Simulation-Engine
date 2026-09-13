# BUILD PHASE 07: Integrate the Assimp package

## Phase Identity

- Track: Build System Migration; phase 07; status: NOT STARTED.
- Kind: **DEPENDENCY INTEGRATION**.
- Branch: build/cmake-conan-migration; source: render-refactor-phase-00-approved.
- Immediate predecessor / rejection checkpoint: `build-phase-06-approved`.
- Technical prerequisites: 01, 04. These do not permit skipping the serial predecessor.

## Goal

Replace ambiguous Assimp import-library choices in the new path with one approved, reproducible Assimp package target and verified importer behavior.

## Why This Phase Exists

Current Assimp binaries span compiler/runtime generations and filenames do not prove ABI compatibility. Model and animation import semantics must be preserved independently from TBB runtime work.

## Preconditions

Phase 06 is fully approved/published; HEAD equals build-phase-06-approved peeled commit; explicit START BUILD PHASE 07.
No other active phase or pending approval transaction. Load AGENTS, execution skill,
BUILD_STATE and this contract first. Verify status/index, remote, tag and protected
work; freeze exact ownership before changes. Read only relevant audit/code-map/source
sections and record concrete reasons for context expansion. REVISE applies only to
this active phase and invalidates its prior seal before edits.

## Allowed Scope

Conan Assimp requirement/options/lock data; CMake package probes; Assimp provenance documentation; narrowly required include/API compatibility adapters.
This phase's review/evidence and local state/receipts may be updated. Scope families
must be expanded into exact paths before editing; they are not staging wildcards.

## Explicit Non-Goals

No oneTBB integration, ray-tracer work, model-import algorithm rewrite, general
Rendering/Physics refactor, C++23 modernization, commit, tag or push during execution.
Include spelling compatibility must be a small documented adapter or later exact
source prerequisite, never global include pollution.

## Expected Changes

Provide exact Assimp config target/import metadata, approved package/version/options,
and reviewed format/API compatibility for current model and animation importers.
Produce one understandable diff and `../reviews/BUILD_PHASE_07_REVIEW.md` with
the exact checkpoint candidate and Final Validation Snapshot.

## Implementation Constraints

Windows x64/MSVC; preserve C++20, Debug /MTd and Release /MT. Use target-scoped
dependencies and correct PUBLIC/PRIVATE/INTERFACE propagation. No broad PUBLIC
dependencies, uncontrolled Conan fallback or checked-in replacement binaries.
Keep applicable Premake/runtime protections and unrelated work intact. Local state,
AGENTS/MANIFEST/skills/receipts are LOCAL_GOVERNANCE and excluded from checkpoints.

## Mandatory Validation

Fresh locked install/configure/build probes in Debug/Release; import representative
tracked OBJ/DAE/model-animation fixtures and compare counts/material/bone data against
the Premake reference. Inspect actual selected headers/import libraries/runtime
closure and CRT/config provenance. Verify missing/unapproved package configurations
fail deterministically.
Record exact commands/cwd/tool/environment/config/exit/results and evidence hashes.
For compilation, require zero first-party compiler and zero linker warnings; count
vendors separately. Finish with git status --short, expanded new/ignored inventory,
git diff --check plus checks on new text, and protected/index hash comparison.
Seal the finalized review and behavior/build input closure per the execution skill.
A later behavior-affecting edit requires affected validation again; no hash-only refresh.

## Premake Reference Comparison

Compare real importer outputs and supported formats/API behavior, not DLL filename
similarity. Runtime/ABI provenance differences must be explicit; retain Premake
reference binaries until equivalence is demonstrated.
Use source-tag/Phase 00 evidence plus the previous approved checkpoint. Distinguish
PASS, unchanged baseline-known failure, NOT RUN and BLOCKED.

## Human Review Checklist

- [ ] Assimp package/version/options and provenance are explicit.
- [ ] Representative importer semantics match the reference.
- [ ] Actual Debug/Release CRT/import/runtime closure is verified.
- [ ] No TBB or unrelated subsystem work entered the phase.
- [ ] Snapshot covers the final exact candidate and preserves unrelated work.

## Approval Boundary

Execution ends AWAITING HUMAN REVIEW or BLOCKED and never commits/tags/pushes.
APPROVE BUILD PHASE 07 uses cheap snapshot/hash/Git checks only. Only explicit
WITH REVALIDATION repeats full mandatory validation after verifying unchanged hashes.
Stage only exact PHASE_OWNED_TRACKED/PHASE_OWNED_NEW_TRACKED files.
Record PRE_APPROVAL_HEAD, create exactly one reviewed checkpoint, require
POST_APPROVAL_HEAD != PRE_APPROVAL_HEAD, and annotate `build-phase-07-approved`
peeling exactly to POST. Push branch then tag; persist final local approved state
only after all gates succeed. Never start the next phase automatically.

## Rejection Boundary

Verify `build-phase-06-approved` and its recorded peeled commit. Restore only exact
owned tracked files from that checkpoint and remove only verified new phase-created
files absent at entry/checkpoint. Preserve bootstrap entry files, local governance,
review history and unrelated bytes/index entries. No reset-hard, clean-fd, broad
restore, wildcard or recursive deletion. If isolation is unsafe, STOP.

## Stop/Block Conditions

Unknown Assimp compatibility/provenance, changed importer semantics, unsupported
legacy API requiring broad source work, unresolved CRT/runtime mismatch, wrong
branch/HEAD/predecessor, unsafe ownership overlap, changed sealed candidate or
required work outside this contract. Recommend the smallest follow-up preparation
phase instead of expanding scope.
