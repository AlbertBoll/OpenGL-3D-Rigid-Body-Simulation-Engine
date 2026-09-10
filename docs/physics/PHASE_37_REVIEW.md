# Phase 37 Review

## Status

PHASE 37 STATUS: AWAITING HUMAN REVIEW

## Objective

Add bounded contact-normal torsional resistance to the approved contact solver. This is an opt-in physical material capability that dissipates supported relative spin, with unchanged sleep thresholds and sliding-friction equations.

## Baseline Commit

1bbd6f360d46740f38220632637c79a989a32859, branch physics/refactor.

## Previous Approved Tag

physics-phase-36-approved; verified at HEAD by the repository preflight helper. No Phase 37 approved tag exists.

The user-owned plan and Phase 36 review retain pre-approval status text. The current explicit START PHASE 37 instruction and the existing approved Phase 36 commit/tag establish the execution baseline. The revised plan's Phase 37 torsional objective is authoritative; the former Phase 37 solver-storage objective belongs to Phase 39.

## Audit Findings Addressed

None of the historical audit findings is claimed fixed by this phase. This is the human-requested physical-model extension following Phase 36 residual-spin diagnostics. It enables physical settling relevant to PHYS-PERF-005 without changing sleep qualification. The historical audit is unchanged.

## Allowed Scope

Four production files, two existing test/benchmark files, and this review: seven phase-owned files.

## Files Changed

- GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
- GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h
- GEngine/include/GEngine/Physics/PhysicsBody.h
- GEngine/include/GEngine/Physics/PhysicsProfile.h
- PhysicsTests/src/main.cpp
- PhysicsBenchmark/src/main.cpp
- docs/physics/PHASE_37_REVIEW.md

## Implementation Summary

Each body exposes a validated SetSpinResistanceLength / GetSpinResistanceLength API. The default is zero. Values must be finite and nonnegative; invalid input leaves state unchanged. A changed value invokes the existing WakeUp API, including wake propagation from an edited static support at the next step. Identical values do not disturb sleep.

The material parameter ell is an effective torsional resistance length in world units: maximum resisting torque divided by normal force. It already includes the contact-patch scale and dimensionless friction coefficient. It is a phenomenological material input, not a radius inferred from shape geometry. The deterministic pair policy is min(ellA, ellB); either surface can disable the model. No automatic scaling with body radius or geometry changes is implied. Sphere, box and valid convex contacts use the same law.

For the unit solver normal n (A to B):

~~~text
s = dot(omegaB - omegaA, n)
k = dot(n, (inverseInertiaA + inverseInertiaB) * n)
limit = min(ellA, ellB) * max(lambdaNormal, 0)
lambdaSpinNext = clamp(lambdaSpin - s/k, -limit, limit)
delta = lambdaSpinNext - lambdaSpin
angularImpulseA = -n * delta
angularImpulseB = +n * delta
~~~

The row executes after the existing normal/tangent solve, including the manifold's coupled normal-block path. Only the incremental impulse is applied. Each point uses its own normal load, so aligned patch points share a total bound of ell times the sum of their normal loads; adding points does not multiply the load budget. Static and kinematic boundaries have zero inverse-inertia response and retain their prescribed state.

Torsional accumulation is local to one positive step. PreSolve always resets it; no torsional warm start is applied. This deliberately avoids inventing an additional cross-step manifold compatibility/dt-ratio/material-cache policy. Existing identity, geometry, feature, normal-coherence and wake validation remain authoritative before constraint preparation. Zero-coefficient rows exit before torsional inertia work or timers. No contact/normal impulse means no new resistance impulse.

Double intermediates handle the scalar solve and load bound. The actual float impulse and resulting float angular velocities are checked before either body changes. The profiler reports enabled row visits, nonzero incremental angular impulses, and inclusive torsional-row time (a subset of solver time). The counter is an operation count, not torque, net impulse, or an allocation count.

For an isolated row starting cold, the angular kinetic-energy change is delta*s + 0.5*k*delta^2. Clamping the cancellation solution toward zero makes this nonpositive. Later PGS corrections may retract earlier impulses when the current load decreases; accumulated state prevents repeatedly spending the full budget. Moving kinematic supports can transfer externally supplied energy while reducing relative spin, as with ordinary friction. No angular-velocity component is explicitly removed in the contact tangent plane.

