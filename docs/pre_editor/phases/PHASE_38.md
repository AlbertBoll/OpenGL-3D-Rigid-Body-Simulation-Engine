# PRE_EDITOR Phase 38 — Energy-aware multiresolution Bloom

**PROPOSED / NOT_STARTED.** Milestone: D. No implementation or activation authorized by this file.

## Goal and traceability

Add required physically based Bloom as linear-HDR post processing.

Canonical findings: LIT-04, LIT-05. Master section references: D.11, D.15, D.16 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 37 (must actually be approved and published before START).

Additional providing phases/baseline: [37](PHASE_37.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-10, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Bounded downsample/filter/upsample reconstruction and energy-aware composite; strength/radius and optional artistic threshold/knee explicitly separate from energy semantics. Owned transient resources, resize behavior and exposure/tone ordering are declared. Emissive/direct/specular highlights participate without fake lights.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/src/Renderer/FrameSubmission.cpp](../../../GEngine/src/Renderer/FrameSubmission.cpp)
- [GEngine/src/Core/RenderTarget.cpp](../../../GEngine/src/Core/RenderTarget.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Minimal Bloom pass implementation and downsample/upsample/composite shader stages within the reviewed file budget.

Change budget and reason: 4–6 production including shaders, 1–3 energy/resize/reference/cost checks/docs.

Non-goals: No Bloom in BRDF/light code, display-space blur, auto exposure, extra scene lights or cinematic effects bundle.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference Bloom off/on for emissive/direct/specular highlights, dark/bright scenes, radius/strength bounds, resize and failure cleanup; inspect energy/halo stability and exposure interaction. D0 applies.

Lighting-specific evidence: Each admitted Bloom chain/profile records mip dimensions, filter work, GPU median/p95/p99, CPU submission and transient bytes; moving small highlight sequence tests temporal behavior, not a screenshot alone. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Phase 37 HDR/target lifetime and D reference identities; reuse unaffected lighting/shadow evidence. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Exercise bright scene contributions and Bloom settings through the actual post chain.

Manual acceptance: Required human review of halo/spread/energy/artifacts and visual benefit versus GPU cost.

## Observable exit and review

Bloom consumes retained HDR energy before final display, meets admitted visual/cost limits, and supports explicit quality settings.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

