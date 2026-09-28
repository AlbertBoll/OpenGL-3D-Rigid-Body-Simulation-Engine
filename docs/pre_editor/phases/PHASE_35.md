# PRE_EDITOR Phase 35 — IBL environment processing and resource publication

**PROPOSED / NOT_STARTED.** Milestone: D. No implementation or activation authorized by this file.

## Goal and traceability

Create real diffuse/specular IBL resources with explicit lifetime and revisions.

Canonical findings: LIT-03. Master section references: D.8, D.9 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 34 (must actually be approved and published before START).

Additional providing phases/baseline: [05](PHASE_05.md), [10](PHASE_10.md), [31](PHASE_31.md), [34](PHASE_34.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-09, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Environment orientation/intensity/encoding; diffuse irradiance, roughness-prefiltered specular mips and BRDF integration LUT. CPU preparation and owner-context GPU processing use existing task/publication/lifetime architecture with bounded quality, cancellation and last-valid replacement.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/src/Assets/Texture.cpp](../../../GEngine/src/Assets/Texture.cpp)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Small environment owner/processing implementation and necessary convolution/prefilter/LUT shader stages; exact six-file allocation selected before START.

Change budget and reason: 4–6 production including shaders and RBS, 1–3 processing/resource/reference checks/docs. Split if stage and ownership files cannot fit.

Non-goals: No GI/DDGI, arbitrary ambient substitute, skybox-only claim, general environment editor or automatic asset database.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference known environment identity, rotation/encoding, finite irradiance/prefilter/LUT output, roughness mip ordering, processing budget/memory, cancellation/failure and retained last-valid maps. D0 applies.

Lighting-specific evidence: Record one-time preprocessing CPU/GPU and storage bytes separately from per-frame cost; timing tails need not invent a steady IBL shading pass before 36. Human target preview is supplementary, not final D approval. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Existing semantic Texture RGB16Float support and context publication; Phase 11 last-valid replacement pattern. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Request/replace the environment through the maintained scene's semantic resource path.

Manual acceptance: Review environment orientation and preprocessing/reference evidence.

## Observable exit and review

Valid IBL resources exist with defined energy/coordinate/quality contract and safe revisions, ready for 36 integration.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

