# Phase 36 Review

## Status

PHASE 36 STATUS: AWAITING HUMAN REVIEW

The requested revision and all final validation are complete. No approval, commit, tag, push, or Phase 37/38 work occurred. Final separated 10,000-body whole-step timing is **2.211312 -> 2.564363 ms** (Measured), **+0.353051 ms / +15.97%** (Derived). This is materially smaller than the previous +0.711206 ms / +32.14% estimate, but remains a real recurring cost for human review. Cold collision regressions remain explicit below.

The controlled 120-second sleeping-disabled trajectory is exactly equal to approved Phase 35. Residual lattice motion is therefore classified as behavior of the existing physical model; moving bodies were not forced to sleep.

## Objective

Finish PHYS-PERF-005 sleep/wake integration by attributing residual lattice motion and reducing measured awake-world bookkeeping overhead while preserving all validated wake, identity, contact-island, and physical behavior.

Authority: the revised `docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md` (2026-09-09) and the explicit human revision instruction govern this work. The revised plan was saved in the shared workspace after revision entry and has now been read and checked against the completed work. It defines Phase 36 as qualification/wake integration and awake-cost attribution; moving lattice bodies must remain awake. Phase 37 owns torsional/spin resistance and Phase 38 owns rolling resistance/physical lattice settling. No work on those phases was started. Both the earlier plan and the revised authoritative copy are preserved as `entry-plan.md` and `final-authority-plan.md`; the user-owned plan was not edited by this phase.

Acceptance evidence: settled boxes and supported isolated spheres sleep; all required wake, island, identity, penetration and disabled-reference checks pass; the long-run lattice's residual motion and eligibility failures are classified; awake overhead has been measured and reduced. Whether the remaining measured overhead is acceptable is explicitly left to human review.

## Baseline Commit

`4efaf4f325eabd5baf9f64104d770c3e425b8c42`, branch `physics/refactor`. HEAD and the empty index remain unchanged.

## Previous Approved Tag

`physics-phase-35-approved`. No Phase 36 approved tag exists.

## Audit Findings Addressed

PHYS-PERF-005: sleep settled dynamic islands, wake dependants conservatively, and skip sleeping work. The historical audit is unchanged.

## Allowed Scope

Five production files, one existing test file, and this review: seven phase-owned files. No contact-model, solver-equation, threshold, damping, rolling/spin resistance, broad-phase algorithm, CCD scheduling, contact-expiry policy, parallelism, application scheduling, or build-system changes.

## Files Changed

- GEngine/include/GEngine/Physics/PhysicsBody.h
- GEngine/include/GEngine/Physics/PhysicsBody.cpp
- GEngine/include/GEngine/Physics/PhysicsSystem.h
- GEngine/include/GEngine/Physics/PhysicsSystem.cpp
- GEngine/include/GEngine/Physics/Manifold.cpp
- PhysicsTests/src/main.cpp
- docs/physics/PHASE_36_REVIEW.md

## Implementation Summary

The existing sleep/wake architecture is preserved. Every body remains visible to SAP. A fresh conservative activation graph is built from retained live contact identities before expiry when sleepers exist; the public graph is independently rebuilt after PreSolve. Full `(slot, generation)` resolution and static/kinematic/zero-mass boundary semantics remain unchanged. Explicit mutations, impulses, gravity changes, active contacts, positive TOIs, and changed/removed supports retain their existing wake behavior.

Complete dynamic islands qualify using the unchanged 0.05 linear speed, 0.02 angular speed, 0.5-second dwell, correction-motion checks, and finite live-anchor depth <=0.02001. Accepted residual velocities are zeroed once. No physical parameter was adjusted to improve the sleeping count. `SetSleepingEnabled(false)` retains the fully active reference behavior; the setting survives world replacement.

The revision optimizes measured Phase 36 work:

- Compare source fields in place and materialize a start snapshot only when they change. At the end of a step, update only motion fields; static bodies do not need a second full snapshot copy.
- Reuse prior physical eligibility only when positive inactivity proves prior eligibility, the complete source has not changed, no external wake/mutation occurred, and pose and velocities remain identical through the step. Policy setters reset inactivity. The shared timer-advance helper preserves the original double-precision saturation expression. Changed state follows the full eligibility path.
- Avoid displacement/quaternion calculations for identical poses. Changed poses use the original equations and thresholds.
- When no retained contacts exist and no dwell has matured, skip singleton qualification traversal. This is a decision from the current tick's contacts and timers; no connectivity decision is cached across ticks. Contact-bearing worlds still process boundary resets and whole-island qualification.
- Use temporary spans within Phase 36 traversals; owning body/island storage and ordering remain unchanged.

