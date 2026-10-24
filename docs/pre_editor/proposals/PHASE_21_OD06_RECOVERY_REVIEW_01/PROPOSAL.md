# Phase 21 OD-06 recovery review 01: one owner decision

**PROPOSED ONLY. No activation, implementation, build, native launch or Phase 21 approval.**

Recommend accepting [prospective contract amendment 03](CONTRACT_AMENDMENT.md)
and the bounded continuation authority below as one decision. Change only the
fixed-lattice dynamic sphere/sphere recovery budget to 200 ms. Keep Physics
frozen. Retain the two attribution findings as blocking required evidence, to be
resolved within the already mandatory qualification work; no exception is proposed.
This package alone grants no execution authority. Current 100 ms enforcement,
stopped qualification authority 02 and the consumed diagnostic authorities remain
unchanged.

## Evidence and gameplay tradeoff

The owner has accepted the completed 120-second, Release MT, 120 Hz, single-pass,
sleep-enabled diagnostic as evidence of stable observed rest for this exact
SphereLattice. It is supporting evidence, not formal qualification. During
110-120 seconds all 180 spheres were quiet and asleep in every sample, with zero
per-body drift, velocity, churn or contact-count change; maximum penetration was
0.0200018175 and energy was constant at 3237.7098484. Continuous sleep began at
24.5917 seconds. The historical 15-second lattice was not yet settled.

Pair 77/113, typed identities (78,78)/(114,114), exceeded 0.05 for 23 samples,
ticks 1306-1328: 191.667 ms sampled exposure. Its safe streak began at 1329 and
completed at 1358. The proposed 200 ms allowance leaves only 8.333 ms, one 120 Hz
sample, of margin. There is no demonstrated 60 Hz margin or repeatability claim.
Across the run, 222 episodes involved 182 pairs; 39 pairs recurred, with at most
three episodes per pair. The late pair had three episodes and 38 unsafe samples
in total. Individual episode compliance cannot establish overall stability.

**200 ms is an owner gameplay-risk budget, not a proven universal safe constant.**
It trades up to 24 fixed steps at 120 Hz or 12 at 60 Hz of bounded transient
embedding for the unchanged single-pass cost. Depth remains at most 10% of the
relevant collider dimension, capped at 0.20, and resting/sleeping depth remains
0.05. A 200 ms unsafe allowance plus 250 ms continuous safe confirmation totals
450 ms of sampled exposure/confirmation, shorter than the existing 500 ms sleep
dwell. That comparison is a gameplay rationale, not a guarantee that recovery and
sleep timers align. Visual acceptance remains required. Neither the observed
191.667 ms duration nor the 0.146690 depth peak proves a generally safe allowance.

P21-PEN-01 and P21-PEN-02 remain historical FAIL under their original criteria.
The diagnostic's late recovery and stable rest do not rescore either result.
Its incomplete attribution findings also remain INCONCLUSIVE.

## Two precise technical dispositions

Both entries below concern mandatory numerical/measurement evidence. Treat them
as **P1 gate blockers pending closure**, not demonstrated unique Physics defects
and not P2 deferrals. Residual owner: engine owner. Acceptance of the 200 ms
amendment is not acceptance of either finding. No P0 or unknown critical exposure
can be waived under the existing MSV addendum.

**P21-ANCHOR-01: retained-anchor versus geometry attribution.** Actual outcome:
INCONCLUSIVE. There were 2,394,925 discrepant samples over 697 pairs, maximum
absolute difference 0.004015839193016291, continuing through tick 14,400. The
existing 1e-5 reconstruction audit is not a penetration allowance. Approved GJK
witness bias/adjustments provide source context, not a verified explanation for
every difference. Preserved compact records lack the contact witness/normal
history needed to complete attribution.

Proposed disposition: keep the conservative End-state metric
`max(0, greatest retained-anchor depth, independent geometric overlap)` and both
raw values. In the already required Debug smoke, use the existing contact/body
and operation callbacks to retain bounded first/worst examples plus complete
classification counts for sphere/sphere and sphere/boundary discrepancies. The
minimum operands are the actual contributing pair's typed identities, End poses,
local/world anchors, normal and retained-anchor creation/refresh provenance where
needed. Reconstruct the reported anchor depth from those operands, independently
reconstruct geometric overlap, and explain their difference from the approved
measurement definitions and source. A source-supported explanation may cover a
class of contacts; it must account for every counted discrepancy class, not just
a favorable pair. Keep these checks and compact counts in subsequent cases.

