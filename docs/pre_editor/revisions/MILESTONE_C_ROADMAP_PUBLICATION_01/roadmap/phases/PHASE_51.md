# PRE_EDITOR Phase 51 — Authoritative transactions and grouped mutations

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: E. No implementation or phase activation authorized by this file.

## Goal and traceability

Apply Undoable authoring edits through validate/apply/commit/rollback semantics.

Canonical findings: AUT-04. Master section references: E.8, E.13, E.17 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 50 (must actually be approved and published before START).

Additional providing phases/baseline: [13](/docs/pre_editor/phases/PHASE_13.md), [14](/docs/pre_editor/phases/PHASE_14.md), [44](PHASE_44.md), [46](PHASE_46.md), [47](PHASE_47.md), [48](PHASE_48.md), [50](PHASE_50.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Property and entity create/delete/duplicate/reparent changes; transforms/materials/cameras/lights/Physics/constraints use shared schema. Grouped local/world/pivot semantics avoid double application to selected parent-child pairs. Stage resource edits safely and emit authoritative notifications only after successful commit.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Scene/_Scene.h](/GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](/GEngine/src/Scene/_Scene.cpp)
- [GEngine/src/Scene/_Entity.cpp](/GEngine/src/Scene/_Entity.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: One transaction implementation pair using Phase 46 metadata and 44 mutations.

Change budget and reason: 4–6 production, 1–3 atomicity/grouped-edit checks/docs.

Non-goals: No selection/gizmo/mixed-value UI, independent property system, event-driven mutation or Undo history storage yet.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E grouped transform/material/camera/light/body/constraint and lifecycle changes; validation/application failure and rollback preserve state/resources; parent-child pivot once, commit revision once, notification count/order; no notifications for failed transaction.

Evidence reuse: Phase 14 successful runtime notifications now wrapped/deferred to commit, 46 schema and 50 safe resource replacement. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Perform real grouped property/hierarchy edits transactionally.

Manual acceptance: Review atomicity, pivot behavior and committed-only notifications.

## Observable exit and review

Atomic authoring mutation has clear boundaries and grouped semantics without claiming UI or an event-command architecture.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_48.md`, SHA256 `33f8a9e0ff5b2311ed3a1f05d98e2a4468d075a0beeff286c951da5f55a36863`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.

## Milestone C successor obligations

Additional providers: [30](PHASE_30.md), [31](PHASE_31.md), [32](PHASE_32.md). Include motor enable/target/cap and mover authored trajectory edits in validate/apply/commit/rollback. Failed grouped updates preserve last-valid runtime targets and send no successful-change event. Use existing transaction provider and budgets.