`PhysicsSystem::GetSleepTimings()` exposes the last Update's source checking, activation/wake traversal, depth guard, eligibility, island qualification, final source storage, and step-flag timings. They reset each Update and are zero when physics profiling is disabled. Activation timing includes its conservative graph build; the existing graph timer includes both builds when applicable. Other wake work is charged to its containing scope, so `wakeNs` is not a measurement of every WakeUp call.

Body layout remains **672 bytes**, including the same embedded source/sleep state. No snapshot storage was moved out of the body. The 560-byte Phase 35 body and padding control are discussed below. `unchanged-semantics.json` verifies exact graph-construction and collision-equation source, plus unchanged solver/world/broad-phase files.

## Tests Added / Modified

Phase 36 now adds 79 focused integration checks and two primitive motion-accessor/integration checks: **18,017 -> 18,098 full-suite checks**. This revision adds seven checks for exact primitive dwell accumulation and invalidation by small material, pose, velocity, angular velocity, policy, and impulse changes during an unfinished dwell.

All prior coverage remains: whole-island and singleton sleep, frozen settled state, every required wake source, independent islands and shared boundaries, positive-TOI wake, removed support before deletion and immediate falling, geometry revision changes, stale generation/reused identity, gravity changes, disabled mode, world reset, differing member dwells, moving kinematic support, penetration guard, empty manifolds, deterministic replay, and unchanged-geometry validity reuse.

The original failed penetration experiment remains documented and preserved: freezing at depth 0.0219131 / mean Y 4.44648 failed the existing 0.02001 / 4.44998 gates. The existing live-anchor guard fixed that without altering solver equations or tolerances. This revision retains it.

## Validation Commands

All new evidence is under `bin-int/phase36-revision1/`; all earlier Phase 36 evidence remains under `bin-int/phase36-sleep-wake/`.

```powershell
python bin-int/phase36-revision1/setup-attribution.py
python bin-int/phase36-revision1/complete-attribution-builds.py
python bin-int/phase36-revision1/run-attribution.py
python bin-int/phase36-revision1/validate-final.py
python bin-int/phase36-revision1/measure-final.py
python bin-int/phase36-revision1/summarize-final.py
python bin-int/phase36-revision1/final-checks.py
```

Actual builds use the current MSBuild x64 project files for PhysicsTests and RigidBodySimulation in Debug and Release. The final script records every command, exit, and elapsed time in `final-validation.json`, checks frozen source SHA-256 values before each stage, and archives the exact final sources and binaries. Generated attribution/replay projects are ignored diagnostic artifacts; repository project files were not changed.

All builds and tests finished before final timing. Each timed child exits before the next starts. The first final block stopped because the new artifact directory lacked its single-sphere fixture (replay exit 3); its complete partial output is preserved in `interrupted-final-block/`. The unchanged fixture was copied and the full ABBA sequence restarted. No production test failed in this revision.

## Debug Result

Measured: Debug x64 tests build, **79 integration**, **119 primitive**, **171 island**, and **18,098 full physics checks** all pass. Application build also passes. Full-suite runtime: 183.469 s. Logs: `Debug-{tests-build,focused,primitives,islands,full,application}.log`.

No new non-gating known issue was observed.

## Release Result

Measured: Release x64 tests build, **79 integration**, **119 primitive**, **171 island**, and **18,098 full physics checks** all pass. Application build also passes. Full-suite runtime: 16.329 s. Logs: `Release-{tests-build,focused,primitives,islands,full,application}.log`.

Nominal Release remains optimized with the existing **/MTd Debug static CRT override**. These are not true Release CRT allocator/runtime measurements.

## Stability Results

Measured final replays use the frozen Phase 34 exports, retaining all 21 box-world bodies (16 dynamics, five statics) and all 185 lattice-world bodies (180 dynamics, five statics). The single-sphere probe is the exact exported floor plus the first exported lattice sphere (initial y=18). Fixed dt=1/60, 1,500 ticks (25 simulated seconds), one solver pass. Every body and frame value is finite.