The validation material is ellA = ellB = 0.05 world units, fixed before enabled measurements. For a unit-radius unit-mass solid sphere at gravity 12, I=0.4 and the predicted normal-axis deceleration is ell*m*g/I=1.5 rad/s^2. From 4 rad/s, exact stopping time is 2.6667 seconds. This parameter is an acceptance fixture and is not installed into application materials.

## Tests Added / Modified

Focused --spin-resistance coverage includes:
- analytic load bounds over three radii, two masses, zero/nonzero/large lengths and zero/nonzero normal loads;
- unchanged tangent-plane angular motion for isotropic spheres;
- accumulation across eight passes, with no per-pass budget multiplication;
- no obsolete-load torsional warm start at a changed timestep or separating contact;
- disabled torsion for paused/reverse constraint preparation;
- two-body angular momentum, arbitrary normal directions, pair reversal, and static/kinematic response;
- rotated anisotropic inertia and kinetic-energy dissipation;
- transactional material validity, largest finite coefficient, and material-change wake propagation;
- physical pure-spin decay on a box plane at 60/120 Hz with sleeping disabled;
- an airborne non-principal spin compared exactly to a zero-resistance reference trajectory.

--spin-stability applies the fixed validation material to the existing exact exported 21-body stack regression at 60 and 120 Hz. It retains the existing 5-18 s and 15-20 s windows and gates: linear <=0.05, angular <=0.02, depth <=0.035, excursion <=0.05, mean Y >=4.45 and energy <=100.5% initial. Existing full-suite stricter penetration and lifetime/contact tests remain unchanged.

Analytic impulse/velocity tolerances are 2e-6 to 3e-5, with the documented per-test scale. Pure-spin one-second angular speed must be within 0.02 rad/s of 2.5, stopping threshold time must be 2.5-2.8 seconds, and rate-to-rate stopping times must differ by at most one 60 Hz tick. Plane peak energy may not exceed initial 21.2 by more than 0.01.

The first focused run exposed an overly strict airborne assertion that required bit-exact constant angular velocity despite existing free-integration rounding. It was replaced by an exact simultaneously integrated zero-resistance control plus a 1e-5 absolute drift bound. The original failure log is retained.

PhysicsBenchmark accepts --spin-resistance-length and --spin-workload, reports configuration and spin counters/timing, and preserves the original workload by default. The motion variant adds opposing unit linear velocities and 4 rad/s normal-dominated spin to the existing dynamic box pair geometry. Both compared variants use identical motion; only the resistance length differs.

## Validation Commands

Artifacts and scripts: bin-int/phase37-spin/ (ignored, not phase commit files).

~~~powershell
python bin-int/phase37-spin/baseline.py
python bin-int/phase37-spin/validate.py
python bin-int/phase37-spin/measure.py
python bin-int/phase37-spin/summarize.py
~~~

The scripts record exact subprocess commands, exits, timings and source hashes. The build uses the existing MSBuild x64 projects; no repository build configuration was changed. Baseline full correctness passed before baseline measurements. Timed children run serially with normal process priority, affinity mask 4 (logical CPU 2), outside build/test activity.

Sandbox shell setup repeatedly failed with helper_unknown_error; scoped shell/file/build actions used the approved escalation route. No automatic approval rejection occurred.

## Debug Result

Measured: x64 test build, 334 focused spin checks, enabled exact-stack 60/120 Hz gates, and all **18,432 full physics checks pass**. Full-suite runtime 189.16 seconds. Application build passes. No new non-gating diagnostic issue; no NaN/Inf in tested states. Logs: Debug-{tests-build,spin,spin-stack,full,application-build}.log.

## Release Result

Measured: x64 test build, 334 focused spin checks, enabled exact-stack 60/120 Hz gates, and all **18,432 full physics checks pass**. Full-suite runtime 16.44 seconds. Application, benchmark and exact-world replay builds pass. The baseline had 18,098 checks; all 334 additional checks are in the full suite.

Nominal Release remains optimized with the existing **/MTd Debug static CRT override**. These are not true Release CRT allocator/runtime measurements. Logs: Release-{tests-build,spin,spin-stack,full,application-build}.log and {benchmark,replay}-build.log.

