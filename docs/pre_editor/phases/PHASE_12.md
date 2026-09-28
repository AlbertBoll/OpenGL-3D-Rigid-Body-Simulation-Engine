# PRE_EDITOR Phase 12 — Async resource adoption and upload-stall evidence

**PROPOSED / NOT_STARTED.** Milestone: A. No implementation or activation authorized by this file.

## Goal and traceability

Finish A through real RBS asynchronous resource editing and an admitted upload-stall diagnosis.

Canonical findings: API-01, PERF65-03. Master section references: 8, A.1, A.5, 18.2 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 11 (must actually be approved and published before START).

Additional providing phases/baseline: [04](PHASE_04.md), [05](PHASE_05.md), [06](PHASE_06.md), [07](PHASE_07.md), [08](PHASE_08.md), [09](PHASE_09.md), [10](PHASE_10.md), [11](PHASE_11.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-04. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Use existing async queues/tickets/cancellation and scheduler upload safe point through semantic APIs; expose typed completion without a new scheduler. Admit original upload workload equivalence, collect stage/stall/memory evidence and decide PERF65-03; any remedy beyond this bounded integration needs prior scope amendment.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Assets/AsyncUploadQueue.h](../../../GEngine/include/GEngine/Assets/AsyncUploadQueue.h)
- [GEngine/src/Assets/AsyncUploadQueue.cpp](../../../GEngine/src/Assets/AsyncUploadQueue.cpp)
- [GEngine/src/Assets/AsyncTexture.cpp](../../../GEngine/src/Assets/AsyncTexture.cpp)
- [GEngine/src/Renderer/FrameScheduler.cpp](../../../GEngine/src/Renderer/FrameScheduler.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 2–5 production for consumer integration/instrumentation, 1–3 focused checks/docs. No speculative optimization budget.

Non-goals: No driver/staging rewrite without admitted cause, extra workers, broad performance sweep or E task database.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-A full required flow and exact async requests: success/failure/cancel/late result/old frame; decode, enqueue, scheduler upload, publication and bytes; original applicable stall limit and PERF65-03 gate decision.

Evidence reuse: Phase 65 failed async evidence and Phase 68 functional lifecycle; Phase 04–11 unaffected accepted cases. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: All A operations are used by the actual scene; include asynchronous replacement while old render frames exist.

Manual acceptance: Owner reviews A API simplicity, visible operations and residual disposition.

## Observable exit and review

A caller/internal dependency gates pass; async correctness is exercised and PERF65-03 is closed or explicitly accepted with quantified A limitation. Otherwise A exit remains blocked.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