| Measured final state | Phase 35 | Phase 36 |
|---|---:|---:|
| Boxes: linear speed | 2.92506314281e-07 | 0 |
| Boxes: angular speed | 6.37496739841e-08 | 0 |
| Boxes: live-anchor depth | 0.0190289020538 | 0.0190279483795 |
| Boxes: energy | 857.088832855 | 857.098239899 |
| Boxes: manifolds | 16 | 16 |
| Boxes: contact points | 64 | 64 |
| Boxes: sleeping dynamics | 0 | 16 |
| Lattice: linear speed | 2.51013374329 | 2.51013374329 |
| Lattice: angular speed | 2.50781726837 | 2.50781726837 |
| Lattice: live-anchor depth | 0.0200152397156 | 0.0200152397156 |
| Lattice: energy | 3410.67767929 | 3410.67767929 |
| Lattice: manifolds | 191 | 191 |
| Lattice: contact points | 361 | 361 |
| Lattice: sleeping dynamics | 0 | 0 |
| Sphere: linear speed | 0.00320090772584 | 0 |
| Sphere: angular speed | 0.00319451931864 | 0 |
| Sphere: live-anchor depth | 0.00399769283831 | 0.00399775383994 |
| Sphere: energy | 18.0000343436 | 18.0000271797 |
| Sphere: manifolds | 1 | 1 |
| Sphere: contact points | 1 | 1 |
| Sphere: sleeping dynamics | 0 | 1 |

The Phase 35 sleeping count is established by its fully active behavior; its saved replay does not have the newly appended sleep-counter column. All other table values are emitted measurements. At 25 s the candidate active/solved-constraint counts are boxes 0/0, lattice 180/361, sphere 0/0. Frozen contacts are retained: boxes 16 manifolds/64 points; sphere one manifold/one point. Their SAP pair counts remain 46 and one respectively while GJK/EPA calls fall to zero. The lattice retains exactly the baseline physical state and contact counters throughout all 1,500 ticks.

In the measured 5-20 s box window (which contains both existing 5-18 s and 15-20 s gates), Phase 36 has zero peak linear/angular motion and zero excursion, maximum penetration 0.0190279483795, and minimum mean dynamic Y 4.4640533325. Phase 35 values are 2.72497004516e-6 / 1.2158628806e-6, excursion 1.809257753e-6, penetration 0.0190289020538, and minimum mean Y 4.46400434. Both satisfy the established 0.05/0.02 motion, 0.035 penetration, 0.05 excursion, and 4.45 height limits. The full suite also passes the stricter zero-gravity penetrated-stack 0.02001/4.44998 gates in both configurations.

Peak energy after the first tick is identical across revisions: boxes 863.719971878 (initial 864), lattice 30236.4001105 (initial 30240), sphere 215.979991458 (initial 216). Sleep removes only qualified residual velocities; the higher candidate box final potential energy reflects its earlier frozen settled pose, not energy injected by changed solver equations.

**Exact equality:** all four runs of each revision/workload repeat their body CSV hashes and non-timing frame data exactly. Enabled boxes first differ from Phase 35 at body frame 69, exactly the first sleep tick 70; the sphere first differs at frame 128 / sleep tick 129. Lattice has no differing frame or sleep transition in 25 s. Disabling sleeping reproduces the Phase 35 body CSV hashes and all common non-timing frame fields exactly for boxes, lattice and sphere. Every final candidate replay also matches the pre-optimization Phase 36 states/counters exactly.

The final revision repeats these exact physical states, first-sleep ticks, and common non-timing fields. The benchmark timings below are newly measured.

## Long-Run Lattice Attribution

Measured: four serial 120-second runs in ABBA order (Phase 35, Phase 36 disabled, Phase 36 disabled, Phase 35), followed by separate disabled and enabled eligibility diagnostics. All use the same exported 185-body world, fixed float dt=1/60, one solver pass, and 7,200 ticks (120.000006258 simulated seconds). Normal priority and affinity mask 4 / logical CPU 2 match the other measurements.

The Phase 35 diagnostic build's first 1,500 body-state frames exactly match the archived approved Phase 35 replay. Both full Phase 35 runs, both disabled runs, and the disabled diagnostic have the same complete body CSV SHA-256 and every common non-timing frame field across all 7,200 ticks. Every recorded state/frame value is finite. Timing fields and CPU-cycle samples are excluded from equality, not hidden.