Closure requires valid identities/geometry, reconstruction consistency and an
explicit explanation of the distinct measurements without subtracting a bias,
adding an acceptance epsilon or discarding the larger depth. Arithmetic agreement
alone does not establish correct contact normals, support or impulses; the existing
contact-correctness gates still apply. Unsupported geometry, a new/unexplained
class, missing provenance or an actual contact failure blocks dependent work.
If the existing callbacks cannot supply the operands within the two-file/cap
boundary, stop INCONCLUSIVE; no production hook or separate launch is proposed.
Revisit on any new discrepancy class, contact correctness failure or change to
geometry, collider scale, witness construction or observer boundary.

**P21-ENERGY-01: early local energy increase attribution.** Actual outcome:
INCONCLUSIVE. Energy rose by 1917.34794032414 at tick 151; ten positive steps from
149 through 159 added 5530.495513994594 over 83.333 ms. Global peak including the
initial state was 30240, below 30542.4, and the resting tail had no growth. These
facts bound the observation; they do not explain energy creation or prove local
numerical correctness.

Proposed disposition: reuse the approved Phase 20 formulas, tolerances and
unaffected operation-level correctness evidence with source/dependency/impact
proof. The minimum new evidence is a compact energy ledger from the existing
stage/operation callbacks during the already required Debug smoke: kinetic and
potential changes, gravity/external work, contact impulse work, integration,
position correction and sleep contributions. Account for the known early growth
window and retain full-smoke summaries with first/worst unexplained contributions;
do not collect an exhaustive operation trace. Subsequent mandatory cases retain
the same applicable checks and compact anomaly summaries.

Closure requires a supported explanation of the measured increase and compliance
with the applicable normal/friction/restitution/contact energy invariants, with
no sustained unexplained growth or severe local instability. Merely balancing an
energy ledger, remaining below the global ceiling or eventually sleeping is
insufficient. No new allowable energy-gain, growth-duration or residual tolerance
is proposed. If an explanation needs a new physical assumption, tolerated defect
or threshold, stop with the specific finding; do not call it PASS. Revisit on
unexplained positive work, recurrence not covered by the attribution, resting
energy growth, an invariant violation or a changed timestep/material/solver/input.

These are two bounded evidence-closure tasks inside the existing matrix, not a
renewed penetration root-cause campaign or solver-remediation proposal. Freeze
the exact formulas, reused tolerances, record layout and capacity proof before
implementation/measurement. The first planned Debug process may collect this
missing evidence, but it earns no PASS and unlocks no Release work until both
findings and its other gates close. Any observed hard/P0/P1 failure retains the
original stop behavior. Insufficient evidence stops the batch; there is no
standalone diagnostic, automatic retry or implicit exception.

## Minimum remaining work and requested authority

No Phase 21 matrix slot has a complete applicable formal PASS. Reuse the completed
long-tail diagnostic for risk justification and stable-rest support only. Reuse
unchanged Phase 20 production, low-speed/impact, friction/spin, resource and
operation correctness evidence only for its original matched claims. No replay
of that historical campaign is proposed.

A distinct **qualification authority 03** must explicitly authorize the amended
candidate: at most the seven native attempts below, one per slot, including one
new affected Debug smoke attempt and the six uncompleted Release slots. These are
up to seven additional attempts, not seven total historical attempts. Five prior
launches remain charged; the proposed maximum would be twelve cumulative phase
launches. Do not modify, reopen or reuse authority 02 or either consumed diagnostic
attempt. Do not present the unexecuted slots as already authorized by this proposal.

