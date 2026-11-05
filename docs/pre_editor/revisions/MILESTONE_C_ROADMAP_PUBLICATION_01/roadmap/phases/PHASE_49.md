# PRE_EDITOR Phase 49 — AssetDatabase, import settings and observable tasks

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: E. No implementation or phase activation authorized by this file.

## Goal and traceability

Own persistent asset provenance and task state independently from GPU registries.

Canonical findings: AUT-02, AUT-03. Master section references: E.5, E.6, E.15 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 48 (must actually be approved and published before START).

Additional providing phases/baseline: [12](/docs/pre_editor/phases/PHASE_12.md), [45](PHASE_45.md), [46](PHASE_46.md), [47](PHASE_47.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

AssetId/source-relative path/type/import settings/dependencies/source-derived relationship/revision; observable Pending/Running/Succeeded/Failed/Cancelled task states projected over existing async services, including shader/import operations. Explicit request/detection only; preserve CPU worker and owner publication boundary.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Core/RuntimeAssets.h](/GEngine/include/GEngine/Core/RuntimeAssets.h)
- [GEngine/src/Core/RuntimeAssets.cpp](/GEngine/src/Core/RuntimeAssets.cpp)
- [GEngine/include/GEngine/Assets/AsyncUploadQueue.h](/GEngine/include/GEngine/Assets/AsyncUploadQueue.h)
- [GEngine/src/Mesh/MeshImporter.cpp](/GEngine/src/Mesh/MeshImporter.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Small AssetDatabase/import-record provider pair, using shared metadata; exact allowlist reviewed.

Change budget and reason: 4–6 production, 1–3 database/task/path checks/docs.

Non-goals: No second runtime asset registry/job scheduler, automatic watcher, thumbnail system or Content Browser UI.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E register/import source, persistent identity and derived paths, settings changes/dependencies, pending/running/completion/failure/cancel states, stale completion after scene/project close; native importer details remain private.

Evidence reuse: Approved async Texture/Mesh pipeline, Phase 12 cancellation/last-valid behavior, Phase 45 identity and 46 diagnostics. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Import and observe an actual asset through the maintained project scene.

Manual acceptance: Review task transitions, identity and error visibility.

## Observable exit and review

AssetDatabase describes authored provenance while runtime registry owns GPU lifetime; task state is observable without backend knowledge.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_46.md`, SHA256 `01d0ccaeee70d5181b3b274f3c99fe8c364b57b7ae9ebc276de20437876ec574`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
