# PRE_EDITOR Phase 52 — Undo/Redo and dirty/saved revisions

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: E. No implementation or phase activation authorized by this file.

## Goal and traceability

Restore authored changes and track save state through engine-level history.

Canonical findings: AUT-04. Master section references: E.9, E.10 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 51 (must actually be approved and published before START).

Additional providing phases/baseline: [47](PHASE_47.md), [48](PHASE_48.md), [51](PHASE_51.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Transaction history/deltas for properties/entity creation/deletion/hierarchy/material/camera/light/body/constraints; deterministic identity restoration and resource retention policy; Scene/Asset document revision versus saved revision, history truncation and failure rules.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Scene/_Scene.h](/GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](/GEngine/src/Scene/_Scene.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Undo history/dirty-state extension of Phase 51 transactions and Phase 46 schema.

Change budget and reason: 3–5 production, 1–3 history/dirty/resource checks/docs.

Non-goals: No Editor Undo UI, disk autosave service or serialization of live runtime handles.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E edit/Undo/Redo across all required property categories and create/delete/reparent, save/load revision transitions, branching history, missing asset failure, bounded history bytes/retained versions and committed notifications.

Evidence reuse: Phase 51 atomic deltas, Phase 47/48 persistence and generation identity. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Use actual maintained edits, save, Undo and Redo and verify scene/render/Physics descriptions.

Manual acceptance: Human review restored state and meaningful dirty indicators.

## Observable exit and review

Undo/Redo restores semantic authoring state and correct dirty/saved status without reviving stale runtime references.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_49.md`, SHA256 `bdf7a6ff8eeb4a99d603b0ce80a5c5d897122f8453568f0bec343e4a8b7ee1fc`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.

## Milestone C successor obligations

Additional providers: [30](PHASE_30.md), [31](PHASE_31.md), [32](PHASE_32.md). Include motor/mover authored settings in Undo/Redo and dirty/saved revisions. Validate restored references and limits; runtime accumulated impulses and elapsed simulation state are not authoring history. Use existing history provider and budgets.
