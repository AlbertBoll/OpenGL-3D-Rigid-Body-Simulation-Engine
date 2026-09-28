# PRE_EDITOR Phase 26 — Managed distance constraints and representative chain

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Exercise a minimal reusable joint foundation with distance constraints and a chain.

Canonical findings: PHY-04. Master section references: C.8, C.9, C.12 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 25 (must actually be approved and published before START).

Additional providing phases/baseline: [18](PHASE_18.md), [21](PHASE_21.md), [22](PHASE_22.md), [23](PHASE_23.md), [24](PHASE_24.md), [25](PHASE_25.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-08. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

World-owned constraint identity/lifetime/body validation; Jacobian/effective-mass form, accumulated impulses, warm start and stabilization within the existing solver/island order; distance settings and chained-body scenario. Deletion/filtering/wake and debug anchors are explicit.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/Constraints/Constraint.h](../../../GEngine/include/GEngine/Physics/Constraints/Constraint.h)
- [GEngine/include/GEngine/Physics/Constraints/SolverMath.h](../../../GEngine/include/GEngine/Physics/Constraints/SolverMath.h)
- [GEngine/include/GEngine/Physics/PhysicsWorld.h](../../../GEngine/include/GEngine/Physics/PhysicsWorld.h)
- [GEngine/include/GEngine/Physics/PhysicsWorld.cpp](../../../GEngine/include/GEngine/Physics/PhysicsWorld.cpp)
- [GEngine/include/GEngine/Physics/PhysicsSystem.cpp](../../../GEngine/include/GEngine/Physics/PhysicsSystem.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: A distance-constraint header/source pair; read-only base locators are not all modification targets.

Change budget and reason: 4–6 production including new pair and RBS; 1–3 constraint correctness/cost checks/docs. Amend/split if world/system glue cannot fit.

Non-goals: No generic constraint language, separate solver, motor or active ragdoll.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C-Joints distance and representative chain; anchor/length drift, warm-start reset, energy/sleep, delete one body/joint, filter interactions and body/joint/pass CPU scaling under OD-08.

Evidence reuse: Core stability gate 21 must be satisfied; existing islands/contact solver and Phase 18 overlays. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: A visible interactive chain in the maintained scene, not an isolated algebra test alone.

Manual acceptance: Human review chain motion/settling and debug anchors; confirm core stability admission.

## Observable exit and review

Managed distance joints and required chain meet correctness, stability and performance gates.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