| Measured at 120 seconds | Phase 35 | Phase 36 disabled | Phase 36 enabled |
|---|---|---|---|
| Max linear speed | 0.574241697788 | 0.574241697788 | 0.838442444801 |
| Max angular speed | 0.913947999477 | 0.913947999477 | 0.913947582245 |
| Max live-anchor depth | 0.0200004596263 | 0.0200004596263 | 0.0200004596263 |
| Energy | 3226.84995696 | 3226.84995696 | 3227.36074929 |
| Manifolds | 245 | 245 | 249 |
| Contact points | 245 | 245 | 251 |
| Sleeping | 0 | 0 | 24 |
| Active | 180 | 180 | 156 |
| Solved constraints | 245 | 245 | 209 |
| Candidate pairs | 357 | 357 | 354 |
| GJK calls | 257 | 257 | 229 |
| EPA calls | 195 | 195 | 167 |

**Derived interpretation:** residual rolling/spinning motion is already present in the approved Phase 35 physical model. It is not a Phase 36 sleep/wake defect. Enabled sleeping intentionally changes the trajectory after eligible groups sleep; its full 120-second body state and common counters exactly match the pre-revision Phase 36 diagnostic. No new physical divergence was introduced by this bookkeeping revision. Residual motion was not hidden by threshold changes or extra dissipation.

Eligibility reasons below are separate, overlapping flags on awake dynamic bodies at the final tick; they must not be summed. There are 180 dynamics in both modes, with 0 disabled-mode sleepers and 24 enabled-mode sleepers. The disabled diagnostic calculates shadow dwell on its measured fully active trajectory because live inactivity is deliberately held at zero. It does not simulate a hypothetical post-sleep trajectory. Enabled-mode dwell uses the engine's actual inactivity timer. This frozen world has static boundaries and no external edits or moving kinematic boundary, so shadow dwell has no omitted external wake source.

| Measured diagnostic count | Disabled (shadow eligibility) | Enabled (live dwell) |
|---|---|---|
| Linear-speed failure | 83 | 82 |
| Angular-speed failure | 140 | 135 |
| Inactivity/dwell failure | 140 | 135 |
| Penetration/depth failure | 0 | 0 |
| Correction/pose-motion failure | 140 | 135 |
| Invalid-state/shape failure | 0 | 0 |
| Members of unready islands | 156 | 156 |
| Individually eligible but blocked by island | 16 | 21 |
| Individually ready awake bodies | 40 | 21 |

`eligible_blocked_by_island` isolates bodies that pass their own speed, dwell, pose, depth and validity checks but depend on an unready island member/boundary. `island_unready` counts all awake members of such islands. The full per-tick reason counts and final per-body island membership, speeds, depths and dwell are retained in `long-4-diagnostic-disabled-eligibility-*.csv` and `long-5-diagnostic-enabled-eligibility-*.csv`. These diagnostics run after each timed step; their timing is not used as a benchmark claim.


Separate kinetic-energy history required by the revised plan is **Derived** offline from the saved body-state CSVs and exported radii. All 180 dynamics are verified spheres, so the calculation uses `0.5*m*|v|^2` and `0.5*(2*m*r^2/5)*|w|^2` in double precision. These analytic components are not asserted bit-equal to the engine's float-matrix total-energy calculation. Full 7,200-frame histories are retained in `long-0-baseline-derived-kinetic.csv` and `long-5-diagnostic-enabled-derived-kinetic.csv`; sleeping-disabled mode has the same states and therefore the same derived values as Phase 35.

| Derived at 120 seconds | Phase 35 / disabled | Phase 36 enabled |
|---|---:|---:|
| Translational kinetic energy | 1.396988359314 | 1.732686686820 |
| Rotational kinetic energy | 1.255856192238 | 1.428041977558 |
| Bodies above either sleep speed threshold | 140 | 135 |

Both recorded trajectories have derived peak translational/rotational kinetic energy 17639.978328007/282.457494745; the peaks occur before sleeping changes their subsequent trajectories.

## Benchmark Results

Protocol: two complete ABBA blocks, Phase 35 / final Phase 36 / final Phase 36 / Phase 35, repeated. Normal priority, affinity mask 4 / logical CPU 2, serial child processes, nominal Release x64 /Ox /Oi /fp:precise /MTd, physics profiling enabled. All 40 runs and three disabled 25-second reference replays pass.

