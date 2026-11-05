# PRE_EDITOR Phase 37 — Spot caster pass and per-light shadow visibility

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: D. No implementation or phase activation authorized by this file.

## Goal and traceability

Complete required Spot shadows within the shared Direct PBR path.

Canonical findings: LIT-02, LIT-05. Master section references: D.4, D.5, D.6 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 36 (must actually be approved and published before START).

Additional providing phases/baseline: [34](PHASE_34.md), [36](PHASE_36.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-09, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Spot caster culling/submission/depth pass and deterministic binding, shadow sampling/bias/filter and revision/cache invalidation. Remove UnsupportedLights for admitted Spot-shadow cases only after real support. Visibility multiplies only that Spot's direct contribution.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/src/Renderer/FrameSubmission.cpp](/GEngine/src/Renderer/FrameSubmission.cpp)
- [GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag](/GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag)
- [GEngine/src/Renderer/SceneRenderResources.cpp](/GEngine/src/Renderer/SceneRenderResources.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Minimal Spot depth shader stage(s), reusing compatible existing stages when correct.

Change budget and reason: 4–6 production including shaders, 1–3 visibility/culling/reference checks/docs.

Non-goals: No shadowing IBL/emissive/other lights, new many-light system or silent missing-caster fallback.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference Spot-only/mixed/on-off, RBS-D-Motion moving cone/light/caster/camera, near/far/thin/grazing/contact coverage, cached/dirty and typed replacement failure. D0 applies.

Lighting-specific evidence: Spot pass CPU/GPU, caster/draw/instancing/state/upload counts, bytes and relevant p95/p99; compare matched quality and one supported count/caster variant; PERF65-04 shared gate stays open until 43. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Phase 36 projection/ownership and Phase 35 caster/quality conventions; independent visibility already correct for Directional/Point. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Create/enable/configure a shadowed Spot in the actual mixed-light scene.

Manual acceptance: Required human Spot shadow alignment/contact/temporal review.

## Observable exit and review

All three required light-shadow types work; Spot cone, projected depth and visible influence align with accepted temporal/reference evidence.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_34.md`, SHA256 `ef0c54284d4fb4517d0c0c243223ac989803adbcd963a8d9da5ab2db67e8f508`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
