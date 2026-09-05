# Phase 19 Review

## Status

PHASE 19 STATUS: AWAITING HUMAN REVIEW

Validation is complete. Debug and nominal Release pass 138/138 focused checks and 1,052/1,052 full-suite checks. The default preserves the approved baseline; the measured opt-in iteration tradeoffs are recorded below.

## Objective

Replace the hard-coded single global solver pass with a bounded per-system iteration count. Address only the repeated-traversal portion of **PHYS-BUG-005**.

## Baseline Commit

`9a9dad3da10c33c99270a638cb9de559aa0b3e5e` — `physics: phase 18 suppress low-speed restitution`.

Branch: `physics/refactor`. The required preflight verified the preceding tag is an ancestor of HEAD and no Phase 19 approved tag exists.

## Previous Approved Tag

`physics-phase-18-approved`.

## Audit Findings Addressed

Part of **PHYS-BUG-005**: insufficient support propagation from only one global manifold traversal. Contact rank/feature generation and position stabilization remain separate work.

## Allowed Scope

Repeated solver traversal only: two production files, one dedicated test file, the existing benchmark, and this review (five files total).

## Files Changed

| File | Purpose |
|---|---|
| `GEngine/include/GEngine/Physics/PhysicsSystem.h` | Per-system bounded iteration policy and accessors |
| `GEngine/include/GEngine/Physics/PhysicsSystem.cpp` | Read the configured count in the existing solve loop |
| `PhysicsTests/src/main.cpp` | Configuration, analytical propagation, warm-start, and repeatability checks |
| `PhysicsBenchmark/src/main.cpp` | Iteration selector, pass-count validation, physical-state fingerprint, and additional probe metrics |
| `docs/physics/PHASE_19_REVIEW.md` | Review evidence |

The audit, plan, previous phase reviews, scene source, build files, and all pre-existing working-tree files are preserved.

## Implementation Summary

`PhysicsSystem::SetSolverIterations(int)` accepts **1 through 32**, returning `true`. Invalid requests return `false` and preserve the previous setting. `GetSolverIterations() const` exposes the current count. The public constants document the bounds and default. Configuration is per system, remains set across world replacement/teardown, and takes effect on the next update. Setting and stepping are single-threaded.

The default is **1**, preserving the approved production behavior. Higher counts are an explicit caller choice. The upper bound limits accidental unbounded traversal cost; it is not a guarantee that every scene settles at every count.

The only update-path change is `maxIters = m_SolverIterations` in place of `maxIters = 1`. `PreSolve` and warm starting still run once per step. Only the existing ordered `ManifoldCollector::Solve()` traversal repeats. The profiler's existing nonempty-contact iteration counter now reports the selected count. The existing disabled `PostSolve` call stays disabled.

Restitution threshold (1.0), product material combination, constraint mathematics, Baumgarte, damping, sleeping, GJK/EPA, manifold generation/persistence, TOI handling, and integration scheduling are unchanged. No islands, reordering, adaptive early exit, or solver-storage optimization were introduced.

## Tests Added / Modified

Added `--solver-iterations` and included the same checks in the full suite: **138 checks**, increasing the full total from **914 to 1,052**. No existing assertion or expected-failure exemption was weakened.

- Default and boundary values; rejection of `INT_MIN`, -1, 0, 33, and `INT_MAX` without changing the previous count.
- Independent system settings, world replacement/teardown retention, and zero contact-pass telemetry for an empty world.
- Counts **1, 2, 4, 8, 16, 32** on a two-contact equal-mass support chain through `PhysicsSystem::Update`.
- Analytical velocity residual, finite state, static support, unchanged angular velocity, one integration, persistent contacts, and actual profiler counts.
- Repeat state equality and changing from 1 to 8 passes with populated warm-start caches.

### Analytical fixture and tolerances

Three radius-1 spheres have centers at y=0, 2, 4. The bottom body is static; the upper two have mass 1 and initial vertical velocity -1. Gravity is zero; two exact touching witnesses are inserted in floor-to-top order. Collision masks disable new narrow-phase contacts so this test isolates traversal of those persistent constraints. The same geometry is used in red and green runs.

A floor pass removes the lower dynamic body's velocity, and the upper equal-mass contact shares the remaining momentum. After N passes, both vertical velocities are **-2^-N**. Restoring the same geometry/initial velocity while retaining cached impulses gives **-2^(-2N)** after the next update; applying warm starting on every pass violates this result. Changing from 1 to 8 passes gives **-2^-9**.