Collision: 50/100/200/500/1k/2k/10k bodies, two warmup samples, five measured samples, one 1/120-second step per freshly constructed world. World construction is outside the external timer; graph/output capacity is initially cold. Separated: 100/1k/10k bodies, two warmup samples, five measured samples, four warmup ticks and eight measured ticks at 1/120 per sample. These steps remain below the sleep dwell and retain full awake work; graph capacity is steady. The workload contains N/2 dynamic bodies and N/2 statics, matching every previous separated measurement.

Exact-world timing uses 1,500 ticks at 1/60, one solver pass; primary window ticks 300-1200 inclusive (901 samples, about 5-20 s), late window 1201-1500. Point estimates are medians of four process medians. All raw rows, per-process ranges, counters and hashes remain in `benchmark-measurements.json`, `benchmark-summary.json` and the per-process CSVs/logs.

Measured timings in ms; changes are Derived:

| Workload | Bodies | Step 35 | Step 36 | Delta ms | Delta % | Solver 35 -> 36 | Graph 35 -> 36 |
|---|---|---|---|---|---|---|---|
| collision | 50 | 2.049900 | 2.151000 | +0.101100 | +4.93% | 0.584170 -> 0.632940 | 0.014470 -> 0.011940 |
| collision | 100 | 4.218700 | 4.406900 | +0.188200 | +4.46% | 1.183810 -> 1.281310 | 0.023650 -> 0.028190 |
| collision | 200 | 8.498600 | 8.740500 | +0.241900 | +2.85% | 2.334060 -> 2.516460 | 0.043720 -> 0.043350 |
| collision | 500 | 20.689850 | 21.760600 | +1.070750 | +5.18% | 5.908790 -> 6.523440 | 0.124340 -> 0.114520 |
| collision | 1000 | 42.214500 | 43.863350 | +1.648850 | +3.91% | 11.642920 -> 12.620400 | 0.256210 -> 0.217580 |
| collision | 2000 | 87.203050 | 90.777550 | +3.574500 | +4.10% | 24.049690 -> 25.711380 | 0.545080 -> 0.483850 |
| collision | 10000 | 451.853600 | 469.693700 | +17.840100 | +3.95% | 122.581860 -> 133.161390 | 2.970810 -> 2.808820 |
| separated | 100 | 0.020731 | 0.022600 | +0.001869 | +9.02% | 0.000080 -> 0.000069 | 0.001883 -> 0.001896 |
| separated | 1000 | 0.207537 | 0.228087 | +0.020550 | +9.90% | 0.000075 -> 0.000068 | 0.018777 -> 0.018703 |
| separated | 10000 | 2.211312 | 2.564363 | +0.353051 | +15.97% | 0.000135 -> 0.000105 | 0.211852 -> 0.224301 |

| Exact world | Step 35 | Step 36 | Derived % | Solver 35 -> 36 | Graph 35 -> 36 |
|---|---|---|---|---|---|
| boxes | 2.961750 | 0.032450 | -98.90% | 1.638300 -> 0.004300 | 0.001600 -> 0.002500 |
| lattice | 16.100100 | 16.668200 | +3.53% | 10.808250 -> 11.074850 | 0.024650 -> 0.023850 |
| sphere | 0.068100 | 0.002750 | -95.96% | 0.031600 -> 0.000200 | 0.000400 -> 0.000600 |

Previous pre-revision Phase 36 separated 10k: measured 2.212825 -> 2.924031 ms, derived +0.711206 ms / +32.14%. Final co-timed comparison: 2.211312 -> 2.564363 ms, +0.353051 ms / +15.97%. Comparing those per-block overhead estimates gives a derived **50.36% reduction**; this is a comparison across separate blocks, not an exclusive attribution of every saved cycle. Final whole step is -12.30% versus the preceding candidate's measured whole step. The earlier 4.099200 ms pre-validity-cache Phase 36 result and its full evidence also remain preserved.

The cold collision workload remains 2.85-5.18% slower than Phase 35, versus approximately 1.47-3.21% in the previous Phase 36 block. Solver time also moved despite unchanged equations. These are measured limitations; the full differences are not attributed to sleep bookkeeping or body size. No solver/codegen improvement is used as evidence that graph or bookkeeping work is free.

