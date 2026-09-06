# Phase 25 Review

## Status

PHASE 25 STATUS: AWAITING HUMAN REVIEW

## Objective

Prevent initially detected positive-TOI events from applying obsolete impulses after the resting solver or an earlier ballistic impact changes a participant's motion. Recompute pending event times and revalidate contact geometry at the actual impact pose.

## Baseline Commit

`46ee88619a8f1573456735593e8ba1c41355e0ba` (`46ee886 physics: phase 24 make collision prediction pure`), branch `physics/refactor`.

The execution preflight verified the previous approved tag exists and is an ancestor of HEAD, and that no approved Phase 25 tag exists. HEAD and the empty index remain unchanged.

## Previous Approved Tag

`physics-phase-24-approved`, pointing at the baseline commit above.

## Audit Findings Addressed

- **Remaining PHYS-BUG-010**: the initial positive-TOI list previously supplied both the event schedule and obsolete normals/anchors after velocity-changing solves. A still-closing event could apply an impulse across a gap or at the wrong time. Phase 17's existing separating-velocity guard is retained and now receives current geometry.

The historical audit is unchanged. This phase covers the pending positive-TOI pairs identified by the initial narrow phase; its bounded event-discovery limitations are explicit below.

## Allowed Scope

One collision-response objective, one production file, one existing dedicated test file, and this review: three files total. No solver equation, coefficient, build configuration, broad-phase, GJK/EPA, or integration-equation change.

## Files Changed

- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_25_REVIEW.md`

## Implementation Summary

- Requery every initially detected positive-TOI pair after the resting solve, using Phase 24's pure prediction and the remaining step horizon. Cache valid times relative to the step start; a failed/out-of-horizon query retains the pair with an unscheduled sentinel.
- Select the earliest updated time. Equal times retain initial pair order. Consume one initial event per iteration, advance the whole world for the elapsed interval, and integrate the final remainder exactly once. Zero-time events do not call the integrator.
- Query both bodies again at their actual integrated pose before response. Swept-sphere queries at zero horizon reuse the existing 0.001 world-unit short-ray tolerance; generic instantaneous queries preserve GJK/EPA's existing contact/failure policy. Failed or non-finite current output supplies no impulse. There is no fallback to predicted geometry.
- Use current world/local anchors and the current B-to-A normal. The unchanged resolver checks effective contact velocity, including angular motion, and rejects non-closing impulses.
- Recompute every remaining event sharing either participant before selecting another time. This includes previously unscheduled pairs, which a later impulse can make valid again. Unrelated cached events retain their absolute time and still receive geometry validation before response.
- The resolver's impulse implementation is shared through a file-local helper with an explicit position-projection switch. The ballistic loop disables projection even for a refreshed zero-time event. The public `Collision::ResolveContact` retains its existing zero-TOI projection behavior; restitution, friction, denominators and thresholds are unchanged.
- Requeries and current-pose checks use existing narrow-phase profiling counters/timers. The vector's capacity is reused and no new persistent state, public API or body reference lifetime is introduced.

Each iteration consumes one initially detected event. The loop cannot reschedule a processed pair forever at zero time. Event selection, erase, and affected-pair scans have **Derived** quadratic worst-case bookkeeping in the number of initial positive events; expensive prediction is repeated only for affected pairs after the initial post-solver refresh. This is a correctness phase, with no performance improvement claim.

## Tests Added / Modified

Added `--toi-revalidation`, with all **94 checks** also registered in the full suite:

- Equal-mass impacts that advance a pending contact from time 1.0 to 0.4, checking analytic positions, velocities, momentum and energy.
- An earlier collision that slows a body while it remains closing, delaying its next impact from 0.6 to 2.0. Both an in-horizon delayed hit and an out-of-horizon rejection are tested.
- An inelastic oblique impact that deflects a sphere away from a stale event while it still approaches along the old normal.
- A deflected second hit with independently calculated quadratic TOI, new normal and impulse; central frictionless sphere response has no artificial torque.
- Resting-contact velocity changes that invalidate an initial ballistic prediction.
- Simultaneous events at the exact end of the step, including zero remaining time, elastic invariants, and eight repeated runs with exact fixed-order velocity comparison.
- A pending pair temporarily outside the horizon that becomes valid again after a third impact involving a prescribed Kinematic driver. Analytic final state verifies the event was retained and recomputed.
- Successful generic sphere-box impact and injected invalid support output only when the live body reaches the actual impact pose. The latter verifies safe rejection of a previously valid prediction when current GJK evaluation fails.
- Reversed creation/pair order for the non-ambiguous fixtures; finite body state and empty transient storage after every fixture.

The preliminary red control ran the first 76 checks against unchanged Phase 24 production code: **12 failed**, covering early/late timing, both deflection cases and post-resting-solver revalidation. The final 18 revival/generic-failure checks were added after that control. All 94 pass in both configurations. No old test was relaxed and no expected failure was introduced.

Tolerances: analytic sphere positions/velocities use `2e-5` to `3e-5`; the independently computed two-dimensional root/response uses `1e-4`; generic impact uses `3e-3`, consistent with the existing 0.001 collision bias and generic TOI tests. Repeatability is exact. Existing finite-state, lifetime, body-type, pure-prediction, friction, restitution, contact and stack stability gates remain enabled.

## Validation Commands

Class B mathematical/current-state validation and Class C deterministic stability, with supplemental before/after Class D stock measurements. The existing generated Visual Studio solution, x64 targets, profiling and compiler flags were inspected. Process-local Path normalization avoids the pre-existing duplicate Path/PATH environment problem. The sandbox launcher failed before execution, so scoped source edits and validation used reviewed elevated execution; no machine environment or project configuration changed.

```powershell
& .agents/skills/physics-phase-execution/scripts/Get-PhysicsPhasePreflight.ps1 -RepositoryRoot (Get-Location).Path -Phase 25
$phase25Path = $env:Path
Remove-Item Env:Path
$env:Path = $phase25Path
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Debug /p:Platform=x64 /clp:ErrorsOnly
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Release /p:Platform=x64 /clp:ErrorsOnly
.\bin\Debug\PhysicsTests\PhysicsTests.exe --toi-revalidation
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --toi-revalidation
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=1
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=8
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5 --steady-state-warmup-steps=4 --steady-state-measured-steps=8
git diff --check
git diff --check -- GEngine/include/GEngine/Physics/PhysicsSystem.cpp PhysicsTests/src/main.cpp
git diff --no-index --check NUL docs/physics/PHASE_25_REVIEW.md
git status --short
```

The baseline Release build/full suite (3,124 checks), stability probes and benchmarks preceded production edits. Correctness passed before the after measurements. Benchmarks ran serially without a concurrent build/test run. All final native build/test/probe exits are 0; the intentional red control exited 1.

Ignored evidence: `bin/phase25-entry-{status,hashes}.log`, `bin/phase25-before-*`, `bin/phase25-red-*`, `bin/phase25-final-{debug,release}-{build,focused,tests}.log`, `bin/phase25-after-stability-{1,8}.log`, and `bin/phase25-after-{collision,separated}.log`. Build logs include normal-verbosity compiler/linker evidence. Extended probes use nominal Release; Debug includes the full suite's focused stability gates.

## Debug Result

x64 engine/tests/benchmark build: **pass**, exit 0. Focused **94/94**, full **3,218/3,218**, both exit 0. The full suite reports zero observed known issues in its one existing non-gating diagnostic.

## Release Result

Nominal optimized x64 engine/tests/benchmark build: **pass**, exit 0. Focused **94/94**, full **3,218/3,218**, both exit 0. Compiler output confirms the engine's `/MT` is overridden by `/MTd`; these are not representative true Release-CRT allocator/runtime measurements. Existing vendor/linker warnings remain.

## Stability Results

**Measured**, headless fixed geometry/materials/body order/gravity, dt `1/120`, 1,200 steps (10 seconds), separate one/eight solver-pass runs. Every recorded state is finite. Stack, single sphere and lattice peak mechanical energy remains 100% of its initial value in both revisions. The following are comparisons, not new acceptance tolerances or settling claims.

| Stack state / passes | Final energy | Final average Y | Peak linear / angular speed | Final linear / angular speed | Moving bodies | Final manifolds / points | Peak / final floor penetration |
| --- | ---: | ---: | --- | --- | ---: | --- | --- |
| Phase 24 / 1 | 860.585892 | 4.481866 | 0.565015 / 0.205754 | 0.169505 / 0.068155 | 8 | 18 / 70 | 0.043898 / 0.018244 |
| Phase 25 / 1 | 859.501459 | 4.476312 | 0.605936 / 0.221727 | 0.219385 / 0.043763 | 5 | 25 / 78 | 0.044216 / 0.020010 |
| Phase 24 / 8 | 863.821763 | 4.499072 | 0.065288 / 0.058103 | 0.000000 / 0.000000 | 0 | 16 / 64 | 0.000983 / 0.000891 |
| Phase 25 / 8 | 863.821763 | 4.499072 | 0.065288 / 0.058103 | 0.000000 / 0.000000 | 0 | 16 / 64 | 0.000983 / 0.000891 |

The eight-pass stack's non-timing fields match exactly. The default one-pass stack has changed residual motion, slightly greater peak/final floor penetration and lower average height. It retains five moving bodies; the phase does not establish one-pass settling.

| Lattice state / passes | Final energy | Final average Y | Peak linear / angular speed | Final linear / angular speed | Final manifolds / points | Peak / final floor penetration |
| --- | ---: | ---: | --- | --- | --- | --- |
| Phase 24 / 1 | 4284.630649 | 1.489803 | 14.400019 / 6.416298 | 6.058355 / 6.065278 | 183 / 380 | 0.180509 / 0.135938 |
| Phase 25 / 1 | 4278.979598 | 1.490630 | 14.400019 / 6.611325 | 6.408699 / 6.454880 | 182 / 371 | 0.198102 / 0.078580 |
| Phase 24 / 8 | 4180.014077 | 1.492569 | 14.300018 / 6.665102 | 5.646073 / 5.711784 | 182 / 373 | 0.192301 / 0.042836 |
| Phase 25 / 8 | 4168.224381 | 1.492660 | 14.300018 / 6.217107 | 5.687394 / 5.832269 | 188 / 375 | 0.181385 / 0.020635 |

All 180 lattice bodies remain moving in every run (linear/angular moving threshold 0.05). The one-pass lattice's peak floor penetration and final speed increase, while final penetration decreases. The eight-pass lattice also changes trajectory. These outcomes remain a human-review tradeoff; no solver tuning or damping was added to hide them.

Single-sphere and free-asymmetric-body non-timing results match exactly before/after in both configurations. The sphere ends at Y=1.499874, linear/angular speed 0.001068/0.001066, zero moving bodies, one manifold/point, and penetration 0.000126. The unchanged free-body integrator reaches energy 5.432790 from 5.211667 (+4.242841%) and angular-momentum magnitude +2.635889% at this timestep. Existing angular mathematical gates pass.

## Benchmark Results

**Measured**, nominal Release, profiling enabled, one solver pass, identical workload/options before/after. Each body count uses two fresh-world warmup samples and five measured samples. Collision samples measure one step. Separated samples use four per-world warmup steps followed by eight measured steps. The table reports median external whole-step time (median of per-sample means for separated workloads).

| Workload | Bodies | Before median ms | After median ms |
| --- | ---: | ---: | ---: |
| Collision | 100 | 4.099100 | 4.121100 |
| Collision | 1000 | 41.694500 | 40.804000 |
| Collision | 10000 | 452.233100 | 453.555600 |
| Separated | 100 | 0.018200 | 0.018675 |
| Separated | 1000 | 0.187262 | 0.189337 |
| Separated | 10000 | 2.076425 | 2.970887 |

All non-timing CSV fields and final-state fingerprints match exactly for all six comparisons. Collision candidates/contacts/manifolds/points/constraints are 38, 375 and 3,750; separated counts are zero. Collision GJK/EPA calls equal those contact counts; GJK average/maximum iterations are three; support calls are 418, 4,125 and 39,958. Active bodies are 50, 500 and 5,000; sleeping bodies remain zero. Collision fingerprints are `5169353994866276594`, `11439712392227242364`, `606644245463582146`; separated fingerprints are `18053003736431092937`, `17122425323131172573`, `16659606550767655509`.

The 10,000-body separated median increased; its five sample values span 2.025200-2.122313 ms before and 2.467713-3.415987 ms after. Host scheduling/load was not controlled. These stock workloads produce initial resting contacts or no contacts, and do not isolate the new positive-TOI loop. Isolated revalidation cost is **Not available**; neither speedup nor general performance equivalence is claimed.

**Measured**, diagnostic arithmetic mean step/solver time across all 1,200 stability steps (no warmup). Changed trajectories prevent an isolated revalidation-cost interpretation:

| State / passes | Stack step / solver ms | Lattice step / solver ms |
| --- | --- | --- |
| Phase 24 / 1 | 3.177209 / 1.780106 | 12.974103 / 9.195942 |
| Phase 25 / 1 | 3.320946 / 1.887170 | 12.688480 / 8.934949 |
| Phase 24 / 8 | 12.913784 / 11.190767 | 70.240260 / 66.098282 |
| Phase 25 / 8 | 13.038590 / 11.314281 | 70.342714 / 66.255832 |

Stack/lattice solver telemetry remains one/eight passes. Full stage times and contact averages are retained in the logs.

## Behavior Changes

Pending ballistic events can move earlier/later or disappear after a velocity change. An unscheduled pending pair can revive after another incident impact. Response uses current geometry rather than the original prediction's normal and anchors. Exact ties follow initial pair order. Removing stale impulses and changing event partitions changes some multi-contact trajectories, as quantified above.

## Known Limitations

- Event discovery remains bounded to initial positive-TOI pairs. An initially missed/non-TOI pair, a pair outside the initial swept broad phase, or a second impact on an already consumed pair is not newly scheduled within the same step. This is not a complete repeated-impact or speculative-contact engine.
- Simultaneous contacts are sequential and deterministic for fixed pair order; a physically unique or creation-order-independent multi-contact solution is not promised. The analytic order-reversal checks cover non-ambiguous fixtures.
- Only events incident to the latest participants are recomputed after each impact. Unrelated predicted times retain the existing numerical integration approximation. Nonlinear rotation can differ between incremental prediction and actual step partitions; current geometry is still revalidated and safe failures are discarded.
- Existing generic query convergence/failure behavior and absolute tolerances remain. A failed current query can miss a real response, but cannot reuse its stale predicted impulse. Sphere revalidation inherits the existing 0.001 proximity tolerance; no scale-policy change is introduced.
- The bounded event loop can have quadratic bookkeeping and repeated narrow-phase cost in dense positive-TOI sets. Current benchmarks do not isolate that cost.
- The default one-pass stack and both lattice configurations retain the residual-motion/penetration limitations above. No sleeping, solver-iteration default change or damping is introduced.
- No ASan/Application Verifier or allocator instrumentation was run; existing lifetime, finite-state and query-purity tests pass in Debug and nominal Release.

## Out-of-Scope Findings

- `PhysicsSystem.cpp:796` retains the existing conservative-advancement iteration limit. The prior rotating offset box/convex query diagnostics still safely miss with last separations 0.000199557/0.000200987. `GJK.cpp:727-783` retains the existing progress/termination policy; convergence work remains outside this phase.
- `PhysicsSystem.cpp:810-811` passes world angular velocity/direction to shape angular-speed estimates. `ShapeBox.cpp:244-246` crosses that velocity with a local point offset. Angular swept bounds/frame correction belongs to Phase 26.
- `PhysicsBody.cpp:334` retains the existing angular displacement deadband and non-reversible integration approximation. Pure prediction and TOI revalidation do not change the integrator.
- Nominal Release `/MTd` remains in `GEngine/GEngine.vcxproj:86` and `PhysicsTests/PhysicsTests.vcxproj:87`.
- `.gitignore:33` has a pre-existing blank line at EOF. The unrelated Scene edit also emits the existing LF-to-CRLF warning during the global diff check. Both remain byte-for-byte unchanged.

## git diff --check

Literal global output:

```text
warning: in the working copy of 'GEngine/src/Scene/_Scene.cpp', LF will be replaced by CRLF the next time Git touches it
.gitignore:33: new blank line at EOF.
```

Global exit **2**, entirely pre-existing. The phase-owned tracked-file check has no output and exit **0**. The new review is checked separately with `git diff --no-index --check NUL docs/physics/PHASE_25_REVIEW.md`: no whitespace diagnostics; exit 1 indicates that a new file differs from NUL.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
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
?? docs/physics/PHASE_25_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/rendering/
```

All **24/24** pre-existing status entries retain their entry existence/hash state, including existing deletions. Only the three Phase 25 files were changed/created as deliverables. Logs/build outputs remain ignored. The index is empty; no commit, tag, push or next-phase work was performed.

## Human Decision

PENDING

Review the bounded pending-event strategy, current-geometry failure safety, and measured default-solver trajectory changes. The execution skill requires stopping at human review.
