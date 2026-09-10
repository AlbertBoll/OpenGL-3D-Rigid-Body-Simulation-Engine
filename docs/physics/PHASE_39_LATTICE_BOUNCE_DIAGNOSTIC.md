# Phase 39: separate lattice bounce diagnostic

## Status and disposition

Diagnostic requested before Phase 39 approval. **Diagnostic complete. Phase 38 and Phase 39 are equivalent in all eight controlled setups; this observation is not a Phase 39 blocker.** Phase 39 remains **AWAITING HUMAN REVIEW**. This document is separate from the fixed-storage acceptance evidence in [PHASE_39_REVIEW.md](PHASE_39_REVIEW.md).

No production physics, material, solver-pass default, stabilization, sleeping threshold, or lattice parameter was changed. No commit, tag, push, approval, or Phase 40 work was performed.

## Controlled setup and identity

Baseline: `physics-phase-38-approved`, commit `fb63350a46fc5ceef9a446cb5ac8c712a66929ff`. Candidate: the existing, unapproved Phase 39 working tree. The diagnostic uses the **same frozen exported-lattice protocol as Phase 39 acceptance**, not a newly tuned scene.

- Input: `bin-int/phase34-sleep-primitives/worlds/lattice.txt`; SHA-256 `79eb5a76b06726b77333fff93ddb47999671df025ef1c75322494b8ac62116c6`.
- 185 bodies: five static container surfaces and 180 dynamic, unit-mass, radius-1 spheres. Fixed creation order, zero initial linear/angular velocities, gravity `(0,-12,0)`.
- Exported elasticity/friction are `0.5` per body; the existing pair products are `0.25`. Spin and rolling resistance lengths both remain `0.05`, exactly as in the acceptance replay.
- Fixed 60 Hz and 120 Hz, 15 simulated seconds per run. Global solver passes: **1, 2, 4, 8**. The three local normal/sliding iterations and all other code remain unchanged.
- Sleeping enabled throughout, with unchanged thresholds `0.05` linear, `0.02` angular and `0.5 s` dwell. No material ablations or sleeping-policy changes were made.
- MSVC x64 nominal optimized Release, including the existing **`/MTd` override**. This is not a true Release-CRT allocator benchmark. Diagnostic timing is not a performance measurement.

All top-layer spheres are export indices 5–40, initially at Y=18. Full trajectories cover all 185 bodies. Detailed contact tracing selects:

| Export index | Stable slot / generation | Initial position |
|---:|---|---|
| 10 | 11 / 11 | (8, 18, -2) |
| 12 | 13 / 13 | (6, 18, 6) |
| 22 | 23 / 23 | (4, 18, -2) |
| 24 | 25 / 25 | (2, 18, 6) |

Support identities use the same mapping: index 123 is slot/generation 124/124, index 154 is 155/155, and index 169 is 170/170. These identities are stable within each single-world replay. Contact rows identify both bodies and both world anchors, distinguishing multiple witnesses for the same pair.

The camera, rendered frame interpolation and the user's particular observed sphere were not supplied. The selected numerical trajectories reproduce upward hops in the frozen exported scene; this does not establish camera-level correspondence to a specific visible sphere or exact wall-clock timing in the app. The protocol's resistance lengths are retained even though live scene settings may differ.

## Observation method and validity

Instrumentation exists only in ignored copies under `bin-int/phase39-bounce/p38/` and `p39/`. All physics translation units are compiled in each copy, including the baseline contact/manifold layouts. Observers surround warm starting, normal/sliding response, manifold normal blocks, spin, rolling, TOI normal/friction response, and post-integration position correction. They record body states before/after each operation and after every complete global pass.

