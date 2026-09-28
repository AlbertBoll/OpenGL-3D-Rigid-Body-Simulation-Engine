# PRE_EDITOR Phase 11 — Explicit shader/material reload and atomic replacement

**PROPOSED / NOT_STARTED.** Milestone: A. No implementation or activation authorized by this file.

## Goal and traceability

Make explicit reload replace a valid dependent shader/material graph safely.

Canonical findings: API-03. Master section references: A.2 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 10 (must actually be approved and published before START).

Additional providing phases/baseline: [05](PHASE_05.md), [09](PHASE_09.md), [10](PHASE_10.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-03. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Prepare/validate replacement program, pipeline and templates before one publication; preserve last-valid graph on compile/schema/allocation failure and retain old-frame versions. Expose reload status/errors through existing typed operations; E later projects task metadata.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [GEngine/src/Assets/ShaderCompilation.cpp](../../../GEngine/src/Assets/ShaderCompilation.cpp)
- [GEngine/include/GEngine/Assets/AssetRegistry.h](../../../GEngine/include/GEngine/Assets/AssetRegistry.h)
- [GEngine/src/Material/MaterialTemplate.cpp](../../../GEngine/src/Material/MaterialTemplate.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 3–5 production, 1–3 focused replacement/rollback checks/docs. Do not alter registry mechanics unless evidence requires it.

Non-goals: No automatic file watching, dependency database, Undo or generic task system.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-A valid edit, compile failure, schema mismatch, cancelled/stale replacement, retained old frame; no partial visible graph or duplicate destroy.

Evidence reuse: Registry retirement/fences and Phase 10 shader identity; unaffected standard-material checks. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Reload the maintained custom material during rendering and observe retained old-frame correctness.

Manual acceptance: Review successful intended visual change and failure recovery.

## Observable exit and review

Explicit reload succeeds atomically or leaves last-valid resources intact with contextual typed diagnostics.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

