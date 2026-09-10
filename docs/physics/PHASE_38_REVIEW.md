# Phase 38 Review

## Status

PHASE 38 STATUS: AWAITING HUMAN REVIEW

## Objective

Add bounded tangent-plane rolling contact resistance after approved Phase 37 spin resistance. This phase includes the human-approved final sphere-lattice validation experiment described below. Approval of that experiment does not approve Phase 38.

## Baseline Commit

4f480a13e61bd2cf284cb9cc02243a69b0b95130 on physics/refactor.

## Previous Approved Tag

physics-phase-37-approved, verified at HEAD. Phase 38 has no approved tag.

## Audit Findings Addressed

No historical audit bug is claimed fixed. This is the human-requested contact-model extension, relevant to physical settling and PHYS-PERF-005. The historical audit and user-owned plan remain unchanged.

## Allowed Scope

Four production files, two existing test/benchmark files, this review: seven files.

## Files Changed

- GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
- GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h
- GEngine/include/GEngine/Physics/PhysicsBody.h
- GEngine/include/GEngine/Physics/PhysicsProfile.h
- PhysicsTests/src/main.cpp
- PhysicsBenchmark/src/main.cpp
- docs/physics/PHASE_38_REVIEW.md

## Implementation Summary

Each body has a finite nonnegative rolling-resistance length ell in world units (maximum tangent-plane resisting torque / normal force). The pair combines min(ellA,ellB); zero on either side disables the row. It is an explicit phenomenological material length; there is no inferred geometric radius or automatic scaling. Changing the value calls the existing wake API.

For an orthonormal basis T of the contact-normal tangent plane, relative speed s=T^T*(omegaB-omegaA), K=T^T*(inverseInertiaA+inverseInertiaB)*T, old accumulated impulse p, and L=ell*max(lambdaNormal,0), minimize:

    0.5*x^T*K*x - (K*p-s)^T*x, subject to |x| <= L.

The SPD two-dimensional disk solve uses a bounded scalar multiplier search on (K+alpha*I)^-1*(K*p-s). Only T*(x-p) is applied, equally and oppositely. This is a coupled anisotropic solve, not independent box clamps or radial projection of an anisotropic unconstrained result. World-basis choice depends deterministically on the normal's least-aligned coordinate axis. Float impulses and both resulting angular velocities are checked before mutation.

The rolling state starts cold in every PreSolve, with positive finite dt required. It executes after the unchanged normal/sliding and spin solve, including the manifold normal-block path. Each point uses its own load, bounding the combined aligned patch torque by ell times total normal impulse. No rolling warm start crosses a step, feature replacement, wake or material edit. Zero rows return before timers/inertia work. Counters record visited enabled rows and nonzero incremental angular impulses; rolling time is an inclusive subset of solver time.

## Tests Added / Modified

Analytic load/length/radius/mass and accumulated-budget tests; loss of support, paused/reverse step, static/kinematic and pair reversal; anisotropic circular-disk KKT and energy checks; material validity/wake; pure-spin separation; exact airborne zero control; no-slip diagonal plane rolling at 60/120 Hz with sleeping disabled; enabled exact exported stack at both rates.

Added before final measurements: `TestCombinedAngularResistanceAnisotropy`, accessible via `--angular-resistance-coupling` and included in the rolling-focused and full suites. Two rotated, unequal anisotropic boxes have spin and rolling enabled together. The bounded 0.05-length case and an isolated unconstrained 100-length mathematical case each run 32 contact passes, exact repeats and reversed pairs. The latter is regression-only and does not change the lattice's fixed 0.05 materials. These add 28 checks for finite state, accumulated bounds, repeat determinism, reversal, energy, momentum and converged coupling. Both Debug and Release pass.

Measured combined-case initial/peak rotational energy is 14.8633 in all variants. Final bounded/unconstrained energies are 14.7611 / 6.7382. Unconstrained final relative angular speed is 6.664e-8. Per-pass energy tolerance is 1e-6 * max(1, initial energy); pair reversal, momentum, final-pass coupling change and cancellation tolerances are 3e-5. Exact repeats agree bit-for-bit in the tested final velocities. The bounded case retains physical residual speed 2.82933 because its resisting impulse is load-limited.

