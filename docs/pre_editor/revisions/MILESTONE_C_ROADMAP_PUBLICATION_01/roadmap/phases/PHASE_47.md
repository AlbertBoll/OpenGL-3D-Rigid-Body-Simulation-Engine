# PRE_EDITOR Phase 47 — Scene/component persistence and semantic round trip

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: E. No implementation or phase activation authorized by this file.

## Goal and traceability

Persist normal Scene/component authoring state through the shared schema.

Canonical findings: AUT-03. Master section references: E.4, E.7, E.11 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 46 (must actually be approved and published before START).

Additional providing phases/baseline: [44](PHASE_44.md), [45](PHASE_45.md), [46](PHASE_46.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Versioned scene/entity/hierarchy/asset reference, geometry/material parameters, camera, all three lights/shadows/environment/HDR/Bloom/exposure and Physics body/collider data; validate complete load before replacing the active Scene; unknown/missing data and typed recovery rules. Constraint/ragdoll extensions belong to 48.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Scene/_Scene.h](/GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](/GEngine/src/Scene/_Scene.cpp)
- [GEngine/include/GEngine/Component/Component.h](/GEngine/include/GEngine/Component/Component.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Scene serialization implementation using Phase 46 metadata; no new third-party serializer dependency without explicit approval.

Change budget and reason: 3–6 production, 1–3 semantic round-trip/malformed-input checks/docs. Schema-driven adapters; split before START if bounded allocation cannot cover the admitted component set.

Non-goals: No runtime pointers/GPU names saved, old state destroyed before load validation, editor layout persistence or joint schema hidden in this phase.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E save/unload/load, semantic equality including hierarchy/references/lighting and physical values; truncated/wrong-version/unknown/missing data leaves active state valid; project relocation and stable IDs.

Evidence reuse: Phase 46 metadata, 45 identity, 44 lifecycle and stable D contracts; existing resource loading/publication. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Save and reload the actual maintained authoring scene and render it.

Manual acceptance: Review persisted schema, missing-data diagnostics and restored appearance/state.

## Observable exit and review

Normal Scene authoring round-trips semantically with explicit migration/error policy and no machine-specific runtime identity.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_44.md`, SHA256 `57104321a5403c8658e1180fb6bc0109c8e921ef25b24e20fe680342546eaa21`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
