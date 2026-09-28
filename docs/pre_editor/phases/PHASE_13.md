# PRE_EDITOR Phase 13 — Typed subscriptions, lifetime and dispatch semantics

**PROPOSED / NOT_STARTED.** Milestone: B. No implementation or activation authorized by this file.

## Goal and traceability

Replace unsafe subscription identity/lifetime and make dispatch behavior explicit.

Canonical findings: EVT-01. Master section references: B.1, 18.1 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 12 (must actually be approved and published before START).

Additional providing phases/baseline: [12](PHASE_12.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-05. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Use one engine-owned typed subscription provider with RAII release, stable generation identity, owner-thread synchronous delivery, bounded deferred completion and explicit mutation/reentrancy/overflow/cancellation rules. Inspect/reuse useful Event/Signal pieces; migrate the scale-bridge connection lifetime while preserving existing supported setter delivery, new-scale callback argument, shape-specific/base-source behavior and disconnect/copy cleanup in [S22](../HANDOFF_AUDIT.md#scale-mutation-and-collider-behavior-s22). Do not reinterpret a direct Scale write as a signal emission or change the shape policy. Phase 14 deliberately addresses authoritative mutation/notification ordering; this phase adds no second notification system.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Events/Event.h](../../../GEngine/include/GEngine/Events/Event.h)
- [GEngine/include/GEngine/Events/Signal.h](../../../GEngine/include/GEngine/Events/Signal.h)
- [GEngine/include/GEngine/Managers/EventManager.h](../../../GEngine/include/GEngine/Managers/EventManager.h)
- [GEngine/src/Managers/EventManager.cpp](../../../GEngine/src/Managers/EventManager.cpp)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–3 focused lifetime/dispatch checks/docs; choose minimal consolidation before START.

Non-goals: No general task/frame/Physics scheduler, input feature bundle, transactions or blanket legacy Signal rewrite.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-B listener destroy/recreate, self/cross unsubscribe, add-during-dispatch, nested dispatch, queued stale completion and teardown; preserve changed-versus-unchanged setter delivery and Physics bridge startup/stop/rollback/copy disconnection. Check callback argument and shape effect under the existing supported conditions. Measure order/count, bounded backlog, allocations/copies/latency and observer cost before optimization.

Evidence reuse: Existing scheduler safe points and typed IDs; S11/S22 identify concrete failure modes. No existing Event/Signal lifetime safety is assumed. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Replace raw event subscriptions capturing the application; demonstrate lifetime cases through the maintained loop.

Manual acceptance: Review routing/queue contract and safe callback ownership.

## Observable exit and review

Subscription release identifies exactly one connection; dispatch cannot invalidate live iteration or deliver to destroyed owner; explicit queue/thread contract.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

