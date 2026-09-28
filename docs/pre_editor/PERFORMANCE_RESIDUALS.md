# Rendering performance residuals

**PROPOSED / NOT_STARTED.** Technical status of all five original receipt families remains **OPEN**. Their historical gate status remains **ACCEPTED_OUT_OF_SCOPE** under the [Phase 65 owner closure amendment](../rendering/reviews/PHASE_65_OWNER_CLOSURE_AMENDMENT.md) and [Phase 68 validation amendment](../rendering/reviews/PHASE_68_OWNER_VALIDATION_AMENDMENT.md). Acceptance of that carry-forward did not fix the issues, change thresholds, or authorize another campaign.

This is the canonical expanded record for the five original IDs. PRE_EDITOR phase ownership/gates below are proposals. No performance measurements were taken in this audit. Relative improvements, correctness evidence and failed/inconclusive results retain their separate meanings.

## Evidence lineage and limits

| Record | Immutable identity / role |
| --- | --- |
| Phase 13 | Commit 0751c1405661a3b7da7b20a94547bf133be42087; historical absolute comparison reference |
| Phase 48 | Commit 3dc46bc473516077e233856176f3e246e733aa23; timestamp/CPU evidence and qualification limits |
| Phase 59 | Commit a39bc2290ff53d234d8676cc54a77068f61939c0; culling versus GPU tail observation |
| Phase 64 control | Commit 01064633df6018af847fb901327e3911cec97872 |
| Phase 65 checkpoint | Commit 342ad6f4350ec6230eeff8cd42b9fa72535e5296; tracked rendering-checkpoints/phase-65.json retains original IDs |
| Phase 68 / PRE_EDITOR entry | Commit 9995074e7af161db76c5bf79a5ede030702fb4ca; matching final functional evidence, no fresh closure of historical performance |
| Final Phase 65 disposition | PHASE_65_FINAL_REVIEW.md and owner closure amendment supersede the older BLOCKED iteration review |
| Technical packet | docs/rendering/reviews/PHASE_65_CLOSURE_PACKET.md; failed and qualified series are both retained |
| Run identities | logs/rendering/phase65/owner-closure/final-identity.json; archived engine Release library SHA-256 90f83808f7bb441e0eb71d01e21445a5e6f619ffd3b2faf1c687c18b77733a24; RBS executable 259cee11efabd8e4845aae5ec23f3df43687cc01ec8a3b8227587f6c49886d11 |
| Source mismatch to current | Phase 65 RBS source 3088cfb02d90dc7deb274e855a0723f398914fda4f0af025191d86d4fdc7b75a differs from current Phase 68 source; do not present Phase 65 measurements as current executable performance |

Phase 13 used RBS at 1280x720, VSync off, 16 ms pacing, 120 warm-up frames, 240 samples, three processes, zero-delta/Physics-off conditions, Ryzen 9 5900HX / 16 logical CPUs / 16 GB, RTX 3070 Laptop 8192 MiB, NVIDIA 552.44, OpenGL 4.6. Original render CPU median was 0.958 ms. Its six directional layers plus six point faces at 4096 represent 768 MiB of texture payload, not measured VRAM residency. Its image comparison was a same-GPU diagnostic (at most 0.5% scene-region difference; maximum observed 0.282%), not a general new-PBR visual criterion.

Phase 48's historical 120/240/three-run protocol also included a separate 64x64 fixture with 256 shadow maps and four query-drain frames. Eight of 41 series exceeded its 10% spread rule. GPU query elapsed time includes gaps/stalls; it is not GPU busy time. UI/present was CPU-only. Neither fixture nor its numeric qualification rule silently transfers to a new RBS lighting workload.

Retained Phase 65 changes include cached shadow preparation, a build-boundary repair, and PersistentRenderWorld revision 2's fixed 128-entry high-Q compaction pages. O(N) exact observation and O(Q) frame binding remain. Rejected material memoization, C2 packing and extra workers were not retained.

## PERF65-01 — C scaling qualification