## Validation Commands

Ignored artifacts: bin-int/phase38-rolling/.

~~~powershell
python bin-int/phase38-rolling/first-validation.py
python bin-int/phase38-rolling/validate.py
python bin-int/phase38-rolling/measure-controls.py
python bin-int/phase38-rolling/summarize.py
python bin-int/phase38-rolling/final-protocol/validate.py
python bin-int/phase38-rolling/final-protocol/run-final.py
~~~

The scripts record exact subprocess commands, exit codes, timings and frozen source hashes. The baseline consists of the archived Phase 37 approval binaries and source at 4f480a1, with a fresh baseline full-suite pass. Existing MSBuild x64 projects are used. The approved final experiment uses final-protocol/run-final.py and final-protocol/lattice-protocol-approved.json. The older proposed measure-final-lattice.py was superseded and was not run. Final validation has 16 successful build/test commands, with a frozen candidate executable and source hashes.

Sandbox setup repeatedly fails with helper_unknown_error; scoped reads/writes/builds used approved escalation. No automatic approval rejection occurred.

## Debug Result

Measured: x64 test/application builds pass; all **18,799 full physics checks pass**, including 367 added rolling checks. Focused rolling (367), approved spin (334), and enabled exact stack at 60/120 Hz pass. Full-suite runtime 191.28 seconds. No new NaN/Inf or non-gating diagnostic issue. Final logs under final-protocol/: Debug-{tests-build,rolling,spin,stack,full,application-build}.log.

## Release Result

Measured: x64 test/application builds pass; all **18,799 full physics checks pass**, including 367 added rolling checks. Focused rolling (367), approved spin (334), and enabled exact stack at 60/120 Hz pass. Full-suite runtime 16.84 seconds. No new NaN/Inf or non-gating diagnostic issue. Final logs under final-protocol/: Release-{tests-build,rolling,spin,stack,full,application-build}.log.

Benchmark and replay builds also pass. Nominal Release retains the optimized /Ox /Oi /fp:precise configuration with the **/MTd Debug static CRT override**; these are not true Release CRT allocator/runtime measurements.

## Stability Results

Measured isolated plane rolling, with sleeping disabled, unit sphere/mass, gravity 12, diagonal initial linear speed 2 and no-slip angular speed 2, rolling length 0.05:

| Rate | Speed at 1 s | Time to both speeds <=0.02 | Peak / initial energy | Final linear speed | Final angular speed |
|---|---:|---:|---:|---:|---:|
| 60 Hz | 1.58017 | 4.65000 s | 20.8 / 20.8 | 0.0000116349 | 0.000115911 |
| 120 Hz | 1.57660 | 4.64167 s | 20.8 / 20.8 | 0.00000570708 | 0.0000352628 |

Debug and Release agree. Predicted no-slip deceleration ell*m*g*R/(I+m*R^2) is 3/7, predicted one-second speed 11/7 and exact rest time 14/3 seconds. The measured threshold time difference is 0.00833 s. Acceptance: speed at one second within 0.03; threshold time 4.4-5.0 s; rate difference <=1/30 s; peak energy <=20.81. The sequential sliding/rolling split leaves a small per-step slip while decelerating; it converges as dt decreases. With zero material on either side, no rolling impulse occurs and eight-second linear/angular speeds remain approximately 2.00268 / 1.99868. Airborne enabled/reference trajectories match exactly; a pure normal-spin row matches Phase 37 exactly and applies zero rolling impulse.

For a cold isolated row the energy change is s^T*delta + 0.5*delta^T*K*delta. Minimization over a disk containing zero makes this nonpositive. A rotated anisotropic test checks energy, absence of normal-axis torque, unconstrained tangent cancellation and boundary KKT conditions. Float response/bound tolerances are 2e-6 to 3e-5. Later solver passes may retract an earlier impulse as its supporting load shrinks; this is tested. Prescribed kinematic motion can transfer external energy while relative rolling decreases, as with ordinary contact friction.

