# PRE_EDITOR Phase 48 — Constraint and ragdoll persistence extensions

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: E. No implementation or phase activation authorized by this file.

## Goal and traceability

Persist required joints/ragdoll descriptions using the same versioned metadata.

Canonical findings: AUT-03, PHY-04, PHY-05. Master section references: E.4, C.9, C.10 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 47 (must actually be approved and published before START).

Additional providing phases/baseline: [26](PHASE_26.md), [27](PHASE_27.md), [28](PHASE_28.md), [29](PHASE_29.md), [47](PHASE_47.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-13. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Serialize body references, anchors/axes, distance/ball/hinge/limits, filters and passive ragdoll hierarchy/bone mapping; restore after body resolution with deterministic validation and rollback. No solver impulses/runtime pointers persist unless an explicitly separate runtime snapshot contract is approved.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Physics/PhysicsWorld.h](/GEngine/include/GEngine/Physics/PhysicsWorld.h)
- [GEngine/include/GEngine/Physics/Constraints/Constraint.h](/GEngine/include/GEngine/Physics/Constraints/Constraint.h)
- [GEngine/src/Scene/_Scene.cpp](/GEngine/src/Scene/_Scene.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Proposed new/earlier-phase design slots: Extend Phase 46/47 metadata/serialization adapters and Phase 29 description only.

Change budget and reason: accepted 4-7 production (formerly 3-5), 1-3 missing-reference/round-trip checks/docs; motor/mover description adapters share this owner.

Non-goals: No independent Physics serializer, animation state, active ragdoll or runtime solver snapshot format.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-E round-trip chain, hinge/limits and ragdoll descriptions; stale/missing/wrong-body references, invalid limits, unknown versions and partial-load failure; then RBS-C brief representative behavior under reused accepted criteria.

Evidence reuse: C joint/ragdoll behavior and Phase 47 transaction-free load replacement; do not rerun all Physics performance cases without changed behavior. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Reload the maintained chain/hinge/ragdoll and verify semantic/debug identity.

Manual acceptance: Review restored assemblies and missing-reference diagnostics.

## Observable exit and review

Persistent restored joints/ragdoll preserve authored constraints/filtering/mapping and deterministic typed recovery.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Milestone C successor obligations

Additional providers: [30](PHASE_30.md), [31](PHASE_31.md), [32](PHASE_32.md). Extend the existing constraint persistence owner to motor enable/target/cap and mover descriptor/initial phase. Restore after body resolution, validate units/references and rollback partial load; runtime solver impulses and handles remain excluded. The single accepted roadmap budget above includes these added descriptors.

## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_45.md`, SHA256 `14eeb8da570dc82fc8e0537e3285d2f881b423e257128060d3a489dbf00ee000`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
