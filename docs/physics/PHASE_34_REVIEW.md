# Phase 34 Review

## Status

PHASE 34 STATUS: AWAITING HUMAN REVIEW

The requested three-way attribution investigation is complete: 36 isolated benchmark processes and 72 exact-world replays, with all prior correctness coverage preserved. Phase 33 plus the matched 32-byte passive tail reproduces the Phase 34 timed binaries byte for byte except build timestamps. This identifies the body-representation change as the primary software mechanism, but timing drift and non-additive pair estimates prevent a reliable numerical allocation of the original approximately 5.3% slowdown. The measurements, separately reported unexplained remainder, limits and recommendation appear in the controlled-attribution section below. No production/test revision, runtime sleeping, approval, commit, tag, push or next-phase work occurred.

## Objective

Sleeping State Primitives: add body-level settings, an inactivity timer, a passive sleeping flag and explicit wake/reset operations. This is the foundation for contact islands (Phase 35) and runtime sleep/wake integration (Phase 36).

## Baseline Commit

`10ca1e239694251baf87bde5a081223a3ccad737` - `physics: phase 33 fixed physics timestep scheduling`.

Branch: `physics/refactor`. The index was empty at entry. Preflight passed; HEAD and the previous approved tag agree.

## Previous Approved Tag

`physics-phase-33-approved`. Earlier approved tags and the rejected Phase 31 review remain unchanged.

## Audit Findings Addressed

Foundation only for `PHYS-PERF-005` (no sleeping). Automatic sleeping and its performance benefit remain open until island-aware integration. No audit history, solver/contact equations, damping, friction, scheduling, rendering or application scene edits belong to this phase.

## Allowed Scope

Two production files, one existing dedicated test file, this review: four files total. `PhysicsSystem.cpp` needs no change for passive body primitives. The plan assigns contact islands to Phase 35 and runtime sleep/wake behavior to Phase 36.

## Files Changed

- GEngine/include/GEngine/Physics/PhysicsBody.h
- GEngine/include/GEngine/Physics/PhysicsBody.cpp
- PhysicsTests/src/main.cpp
- docs/physics/PHASE_34_REVIEW.md

## Implementation Summary

Each body starts awake with zero inactivity. Validated per-body settings default to 0.05 world units/s linear speed, 0.02 rad/s angular speed, and a 0.5 s dwell. The speed defaults match the existing box stability motion gates; the half-second dwell is a provisional configurable primitive default, not a measured settling policy or a production activation. None of these values affect simulation yet. Island-level sleep policy still requires separate validation in Phase 36.

`UpdateSleepTimer` is explicitly sampled by a caller once after a completed positive physics step. Only finite Dynamic bodies with positive effective inverse mass and both vector speeds at or below their limits accumulate inactivity. Finite double magnitude arithmetic avoids float-square overflow and underflow. Invalid/nonpositive elapsed time is ignored. Active or invalid bodies lose qualification on a valid sample. Time saturates at the configured duration without overflowing for huge finite inputs; ordinary floating accumulation can conservatively require one extra boundary sample at 60/120 Hz.

`CanSleep` rechecks current eligibility. `TrySleep` requires the complete dwell, rechecks current state, and records the explicit decision. `WakeUp` clears the flag and timer. Neither transition edits physical state. Settings require finite nonnegative speed limits and finite positive duration; invalid input is transactional, changed valid input wakes, identical input preserves the timer.

The cold fields are appended after existing derived data. Runtime paths do not read or write these primitives. Gravity, impulses, integration, collision queries, constraints, scheduling and profiler sleeping-body counts retain approved behavior. All dependent clients must rebuild because body size changes.

## Acceptance Defined Before Measurements

Pass focused configuration, timer, threshold, finite/overflow, transition, physical-state-preservation and lifetime regressions, plus full Debug and nominal Release physics tests. Compare exact exported box and lattice worlds for 25 simulated seconds at fixed 1/60 on Phase 33 and Phase 34: every emitted physical state and non-timing workload counter must match exactly.

Existing box gates remain: in 5-18 s and 15-20 s windows, linear <=0.05 units/s, angular <=0.02 rad/s, live-anchor penetration <=0.035 units, body excursion <=0.05 units, average dynamic Y >=4.45, finite state, peak energy <=864 * 1.005. The lattice is a preservation test, not a requirement to settle in this primitive phase.

Body layout can affect allocation/cache/code generation even with passive fields. Run the approved isolated Release benchmark in controlled A/B order with identical source path, compiler/configuration, profiling, workloads, warmup, samples and CPU affinity. Report all medians and counters. Investigate any consistent slowdown greater than 10%; claim no sleep performance benefit.

