# PRE_EDITOR Phase 44 — Complete Scene/ECS authoring and hierarchy lifecycle

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: E. No implementation or phase activation authorized by this file.

## Goal and traceability

Extend minimum runtime operations into the Engine authoring Scene surface.

Canonical findings: AUT-01, TRN-01. Master section references: E.1, E.11, E.17 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 43 (must actually be approved and published before START).

Additional providing phases/baseline: [04](/docs/pre_editor/phases/PHASE_04.md), [14](/docs/pre_editor/phases/PHASE_14.md), [24](/docs/pre_editor/phases/PHASE_24.md), [43](PHASE_43.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Scene/entity create/delete/duplicate, component add/remove/get and semantic queries; clone/replace/unload/destroy lifecycle; local/world Transform, parent/unparent/reparent with preserve-world/local, scale/negative/zero/singular policy and hierarchy revisions. Keep normal callers independent of raw EnTT.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Scene/_Scene.h](/GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](/GEngine/src/Scene/_Scene.cpp)
- [GEngine/include/GEngine/Scene/_Entity.h](/GEngine/include/GEngine/Scene/_Entity.h)
- [GEngine/src/Scene/_Entity.cpp](/GEngine/src/Scene/_Entity.cpp)
- [GEngine/src/Scene/RenderState.cpp](/GEngine/src/Scene/RenderState.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–3 lifecycle/hierarchy checks/docs. Reuse existing algorithms, do not rewrite the full Scene file.

Non-goals: No selection/gizmo/Inspector UI, persistence or transaction implementation yet.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E authoring lifecycle, clone isolation/runtime clearing, parent cycle/foreign/stale rejection, preserve-world/local including nonuniform/singular cases, deletion/replacement and render/Physics coherence.

Evidence reuse: Phase 04 sharing, Phase 14 mutation/notifications, Phase 24 body pose policy and accepted D semantics. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Author a small hierarchy and replace/clone scenes through the actual app.

Manual acceptance: Review authoring API and hierarchy behavior.

## Observable exit and review

Complete normal Scene operations have typed errors and explicit hierarchy/lifecycle semantics; no raw registry knowledge required.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_41.md`, SHA256 `962e3b1f6822f1cbc5799f74705ededfd4de4670900dbd1a79348a40b14a3869`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
