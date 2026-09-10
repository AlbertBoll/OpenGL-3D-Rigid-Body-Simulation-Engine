# Phase 39 Review

## Status

PHASE 39 STATUS: AWAITING HUMAN REVIEW

## Objective

Remove heap-backed fixed-size vector/matrix storage from the approved contact solver while preserving its numerical behavior.

## Baseline Commit

`fb63350a46fc5ceef9a446cb5ac8c712a66929ff` on `physics/refactor`.

## Previous Approved Tag

`physics-phase-38-approved`, verified at HEAD by the execution preflight. No Phase 39 approved tag exists.

## Audit Findings Addressed

`PHYS-PERF-003`: heap-heavy tiny contact-solver algebra. The historical audit is preserved. This phase optimizes the equations accepted through Phase 38, including sliding, spin and rolling resistance.

## Allowed Scope

Four production files, one existing test file, this review: six implementation/review files. The human-requested separate lattice diagnostic adds a seventh documentation file without expanding production scope. The general engine math library and other constraint types retain their existing storage.

## Files Changed

- GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
- GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h
- GEngine/include/GEngine/Physics/Constraints/SolverMath.h
- GEngine/include/GEngine/Physics/Manifold.cpp
- PhysicsTests/src/main.cpp
- docs/physics/PHASE_39_REVIEW.md
- docs/physics/PHASE_39_LATTICE_BOUNCE_DIAGNOSTIC.md

## Implementation Summary

`SolverMath.h` provides contact-local vectors and matrices backed by zero-initialized `std::array`. Const matrix indexing returns a row reference. Copying, transposition, multiplication, residuals and cached impulses have fixed storage. Dot products retain ascending-index float accumulation and the existing multiplication grouping.

The contact solver uses fixed Jacobian/cache storage and fixed local velocity, inverse-mass and impulse buffers. Its body bridge preserves the existing inverse-inertia row/column mapping, body-type-aware getters and linear/angular impulse order. The dense `J * W * J^T` evaluation remains unchanged. No effective-mass caching or sparse/scalar reordering is introduced. All finite checks, Coulomb projection, local/global iterations, warm starting, position correction, torsional resistance and rolling resistance retain their equations and order.

`Manifold.cpp` changes only its refreshed cached-impulse temporary to the same fixed vector type. This avoids a heap conversion at the contact persistence boundary.

## Tests Added / Modified

`--solver-storage`, also in the full suite, exercises 96 deterministic contact cases with rotated unequal box inertias, dynamic/static/kinematic combinations, off-center anchors, sliding enabled/disabled, spin/rolling combinations, cached warm starts and positive/zero/negative timesteps. Each case runs three warm-start cycles, six alternating normal-plus-friction/friction-only passes per cycle, and position correction. A scalar fingerprint includes all Jacobian entries, accumulated sliding/spin/rolling impulses, physical velocities and positions. The reference fingerprint is `14496816551934000004`, measured identically in baseline Debug and nominal Release before production changes. MSVC x64 uses exact fingerprint equality; other toolchains retain finite-state checks without claiming this platform-specific golden.

A scoped MSVC Debug-CRT allocation hook covers contact construction/copy, warm start, solve and post-solve. An independent heap-vector sentinel verifies that the hook detects allocation. Baseline measured **307,200** allocations; candidate Debug and Release focused runs measure **0**. The inline contact size changes from **224 to 288 bytes**, excluding the baseline separately allocated storage. Fixed types also have compile-time trivial-copy and inline-size checks. The hook is restored before reporting results. It is available in both current x64 configurations because both use `/MTd`.

## Validation Commands

Artifacts and exact subprocess commands are retained under `bin-int/phase39-solver/` (ignored).

```powershell
python bin-int/phase39-solver/validate.py before
python bin-int/phase39-solver/capture.py
python bin-int/phase39-solver/measure.py before
python bin-int/phase39-solver/validate.py after
python bin-int/phase39-solver/measure.py after
```

Representative direct commands (repeat the build/test commands for Debug and Release; exact full lists and exits are in `after-validation.json`):

