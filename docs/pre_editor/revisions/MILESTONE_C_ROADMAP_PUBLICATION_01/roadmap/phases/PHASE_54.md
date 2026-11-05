# PRE_EDITOR Phase 54 — Extend shared authoring/debug visualization and E exit

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: E. No implementation or phase activation authorized by this file.

## Goal and traceability

Expose semantic authoring overlays through the C debug provider.

Canonical findings: DBG-01, AUT-01, AUT-03. Master section references: E.16, E.17, 14 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 53 (must actually be approved and published before START).

Additional providing phases/baseline: [18](/docs/pre_editor/phases/PHASE_18.md), [29](PHASE_29.md), [43](PHASE_43.md), [44](PHASE_44.md), [46](PHASE_46.md), [53](PHASE_53.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Extend lines/rays/boxes/spheres/axes to frustum/grid/icon-billboard/bounds/Camera/Light and justified normals/tangents; reuse collider/contact/constraint displays; stable depth/lifetime/view scaling and bounded payload, no Core Editor GL.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Renderer/RenderFrame.h](/GEngine/include/GEngine/Renderer/RenderFrame.h)
- [GEngine/src/Renderer/RenderExtraction.cpp](/GEngine/src/Renderer/RenderExtraction.cpp)
- [GEngine/src/Renderer/FrameSubmission.cpp](/GEngine/src/Renderer/FrameSubmission.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

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

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Milestone C successor obligations

Additional providers: [30](PHASE_30.md), [31](PHASE_31.md), [32](PHASE_32.md). Extend existing semantic overlays to drive axis/target/cap and mover trajectory/velocity, read-only from shared providers; no parallel debug system.

## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_51.md`, SHA256 `50fe4fe8b2d5a340bf472ed9f4d98b087a87573117f0ab51dbf08e3357c2c281`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