- **Category / severity / priority:** MILESTONE_F_HANDOFF (performance) / high / REQUIRED_BY_OWNER_PHASE, Phase 52; no demonstrated pre-A entry blocker.
- **Technical / historical gate:** OPEN; C batch FAILED/UNQUALIFIED even though its numerical inequalities passed; ACCEPTED_OUT_OF_SCOPE historically.
- **Evidence:** logs/rendering/phase65/render-world/scaling/{RESULTS.json,summary.json,STOP*}; scaling-qualification/RESULTS.json; closure packet. Eight of 14 paired cases failed the 5% run-median spread qualification: candidate seven cases/18 metrics, control one case/two metrics.
- **Known:** variability prevents accepting the batch; no specific cause was established. Do not select favorable runs or call numerical budget success a qualified PASS.
- **Unproven:** whether scheduling, workload composition, cache state, measurement boundaries or implementation explains variability.
- **Original criteria retained:** N=8192 steady and 1%-dirty CPU median AND p95 at most 16.667 ms; no more than 5% small/medium full-dirty regression. High-Q requested-byte gates: persistent A at most 16 MiB and A+B at most 32 MiB for N=8192, Q=1/3724/7448. Recorded Q=7448 A=16,411,462 bytes and A+B=29,506,002 bytes; only 365,754 bytes of A headroom. C/D/E transient peaks are reported separately; inclusive peak 45,159,945 requested bytes is not RSS.
- **Smallest next diagnostic:** Before Phase 52 START, approve bounded RBS adapter/diagnostic scope, equivalence criteria, measurement protocol, original applicable thresholds and execution limits. Establish N/Q/dirty workload, counter/input and cost-boundary equivalence during the phase. Verify and explicitly admit comparability before the single complete paired batch; review any protocol correction before comparison. Old standalone fixtures are not authorized application-level execution. If equivalence cannot be established within the bound, retain OPEN / INCONCLUSIVE and request explicit owner disposition; no weaker thresholds or indefinite retries.
- **Proposed owner / gate:** Phase 52, BLOCKS_CORE_EDITOR_HANDOFF. OD-12/P0 separates approved entry scope/criteria/protocol/limits from in-phase equivalence evidence and comparability admission before comparative performance measurement.
- **Closure:** a qualified matched complete batch satisfies every unchanged CPU/memory/qualification inequality, or a separately recorded owner acceptance explicitly leaves the finding technically OPEN with bounds and rationale. Changing the historical target requires a separate budget decision; it cannot be presented as meeting the old target.

## PERF65-02 — Historical absolute RBS CPU budget

- **Category / severity / priority:** MILESTONE_F_HANDOFF (performance) / high / REQUIRED_BY_OWNER_PHASE, Phase 53.
- **Technical / historical gate:** OPEN / ACCEPTED_OUT_OF_SCOPE.
- **Evidence:** closure packet and high-q-task-harness-continuation/rbs/summary.json, paired inputs/build identities.
- **Known:** qualified control/retained render medians were 2.33790/1.44025, 2.32345/1.42710 and 2.29735/1.46640 ms. Savings were 38.40%, 38.58%, 36.17%. Corresponding p95 pairs were 2.5844/1.7298, 2.5232/1.6756 and 2.6484/2.3167 ms. This is valid relative evidence for those pairs.
- **Still unmet:** original absolute render median/p95 limits 1.05755/1.92696 ms and work median/p95 limits 1.14598/2.11812 ms. Render and work median gates remain exceeded. The older roughly 2.283 to 3.5–3.6 ms drift has no sufficiently qualified causal ledger.
- **Unproven:** source/build/workload comparability across all historical steps and at least 80% attribution of the historical increase.
- **Smallest next diagnostic:** Before Phase 53 START, approve the bounded lineage diagnostic scope, equivalence/lineage criteria, measurement protocol, original applicable thresholds and execution limits. During the phase map immutable source/build/input/metric definitions and shared versus changed work, jointly with PERF65-05-06. Verify the resulting lineage and admit exact comparability before comparative RBS performance measurements may contribute to attribution. Missing comparability retains OPEN / INCONCLUSIVE for explicit owner disposition; no threshold weakening or indefinite retries.
- **Proposed owner / gate:** Phase 53, BLOCKS_CORE_EDITOR_HANDOFF; no pre-A optimization implied.
- **Closure:** meet applicable unchanged absolute limits with qualified comparable RBS evidence and satisfy attribution where required. If comparability cannot be established, retain OPEN/INCONCLUSIVE and ask for an explicit disposition or separately proposed future budget. Never relabel a relative speedup as historical absolute closure.

## PERF65-03 — Asynchronous upload scheduler stall

- **Category / severity / priority:** MILESTONE_A (performance) / high / REQUIRED_BY_OWNER_PHASE, Phase 12; directly relevant to A resource editing.
- **Technical / historical gate:** OPEN / ACCEPTED_OUT_OF_SCOPE.
- **Evidence:** tools/FINAL_PERFORMANCE_VALIDATION.md, Phase 65 async evidence linked from its closure packet. Every originally measured texture request exceeded the 16.667 ms scheduler-stall limit; original maximum 30.764 ms. Supplementary driver-attribution measurements were different/noisier and sometimes worse; they do not replace the original result.
- **Known:** bounded queued CPU payload and a scheduling budget cannot interrupt one indivisible driver upload. Cancellation/readiness/publication semantics and owners already exist.
- **Unproven:** which operation or driver behavior dominates matched current RBS stalls, and whether staging/fences/mip sequencing would improve it within memory and correctness limits.
- **Smallest next diagnostic:** Phase 12 embeds the exact asynchronous request sizes/formats/content identities and queue state in RBS, measures decode/enqueue/wait/owner-thread upload/publication separately with observer-overhead control. Retain failed loads, cancellation and old-frame validity.
- **Proposed owner / gate:** Phase 12, BLOCKS_MILESTONE_EXIT:A for its adopted async authoring operation. Request identity, comparable scheduler boundary and payload cap are OPEN_DECISION before START; the historical 16.667 ms limit is not silently weakened.
- **Closure:** qualified applicable request cases meet the unchanged stall criterion, typed completion/cancellation and memory/lifetime conditions, or an explicit owner carry-forward decision names a bounded limitation for A. A discovered implementation remedy requires a bounded approved scope amendment; it is not authorization for an upload rewrite in a diagnostic phase.

