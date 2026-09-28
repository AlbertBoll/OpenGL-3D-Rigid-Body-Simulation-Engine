# PRE_EDITOR Phase 06 — CPU simple geometry templates and canonical sharing

**PROPOSED / NOT_STARTED.** Milestone: A. No implementation or activation authorized by this file.

## Goal and traceability

Provide Cube, Plane, Quad and Grid templates through the existing mesh architecture.

Canonical findings: API-02. Master section references: A.3, A.5 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 05 (must actually be approved and published before START).

Additional providing phases/baseline: [04](PHASE_04.md), [05](PHASE_05.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-03. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Create CPU-only validated mesh descriptions with canonical parameter keys, bounds/normals/tangents and shared MeshHandle publication. Keep ShapeManager frozen users; modern creation must not instantiate legacy Geometry/GPU owners.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Mesh/MeshAsset.h](../../../GEngine/include/GEngine/Mesh/MeshAsset.h)
- [GEngine/src/Mesh/MeshAsset.cpp](../../../GEngine/src/Mesh/MeshAsset.cpp)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: One GeometryTemplates semantic header/source pair, exact names selected before START.

Change budget and reason: 4–6 production including new pair and RBS; 1–2 focused geometry/identity tests/docs.

Non-goals: No mesh editor, arbitrary topology editing, GPU readback or new mesh registry.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-A simple shapes; invalid dimensions/overflow/generation bounds; winding, topology counts, AABB, normal/tangent orientation, canonical key reuse and typed upload errors.

Evidence reuse: MeshAsset validation/upload and S06 generations/leases; existing approved rendering for unchanged material. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Use Cube/Plane/Quad/Grid directly in maintained scene construction.

Manual acceptance: Review silhouette/winding/UV and ordinary caller API.

## Observable exit and review

Simple CPU template requests create correct rendered shapes and repeated equivalent requests share handles.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

