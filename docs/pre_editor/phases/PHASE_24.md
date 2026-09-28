# PRE_EDITOR Phase 24 — Physics, Scene and Rendering Transform contract

**PROPOSED / NOT_STARTED.** Milestone: C. No implementation or activation authorized by this file.

## Goal and traceability

Make authoring, simulation and presentation ownership explicit for all body modes.

Canonical findings: PHY-02, TRN-01. Master section references: C.6, C.7, 18.1 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 23 (must actually be approved and published before START).

Additional providing phases/baseline: [14](PHASE_14.md), [16](PHASE_16.md), [22](PHASE_22.md), [23](PHASE_23.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-06. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Typed Scene/body bindings replace raw RuntimeBody consumers; one scheduler publishes previous/current simulation poses, render-only interpolation and deliberate authoring teleport/kinematic updates. Integrate the deliberate 08/13/14 migration of the existing supported scale bridge from [S22](../HANDOFF_AUDIT.md#scale-mutation-and-collider-behavior-s22): connected changed setters, shape-specific scale/base-source initialization, revisions, body cache/wake handling and disconnect/copy cleanup. Keep Transform scaling, mesh regeneration/Bake and explicit collider replacement distinct; a Transform reset within Bake must account for its collider effect rather than silently adding or removing coupling. Define reviewed invalid-input and parent/body policies; preserve no-Physics root/world semantics.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Component/Component.h](../../../GEngine/include/GEngine/Component/Component.h)
- [GEngine/include/GEngine/Scene/_Scene.h](../../../GEngine/include/GEngine/Scene/_Scene.h)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [GEngine/src/Scene/RenderState.cpp](../../../GEngine/src/Scene/RenderState.cpp)
- [GEngine/src/Renderer/RenderExtraction.cpp](../../../GEngine/src/Renderer/RenderExtraction.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–3 transform/fixed-step/lifecycle checks/docs.

Non-goals: No second scheduler, interpolation writing simulation, full E persistence or silent hierarchy policy expansion.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-C static/kinematic/dynamic movement, authoring teleport, variable presentation frame rates versus fixed simulation, pause/resume/drop-backlog, scale/body deletion and shadow/pick/debug alignment. Verify supported repeated/no-op/uniform/nonuniform setter behavior, initial shape base versus explicit collider replacement, invalid requests under the reviewed migration policy, revision-driven bounds/inertia/wake behavior and live/stopped/copied bridge ownership. Mesh regeneration/Bake alone must not silently rewrite a collider; any associated Transform reset is explicit.

Evidence reuse: S10 fixed-step design, Phase 14 mutation notifications and Phase 21/22 stability/body semantics. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Exercise interactive body manipulation while rendering/interpolating/picking through the maintained loop.

Manual acceptance: Review visible interpolation and ownership cases.

## Observable exit and review

Documented pose ownership table matches observed RBS behavior and no normal caller mutates solver state to render.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