Validation uses Class B numerical/guard coverage, Class C deterministic stability preservation and Class D before/after measurements. The plan gives Phase 34 no separate validation class or additional exit gate.

## Tests Added / Modified

Added 117 focused checks under `--sleep-primitives`, also included in the full suite (17,729 -> 17,846 checks).

Coverage includes default awake state; complete dwell and explicit transition; signed threshold equality and one-ULP excess on all axes; vector magnitude versus component tests; interruption and fresh dwell; 60/120 Hz cadence; invalid dt and policies; same/changed policy behavior; huge finite saturation; float-square overflow and underflow; static/kinematic/invalid mass and type guards; non-finite physical state; idempotent sleep/wake; preservation of pose, velocities, materials, mass, shape and derived data; identical full-world gravity/contact trajectories with a manually set passive flag; no automatic sampling/sleeping; and fresh state after identity-slot reuse.

State and counter comparisons are exact. Binary-exact timer boundaries use equality. The 60/120 Hz accumulation test allows at most one conservative extra sample at the 0.5 s boundary. No timing tolerance enters the solver or production scheduling.

## Validation Commands

Commands were run from `C:\dev\GEngine-physics` using the actual repository MSBuild projects:

```powershell
$phaseMsbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
& $phaseMsbuild PhysicsTests/PhysicsTests.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:quiet
& bin/Debug/PhysicsTests/PhysicsTests.exe --sleep-primitives
& bin/Debug/PhysicsTests/PhysicsTests.exe
& $phaseMsbuild PhysicsTests/PhysicsTests.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:quiet
& bin/Release/PhysicsTests/PhysicsTests.exe --sleep-primitives
& bin/Release/PhysicsTests/PhysicsTests.exe
& $phaseMsbuild RigidBodySimulation/RigidBodySimulation.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:quiet
& $phaseMsbuild RigidBodySimulation/RigidBodySimulation.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:quiet
python bin-int/phase34-sleep-primitives/build-ab.py
python bin-int/phase34-sleep-primitives/measure.py
python bin-int/phase34-sleep-primitives/measure-lattice-balanced.py
python bin-int/phase34-sleep-primitives/audit-entry.py
python bin-int/phase34-sleep-primitives/check-additive.py
git diff --check
git status --short
```

The saved scripts and logs are under `bin-int/phase34-sleep-primitives/` (ignored local evidence, not phase commit files). The A/B helper builds `PhysicsBenchmark` and `CostReplay` with `/p:Configuration=Release /p:Platform=x64 /m /v:quiet`. Baseline sources come from the immutable Phase 33 HEAD; both revisions use `common/` as their source/build path. Source manifests prove only the two body files differ. The initial post-build source assertion needed Windows separator normalization; both builds had already succeeded and the corrected assertion passed without changing either binary.

Benchmark arguments: `--body-counts=50,100,200,500,1000,2000 --warmup=2 --samples=5`. Replay arguments: `CostReplay.exe <world.txt> fixed 0.016666666666666666 <output-prefix> 1`. Byte-identical world copies are retained in `worlds/boxes.txt` and `worlds/lattice.txt`. The scripts preserve outputs and require a fresh artifact directory for another complete build run.

Evidence includes `validation.json`, all `full-*.log` / `final-focused-*.log` / build logs, both source hash manifests, `build-results.json`, compiler command logs, `benchmark-runs.json`, `benchmark-summary.json`, `replay-results.json`, all replay frame/body CSVs, object text comparisons, entry hashes, and final ownership/Git checks.

## Debug Result

PASS. Focused sleep primitives: **117/117**. Full physics suite: **17,846/17,846**, including the approved scheduling, exact box timestep stability, contact convergence, friction, collision and lifetime regressions. Revision full test elapsed time: 547.109 s. PhysicsTests and the actual RigidBodySimulation application build succeeded. No new build error or test failure.

## Release Result

PASS. Focused sleep primitives: **117/117**. Full physics suite: **17,846/17,846**; revision elapsed time 73.500 s. PhysicsTests, both isolated A/B benchmark/replay binaries, and the actual RigidBodySimulation application build succeeded.

Nominal optimized Release uses MSBuild 17.14.51, v143/MSVC 14.44.35207, Full optimization (`/Ox /Oi`), `/fp:precise`, and the existing `/MTd` override (Debug static CRT). These are measured results for that configuration, not a true Release-CRT allocator/runtime claim. Existing conversion and duplicate-definition warnings remain unchanged.

## Stability Results

