# PRE_EDITOR Phase 29 — Passive ragdoll composition and C exit

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Compose ordinary bodies, shapes, joints and limits into a basic passive ragdoll.

Canonical findings: PHY-05, PHY-04, PHY-03, DBG-01. Master section references: C.10, C.11, C.12, 14 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 28 (must actually be approved and published before START).

Additional providing phases/baseline: [18](PHASE_18.md), [21](PHASE_21.md), [23](PHASE_23.md), [24](PHASE_24.md), [25](PHASE_25.md), [26](PHASE_26.md), [27](PHASE_27.md), [28](PHASE_28.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-06, OD-07, OD-08. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Body hierarchy and bone-mapping description; capsule/box/sphere parts, parent-child joints, limits and collision filters; deterministic create/rollback/destroy, sleep/wake and debug body/anchor/axis/limit/island display. Use the same solver and typed APIs.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/PhysicsWorld.h](../../../GEngine/include/GEngine/Physics/PhysicsWorld.h)
- [GEngine/include/GEngine/Physics/PhysicsWorld.cpp](../../../GEngine/include/GEngine/Physics/PhysicsWorld.cpp)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: A small ragdoll-description/composition provider pair, no separate simulation system.

Change budget and reason: 4–6 production, 1–3 composition/settling/cost checks/docs.

Non-goals: No active ragdoll, animation blending, IK, muscles, get-up, motor or skeleton animation system.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C-Joints drop/settle/wake ragdoll, filters/limits/bone mapping and partial-create cleanup; retained lattice, box stack, chain and hinge evidence plus changed interactions; CPU/joint/body scaling and semantic debug coverage.

Evidence reuse: Accepted 21 stability, 22–28 body/shape/constraint/Transform/API evidence; C exits require all rather than a launch alone. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Required passive ragdoll drop and stable settling through the actual maintained app.

Manual acceptance: Owner accepts C and visible chain/hinge/ragdoll/debug behavior with quantitative evidence.

## Observable exit and review

C correctness, settling, chain, hinge/limits, passive ragdoll, API, Transform/rendering, debug and CPU gates pass or carry explicit permitted owner bounds.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