The raw contact CSV includes Y and full position, all linear/angular velocity components for both bodies, body identities, world anchors, A-to-B normal, signed penetration, contact-relative normal speed, accumulated/incremental normal and sliding impulses, world sliding impulse, accumulated/incremental spin and rolling impulses, and active restitution coefficient/term. Position displacement is the difference of its recorded before/after positions. Positive depth denotes penetration; negative relative normal speed denotes closing. Impulse vectors act on B and oppositely on A. Spin is along the normal; rolling coordinates use the solver's deterministic least-aligned-axis tangent basis. Resting solver rows have no restitution term; TOI rows record the actual branch-selected coefficient.

Warm-start impulses and subsequent signed corrections are retained separately: counting only positive iterations would overstate the applied load. Per-tick summaries sum actual contributions. For unit-mass spheres, summed signed normal-Y and sliding-Y impulses give their respective contributions to Vy. Spin/rolling change angular velocity, and position correction changes position. The summaries separately account for gravity and any final sleeping transition.

Every detected upward-velocity episode of a selected sphere has **five ticks before and five ticks after the entire positive-Vy interval** extracted, including the first impact, later collapse hops and floor rebounds. A separate position-only window is also extracted. Full unfiltered raw traces remain available.

## Pass sensitivity: align events, not clock windows

A reversal is a transition from Vy <= 0 to Vy > 0.02 units/s. Its rise is the maximum Y during the following continuous positive-Vy interval minus Y on the tick immediately before reversal. These are diagnostic detection rules, not new engine thresholds. Full trajectories and stage traces also preserve smaller/position-only motion excluded by this detector.

The first two collective upward episodes of **the same sphere 10**, aligned by event order:

| Hz | Passes | First rise | Second onset (s) | Second rise | Second peak Vy |
|---:|---:|---:|---:|---:|---:|
| 60 | 1 | 3.718243 | 2.916667 | 0.645962 | 3.330054 |
| 60 | 2 | 2.325997 | 2.533333 | 0.313636 | 2.267505 |
| 60 | 4 | 1.199289 | 2.150000 | 0.093018 | 1.156097 |
| 60 | 8 | 0.472428 | 1.766667 | 0.015615 | 0.395302 |
| 120 | 1 | 5.234237 | 3.125000 | 0.990228 | 4.574810 |
| 120 | 2 | 3.353152 | 2.733333 | 0.400732 | 2.918479 |
| 120 | 4 | 1.738604 | 2.300000 | 0.110449 | 1.505210 |
| 120 | 8 | 0.754628 | 1.908333 | 0.019896 | 0.601238 |

**Measured:** rise decreases materially and monotonically at both rates. The second rise decreases approximately 97.6% at 60 Hz and 98.0% at 120 Hz from 1 to 8 passes. Eight passes move the second pulse earlier than 2 seconds; it would be misleading to call it absent based on a 2–4.5-second scan.

There is also direct evidence within an identical tick/contact graph: at 60 Hz, eight-pass tick 105, the maximum remaining closing speed over contacts incident to the four traced top spheres after successive passes is **1.323266, 0.586950, 0.320168, 0.216788, 0.170932, 0.145922, 0.128992, 0.115639** units/s. Additional passes reduce the unresolved contact velocity but do not eliminate it in this example.

One-pass tick 174 illustrates delayed propagation: contact 10–46 leaves its local normal solve at approximately zero relative normal speed, but after the entire graph traversal it is closing again at **0.214188** units/s. At tick 175 its cached normal impulse is **1.351740**, followed by a **+0.127809** incremental correction. Supporting sphere 46 is moving upward at **0.764257** immediately before that local solve; both reach approximately **0.636448** afterward. The load is transmitted through the network and is not resolved simultaneously by one traversal.

**Classification: contact-network convergence limitation (cause 2), with actual multi-body transfer (cause 1).** These are overlapping observations: impulses obey equal/opposite momentum transfer, while their network-level result remains pass-sensitive. Momentum conservation alone does not establish a converged or physically correct multi-contact trajectory.

## Later micro-hops and their immediate causes

### Sphere 10, 60 Hz, one pass: 0.029706-unit rise

