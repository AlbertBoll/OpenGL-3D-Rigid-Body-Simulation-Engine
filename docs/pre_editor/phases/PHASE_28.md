# PRE_EDITOR Phase 28 — Hinge constraints and angular limits

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Deliver required hinge motion and bounded angular/hinge limits for passive joints.

Canonical findings: PHY-04. Master section references: C.9, C.10 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 27 (must actually be approved and published before START).

Additional providing phases/baseline: [26](PHASE_26.md), [27](PHASE_27.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-08. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Hinge anchor/axis representation, shared solver rows/warm start and consistent angle convention; minimum angular limit capability needed by the planned ragdoll; invalid/degenerate axis and limit errors. Share world ownership/filtering/debug axis/limit drawing.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/Constraints/SolverMath.h](../../../GEngine/include/GEngine/Physics/Constraints/SolverMath.h)
- [GEngine/include/GEngine/Physics/PhysicsWorld.cpp](../../../GEngine/include/GEngine/Physics/PhysicsWorld.cpp)
- [GEngine/include/GEngine/Physics/PhysicsSystem.cpp](../../../GEngine/include/GEngine/Physics/PhysicsSystem.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Hinge/limit implementation pair extending the same Phase 26/27 provider.

Change budget and reason: 3–6 production, 1–3 hinge/limit/energy/cost checks/docs.

Non-goals: No motor/drive, animation controller or full arbitrary constraint graph editor.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C-Joints free hinge then lower/upper limits, angle wrapping/axis stability, loaded/moving bodies, warm-start transitions at limit, error/energy/cost and sleep/wake.

Evidence reuse: Phase 26/27 ownership/anchor mechanisms and current core stability. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Demonstrate a hinge/limits assembly visibly separate from the chain and ball/socket cases.

Manual acceptance: Human review single-axis behavior and limit compliance.

## Observable exit and review

Required hinge and angular limits are implemented, observable and qualified for passive ragdoll composition.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