## Stability Results

Measured pure-spin test (sleeping disabled, fixed ell=0.05, gravity 12, unit sphere, initial omega=4 rad/s):

| Rate | Spin after 1 s | Time to <=0.02 rad/s | Peak / initial energy | Spin after 4 s |
|---|---:|---:|---:|---:|
| 60 Hz | 2.5 | 2.66667 s | 21.2 / 21.2 | 0.00000641411 |
| 120 Hz | 2.5 | 2.65833 s | 21.2 / 21.2 | -0.00000293654 |

Both Debug and Release give these results. The rate difference is 0.00833 seconds, below the predeclared one-60-Hz-tick tolerance. With either material set to zero, the one-second spin remains 4 rad/s and no torsional impulse occurs. The airborne enabled/reference rotations and angular velocities match exactly.

The enabled exported stack passes both established windows at 60/120 Hz: zero settled linear/angular speed and excursion; maximum live depth 0.0190279 / 0.0171305; minimum mean Y 4.46405 / 4.46966; 16 manifolds and 64 points; peak total energy including the initial state 864. These preserve the established gates, including the original full-suite strict penetration check.

Measured exact-world replays retain all exported geometry, ordering, transforms, velocities, friction, restitution and gravity. Each runs 1,500 fixed 1/60-second ticks (25 seconds), one solver pass and default sleep policy. World files are the frozen Phase 34 exports: 21-body box world, 185-body lattice, and the Phase 36 floor/first-sphere export. The only enabled-mode change is ell=0.05 on both sides of every contact.

**Exact zero reference:** all three complete body CSV SHA-256 values and every common non-timing frame field match the archived Phase 36 executable. Enabled repeats also match each other's full body hashes and non-timing fields. All frame/body values are finite. Raw histories, eligibility diagnostics and hashes are retained in measurements.json and the named CSVs.

| Measured final state (25 s) | Phase 36 / zero length | Enabled length 0.05 |
|---|---:|---:|
| Boxes: linear speed | 0 | 0 |
| Boxes: angular speed | 0 | 0 |
| Boxes: depth | 0.0190279484 | 0.0190279484 |
| Boxes: energy | 857.09824 | 857.09824 |
| Boxes: sleeping | 16 | 16 |
| Boxes: manifolds | 16 | 16 |
| Boxes: points | 64 | 64 |
| Sphere: linear speed | 0 | 0 |
| Sphere: angular speed | 0 | 0 |
| Sphere: depth | 0.00399775384 | 0.00399775384 |
| Sphere: energy | 18.0000272 | 18.0000272 |
| Sphere: sleeping | 1 | 1 |
| Lattice: linear speed | 2.51013374 | 2.7237618 |
| Lattice: angular speed | 2.50781727 | 2.7183249 |
| Lattice: depth | 0.0200152397 | 0.0200713295 |
| Lattice: energy | 3410.67768 | 3428.98706 |
| Lattice: linear kinetic energy | 126.108803 | 146.824188 |
| Lattice: rotational kinetic energy | 60.3972183 | 58.4362581 |
| Lattice: max relative normal spin | 1.80215418 | 1.68713546 |
| Lattice: sleeping | 0 | 1 |
| Lattice: manifolds | 191 | 187 |
| Lattice: points | 361 | 376 |
| Lattice: solved constraints | 361 | 374 |

Boxes retain mean Y 4.46405333; the sphere retains Y 1.50000226. Both are fully sleeping and have zero physical kinetic energy at 25 s. The lattice still has 179 moving bodies under the unchanged 0.05 linear / 0.02 angular thresholds, versus 180 in the reference.

**Lattice interpretation:** this is not a successful global-settling result. Final maximum normal-axis relative spin and rotational kinetic energy are lower, but linear kinetic energy, final maximum linear/angular speeds and total energy are higher than reference. Adding a dissipative row changes the many-contact trajectory and subsequent contact work; no claim of uniformly reduced residual motion is made. Peak total energy after the first tick is 30,236.4001105 in both modes, below the initial 30,240. The isolated-row energy proof and focused tests establish the new row's dissipation; final total energy alone does not prove every contact path is energy-safe.