The sphere rolls onto supports 123/154, reverses at tick 596 (9.933333 s), peaks at Vy=0.357370, and reaches its apex at tick 604. Position correction is **zero throughout ticks 591–609**. This is a true velocity hop, not visible depenetration.

The following tick totals include warm start plus all normal/friction corrections and any TOI response. Impulses are signed upward contributions for sphere 10; gravity contributes approximately -0.2 Vy per tick. The complete extracted CSV includes all angular components and individual contacts.

| Tick | Y | Vy | Normal impulse Y | Sliding impulse Y | Correction Y |
|---:|---:|---:|---:|---:|---:|
| 592 | 3.116316 | -1.086663 | 0.110290 | 0.018406 | 0 |
| 593 | 3.097147 | -1.150153 | 0.116381 | 0.020129 | 0 |
| 594 | 3.081509 | -0.134673 | 1.103473 | 0.112007 | 0 |
| 595 | 3.081246 | -0.015781 | 0.268741 | 0.050151 | 0 |
| 596 | 3.084586 | 0.200408 | 0.351323 | 0.064866 | 0 |
| 597 | 3.089727 | 0.308446 | 0.260390 | 0.047649 | 0 |
| 598 | 3.095683 | 0.357370 | 0.212034 | 0.036891 | 0 |
| 600 | 3.105478 | 0.257101 | 0.109840 | 0.016658 | 0 |
| 604 | 3.110952 | 0.007837 | 0.139526 | 0.019894 | 0 |
| 605 | 3.110424 | -0.031689 | 0.142110 | 0.018363 | 0 |
| 606 | 3.109157 | -0.076022 | 0.138712 | 0.016955 | 0 |
| 608 | 3.103936 | -0.184390 | 0.125318 | 0.019130 | 0 |

At tick 596, supporting sphere 123 enters warm start with velocity **(1.114913, 0.649348, -0.917519)**. Sphere 10 enters with **(0.619872, -0.215781, -0.052701)** after gravity. Upward-moving support is present before the selected pair receives its warm impulse; it is not an upward velocity inferred only from the top sphere.

For the loaded 123-to-10 witness at this tick:

- Normal `(0.078302264, 0.803894877, -0.589594603)`; penetration **0.006985989**, below the position-correction slop.
- Normal accumulated impulse **0.437025964**; increment **+0.437025964** on this witness. The other witness removes **0.335458428** of its warm-started normal load; those changes are not double-counted.
- Sliding accumulated/incremental coordinates **(0.079683043, 0.074750207)**; world impulse on sphere 10 **(-0.005054234, +0.064865947, +0.087771565)**.
- Spin accumulated/incremental impulse **+0.021851299**. Rolling accumulated/incremental coordinates **(0.013128823, 0.017467491)**, norm approximately **0.021851298**. Both are at their existing load-scaled limits.
- Sphere 10 angular velocity is **(2.062434, -0.075424, 1.498178)** at the start of tick 596 and **(1.751168, -0.038431, 1.414624)** at its end.
- The normal block reduces this witness's closing speed from **-0.874051929** to approximately zero. Its later sliding solve leaves a small residual around **-0.002879918**. Neither spin nor rolling changes this witness's measured normal speed at this step; both have exactly zero direct linear-velocity change.

**The preceding history matters:** at tick 594, two ticks before the positive-Vy reversal, a TOI contact with sphere 123 closes at **2.004844** units/s. Restitution is active at **e=0.25**, with target separating normal speed **0.501211** and normal impulse **1.253028**. Sphere 10 still ends that tick moving downward. Ticks 595–604 use inelastic resting response and moving supports. Thus this hop has **mixed earlier restitution and subsequent multi-body/contact-network history**, not a newly activated low-speed restitution term at tick 596.

### Sphere 10, same run: smaller inelastic hop

The next hop starts at tick 616 (10.266667 s), rises **0.002894402**, peaks at Vy=**0.080998302** on tick 617, and returns to downward Vy on tick 619. Its full ticks 611–623 window has **zero active restitution term and zero position correction**. The TOI contact at tick 614 is inelastic.