**Measured:** exact exported application worlds, 25 simulated seconds / 1,500 completed 1/60 steps per process, one solver pass. The 21-body box world contains 16 dynamic boxes; the 185-body lattice contains 180 dynamic spheres. Fixed creation order, shapes, materials, filters and initial state are identical. No renderer timing controls stability measurements.

Boxes: three runs per revision. Lattice: six runs per revision after completing the balanced timing series. All emitted body states match the approved Phase 33 body CSV byte for byte on every run; non-timing frame metrics/counters also match exactly. Repeated runs give identical physical metrics. Every state is finite. For headless replay, movement means consecutive completed physics ticks; application frame-to-frame movement is not newly measured.

Box stack:

| Measured metric (identical on both revisions) | 5-18 simulated s | 15-20 simulated s |
| --- | ---: | ---: |
| Peak linear speed (units/s) | 2.72497e-06 | 2.012405e-06 |
| Peak angular speed (rad/s) | 1.215863e-06 | 7.069046e-07 |
| Live-anchor penetration (units) | 0.0190289 | 0.0190289 |
| Per-body position excursion (units) | 1.899371e-06 | 7.816075e-07 |
| Maximum per-tick movement (units) | 3.529727e-08 | 2.837896e-08 |
| Minimum average dynamic-body Y | 4.464004 | 4.464004 |
| Final average dynamic-body Y | 4.464004 | 4.464004 |
| Final maximum linear speed | 3.26208e-07 | 3.018905e-07 |
| Final maximum angular speed | 5.635565e-08 | 8.931292e-08 |
| Bodies above either motion gate at window end | 0 | 0 |
| Manifold range | 16-16 | 16-16 |
| Contact range | 64-64 | 64-64 |
| Window peak mechanical energy | 857.0888 | 857.0888 |

The box gates pass in both windows. Derived initial mechanical energy is 864; the replay's maximum after the first completed tick is 863.719971878, with no gain above the initial state. The full Debug/Release exact-stack fixture additionally passes at 1/120: peak early-window linear 2.03366e-6, angular 6.88324e-7, penetration 0.0171272, excursion 1.17294e-6, average Y 4.46967, 16 manifolds / 64 contacts, and peak including initial energy 864.

Sphere lattice:

| Measured metric (identical on both revisions) | 5-18 simulated s | 15-20 simulated s |
| --- | ---: | ---: |
| Peak linear speed (units/s) | 11.25693 | 5.049135 |
| Peak angular speed (rad/s) | 5.953533 | 5.040651 |
| Live-anchor penetration (units) | 0.362613 | 0.1265976 |
| Per-body position excursion (units) | 61.99776 | 18.28192 |
| Maximum per-tick movement (units) | 0.1876153 | 0.08415391 |
| Minimum average dynamic-body Y | 1.488042 | 1.489819 |
| Final average dynamic-body Y | 1.491463 | 1.49213 |
| Final maximum linear speed | 4.119779 | 3.65001 |
| Final maximum angular speed | 4.284078 | 3.695078 |
| Bodies above either motion gate at window end | 180 | 180 |
| Manifold range | 177-296 | 179-191 |
| Contact range | 320-435 | 408-432 |
| Window peak mechanical energy | 9424.701 | 3848.752 |

Derived initial lattice mechanical energy is 30,240 (zero initial angular velocity, centered dynamic spheres). Maximum measured after a completed tick: 30236.400110500; the unchanged trajectory does not gain energy above initial. All 180 spheres still exceed the motion gates at 20 s. The penetration and continuing motion are preserved baseline behavior, not a successful sleeping/settling result. No threshold was tuned to freeze the lattice.

Body CSV SHA-256, shared by both revisions and every repeat:

- Boxes: `59924d46c36424e72c5d1d6232701805ab4ac965d6d26366b3423d2a08dc014c`
- Lattice: `ad4ae1d700e870d9c7413e54fd06f903645ec6f61a3905ebb23826babfcdcc08`

Exact export SHA-256:

- Boxes: `2b7efcbeb4a3ea3eba9c77da53baa1ed698f5aa285f1b604737088f72465f2b4`
- Lattice: `79eb5a76b06726b77333fff93ddb47999671df025ef1c75322494b8ac62116c6`

The six existing Scene schedule patterns each execute 1,200 ticks for 20 s of supplied time and match direct stepping exactly. Scheduling architecture and policy remain at Phase 33.

## Original Benchmark Results (Before Attribution Revision)

**Measured:** approved paired-overlapping-box benchmark. Three sequential ABBA blocks give six fresh-process runs per revision; each run uses two warmup worlds and five measured worlds at every body count. One 1/120 tick per sample, one solver iteration, profiling enabled, x64 nominal Release, normal priority, logical CPU 2 affinity inherited by children. Builds/tests completed before timing; no overlapping benchmark or replay processes. Values below are medians of the six per-process medians; ranges and every raw row are retained in `benchmark-summary.json` / `benchmark-runs.json`.

