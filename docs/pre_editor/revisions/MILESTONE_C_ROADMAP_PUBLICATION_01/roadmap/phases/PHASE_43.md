# PRE_EDITOR Phase 43 — Lighting/shadow default profile and D acceptance

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: D. No implementation or phase activation authorized by this file.

## Goal and traceability

Qualify the practical RBS default against correctness, visual quality, temporal stability and performance.

Canonical findings: LIT-01, LIT-02, LIT-03, LIT-04, LIT-05, PERF65-04. Master section references: D.14, D.15, D.16, D.17, D.18, D.19, 14 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 42 (must actually be approved and published before START).

Additional providing phases/baseline: [33](PHASE_33.md), [34](PHASE_34.md), [35](PHASE_35.md), [36](PHASE_36.md), [37](PHASE_37.md), [38](PHASE_38.md), [39](PHASE_39.md), [40](PHASE_40.md), [41](PHASE_41.md), [42](PHASE_42.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-09, OD-10, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Run the predeclared smallest representative reference/motion/cost set; compare supported profiles by visible benefit and CPU/GPU/memory. Explain cached/dirty and scaling/tail behavior, select the lowest-cost owner-accepted quality, and disposition PERF65-04. No algorithm changes hidden in acceptance.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Renderer/ShadowQuality.h](/GEngine/include/GEngine/Renderer/ShadowQuality.h)
- [GEngine/src/Renderer/PassTiming.cpp](/GEngine/src/Renderer/PassTiming.cpp)
- [GEngine/include/GEngine/Core/RenderCounters.h](/GEngine/include/GEngine/Core/RenderCounters.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 0–2 production for selecting already implemented profile/necessary observation, 1–3 validation/reference records. A discovered fix needs revision/amendment to its owning phase.

Non-goals: No silent lower quality, missing required shadows/casters, optimistic p99, Cartesian benchmark matrix or speculative many-light engine.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference/Motion/Cost: all required cases and metrics in D0, representative supported caster/light/shadow counts, cascades/resolution/filter and cache state; matched complete query samples; explicit human acceptance. Insufficient tails/repeatability are INCONCLUSIVE.

Lighting-specific evidence: Report frame/per-light/shadow/IBL/HDR/Bloom/tone CPU/GPU, median/p95/p99, draw/caster/instance/state/upload/invalidation counts and measured byte categories. Existing one-of-each capacity may be scaled only within supported limits; exceeding it is explicit unsupported behavior, not justification for adding clustered lighting. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Accepted 33–42 evidence with exact input/profile identity; historical culling remains functional evidence, not new tail closure. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: One controlled full pipeline reference and admitted dynamic/performance variants in the maintained app.

Manual acceptance: Mandatory owner visual/temporal acceptance and default/cost/residual decision.

## Observable exit and review

All D semantics, three required shadow types, IBL/HDR/Bloom/exposure/display, profiles, visuals, motion and cost gates pass; original tail finding closed or explicitly accepted with bounds. Contracts stable for E persistence.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_40.md`, SHA256 `dad1ee623fca2ba9f3d9534c589d3899fc8e4f4d03dffc7f01dc89f63b47d879`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
