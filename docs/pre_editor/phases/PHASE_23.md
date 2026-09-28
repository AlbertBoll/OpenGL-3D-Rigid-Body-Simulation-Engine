# PRE_EDITOR Phase 23 — Independent collision shapes including Capsule

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Make collision shape creation independent of render Geometry and support passive ragdoll shapes.

Canonical findings: PHY-02, PHY-05. Master section references: C.5, C.10 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 22 (must actually be approved and published before START).

Additional providing phases/baseline: [22](PHASE_22.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-06, OD-08. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Semantic Box/Sphere/Capsule/ConvexHull descriptions and owned shape lifetime; validate scale/material/mass/inertia and degeneracy; implement Capsule support through existing collision architecture; remove normal Scene box/convex dependence on render Geometry CPU owners.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/Shape.h](../../../GEngine/include/GEngine/Physics/Shape.h)
- [GEngine/include/GEngine/Physics/Shape.cpp](../../../GEngine/include/GEngine/Physics/Shape.cpp)
- [GEngine/include/GEngine/Physics/ShapeConvex.cpp](../../../GEngine/include/GEngine/Physics/ShapeConvex.cpp)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Capsule shape implementation pair and/or narrow shape factory; exact six-file allocation reviewed before START.

Change budget and reason: 4–6 production, 1–3 shape/inertia/contact checks/docs; if both factory and collision integration exceed this, split before implementation.

Non-goals: No visual-mesh identity as collider, general convex decomposition or new Physics backend.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C shapes/contacts, Capsule support/bounds/inertia and degenerate errors; render mesh can change independently; nonuniform collider scale policy, lifetime and stale-reference checks.

Evidence reuse: Approved sphere/box/convex math and typed PhysicsShapeError; Phase 22 ownership. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Create bodies from collision descriptions independently of A visual geometry.

Manual acceptance: Inspect collider/debug overlap and shape behavior.

## Observable exit and review

All required body shapes are semantic and independent, with Capsule sufficient for later passive ragdoll.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

