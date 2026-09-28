# PRE_EDITOR Phase 47 — Reimport dependencies, rollback and last-valid notifications

**PROPOSED / NOT_STARTED.** Milestone: E. No implementation or activation authorized by this file.

## Goal and traceability

Make explicit reimport preserve identity and safely replace dependent resources.

Canonical findings: AUT-03. Master section references: E.6, E.13, E.15 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 46 (must actually be approved and published before START).

Additional providing phases/baseline: [11](PHASE_11.md), [13](PHASE_13.md), [46](PHASE_46.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Detect source/dependency change on explicit request, stage CPU/compiled results, validate dependencies and publish safely; preserve AssetId and last-valid resources on failure/cancel; update database revision/task status and notify successful replacement or failure context. E 48 aligns authoring commit semantics.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/src/Mesh/MeshImporter.cpp](../../../GEngine/src/Mesh/MeshImporter.cpp)
- [GEngine/src/Mesh/AsyncMesh.cpp](../../../GEngine/src/Mesh/AsyncMesh.cpp)
- [GEngine/src/Assets/AsyncTexture.cpp](../../../GEngine/src/Assets/AsyncTexture.cpp)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Extend Phase 46 database/import implementation rather than a new pipeline.

Change budget and reason: 4–6 production, 1–3 dependency/rollback/replacement checks/docs.

Non-goals: No automatic filesystem watcher, broad build system, partial dependent publication or discarded old frames.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E changed mesh/texture/shader dependency, success/failure/cancel/obsolete task/project close; semantic IDs stable, failed revision not visible, old frame survives, notification ordering and memory release.

Evidence reuse: A explicit reload and async upload lifetime, Phase 13 stale queue protection and Phase 46 task records. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Reimport an asset used in the active maintained scene while rendering.

Manual acceptance: Review successful change, failure recovery and identity continuity.

## Observable exit and review

Actual reimport either publishes a coherent new revision with notifications or preserves last-valid state and useful error/task evidence.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