| Bodies | Phase 33 ms | Phase 34 ms | Change (derived) | Active bodies | Candidate pairs |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 50 | 2.11725 | 2.10625 | -0.52% | 25 | 19 |
| 100 | 4.28195 | 4.21640 | -1.53% | 50 | 38 |
| 200 | 8.47645 | 8.60155 | +1.48% | 100 | 75 |
| 500 | 20.76345 | 20.95210 | +0.91% | 250 | 188 |
| 1000 | 42.54170 | 42.65630 | +0.27% | 500 | 375 |
| 2000 | 87.38175 | 88.51860 | +1.30% | 1000 | 750 |

All numerical fingerprints and non-timing counters match in every run. Sleeping bodies remain zero; each candidate count also equals GJK calls, EPA calls, contacts, manifolds and solver constraints in this specific workload. Solver passes remain one. The isolated benchmark provides no evidence of a material regression, and no sleeping speedup is claimed.

**Measured headless replay cost:** per-process median of ticks 300-1,200 (5-20 simulated seconds; 901 samples), then median across processes. Three runs/revision for boxes; six/revision for lattice. The initial lattice series drifted, so it was extended to three complete ABBA blocks using the same binaries and metrics. All original slower and faster runs are included.

| Scene / revision | External tick ms (median; run range) | Profiled physics ms | Solver ms | Thread cycles, millions |
| --- | ---: | ---: | ---: | ---: |
| boxes / p33 | 2.9771; 2.9764-2.9896 | 2.9769 | 1.6316 | 9.8053 |
| boxes / p34 | 3.1361; 3.1339-3.1378 | 3.1360 | 1.7636 | 10.3283 |
| lattice / p33 | 17.5040; 16.3674-17.6869 | 17.5033 | 11.6911 | 57.5939 |
| lattice / p34 | 18.4374; 17.2022-18.5665 | 18.4367 | 12.5214 | 60.6871 |

Derived external-tick changes: boxes **+5.34%**, lattice **+5.33%**. Lattice ABBA-block changes: +2.17%, +5.15%, +5.42%. These cost differences are retained as overhead/risk; the isolated benchmark is not a universal neutrality claim. No consistent slowdown exceeded the pre-measurement 10% investigation threshold.

**Measured body layout:** 528 -> 560 bytes on this x64 build: +32 bytes/body, **derived +6.06%**, before allocator overhead. Existing physical/cache member declarations and order are unchanged. The new APIs have no runtime callers. COFF object `.text` sections for `ConstraintPenetration`, `Manifold` and `Broadphase` are byte-identical between builds (this comparison excludes relocated final link addresses). `PhysicsWorld` allocation/lifetime functions and `PhysicsSystem` prediction/copy/update functions have changed generated text. Existing full-body prediction copies at `PhysicsSystem.cpp:569-570`, `675-676`, and `804-805` now copy the larger body. These are source/code-generation observations, not a complete cost attribution. Allocation/cache footprint, copy cost and linked code layout have not been isolated from one another; the measured replay overhead must not be dismissed as noise.

**Not available:** new application FPS/render-frame measurements or a production sleep speedup. Both application configurations build, and the exact application worlds are replayed headlessly. Phase 33 overload/progress limits remain unchanged; this phase makes no new real-time performance claim.

## Controlled Three-Way Attribution Revision

The human requested this investigation while Phase 34 was FIX PENDING. This revision changes only this review document; the two production body files and all 117 added correctness checks remain byte-identical to revision entry. Runtime sleeping remains disabled.

### Attribution, unexplained remainder and recommendation

**Derived attribution:** B reproduces every executable and runtime-data change in the timed C programs, apart from build timestamps. The Phase 34-only methods contribute no executing instructions in these workloads. Thus the larger body representation (including its induced copy, allocation, stack and linked-code layout changes) is the software mechanism reproduced by the control. There is no distinct Phase 34 sleep-code or copy-code difference left between B and C.

**Measured limits (percentages derived):** the timing results do **not** cleanly satisfy “B and C are equally slower than A” across estimators. Across all twelve process medians, boxes change by **B +1.896%, C +1.111%** relative to A; lattice changes by **B -0.450%, C -1.235%**. In contrast, the three paired ABBA block-effect medians give boxes **B-A +1.099%, C-A +4.264%**, and lattice **B-A +0.312%, C-A +3.097%**. One A/B block reverses sign on each scene. The isolated benchmark also shows substantial block variation, including a -12.939% A/B median block effect at 1,000 bodies despite a much smaller pooled difference. These observations prevent a reliable percentage attribution of the original approximately 5.3% penalty; neither the more favorable pooled estimator nor the larger paired estimate is selectively treated as the truth.

