# PRE_EDITOR Phase 14 — Authoritative runtime Transform mutation and successful-change notifications

**PROPOSED / NOT_STARTED.** Milestone: B. No implementation or activation authorized by this file.

## Goal and traceability

Provide minimal runtime mutation/hierarchy semantics before Camera and Physics consume them.

Canonical findings: TRN-01, EVT-01. Master section references: B.1, B.2, C.7, 18.1 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 13 (must actually be approved and published before START).

Additional providing phases/baseline: [04](PHASE_04.md), [08](PHASE_08.md), [13](PHASE_13.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-05. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Validate entity/local pose and hierarchy policy through Scene operations; preserve existing local/world cache and rigid-body root restrictions unless explicitly revised. [S22](../HANDOFF_AUDIT.md#scale-mutation-and-collider-behavior-s22) records the current pre-store callback and supported shape updates. Preserve those supported effects while deliberately separating the internal collider-update step from the new public successful-change notification after authoritative state is stored. Define and review validation/failure/update ordering, including handlers that currently reject a shape update without a typed result; do not describe the existing void callback as atomic rollback. Direct Scale writes, mesh regeneration/Bake and explicit collider edits remain distinct operations. No defect inference, transaction or Undo claim.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Component/Component.h](../../../GEngine/include/GEngine/Component/Component.h)
- [GEngine/include/GEngine/Scene/_Scene.h](../../../GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [GEngine/src/Scene/_Entity.cpp](../../../GEngine/src/Scene/_Entity.cpp)
- [GEngine/src/Scene/RenderState.cpp](../../../GEngine/src/Scene/RenderState.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–3 mutation/hierarchy/notification checks/docs.

Non-goals: No full persistent authoring, generalized preserve-world reparent tool, Undo or parallel Transform state.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-B valid/invalid/no-op pose and scale changes under the reviewed policy: the new public successful-change notification observes stored authoritative state and is absent on failed mutation. Separately preserve internal collider-update delivery/effects for supported changed setters and bridge lifetime; check sphere X-axis and point-shape/base-source behavior against S22. Cover direct-field versus semantic mutation, parent/local/world and physics-root rules; previous/current interpolation resets only for intended authoring teleports.

Evidence reuse: S09/S10 hierarchy caches/fixed-step poses, Phase 08 dimensions and Phase 13 lifetime provider. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Use semantic movement/scale operations for interactive objects and camera anchors.

Manual acceptance: Review ownership table and notification timing.

## Observable exit and review

Camera/Physics can consume authoritative runtime changes with explicit ownership and successful-change notifications; no E prerequisite.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

