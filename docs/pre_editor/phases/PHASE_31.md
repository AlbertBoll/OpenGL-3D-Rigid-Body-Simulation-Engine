# PRE_EDITOR Phase 31 — Shared Direct PBR and light-specific evaluation

**PROPOSED / NOT_STARTED.** Milestone: D. No implementation or activation authorized by this file.

## Goal and traceability

Use one coherent BRDF for Directional, Point and Spot Direct PBR.

Canonical findings: LIT-01. Master section references: D.1, D.2, D.3, D.4, D.6 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 30 (must actually be approved and published before START).

Additional providing phases/baseline: [30](PHASE_30.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-09, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Shared metallic/roughness GGX/Smith/Fresnel energy behavior and TBN/normal-map conventions; light-specific direction/radiance/defined Point attenuation and smooth Spot range/cone falloff. Preserve correct per-light shadow separation and emissive/indirect independence; remove duplicated material evaluation.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag](../../../GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag)
- [GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.vert](../../../GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.vert)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [GEngine/src/Renderer/FrameSubmission.cpp](../../../GEngine/src/Renderer/FrameSubmission.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 3–5 production including shader files, 1–3 material/light/reference checks/docs.

Non-goals: No IBL substitute, Bloom in BRDF, Area Lights, HDR/display completion or unrelated shader cleanup.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference dielectric/metallic low/high roughness, normal-mapped glossy floor, each light isolated then mixed, distance/range and cone boundary cases; finite output/energy/semantic checks and preserved independent shadows. D0 applies.

Lighting-specific evidence: Record profile and matched per-light CPU/GPU/bytes with p95/relevant p99 protocol; compare motion for highlight/normal stability. Old LDR display remains an explicit interim limitation until 37–39, not final D acceptance. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Existing GGX/Smith/Schlick pieces and correctly isolated Point/Directional visibility; Phase 30 packing. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Actual standard materials under all light types, including edits while rendering.

Manual acceptance: Required owner visual review of Direct PBR response and normal mapping.

## Observable exit and review

All three light types use the same material/BRDF with defined attenuation and smooth cone behavior; intentional appearance changes have controlled owner references.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