**Derived residual, reported separately:** direct paired **C-B** external-tick effects are **-0.01910 ms / -0.612%** for boxes (block range -1.219% to -0.317%) and **-0.24910 ms / -1.417%** for lattice (range -1.487% to +2.223%). There is no consistent additional positive C penalty in these direct comparisons. Subtracting the separately measured B-A effect from C-A instead leaves **+0.09800 ms / 3.164 percentage points** for boxes and **+0.45690 ms / 2.784 percentage points** for lattice. These are arithmetic differences of separate block estimates, not isolated extra Phase 34 costs: they disagree in sign with direct C-B and use different baseline windows. Their non-additivity and the systematic small box B/C difference remain unexplained measurement/runtime-placement effects in this experiment. They are not erased or assigned to sleep logic. No exact fraction of the original 5.3% is claimed to have been explained.

The source and binary evidence therefore supports body representation as the primary software attribution, while the timing experiment leaves the magnitude and the copy-versus-cache/allocator/instruction-layout split unresolved. The original review's +5.34% / +5.33% figures remain historical measurements, not a fixed repeatable surcharge or a result disproved by the newer pooled medians. B/C binary identity also demonstrates why apparent timing differences alone cannot establish a Phase 34-specific behavior cost here.

**Recommendation:** retain the current Phase 34 implementation and correctness coverage for human review; make no sleep-architecture or prediction-path optimization in this phase. Treat the +32-byte body footprint and increased copy traffic as confirmed costs, and require explicit acceptance of the unresolved timing magnitude when deciding approval. This investigation is not evidence of performance neutrality or a quantified “all 5.3% is copying” explanation. If a firm percentage budget is required for approval, further measurement under a quieter, more stationary host condition and controlled runtime image/address placement is needed. Cache/allocator hardware attribution belongs to separately authorized investigation. Runtime sleeping remains disabled. Human decision remains PENDING.

### Controls and reproducibility

A is the immutable Phase 33 commit `10ca1e239694251baf87bde5a081223a3ccad737`. B is that exact source plus passive tail storage in `PhysicsBody.h`: a 16-byte dummy settings object (float, float, double), a double, a bool, and seven bytes of tail padding. Their default values also match C. There are no new behavioral methods or readers. C is the unchanged Phase 34 body header and implementation. Source hash manifests verify that A/B differ only in this header and A/C differ only in the two authorized body files. Existing physical/cache member offsets are identical on all three.

All variants were fully rebuilt through the same original source/build directory, `bin-int/phase34-sleep-primitives/common/`, with the same MSBuild projects. Compiler command logs for GEngine, PhysicsBenchmark and CostReplay are byte-identical across A/B/C. Settings remain nominal Release x64, v143/MSVC 14.44.35207, `/Ox /Oi`, default `/fp:precise`, profiling enabled, and the existing `/MTd` override. CPU: AMD Ryzen 9 5900HX, 8 cores / 16 logical processors. Timed child processes inherit affinity mask 4 (logical CPU 2) and normal priority. Builds and correctness tests finish before timing; benchmark/replay processes never overlap. Later lightweight evidence analysis uses logical CPU 4.

There are three literal ABBA blocks for each pair: `A B B A`, `A C C A`, and `B C C B`. Pair-block order rotates by round: AB/AC/BC, AC/BC/AB, BC/AB/AC. Every slot runs the same ordered workloads: isolated benchmark, boxes, lattice. This yields twelve fresh processes per variant per workload (36 benchmarks and 72 replays). No slow or fast run is excluded. Pairwise effect estimates compare the medians of the two processes on each side of a complete block, then report the median and range over its three blocks. Those ranges describe repeatability; they are not confidence intervals or a formal equivalence test. Affinity does not fix processor frequency, thermal state, OS scheduling, ASLR or physical page placement. These factors were not separately measured or controlled, and the block ranges retain their observed variability.

Benchmark arguments remain `--body-counts=50,100,200,500,1000,2000 --warmup=2 --samples=5`: two warmup worlds and five measured worlds per count, one profiled 1/120 tick per world, one solver iteration. Benchmark external values are medians of per-process sample medians; profiled columns are medians of the per-process mean columns emitted by the existing benchmark.

