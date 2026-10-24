# Proposed Phase 21 prospective OD-06 contract amendment 02

**PROPOSED ONLY. NOT ACCEPTED, ACTIVE OR IMPLEMENTED.**

This is the normative part of [one owner-decision proposal](PROPOSAL.md).
The current universal lattice limit of 0.05 remains controlling until explicit
owner acceptance and a separate recorded activation of this exact package.
Nothing here approves Phase 21, changes Physics or authorizes a run now.

## 1. Exact prospective replacement

For future candidates in the already admitted Phase 21 lattice cases only,
replace the universal all-time 0.05 lattice penetration clause in
`PHASE_21_OD06_OD07_PROPOSAL_01.md` and
`PHASE_21_PROSPECTIVE_CONTRACT_AMENDMENT_01.md` with clauses 2-5 below.
No other clause is replaced. Immutable original phase/contracts, approval
records, acceptance 01 and diagnostic acceptance remain unchanged provenance.
An activation must name those two superseded clauses and bind this package's
hashes; updating a numeric constant alone cannot activate this amendment.

## 2. Measurement, dimensions and rest classification

Keep the existing end-of-completed-fixed-tick measurement boundary, after
PostSolve and sleep handling. Do not apply these limits to cached pre-integration
separation, or silently substitute a pre-correction measurement. For each
observed pair use d = max(0, current retained-anchor penetration, independently
computed geometric overlap). Use the greatest retained-contact depth for that
pair. A missing manifold contributes no anchor depth, but never proves geometric
separation. Preserve original metric and independent geometry as separate fields.
Unsupported or ambiguous geometry, invalid identity, missing required samples or
unexplained measurement disagreement is incomplete/blocking evidence, not PASS.

Let D be the smaller relevant full collider dimension:

- Sphere/sphere: D = min(2*rA, 2*rB).
- Lattice sphere/static box: D = min(2*rSphere, the box's smallest full local
  extent). This uses boundary thickness, not its much larger wall/floor span.

For the unchanged fixture, D is 2 for sphere pairs and 1 for sphere/boundary
pairs. No extension to other shapes, changed scales, joints or arbitrary gameplay
assets is granted by this fixed-fixture proposal.

The settled-rest limit remains d <= 0.05 at every applicable completed tick.
A pair is settled when every positive-mass member is quiet for a continuous
0.5 simulated seconds using the existing actual linear/angular sleep thresholds;
static members are ignored. Sleeping members receive no deeper-penetration
allowance: any pair involving a sleeping dynamic body must satisfy 0.05.
These are observer classifications, not changes to Physics sleep thresholds,
eligibility, timers, wake behavior or solver parameters.

A dynamic excursion can begin only if the pair is not settled/sleeping as above
and at least one dynamic member is non-quiet at the current or immediately
preceding completed tick. Otherwise 0.05 applies. Speed alone never excuses
unexplained wake, drift or jitter; all existing guards remain independently binding.

## 3. Dynamic depth and finite recovery budget

| Proposed requirement | Exact value |
| --- | --- |
| Dynamic maximum depth | d <= 0.10*D, and d <= 0.20 world units |
| Fixed lattice equivalents | Sphere/sphere <=0.20; sphere/boundary <=0.10 |
| Maximum consecutive samples above 0.05 | 100 ms of fixed-step samples: 12 at 120 Hz; 6 at 60 Hz |
| Return deadline | Begin a qualifying recovery streak within 100 ms of the first observed exceedance |
| Demonstrated recovery | d <=0.05 for 250 ms continuously: 30 samples at 120 Hz; 15 at 60 Hz |

Any depth above the relevant dynamic cap fails immediately at the original End
boundary, regardless of motion, duration, pass count or subsequent recovery.
The 0.20 absolute ceiling is an additional guard, not a replacement for the
smaller boundary-thickness cap. No epsilon allowance is added to these values.

For exact discrete semantics, let k0 be the first End sample with d>0.05,
N=Hz/10 and H=Hz/4. No run of above-limit samples may exceed N. The same episode
must contain H consecutive safe samples starting at some kr <= k0+N; kr>k0.
Once k0+N has passed, a further unsafe sample before recovery confirmation fails
that episode. A brief safe sample, contact-slot replacement, manifold loss or
wake does not reset k0. Confirmation is due no later than k0+N+H-1. Record both
unsafe-sample exposure (sample count/Hz) and first-exceedance-to-recovery time;
this is a sampled End-state contract, not a continuous substep penetration proof.