```powershell
& 'C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe' PhysicsTests/PhysicsTests.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:quiet
& bin/Release/PhysicsTests/PhysicsTests.exe --solver-storage
& bin/Release/PhysicsTests/PhysicsTests.exe
& bin/Release/PhysicsTests/PhysicsTests.exe --rolling-stability
& bin-int/phase39-solver/after/PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5
& bin-int/phase39-solver/after/PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5 --rolling-workload --spin-resistance-length=0.05 --rolling-resistance-length=0.05
& bin-int/phase39-solver/after/RollingReplay.exe bin-int/phase34-sleep-primitives/worlds/lattice.txt fixed 0.008333333333333333 bin-int/phase39-solver/after-lattice-120-awake 1 disabled 120 0.05 0.05
```

Builds use the current Visual Studio 2022 MSBuild projects, x64, `/m /v:quiet`, with `Configuration=Debug` and `Configuration=Release`. Final validation includes full physics tests, focused storage/convergence/friction/spin/rolling/stack regressions, application builds, and benchmark/replay builds. The unchanged exact-world replay source is reused through an isolated Phase 39 project/output directory; Phase 38 artifacts are preserved.

Sandbox process setup repeatedly failed with `helper_unknown_error`; scoped reads, workspace writes and builds use reviewed escalation. No automatic approval rejection occurred.

## Debug Result

Measured: the Debug build and all **18,898** full physics checks pass, including the 99 new storage checks. All focused storage/convergence/friction/spin/rolling/stack checks and the Debug application build pass. Full-suite runtime is 177.70 seconds. The exact baseline fingerprint matches and the focused CRT allocation count is **0**. No known issue is observed in the existing non-gating diagnostic. Baseline full-suite count was 18,799.

## Release Result

Measured: the candidate Release build and all **18,898** full physics checks pass, including the 99 new storage checks. All focused storage/convergence/friction/spin/rolling/stack checks pass. The golden fingerprint is unchanged and the focused allocation count is **0**. The Release application build also passes. Full-suite runtime is 13.91 seconds. No known issue is observed in the existing non-gating diagnostic. Baseline full-suite count was 18,799.

Nominal Release retains `/Ox /Oi /fp:precise` and the **Debug static CRT `/MTd` override**. Allocation/timing results describe this configuration, not a true Release CRT comparison.

## Stability Results

Measured: all four candidate lattice runs pass the predeclared gates and exactly match the fresh baseline body CSV SHA-256, sleep history, eligibility history and every non-timing frame field. All 180 dynamic identities form the required continuous cohort at both rates/modes. Peak total energy, including the initial state, is **30,240** in every run. All final eligibility failure counts are zero. The protocol preserves the approved exact exported 185-body lattice (180 dynamic spheres), world SHA-256 `79eb5a76b06726b77333fff93ddb47999671df025ef1c75322494b8ac62116c6`, gravity, initial conditions, materials, body order, one solver pass, and spin/rolling lengths 0.05. Both 60/120 Hz run 120 simulated seconds with sleeping enabled and disabled. Sleep thresholds/dwell and contact eligibility remain unchanged. The 110-120 second window requires at least 171 continuously sleeping identities when enabled, or continuously quiet identities when disabled, penetration <=0.035, and peak energy <=30,391.2 including the initial 30,240. Candidate histories must exactly match the fresh baseline body/sleep/eligibility and all non-timing frame fields.

Measured exact exported stack with spin/rolling enabled: both 60/120 Hz pass the unchanged 5-18 s and 15-20 s windows. Settled peak linear/angular motion and excursion remain zero; maximum penetration is 0.0190279 / 0.0171305; minimum/final mean Y is 4.46405 / 4.46966; manifolds/points remain 16/64; peak total energy is 864. Both Debug and Release agree. The full suite additionally retains all original timestep/pass-count, isolated-sphere and resistance regressions.

Measured lattice final state (identical before/after):

