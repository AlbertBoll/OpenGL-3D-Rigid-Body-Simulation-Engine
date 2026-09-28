# PRE_EDITOR Phase 44 — Scene/component persistence and semantic round trip

**PROPOSED / NOT_STARTED.** Milestone: E. No implementation or activation authorized by this file.

## Goal and traceability

Persist normal Scene/component authoring state through the shared schema.

Canonical findings: AUT-03. Master section references: E.4, E.7, E.11 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 43 (must actually be approved and published before START).

Additional providing phases/baseline: [41](PHASE_41.md), [42](PHASE_42.md), [43](PHASE_43.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Versioned scene/entity/hierarchy/asset reference, geometry/material parameters, camera, all three lights/shadows/environment/HDR/Bloom/exposure and Physics body/collider data; validate complete load before replacing the active Scene; unknown/missing data and typed recovery rules. Constraint/ragdoll extensions belong to 45.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Scene/_Scene.h](../../../GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [GEngine/include/GEngine/Component/Component.h](../../../GEngine/include/GEngine/Component/Component.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Scene serialization implementation using Phase 43 metadata; no new third-party serializer dependency without explicit approval.

Change budget and reason: 3–6 production, 1–3 semantic round-trip/malformed-input checks/docs. Schema-driven adapters; split before START if bounded allocation cannot cover the admitted component set.

Non-goals: No runtime pointers/GPU names saved, old state destroyed before load validation, editor layout persistence or joint schema hidden in this phase.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E save/unload/load, semantic equality including hierarchy/references/lighting and physical values; truncated/wrong-version/unknown/missing data leaves active state valid; project relocation and stable IDs.

Evidence reuse: Phase 43 metadata, 42 identity, 41 lifecycle and stable D contracts; existing resource loading/publication. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Save and reload the actual maintained authoring scene and render it.

Manual acceptance: Review persisted schema, missing-data diagnostics and restored appearance/state.

## Observable exit and review

Normal Scene authoring round-trips semantically with explicit migration/error policy and no machine-specific runtime identity.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

