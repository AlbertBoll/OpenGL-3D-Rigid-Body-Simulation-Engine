# PRE_EDITOR Phase 42 — Manual exposure, tone mapping and final display

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: D. No implementation or phase activation authorized by this file.

## Goal and traceability

Finish one coherent HDR-to-display path owned by the camera/view contract.

Canonical findings: LIT-04. Master section references: D.12, D.13, D.9 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 41 (must actually be approved and published before START).

Additional providing phases/baseline: [16](/docs/pre_editor/phases/PHASE_16.md), [40](PHASE_40.md), [41](PHASE_41.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-10, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Stable manual exposure representation/defaults and application point; reviewed pre-exposure/exposure and Bloom order, selected tone mapper and exactly one display/sRGB conversion. Semantic shared engine behavior; other maintained frontend compatibility is inspected, not separately runtime-tested.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Renderer/RenderFrame.h](/GEngine/include/GEngine/Renderer/RenderFrame.h)
- [GEngine/src/Renderer/FrameSubmission.cpp](/GEngine/src/Renderer/FrameSubmission.cpp)
- [GEngine/include/GEngine/Assets/Shaders/aa_post.frag](/GEngine/include/GEngine/Assets/Shaders/aa_post.frag)
- [GEngine/include/GEngine/Component/RenderComponents.h](/GEngine/include/GEngine/Component/RenderComponents.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 3–5 production, 1–3 color/exposure/reference checks/docs.

Non-goals: No automatic exposure, exposure hidden in materials/lights, application-specific tone mappers or unrelated post effects.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference dark/bright/exposure ramps and HDR/Bloom off-on, finite values/highlights, display encoding and view switching; preserve HUD/UI treatment explicitly without double conversion. D0 applies.

Lighting-specific evidence: Post/tone CPU/GPU and output format cost, view/exposure motion temporal checks and relevant p95/p99; intended appearance change uses new controlled reference, not old gamma pixel identity. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Phase 40 color responsibilities, Phase 41 energy/compositing and B camera/view ownership. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Control manual exposure and compare accepted final display references.

Manual acceptance: Required owner review of dark/bright/highlight/Bloom/exposure response and final display.

## Observable exit and review

Standard material -> three Direct Lights -> per-light shadows -> IBL/emissive -> HDR -> Bloom -> exposure -> tone map -> display is one defined maintained path.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_39.md`, SHA256 `14aca6ce0bf5a9237bf02dca1132dbf31a020074d2fceea11f4cc6e1cee89ab1`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
