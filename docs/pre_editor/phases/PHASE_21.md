# PRE_EDITOR Phase 21 — Convergence, sleep and fixed-step qualification

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Decide the core stability gate before constraint/ragdoll expansion.

Canonical findings: PHY-01, PHY-03. Master section references: C.2, C.3, C.12 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 20 (must actually be approved and published before START).

Additional providing phases/baseline: [20](PHASE_20.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-06, OD-07. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Qualify corrected core with admitted 60/120 Hz, active/sleeping and selected pass sensitivity; measure convergence and sleep/wake churn/cost. Only a separately admitted minimal sleep/convergence defect may be fixed; do not blend speculative optimization into qualification.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/PhysicsSystem.h](../../../GEngine/include/GEngine/Physics/PhysicsSystem.h)
- [GEngine/include/GEngine/Physics/PhysicsSystem.cpp](../../../GEngine/include/GEngine/Physics/PhysicsSystem.cpp)
- [GEngine/include/GEngine/Physics/PhysicsProfile.cpp](../../../GEngine/include/GEngine/Physics/PhysicsProfile.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 0–3 production for required observation/integration, 1–3 protocol/evidence files. A new remedy needs an explicit scope amendment.

Non-goals: No blind 1->2 default change from the dirty Physics worktree, threshold-only cure or new constraints.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C-Rest admitted 120 s lattice/tail and 20 s stack where parity holds; ground/friction/spin/low-speed/multi-contact sensitivity; report all quantitative settling and CPU/scaling metrics and insufficient evidence.

Evidence reuse: Phase 19/20 causal/correctness evidence, Phase 39 input-specific historic bounds only when parity proven. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Sphere lattice settling, box stack and sleep/wake exercised in the actual app.

Manual acceptance: Explicit owner decision on core stability and residual bounds before joints.

## Observable exit and review

Core rest stability resolved, or owner explicitly accepts quantified bounds appropriate to chains/ragdoll. Otherwise 26–29 remain blocked.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

