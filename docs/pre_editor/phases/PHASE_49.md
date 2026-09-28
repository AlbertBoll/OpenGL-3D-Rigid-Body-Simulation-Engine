# PRE_EDITOR Phase 49 — Undo/Redo and dirty/saved revisions

**PROPOSED / NOT_STARTED.** Milestone: E. No implementation or activation authorized by this file.

## Goal and traceability

Restore authored changes and track save state through engine-level history.

Canonical findings: AUT-04. Master section references: E.9, E.10 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 48 (must actually be approved and published before START).

Additional providing phases/baseline: [44](PHASE_44.md), [45](PHASE_45.md), [48](PHASE_48.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Transaction history/deltas for properties/entity creation/deletion/hierarchy/material/camera/light/body/constraints; deterministic identity restoration and resource retention policy; Scene/Asset document revision versus saved revision, history truncation and failure rules.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Scene/_Scene.h](../../../GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Undo history/dirty-state extension of Phase 48 transactions and Phase 43 schema.

Change budget and reason: 3–5 production, 1–3 history/dirty/resource checks/docs.

Non-goals: No Editor Undo UI, disk autosave service or serialization of live runtime handles.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E edit/Undo/Redo across all required property categories and create/delete/reparent, save/load revision transitions, branching history, missing asset failure, bounded history bytes/retained versions and committed notifications.

Evidence reuse: Phase 48 atomic deltas, Phase 44/45 persistence and generation identity. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Use actual maintained edits, save, Undo and Redo and verify scene/render/Physics descriptions.

Manual acceptance: Human review restored state and meaningful dirty indicators.

## Observable exit and review

Undo/Redo restores semantic authoring state and correct dirty/saved status without reviving stale runtime references.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