## PERF65-04 — GPU / UI / shadow tails

- **Category / severity / priority:** MILESTONE_D_LIGHTING (performance) / high / REQUIRED_BY_OWNER_PHASE, Phase 40, with evidence collected by 32–39.
- **Technical / historical gate:** OPEN / ACCEPTED_OUT_OF_SCOPE.
- **Evidence:** docs/rendering/POST_RENDERING_PERFORMANCE_AUDIT.md; Phase 59/65 tail evidence. Historical culling reduced GPU shadow median approximately 1.48 to 0.031 ms while p95 increased about 1.49 to 3.05 ms. Casters 128 -> 32, layer submissions 704 -> 42, serial draws 144 -> 48 and instanced draws 5 -> 3; exact image/depth comparisons passed.
- **Known:** less submitted work did not establish improved tail latency. CPU UI/present timings are not GPU UI attribution.
- **Unproven:** reproducible tail cause, GPU busy versus elapsed gaps, and applicability to the future full PBR/IBL/HDR/Bloom pipeline.
- **Smallest next diagnostic:** one matched RBS cached/dirty-shadow pair with delayed GPU-query completeness, CPU submission/presentation and observer-cost records; camera/light/caster sequences are separately identified. Reuse existing culling. No speculative BVH or quality reduction.
- **Proposed owner / gate:** Phase 40, BLOCKS_MILESTONE_EXIT:D. Absolute CPU/GPU budgets, p99 sample sufficiency, dynamic artifact tolerance and practical default profile remain OPEN_DECISION before affected phase STARTs; a screenshot cannot decide them.
- **Closure:** reproducibly quantified median/p95/p99 and missing-query rate under the reviewed quality profile, causal disposition of relevant anomalies, owner visual/temporal acceptance and accepted cost limits. A remaining tail limitation needs explicit owner acceptance with bounds; technical OPEN is retained.

## PERF65-05-06 — Historical lineage and causal attribution

This is the original combined receipt family. Closure-packet subfacets PERF65-05 (comparison/attribution) and PERF65-06 (retained CPU work) remain attached here, not new duplicate findings.

- **Category / severity / priority:** MILESTONE_F_HANDOFF (performance) / high / REQUIRED_BY_OWNER_PHASE, Phase 53, shared evidence ledger with PERF65-02.
- **Technical / historical gate:** OPEN / ACCEPTED_OUT_OF_SCOPE.
- **Evidence:** closure packet, historical Phase 13/42 comparison notes and frozen-frame attribution records, Phase 65 rejected prototype results.
- **Known:** source/build/workload lineage gaps leave historical comparisons unqualified. Frozen-frame replay gives a gross bound, not causal attribution. Retained O(N) observation and O(Q) binding remain. Rejected material memoization saved about 8.2–8.8% against a 10% retention criterion and was rolled back.
- **Unproven:** at least 80% causal attribution and which retained stage can be changed without weakening immutable snapshots or version/lifetime rules.
- **Smallest next diagnostic:** Use Phase 53's shared ledger under the same staged entry as PERF65-02: approve bounded scope, lineage criteria, protocol, original applicable thresholds and limits before START; establish the ledger during the phase; verify and admit comparability before comparative RBS stage measurement. Label observations comparable, bounded inference or unusable; missing evidence stays OPEN / INCONCLUSIVE and requires explicit owner disposition at the declared stop. No indefinite retries, threshold weakening or sum of savings from rejected/different prototypes.
- **Proposed owner / gate:** Phase 53, BLOCKS_CORE_EDITOR_HANDOFF. No standalone pre-A implementation or broad historical rerun.
- **Closure:** complete comparable lineage and at least 80% justified attribution where that historical criterion applies, followed by any separately admitted bounded optimization and its acceptance. Otherwise explicit carry-forward with rationale; no invented PASS.

## Cross-cutting acceptance and evidence ownership

[VALIDATION_CONTRACTS.md](VALIDATION_CONTRACTS.md) owns future execution bounds, identity manifests, insufficient-sample handling and stop rules. Its [P0 staged admission](VALIDATION_CONTRACTS.md#performance-admission-p0) governs Phases 52–53: entry authorizes the bounded diagnostic work, while comparability is an evidence gate before comparative measurement. All new application-level work uses RBS. Existing isolated evidence is read-only provenance; running old probes is not implicitly authorized.

Pre-A performance remediation has **zero phases** because inspection did not demonstrate an entry blocker requiring implementation there. The proposed gates above preserve all five families through A/D/F, and require owner review. Their eventual closure/acceptance must name the original ID, exact evidence, technical status and gate decision separately.


