# PRE_EDITOR Phase 05 — Resource ownership and renderer-private frame preparation

**PROPOSED / NOT_STARTED.** Milestone: A. No implementation or activation authorized by this file.

## Goal and traceability

Remove normal caller dependence on manager/registry/publication chains without a duplicate resource architecture.

Canonical findings: API-01, API-03. Master section references: 6, A.1, 18.1 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 04 (must actually be approved and published before START).

Additional providing phases/baseline: [04](PHASE_04.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-03. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Narrow SceneRenderResources and root service access; separate CPU request intent from context publication and frame lease preparation. Retain approved AssetsManager texture/sampler ownership and legacy Shader/Shape users. Choose clear names distinguishing resource owner from immutable frame resources.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Renderer/SceneRenderResources.h](../../../GEngine/include/GEngine/Renderer/SceneRenderResources.h)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [GEngine/include/GEngine/Managers/AssetsManager.h](../../../GEngine/include/GEngine/Managers/AssetsManager.h)
- [GEngine/src/Managers/AssetsManager.cpp](../../../GEngine/src/Managers/AssetsManager.cpp)
- [GEngine/include/GEngine/Core/GEngine.h](../../../GEngine/include/GEngine/Core/GEngine.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–3 focused boundary/lifetime checks/docs. Internals stay on existing registries.

Non-goals: No AssetDatabase, new TextureManager, global facade, legacy frontend rewrite or async performance fix.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0; RBS-A changes and replacement through narrow operations; old-frame retention/publication exclusion, teardown and error rollback. Inspect dependency graph and affected other-app interfaces.

Evidence reuse: S01/S02/S05–S07 context/retirement infrastructure; unchanged typed registry checks. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Replace the inspected Acquire/copy/Replace chain with semantic operations.

Manual acceptance: Review both caller simplicity and internal dependency direction.

## Observable exit and review

Normal material/mesh authoring omits frame scopes/native/backend knowledge; explicit expensive operations and renderer-owned preparation remain reviewable.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

