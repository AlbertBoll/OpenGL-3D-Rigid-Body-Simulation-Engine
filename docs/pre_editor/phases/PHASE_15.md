# PRE_EDITOR Phase 15 — Complete typed input state and routing

**PROPOSED / NOT_STARTED.** Milestone: B. No implementation or activation authorized by this file.

## Goal and traceability

Deliver coherent input transitions and consumption through the backend-private platform boundary.

Canonical findings: INP-01. Master section references: B.1 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 14 (must actually be approved and published before START).

Additional providing phases/baseline: [13](PHASE_13.md), [14](PHASE_14.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-05. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Keyboard/text/pointer/buttons/wheel/modifiers/focus/capture/cancellation; lossless transitions separate from coalescible motion/latest state; held/pressed/released/delta snapshots; UI-view-game routing and handled state. Keep SDL translation private and measure actual costs before any optimization.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Managers/InputManager.h](../../../GEngine/include/GEngine/Managers/InputManager.h)
- [GEngine/src/Managers/InputManager.cpp](../../../GEngine/src/Managers/InputManager.cpp)
- [GEngine/src/Managers/EventManager.cpp](../../../GEngine/src/Managers/EventManager.cpp)
- [GEngine/include/GEngine/Events/KeyBoardEvent.h](../../../GEngine/include/GEngine/Events/KeyBoardEvent.h)
- [GEngine/include/GEngine/Events/MouseEvent.h](../../../GEngine/include/GEngine/Events/MouseEvent.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–3 focused transition/focus checks/docs; exact payload ownership selected before START.

Non-goals: No input mapping editor, universal commands bus, speculative queue optimization or other application runtime.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-B short press/release same frame, focus-loss releases, capture transfer/cancel, Unicode text and modifiers, motion/wheel accumulation and consumption; latency/backlog/copies/allocations versus admitted baseline.

Evidence reuse: Phase 13 queue/lifetime and platform native boundary; unchanged scheduling. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Actual controls and UI capture use the new engine input path.

Manual acceptance: Human checks focus/capture/text/pointer behavior and responsiveness.

## Observable exit and review

All required input categories have explicit state/routing/lifetime semantics and no stuck/lost transition under admitted cases.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

