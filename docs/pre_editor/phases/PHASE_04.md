# PRE_EDITOR Phase 04 — Minimum runtime Scene operations and shared assignment

**PROPOSED / NOT_STARTED.** Milestone: A. No implementation or activation authorized by this file.

## Goal and traceability

Expose the smallest semantic Scene/Entity create, assign and duplicate operations needed by A.

Canonical findings: API-05, API-01, API-02. Master section references: A.1, A.3, A.5, 18.1 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 03 (must actually be approved and published before START).

Additional providing phases/baseline: [03](PHASE_03.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-03. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Reuse _Scene CreateEntity/DuplicateEntity/Copy, typed scene errors and modern render handles. Validate scene/domain/liveness; duplicate gets new identity and shared mesh/material handles, clears runtime Physics bindings; return a successful-change/revision result without claiming transaction notifications.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Scene/_Scene.h](../../../GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [GEngine/include/GEngine/Scene/_Entity.h](../../../GEngine/include/GEngine/Scene/_Entity.h)
- [GEngine/src/Scene/_Entity.cpp](../../../GEngine/src/Scene/_Entity.cpp)
- [GEngine/include/GEngine/Component/RenderComponents.h](../../../GEngine/include/GEngine/Component/RenderComponents.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–2 focused tests/docs. Reuse existing algorithms; select minimal header/source members.

Non-goals: No full hierarchy authoring, persistent IDs/database, serialization, EventBus or Undo.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0, RBS-A-Authoring create/assign/share/duplicate; focused foreign/stale entity and wrong-domain errors; prove no heavy copies and unchanged old frame.

Evidence reuse: S06 registry identity/lifetime and S09 clone algorithms; approved mesh/material binding behavior. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Adopt these operations in real primitive/material entity construction and duplication.

Manual acceptance: Review caller simplicity and new identity/shared handle evidence.

## Observable exit and review

Normal RBS authoring no longer needs raw registry manipulation for these operations; sharing and error semantics are explicit.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

