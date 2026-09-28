# PRE_EDITOR Phase 07 — Parametric curved geometry templates

**PROPOSED / NOT_STARTED.** Milestone: A. No implementation or activation authorized by this file.

## Goal and traceability

Extend the same CPU provider with Sphere, Cylinder, Cone and Capsule.

Canonical findings: API-02. Master section references: A.3, A.5 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 06 (must actually be approved and published before START).

Additional providing phases/baseline: [06](PHASE_06.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-03. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Bound segment/ring and dimension parameters; reuse canonical cache keys and normal/tangent/bounds rules. Capsule geometry is visual only; C later owns independent collision shape.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Mesh/MeshAsset.h](../../../GEngine/include/GEngine/Mesh/MeshAsset.h)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Extend the Phase 06 GeometryTemplates header/source; no second provider.

Change budget and reason: 3–5 production, 1–2 focused parameter/bounds tests/docs.

Non-goals: No collision implementation, imported topology editing or arbitrary procedural system.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-A parameter variants; typed invalid/degenerate/extreme input, seams/poles/caps, winding/UV/TBN, deterministic identity and bounded generation memory.

Evidence reuse: Phase 06 canonical sharing/publication and baseline GPU upload. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Create and display each parametric family via the same simple API.

Manual acceptance: Inspect poles/seams/caps and low/high segment behavior.

## Observable exit and review

All required primitive families have bounded semantic generation and correct maintained rendering.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

