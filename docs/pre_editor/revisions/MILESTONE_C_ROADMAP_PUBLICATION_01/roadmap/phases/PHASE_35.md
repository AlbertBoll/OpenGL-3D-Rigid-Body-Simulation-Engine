# PRE_EDITOR Phase 35 — Directional and Point shadow quality and invalidation

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: D. No implementation or phase activation authorized by this file.

## Goal and traceability

Preserve existing Directional/Point shadows while making supported quality/caching behavior explicit.

Canonical findings: LIT-02, LIT-05, PERF65-04. Master section references: D.5, D.6, D.15, D.16 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 34 (must actually be approved and published before START).

Additional providing phases/baseline: [16](/docs/pre_editor/phases/PHASE_16.md), [34](PHASE_34.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-09, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Direction/frustum/cascade split coverage, stable transitions, depth/bias/filter and Point range/cube-face/depth conventions; explicit shadow profile and resource ownership/fallback, caster/layer culling and update/reuse invalidation on camera/light/caster/material changes. Choose minimal evidence-required corrections under this bounded contract.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Renderer/ShadowQuality.h](/GEngine/include/GEngine/Renderer/ShadowQuality.h)
- [GEngine/src/Renderer/ShadowQuality.cpp](/GEngine/src/Renderer/ShadowQuality.cpp)
- [GEngine/src/Renderer/FrameSubmission.cpp](/GEngine/src/Renderer/FrameSubmission.cpp)
- [GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag](/GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 3–5 production, 1–3 cache/quality/visual checks/docs; wider shadow algorithm changes require amendment.

Non-goals: No shadow rewrite, silent resolution reduction, BVH speculation or Spot path yet.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference and RBS-D-Motion directional cascade/Point seam, near/far/thin/grazing/contact and shadow-off controls, light/camera/caster motion, cached versus dirty; typed target failures and old-frame cleanup. D0 applies.

Lighting-specific evidence: Profiles name resolutions/cascades/splits/filter/bias/update and byte costs. Record CPU/GPU draw/caster/cache counts, median/p95/p99 and payload versus VRAM distinction; investigate matched tails without claiming fewer draws prove faster. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Approved ShadowQuality target budget/culling/signatures and Phase 59 correct culling with open tails; PERF65-04 remains canonical. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Expose selected quality options semantically and exercise both existing shadow types.

Manual acceptance: Required shadow contact/seam/temporal review; visible shadow alone cannot pass.

## Observable exit and review

Directional and Point shadow correctness, required visual/temporal criteria and measured profiles are preserved or explicitly improved; no unrelated term suppressed.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_32.md`, SHA256 `8f1cb14676776d3822ffea9fac90fc7241d57dcde50a1d6a0aa5e311da2fc900`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