Enabled exact exported stack gates pass unchanged at 60/120 Hz: zero settled speed/excursion, maximum live depth 0.0190279 / 0.0171305, minimum mean Y 4.46405 / 4.46966, 16 manifolds / 64 points, peak energy 864. Existing 5-18 s and 15-20 s windows and all original stability tolerances are retained.

Exact-world controls use frozen exported geometry, materials, transforms, body order and gravity: 21-body boxes, 185-body lattice (180 dynamic spheres) and floor/first-sphere export. Each runs 1,500 fixed 1/60-second ticks (25 s), one solver pass, default sleep policy, and Phase 37 spin length 0.05. Rolling-zero body CSV SHA-256 and every common non-timing frame counter match archived Phase 37 for all three worlds. Enabled boxes/sphere repeat deterministically; all histories are finite. Timings use ticks 300-1200 inclusive (901 samples) and late ticks 1201-1500.

| Final (25 s) | Phase 37 / rolling zero | Rolling 0.05 |
|---|---:|---:|
| Boxes: sleeping / linear / angular speed | 16 / 0 / 0 | 16 / 0 / 0 |
| Boxes: depth / energy | 0.0190279484 / 857.09824 | 0.0190279484 / 857.09824 |
| Boxes: manifolds / points | 16 / 64 | 16 / 64 |
| Sphere: sleeping / linear / angular speed | 1 / 0 / 0 | 1 / 0 / 0 |
| Sphere: depth / energy | 0.00399775384 / 18.0000272 | 0.00399769051 / 18.0000272 |
| Lattice: sleeping / moving | 1 / 179 | 115 / 0 |
| Lattice: max linear / angular speed | 2.72376180 / 2.71832490 | 0.0367920212 / 0.0114901066 |
| Lattice: linear / angular kinetic energy | 146.824188 / 58.4362581 | 0.00667138713 / 0.000125659389 |
| Lattice: total energy / depth | 3428.987058 / 0.0200713295 | 3296.92571 / 0.0200700313 |
| Lattice: manifolds / points | 187 / 376 | 262 / 264 |

The enabled lattice values at 25 seconds are taken from tick 1,500 of the first approved 60 Hz sleeping-enabled final run. Enabled boxes/sphere apply 2,160 / 30 rolling increments over 25 seconds. Their primary median whole steps are 0.0342 / 0.0026 ms. Lattice zero/reference primary medians are 16.7296 / 16.3898 ms; both have exactly the same physical trajectory. The final approved 120-second protocol is reported below; this 25-second table preserves the earlier Phase 37 reference.

### Final lattice protocol approval

The human explicitly approved this validation experiment only. Phase 38 remains unapproved. The fixed protocol is recorded before execution in `bin-int/phase38-rolling/final-protocol/lattice-protocol-approved.json`:

- Exact exported 185-body world with 180 dynamic spheres; original geometry, masses, initial transforms/velocities, gravity (0,-12,0), sliding friction, restitution and creation order are preserved. Input SHA-256 and every body's slot:generation mapping are retained.
- Spin length **0.05**, rolling length **0.05**, on every body and hence both sides of applicable contacts. No coefficient tuning or production physics edits after protocol approval.
- Fixed **60 Hz** and **120 Hz**, **120 simulated seconds**, **one solver pass**; sleeping enabled and matching sleeping-disabled control. Each rate/mode is repeated once from identical initial state to verify determinism.
- Existing Phase 36 speed thresholds **0.05 linear / 0.02 angular**, dwell **0.5 seconds**, pose and contact eligibility remain unchanged. There is no damping or other physical tuning.
- Final closed window **110-120 seconds**: at least **171 of the same 180 identities** continuously below both speed thresholds in the sleeping-disabled control, and at least **171 of the same identities** continuously sleeping in the enabled run. Rates pass independently. Sampling includes both boundaries: 601 samples at 60 Hz, 1,201 at 120 Hz. Continuous means every completed fixed physics tick, with no gaps; simulation dt uses the engine's float representation of 1/rate.
- Final-window maximum live-anchor penetration **<=0.035**; finite body, contact and energy state throughout. Peak total mechanical energy including the initial state **<=100.5% of 30,240 = 30,391.2**. The previously approved stricter box/isolated gates remain in their focused tests.
- All final-window sleep/wake transitions are recorded with body identity and tick, including the transition into the 110-second boundary. Per-tick sleeping counts, speed eligibility, dwell/depth failures, separate kinetic/potential/total energy histories, speeds, contact counts, solver/resistance time and work counters, applied increment counts and state fingerprints are retained.
- If either rate fails, record the failure and return Phase 38 to **FIX PENDING** for human review without retuning. Passing all runs permits **AWAITING HUMAN REVIEW**, not phase approval.

