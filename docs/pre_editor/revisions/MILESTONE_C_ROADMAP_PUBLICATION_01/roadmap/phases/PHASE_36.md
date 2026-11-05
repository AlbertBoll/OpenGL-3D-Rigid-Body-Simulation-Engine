# PRE_EDITOR Phase 36 — Spot shadow projection and owned resources

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: D. No implementation or phase activation authorized by this file.

## Goal and traceability

Provide the missing Spot shadow projection and target ownership contract.

Canonical findings: LIT-02. Master section references: D.4, D.5, D.6 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 35 (must actually be approved and published before START).

Additional providing phases/baseline: [33](PHASE_33.md), [35](PHASE_35.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-09, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Same Spot cone/range/transform conventions as ECS/frame/shader; projection/near-far/clip descriptors, depth target resolution/filter/bias settings, bounded allocation and safe replacement/resize/teardown. Expose readiness/typed unsupported-invalid errors until Phase 37 completes caster/visibility submission.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Core/RenderTarget.h](/GEngine/include/GEngine/Core/RenderTarget.h)
- [GEngine/src/Core/RenderTarget.cpp](/GEngine/src/Core/RenderTarget.cpp)
- [GEngine/include/GEngine/Renderer/ShadowQuality.h](/GEngine/include/GEngine/Renderer/ShadowQuality.h)
- [GEngine/src/Renderer/ShadowQuality.cpp](/GEngine/src/Renderer/ShadowQuality.cpp)
- [GEngine/src/Renderer/FrameSubmission.cpp](/GEngine/src/Renderer/FrameSubmission.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–3 projection/resource/error checks/docs.

Non-goals: No half-working public shadow success, Area shadow, full lighting algorithm change or native consumer target handles.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference Spot frustum/cone alignment through semantic debug and target lifecycle; invalid cone/near-far/allocation/replacement errors and retained old frame. D0 applies.

Lighting-specific evidence: Fixed admitted Spot quality profile; CPU allocation/preparation and depth-payload memory, target resize/retirement behavior. Visual/temporal shadow acceptance remains 37/43, no fabricated GPU shading metric before pass exists. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Existing paired target creation/lifetime and B camera math, Phase 33 Spot data; Phase 18 debug path. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Configure the Spot shadow resource and display semantic projection volume without claiming completed shading.

Manual acceptance: Review cone/frustum alignment, resource budget and error behavior.

## Observable exit and review

Spot projection/resources are valid and owned; full visible shadow capability is explicitly gated on Phase 37.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_33.md`, SHA256 `c2ebf821d0265e578e7ed1b08b9b6effee85476436ea578c6a2f8919ef14b88b`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
