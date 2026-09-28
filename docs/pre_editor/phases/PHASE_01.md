# PRE_EDITOR Phase 01 — Adopt scoped Code Quality policy

**PROPOSED / NOT_STARTED.** Milestone: Code Quality. No implementation or activation authorized by this file.

## Goal and traceability

Adopt readable C++ policy and a pinned, compatible formatter configuration for bounded changes.

Canonical findings: CQ-01, CQ-02. Master section references: 7, 18 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 00 (must actually be approved and published before START).

Additional providing phases/baseline: [00](PHASE_00.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: none beyond C0/V0 setup, exact scope and identity admission. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Review proposals/CODING_STYLE.proposed.md and clang-format.proposed; select formatter version; establish fix-before-A, fix-when-touched and deferred scopes with no automatic source sweep.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Live style documentation/configuration only in the activated worktree after authorization.

Change budget and reason: 0 production; 2–3 policy/config/documentation files. Adoption is intentionally separate from formatting.

Non-goals: No source formatting, semantic cleanup, API change, compiler or dependency migration.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

Inspect configuration compatibility with chosen formatter and representative source styles; deterministic proposed output review without modifying source. No runtime or tests.

Evidence reuse: Audit style metrics and source-observation hashes. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Consumer source is the readability sample; no application run.

Manual acceptance: Owner/reviewer accepts policy and narrow adoption.

## Observable exit and review

Reviewed policy identifies exact initial A touchpoints and separates mechanical versus semantic changes.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