At tick 616, supports 123/154 enter their respective warm operations with upward Vy **0.274610 / 0.128786**. The net upward normal/sliding impulses on sphere 10 are **0.249086907 / 0.047225564**. With gravity, Vy changes from **-0.066795439** to **+0.029517006**. Its last oblique 10–154 solve has normal approximately `(0.261062,-0.796784,-0.544961)` and eliminates closing normal speed **0.136030** while turning the sphere's Vy from **-0.036470** to **+0.029517**. Normal projection and sliding friction can redirect lateral/rolling motion upward even with zero restitution.

**Classification: causes 1 and 6 (moving-support transfer and oblique normal/sliding redirection), in a pass-sensitive contact network (cause 2).** The trace does not prove that the supporting bodies' incoming velocities are themselves a fully converged solution.

### Independent 120 Hz example and near-threshold rebound

Sphere 12, one pass, reverses at tick 1361 (11.341667 s), rises **0.070364952**, peaks at Vy=**0.660375059** at tick 1363, and becomes downward-moving at tick 1392. At onset, normal/sliding contributions are **+0.317299690 / +0.046363752**; gravity is approximately -0.1. Support 169 enters warm start with Vy **+0.905023**. Correction remains zero through this upward episode. As with the first 60 Hz example, the onset's resting solve has zero restitution, but tick 1359 had an earlier TOI restitution term **0.434187**. This is another mixed history, not proof of sub-threshold restitution activation.

A distinct small **floor** rebound is directly attributable to existing restitution: sphere 22 at 60 Hz/two passes, tick 877. End-of-previous-tick Vy is **-0.854014**, but gravity makes the actual contact closing speed **1.054019**. The existing strict `>1.0` branch activates **e=0.25**, normal impulse **1.317520**, and separating term **0.263505**. The observed rise is **0.001431230**. **Cause 4 applies here**, with evidenced above-threshold activation despite the small visible rebound. No below-threshold restitution activation was found in the inspected hop windows.

## Position-only upward movement

Sphere 22 at 60 Hz/one pass, tick 322 (5.366667 s), moves from Y **9.396436691** to **9.396541595** while ending with Vy **-0.000009339**. PostSolve contributes **+0.000104904** Y and changes no velocity. The 22–58 support contact has depth approximately **0.020839604**, just beyond the unchanged 0.02 slop. At this scale, the tiny downward integration increment rounds away in the stored float position.

**Cause 3: positional correction.** This numerical upward drift is much smaller than the measured 0.003–0.070-unit collapse hops and must not be presented as their cause. Before/during/after ticks 317–327 are included in the extracts.

## Spin/rolling contribution and remaining limits

The recorded spin/rolling operations change angular velocity and therefore can affect subsequent tangential response. Across all eight observed setups their direct linear-velocity change is exactly zero. Position correction likewise changes neither linear nor angular velocity. In the 60 Hz main-hop window, summed absolute per-tick changes in relative normal speed attributable directly to spin/rolling are no greater than approximately **9.69e-8 / 1.89e-7** units/s respectively.

There is **no evidence that spin/rolling directly launches these spheres or is the dominant normal-response cause**. This is not a proof of no indirect influence: sliding depends on angular motion, and resistance parameters were intentionally not varied. Cause 5 is an observed angular/tangential coupling, not an established primary fault.

## Later-event pass comparison and qualifications

For all 36 original top spheres over the same 4.5–15 s interval at 60 Hz, detected reversals whose previous-tick `|Vy|<1` have:

| Passes | Count | Median rise | Largest rise |
|---:|---:|---:|---:|
| 1 | 21 | 0.017339 | 0.099280 |
| 2 | 39 | 0.006963 | 0.249804 |
| 4 | 13 | 0.004084 | 0.094826 |
| 8 | 8 | 0.001923 | 0.007477 |

