# PRE_EDITOR Phase 50 — Edit/Play state and resource isolation

**PROPOSED / NOT_STARTED.** Milestone: E. No implementation or activation authorized by this file.

## Goal and traceability

Ensure runtime simulation cannot corrupt persistent authoring state.

Canonical findings: AUT-05. Master section references: E.11, E.12 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 49 (must actually be approved and published before START).

Additional providing phases/baseline: [13](PHASE_13.md), [24](PHASE_24.md), [41](PHASE_41.md), [47](PHASE_47.md), [48](PHASE_48.md), [49](PHASE_49.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Explicit authoring Scene versus Play clone, mutable material/asset copy-on-write policy, entity/Transform/body/camera/light ownership; subscriptions/tasks/events tied to runtime lifetime, stale callback cancellation, deterministic Stop restoration and resource drain.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Scene/_Scene.h](../../../GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [GEngine/src/Scene/RenderState.cpp](../../../GEngine/src/Scene/RenderState.cpp)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 3–5 production, 1–3 state-isolation/cancellation checks/docs.

Non-goals: No automatic apply-Play-changes feature, duplicate Physics scheduler, game editor UI or hidden deep asset copying.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E authored fingerprint/save revision before Play, simulate/mutate runtime bodies/materials/camera/lights, queue completion then Stop/unload, verify authored state and old handles/resources/callback lifetime, repeat once only if contract requires.

Evidence reuse: Existing Scene::Copy clears runtime bindings; Phase 13 lifetimes, 24 poses, 47 tasks and 48/49 transactions. Shared mutable-resource isolation is new evidence, not inferred from clone. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Enter Play, simulate the C scene, stop and verify authoring state in the maintained app.

Manual acceptance: Required human Edit/Play/Stop acceptance and isolation review.

## Observable exit and review

Play and Stop preserve authored state and saved revision while cleaning runtime resources/subscriptions/tasks deterministically.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