Both byte-identical world exports use `CostReplay.exe <world> fixed 0.016666666666666666 <prefix> 1`. Each process completes 1,500 ticks / 25 simulated seconds. As in the original review, ticks 300-1,200 inclusive form the measured 5-20 second window: 901 samples after the first 299 ticks. Replay values are medians of per-process tick medians. Every run checks all emitted body states against the approved Phase 33 CSV and every non-timing frame field against the saved Phase 33 reference; full serialized-state equality is exact, without a numeric tolerance.

Evidence and runnable scripts are saved under `bin-int/phase34-attribution/`: `build.py`, `layout.py`, `codegen.py`, `linked-code.py`, `measure.py`, `summarize.py`, `validate.py`, `audit-final.py`, `protocol.json`, `passive-storage.patch`, source manifests, compiler logs, class-layout reports, all binaries/maps/objects, raw benchmark logs, raw frame/body CSVs, timing/counter summaries, and ownership checks. They are ignored local evidence, not additional phase production or test files. A complete fresh run needs a fresh artifact destination; the common source/build path stays fixed. A preliminary grouped dummy was corrected before any timing because it copied padding differently; that untimed build is retained separately and is excluded from the fixed A/B/C series.

### Copy and layout evidence

**Measured compiler layout:** A is 528 bytes; B and C are 560 bytes. Derived data begins at offset 104 on all three. The appended settings, timer and flag begin at offsets 528, 544 and 552 on B/C, followed by seven padding bytes. **Derived:** +32 bytes/body, +6.06% object footprint before allocator overhead; two simultaneous prediction objects occupy 64 extra stack bytes.

**Measured machine-code evidence:** the final B/C PhysicsBenchmark and CostReplay images are byte-identical except for the PE/COFF and debug-directory build timestamp fields (four differing file bytes in each image). All executable instructions, linked function addresses, initial runtime data, unwind data and relocations match. The linker removes all uncalled sleep methods from both timed executables. `PhysicsWorld` and `PhysicsSystem` object code also match between B/C; the solver, manifold and broad-phase object text matches across A/B/C. Fresh A/C executable text also matches the corresponding binaries used in the original review.

The full-body prediction paths remain at `PhysicsSystem.cpp:569-570` (sphere/sphere hit prediction), `675-676` (zero-time collision query), and `804-805` (conservative advancement). Each copies both complete body values, including independent derived caches, before updating the local predictions. These are lifetime/correctness safeguards; none was optimized or redesigned.

**Measured instruction inspection / derived traffic:** MSVC emits memberwise copies, not a 528/560-byte `memcpy`. The copy constructor contains 62 source loads and 62 destination stores in A, versus 65 each in B/C. Summed instruction widths are 497 bytes read and 497 written per body in A, versus 522 read and 522 written in B/C; padding is skipped. Thus each two-body prediction adds 50 bytes of reads and 50 bytes of writes, plus six load/store instructions per body. The constructor grows from 737 to 778 code bytes. The measured stack reservations grow from 1,264 to 1,328 bytes in sphere/sphere prediction, 1,104 to 1,168 in the zero-time query, and 1,408 to 1,472 in conservative advancement; all B/C reservations match. The extra payload is copied even though no sleep API executes.

**Attribution limit:** the controlled change includes the larger allocation, heap/stack placement, the extra copying, and the resulting linked instruction layout. It isolates the source change as a bundle; the timing variability prevents a precise cost estimate. It does not separate cache misses, allocator size classes, copy latency and instruction-cache effects. Larger object spans can cross additional cache lines depending on alignment; hardware cache-miss and memory-bandwidth counters were not collected, so actual cache/DRAM traffic and the fraction of tick cost spent copying are **Not available**. Most solver work does not copy a body; solver timing changes must not be described as direct copy time. No production copy/cache optimization or sleep redesign was made.

### Three-way measured timing results

**Measured:** completed fixed protocol, all 108 processes retained. Timing values below use the aggregation defined above; percentages are **Derived**.

| Scene / variant | External tick ms (median; process range) | Profiled physics ms | Solver ms | Thread cycles, millions |
| --- | ---: | ---: | ---: | ---: |
| boxes / A | 3.09090; 3.03500-3.24410 | 3.09075 | 1.70855 | 10.15405 |
| boxes / B | 3.14950; 3.10010-3.24000 | 3.14940 | 1.75710 | 10.33601 |
| boxes / C | 3.12525; 3.09920-3.33800 | 3.12510 | 1.74650 | 10.27042 |
| lattice / A | 17.58350; 16.53450-18.11590 | 17.58325 | 11.87645 | 57.33676 |
| lattice / B | 17.50440; 16.92260-18.36100 | 17.50385 | 11.90635 | 57.37200 |
| lattice / C | 17.36635; 16.93790-19.37790 | 17.36610 | 11.75030 | 56.89192 |

