# PRE_EDITOR Phase 37 — Linear HDR scene color and resolve

**PROPOSED / NOT_STARTED.** Milestone: D. No implementation or activation authorized by this file.

## Goal and traceability

Preserve linear lighting energy through an owned HDR scene-color target.

Canonical findings: LIT-04. Master section references: D.9, D.10 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 36 (must actually be approved and published before START).

Additional providing phases/baseline: [31](PHASE_31.md), [34](PHASE_34.md), [36](PHASE_36.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-10, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Select supported HDR target precision/format, lifetime/resize and MSAA resolve; define sRGB BaseColor/encoded color decoding versus linear metallic/roughness/normal/AO data, authored colors/environment/emissive. Remove per-material display gamma from the migrated path and establish one later display boundary.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Core/RenderTarget.h](../../../GEngine/include/GEngine/Core/RenderTarget.h)
- [GEngine/src/Core/RenderTarget.cpp](../../../GEngine/src/Core/RenderTarget.cpp)
- [GEngine/src/Renderer/FrameSubmission.cpp](../../../GEngine/src/Renderer/FrameSubmission.cpp)
- [GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag](../../../GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag)
- [GEngine/include/GEngine/Assets/Shaders/aa_post.frag](../../../GEngine/include/GEngine/Assets/Shaders/aa_post.frag)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–3 format/color/resize/resolve checks/docs.

Non-goals: No display-space lighting, duplicated gamma, Bloom/tone-map bundle or silent HDR clamp.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference known linear/encoded inputs and >1 direct/IBL/emissive highlights, texture-channel interpretation, resolve/resize/minimize, typed allocation rollback and final-output boundary identity. D0 applies.

Lighting-specific evidence: HDR/resolve CPU/GPU and byte cost by dimensions/sample count; matched captures distinguish intended range change from regression; temporal resize and tails recorded. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Approved target ownership/move-only lifetime and semantic Texture encodings; accepted D direct/IBL computations. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Use the actual maintained render target and inspect controlled HDR contribution cases.

Manual acceptance: Review HDR reference and color-space ledger; final display approval follows 39.

## Observable exit and review

Documented HDR values survive lighting/resolve; no double/missing decode. Post processing has a defined linear input.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