Velocity/position tolerance: **1e-6 absolute per component**. Zero angular motion and repeat comparisons use exact equality. Profile assertions run when profiling is enabled, as in both validated engine configurations.

### Failure-before -> pass-after

| State | Nominal Release result |
|---|---|
| New API and final tests present; approved hard-coded one-pass traversal still in place | **80/138 failed**, exit 1 |
| Same tests, loop reads the configured count | **138/138 passed**, exit 0 |

The red control retained the approved `PhysicsSystem.cpp` traversal, with the newly introduced header API present so the final tests could compile. It is not a claim that the old header had this API. The final focused fixture/assertions were unchanged between red and green.

Raw evidence: `bin/phase19-red-Release-focused.log`, `bin/phase19-green-{Debug,Release}-focused.log`.

## Validation Commands

Inspected the current Visual Studio x64 solution/projects. GEngine and PhysicsBenchmark have physics profiling enabled. Used MSBuild v143 via the installed Visual Studio 2022 Community toolchain.

```powershell
$phase19BuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $phase19BuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=<Debug-or-Release> /p:Platform=x64 /clp:ErrorsOnly

.\bin\Debug\PhysicsTests\PhysicsTests.exe --solver-iterations
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --solver-iterations
.\bin\Release\PhysicsTests\PhysicsTests.exe

# Nominal Release: N=1,2,4,8,16, each twice.
# Debug: N=1,8, each once.
.\bin\<configuration>\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=<N>

# Nominal Release: N=1,2,4,8,16.
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5 --solver-iterations=<N>
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5 --steady-state-warmup-steps=4 --steady-state-measured-steps=8 --solver-iterations=<N>
```

The local Python subprocess runner removes duplicate Path/PATH environment entries equivalently and logs each command separately. Benchmarks run sequentially without concurrent builds/tests.

Logs are local ignored artifacts: `bin/phase19-validation.log` (runner), `bin/phase19-analysis.log` (comparison), `bin/phase19-results.log` (parsed measurements), `bin/phase19-entry.log` (ownership snapshot), `bin/phase19-before-*`, `bin/phase19-red-*`, and `bin/phase19-green-*`. Build logs have companion `-build-details.log` files. This review records the durable results and reproduction commands.

## Debug Result

- Before: build succeeded; full suite **914/914**.
- After: build succeeded; focused **138/138**; full suite **1,052/1,052**.
- No new expected failures. The existing non-gating diagnostic reported no observed known issue.

## Release Result

- Before: build succeeded; full suite **914/914**.
- After: build succeeded; focused **138/138**; full suite **1,052/1,052**.
- **Nominal Release remains optimized with the Debug static CRT override `/MTd`.** D9025 is still present. These are relative measurements under that configuration, not representative true Release-CRT allocator/runtime results.

## Stability Results

### Workload and comparison method

All four existing fixtures retain their original geometry, materials, body creation order, gravity, fixed timestep **1/120 s**, and **1,200 steps** (10 seconds), with no random input or discarded warmup. Floors have half-extents **(50, 0.5, 50)** at the origin; gravity is (0, -12, 0). Stack/lattice/sphere materials remain elasticity 0.5 and friction 0.5 per body. The stack is four columns by four levels of unit-half-extent boxes; the lattice is 180 radius-1 spheres in the original 5 x 6 x 6 layout. The free asymmetric body retains its original shape/angular velocity and zero gravity.

The benchmark now reports mean solver time, nonempty solver passes per step, and peak/final floor-plane intrusion. Intrusion is `max(0, 0.5 - lowest_world_bound_y)` over dynamic shapes. It is a geometric measure relative to the floor plane, **not** pairwise penetration; bodies outside the finite floor's footprint can also contribute. No claim of full contact-depth validation is made.

Default neutrality requires exact equality of all original non-timing fields at printed precision (six decimals). Deterministic repeat comparisons include all 92 non-timing fields in the expanded four-row output; timings are excluded. These checks do not establish cross-machine bitwise determinism.

**Measured**, nominal Release; first run shown. All repeated physical fields match. Every finite flag is 1.

### 4 x 4 stack

Initial mechanical energy: **864**; initial average Y: **4.5**. Linear speeds are in engine length units/s; angular speeds are in rad/s.