The diagnostic also retains substantial transient penetration: maximum live-anchor depth in the 5-20 s window is 0.362613 reference / 0.408557 enabled; in the 20-25 s window it is 0.0998285 / 0.115171. These are reported rather than treated as settled-state acceptance. Box/single-sphere gates pass; the lattice is diagnostic in this phase and its broader settling/robustness remains open.

At 25 s, overlapping awake-body eligibility failures are:

| Measured failure | Reference | Enabled |
|---|---:|---:|
| Linear speed | 177 | 179 |
| Angular speed | 180 | 179 |
| Dwell | 180 | 179 |
| Depth | 1 | 1 |
| Correction/pose motion | 180 | 179 |
| Invalid state/shape | 0 | 0 |
| Members of unready islands | 180 | 179 |
| Individually eligible but blocked by island | 0 | 0 |

Failure counts must not be summed. Enabled sleeping count is one; 179 genuinely moving spheres remain awake. No thresholds were relaxed. Full per-tick histories are in *-eligibility-frames.csv; final per-body classifications are in *-eligibility-bodies.csv.

Applied torsional increments over one enabled replay: boxes 2,785; single sphere 30; lattice 257,102. At the last lattice tick, 374 enabled rows are visited and 179 nonzero increments are applied. These are operation counts, not impulse magnitudes. The relative-spin diagnostic uses retained world-space contact normals after the step.

## Benchmark Results

Measured protocol: normal process priority, affinity mask 4 / logical CPU 2, serial runs after all builds/tests, Release x64 /Ox /Oi /fp:precise /MTd with profiling. Source hashes were frozen before validation. Original collision/separated order is baseline / zero / enabled / enabled / zero / baseline; loaded order is zero / enabled / enabled / zero. Each process uses two warmup samples and five measured samples per size. Table values are medians of two process medians (ten measured samples per mode/size); stage values are medians of the process means reported by the existing benchmark.

Original collision samples construct a fresh identical world outside the timer and measure one 1/120-second step. They begin with zero velocities and therefore no supporting normal impulse; enabled rows are visited but apply no torsion. Separated samples retain four warmup steps and eight measured steps per sample, all awake, with no candidates. The loaded variant uses the same collision geometry plus the fixed opposing linear/spin velocities described above, with identical initial motion in zero/enabled modes.

| Workload | Bodies | Phase 36 step ms | Phase 37 zero ms | Phase 37 enabled ms | Enabled spin ms | Enabled applied increments |
|---|---:|---:|---:|---:|---:|---:|
| collision | 100 | 4.446400 | 4.704500 | 4.515200 | 0.003430 | 0 |
| collision | 1000 | 43.742750 | 45.712400 | 45.183850 | 0.034890 | 0 |
| collision | 10000 | 474.423900 | 476.973700 | 480.185150 | 0.340290 | 0 |
| separated | 100 | 0.036881 | 0.022732 | 0.023213 | 0.000000 | 0 |
| separated | 1000 | 0.255050 | 0.233681 | 0.240150 | 0.000000 | 0 |
| separated | 10000 | 2.853069 | 2.563306 | 2.625275 | 0.000000 | 0 |
| loaded | 100 | Not available | 4.629700 | 4.642150 | 0.006340 | 38 |
| loaded | 1000 | Not available | 45.439950 | 45.448500 | 0.062270 | 375 |
| loaded | 10000 | Not available | 481.909000 | 478.632300 | 0.617550 | 3750 |

Original collision candidate/contact counts are 38 / 375 / 3,750; each has one manifold point and one solver pass. These counts and all physical fingerprints match Phase 36 in both zero and unloaded-enabled mode. Separated workloads have zero candidates, contacts, solver rows and torsional work. The loaded variant retains the same contact counts; every contact applies a spin impulse when enabled. Loaded final-state fingerprints intentionally differ because physical resistance is enabled and are deterministic across repeats. A Phase 36 binary with the newly added loaded CLI is not available; loaded before/after cost is explicitly the current binary's zero/enabled comparison.

At 10,000 loaded bodies, zero/enabled solver means are 134.36355 / 133.08778 ms; the directly timed spin-row subset is 0.61755 ms. Whole-step process medians range 480.4408-483.3772 ms (zero) and 477.7409-479.5237 ms (enabled). The measured whole-step difference is not claimed as a speedup: the implementation adds work and the surrounding timings vary.

