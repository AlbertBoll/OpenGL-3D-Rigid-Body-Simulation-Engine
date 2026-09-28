# PRE_EDITOR Phase 51 — Extend shared authoring/debug visualization and E exit

**PROPOSED / NOT_STARTED.** Milestone: E. No implementation or activation authorized by this file.

## Goal and traceability

Expose semantic authoring overlays through the C debug provider.

Canonical findings: DBG-01, AUT-01, AUT-03. Master section references: E.16, E.17, 14 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 50 (must actually be approved and published before START).

Additional providing phases/baseline: [18](PHASE_18.md), [29](PHASE_29.md), [40](PHASE_40.md), [41](PHASE_41.md), [43](PHASE_43.md), [50](PHASE_50.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Extend lines/rays/boxes/spheres/axes to frustum/grid/icon-billboard/bounds/Camera/Light and justified normals/tangents; reuse collider/contact/constraint displays; stable depth/lifetime/view scaling and bounded payload, no Core Editor GL.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Renderer/RenderFrame.h](../../../GEngine/include/GEngine/Renderer/RenderFrame.h)
- [GEngine/src/Renderer/RenderExtraction.cpp](../../../GEngine/src/Renderer/RenderExtraction.cpp)
- [GEngine/src/Renderer/FrameSubmission.cpp](../../../GEngine/src/Renderer/FrameSubmission.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Extend Phase 18 debug descriptors/implementation; icon resources only as needed in the same provider.

Change budget and reason: 3–6 production, 1–3 overlay/lifetime/reference checks/docs.

Non-goals: No selection/gizmo/Inspector/Content Browser UI, second debug path or arbitrary geometry editor.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E overlay hierarchy/camera/light/collider/constraint views, view switch/DPI/depth and payload lifetime; verify E save/load/reimport/transactions/Undo/Play accepted evidence still covers final authoring surface.

Evidence reuse: C debug primitives and accepted E capability evidence; no automatic complete rerun. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Use actual authoring scene/debug displays and inspect E capability integration.

Manual acceptance: Owner reviews E API/visualization and authoring workflow acceptance.

## Observable exit and review

E foundation is sufficient for future authoring UI with semantic APIs, persistence, transactions and shared overlays; no A–D dependency had waited on it.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