Paired effects below are the median of three complete ABBA block effects, followed by their minimum-maximum range. B-A is the controlled source contrast for passive representation; its timing estimate still includes the variability described above. C-B is the separately reported direct residual.

| Scene / metric | B-A % (block range) | C-A % (block range) | C-B % (block range) |
| --- | ---: | ---: | ---: |
| boxes / external_ms | +1.099 (-1.828 to +2.251) | +4.264 (+2.075 to +6.618) | -0.612 (-1.219 to -0.317) |
| boxes / step_ms | +1.094 (-1.825 to +2.257) | +4.265 (+2.076 to +6.617) | -0.614 (-1.217 to -0.314) |
| boxes / solver_ms | +2.606 (-0.136 to +3.361) | +4.549 (+3.440 to +7.404) | -0.666 (-0.982 to -0.516) |
| boxes / cpu_cycles | +1.445 (-1.369 to +2.225) | +3.944 (+2.164 to +6.110) | -0.636 (-0.924 to -0.403) |
| lattice / external_ms | +0.312 (-1.023 to +3.848) | +3.097 (+0.975 to +6.236) | -1.417 (-1.487 to +2.223) |
| lattice / step_ms | +0.313 (-1.023 to +3.848) | +3.098 (+0.975 to +6.233) | -1.417 (-1.488 to +2.222) |
| lattice / solver_ms | +1.012 (+0.019 to +4.207) | +4.045 (+1.138 to +6.416) | -1.245 (-1.990 to +1.814) |
| lattice / cpu_cycles | +0.184 (-0.465 to +3.645) | +3.100 (+1.294 to +5.728) | -1.174 (-1.379 to +1.947) |

| Bodies | A external ms | B external ms | C external ms | Paired B-A % | Paired C-A % | Paired C-B % |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 50 | 2.10985 | 2.07995 | 2.10570 | -2.756 | +1.829 | +2.234 |
| 100 | 4.31175 | 4.25515 | 4.30860 | -4.850 | +0.388 | -0.316 |
| 200 | 8.88370 | 8.77140 | 8.75440 | -6.016 | -0.911 | -0.672 |
| 500 | 21.69515 | 21.52935 | 21.64240 | -8.554 | +1.716 | -2.737 |
| 1000 | 44.38340 | 44.00810 | 43.26015 | -12.939 | +0.312 | -1.769 |
| 2000 | 91.82805 | 90.25185 | 90.72315 | -9.582 | +1.499 | +0.391 |

| Bodies | Profiled physics ms A / B / C | Solver ms A / B / C |
| ---: | ---: | ---: |
| 50 | 2.10984 / 2.08998 / 2.11500 | 0.60885 / 0.60886 / 0.60860 |
| 100 | 4.30888 / 4.27999 / 4.32707 | 1.22142 / 1.20679 / 1.21642 |
| 200 | 9.02503 / 8.76686 / 8.79341 | 2.44465 / 2.40229 / 2.40249 |
| 500 | 21.74842 / 21.52821 / 21.76368 | 6.08254 / 6.07372 / 6.05094 |
| 1000 | 44.36656 / 44.21261 / 43.81584 | 12.32646 / 12.36105 / 12.12104 |
| 2000 | 92.48389 / 90.35339 / 90.81919 | 25.25708 / 24.75452 / 24.84344 |

All per-process values, benchmark ranges, paired block ranges, and the additional narrow-phase/manifold/support timing columns are in `summary.json`.

### Exact correctness, counters and validation rerun

**Measured:** all 36 benchmark processes match in every non-timing column, including the final-state fingerprint. Sleeping-body counters remain zero and solver passes remain one. The common benchmark counters are:

| Bodies | Dynamic / active | Candidate pairs = GJK = EPA = contacts = manifolds = constraints | Support calls |
| ---: | ---: | ---: | ---: |
| 50 | 25 | 19 | 209 |
| 100 | 50 | 38 | 418 |
| 200 | 100 | 75 | 825 |
| 500 | 250 | 188 | 2068 |
| 1000 | 500 | 375 | 4125 |
| 2000 | 1000 | 750 | 8250 |

Every one of the 72 replay processes completes all 1,500 finite-state ticks and matches the exact approved body CSV hashes already recorded above. All non-timing frame fields, including physical metrics, candidates, GJK/EPA calls, contacts and manifolds, also match exactly across A/B/C and the original Phase 33 reference. Box gates pass in both 5-18 s and 15-20 s windows; the lattice retains its exact moving baseline. This confirms equality of the emitted state at the existing serializer precision, not an additional claim about unexported internal bits.

