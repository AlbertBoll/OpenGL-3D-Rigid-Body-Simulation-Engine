# PRE_EDITOR Phase 18 — Shared semantic debug rendering foundation

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Make the existing RenderFrame debug payload submit through the approved renderer.

Canonical findings: DBG-01. Master section references: C.11, 18.1, E.16 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 17 (must actually be approved and published before START).

Additional providing phases/baseline: [12](PHASE_12.md), [17](PHASE_17.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-06. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Bounded engine-owned line/ray/box/sphere/axis descriptors, depth/lifetime/color policy and owner-thread preparation; remove current unsupported-debug-line rejection only with real submission support. Physics adapters emit colliders/AABBs/contacts/normals/COM; joint axes/limits and island/sleep colors extend it later.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Renderer/RenderFrame.h](../../../GEngine/include/GEngine/Renderer/RenderFrame.h)
- [GEngine/src/Renderer/RenderExtraction.cpp](../../../GEngine/src/Renderer/RenderExtraction.cpp)
- [GEngine/src/Renderer/FrameSubmission.cpp](../../../GEngine/src/Renderer/FrameSubmission.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: A small semantic debug provider header/source, within file budget.

Change budget and reason: 4–6 production, 1–3 bounded-payload/lifetime/visual checks/docs.

Non-goals: No raw GL in Physics/RBS, second debug renderer, Editor gizmos or full icon system.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C-Rest overlay on/off, depth/occlusion/lifetime and payload capacity, correct collider/normal coordinates, retained frame and shutdown; measure observer overhead used by later C diagnostics.

Evidence reuse: S16 existing frame payload and Debug pass helper path; immutable frame/GPU owners. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Draw actual collider/AABB/contact/COM overlays in the maintained scene.

Manual acceptance: Review spatial alignment, readability and depth policy.

## Observable exit and review

C can inspect semantic Physics overlays through a working shared path before any diagnostic depends on it.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

