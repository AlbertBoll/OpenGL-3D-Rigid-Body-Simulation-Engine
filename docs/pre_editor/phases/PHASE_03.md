# PRE_EDITOR Phase 03 — Bounded resource and RBS readability cleanup

**PROPOSED / NOT_STARTED.** Milestone: Code Quality. No implementation or activation authorized by this file.

## Goal and traceability

Separate dense acquisition/error/cleanup steps so A API changes can be reviewed clearly.

Canonical findings: CQ-01, CQ-02. Master section references: 5, 7, 18 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 02 (must actually be approved and published before START).

Additional providing phases/baseline: [02](PHASE_02.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: none beyond C0/V0 setup, exact scope and identity admission. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Refactor only resource construction/publication setup and the RBS material/scene initialization path into named coherent steps; preserve behavior, ownership and failure order.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [GEngine/src/Managers/AssetsManager.cpp](../../../GEngine/src/Managers/AssetsManager.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 2–3 production, 1–2 focused validation/review files. Responsibility cleanup only, not new APIs.

Non-goals: No public redesign, resource cache optimization, formatting spillover or new capability.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

Affected Debug/Release build; focused RBS successful setup and one relevant existing typed failure/cleanup path; inspect exact resource retirement/error equivalence.

Evidence reuse: Phase 68 unaffected rendering/native/exception evidence with matching inputs; only changed paths receive new evidence. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Actual maintained initialization and teardown path, not startup alone if a failure branch changed.

Manual acceptance: Review readability and error/ownership equivalence.

## Observable exit and review

Readable control/error flow with no behavior or lifetime regression; CQ-01 pre-A gate satisfied after approval.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