| Scene | Candidate pairs min / median / max | GJK min / median / max | EPA min / median / max | Contacts min / median / max | Manifolds min / median / max |
| --- | ---: | ---: | ---: | ---: | ---: |
| boxes | 46 / 46 / 46 | 76 / 76 / 76 | 16 / 16 / 16 | 64 / 64 / 64 | 16 / 16 / 16 |
| lattice | 203 / 214 / 600 | 45 / 186 / 233 | 45 / 180 / 209 | 320 / 410 / 435 | 177 / 185 / 296 |

Revision rerun: **117/117 focused checks and 17,846/17,846 full physics checks pass in both Debug and nominal Release**. Both actual RigidBodySimulation application builds pass. Full-suite elapsed times: Debug 547.109 s, Release 73.500 s; these correctness timings are not performance samples. All three isolated benchmark/replay builds pass. No production or dedicated test file was changed during this revision.

Revision commands (all run from the repository root):

```powershell
python bin-int/phase34-attribution/validate.py
python bin-int/phase34-attribution/build.py
python bin-int/phase34-attribution/layout.py
python bin-int/phase34-attribution/codegen.py
python bin-int/phase34-attribution/linked-code.py
python bin-int/phase34-attribution/measure.py
python bin-int/phase34-attribution/summarize.py
python bin-int/phase34-attribution/audit-final.py
git diff --check
git status --short
```

The untimed dummy segmentation correction used `build.py --only=B`; all final source and compiler-command assertions were repeated. The validation script invokes the unchanged actual Debug/Release MSBuild and focused/full test commands listed earlier.

## Behavior Changes

Explicit body sleep bookkeeping APIs only. Ordinary simulation remains active and follows identical approved physical trajectories. No automatic sleeping, velocity zeroing, damping, solver/constraint modification, scheduling change, or wake propagation has been introduced. Public body size changes require rebuilding clients; both application configurations were rebuilt.

## Known Limitations

Body-local low speed is only a necessary candidate condition. It does not prove force balance, support, stable contacts, safe island membership or lack of pending interaction. This phase never uses it to suppress work or freeze a body. Public mutation and impulses do not automatically clear a manually set flag; clients must call `WakeUp` or sample/recheck as documented. Automatic wake sources, propagation and real sleeping await Phase 36. No wall-clock/frame time enters the inactivity API automatically.

The default policy is not scale invariant and is not a claim of acceptable sleeping behavior for arbitrary worlds. No sleeping performance or missed-wake claim is made.

The original +5.34% / +5.33% replay measurements remain historical evidence. The requested passive-storage control reproduces Phase 34 executable code and confirms added storage/copy traffic, but the new timing estimates do not support a precise percentage split; the direct C-B residual and inconsistent separate-pair remainder are reported above. No production representation or padding was tuned, and no timed run was discarded.

## Out-of-Scope Findings

The existing full-body prediction copies and allocator/cache/instruction-layout decomposition remain out of scope for optimization (`PhysicsSystem.cpp:569-570,675-676,804-805`; `PhysicsWorld.cpp:50`). No new physics defect was established by this revision. Existing `.gitignore` trailing blank-line diagnostic is user-owned and preserved. The Scene `constexpr maxTotal` edit, non-timing `RigidBodySimulation.cpp` scene edits, deleted historical reviews and all pre-existing untracked paths remain outside Phase 34. Prior audit/Phase 33 limitations remain historical evidence.

## git diff --check

Global command: exit **2**, unchanged pre-existing user-owned diagnostic:

```text
.gitignore:33: new blank line at EOF.
```

The Phase 34 tracked-file check exits **0** with no whitespace diagnostics. The review has no no-index whitespace diagnostics; exit 1 denotes its new-file content difference from an empty file. Git may additionally print existing LF/CRLF conversion notices; no unrelated line endings were changed.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/PhysicsBody.cpp
 M GEngine/include/GEngine/Physics/PhysicsBody.h
 M GEngine/src/Scene/_Scene.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
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
?? docs/physics/PHASE_34_REVIEW.md
?? docs/rendering/
```

Original phase ownership: the two body files, the existing physics test file and this review. Revision ownership audit: all 1,574 entry paths checked; **only this review changed during the requested revision**. The existing Phase 34 production code/tests and unrelated user work remain byte-identical or absent as at revision entry. `git status --short` is byte-identical to revision entry. HEAD remains `10ca1e2`; index empty; all tags unchanged; no Phase 34 approved tag. The prior rejected review is preserved.

## Human Decision

PENDING
