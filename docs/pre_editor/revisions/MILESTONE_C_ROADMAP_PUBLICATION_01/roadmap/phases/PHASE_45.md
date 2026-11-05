# PRE_EDITOR Phase 45 — Project roots and persistent identity

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: E. No implementation or phase activation authorized by this file.

## Goal and traceability

Create explicit project-relative identity distinct from runtime resource/entity handles.

Canonical findings: AUT-02. Master section references: E.2, E.3 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 44 (must actually be approved and published before START).

Additional providing phases/baseline: [44](PHASE_44.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Project ID/root/asset and Scene roots/derived-cache/settings/start Scene; AssetId, EntityGuid and SceneId with duplication/restoration policy. Separate project settings from editor/user preferences; path/URI resolution rejects inappropriate absolute or out-of-root references.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Core/RuntimeAssets.h](/GEngine/include/GEngine/Core/RuntimeAssets.h)
- [GEngine/src/Core/RuntimeAssets.cpp](/GEngine/src/Core/RuntimeAssets.cpp)
- [GEngine/include/GEngine/Core/UUID.h](/GEngine/include/GEngine/Core/UUID.h)
- [GEngine/src/Core/UUID.cpp](/GEngine/src/Core/UUID.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: A narrow Project/persistent-reference provider pair; select required existing files within budget.

Change budget and reason: 4–6 production, 1–3 identity/path/project checks/docs.

Non-goals: No GPU registry replacement, database/import bundle, project browser UI or development-machine path persistence.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E create/open/move/reopen project, default Scene resolution, duplicate identity rules, wrong-project/missing/invalid URI diagnostics; runtime generations remain separate.

Evidence reuse: Existing RuntimeAssets root and UUID facilities after semantics audit; Phase 44 lifecycle. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Open an actual controlled project root and create persistent Scene/Entity identities.

Manual acceptance: Review path ownership and identity schema.

## Observable exit and review

Project and persistent-reference policy are concrete enough for metadata/serialization without serializing native/runtime handles.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_42.md`, SHA256 `566974a650e7d35ffb79fb6f5cb12c13c3413f20c406d8d355e95fcbe552f5f3`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
