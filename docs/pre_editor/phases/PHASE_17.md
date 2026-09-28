# PRE_EDITOR Phase 17 — Scene Camera and Editor view lifecycle adoption

**PROPOSED / NOT_STARTED.** Milestone: B. No implementation or activation authorized by this file.

## Goal and traceability

Distinguish authored Scene Cameras from transient Editor views and finish B in RBS.

Canonical findings: CAM-01, INP-01. Master section references: B.2 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 16 (must actually be approved and published before START).

Additional providing phases/baseline: [15](PHASE_15.md), [16](PHASE_16.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-05. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Active camera, preview, switch, delete/replace and viewport binding semantics; Editor Camera control uses B input and runtime Transform contract; both produce Phase 16 snapshots, with no duplicated projection implementation.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Camera/EditorCamera.h](../../../GEngine/include/GEngine/Camera/EditorCamera.h)
- [GEngine/src/Camera/EditorCamera.cpp](../../../GEngine/src/Camera/EditorCamera.cpp)
- [GEngine/include/GEngine/Scene/_Scene.h](../../../GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 3–5 production, 1–3 lifecycle/routing checks/docs.

Non-goals: No Core Editor UI, persistence, selecting/gizmo tools or additional runtime apps.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 full RBS-B actual camera movement, preview/active switch/delete/resize/focus/capture, stale reference failure and picking. Inspect compatibility; compile affected shared legacy users only if justified.

Evidence reuse: Phase 13–16 accepted dispatch, math and input cases; repeat only changed combinations. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Switch between a Scene Camera and Editor view while interacting/picking.

Manual acceptance: Human acceptance of routing, controls, camera lifecycle and picking.

## Observable exit and review

B input/camera usability, lifetime and measurement gates pass with real RBS control and two distinct view roles.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

