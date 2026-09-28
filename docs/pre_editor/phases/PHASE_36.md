# PRE_EDITOR Phase 36 — Indirect PBR integration

**PROPOSED / NOT_STARTED.** Milestone: D. No implementation or activation authorized by this file.

## Goal and traceability

Combine Direct PBR and IBL under the same A material metallic/roughness model.

Canonical findings: LIT-03, LIT-01. Master section references: D.1, D.6, D.8, D.9 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 35 (must actually be approved and published before START).

Additional providing phases/baseline: [30](PHASE_30.md), [31](PHASE_31.md), [35](PHASE_35.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-09, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Bind environment resources deterministically, evaluate diffuse irradiance and prefiltered specular/LUT with shared Fresnel/material semantics and defined AO; remove fixed arbitrary ambient as final substitute. Environment revisions and old-frame lifetime remain coherent; shadow visibility does not suppress IBL/emissive.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag](../../../GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [GEngine/src/Renderer/FrameSubmission.cpp](../../../GEngine/src/Renderer/FrameSubmission.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 3–4 production, 1–3 IBL/material/visibility checks/docs.

Non-goals: No separate material model, global illumination, HDR display completion or area lights.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference metallic/dielectric roughness range, rotate/replace environment, direct lights/shadows off/on independently, normal maps and emissive independence; resource replacement failure. D0 applies.

Lighting-specific evidence: Matched per-frame IBL GPU/CPU and bound resource bytes; temporal rotation/replacement and tails under admitted profile. Explicitly label interim old display limitations. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Phase 35 map correctness/ownership and Phase 31 shared BRDF; unaffected direct-light/shadow cases. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Demonstrate material changes and environment replacement while rendering.

Manual acceptance: Required visual review of metallic/roughness/environment response.

## Observable exit and review

Indirect PBR responds correctly to shared material/environment semantics; final full dynamic range acceptance continues at 37–40.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

