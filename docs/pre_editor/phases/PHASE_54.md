# PRE_EDITOR Phase 54 — Full RBS authoring-to-runtime integration

**PROPOSED / NOT_STARTED.** Milestone: F. No implementation or activation authorized by this file.

## Goal and traceability

Demonstrate the complete Core Editor foundation through the maintained consumer.

Canonical findings: INT-01. Master section references: 14 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 53 (must actually be approved and published before START).

Additional providing phases/baseline: [12](PHASE_12.md), [17](PHASE_17.md), [29](PHASE_29.md), [40](PHASE_40.md), [51](PHASE_51.md), [52](PHASE_52.md), [53](PHASE_53.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-14. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Exercise the integrated sequence below using A–E APIs already adopted along the way. Only small integration glue or concrete fixes within the declared budget; a subsystem defect routes to its owner contract/amendment.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)
- [GEngine/src/Scene/_Scene.cpp](../../../GEngine/src/Scene/_Scene.cpp)
- [GEngine/src/Renderer/FrameScheduler.cpp](../../../GEngine/src/Renderer/FrameScheduler.cpp)
- [GEngine/src/Renderer/SceneRenderResources.cpp](../../../GEngine/src/Renderer/SceneRenderResources.cpp)

Change budget and reason: 1–4 production, 1–3 integrated scenario/evidence files. No broad catch-all implementation.

Non-goals: No postponed A–E capability, Core Editor UI, new demo or automatic full historical test matrix.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-F-Integrated in Debug/Release; reuse qualified C/D timings and accepted references when input identity matches, run only changed interactions. All final API/code-quality/rendering/Physics/PBR/shadow/temporal/cost/persistence/isolation/shutdown gates required.

Evidence reuse: Completed A–E phase evidence and all five explicit residual dispositions; integration never erases earlier failures. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Project open/create -> Scene -> primitives/material assignment -> move/rotate/scale/stretch -> duplicate/share/MakeUnique -> Scene Camera and Editor view -> Directional/Point/Spot and all three shadows -> visual/motion review -> IBL/emissive HDR/Bloom/exposure/tone/display -> bodies/colliders -> chain/hinge/limits/ragdoll -> render/pick/interact -> property transaction -> Undo/Redo -> save/unload/reload/references -> Play/simulate/Stop/authored-state verification -> import/reimport/safe replacement/notifications -> reference images and Lighting/Physics cost -> shutdown.

Manual acceptance: Required owner human acceptance of the whole maintained flow and no missing intermediate capability.

## Observable exit and review

Actual integrated flow passes with no normal consumer dependency on GL/GLAD/SDL/Assimp/GpuMesh/FrameResource/native programs/publication/raw EnTT/solver internals.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

