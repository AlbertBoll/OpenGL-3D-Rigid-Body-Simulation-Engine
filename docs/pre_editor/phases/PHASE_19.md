# PRE_EDITOR Phase 19 — Resting-contact and impact diagnosis in RBS

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Establish a current, bounded causal account of lattice jitter/settling and impact preservation.

Canonical findings: PHY-01. Master section references: C.1, C.2, C.3, C.12, 18.2 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 18 (must actually be approved and published before START).

Additional providing phases/baseline: [18](PHASE_18.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-06. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Admit exact historical input parity in RBS; instrument restitution/threshold, accumulated normal/friction impulses, warm start/manifold persistence, contact churn, bias, passes, fixed step, sleep/islands and floating residuals. Separate energy injection, insufficient convergence, contact instability and policy failure.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/PhysicsSystem.cpp](../../../GEngine/include/GEngine/Physics/PhysicsSystem.cpp)
- [GEngine/include/GEngine/Physics/Manifold.cpp](../../../GEngine/include/GEngine/Physics/Manifold.cpp)
- [GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp](../../../GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp)
- [GEngine/include/GEngine/Physics/PhysicsProfile.h](../../../GEngine/include/GEngine/Physics/PhysicsProfile.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 2–5 production for bounded observation/RBS scenario, 1–3 evidence/protocol files. No solver remedy bundled.

Non-goals: No sleep-threshold masking, automatic extra passes, experimental Physics merge or standalone probe execution.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C-Rest initial admitted 15 s lattice plus named isolated ground/impact/spin/stack cases; record velocity/drift/penetration/impulses/energy/churn/active bodies and instrumentation overhead. Distinguish earlier TOI impact response from persistent inelastic response.

Evidence reuse: Phase 39 exact object list/hash and diagnostics; rejected Phase 40 attribution only, never rejected policy/code as approved. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Current authoritative Physics executing the exact admitted lattice and focused scenarios.

Manual acceptance: Review debug overlays and quantitative model; residual impulse is not assumed root cause.

## Observable exit and review

Reviewed causal ledger states measured versus hypothetical mechanisms and admits exact bounded Phase 20 correction/acceptance criteria. If cause remains insufficient, stop dependent remedy/joints.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