For the original 10,000-body collision case, zero versus archived baseline differs by +2.5498 ms (+0.54%, Derived). The 100/1,000-body comparisons vary more: +5.80% / +4.50%, with overlapping/noisy process ranges, including a slower first zero run. Every raw sample summary and range is retained; none was discarded. Separated baseline ranges are also broad (at 10,000, 2.644862-3.061275 ms); do not infer an optimization from its lower candidate median.

Measured body size grows 672 -> 680 bytes for the private material field. Source-level extra storage also includes per-constraint accumulated spin and its prepared length. Allocation counts and isolated cache/layout attribution are Not available; no unsupported allocation/performance claim is made.

Exact-world primary timing windows use ticks 300-1200 inclusive (901 samples) after five simulated seconds. The late window uses ticks 1201-1500. One reference and one zero replay plus two enabled repeats were run. These are diagnostic trajectory timings, not a claim of algorithmic speedup between physically different worlds.

| Measured primary median | Reference step ms | Zero step ms | Enabled step ms | Enabled solver ms | Enabled spin ms |
|---|---:|---:|---:|---:|---:|
| boxes | 0.031900 | 0.034400 | 0.034350 | 0.004600 | 0.000000 |
| sphere | 0.002700 | 0.002900 | 0.002900 | 0.000200 | 0.000000 |
| lattice | 16.382700 | 16.671600 | 16.667500 | 11.155600 | 0.053250 |

All 28 final measurement processes exited zero and passed their finite-state, gate and deterministic-reference checks. The lattice material was not retuned after inspecting results. Detailed commands, samples, counters, hashes and windows: protocol.json, measurements.json, summary.json and summary.txt.

## Behavior Changes

Resistance is disabled by default. With two positive material lengths, load-bearing contacts dissipate relative contact-normal spin using the bounded law above. The API wakes affected bodies through existing propagation. It does not serialize or enable materials in scene/editor/application configuration.

## Known Limitations

- No tangent-plane rolling resistance; residual rolling in the lattice is expected and belongs to Phase 38.
- No torsional warm starting across timesteps; only within-step accumulated projection is implemented.
- Positive-TOI ballistic impact response is unchanged; torsion acts in the persistent contact constraint path.
- The resistance length is supplied explicitly and does not adapt to contact patch shape or geometric scaling.
- Existing angular impulse speed limiting and integration accuracy remain in effect.
- Profiling is the existing single-thread global instrumentation; no parallelism was added.

## Out-of-Scope Findings

- The user-owned .gitignore already has a trailing blank line at line 33, causing the whole-tree git diff --check to fail. It is byte-identical to entry and is not fixed under Phase 37.
- PhysicsBody.cpp ApplyImpulseAngular retains its existing 30 rad/s speed clamp. It can alter equal/opposite momentum behavior above that existing operating cap. Focused conservation tests stay below it; this phase does not redesign that policy.
- The plan and Phase 36 review's status prose predates the approved Phase 36 tag; those user-owned/historical documents are not rewritten.

## git diff --check

Whole-tree command exit 2; literal stdout:

~~~text
.gitignore:33: new blank line at EOF.
~~~

This is the unchanged user-owned .gitignore, confirmed against the entry SHA-256. Pre-existing line-ending warnings for the scene and plan may also appear on stderr. Full output is saved in final-checks.json. The scoped check over the seven Phase 37 paths exits 0 with no output. The new review is also checked separately because ordinary git diff excludes untracked files: git diff --no-index --check reports no whitespace diagnostics (exit 1 denotes the new-file difference). No user-owned file was edited to hide the global result.

## git status --short

Literal final working-tree status (includes untouched user work):

~~~text
 M .gitignore
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h
 M GEngine/include/GEngine/Physics/PhysicsBody.h
 M GEngine/include/GEngine/Physics/PhysicsProfile.h
 M GEngine/src/Scene/_Scene.cpp
 M PhysicsBenchmark/src/main.cpp
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
?? docs/physics/PHASE_37_REVIEW.md
?? docs/rendering/
~~~

No files were staged; HEAD and approved tags are unchanged. No commit, tag, push, or Phase 38 work was performed.

## Human Decision

PENDING
