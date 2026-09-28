# PRE_EDITOR Phase 00 — Minimal isolated activation and workflow setup

**PROPOSED / NOT_STARTED.** Milestone: Setup. No implementation or activation authorized by this file.

## Goal and traceability

Create the separately authorized PRE_EDITOR worktree and smallest reusable workflow adaptation, with reviewed document provenance and output isolation.

Canonical findings: Workflow setup (no production defect). Master section references: 16, 16.1, 18 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: Rendering Phase 68: 9995074e7af161db76c5bf79a5ede030702fb4ca, approved and published.

Bootstrap entry requires verified Rendering Phase 68 approval/commit/tag/publication/completed workflow records, owner acceptance of the PRE_EDITOR plan, and separate explicit activation/minimal-setup authorization. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies with its Phase 00 bootstrap exception: the already activated isolated worktree requirement starts at Phase 01. Creating that worktree and minimal workflow is Phase 00's authorized deliverable, not a prerequisite. This documentation revision grants no activation authorization.

Applicable unresolved decisions before affected START/gate: OD-01, OD-02. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Use ACTIVATION_PROPOSAL only after plan acceptance plus explicit setup authorization. Resolve all mutable Premake/runtime output paths; transfer allowlisted hashes; adopt compact routing/checkpoint namespace and publication policy without changing Rendering history.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [premake5.lua](../../../premake5.lua)
- [.codex/tools/rendering_checkpoint.py](../../../.codex/tools/rendering_checkpoint.py)
- [docs/rendering/PUBLICATION_POLICY.json](../../../docs/rendering/PUBLICATION_POLICY.json)

Proposed new/earlier-phase design slots: PRE_EDITOR local routing/state and compact checkpoint namespace; exact helper reuse delta selected before setup.

Change budget and reason: 0 production; at most 1 build and 3 governance implementation files plus required compact records/transferred docs. Reuse existing helpers; a larger framework change requires a scope amendment.

Non-goals: No engine implementation, formatting, build, application launch, benchmark, broad governance rewrite or next phase.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

Read-only identity/isolation/transfer-hash and protected-index verification; inspect routing ambiguity and exact publication consent. No builds or tests merely for creating the worktree.

Evidence reuse: Verified Phase 68 commit/tag/publication and audit preservation record; do not recreate historical receipts. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: No execution; prove future RBS runtime/settings/output paths are isolated.

Manual acceptance: Owner accepts setup identities, scope, routing and publication destination.

## Observable exit and review

Under the separate explicit setup authorization, Phase 00 creates the isolated worktree and minimal workflow setup, verifies concrete paths, transfer provenance and publication policy, then stops for owner review. No production output can target the Rendering worktree.

Activation is a separate owner action; this contract itself starts nothing.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

