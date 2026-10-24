# Proposed Phase 21 prospective OD-06 contract amendment 03

**PROPOSED ONLY. NOT ACCEPTED, ACTIVE OR IMPLEMENTED.**

Normative part of [recovery review 01](PROPOSAL.md). The currently accepted
amendment 02 continues to enforce 100 ms for both pair classes. Owner acceptance
of stable observed diagnostic rest does not activate this amendment.

## Exact replacement and scope

For future qualification of the unchanged SphereLattice input only (SHA-256
`79eb5a76b06726b77333fff93ddb47999671df025ef1c75322494b8ac62116c6`), replace the
sphere/sphere recovery-budget entries in section 3 of accepted amendment 02:
`docs/pre_editor/proposals/PHASE_21_OD06_PENETRATION_REVIEW_01/CONTRACT_AMENDMENT.md`.
That document remains immutable provenance. All other amendment 02 clauses remain
in force. A separate activation record must identify this exact replacement and
bind this proposal package; editing a constant alone is not activation.

Applicability is the existing 180 radius-1 spheres/five fixed boundaries and the
already admitted 60/120 Hz lattice cases, including their Debug/prefix counterparts.
LA/LP retain diagnostic sensitivity status and cannot qualify a failed default.
No other shape, stack/wake fixture, scale, material, scene or constraint topology
inherits the allowance. Physics production behavior and ordinary defaults do not
change.

| Pair / requirement | Prospective value |
| --- | --- |
| Dynamic sphere/sphere unsafe-sample and return budget | 200 ms; N=Hz/5: 24 samples at 120 Hz, 12 at 60 Hz |
| Dynamic sphere/boundary unsafe-sample and return budget | Unchanged 100 ms; N=Hz/10: 12 samples at 120 Hz, 6 at 60 Hz |
| Continuous safe recovery confirmation | Unchanged 250 ms; H=Hz/4: 30 samples at 120 Hz, 15 at 60 Hz |
| Dynamic penetration | Unchanged d<=min(0.10*D,0.20); sphere/sphere 0.20, sphere/boundary 0.10 in this fixture |
| Settled/sleeping penetration | Unchanged d<=0.05 at every applicable completed tick |

D remains min(2*rA,2*rB) for sphere/sphere, and min(sphere diameter, smallest full
local box extent) for sphere/boundary. Measurements remain after PostSolve and
sleep at End: d=max(0, greatest current retained-anchor depth, independent geometry).
Both metrics remain separately recorded. No epsilon or witness-bias subtraction.
Rest/dynamic classification and the existing 0.5-second quiet dwell are unchanged.

Let k0 be the first End sample above 0.05 and select N by the actual pair class.
No run of unsafe samples may exceed N. Require H consecutive samples with d<=0.05,
starting at kr with k0<kr<=k0+N; confirmation is due by k0+N+H-1. An unsafe sample
at k0+N cannot start recovery and fails; once the deadline passes, another unsafe
sample before confirmation fails. Brief safe intervals, manifold loss, slot
replacement or wake never reset k0. Preserve full typed unordered pair identity,
pending-pair geometric observation and per-pair episode counts, cumulative unsafe
exposure and temporal distribution. A pending episode at process end is incomplete.
Repeated individually compliant episodes do not prove stability.

200 ms is an explicit owner gameplay-risk budget, not a proven universal safe
constant. The observed 191.667 ms event leaves only 8.333 ms, one 120 Hz sample,
of margin. No 60 Hz margin, continuous substep guarantee or historical PASS is
inferred. The justification and bounded evidence limitations are in the proposal.

## Unchanged gates and activation boundary

Retain 120-second one-pass sleep-enabled L120/L60 qualifications. In the final
ten seconds L120 mean quiet/sleeping count must be >=162/180 and every one-second
mean >=153/180; L60 mean >=153/180 and every one-second mean >=144/180. Preserve
per-body drift, quiet/sleep state, wake/churn and contact evidence, and all existing
stack/wake requirements, including next-tick impulse wake and support <=0.035.

Retain mechanical-energy peak <=30542.4 including the initial state, no sustained
unexplained energy growth, no severe local instability, finite state, contact
correctness/identity, lifecycle, resource, evidence-integrity and mandatory owner
visual guards. Preserve historical 0.035/30391.2 comparisons. P21-ANCHOR-01 and
P21-ENERGY-01 remain blocking until the proposal's bounded closure conditions are
met; this amendment neither waives them nor adds numerical tolerances. All P0
failures block. OD-07, PHY-03, resources and aggregate accounting remain unchanged.

P21-PEN-01 and P21-PEN-02 remain historical FAIL, with original criteria, source
identities and evidence. The completed long-tail diagnostic remains diagnostic
support only. The historical 15-second lattice was not settled. No historical
candidate is re-scored and no diagnostic is promoted to formal qualification.

Explicit owner acceptance plus a separate recorded activation and qualification
authority 03 are required before implementation or affected validation. Preserve
authority 02 and consumed diagnostic attempts without alteration or budget reset.
The exact proposed execution scope and two blocking dispositions are in PROPOSAL.md.
No production remedy, Phase 21 approval, seal, publication or next phase is granted.
