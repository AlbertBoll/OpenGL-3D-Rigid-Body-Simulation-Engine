# PRE_EDITOR Phase 09 — Typed standard material templates

**PROPOSED / NOT_STARTED.** Milestone: A. No implementation or activation authorized by this file.

## Goal and traceability

Provide one typed Standard/Unlit/Masked/Transparent/Debug material model for ordinary callers.

Canonical findings: API-04, API-03. Master section references: A.4, A.5, 18.1 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 08 (must actually be approved and published before START).

Additional providing phases/baseline: [04](PHASE_04.md), [05](PHASE_05.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-03. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Define semantic BaseColor, Metallic, Roughness, Normal, AO, Emissive, Alpha/Cutoff and texture/sampler roles; map to current approved material/template/pipeline behavior with explicit temporary limitations. Provide the smallest fixed built-in template/program cache needed here, using existing registry identities; Phase 10 extends this to custom descriptions/reflection/variants. Keep the advanced API; no manual uniforms/pipelines/publication in RBS. D 30–31 completes PBR on this same schema.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Material/MaterialTemplate.h](../../../GEngine/include/GEngine/Material/MaterialTemplate.h)
- [GEngine/include/GEngine/Material/MaterialInstance.h](../../../GEngine/include/GEngine/Material/MaterialInstance.h)
- [GEngine/src/Material/MaterialTemplate.cpp](../../../GEngine/src/Material/MaterialTemplate.cpp)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: A small standard material descriptor/provider if existing headers cannot host the semantic contract.

Change budget and reason: 4–6 production, 1–3 schema/alpha/binding checks/docs. No D shading rewrite.

Non-goals: No second Standard material system, complete new BRDF/HDR pipeline, AssetDatabase or per-instance shader compilation.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-A templates/parameter edit; semantic ranges/type errors, texture defaults, deterministic binding, alpha/pipeline agreement, identical shader description reuses program; count compile calls.

Evidence reuse: Approved MaterialTemplate/Instance binding and alpha coverage; Phase 05 publication operations. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Replace the inspected string-based standard material lambda and updates.

Manual acceptance: Review five template appearances and consumer/internal simplicity.

## Observable exit and review

Ordinary material creation/editing avoids shader names, uniform locations, native programs and pipeline setup; limitations are explicit until D.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.


