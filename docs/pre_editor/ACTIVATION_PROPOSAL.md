# Inactive PRE_EDITOR activation proposal

**PROPOSED / NOT_STARTED.** Master sections 16–16.1. Reviewing or storing this package does not activate command routing, create approval state or start Phase 00. No existing PRE_EDITOR command is claimed.

## Proposed destination and starting point

Owner review/acceptance of the plan comes first, followed by a **separate explicit natural-language authorization for PRE_EDITOR activation/minimal setup**.

Phase 00 is the bootstrap entry: verified Rendering Phase 68 completion, owner plan acceptance and that separate setup authorization are prerequisites. Phase 00 then creates the isolated worktree and minimal workflow setup and stops for review. C0's already-activated-worktree requirement applies from Phase 01 onward. This documentation revision does not grant setup or activation authorization.

- Branch: pre-editor/refactor.
- Dedicated worktree: C:/dev/GEngine-pre-editor.
- Starting commit: 9995074e7af161db76c5bf79a5ede030702fb4ca, the verified approved/published Rendering Phase 68 checkpoint.
- Rendering reference remains render-refactor-phase-68-approved, immutable. Do not select an arbitrary later HEAD.
- If a new Rendering completion defect requires an owner-approved corrective checkpoint, record its full SHA and separate entry authorization before setup; never move the Phase 68 tag.
- Read-only audit observed no local pre-editor/* branch and no C:/dev/GEngine-pre-editor directory. Recheck collisions/remotes/worktree registration at activation; do not overwrite, move, clean or reuse an unknown path.

No branch or worktree is created now. The Rendering worktree, its index, owner changes, deleted Physics documents, sealed outputs and old evidence stay intact. Physics experimental edits are not an entry source.

## Isolated writable outputs

Phase 00 must resolve the actual Premake output/runtime path graph before any build/run and document that every writer stays inside the new worktree. Proposed directories are:

| Writer | Isolated destination |
| --- | --- |
| Generated projects / build-system files | C:/dev/GEngine-pre-editor/build/ |
| Intermediate objects / PCH / generated build cache | C:/dev/GEngine-pre-editor/bin-int/ |
| Configuration-specific executables/libraries/symbols | C:/dev/GEngine-pre-editor/bin/{Debug,Release}/ with the actual configured substructure recorded |
| Runtime working directories, imgui.ini, settings, mutable import/shader caches, save data | C:/dev/GEngine-pre-editor/runtime/ |
| Captures, timings, logs, test/probe outputs if specifically admitted | C:/dev/GEngine-pre-editor/logs/pre_editor/phase-XX/ |
| PRE_EDITOR local workflow journals / retention refs | New worktree Git administration namespace under the reused checkpoint design; never Rendering journals |

Relative directories already resolve independently if generation runs from the new worktree, but setup must prove this from current configuration; it must not guess. If redirection requires a small setup configuration change, put it in the Phase 00 exact allowlist before review. Preserve compiler, CRT, language and dependencies. Immutable tools/dependency inputs may be shared read-only only if their caches and outputs remain isolated. No PRE_EDITOR process may write to Phase 68 bin, bin-int, logs, runtime settings or sealed validation artifacts.

## Document transfer and provenance

After separate setup authorization, copy only the reviewed allowlisted docs/pre_editor files enumerated with hashes in evidence/package-files.json, including the unchanged master instruction, audit, canonical findings, residuals, plan, phase contracts, validation/activation/style proposals and observation manifests. Re-hash source and destination and record the original worktree, Phase 68 SHA, audit timestamp and package manifest hash.

The manifest is an audit transfer list, not a seal or approved checkpoint. It lists itself separately to avoid a recursive hash. The destination can retain the current audit package as reviewed source provenance and create active workflow state only under the authorized setup. No broad copy of the Rendering worktree, Git index, .codex/.agents folders, binaries, owner edits, untracked probes or Physics experiments.

Respect real tracking policy: local governance stays local; deliberately tracked compact checkpoints remain small; documentation transfer does not justify force-add. Identify which transferred files are tracked versus local before staging in the later authorized workflow. This audit stages nothing.

## Minimal workflow reuse

Phase 00 should adapt the existing successful checkpoint/manifest/seal/snapshot/protected-path/publication concepts with the smallest explicit PRE_EDITOR namespace/routing extension. Inspect helper reuse before coding; do not create a parallel framework. Existing Rendering command behavior, journals and approved receipts remain unchanged.

A future accepted routing table may map explicit START / REVISE / APPROVE / REJECT PRE_EDITOR PHASE XX instructions to the adapted operations. These words are a design proposal only. No current routing file or skill is changed here. Short ambiguous aliases should not select PRE_EDITOR while Rendering or Physics context could apply.

Execution owns validation and sealing; ordinary approval verifies compact receipt/seal/identity/evidence without routine reruns, then uses safe exact staging/commit/tag/publication. Resume uses the same checkpoint. Rejection preserves unrelated work and approved history. Carry forward the full canonical RBS owner-mutable checkpoint member/fingerprint policy; exact raw/index objects, modes and durable retention refs remain required.

Publication requires an explicit reviewed PRE_EDITOR destination policy or applicable owner grant. The Rendering standing policy is specific to Rendering and is not inferred consent for a new branch/tag family. Proposed repository is the existing AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine; proposed tag family is pre-editor-phase-XX-approved. Destination and setup/phase commit bookkeeping remain OD-01. Discovery alone never produces authorization arguments.

Phase 00 may validate setup identities, transfer hashes and isolation with non-mutating checks; no automatic build, test, application or benchmark matrix. It stops for review of the concrete setup checkpoint. Approval/publication, if explicitly authorized later, is separate from this audit and never starts Phase 01 automatically.

## Smallest next decision

Review and accept or revise PRE_EDITOR_PLAN.md, including phase order, residual gates and open acceptance decisions. After acceptance, separately authorize the proposed isolated activation/minimal setup with its branch/worktree, baseline, output isolation and publication policy. Milestone A is NOT_READY until actual setup/Code Quality prerequisites are implemented, validated and approved.

