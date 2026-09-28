# PRE_EDITOR Phase 20 — Bounded resting-contact and impact ownership correction

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Correct the demonstrated resting-contact mechanism while retaining valid impact restitution.

Canonical findings: PHY-01. Master section references: C.2, C.3, C.12 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 19 (must actually be approved and published before START).

Additional providing phases/baseline: [19](PHASE_19.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-06, OD-07. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Only the mechanism admitted by Phase 19: reconcile persistent normal solve, warm starting and impact/restitution ownership in the existing solver. Exact model, touched paths and numerical tests must be reviewed before START; if diagnosis proves no remedy necessary, amend the plan instead of fabricating a fix.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/PhysicsSystem.cpp](../../../GEngine/include/GEngine/Physics/PhysicsSystem.cpp)
- [GEngine/include/GEngine/Physics/Manifold.cpp](../../../GEngine/include/GEngine/Physics/Manifold.cpp)
- [GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp](../../../GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 2–4 production, 1–3 focused contact/impact regressions/docs. One mechanism only.

Non-goals: No broad solver rewrite, arbitrary damping, sleep-threshold mask, forced iteration increase or new joint.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 matched RBS-C-Rest candidate/control: lattice, low-speed and genuine impacts, frictional/multi-contact/spin; energy/restitution/penetration/settling and cost gates from OD-06/07, fail on impact suppression.

Evidence reuse: Phase 19 causal observations, Phase 39/40 diagnostic caution and unaffected contact math tests. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Observe corrected current lattice and representative real impacts through maintained transform/render path.

Manual acceptance: Owner reviews causal correction and any quantified residual limitation.

## Observable exit and review

The admitted mechanism is corrected with qualified evidence and preserved impact behavior; any remaining bound is explicit for Phase 21.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