### Final lattice measurements

Measured: all eight fixed-protocol processes completed, two repeats of each rate/mode. Every predeclared gate passes at both rates.

| Rate / sleeping | Continuous quiet identities | Continuous sleeping identities | Required cohort | Final-window sleep / wake transitions | Depth maximum | Peak total / initial | Result |
|---|---|---|---|---|---|---|---|
| 60 Hz enabled | 180 | 180 | 180/171 | 0 / 0 | 0.0200038608 | 30240 / 30240 | PASS |
| 120 Hz enabled | 180 | 180 | 180/171 | 0 / 0 | 0.020003017 | 30240 / 30240 | PASS |
| 60 Hz disabled | 180 | 0 | 180/171 | 0 / 0 | 0.0200027805 | 30240 / 30240 | PASS |
| 120 Hz disabled | 180 | 0 | 180/171 | 0 / 0 | 0.0200002212 | 30240 / 30240 | PASS |

Quiet means strictly below **both** existing speed thresholds at every sampled tick. Sleeping-enabled quiet counts include bodies whose velocities have been zeroed by sleeping; only the matching sleeping-disabled cohort establishes physical settling. Each mode is gated independently; cohorts are not pooled across modes or rates. Penetration and energy gates are evaluated independently of settling.

**60 Hz enabled**: continuously settled: k:k for every integer k from 6 through 185 (inclusive; slot:generation). Continuously sleeping: k:k for every integer k from 6 through 185 (inclusive; slot:generation).

**120 Hz enabled**: continuously settled: k:k for every integer k from 6 through 185 (inclusive; slot:generation). Continuously sleeping: k:k for every integer k from 6 through 185 (inclusive; slot:generation).

**60 Hz disabled**: continuously settled: k:k for every integer k from 6 through 185 (inclusive; slot:generation). Continuously sleeping: None.

**120 Hz disabled**: continuously settled: k:k for every integer k from 6 through 185 (inclusive; slot:generation). Continuously sleeping: None.

Identity formulas/ranges above are inclusive slot:generation values, not ranks in a sorted motion list. This exported world assigns the dynamic bodies identities 6:6 through 185:185, with both slot and generation increasing together. The full [cohort lists](../../bin-int/phase38-rolling/final-protocol/cohort-identities.json) and per-run `identities.csv` preserve the exact mappings to exported body order. All 185 identities remain unchanged throughout each run.

#### Motion, contacts and transition histories

| Rate / sleeping | Whole-run max linear / angular | Final-window max linear / angular | Final-window sleeping min-max | Final manifolds / points | Final-window manifold / point ranges |
|---|---|---|---|---|---|
| 60 Hz enabled | 14.1999912 / 6.41615534 | 0 / 0 | 180-180 | 267 / 268 | [267, 267] / [268, 268] |
| 120 Hz enabled | 14.5000191 / 6.25115108 | 0 / 0 | 180-180 | 223 / 223 | [223, 223] / [223, 223] |
| 60 Hz disabled | 14.1999912 / 6.41615534 | 0.000374043273 / 6.38526471e-05 | 0-0 | 271 / 271 | [271, 271] / [271, 271] |
| 120 Hz disabled | 14.5000191 / 7.12122917 | 0.000137990719 / 5.08886251e-06 | 0-0 | 245 / 246 | [245, 245] / [246, 246] |

