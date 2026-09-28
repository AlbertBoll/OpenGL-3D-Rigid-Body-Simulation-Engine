# PRE_EDITOR Phase 08 — Explicit geometry copy, MakeUnique and dimension/Bake semantics

**PROPOSED / NOT_STARTED.** Milestone: A. No implementation or activation authorized by this file.

## Goal and traceability

Make sharing and object dimensions explicit without confusing scale, regeneration and mesh editing.

Canonical findings: API-02. Master section references: A.3, A.5, 18.1 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 07 (must actually be approved and published before START).

Additional providing phases/baseline: [04](PHASE_04.md), [06](PHASE_06.md), [07](PHASE_07.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-03. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Retain CPU source provenance for explicit copy/MakeUnique; new handle only on request. Distinguish Transform/object scale, primitive regeneration, mesh Apply/Bake and explicit collider changes. For Transform edits preserve the existing supported SetScale-to-OnScaleChanged-to-Physics path when a live runtime bridge exists; do not substitute a direct Scale write that bypasses it. Use the current shape-specific policy and initialization conditions documented in [S22](../HANDOFF_AUDIT.md#scale-mutation-and-collider-behavior-s22). Mesh regeneration/Bake does not by itself invoke that bridge; a Bake that resets Transform through SetScale must account for its callback explicitly. Return updated bounds/revisions; B 13–14 later migrate lifetime/mutation semantics and C 24 owns integrated collider behavior.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Mesh/MeshAsset.h](../../../GEngine/include/GEngine/Mesh/MeshAsset.h)
- [GEngine/src/Mesh/MeshAsset.cpp](../../../GEngine/src/Mesh/MeshAsset.cpp)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [GEngine/include/GEngine/Component/Component.h](../../../GEngine/include/GEngine/Component/Component.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Extend Phase 06 template/authoring provider only where needed; exact allowlist within budget.

Change budget and reason: 4–6 production, 1–3 focused sharing/transform/bounds checks/docs.

Non-goals: No topology editor, persistent authoring transactions, new implicit mesh-regeneration/Bake-to-collider coupling, removal of existing supported setter-driven collider scaling, or silent mutation of shared mesh.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-A duplicate/share/MakeUnique; scale versus regenerated dimensions versus baked vertices; nonuniform normal/tangent transformation, winding and admitted negative/zero-scale policy, failed copy rollback and old-frame bounds. Focused future preservation cases distinguish changed/unchanged SetScale with and without a live bridge from direct-field writes, cover existing shape-specific scaling/base-source behavior, and account explicitly for any Bake-related Transform reset. This revision runs none of those cases.

Evidence reuse: Existing local TRS/render extraction and Phase 04 sharing; Phase 06/07 templates. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Perform required A sharing/duplicate/MakeUnique/resize sequence through semantic calls.

Manual acceptance: Review API behavior and intended geometry/bounds changes.

## Observable exit and review

RBS demonstrates shared changes remain shared, MakeUnique isolates, and resize/stretch/Bake have distinct observable effects.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

