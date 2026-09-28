# PRE_EDITOR Phase 48 — Authoritative transactions and grouped mutations

**PROPOSED / NOT_STARTED.** Milestone: E. No implementation or activation authorized by this file.

## Goal and traceability

Apply Undoable authoring edits through validate/apply/commit/rollback semantics.

Canonical findings: AUT-04. Master section references: E.8, E.13, E.17 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 47 (must actually be approved and published before START).

Additional providing phases/baseline: [13](PHASE_13.md), [14](PHASE_14.md), [41](PHASE_41.md), [43](PHASE_43.md), [44](PHASE_44.md), [45](PHASE_45.md), [47](PHASE_47.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Property and entity create/delete/duplicate/reparent changes; transforms/materials/cameras/lights/Physics/constraints use shared schema. Grouped local/world/pivot semantics avoid double application to selected parent-child pairs. Stage resource edits safely and emit authoritative notifications only after successful commit.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Scene/_Scene.h](../../../GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [GEngine/src/Scene/_Entity.cpp](../../../GEngine/src/Scene/_Entity.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: One transaction implementation pair using Phase 43 metadata and 41 mutations.

Change budget and reason: 4–6 production, 1–3 atomicity/grouped-edit checks/docs.

Non-goals: No selection/gizmo/mixed-value UI, independent property system, event-driven mutation or Undo history storage yet.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E grouped transform/material/camera/light/body/constraint and lifecycle changes; validation/application failure and rollback preserve state/resources; parent-child pivot once, commit revision once, notification count/order; no notifications for failed transaction.

Evidence reuse: Phase 14 successful runtime notifications now wrapped/deferred to commit, 43 schema and 47 safe resource replacement. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Perform real grouped property/hierarchy edits transactionally.

Manual acceptance: Review atomicity, pivot behavior and committed-only notifications.

## Observable exit and review

Atomic authoring mutation has clear boundaries and grouped semantics without claiming UI or an event-command architecture.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