| Rate / sleeping | Continuous cohort | Final sleeping / moving | Final max linear / angular speed | Final linear / angular KE | Final energy | Late peak depth | Manifolds / points |
|---|---:|---|---|---|---:|---:|---|
| 60-sleep | 180/180 | 180 / 0 | 0 / 0 | 0 / 0 | 3218.964780810 | 0.020003861 | 267 / 268 |
| 60-awake | 180/180 | 0 / 0 | 0.000374043273 / 6.3202875e-05 | 2.36838932e-07 / 1.45213679e-09 | 3219.024590730 | 0.020002780 | 271 / 271 |
| 120-sleep | 180/180 | 180 / 0 | 0 / 0 | 0 / 0 | 3249.895830630 | 0.020003017 | 223 / 223 |
| 120-awake | 180/180 | 0 / 0 | 0.000137760871 / 5.02396279e-06 | 1.3255166e-08 / 5.07899649e-12 | 3224.151698130 | 0.020000221 | 245 / 246 |

## Benchmark Results

Measured: all eight before/after benchmark processes and eight before/after lattice processes pass. Both benchmark repeats match exactly within each revision/mode. Predeclared configuration: normal priority, affinity mask 4 (logical CPU 2), serial timed children with no concurrent build/test work. Collision-heavy 100/1,000/10,000 bodies: two warmups, five measured samples, two processes per mode/revision. Report the median of process medians (ten measured samples per size). Profiler stage columns are process means, summarized across repeats. Original unloaded and loaded spin/rolling workloads both run. Lattice primary timing window is 5-20 seconds inclusive after five seconds of warmup (901/1,801 samples); late window is 110-120 seconds (601/1,201). CSV/diagnostic work is outside the profiled physics update.

Measured collision-heavy results in the documented nominal Release configuration. Step columns are medians; solver columns summarize profile means. Percent reductions are Derived.

| Workload | Bodies | Before step ms | After step ms | Before solver ms | After solver ms | Step reduction |
|---|---:|---:|---:|---:|---:|---:|
| collision | 100 | 4.368450 | 2.408350 | 1.219600 | 0.052300 | 44.87% |
| collision | 1000 | 47.218000 | 23.684600 | 12.443570 | 0.549880 | 49.84% |
| collision | 10000 | 493.962500 | 235.870600 | 134.918040 | 7.744720 | 52.25% |
| loaded | 100 | 4.800950 | 2.781900 | 1.549290 | 0.350960 | 42.06% |
| loaded | 1000 | 47.331100 | 26.702350 | 15.084350 | 3.389180 | 43.58% |
| loaded | 10000 | 500.829000 | 264.946100 | 155.655350 | 36.758400 | 47.10% |

All common non-timing counters and final physical fingerprints match exactly at all three sizes and in both repeats. Candidates/contacts/manifolds/constraints remain 38 / 375 / 3,750. Loaded rolling applies 38 / 375 / 3,750 increments, unchanged. The loaded 10,000-body rolling subset is 20.60618 -> 20.17589 ms (Measured), so the contact model remains active.

The 10,000-body unloaded manifold stage is 141.11200 -> 20.80921 ms (Measured). Manifold lookup is still linear; the reduction includes construction/copy of contacts with fixed storage and the fixed refresh temporary. The unchanged narrow-phase stage is 203.22940 -> 193.30946 ms (Measured), with no narrow-phase algorithm change. These timings do not isolate run-to-run, allocator or layout effects; isolated attribution is Not available.

Measured exact-lattice timing windows (milliseconds):

| Rate / sleeping | Window | Before step median | After step median | Before solver median | After solver median |
|---|---|---:|---:|---:|---:|
| 60-sleep | primary | 12.489900 | 4.809100 | 9.108100 | 1.360600 |
| 60-sleep | late | 0.225700 | 0.237000 | 0.026300 | 0.025500 |
| 60-awake | primary | 15.760000 | 7.183600 | 9.657400 | 1.400000 |
| 60-awake | late | 18.357700 | 10.342900 | 9.054600 | 0.805700 |
| 120-sleep | primary | 10.867100 | 4.465900 | 7.910000 | 1.107300 |
| 120-sleep | late | 0.212900 | 0.213300 | 0.024200 | 0.023600 |
| 120-awake | primary | 14.273100 | 6.496000 | 8.636600 | 1.114900 |
| 120-awake | late | 14.694600 | 7.276400 | 8.061900 | 0.512900 |