These are **low-Vy reversal statistics**, not certified low contact-speed/restitution-off impacts. The median decreases consistently, but count and maximum do not. Different pass counts change collapse paths and neighbors, so these are not matched copies of each individual later collision. Sphere 10's approximately 0.030-unit episode does not recur with the same contact history in the higher-pass runs. Higher-pass trajectories still contain ordinary, faster floor rebounds.

At 120 Hz, the 2/4/8-pass runs qualify all 180 spheres for the unchanged sleeping policy at approximately **3.9083 / 3.1000 / 2.5333 s** and do not undergo the one-pass late collapse in this 15-second observation. Their absent late hops cannot be interpreted as matched-event amplitudes: sleeping is an indirect consequence of the different trajectories. This diagnostic does not establish long-horizon stability without sleeping or justify changing defaults.

## Classification and follow-up

| Requested mechanism | Evidence-based finding |
|---|---|
| 1. Multi-body momentum transfer | Present: supporting spheres carry upward velocity into the selected contacts; measured equal/opposite impulses transfer it. Incoming network motion may itself include iterative error/restitution. |
| 2. Insufficient convergence / delayed propagation | Strong evidence: monotonic aligned-pulse reduction at both rates, residual closing velocity after a global traversal, and reduction within the same tick over additional passes. |
| 3. Position correction | Present as much smaller, separately identified upward drift; zero correction in the principal collapse-hop intervals. |
| 4. Restitution / normal rebound | Present in earlier impacts and a near-threshold floor rebound. Inspected inelastic micro-hop window has no restitution activation. |
| 5. Spin/rolling coupling | Angular response is measured; no direct linear launch or material direct normal-speed effect in the selected window. Indirect influence is not isolated. |
| 6. Another mechanism | Oblique normal projection and sliding friction redirect motion vertically without requiring restitution. |

Record **follow-up contact/solver-convergence finding**, not a Phase 39 fixed-storage regression. Potential future investigation should preserve momentum/energy accounting, distinguish persistent and TOI contact ordering, evaluate convergence before choosing a pass policy, and retain separate measures of physical and corrective motion. This diagnostic authorizes no corrective implementation or tuning.

## Artifacts and reproduction

All raw data and build helpers are in ignored `bin-int/phase39-bounce/`. Paths below are relative to the repository root:

- `run-coarse.py`, `coarse-runs.json`: archived, uninstrumented Phase 38/39 runs; `coarse/` contains all-body trajectories and world telemetry.
- `BounceTrace.h`, `prepare.py`, `p38/Replay.vcxproj`, `p39/Replay.vcxproj`: isolated observer source/builds. `build-p38.log` and `build-p39.log` record build results.
- `run-trace.py`, `trace-runs.json`: observer noninterference and phase comparisons; `trace/` holds complete contact, stage and all-body CSVs.
- `analyze-coarse.py`, `aligned-pulses.json`, `coarse-summary.json`, `equivalence.json`: event definitions, pass measurements and uninstrumented physical comparisons.
- `extract.py`, `extraction-summary.json`: impulse/position accounting and before/during/after extracts. In `extracts/`, each `{60,120}-{1,2,4,8}-contact-windows.csv` retains individual impulses, normals, penetration and both-body states; matching `-tick-windows.csv` has tick totals. `-ticks.csv` and `-residuals.csv` retain all selected ticks and per-pass convergence.
- `entry-hashes.json`, `initial-status.txt`: ownership boundary; final verification is recorded separately below.

Representative commands:

```powershell
python bin-int/phase39-bounce/run-coarse.py
python bin-int/phase39-bounce/prepare.py
& 'C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe' bin-int/phase39-bounce/p38/Replay.vcxproj /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /m:2 /v:minimal
& 'C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe' bin-int/phase39-bounce/p39/Replay.vcxproj /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /m:2 /v:minimal
python bin-int/phase39-bounce/run-trace.py
python bin-int/phase39-bounce/analyze-coarse.py
python bin-int/phase39-bounce/extract.py
```

