# PRE_EDITOR Phase 27 — Ball-and-socket constraints

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Extend the exercised joint provider with point/ball-and-socket behavior.

Canonical findings: PHY-04. Master section references: C.8, C.9 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 26 (must actually be approved and published before START).

Additional providing phases/baseline: [26](PHASE_26.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-08. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Three-axis anchor constraint using shared effective-mass/warm-start/stabilization and world ownership; integrate filtering/wake and debug anchors through existing Phase 26 interfaces.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/Constraints/SolverMath.h](../../../GEngine/include/GEngine/Physics/Constraints/SolverMath.h)
- [GEngine/include/GEngine/Physics/PhysicsSystem.cpp](../../../GEngine/include/GEngine/Physics/PhysicsSystem.cpp)
- [GEngine/include/GEngine/Physics/PhysicsWorld.cpp](../../../GEngine/include/GEngine/Physics/PhysicsWorld.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: One ball/socket constraint implementation pair.

Change budget and reason: 3–6 production, 1–2 focused anchor/energy/cost checks/docs.

Non-goals: No motors, angular limit bundle or duplicated body/solver architecture.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C-Joints anchored multi-axis swing and loaded pair; translation locking/free rotation, joint error/energy/convergence/CPU, teardown and stale body errors.

Evidence reuse: Phase 26 lifetime/chain and Phase 21 stability; repeat affected solver interactions only. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Manipulate and settle an anchored ball/socket assembly using semantic API.

Manual acceptance: Review anchor locking, free rotation and debug alignment.

## Observable exit and review

Ball/socket behavior passes admitted error/stability/cost criteria with no distance-chain regression.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