Non-timing collision/separated benchmark columns and physical fingerprints remain exact. Separated collision/contact/constraint counts are zero; all dynamics stay active. Collision candidate-pair/GJK/EPA/generated-contact/manifold-point/constraint counts for the listed sizes remain 19/38/75/188/375/750/3750, with one solver pass. All other original counters are retained in the summary/raw data.

Cold collision graph capacity-growth counts remain 61/118/229/568/1129/2254/11254. Repeated separated builds have zero graph growth at every size; frozen box/sphere windows also have zero graph growth. Growth counts are vector capacity events, not malloc/free counts. No whole-step allocation-free claim is made. Initial body/snapshot storage is part of body construction outside the external step timer; dynamic graph output may still allocate as topology changes.

## Awake-World Attribution

The isolated A/B/C/D experiment uses the identical separated workload, N=100/1k/10k, three warmup samples, nine measured samples, four warmup ticks and eight measured ticks. Balanced order A/B/C/D/D/C/B/A, twice. A and B compile the approved Phase 35 physics sources; B adds only 112 inert bytes to RigidBody3D, preserving identity layout and every existing member offset. C and D share the same pre-revision Phase 36 binary, with the policy chosen outside timing. An independent instrumented pre-revision binary is run in enabled/disabled ABBA blocks. State fingerprints and all existing non-timing benchmark columns are exact across every variant.

Measured 10k whole-step medians and Derived deltas:

| Control | Meaning | Body bytes | Step ms | Delta vs A ms | Delta vs A % | Delta ns/body |
|---|---|---|---|---|---|---|
| A | Phase 35 | 560 | 2.200087 | +0.000000 | +0.00% | 0.000 |
| B | Phase 35 + inert padding | 672 | 2.255131 | +0.055044 | +2.50% | 5.504 |
| C | Pre-revision 36, disabled | 672 | 2.611337 | +0.411249 | +18.69% | 41.125 |
| D | Pre-revision 36, enabled/awake | 672 | 2.952844 | +0.752756 | +34.21% | 75.276 |

The padding control measures effects of object size, allocator placement, and associated compiler/layout consequences; it is not a cache-miss measurement or a pure coefficient for sizeof. Its observed movement is much smaller than the full Phase 36 regression, so moving snapshots out of the body was not justified. C minus B includes snapshots plus other Phase 36 code paths/codegen; D minus C also includes different wake/timer policy work. Neither subtraction is labeled an exclusive subsystem cost.

Direct timers identify the material paths. The table reports medians of per-process **mean** scoped times over measured steps (distinct from whole-step medians). The before values come from the separate instrumented entry revision; the after values come from the final separated benchmark. Timer scopes overlap the inclusive whole step and use different summary statistics; their deltas are not an exact additive decomposition of its median.

| Scope | Before ms | Final ms | Before ns/body | Final ns/body |
|---|---|---|---|---|
| sourceCheckNs | 0.168458 | 0.147322 | 16.846 | 14.732 |
| wakeNs | 0.000025 | 0.000026 | 0.003 | 0.003 |
| depthNs | 0.000045 | 0.000035 | 0.005 | 0.004 |
| eligibilityNs | 0.223938 | 0.083775 | 22.394 | 8.377 |
| qualificationNs | 0.076398 | 0.000024 | 7.640 | 0.002 |
| sourceStoreNs | 0.102688 | 0.034807 | 10.269 | 3.481 |
| stepFlagsNs | 0.010445 | 0.010259 | 1.045 | 1.026 |

Empty wake/depth/qualification scopes are near the timer floor. Graph time is measured separately and does not account for the source/eligibility cost. Body size remains 672 bytes (+112 / +20% versus Phase 35, Derived); all new timing fields are per-system. Exclusive cache-miss, allocator-call, and instruction/codegen costs are **Not available**. Initial/cold allocation behavior is distinguished from repeated capacity reuse above; no connectivity decision or cross-tick island membership is cached.

## Behavior Changes

The original Phase 36 behavior intentionally zeros eligible residual velocity and stops integrating settled groups. This bookkeeping revision preserves the preceding Phase 36 enabled physical trajectories/counters exactly for all recorded 25-second and 120-second replays. Sleeping-disabled mode preserves approved Phase 35 physical states and common counters exactly, including the full long run. Whole-island order, identities, boundary behavior, SAP visibility, positive-TOI wake, explicit mutation/impulse wake, and the penetration guard remain intact.

