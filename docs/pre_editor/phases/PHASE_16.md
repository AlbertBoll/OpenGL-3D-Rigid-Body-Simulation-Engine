# PRE_EDITOR Phase 16 — Unified semantic camera snapshot and conversions

**PROPOSED / NOT_STARTED.** Milestone: B. No implementation or activation authorized by this file.

## Goal and traceability

Give rendering, culling, shadows and picking one immutable camera interpretation.

Canonical findings: CAM-01. Master section references: B.2, 18.1 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 15 (must actually be approved and published before START).

Additional providing phases/baseline: [14](PHASE_14.md), [15](PHASE_15.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-05. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Projection/view/inverses/frustum/viewport pixel-DPI contract and world-to-view/NDC/pixel plus pixel-to-ray conversion; typed zero/minimized/invalid projection handling; produce RenderFrame camera snapshots from the same validated data.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Camera/SceneCamera.h](../../../GEngine/include/GEngine/Camera/SceneCamera.h)
- [GEngine/include/GEngine/Camera/SceneCamera.cpp](../../../GEngine/include/GEngine/Camera/SceneCamera.cpp)
- [GEngine/include/GEngine/Component/RenderComponents.h](../../../GEngine/include/GEngine/Component/RenderComponents.h)
- [GEngine/include/GEngine/Renderer/RenderFrame.h](../../../GEngine/include/GEngine/Renderer/RenderFrame.h)
- [GEngine/src/Renderer/RenderExtraction.cpp](../../../GEngine/src/Renderer/RenderExtraction.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: One semantic camera snapshot/math provider if the existing structures need consolidation; select within budget.

Change budget and reason: 4–6 production, 1–3 matrix/round-trip/projection checks/docs.

Non-goals: No Editor selection/gizmo UI, persistent camera schema or wholesale legacy Camera hierarchy rewrite.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-B projection modes/near-far/aspect, minimize/restore, DPI and coordinate round trips, ray/frustum/render agreement using approved floating tolerances.

Evidence reuse: Existing FrameCamera and rendering extraction; Phase 14 pose ownership. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Replace manually assembled frame matrices and verify visible object picking.

Manual acceptance: Human review pick alignment across viewport/DPI and projection modes.

## Observable exit and review

One source of camera matrices/conventions drives the maintained view, culling and picking; invalid extents fail semantically.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

