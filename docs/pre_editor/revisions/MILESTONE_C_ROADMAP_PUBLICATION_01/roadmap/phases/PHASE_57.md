# PRE_EDITOR Phase 57 — Full RBS authoring-to-runtime integration

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: F. No implementation or phase activation authorized by this file.

## Goal and traceability

Demonstrate the complete Core Editor foundation through the maintained consumer.

Canonical findings: INT-01. Master section references: 14 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 56 (must actually be approved and published before START).

Additional providing phases/baseline: [12](/docs/pre_editor/phases/PHASE_12.md), [17](/docs/pre_editor/phases/PHASE_17.md), [29](PHASE_29.md), [43](PHASE_43.md), [54](PHASE_54.md), [55](PHASE_55.md), [56](PHASE_56.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-14. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Exercise the integrated sequence below using A–E APIs already adopted along the way. Only small integration glue or concrete fixes within the declared budget; a subsystem defect routes to its owner contract/amendment.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)
- [GEngine/src/Scene/_Scene.cpp](/GEngine/src/Scene/_Scene.cpp)
- [GEngine/src/Renderer/FrameScheduler.cpp](/GEngine/src/Renderer/FrameScheduler.cpp)
- [GEngine/src/Renderer/SceneRenderResources.cpp](/GEngine/src/Renderer/SceneRenderResources.cpp)

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

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Milestone C successor obligations

Additional providers: [30](PHASE_30.md), [31](PHASE_31.md), [32](PHASE_32.md). Include motor and mover authored description persistence, Edit/Play isolation, representative loaded contact, clean shutdown and accepted C conformance in the full RBS operation sequence. Startup preset selection alone is not proof of authoring round trip.

## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_54.md`, SHA256 `f3ec138fb8ceb94fa1e351cada2cf468b4d0b51d3c694fbe384efc20ac654942`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
