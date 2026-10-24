# Phase 21 OD-06 penetration policy: one owner-decision proposal

**PROPOSED ONLY. NO THRESHOLD ACTIVATION OR EXECUTION.**

Recommendation: accept the accompanying
[prospective contract amendment 02](CONTRACT_AMENDMENT.md) as one bounded
owner-directed policy decision for future Phase 21 candidates. The present step
creates proposal documents only. The current all-time 0.05 gate, NOT READY state,
and consumed diagnostic authorization remain unchanged.

## Proposed numerical decision and rationale

| Item | Proposal | Engineering/gameplay rationale |
| --- | --- | --- |
| Dynamic depth | At most 10% of the smaller relevant full collider dimension; absolute maximum 0.20 | Sets a shape-scale compression budget rather than a number fitted to a measured peak. The unchanged unit spheres allow 0.20; thickness-1 boundaries allow only 0.10. Thin geometry cannot inherit a wall's long span. |
| Consecutive exceedance/return | At most 100 ms above the 0.05 rest limit; recovery streak must begin within that same deadline | Limits embedding to a short impact interval: 6 fixed updates at 60 Hz or 12 at 120 Hz, five times shorter than the existing 0.5 s sleep dwell. It is a gameplay responsiveness target, not a claim that six visible frames are imperceptible. |
| Recovery confirmation | At most 0.05 for 250 ms continuously, with no resetting the original episode clock | Requires 15/30 completed low-penetration samples; a single favorable frame or disappearing manifold cannot establish recovery. This is half the existing sleep dwell and does not itself prove rest. |
| Settled rest | Preserve 0.05; preserve 0.035 for existing small-fixture support gates | Static support and sleeping contact quality retain their stricter requirements. |

These are explicit proposed owner fidelity/risk budgets, not measured safe limits,
industry constants or promises that the approved solver will pass. The 10% depth
budget limits one linear dimension of transient shape interpenetration. For equal
radius-1 spheres at the 0.20 cap, the geometric intersection lens is 1.45% of one
sphere's volume: `(6*r-d)*d*d/(16*r*r*r)`. This geometric bound motivates a limited
impact allowance; it does not bound impulse error, energy creation, visibility
or gameplay consequences. Those remain independently guarded. The dynamic cap
is an upper bound, not permission to consume the whole depth/time budget in every
collision. A deep event must also recover quickly; partial correction alone is
not assumed to meet that deadline.

The 10%, 100 ms and 250 ms choices are a proposed game-scale compression and
response budget. They are not derived by rounding up the observed 0.146690 peak
or its logged duration. The historical peak is context only. A future candidate
can still fail on thickness-scaled depth, per-pair duration, recovery, rest,
energy or sleep/wake behavior even if its global peak is below 0.20.

## Evidence used and limits already established

Use the completed [causal review](../../../../logs/pre_editor/phase-21/stop-02/causal-review.json)
and [diagnostic handoff](../../../../logs/pre_editor/phase-21/stop-02/handoff.json);
no repeated root-cause investigation is required. Tick143 demonstrates real
sphere-pair compression from approximately 0.10 before PostSolve to 0.08 afterward.
The unchanged partial correction explains that immediate reduction; it does not
establish harmlessness, self-recovery or a unique production remedy.

The preserved 15-second aggregate trajectory peaks at 0.146690, records recurring
exceedances, and ends with only three sleeping bodies. It was not yet settled.
Aggregate return below 0.05 is not pair-resolved recovery evidence. The missing
pair evidence will be obtained in the already planned L120 qualification after
policy acceptance, rather than in another standalone diagnostic campaign.
No Phase20 approval or earlier FAIL/INCONCLUSIVE result is reclassified.

## Scope, feasibility and remaining authority

After explicit package acceptance only, implementation would remain confined to
`RigidBodySimulation/tests/RestingContactDiagnosis.h` and
`tools/pre_editor_phase21_qualification.py`. Reuse current shape inputs, typed IDs,
End observer, fixed scheduler and existing output controls. Physics algorithms,
default solver passes, materials, sleep policy, canonical RBS and owner main.cpp
stay fixed. Preserve the old consumed diagnostic record and its one-run guard;
a new accepted qualification authority must be recorded separately, never inferred
from spare budget or by deleting the guard.

Static capacity design: the fixed fixture has 16,110 sphere/sphere plus 900
sphere/boundary pairs = 17,010 possible relevant keys. A <=64-byte state entry
for every key needs 1,088,640 bytes; a compact pending-key list and bounded buffers
fit within the proposed 2 MiB incremental allocation. With the recorded current
1,311,104-byte Phase21 recorder, that is below its existing 8 MiB allowance.
Only retained/pending pairs need per-tick work; no all-pairs collision scan.

The first-two-second trace of 21 known pairs needs 5,040 records at 120 Hz;
at a proposed <=192 bytes/record it uses <=967,680 bytes. Reserve up to 8 MiB
inside the existing 64 MiB export cap for these records, all additional excursion/
recovery records, summaries and any first-failure packet. This is a bounded
implementation design, not a measured guarantee that a 120-second run fits or
finishes; actual sizes, complete coverage and all runtime limits must be checked.
Do not claim PASS on overflow or silently omit an episode to finish the run.

Current accounting remains 247.032/2400 build seconds and 83.361/1500 native
seconds (three previous launches). Remaining allowances are 2152.968 and
1416.639 seconds. The 1200-second build command, 240-second long-case process,
180-second Debug/small-case and 90-second prefix deadlines stay unchanged.
No new launch slot/campaign, production file, observer framework or budget reset.
Affected Debug/Release supporting-code validation follows the existing MSV matrix
with matching engine/glad and unaffected evidence reuse; no broad requalification.
If scope or capacity cannot be met, stop with that concrete issue.

## One consolidated owner decision

Accept this exact proposal and amendment together, including their numerical
budgets, named prospective treatment of P21-PEN-01, bounded observation within
planned L120 and unchanged safety/resource constraints, and authorize continuation
of the existing START under those terms. One explicit acceptance can settle both
the governance amendment and the admitted supporting implementation; no second
standalone diagnostic approval or extra START is proposed.

Before any later qualification, record acceptance/activation with package hashes,
freeze the affected acceptance matrix and inputs, and preserve all prior evidence.
Until that acceptance is supplied, do not implement or activate the new criteria,
build, launch, or modify active acceptance/state pointers. Policy acceptance is
not Phase21 approval and does not establish long-duration stability. Only a future
qualified candidate can reach owner RBS review; explicit Phase21 APPROVE still
controls the unchanged seal/publication workflow.