The existing integrated-body counter counts Update calls, including statics, and excludes sleepers. It is not a count of actually moved poses. The graph timer includes both normal and conservative activation builds when applicable.

## Known Limitations

Awake separated overhead remains measured at +0.353051 ms / +15.97% for 10k bodies, and cold collision overhead remains visible. These require human performance judgment; no zero-cost claim is made. The lattice still has moving bodies and only 24/180 sleepers at 120 seconds (13.33%, Derived), which satisfies classification rather than a mostly-sleeping requirement under the revised instruction. Public writes are observed on the next positive world tick. Eligibility/source validity reuse relies on the existing single-threaded mutation and shape-revision contracts. The nominal Release /MTd caveat remains.

## Out-of-Scope Findings

The approved Phase 35 contact model already retains lattice rolling/spinning motion, proven by exact long-run disabled comparison. This is not proof of an equation defect. Existing contact-point normal/tangent impulses in `ConstraintPenetration.cpp` have no separately modeled rolling/spin resistance; such dissipation is deferred to Phases 37/38 by the human, and none is introduced here.

The pre-existing direct collision-mask invalidation limitation remains: `Manifold.cpp::CacheCompatible` stamps identity/shape/revision, not filter bits. Defensive narrow-phase filtering does not explicitly retire all previously retained pairs after mask edits. Phase 36 detects such edits as wake requests but does not change contact-expiry/filter policy.

Unrelated working-tree changes and the pre-existing .gitignore trailing blank line are preserved. Earlier evidence, failures, binaries, source copies, and probes remain under `bin-int/phase36-sleep-wake/`; this revision's entry sources/review, A/B/C/D controls, round1/round2 sources and binaries, partial interrupted block, final logs and raw measurements remain under `bin-int/phase36-revision1/`. No raw evidence was discarded.

## git diff --check

Final phase-owned tracked-file check: exit 0, no output. The new review also passes its separate whitespace check against NUL: no output; `git diff --no-index --check` returns 1 because this is a new nonempty file.

Global `git diff --check`: exit 2 for the untouched, pre-existing .gitignore blank line. Literal stdout followed by stderr:

```text
.gitignore:33: new blank line at EOF.
warning: in the working copy of 'GEngine/src/Scene/_Scene.cpp', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md', LF will be replaced by CRLF the next time Git touches it
```

`final-checks.json` verifies the six production/test SHA-256 values against both the final validation manifest and archived final sources, all 16 passing validation stages, 43 final benchmark/reference runs, 24 attribution runs, and six passing long-run/eligibility runs. The index remains empty, HEAD is the approved Phase 35 commit, and no Phase 36 approved tag exists.

Ownership reconciliation: the unrelated application source's last-write timestamp is 2026-09-09 22:00:05 UTC, preceding this revision's entry at 23:04:21 UTC; its older Phase 36 hash was stale. No initial content hash for that unrelated file was captured for this revision, so timestamp evidence is explicitly distinguished from a start/end content-hash comparison. The revised plan was saved at 23:09:24 UTC during the run, read, and retained as user-owned authority. Neither file was edited by this phase. Their observed hashes/timestamps and the earlier unchanged unrelated-file hashes are recorded in `ownership-reconciliation.json`; the final check verifies them and the remaining entry status boundary. The plan is excluded from the seven phase-owned files.

## git status --short

Literal final output, including preserved user-owned changes:

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Manifold.cpp
 M GEngine/include/GEngine/Physics/PhysicsBody.cpp
 M GEngine/include/GEngine/Physics/PhysicsBody.h
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M GEngine/include/GEngine/Physics/PhysicsSystem.h
 M GEngine/src/Scene/_Scene.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
 M docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
 D docs/physics/review/PHASE_00_REVIEW.md
 D docs/physics/review/PHASE_01_REVIEW.md
 D docs/physics/review/PHASE_02_REVIEW.md
 D docs/physics/review/PHASE_03_REVIEW.md
 D docs/physics/review/PHASE_04_REVIEW.md
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_31_REVIEW.md
?? docs/physics/PHASE_36_REVIEW.md
?? docs/rendering/
```

## Human Decision

PENDING
