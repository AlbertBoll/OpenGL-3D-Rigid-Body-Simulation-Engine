# PRE_EDITOR Phase 33 — Shared material/light semantics and GPU representation

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: D. No implementation or phase activation authorized by this file.

## Goal and traceability

Stabilize one Direct PBR material/light contract and revision-driven GPU packing.

Canonical findings: LIT-01, API-04. Master section references: D.1, D.2, D.3, D.4, D.7, 18.1 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 32 (must actually be approved and published before START).

Additional providing phases/baseline: [09](/docs/pre_editor/phases/PHASE_09.md), [16](/docs/pre_editor/phases/PHASE_16.md), [32](PHASE_32.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable unresolved decisions before affected START/gate: OD-09, OD-11. These resolve in the shared decision register; this proposal does not pretend they are settled. Baseline plus earlier providers satisfy architectural dependency order; future E work is never a hidden A–D prerequisite.

## Bounded scope and source locators

Extend A's typed material schema, define Directional radiance/direction, Point intensity/range/attenuation and Spot direction/inner-outer cone/falloff units; consistent local-to-world transforms and finite validation. Pack stable light data with explicit capacity/overflow and dirty revisions, not per-frame hidden allocations.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/include/GEngine/Component/RenderComponents.h](/GEngine/include/GEngine/Component/RenderComponents.h)
- [GEngine/include/GEngine/Material/MaterialTemplate.h](/GEngine/include/GEngine/Material/MaterialTemplate.h)
- [GEngine/include/GEngine/Renderer/RenderFrame.h](/GEngine/include/GEngine/Renderer/RenderFrame.h)
- [GEngine/src/Renderer/RenderExtraction.cpp](/GEngine/src/Renderer/RenderExtraction.cpp)
- [GEngine/src/Renderer/FrameSubmission.cpp](/GEngine/src/Renderer/FrameSubmission.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 4–6 production, 1–3 semantic/layout/revision checks/docs.

Non-goals: No second material model, Area Light, many-light algorithm or new shadows in this phase.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-D-Reference isolated/mixed lights, zero/invalid/range/cone/capacity values; CPU-to-GPU packing and revision invariants, transform directions and no stale data. D0 applies.

Lighting-specific evidence: Initial profile records existing High4096 behavior; preparation CPU, bytes/uploads and repeatability baseline. No new visual model accepted merely by packing success. See [D0](../VALIDATION_CONTRACTS.md#lighting-envelope-d0) for mandatory quality, memory, motion, tail and human acceptance dimensions.

Evidence reuse: Existing semantic three-kind extraction, approved one-of-each capacity/error and A material declarations. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Create/edit all three light types using A/B semantic APIs.

Manual acceptance: Review light/material units, supported capacity and reference configuration.

## Observable exit and review

Typed CPU/ECS/frame/shader expectations agree on units/fields/capacity; D shader work can implement them without changing the public model.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_30.md`, SHA256 `0c577b466b15f120f3cfbcf451a79d78bb5b34ad21b16c8767e68dc70620fba4`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