`run-trace.py` stops if either an observer changes its archived trajectory or the two versions diverge. The original archived binaries and input are prerequisites retained from Phase 39 validation. No source swapping in the production tree is needed.

Source evidence at this candidate: `PhysicsSystem.cpp:689,708,814` defines warm start, global passes and post-integration correction; `PhysicsSystem.cpp:1151,1180` defines the actual restitution threshold/branch; `ConstraintPenetration.cpp:253,267,309,348,416` contains warm starting, normal/sliding, spin, rolling and translation-only correction; `Manifold.cpp:410,424` covers block normal application and ordered friction solves. All paths are under `GEngine/include/GEngine/Physics/`, with `ConstraintPenetration.cpp` under `Constraints/`.

## Final validation and repository state

**PASS:** 16 uninstrumented runs and 16 instrumented runs completed successfully. For each of the eight rate/pass combinations, Phase 38 and Phase 39 all-body trajectories are byte-identical, as are every non-timing world-telemetry field and the instrumented contact/stage CSVs. Each instrumented trajectory is also byte-identical to its own archived executable. This covers **10,800 ticks and 1,998,000 body-state samples per phase**, plus all intermediate recorded contact operations. No tolerance-based trajectory equivalence was substituted for exact comparison.

The per-tick Vy accounting residual over all selected bodies/runs is at most **5.55e-17 units/s** (double summation of stored float changes). Position-correction accounting residual is exactly zero. All measured spin/rolling direct linear-velocity changes and all position-correction velocity changes are exactly zero. Excerpts contain five ticks before and after every detected selected-body upward interval at every pass count, not only the examples printed above.

Both isolated nominal Release builds pass. No Debug diagnostic run or new production test was needed for this observation-only task. The existing Phase 39 Debug/Release 18,898-check acceptance results and controlled 120-second stability/benchmark evidence were preserved without alteration; this report adds separate 15-second diagnostic evidence. No new performance claim is made.

Changed non-ignored files in this diagnostic only:

- `docs/physics/PHASE_39_LATTICE_BOUNCE_DIAGNOSTIC.md`: this separate report.
- `docs/physics/PHASE_39_REVIEW.md`: documentation inventory and follow-up reference; original implementation, tests, build results, stability results and benchmark sections are byte-identical to their entry versions.

Every other entry-owned file, including all Phase 39 production/test files and all unrelated user changes, retains its entry SHA-256. The index remains unchanged, HEAD remains `fb63350`, and no Phase 39 approved tag exists. The original review is retained in `bin-int/phase39-bounce/phase39-review-before-diagnostic.md` for the narrow documentation comparison.

Whole-tree `git diff --check`: exit **2**, the same pre-existing user-owned whitespace issue:

```text
.gitignore:33: new blank line at EOF.
```

Its pre-existing line-ending warnings are:

```text
warning: in the working copy of 'GEngine/src/Scene/_Scene.cpp', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md', LF will be replaced by CRLF the next time Git touches it
```

The two diagnostic documents are checked separately with `git diff --no-index --check -- NUL <path>`; final command results are in `bin-int/phase39-bounce/final-verification.json`.

Literal final `git status --short`:

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h
 M GEngine/include/GEngine/Physics/Manifold.cpp
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
?? GEngine/include/GEngine/Physics/Constraints/SolverMath.h
?? docs/audit/
?? docs/physics/PHASE_31_REVIEW.md
?? docs/physics/PHASE_39_LATTICE_BOUNCE_DIAGNOSTIC.md
?? docs/physics/PHASE_39_REVIEW.md
?? docs/rendering/
```

**PHASE 39 STATUS: AWAITING HUMAN REVIEW. Human decision remains PENDING.** This diagnostic does not approve the phase and does not require FIX PENDING: no Phase 38-to-39 divergence was found.