The late sleeping windows contain no active contact solve: 60 Hz step median changes from 0.2257 to 0.2370 ms (+0.0113 ms, +5.01%, Derived); 120 Hz changes from 0.2129 to 0.2133 ms (+0.0004 ms, +0.19%, Derived). These whole-step changes are reported without attributing them to the solver optimization. Active lattice windows improve substantially while all compared physical states and work counts remain exact.

Measured unchanged lattice work (counts over 120 simulated seconds):

| Rate / sleeping | Spin row visits | Applied spin increments | Applied rolling increments |
|---|---:|---:|---:|
| 60-sleep | 586902 | 432224 | 432235 |
| 60-awake | 2020499 | 1792986 | 1792998 |
| 120-sleep | 659783 | 400302 | 400314 |
| 120-awake | 3655428 | 3073363 | 3073375 |

All state hashes, counters and exact commands are retained in `before-measurements.json` / `after-measurements.json`; tables are generated in `summary.json` / `summary.md`. The measurement artifacts are local ignored files, not phase commit contents.

## Behavior Changes

Storage/allocation behavior only. No intended numerical change; exact before/after equivalence is the acceptance target.

## Known Limitations

The dense 12x12 inverse-mass calculation and general engine heap math remain. The fixed Jacobian increases inline contact size while replacing separately allocated storage; layout and whole-step effects are measured together. The focused CRT hook does not constitute whole-world allocation accounting. Exact fingerprints apply to the validated MSVC x64 floating-point configuration.

## Out-of-Scope Findings

- The human-requested [lattice bounce diagnostic](PHASE_39_LATTICE_BOUNCE_DIAGNOSTIC.md) confirms exact approved Phase 38/candidate Phase 39 equivalence at 60/120 Hz and 1/2/4/8 passes, including instrumented contact traces. It records a follow-up contact/solver-convergence finding with moving-support, restitution-history and separate position-correction evidence. This is not a Phase 39 blocker; production physics and the acceptance measurements remain unchanged.
- `.gitignore:33` has a pre-existing extra blank line at EOF; whole-tree whitespace checking reports it. The file is user-owned and preserved byte-for-byte.
- The nominal Release `/MTd` override remains in the current generated PhysicsTests/PhysicsBenchmark projects and `premake5.lua`; this phase does not own the build configuration finding `PHYS-BUG-024`.
- Plan/predecessor review status prose predates the approved tags; the preflight verified the actual Phase 38 approved state. These unrelated documents are preserved.

## git diff --check

Whole-tree `git diff --check`: exit 2; literal stdout:

```text
.gitignore:33: new blank line at EOF.
```

Literal stderr (pre-existing line-ending warnings):

```text
warning: in the working copy of 'GEngine/src/Scene/_Scene.cpp', LF will be replaced by CRLF the next time Git touches it
warning: in the working copy of 'docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md', LF will be replaced by CRLF the next time Git touches it
```

The check over the six phase paths passes with exit 0 and no output. The two new files are also checked with `git diff --no-index --check -- NUL <file>`: exit 1 denotes new-file differences; stdout/stderr are empty, with no whitespace errors. The user-owned `.gitignore` is byte-identical to entry. No unrelated whitespace correction was made.

## git status --short

Literal `git status --short` at the original implementation acceptance validation (the separate diagnostic has its own final status/checks):

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
?? docs/physics/PHASE_39_REVIEW.md
?? docs/rendering/
```

The six new/modified phase paths are exactly the additions to the initial status. All 20 existing unrelated-file hashes match entry. Index is empty; HEAD remains `fb63350`; no Phase 39 approved tag exists. No commit, tag, push or later-phase work was performed. Final commands/exits are in `final-checks.json`.

## Human Decision

PENDING