Key episodes by the unordered pair of full typed body identities, independently
of manifold/contact slots. Continue geometric observation while an episode is
pending, including when its manifold disappears. Confirmed geometric separation
may satisfy the recovery streak; missing identity/body data may not. Preserve
all excursions, episode counts and cumulative exposure. An episode outstanding
at process end is incomplete, never presumed recovered. Repeated episodes remain
subject to the existing persistent-jitter/drift/contact-instability guards.

## 4. Bounded observation within the existing matrix

Use the two already admitted supporting files and existing observer/data access
only. No production hook, Physics implementation or separate native diagnostic.
Maintain compact per-pair depth, episode and recovery state for retained contact
pairs and pending episodes. Reuse the existing complete body-membership and
contact-correctness guards; this observer is not an exhaustive all-pairs collision
detector and does not weaken those guards.

In the already planned L120 run, retain typed-pair geometric/velocity records for
all 21 identified historical tied pairs throughout the first 2 simulated seconds,
and retain every new excursion from detection through recovery confirmation or
failure, including pairs other than those 21. Continue per-pair event accounting
through all 120 seconds. This is no early-stop substitute for L120. L60 and other
lattice cases enforce the same time-in-seconds rules with their native tick counts;
retain compact episode evidence there as needed to audit their applicable gates.
The planned minimal-observation prefix keeps its safety checks; optional detailed
export is distinguished when reporting observer cost and physical-state parity.

New pair-state/buffering allocation is capped at 2 MiB within the existing 8 MiB
additional-recorder and 64 MiB named-storage limits. Reserve at most 8 MiB per
process for pair traces, episode summaries and any first-failure packet, within
(not in addition to) the existing 64 MiB total export limit. Include pair IDs,
end-tick ordinals, poses needed for supported independent geometry, velocities,
quiet/sleep/wake state, measured/independent depths, D and recovery counters.

Implementation must prove actual struct/record sizes and bounded allocation
before launch, and enforce both local and total limits. Overflow, missing samples
or insufficient remaining time means INCONCLUSIVE/stop. No truncation, cap increase,
extra standalone launch or clock reset is authorized. All current build/process/
aggregate deadlines remain unchanged. Reuse matching evidence and products; rerun
only the affected checks already required by the existing matrix.

## 5. Unchanged gates and historical disposition

Retain L120/L60 durations, one-pass sleep-enabled defaults, tail means >=162/>=153,
and every final one-second mean >=153/>=144. Retain full per-body tail evidence,
reliable contacts, next-tick impulse wake, stack/ground support limit 0.035 and
all other accepted stack/ground criteria. LA/LP remain diagnostic sensitivity
comparisons and cannot rescue a failed default. Preserve original historical
0.035/30391.2 comparison reporting.

Retain energy peak <=30542.4, finite states, no sustained unexplained energy growth,
no severe local instability, no persistent nonphysical drift/jitter, contact
identity, lifecycle, resource and evidence-integrity guards. An energy ceiling
alone does not explain energy increases. Owner visual inspection is necessary
but cannot override a failed numeric or technical guard. OD-07 and PHY-03's
Phase-25 disposition remain unchanged.

P21-PEN-01 remains a historical FAIL against its accepted 0.05 all-time criterion:
End contact depth 0.080003261566162109 and independent sphere overlap
0.08000314234680661 at tick143. The earlier 15-second lattice was not settled;
its recurrent aggregate exceedances do not establish individual-pair recovery.
No old candidate is re-scored, and this proposal does not close the P1 blocker.

Specific owner acceptance would disposition only the universal-versus-dynamic
applicability of that criterion for a new candidate. The current failure's actual
outcome and evidence remain named in subsequent reviews. A new candidate still
requires its own successful amended evidence and all unchanged gates. No blanket
P1 exception, P2 downgrade or waiver of unexplained critical exposure is granted.
Residual owner: engine owner. Revisit triggers: any dynamic cap/recovery failure,
recurrent unexplained churn/drift, energy anomaly, or a change in collider scale,
materials, timestep, solver policy or constraint topology.

Phase 21 remains NOT READY until its complete applicable qualification and owner
acceptance succeed. Phases 26-29 remain blocked until the core-stability gate is
resolved through the normal workflow. This fixed-lattice allowance supplies no
universal joint/ragdoll stability guarantee and grants no automatic next phase.
