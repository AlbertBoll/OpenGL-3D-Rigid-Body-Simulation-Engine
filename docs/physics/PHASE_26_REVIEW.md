# Phase 26 Review

## Status

PHASE 26 STATUS: AWAITING HUMAN REVIEW

## Objective

Make broad-phase swept bounds conservative for rotating bodies and correct the local/world frame mismatch in the conservative-advancement angular-speed estimate. A long box rotating into an obstacle must reach collision response rather than be rejected by its initial linear swept bounds.

## Baseline Commit

`0aa5f5f6ee0d21cda7c7d766efab2f0c2eead60b` (`0aa5f5f physics: phase 25 revalidate pending TOI events`), branch `physics/refactor`.

The execution preflight verified the preceding approved tag exists and is an ancestor of HEAD, and no approved Phase 26 tag exists. HEAD and the empty index remain unchanged.

## Previous Approved Tag

`physics-phase-25-approved`, pointing at the baseline commit above.

## Audit Findings Addressed

**PHYS-BUG-011**: broad phase previously expanded only the initial world AABB by linear displacement. Rotating elongated geometry could therefore be rejected before narrow phase. Conservative advancement also passed world angular velocity/direction to box/convex methods that cross those values with local COM-relative points.

The historical audit remains unchanged. Previously approved query purity, TOI revalidation, body-type semantics, and failure guards remain the current implementation baseline.

## Allowed Scope

One tightly coupled angular-sweep correctness boundary: three production files, one existing test file and this review, five files total.

The plan lists `ShapeBox.cpp`/`ShapeConvex.cpp` as expected targets. Their existing two-argument angular-speed functions have no body orientation and correctly evaluate local vectors. The correction belongs at their only production caller, where each predicted body's rotation is available. `Shape.h` documents that existing local-frame contract. No shape implementation or signature change is necessary.

## Files Changed