| Passes | Peak energy % | Final energy | Final avg Y | Peak linear | Final linear | Peak angular | Final angular | Moving bodies |
|---|---|---|---|---|---|---|---|---|
| 1 | 100.000000 | 862.686012 | 4.493112 | 0.215006 | 0.097994 | 0.090607 | 0.019983 | 2 |
| 2 | 100.000000 | 863.290968 | 4.496307 | 0.150877 | 0.000003 | 0.078246 | 0.000001 | 0 |
| 4 | 100.000000 | 863.714973 | 4.498515 | 0.100989 | 0.001229 | 0.093604 | 0.000175 | 0 |
| 8 | 100.000000 | 863.878419 | 4.499367 | 0.083085 | 0.000112 | 0.104020 | 0.000144 | 0 |
| 16 | 100.000000 | 863.953138 | 4.499756 | 0.082641 | 0.000390 | 0.107024 | 0.000442 | 0 |

### 180-sphere lattice

Initial mechanical energy: **30,240**.

| Passes | Peak energy % | Final energy | Final avg Y | Peak linear | Final linear | Peak angular | Final angular | Moving bodies |
|---|---|---|---|---|---|---|---|---|
| 1 | 102.332486 | 5769.001126 | 1.458899 | 20.800062 | 15.614874 | 12.364333 | 10.979907 | 180 |
| 2 | 100.000000 | 4740.853818 | 1.434988 | 16.134418 | 13.302149 | 13.996249 | 8.328952 | 180 |
| 4 | 100.000000 | 4354.961938 | 1.492553 | 14.300018 | 6.144273 | 9.007620 | 6.132289 | 180 |
| 8 | 100.000000 | 4224.305124 | 1.487536 | 14.300018 | 5.600746 | 6.746700 | 5.626596 | 180 |
| 16 | 100.000000 | 4214.523069 | 1.489605 | 14.300018 | 5.740648 | 6.586999 | 5.731107 | 180 |

### Contact counts and floor-plane intrusion

| Passes | Stack manifolds / contacts | Stack peak / final intrusion | Lattice manifolds / contacts | Lattice peak / final intrusion |
|---|---|---|---|---|
| 1 | 16 / 52 | 0.008241 / 0.005632 | 174 / 626 | 5.180741 / 5.180741 |
| 2 | 19 / 69 | 0.011739 / 0.010488 | 180 / 676 | 4.725971 / 4.725971 |
| 4 | 17 / 52 | 0.003085 / 0.002862 | 179 / 678 | 0.196781 / 0.030957 |
| 8 | 17 / 50 | 0.002292 / 0.002263 | 182 / 695 | 0.158858 / 0.017013 |
| 16 | 16 / 43 | 0.001994 / 0.001990 | 184 / 691 | 0.175413 / 0.018994 |

### Observed solver cost in the stability probes

These are mean solver-stage milliseconds over all 1,200 steps, not the controlled five-sample performance medians below.

| Passes | Stack solver ms | Sphere solver ms | Lattice solver ms | Stack / lattice mean step ms |
|---|---|---|---|---|
| 1 | 1.341162 | 0.020635 | 11.236922 | 2.630864 / 15.051769 |
| 2 | 3.143409 | 0.038772 | 22.273437 | 5.160596 / 26.400319 |
| 4 | 4.622916 | 0.072419 | 45.059908 | 6.135668 / 49.209436 |
| 8 | 8.610333 | 0.142316 | 95.157321 | 9.945857 / 99.246553 |
| 16 | 14.778839 | 0.289093 | 193.455378 | 16.832455 / 197.519065 |

The single sphere's physical outputs are identical at all five measured counts: final Y **1.499874**, final maximum linear speed **0.001041**, final maximum angular speed **0.001039**, zero moving bodies, final energy **17.998483**, peak energy **100%**, and peak/final floor-plane intrusion **0.000126**. Its solver-pass average is N times 0.805833 because airborne steps have no resting constraint.

The free asymmetric body's physical output is also unchanged: peak/final energy **5.432790**, a **4.242841%** rise from 5.211667, with finite state. This existing angular-integration drift is preserved.

**Acceptance:** the default is neutral against the approved baseline (all 80 original non-timing fields equal), and matches all 92 expanded non-timing fields against the final-harness one-pass control. Every nominal Release count repeats identically. Debug and nominal Release match all 92 non-timing fields at counts 1 and 8. The four-pass example improves final stack height, residual motion, and floor-plane intrusion while retaining peak energy at 100% of initial.

