# PRE_EDITOR Phase 56 — Historical CPU lineage and causal attribution

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone: F. No implementation or phase activation authorized by this file.

## Goal and traceability

Decide historical absolute CPU and attribution findings from one defensible evidence ledger.

Canonical findings: PERF65-02, PERF65-05-06. Master section references: 8, 14, 18.2 in [the master instruction](/docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md). Source evidence and provider semantics: [HANDOFF_AUDIT](/docs/pre_editor/HANDOFF_AUDIT.md); findings resolve in [FINDINGS](../FINDINGS.md) or [PERFORMANCE_RESIDUALS](../PERFORMANCE_RESIDUALS.md).

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 55 (must actually be approved and published before START).

Additional providing phases/baseline: [55](PHASE_55.md). Sequential predecessor approval is required even where additional dependencies name earlier providers. [Common contract C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0) applies in full.

Applicable decision: OD-12, applied in stages under [P0](../VALIDATION_CONTRACTS.md#performance-admission-p0). Before START, the owner approves bounded adapter/diagnostic scope, equivalence/lineage criteria, measurement protocol, original applicable thresholds and execution limits. Establish the required evidence during this phase; completed comparability proof is not a START prerequisite. Before comparative performance measurement, verify and explicitly admit comparability against those preapproved criteria for the exact identities/workload/boundaries. If it cannot be established, retain OPEN / INCONCLUSIVE and request explicit owner disposition at the declared stop; no weakened thresholds or indefinite retries.

## Bounded scope and source locators

During the admitted diagnostic work, map immutable source/build/input/metric identities through original Rendering Phase 13/42/65 evidence and PRE_EDITOR; classify comparable data versus bounded inference/unknown. This lineage ledger is a phase output. Verify it against the preapproved criteria and admit exact comparability before any necessary RBS comparative stage measurements; preserve the original applicable absolute budgets and at-least-80% causal attribution criterion.

Existing inspected locators (select exact modification members before START; these are not a blanket edit allowlist):

- [GEngine/src/Scene/RenderState.cpp](/GEngine/src/Scene/RenderState.cpp)
- [GEngine/src/Renderer/RenderExtraction.cpp](/GEngine/src/Renderer/RenderExtraction.cpp)
- [GEngine/src/Renderer/PassTiming.cpp](/GEngine/src/Renderer/PassTiming.cpp)
- [RigidBodySimulation/src/RigidBodySimulation.cpp](/RigidBodySimulation/src/RigidBodySimulation.cpp)

Change budget and reason: 0–2 production for a justified RBS measurement boundary, 1–3 lineage/evidence files. Discovered optimizations need separate scope amendment.

Non-goals: No sum of rejected prototype savings, new baseline as historical closure, broad checkout/run matrix or workload simplification.

## Invariants and required evidence

Preserve all C0 invariants, including typed recoverable errors/native isolation, the existing runtime resource/identity model, context-thread GPU ownership, immutable exact-version frames, single Physics scheduler, scoped changes and protected Git work.

V0/M0 admitted RBS historical/modern comparison only when identities/metric boundaries support it; original render/work median/p95 targets and attribution gates; complete raw runs and observer costs. Missing comparability is INCONCLUSIVE.

Evidence reuse: Canonical PERFORMANCE_RESIDUALS evidence lineage and qualified Phase 65 relative gains, without upgrading them to absolute success. Reuse requires source/build/input/settings identity; changed behavior receives focused evidence. All new application-level execution is RBS only; V0 defines other-app compile-only limits and stop rules.

RBS consumer coverage: Any fresh measurements exercise the maintained consumer; historical standalone evidence remains read-only.

Manual acceptance: Owner approves bounded diagnostic scope, lineage/equivalence criteria, protocol, original applicable thresholds and execution limits before START; comparability is verified and admitted before comparative measurement; the owner reviews the causal ledger and any explicit carry-forward disposition. Missing comparability remains OPEN / INCONCLUSIVE.

## Observable exit and review

Both original IDs receive traceable technical status and explicit handoff gate decision; unresolved history is visible and never fabricated PASS.

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.


## Immutable origin

Derived from original unstarted `docs/pre_editor/phases/PHASE_53.md`, SHA256 `14ced965047cb73b044c493bf8c659589c5a2d8c5fcce71b7991dad0e4f937ba`. That original is retained verbatim; approved/historical IDs in other namespaces are not remapped.
