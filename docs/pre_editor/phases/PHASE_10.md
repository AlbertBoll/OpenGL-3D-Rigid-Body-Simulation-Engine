# PRE_EDITOR Phase 10 — Shader descriptions, reflection and bounded variants

**PROPOSED / NOT_STARTED.** Milestone: A. No implementation or activation authorized by this file.

## Goal and traceability

Unify modern shader identity and schema validation behind bounded explicit creation.

Canonical findings: API-03. Master section references: A.2, 6.3 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 09 (must actually be approved and published before START).

Additional providing phases/baseline: [05](PHASE_05.md), [09](PHASE_09.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-03. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Separate source/parameter/semantic description and finite variant key from context compilation; validate reflection against shared material schema. Consolidate modern program reuse without routing new APIs back through legacy borrowed ShaderManager pointers.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Assets/Shaders/Shader.h](../../../GEngine/include/GEngine/Assets/Shaders/Shader.h)
- [GEngine/src/Assets/ShaderCompilation.cpp](../../../GEngine/src/Assets/ShaderCompilation.cpp)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [GEngine/include/GEngine/Material/MaterialTemplate.h](../../../GEngine/include/GEngine/Material/MaterialTemplate.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: One shader-description/cache implementation slot only if needed within file budget.

Change budget and reason: 4–6 production, 1–3 focused schema/variant/cache tests/docs.

Non-goals: No auto watching, unbounded permutations, global shader god service or new compiler/toolchain.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-A custom material; valid and missing/mismatched semantic parameters, finite variant admission, deterministic cache identity, compile-count evidence and typed source diagnostics.

Evidence reuse: Existing private ShaderCompilation reflection and typed errors; Phase 09 schema and Phase 05 owners. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Author one controlled custom shader/material and exercise typed schema failure.

Manual acceptance: Inspect diagnostics and ordinary/advanced boundary.

## Observable exit and review

Custom and standard paths use validated descriptions with bounded variant count and no hidden repeated compilation.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