| Order / slot | Mandatory remaining work | Process ceiling |
| --- | --- | --- |
| 1. Debug MTd smoke | 15 s lattice at 120 Hz; 20 s stack at 60 Hz; 10 s wake. Close both attribution findings and functional/lifecycle gates. | 180 s |
| 2. Release MT prefix | 15 s minimal-observation lattice at 120 Hz, with safety checks. | 90 s |
| 3. Release L120 | 120 s / 14,400 ticks, one pass, sleep enabled; all amended gates and tail; paired state-hash/overhead comparison to prefix. | 240 s |
| 4. Release L60 | 120 s / 7,200 ticks, one pass, sleep enabled; independent amended qualification. | 240 s |
| 5. Release small | 20 s stack at each 60/120 Hz and 10 s wake; support, quiet fractions, next-tick wake and lifecycle. | 180 s |
| 6. Release LA | 120 s at 120 Hz, one pass, sleep disabled; required OD-07 sensitivity/cost characterization, not matching-rest qualification. | 240 s |
| 7. Release LP | 120 s at 120 Hz, two passes, sleep enabled; required OD-07 sensitivity/cost characterization, not matching-rest qualification. | 240 s |

This is the original 590 simulated seconds, without an extra diagnostic. LA/LP
cannot rescue failed defaults and retain applicable safety guards. OD-07 remains
current cost characterization, not an unmatched regression PASS; PHY-03 remains
open for Phase 25. Owner RBS visual acceptance is still required after successful
automated work; this proposal supplies no human PASS or Phase 21 approval.

Proposed implementation ownership is exactly the existing two supporting files:
`RigidBodySimulation/tests/RestingContactDiagnosis.h` and
`tools/pre_editor_phase21_qualification.py`. Implement the type-specific budget,
new authority isolation and bounded evidence closure using existing callbacks.
Freeze criteria/inputs before validation. Run focused static checks of pair-type
dispatch, recovery deadline/confirmation, recurrence and hard-stop behavior.
Compile only affected RBS in Debug MTd and Release MT, at most one build command
per configuration; reuse matching GEngine/glad/runtime dependencies. A changed
compiled header requires fresh RBS products; the historical diagnostic executable
cannot acquire the new policy by relabeling it. Do the Release build only after
Debug closes its gates. No full-solution rebuild, other application, new observer
framework or production change is proposed.

| Shared accounting | Used, retained | Remaining, not reset |
| --- | --- | --- |
| Build wall time | 461.720 / 2400 s | 1938.280 s |
| Native wall time | 325.596 / 1500 s | 1174.404 s |

Retain 1200 s per build command and the process ceilings above. Effective deadlines
are the smaller of the applicable process ceiling and remaining aggregate budget;
all attempts and wrapper time stay charged. The seven native ceilings sum to
1410 s, more than the remaining 1174.404 s: completion is conditional on actual
runtime, not guaranteed by spare launch slots. Preflight capacity before each
attempt and stop on exhaustion; no aggregate increase, clock reset, shortened
workload, discarded sample, retry or extra launch is included.

Retain pair allocation <=2 MiB within recorder <=8 MiB, total named storage
<=64 MiB, pair evidence <=8 MiB within total exports <=64 MiB/process. Prove the
bounded summaries/packets fit before launch. Preserve raw/derived evidence,
failed candidates, full identities, owner source and index. Use existing isolated
OUTPUT_PATHS. Scope/capacity failure stops with one consolidated owner decision.

## Decision and phase implications

Accept this package as one prospective policy-and-continuation decision only if
the owner accepts the 200 ms fidelity risk and the precise bounded authority above.
After explicit acceptance, record amendment 03 activation and distinct authority
03, binding this package's hashes, current usage floor and historical failures.
Only then may the admitted supporting implementation/build/qualification proceed.
No P1 exception is bundled: either attribution gate remaining unresolved blocks
Release continuation/READY under the MSV addendum. Any demonstrated P0 always blocks.

Phase 21 stays NOT READY until all applicable new qualification, technical and
owner visual gates are satisfied. Phases 26-29 stay blocked by the core-stability
gate until the normal workflow resolves it. This fixture-specific gameplay budget
is no safety guarantee for joints, ragdolls, other scales/materials or future scenes,
and supplies no automatic next-phase authority. Physics algorithms, one-pass
default, materials and sleep policy remain frozen. No approval, seal or publication.

Evidence: [diagnostic handoff](../../../../logs/pre_editor/phase-21/stop-04/handoff.json),
[diagnostic review](../../../../logs/pre_editor/phase-21/stop-04/diagnostic-review.json),
[complete existing analysis](../../../../logs/pre_editor/phase-21/analysis/Release-long-tail-01-01/analysis.json).
Exact identities, unchanged controlling records and static validation are bound in
[manifest.json](manifest.json).
