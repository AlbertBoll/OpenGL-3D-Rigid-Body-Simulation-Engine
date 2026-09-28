# PRE_EDITOR Phase 02 — Mechanical formatting of initial A touchpoints

**PROPOSED / NOT_STARTED.** Milestone: Code Quality. No implementation or activation authorized by this file.

## Goal and traceability

Make initial resource/consumer code readable without changing tokens or behavior.

Canonical findings: CQ-01. Master section references: 7, 18 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 01 (must actually be approved and published before START).

Additional providing phases/baseline: [01](PHASE_01.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: none beyond C0/V0 setup, exact scope and identity admission. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Format only selected SceneRenderResources header/source, AssetsManager implementation and RBS setup source using Phase 01 policy; isolate all whitespace-only changes.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Renderer/SceneRenderResources.h](../../../GEngine/include/GEngine/Renderer/SceneRenderResources.h)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [GEngine/src/Managers/AssetsManager.cpp](../../../GEngine/src/Managers/AssetsManager.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 2–4 production, at most 1 review document. No new tests for whitespace.

Non-goals: No renames, comments with changed meaning, logic changes, include-order surprises or repository-wide formatting.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

Token/lexical comparison plus diff review and affected Debug/Release compile. Any semantic delta leaves this phase for 03; no new runtime case required.

Evidence reuse: Phase 68 functional evidence remains applicable after token identity and compilation. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: RBS source participates in lexical/compile validation; existing runtime evidence reused.

Manual acceptance: Review the mechanical diff independently.

## Observable exit and review

Only approved mechanical changes; long statement structure is readable and semantic identity is established.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

