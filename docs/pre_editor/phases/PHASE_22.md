# PRE_EDITOR Phase 22 — Typed Physics world and body API ownership

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Offer ordinary body operations without solver internals or ambiguous raw ownership.

Canonical findings: PHY-02. Master section references: C.5, C.6 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 21 (must actually be approved and published before START).

Additional providing phases/baseline: [14](PHASE_14.md), [21](PHASE_21.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-06. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

World-owned typed body handles using the shared generation model; static/kinematic/dynamic behavior, create/destroy, mass/inertia/material, velocity/forces/impulses and sleep APIs with typed failures. Keep internal pointers private and teardown deterministic.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/PhysicsWorld.h](../../../GEngine/include/GEngine/Physics/PhysicsWorld.h)
- [GEngine/include/GEngine/Physics/PhysicsWorld.cpp](../../../GEngine/include/GEngine/Physics/PhysicsWorld.cpp)
- [GEngine/include/GEngine/Physics/PhysicsBody.h](../../../GEngine/include/GEngine/Physics/PhysicsBody.h)
- [GEngine/include/GEngine/Physics/PhysicsBody.cpp](../../../GEngine/include/GEngine/Physics/PhysicsBody.cpp)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–3 lifetime/body-semantics checks/docs.

Non-goals: No contact-algorithm rewrite, second identity model, persistent body IDs or Ragdoll solver.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C body create/edit/delete and wrong-world/stale handle; static immobility, prescribed kinematic motion and dynamic simulation; invalid mass/material/force inputs, world shutdown.

Evidence reuse: Approved Physics slot/identity infrastructure, Phase 14 Transform intent and Phase 21 settled behavior. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Migrate maintained body construction, forces/impulses and deletion.

Manual acceptance: Review public API simplicity and state ownership.

## Observable exit and review

Normal RBS body usage requires semantic descriptors/handles and typed results; no raw solver fields/pointers.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

