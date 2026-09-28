# PRE_EDITOR Phase 42 — Project roots and persistent identity

**PROPOSED / NOT_STARTED.** Milestone: E. No implementation or activation authorized by this file.

## Goal and traceability

Create explicit project-relative identity distinct from runtime resource/entity handles.

Canonical findings: AUT-02. Master section references: E.2, E.3 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 41 (must actually be approved and published before START).

Additional providing phases/baseline: [41](PHASE_41.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Project ID/root/asset and Scene roots/derived-cache/settings/start Scene; AssetId, EntityGuid and SceneId with duplication/restoration policy. Separate project settings from editor/user preferences; path/URI resolution rejects inappropriate absolute or out-of-root references.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Core/RuntimeAssets.h](../../../GEngine/include/GEngine/Core/RuntimeAssets.h)
- [GEngine/src/Core/RuntimeAssets.cpp](../../../GEngine/src/Core/RuntimeAssets.cpp)
- [GEngine/include/GEngine/Core/UUID.h](../../../GEngine/include/GEngine/Core/UUID.h)
- [GEngine/src/Core/UUID.cpp](../../../GEngine/src/Core/UUID.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: A narrow Project/persistent-reference provider pair; select required existing files within budget.

Change budget and reason: 4–6 production, 1–3 identity/path/project checks/docs.

Non-goals: No GPU registry replacement, database/import bundle, project browser UI or development-machine path persistence.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E create/open/move/reopen project, default Scene resolution, duplicate identity rules, wrong-project/missing/invalid URI diagnostics; runtime generations remain separate.

Evidence reuse: Existing RuntimeAssets root and UUID facilities after semantics audit; Phase 41 lifecycle. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Open an actual controlled project root and create persistent Scene/Entity identities.

Manual acceptance: Review path ownership and identity schema.

## Observable exit and review

Project and persistent-reference policy are concrete enough for metadata/serialization without serializing native/runtime handles.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

