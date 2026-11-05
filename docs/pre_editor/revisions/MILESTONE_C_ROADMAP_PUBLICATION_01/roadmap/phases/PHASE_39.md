# PRE_EDITOR Phase 39 — Indirect PBR integration

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: D. No implementation or phase activation authorized by this file.

## Goal and traceability

Combine Direct PBR and IBL under the same A material metallic/roughness model.

Canonical findings: LIT-03, LIT-01. Master section references: D.1, D.6, D.8, D.9 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 38 (must actually be approved and published before START).

Additional providing phases/baseline: [33](PHASE_33.md), [34](PHASE_34.md), [38](PHASE_38.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-09, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Bind environment resources deterministically, evaluate diffuse irradiance and prefiltered specular/LUT with shared Fresnel/material semantics and defined AO; remove fixed arbitrary ambient as final substitute. Environment revisions and old-frame lifetime remain coherent; shadow visibility does not suppress IBL/emissive.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag](/GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag)
- [GEngine/src/Renderer/SceneRenderResources.cpp](/GEngine/src/Renderer/SceneRenderResources.cpp)
- [GEngine/src/Renderer/FrameSubmission.cpp](/GEngine/src/Renderer/FrameSubmission.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 3–4 production, 1–3 IBL/material/visibility checks/docs.

Non-goals: No separate material model, global illumination, HDR display completion or area lights.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference metallic/dielectric roughness range, rotate/replace environment, direct lights/shadows off/on independently, normal maps and emissive independence; resource replacement failure. D0 applies.

Lighting-specific evidence: Matched per-frame IBL GPU/CPU and bound resource bytes; temporal rotation/replacement and tails under admitted profile. Explicitly label interim old display limitations. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Phase 38 map correctness/ownership and Phase 34 shared BRDF; unaffected direct-light/shadow cases. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Demonstrate material changes and environment replacement while rendering.

Manual acceptance: Required visual review of metallic/roughness/environment response.

## Observable exit and review

Indirect PBR responds correctly to shared material/environment semantics; final full dynamic range acceptance continues at 40–43.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_36.md`, SHA256 `653d6aa1765019be9ad4c5a9253e0b777651db9c9ef189e435629dcdb38567a2`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