This is not monotonic tuning: two passes have more stack intrusion than one; counts 4/8/16 have slightly higher peak stack angular speed than one; sixteen passes leave more residual stack/lattice motion than eight. The lattice retains **180 moving bodies** at every measured count. Higher counts improve selected metrics but do not establish general settling or justify silently replacing the one-pass default.


## Benchmark Results

**Measured**, nominal Release x64 with profiling and the existing CRT caveat. Same overlapping-box and separated-box workloads, fixed 1/120 s timestep, body counts **100 / 1,000 / 10,000**, **2 discarded warmup samples and 5 measured samples** per count. Each collision-heavy sample creates the same fresh scene and measures one update. Separated samples additionally discard 4 update steps, then measure 8 steps; results are per step. Reported total costs are the median of five samples; solver-stage costs are means from the profiler and include its existing PreSolve work.

The final benchmark harness was first run with the approved hard-coded loop at one pass, then with the configured loop using the same fixtures and measurement code. Its one-pass stability output also matched all 80 original non-timing fields from the unmodified approved baseline. Physical state fingerprints cover body pose, quaternion, linear velocity, and angular velocity in fixed body order; timing, addresses and IDs are excluded. Measured repeated benchmark samples must have matching fingerprints or the executable fails.

### Collision-heavy paired boxes

| Passes | 100 bodies median ms | 1,000 bodies median ms | 10,000 bodies median ms | 10,000 solver mean ms |
|---|---|---|---|---|
| Before: 1 | 4.027000 | 41.027500 | 448.484600 | 99.616780 |
| 1 | 4.180000 | 45.960900 | 534.541000 | 112.299220 |
| 2 | 5.208900 | 57.699600 | 618.153900 | 196.424280 |
| 4 | 6.761800 | 73.611500 | 808.196400 | 380.806040 |
| 8 | 10.613700 | 111.741900 | 1177.048200 | 752.466300 |
| 16 | 19.019200 | 175.314000 | 1898.570800 | 1483.894720 |

### Separated steady-state boxes

| Passes | 100 bodies median ms | 1,000 bodies median ms | 10,000 bodies median ms | 10,000 solver mean ms |
|---|---|---|---|---|
| Before: 1 | 0.018800 | 0.196038 | 2.204575 | 0.000047 |
| 1 | 0.018275 | 0.188475 | 2.958638 | 0.000125 |
| 2 | 0.018650 | 0.202038 | 2.368850 | 0.000115 |
| 4 | 0.018412 | 0.190750 | 2.971875 | 0.000147 |
| 8 | 0.019550 | 0.189000 | 2.969225 | 0.000180 |
| 16 | 0.018475 | 0.210187 | 2.826788 | 0.000107 |

All collision-heavy runs preserve the initial contact workload counters: **3750.000000 generated contacts**, **3750.000000 manifolds**, and **3750.000000 constraints** at 10,000 bodies. The measured solver traversal count equals the configured count. Separated runs retain zero contacts/constraints and zero nonempty-contact solver passes.

The one-pass before/after physical fingerprints and all non-timing counters match for all three body counts in both workloads. Each five-sample run passes its internal fingerprint repeat check. Higher-count physical output may differ as expected from repeated impulses; no cross-count bitwise equivalence is claimed. The measured one-pass 10,000-body median increased from 448.484600 to 534.541000 ms for collision-heavy input (**19.19%**, derived), and from 2.204575 to 2.958638 ms for separated input (**34.20%**, derived). The before and after runs used identical configurations at different times; host/run-to-run effects were not isolated. These results do not establish default performance neutrality or attribute the increase to the configured loop. Physics neutrality is supported by the matching physical outputs.

**Derived:** the measured 10,000-body median at 16 passes is 3.552 times the one-pass after median. The tables report the measured cost instead of assuming linear whole-step scaling.

The final benchmark builds also reject CLI counts 0, -1, 33, INT_MAX, 8junk, and 1.5 in both configurations. Nominal Release passes the two-body 32-pass smoke run; the Debug performance entry point hits the pre-existing logger defect documented below. The final rebuild widens the new counter multiplication before multiplying; final performance logs are bin/phase19-final-Release-{collision,separated}-<N>.log.


## Behavior Changes

Explicit callers may request 2–32 existing ordered solve passes. The default remains 1 and preserves the approved probe outputs. Increased counts improve propagation on the analytical fixture and selected stack metrics, with additional measured cost and non-monotonic changes in other stability metrics. This phase does not select a new global production iteration policy.

## Known Limitations