- `GEngine/include/GEngine/Physics/Broadphase.cpp`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `GEngine/include/GEngine/Physics/Shape.h`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_26_REVIEW.md`

## Implementation Summary

- Retain persistent one-axis SAP, its filters, tie ordering, storage reuse, linear sweep and 0.01 margin.
- For nonzero effective spin, nonzero duration, and valid nonspherical geometry, enclose the entire possible rotation around the shape COM. The radius comes from the farthest corner of the shape's local AABB relative to its local COM. Sweep that COM-radius envelope along the existing linear displacement and union it with the original bounds.
- Compute radius and envelope endpoints in double precision; convert outward to finite float endpoints. This avoids narrowing the new enclosure through float rounding. No new persistent cache or allocation is added.
- Use effective angular velocity, so Static and non-integrating zero-mass Dynamic bodies do not receive angular expansion. Kinematic spin is included. Sphere geometry is rotation-invariant and keeps its existing linear bounds.
- Transform both angular velocity and the separation direction through each predicted body's own world-to-body rotation before calling `FastestLinearSpeed`. Body B uses the opposite direction. These are query-local bodies, preserving Phase 24 purity.
- Document the shape API's model-space angular velocity and unit-direction contract in `Shape.h`. Existing shape speed equations, finite guards, GJK/EPA policy, contact response, and integration equations are unchanged.

For local AABB endpoints `l`, `u` and local COM `c`, the construction is:

```text
e[i] = max(abs(l[i] - c[i]), abs(u[i] - c[i]))
r    = sqrt(e dot e)
C1   = C0 + v * dt
angularMin[i] = min(C0[i], C1[i]) - r
angularMax[i] = max(C0[i], C1[i]) + r
```

Every local point is at most `r` from COM; rigid rotation preserves that distance. The COM segment swept with this radius therefore contains intermediate poses, including a full turn whose endpoints have the same orientation. The full radius also avoids assuming that initial angular speed/axis remains constant under the existing gyroscopic integrator. The deliberate tradeoff is a loose bound even for small nonzero spin.

The frame correction follows rotation invariance of the scalar triple product:

```text
d_world dot (omega_world cross (R * r_local))
= (R^T * d_world) dot ((R^T * omega_world) cross r_local)
```

## Tests Added / Modified

Added `--angular-sweep`; all **191 checks** also run in the full suite.

- Forty-eight containment cases span box/convex shapes, offset local COM, scales 0.25/1/8, Dynamic/Kinematic bodies, initial rotations, principal spin and non-principal gyroscopic motion. Each samples 65 times with both direct and partitioned integration, covering 6,240 pose-bound comparisons. Bounds must contain sampled AABBs without an added test tolerance, and all sampled states/bounds must remain finite.
- The same cases require exact preservation of live pose/velocities and exactly repeated bounds without a SAP membership rebuild.
- A full-turn long rod misses the obstacle at both endpoint AABBs but must retain the intermediate collision candidate. Guard tests cover zero duration, Static stored spin, non-integrating Dynamic stored spin, Kinematic rotation, reciprocal masks, rotation-invariant spheres and zero spin.
- Observing box/convex speed queries check both CA participants against independently transformed world-space vertex velocities and conjugate-quaternion local inputs, across three orientations and distinct body rotations.
- A long rotating box must produce a finite positive TOI and reach system collision response with changed angular/linear velocity, in both creation orders and two common orientations.

The identical focused suite ran first against unchanged Phase 25 production code: **80/191 checks failed**. Failures covered missing angular containment/candidates, incorrect angular frames and missed system response. The rotated long-box query also failed before the frame correction. No acceptance test was relaxed or marked expected-failure.

Both final configurations report the same long-box TOI, approximately **0.0485418 s**, in all four order/orientation cases. Before correction, the unrotated direct query returned approximately 0.0494662 s while the rotated query missed; the system response was rejected by broad phase.

Tolerances: swept containment uses direct inclusive bounds comparison with the existing production margin; repeated bounds and live physical state are exact; independently calculated angular speed uses `3e-5`, and transformed vectors use `3e-6`. The long-box response requires a finite hit within 0.06 s and a material decrease from its initial 30 rad/s spin. No collision tolerance, damping or solver constant changed. All previous acceptance tests remain registered.

## Validation Commands

Class B mathematical correctness plus Class C stability probes and supplemental Class D measurements. The current generated Visual Studio x64 solution, profiling and compiler flags were inspected. Process-local Path normalization handles the existing duplicate Path/PATH environment issue. The sandbox launcher intermittently failed before execution; scoped edits/builds used reviewed elevated execution. No machine environment or project configuration changed.

```powershell
& .agents/skills/physics-phase-execution/scripts/Get-PhysicsPhasePreflight.ps1 -RepositoryRoot (Get-Location).Path -Phase 26
$phase26Path = $env:Path
Remove-Item Env:Path
$env:Path = $phase26Path
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Debug /p:Platform=x64 /clp:ErrorsOnly
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Release /p:Platform=x64 /clp:ErrorsOnly
.\bin\Debug\PhysicsTests\PhysicsTests.exe --angular-sweep
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --angular-sweep
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=1
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=8
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5 --steady-state-warmup-steps=4 --steady-state-measured-steps=8
git diff --check
git diff --check -- GEngine/include/GEngine/Physics/Broadphase.cpp GEngine/include/GEngine/Physics/PhysicsSystem.cpp GEngine/include/GEngine/Physics/Shape.h PhysicsTests/src/main.cpp
git diff --no-index --check NUL docs/physics/PHASE_26_REVIEW.md
git status --short
```

Baseline Release build/full suite (3,218 checks), both stability probes and both benchmarks preceded production edits. Final correctness passed before after-change measurements; probes/benchmarks ran serially without a concurrent build/test run. Every final native build/test/probe exit is 0; the intentional red control exited 1.

Ignored evidence: `bin/phase26-entry-{status,hashes}.log`, `bin/phase26-before-*`, `bin/phase26-red-*`, `bin/phase26-first-release-build.log`, `bin/phase26-first-focused.log`, `bin/phase26-first-release-tests.log`, `bin/phase26-final-debug-{build,focused,tests}.log`, `bin/phase26-after-stability-{1,8}.log`, and `bin/phase26-after-{collision,separated}.log`. Builds retained normal-verbosity file logs. Extended probes use nominal Release; Debug runs the full suite including focused stability gates.

## Debug Result

x64 engine/tests/benchmark build: **pass**, exit 0. Focused **191/191** and full **3,409/3,409**, both exit 0. The full suite reports zero observed known issues in its one existing non-gating diagnostic.

## Release Result

Nominal optimized x64 engine/tests/benchmark build: **pass**, exit 0. Focused **191/191** and full **3,409/3,409**, both exit 0. The compiler still overrides `/MT` with `/MTd`; allocator/runtime behavior is not a true Release-CRT result. Existing vendor/linker warnings remain.

## Stability Results

**Measured**, headless fixed geometry/materials/body order/gravity, dt `1/120`, 1,200 steps (10 seconds), separate one/eight solver-pass runs. Every sampled scenario remains finite. Stack, single sphere and lattice peak mechanical energy stays at 100% of initial energy in both revisions. These are measured comparisons, not new acceptance tolerances.

| Stack state / passes | Final energy | Final average Y | Peak linear / angular speed | Final linear / angular speed | Moving bodies | Final manifolds / points | Peak / final floor penetration |
| --- | ---: | ---: | --- | --- | ---: | --- | --- |
| Phase 25 / 1 | 859.501459 | 4.476312 | 0.605936 / 0.221727 | 0.219385 / 0.043763 | 5 | 25 / 78 | 0.044216 / 0.020010 |
| Phase 26 / 1 | 860.015904 | 4.478818 | 0.703708 / 0.245699 | 0.322788 / 0.058755 | 3 | 19 / 72 | 0.047577 / 0.008523 |
| Phase 25 / 8 | 863.821763 | 4.499072 | 0.065288 / 0.058103 | 0.000000 / 0.000000 | 0 | 16 / 64 | 0.000983 / 0.000891 |
| Phase 26 / 8 | 863.821763 | 4.499072 | 0.065288 / 0.058103 | 0.000000 / 0.000000 | 0 | 16 / 64 | 0.000983 / 0.000891 |

The eight-pass stack's non-timing fields match exactly and it settles. The default one-pass stack retains three moving bodies; peak/residual speed and peak penetration increase, while final penetration decreases and average height increases slightly. Its changed trajectory remains an explicit review tradeoff; no solver tuning was added.

All non-timing fields for the free asymmetric body, single sphere and sphere lattice match the baseline in both pass configurations:

| Lattice passes | Final energy | Final average Y | Peak linear / angular speed | Final linear / angular speed | Moving bodies | Final manifolds / points | Peak / final floor penetration |
| --- | ---: | ---: | --- | --- | ---: | --- | --- |
| 1 | 4278.979598 | 1.490630 | 14.400019 / 6.611325 | 6.408699 / 6.454880 | 180 | 182 / 371 | 0.198102 / 0.078580 |
| 8 | 4168.224381 | 1.492660 | 14.300018 / 6.217107 | 5.687394 / 5.832269 | 180 | 188 / 375 | 0.181385 / 0.020635 |

The moving threshold is 0.05 linear/angular speed. All 180 lattice bodies remain moving; sleeping remains absent. The single sphere ends at Y=1.499874, linear/angular speed 0.001068/0.001066, zero moving bodies, one manifold/point, and penetration 0.000126. The unchanged free-body integrator reaches energy 5.432790 from 5.211667 (+4.242841%) and angular-momentum magnitude +2.635889% at this timestep. Existing angular mathematical gates pass.

## Benchmark Results

**Measured**, nominal Release, profiling enabled, one solver pass, identical workload/options before/after. Each body count uses two fresh-world warmup samples and five measured samples. Collision samples measure one step. Separated samples use four per-world warmup steps followed by eight measured steps; their table entries are medians of per-sample mean step times.

| Workload | Bodies | Before median ms | After median ms |
| --- | ---: | ---: | ---: |
| Collision | 100 | 4.344600 | 4.375100 |
| Collision | 1000 | 46.768300 | 47.842300 |
| Collision | 10000 | 503.907000 | 531.028800 |
| Separated | 100 | 0.019100 | 0.025837 |
| Separated | 1000 | 0.192450 | 0.208462 |
| Separated | 10000 | 3.964775 | 3.230062 |

Every non-timing CSV field, including final-state fingerprints, matches exactly for all six comparisons. Collision candidates/contacts/manifolds/points/constraints are 38, 375 and 3,750; separated counts are zero. Collision GJK/EPA calls equal those contact counts, with three average/maximum GJK iterations; support calls are 418, 4,125 and 39,958. Active bodies are 50, 500 and 5,000; sleeping bodies remain zero. Collision fingerprints are `5169353994866276594`, `11439712392227242364`, `606644245463582146`; separated fingerprints are `18053003736431092937`, `17122425323131172573`, `16659606550767655509`.

The 10,000-body collision median increased from 503.907000 to 531.028800 ms (after samples 528.783300-535.552900 ms); the separated median decreased from 3.964775 to 3.230062 ms (after samples 2.852000-3.782150 ms). Host scheduling/load was not controlled. These stock workloads do not isolate rotating elongated-body candidate inflation, so no general speedup or performance-equivalence claim is made.

**Measured**, diagnostic arithmetic mean step/solver time over all 1,200 stability steps, without warmup:

| State / passes | Stack step / solver ms | Lattice step / solver ms |
| --- | --- | --- |
| Phase 25 / 1 | 3.588823 / 2.029142 | 14.034385 / 9.685794 |
| Phase 26 / 1 | 3.704837 / 2.025702 | 15.585476 / 11.114260 |
| Phase 25 / 8 | 13.468185 / 11.647058 | 73.992316 / 69.558755 |
| Phase 26 / 8 | 14.617473 / 12.660458 | 79.982771 / 75.356454 |

These means include host variation and, for the one-pass stack, changed trajectories. Isolated angular-bound cost and extra candidate counts in the stability probes are **Not available** from the existing CSV. **Derived:** envelope construction adds bounded constant work per rotating nonspherical body, while enlarged intervals can increase SAP overlap scans and narrow-phase candidates. Dense elongated-body workloads can therefore cost substantially more; this phase makes no SAP replacement or optimization claim.

## Behavior Changes

Rotating boxes/convexes can now enter narrow phase despite disjoint initial/endpoint AABBs. Conservative advancement uses the correct angular frame at each predicted pose, so some TOIs/trajectories change. Translation-only and sphere-rotation bounds retain the previous path. Static/static filtering, reciprocal masks, persistent SAP ordering/storage and zero-time behavior remain intact.

## Known Limitations

- Full COM-radius envelopes are intentionally loose, including for tiny nonzero angular velocity. They can admit many extra false-positive candidates for elongated rotating bodies. A tighter bound would need a validated angular-motion bound compatible with gyroscopic and partitioned integration.
- Broad-phase sweeps describe the motion available when candidates are generated. A later impulse can create a new trajectory/pair outside that initial sweep. Phase 25 still schedules only its initial positive-TOI pairs and consumes each once; no new event-discovery engine is added.
- Conservative advancement's existing iteration/progress and instantaneous directional-speed policy remain. Correct coordinate frames and conservative broad phase do not establish complete CCD for all rotating convex trajectories. GJK/EPA failure safety is preserved, including safe misses.
- Numerical containment was exercised at scales 0.25/1/8 with finite poses. Outward double-to-float enclosure avoids new inward rounding; arbitrary extreme-coordinate/overflow robustness of the existing bounds, shape and integrator pipeline is not claimed.
- Broad phase may populate existing body-derived caches while reading bounds/COM, as before; it does not modify physical pose or velocity. Collision prediction still uses local copies and passes all prior purity checks.
- Default one-pass stack and lattice settling limitations remain as quantified above. No sleeping, damping, solver-default change or integrator correction is included.
- No ASan/Application Verifier or allocation profiler was run. Debug/Release finite-state, lifetime and query-purity regressions remain enabled and pass.

## Out-of-Scope Findings

- The prior rotating offset box/convex diagnostics still safely miss, now with last separations 0.000571013/0.000586987. `PhysicsSystem.cpp:796` retains the existing CA iteration limit, and `GJK.cpp:727-783` retains its progress/termination policy. These differ from the successful new long-box fixture. Convergence/failure-policy work belongs to subsequent phases.
- `Broadphase.cpp:123` still uses stored linear velocity for the pre-existing linear sweep. A Static body with stored linear velocity can produce unnecessary candidates; effective angular velocity is used by the new angular path. This existing linear overestimate is conservative and unchanged.
- `PhysicsBody.cpp:334` retains its angular-displacement deadband and non-reversible numerical integration. The full rotation envelope avoids relying on an initial angular-speed bound but does not change those equations.
- `/MTd` remains in the nominal Release configurations at `GEngine/GEngine.vcxproj:86`, `PhysicsTests/PhysicsTests.vcxproj:87`, and `PhysicsBenchmark/PhysicsBenchmark.vcxproj:87`.
- `.gitignore:33` has the pre-existing blank line at EOF; the unrelated Scene file emits the existing LF-to-CRLF warning. Both remain byte-for-byte unchanged.

## git diff --check

Literal global output:

```text
warning: in the working copy of 'GEngine/src/Scene/_Scene.cpp', LF will be replaced by CRLF the next time Git touches it
.gitignore:33: new blank line at EOF.
```

Global exit **2**, entirely pre-existing. The four phase-owned tracked-file check has no output and exit **0**. The new review is separately checked with `git diff --no-index --check NUL docs/physics/PHASE_26_REVIEW.md`: no whitespace diagnostics; exit 1 means a new file differs from NUL.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Broadphase.cpp
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M GEngine/include/GEngine/Physics/Shape.h
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
?? docs/physics/PHASE_26_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/rendering/
```

All **24/24** pre-existing entries retain their entry existence/hash state, including existing deletions. Only the five Phase 26 files were changed/created as deliverables. Generated logs/build outputs remain ignored. The index is empty; no commit, tag, push or next-phase work was performed.

## Human Decision

PENDING

Review the conservative full-radius tradeoff, the frame correction at the caller, the successful long-box collision, and the measured one-pass stack changes. The execution skill requires stopping at human review.