- 60 Hz enabled: [per-tick final-window counts](../../bin-int/phase38-rolling/final-protocol/lattice-60-sleep-0-final-ticks.csv), [every final-window sleep/wake event](../../bin-int/phase38-rolling/final-protocol/lattice-60-sleep-0-final-transitions.csv), [full identity sleep/quiet history](../../bin-int/phase38-rolling/final-protocol/lattice-60-sleep-0-sleep-history.csv). Whole-run sleep/wake transitions: 270 / 90.
- 120 Hz enabled: [per-tick final-window counts](../../bin-int/phase38-rolling/final-protocol/lattice-120-sleep-0-final-ticks.csv), [every final-window sleep/wake event](../../bin-int/phase38-rolling/final-protocol/lattice-120-sleep-0-final-transitions.csv), [full identity sleep/quiet history](../../bin-int/phase38-rolling/final-protocol/lattice-120-sleep-0-sleep-history.csv). Whole-run sleep/wake transitions: 534 / 354.
- 60 Hz disabled: [per-tick final-window counts](../../bin-int/phase38-rolling/final-protocol/lattice-60-awake-0-final-ticks.csv), [every final-window sleep/wake event](../../bin-int/phase38-rolling/final-protocol/lattice-60-awake-0-final-transitions.csv), [full identity sleep/quiet history](../../bin-int/phase38-rolling/final-protocol/lattice-60-awake-0-sleep-history.csv). Whole-run sleep/wake transitions: 0 / 0.
- 120 Hz disabled: [per-tick final-window counts](../../bin-int/phase38-rolling/final-protocol/lattice-120-awake-0-final-ticks.csv), [every final-window sleep/wake event](../../bin-int/phase38-rolling/final-protocol/lattice-120-awake-0-final-transitions.csv), [full identity sleep/quiet history](../../bin-int/phase38-rolling/final-protocol/lattice-120-awake-0-sleep-history.csv). Whole-run sleep/wake transitions: 0 / 0.

Transition files include headers even when there are zero events. They compare identities with the preceding tick, so boundary transitions and simultaneous sleep/wake changes cannot disappear in aggregate counts. No final-window transition is omitted. WakeUp resets inactivity to zero (PhysicsBody.cpp:124-128), and TrySleep occurs only at the end-of-step island decision (PhysicsSystem.cpp:870-886), followed by source storage. With fixed dt below the 0.5-second dwell, a body cannot wake and re-sleep between samples. Repeat histories and transition lists are identical.

| Rate / sleeping | Linear failures final / window max | Angular failures final / window max | Dwell failures final / window max | Penetration failures final / window max | Pose failures final / window max | Island-unready final / window max |
|---|---|---|---|---|---|---|
| 60 Hz enabled | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| 120 Hz enabled | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| 60 Hz disabled | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| 120 Hz disabled | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |

Eligibility counts concern awake dynamic bodies and may overlap. Sleeping-disabled dwell is a diagnostic shadow of the unchanged 0.5-second policy along the fully active trajectory; sleeping-enabled dwell uses the engine timer, including its wake resets. Penetration eligibility uses the existing 0.02001 tolerance, distinct from the 0.035 final stability bound. Pose eligibility and island readiness remain active. Per-tick classifications are in `eligibility-frames.csv`; final per-body classifications are in `eligibility-bodies.csv`; overlapping failure body-tick totals are retained in `derived-results.json`. All invalid-state failure counts are zero.

Repeat equality is required within each rate/mode. Cross-rate comparison uses the predeclared independent settling, penetration and energy gates; the rates reach different resting configurations and potential energies.

#### Separate mechanical-energy histories

| Rate / sleeping | Final translational KE | Final rotational KE | Final potential | Final total | Peak total (including t=0) |
|---|---|---|---|---|---|
| 60 Hz enabled | 0 | 0 | 3218.96478 | 3218.96478 | 30240 |
| 120 Hz enabled | 0 | 0 | 3249.89583 | 3249.89583 | 30240 |
| 60 Hz disabled | 2.36838932e-07 | 1.45213679e-09 | 3219.02459 | 3219.02459 | 30240 |
| 120 Hz disabled | 1.3255166e-08 | 5.07899649e-12 | 3224.1517 | 3224.1517 | 30240 |

