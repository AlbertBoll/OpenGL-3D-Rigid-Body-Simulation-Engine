# PRE_EDITOR Phase 45 — Constraint and ragdoll persistence extensions

**PROPOSED / NOT_STARTED.** Milestone: E. No implementation or activation authorized by this file.

## Goal and traceability

Persist required joints/ragdoll descriptions using the same versioned metadata.

Canonical findings: AUT-03, PHY-04, PHY-05. Master section references: E.4, C.9, C.10 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 44 (must actually be approved and published before START).

Additional providing phases/baseline: [26](PHASE_26.md), [27](PHASE_27.md), [28](PHASE_28.md), [29](PHASE_29.md), [44](PHASE_44.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Serialize body references, anchors/axes, distance/ball/hinge/limits, filters and passive ragdoll hierarchy/bone mapping; restore after body resolution with deterministic validation and rollback. No solver impulses/runtime pointers persist unless an explicitly separate runtime snapshot contract is approved.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/PhysicsWorld.h](../../../GEngine/include/GEngine/Physics/PhysicsWorld.h)
- [GEngine/include/GEngine/Physics/Constraints/Constraint.h](../../../GEngine/include/GEngine/Physics/Constraints/Constraint.h)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Extend Phase 43/44 metadata/serialization adapters and Phase 29 description only.

Change budget and reason: 3–5 production, 1–3 missing-reference/round-trip checks/docs.

Non-goals: No independent Physics serializer, animation state, active ragdoll or runtime solver snapshot format.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E round-trip chain, hinge/limits and ragdoll descriptions; stale/missing/wrong-body references, invalid limits, unknown versions and partial-load failure; then RBS-C brief representative behavior under reused accepted criteria.

Evidence reuse: C joint/ragdoll behavior and Phase 44 transaction-free load replacement; do not rerun all Physics performance cases without changed behavior. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Reload the maintained chain/hinge/ragdoll and verify semantic/debug identity.

Manual acceptance: Review restored assemblies and missing-reference diagnostics.

## Observable exit and review

Persistent restored joints/ragdoll preserve authored constraints/filtering/mapping and deterministic typed recovery.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

