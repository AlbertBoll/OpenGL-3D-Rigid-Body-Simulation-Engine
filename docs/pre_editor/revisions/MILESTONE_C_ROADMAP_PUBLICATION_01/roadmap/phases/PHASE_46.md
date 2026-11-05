# PRE_EDITOR Phase 46 — Shared property metadata and structured authoring diagnostics

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: E. No implementation or phase activation authorized by this file.

## Goal and traceability

Provide one typed property/diagnostic foundation for persistence, transactions and future tools.

Canonical findings: AUT-03. Master section references: E.7, E.14, 18.1 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 45 (must actually be approved and published before START).

Additional providing phases/baseline: [09](/docs/pre_editor/phases/PHASE_09.md), [13](/docs/pre_editor/phases/PHASE_13.md), [29](PHASE_29.md), [43](PHASE_43.md), [44](PHASE_44.md), [45](PHASE_45.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Reuse A material/geometry, B camera and C/D semantics in typed property descriptors: identity, type, validation, defaults and ownership. Define lightweight severity/subsystem/code/message plus persistent entity/asset/source context projections over existing typed errors; no giant manager.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Material/MaterialTemplate.h](/GEngine/include/GEngine/Material/MaterialTemplate.h)
- [GEngine/include/GEngine/Component/RenderComponents.h](/GEngine/include/GEngine/Component/RenderComponents.h)
- [GEngine/include/GEngine/Scene/SceneError.h](/GEngine/include/GEngine/Scene/SceneError.h)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Shared property metadata and diagnostic header/source slots; extend adapters incrementally in 47–52.

Change budget and reason: 3–6 production, 1–3 schema/diagnostic checks/docs. The core plus representative adapters, not all subsystem rewrites.

Non-goals: No separate reflection per subsystem, Inspector UI, arbitrary scripting schema or exception adapter.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E metadata-driven access/validation for representative material/geometry/camera/light/body/scene values; typed error retains original cause and relevant persistent/source context; enum/range/reference validation.

Evidence reuse: Existing typed Shader/Material/Mesh/Physics errors and Phase 09/33 schemas; early A–D diagnostics remain owned by their subsystem. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Inspect/edit representative properties through Engine metadata and display contextual errors in existing diagnostic UI.

Manual acceptance: Review shared schema and useful diagnostic context.

## Observable exit and review

One schema can serve serialization/transaction/Copy-Paste/future Inspector without knowing implementation details; diagnostic structure is demonstrated.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Milestone C successor obligations

Additional providers: [30](PHASE_30.md), [31](PHASE_31.md), [32](PHASE_32.md). Expose authored motor parameters and mover trajectory descriptors through the same property metadata; never expose solver cache/internal impulses as editable state.

## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_43.md`, SHA256 `723ba090e1e2ce36e3d1f2ea9ae5fc473895acb451df50a7c0fd2c17e1cc69ad`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