| Rate / sleeping | Peak translational KE | Peak rotational KE | Peak potential | Final-window peak total |
|---|---|---|---|---|
| 60 Hz enabled | 17639.978 | 143.521729 | 30240 | 3218.96478 |
| 120 Hz enabled | 18147.6453 | 194.772617 | 30240 | 3249.89583 |
| 60 Hz disabled | 17639.978 | 143.521729 | 30240 | 3219.02459 |
| 120 Hz disabled | 18147.6453 | 210.65547 | 30240 | 3224.1517 |

- 60 Hz enabled: [separate component energy histories, including t=0](../../bin-int/phase38-rolling/final-protocol/lattice-60-sleep-0-energy-history.csv).
- 120 Hz enabled: [separate component energy histories, including t=0](../../bin-int/phase38-rolling/final-protocol/lattice-120-sleep-0-energy-history.csv).
- 60 Hz disabled: [separate component energy histories, including t=0](../../bin-int/phase38-rolling/final-protocol/lattice-60-awake-0-energy-history.csv).
- 120 Hz disabled: [separate component energy histories, including t=0](../../bin-int/phase38-rolling/final-protocol/lattice-120-awake-0-energy-history.csv).

Every tick reports separate `linear_ke`, `angular_ke`, `potential_energy` and `energy` columns in the linked `frames.csv` below; `initial-energy.csv` adds t=0. Potential uses the preserved gravity and center-of-mass height, with the original world origin. Total-energy peaks include the initial **30,240**, avoiding an incorrectly reduced post-impact denominator. Component-sum roundoff is below 1e-6 for every frame. The conservative ceiling is **30,391.2**. All runs remain finite; neither a low terminal energy nor sleeping substitutes for the continuous identity gate.

#### Solver and resistance cost/work

Measured with normal process priority and affinity mask 4 (logical CPU 2), the same nominal optimized x64 /MTd build. Primary timing window is 5-20 seconds inclusive (901 / 1,801 samples); final window is 110-120 seconds inclusive (601 / 1,201). These are trajectory windows, without discarded trials. The table reports the median of the two repeat medians; profiles exclude CSV writing and diagnostic collection outside the physics update. Spin and rolling time are subsets of solver time.

| Rate / sleeping | Window | Step ms | Solver ms | Spin ms | Rolling ms |
|---|---|---|---|---|---|
| 60 Hz enabled | primary | 11.9409 | 8.83105 | 0.0411 | 0.87295 |
| 60 Hz enabled | late | 0.22685 | 0.0261 | 0 | 0 |
| 120 Hz enabled | primary | 10.29105 | 7.5484 | 0.0369 | 0.7211 |
| 120 Hz enabled | late | 0.21125 | 0.0241 | 0 | 0 |
| 60 Hz disabled | primary | 15.89345 | 9.71255 | 0.04585 | 0.8773 |
| 60 Hz disabled | late | 18.3624 | 9.0654 | 0.04525 | 0.26065 |
| 120 Hz disabled | primary | 14.2876 | 8.54185 | 0.041 | 0.63165 |
| 120 Hz disabled | late | 15.97545 | 8.801 | 0.04255 | 0.0896 |

| Rate / sleeping | Total solver / spin / rolling seconds | Visited spin / rolling rows | Applied spin / rolling increments |
|---|---|---|---|
| 60 Hz enabled | 20.0308734 / 0.091997 / 1.2325054 | 586902 / 586902 | 432224 / 432235 |
| 120 Hz enabled | 22.3642805 / 0.0958555 / 1.3804984 | 659783 / 659783 | 400302 / 400314 |
| 60 Hz disabled | 66.9957962 / 0.3260008 / 2.4048504 | 2020499 / 2020499 | 1792986 / 1792998 |
| 120 Hz disabled | 118.723545 / 0.5746106 / 2.6420387 | 3655428 / 3655428 | 3073363 / 3073375 |

Totals above are the first complete repeat, with all per-tick values retained for both. Work means profiled row visits and nonzero applied incremental angular-impulse operations; these counters are not impulse magnitudes or physical work in joules. Separate per-row mechanical-work attribution is Not available in the unchanged profiler. The energy histories measure the complete physical trajectory.

#### Deterministic fingerprints and evidence