- The one-pass after-run performance medians are higher than the before-run medians; their cause is not isolated. See the measured values above.
- The existing Debug performance entry point crashes before solver execution because logging is uninitialized. Its separately initialized regression mode and PhysicsTests pass.
- Only 1, 2, 4, 8, 16 received full workload/performance sweeps; 32 additionally received focused analytical/lifecycle-bound coverage. Intermediate counts were not individually benchmarked.
- More passes do not provide missing face contacts, remove velocity bias, guarantee monotonic improvement, or establish global lattice settling.
- Floor-plane intrusion is not an inter-body/contact-depth metric and is not restricted to the finite floor footprint.
- Exact repeated output is demonstrated for these fixed workloads on this machine/build pair.
- The profiler includes existing timing overhead; no allocation count or true Release-CRT measurement is claimed.

## Out-of-Scope Findings

**Newly observed pre-existing benchmark harness defect:** Debug paired-box performance mode exits with access violation 0xC0000005 at counts 1, 8, and 32 (also with 100 bodies at count 1). The debugger stack is `std::_Atomic_storage::load -> spdlog::logger::should_log -> GEngine::ShapeBox::Build -> ShapeBox::ShapeBox -> main`, before any PhysicsSystem/solver call. `PhysicsBenchmark/src/main.cpp:783` constructs the box without the logger initialization used only by regression mode at line 493; `ShapeBox.cpp:73` logs its center of mass. The approved Phase 18 main has the same omission, and ShapeBox.cpp/Core/Log.h match that tag. This unrelated initialization defect is documented and left unchanged under the repository scope rule. Raw evidence: `bin/phase19-debug-benchmark-crash-stack.log` and `bin/phase19-investigate-*`. The added Debug performance smoke is a recorded failure, not a passing check; required Debug focused/full tests and deterministic stability probes passed, while performance comparisons use nominal Release.

The approved Phase 18 regressions remain part of the default baseline: stack residual motion increased (final maximum linear speed **0.081224 -> 0.097994**; angular speed **0.015761 -> 0.019983**), and the lattice's final maximum linear/angular speeds increased **11.629561 -> 15.614874** and **9.088459 -> 10.979907**. See `PHASE_18_REVIEW.md`; Phase 19 does not rewrite that history or claim the default fixes it.

- **PHYS-BUG-008:** `Constraints/ConstraintPenetration.cpp:170,188` still computes Baumgarte velocity bias and subtracts it from the physical normal RHS. Position stabilization remains Phase 20.
- Remaining **PHYS-BUG-005:** `Manifold.cpp:8-104` still accumulates support-derived witnesses; `Manifold.h:27` still limits storage to four contacts without feature clipping. Iterations cannot supply missing contact rank.
- **PHYS-PERF-003:** `ConstraintPenetration.cpp:180-193` still constructs heap-backed 12x12/3x3 algebra during every contact solve. Phase 19 measures the repeated cost; it does not optimize that math.
- TOI sorting/resolution and restitution remain in `PhysicsSystem.cpp:357,395-418,617-646`. No stale-event or threshold repair is included.
- **PHYS-BUG-024:** the generated Release projects retain `/MTd`. No build configuration change is included.

## git diff --check

Phase-owned source check: exit 0, no whitespace errors.

```text
git diff --check -- GEngine/include/GEngine/Physics/PhysicsSystem.cpp GEngine/include/GEngine/Physics/PhysicsSystem.h PhysicsTests/src/main.cpp PhysicsBenchmark/src/main.cpp
```

The new review is checked separately against NUL with `git diff --no-index --check -- NUL docs/physics/PHASE_19_REVIEW.md`: exit 1 represents the new-file difference, with no whitespace diagnostics.

Repository-wide `git diff --check` returns exit 1 and reports the preserved, pre-existing issue:

```text
.gitignore:33: new blank line at EOF.
```

No phase-owned whitespace defect is hidden by that unrelated result. LF-to-CRLF notices are informational.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M GEngine/include/GEngine/Physics/PhysicsSystem.h
 M PhysicsBenchmark/src/main.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_19_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

The entry snapshot covers 15 pre-existing modified/untracked files; their hashes are unchanged. None are staged, cleaned, or included in this phase. HEAD remains the approved Phase 18 commit; Phase 19 is uncommitted and untagged.

## Human Decision

PENDING. Review the 1–32 API contract, retained one-pass default, measured opt-in tradeoffs, and existing limitations before approval.
