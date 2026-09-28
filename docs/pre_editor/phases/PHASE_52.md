# PRE_EDITOR Phase 52 — RBS admission and qualification of original C scaling residual

**PROPOSED / NOT_STARTED.** Milestone: F. No implementation or activation authorized by this file.

## Goal and traceability

Resolve or explicitly disposition original scaling qualification with comparable RBS evidence.

Canonical findings: PERF65-01. Master section references: 8, 14, 18.2 in [the master instruction](../POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](../HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 51 (must actually be approved and published before START).

Additional providing phases/baseline: [12](PHASE_12.md), [40](PHASE_40.md), [51](PHASE_51.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable decision: OD-12, applied in stages under [P0](../VALIDATION_CONTRACTS.md#performance-admission-p0). Before START, the owner approves bounded adapter/diagnostic scope, equivalence/lineage criteria, measurement protocol, original applicable thresholds and execution limits. Establish the required evidence during this phase; completed comparability proof is not a START prerequisite. Before comparative performance measurement, verify and explicitly admit comparability against those preapproved criteria for the exact identities/workload/boundaries. If it cannot be established, retain OPEN / INCONCLUSIVE and request explicit owner disposition at the declared stop; no weakened thresholds or indefinite retries.

## Bounded scope and source locators

Within the preapproved bounded scope, establish RBS adapter equivalence for original N/Q/dirty inputs, phase boundaries and requested-byte accounting. Review any evidence-supported protocol correction before comparison; verify and admit the resulting comparability before the single permitted complete paired batch. Preserve original applicable thresholds and every failed/inconclusive observation. Equivalence work does not itself authorize comparative measurement.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/src/Scene/RenderState.cpp](../../../GEngine/src/Scene/RenderState.cpp)
- [GEngine/src/Renderer/RenderExtraction.cpp](../../../GEngine/src/Renderer/RenderExtraction.cpp)
- [GEngine/src/Renderer/PassTiming.cpp](../../../GEngine/src/Renderer/PassTiming.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](../../../RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 0–3 production for required RBS scenario/measurement adapter, 1–3 protocol/evidence files. No optimization bundled.

Non-goals: No old standalone fixture execution, retry-until-pass, weakened memory cap or retrospective threshold change.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 RBS-F-Scaling; PERF65-01 exact 16.667 ms median/p95, 5% regression/qualification and A/A+B memory limits where admitted. If equivalent RBS inputs cannot be proven, report INCONCLUSIVE and seek explicit disposition.

Evidence reuse: Phase 65 raw complete batches and old allocation/correctness evidence only with matching identities; current A–E functional evidence. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: All new application performance through actual maintained RBS workload, not a substitute demo.

Manual acceptance: Owner approves scope/criteria/protocol/thresholds/execution limits before START; comparability evidence is verified and admitted before comparative measurement; the owner makes the final residual gate decision.

## Observable exit and review

Original scaling residual closes with qualified matched evidence or remains technically OPEN under explicit bounded owner acceptance for handoff.

Execution stops after candidate validation and compact seal for HUMAN REVIEW under C0 and the later activated workflow. No self-approval, commit/tag/push or next phase during execution. Ordinary approval verifies the sealed identity/evidence without routine reruns; publication authorization and protected/index/content checks remain mandatory. This proposal creates no receipt, seal or approval state.