Both repeats match exactly at each rate/mode in the full reported body-state history, identity sleep/quiet history, non-timing frame fields, identity mapping and eligibility history. Timing and CPU-cycle measurements are intentionally excluded. Body hashes cover all reported poses, velocities, bounds and mass values at round-trip float precision; they do not claim to serialize every hidden solver cache. Per-tick SHA-256 state fingerprints are also retained.

| Rate / sleeping | Full body-state CSV SHA-256 | Frame and fingerprint histories |
|---|---|---|
| 60 Hz enabled | `d5db4ea2b5c313fc00c794a1dec86f2e2e68f3805184f67b86fe10278cd520e9` | [frames](../../bin-int/phase38-rolling/final-protocol/lattice-60-sleep-0-frames.csv) / [per-tick hashes](../../bin-int/phase38-rolling/final-protocol/lattice-60-sleep-0-state-fingerprints.csv) |
| 120 Hz enabled | `e792565f2be9e1298d6e477a2c864f33129f493189b7e25b52a4411e5e11886c` | [frames](../../bin-int/phase38-rolling/final-protocol/lattice-120-sleep-0-frames.csv) / [per-tick hashes](../../bin-int/phase38-rolling/final-protocol/lattice-120-sleep-0-state-fingerprints.csv) |
| 60 Hz disabled | `dcf98fdc62fa6358050c8edb99c4bf7fa8b780187afab97245b515abe1ae96f8` | [frames](../../bin-int/phase38-rolling/final-protocol/lattice-60-awake-0-frames.csv) / [per-tick hashes](../../bin-int/phase38-rolling/final-protocol/lattice-60-awake-0-state-fingerprints.csv) |
| 120 Hz disabled | `33d4acb6e00245235206032824647c3444c78192dae0c8212f32d917f0061c07` | [frames](../../bin-int/phase38-rolling/final-protocol/lattice-120-awake-0-frames.csv) / [per-tick hashes](../../bin-int/phase38-rolling/final-protocol/lattice-120-awake-0-state-fingerprints.csv) |

Detailed commands, all gate decisions, cohort identities, component peaks, timing medians, work counts and additional SHA-256 values are in [lattice-results.json](../../bin-int/phase38-rolling/final-protocol/lattice-results.json), [derived-results.json](../../bin-int/phase38-rolling/final-protocol/derived-results.json) and [gate-result.json](../../bin-int/phase38-rolling/final-protocol/gate-result.json). Frozen source/executable/harness hashes and the approved world SHA-256 are in `source-hashes.json`, `pre-protocol-source-hashes.json`, `frozen-experiment-hashes.json` and `lattice-protocol-approved.json`. All artifacts stay under ignored `bin-int/phase38-rolling/final-protocol/`.

The zero-rolling Phase 37 reference comparison is preserved and rechecked for the exact lattice at **both 60 and 120 Hz over 25 seconds**, with common spin length 0.05. Complete body CSV SHA-256 and every common non-timing frame field match the archived approval binary exactly; rolling increments remain zero. The 60 Hz archived reference is reused, and 120 Hz has a fresh archived-binary run. Exact commands, hashes and compared fields are in [reference-results.json](../../bin-int/phase38-rolling/final-protocol/reference-results.json). This is a zero-feature compatibility comparison, not a claim that the Phase 37 lattice settled.


## Benchmark Results

Measured protocol: serial children after correctness validation, normal priority, affinity mask 4 (logical CPU 2), Release x64 /Ox /Oi /fp:precise /MTd with profiling. Original collision/separated order: baseline / zero / enabled / enabled / zero / baseline. Loaded order: zero / enabled / enabled / zero. Two warmups and five measured samples per body count per process. Values below are medians of two process medians (10 measured samples per mode/size); rolling subset values are medians of process means.

Original collision: a fresh world, one 1/120-second step per sample, no initial motion or supporting impulse, so enabled rows apply zero rolling torque. Separated: four warmup steps and eight measured steps per sample, zero candidates/contacts. Loaded rolling workload uses the same geometry and opposing unit linear velocities plus angular velocities (+/-4,0,+/-4) on dynamic bodies; spin length 0.05 is common to zero/enabled modes, with rolling length 0/0.05. The archived Phase 37 CLI lacks the new loaded-rolling option, so this loaded before/after comparison uses the current binary's zero and enabled paths.

