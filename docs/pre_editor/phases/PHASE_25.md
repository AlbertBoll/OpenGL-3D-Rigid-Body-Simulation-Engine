# PRE_EDITOR Phase 25 — Physics CPU and scaling acceptance

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Establish qualified Physics performance and cost limits before adding joints.

Canonical findings: PHY-03. Master section references: C.4, C.12, 18.2 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 24 (must actually be approved and published before START).

Additional providing phases/baseline: [21](PHASE_21.md), [24](PHASE_24.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-07. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Admit a small RBS workload set for active/sleeping bodies, contact count and selected pass counts; use PhysicsProfile for broad/narrow/contact/island/solver timings, transient allocations and repeated work. One diagnostic/qualification phase, not assumed optimization.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/PhysicsProfile.h](../../../GEngine/include/GEngine/Physics/PhysicsProfile.h)
- [GEngine/include/GEngine/Physics/PhysicsProfile.cpp](../../../GEngine/include/GEngine/Physics/PhysicsProfile.cpp)
- [GEngine/include/GEngine/Physics/PhysicsSystem.cpp](../../../GEngine/include/GEngine/Physics/PhysicsSystem.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 0–3 production for necessary instrumentation/RBS selection, 1–3 protocol/evidence files.

Non-goals: No standalone PhysicsBenchmark app, automatic full matrix, changed thresholds after data or unproven optimization.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C-Rest Release MT matched cost, median/p95/relevant tails, body/contact/pass scaling and sleep savings/overhead; explicit observer cost, allocation bytes and total run cap.

Evidence reuse: Existing PhysicsProfile and accepted current correctness; old MTd diagnostic numbers remain provenance only. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Physics performance is measured through the maintained app with unrelated render cost separated.

Manual acceptance: Review workload relevance, cost ledger and any accepted limits.

## Observable exit and review

Qualified measured core cost fits predeclared OD-07 or a concrete owner gate decision; later joints have an admitted comparison baseline.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