| Workload | Bodies | Phase 37 ms | Phase 38 zero ms | Rolling enabled ms | Rolling subset ms | Applied rolling increments |
|---|---:|---:|---:|---:|---:|---:|
| collision | 100 | 4.512900 | 4.396450 | 4.380050 | 0.004120 | 0 |
| collision | 1000 | 45.152600 | 44.466150 | 44.119050 | 0.039710 | 0 |
| collision | 10000 | 477.680300 | 472.894950 | 470.755350 | 0.397520 | 0 |
| separated | 100 | 0.022950 | 0.023563 | 0.023619 | 0.000000 | 0 |
| separated | 1000 | 0.236594 | 0.242475 | 0.243200 | 0.000000 | 0 |
| separated | 10000 | 2.730344 | 2.821038 | 2.725213 | 0.000000 | 0 |
| loaded | 100 | Not available | 4.499850 | 4.697200 | 0.207660 | 38 |
| loaded | 1000 | Not available | 44.885050 | 47.510100 | 2.033990 | 375 |
| loaded | 10000 | Not available | 478.988650 | 502.921900 | 20.302980 | 3750 |

Measured loaded 10,000-body step cost increases from 478.98865 to 502.92190 ms; difference 5.00% (Derived). Direct rolling-row time is 20.30298 ms for 3,750 applied increments; solver mean is 135.71353 / 156.13091 ms. This is an added physical capability with measurable cost, not an optimization. The bounded anisotropic disk solve uses 48 scalar bisections when its unconstrained result exceeds the load limit.

All original collision/separated common non-timing counters and physical fingerprints match Phase 37 in both zero and unloaded-enabled modes. Collision counts are 38 / 375 / 3,750 candidates, contacts and constraints; separated counts are zero. Loaded fingerprints intentionally change and match their respective repeated mode exactly. No samples were discarded.

Measured zero path versus baseline at separated 10,000 bodies is 2.821038 / 2.730344 ms (+3.32%, Derived); the original collision zero path is lower in this batch. Body size stays **680 -> 680 bytes**. Timing differences are reported without claiming their cause; isolated allocator/cache/layout attribution is Not available. Per-constraint rolling state adds two doubles and one float, plus layout padding as required; no allocation counter is available.

All **26** control/benchmark processes pass finite-state, deterministic-reference and applicable stack energy/motion gates. Detailed process ranges, counters and commands: measurements.json, summary.json, summary.txt and protocol.json. The final experiment is separately recorded under final-protocol/; the control benchmark parameters and results above remain unchanged.

## Behavior Changes

Rolling is opt-in; default remains zero. No scene/editor serialization or application material configuration changes.

## Known Limitations

No cross-step rolling warm start or positive-TOI rolling response. Explicit resistance length has no inferred contact-patch geometry. Existing angular impulse speed limiting remains.

## Out-of-Scope Findings

- The unchanged user-owned .gitignore has a trailing blank line at line 33; whole-tree diff checking reports it.
- PhysicsBody.cpp ApplyImpulseAngular retains its existing 30 rad/s speed clamp; tests of equal/opposite momentum stay below that operating limit.
- Plan and historical review status prose predates approved tags; it is not rewritten.

## git diff --check

Whole-tree exit 2; literal stdout:

~~~text
.gitignore:33: new blank line at EOF.
~~~

The user-owned .gitignore is byte-identical to phase entry. Pre-existing scene/plan line-ending warnings remain on stderr. The scoped check over the seven phase paths passes (exit 0); the new review is also checked separately. Final commands/output and ownership/freeze checks are retained in bin-int/phase38-rolling/final-protocol/final-checks.json. The earlier checkpoint remains preserved.

## git status --short

Literal final status, including untouched user work:

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
?? docs/physics/PHASE_38_REVIEW.md
?? docs/rendering/
~~~

Index is empty; HEAD remains 4f480a1. No phase commit, tag, push or later-phase work was performed.

## Human Decision

PENDING
