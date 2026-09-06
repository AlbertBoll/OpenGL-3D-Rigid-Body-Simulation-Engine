# Phase 24 Review

## Status

PHASE 24 STATUS: AWAITING HUMAN REVIEW

## Objective

Remove temporary mutation and approximate rewind of live rigid bodies from conservative advancement and swept-sphere collision prediction. Repeated queries must preserve live pose, velocities, identity, and derived-cache state exactly.

## Baseline Commit

`f6fbb994c7dfda068bdf8cc76f6586016f300622` (`f6fbb99 physics: phase 23 improve manifold persistence`), branch `physics/refactor`.

The preflight helper verified the preceding approved tag exists and is an ancestor of HEAD, and no approved Phase 24 tag exists. HEAD and the index remain unchanged.

## Previous Approved Tag

`physics-phase-23-approved`, pointing at the baseline commit above.

## Audit Findings Addressed

- **PHYS-BUG-009**: conservative advancement and both swept-sphere entry points advanced and rewound live bodies. Forward/backward nonlinear integration changed physical state and caches; even zero-time queries could populate caches or normalize orientation.

The historical audit remains unchanged. TOI event revalidation, angular swept bounds, and GJK/EPA convergence policy belong to subsequent phases.

## Allowed Scope

One objective at the collision-prediction boundary. One production file, one existing test file, and this review: three files total. No build configuration or solver changes.

## Files Changed

- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_24_REVIEW.md`

## Implementation Summary

- Conservative advancement creates two local `RigidBody3D` value copies, including independent derived-cache storage. It advances these copies using the existing integrator and the existing incremental time sequence. It never integrates or restores the live bodies.
- The internal `IntersectAtQueryState` evaluates local query bodies. The public instantaneous query also creates local copies because world/local-anchor conversion can populate lazy caches without advancing time.
- After every internal query, including false returns, contacts are rebound to the original body pointers. Local copies are never registered in a world or retained in a contact/manifold.
- Swept spheres compute impact-space anchors and normals on local copies. Initial separation uses the unchanged input positions. The dispatch overload delegates to the dedicated sphere implementation so both public entry points share the correction.
- Shape objects remain shared and are queried through their existing read-only operations. Body-copy lifetime is bounded to the call. Existing body-type, integration, normal, feature-reset, finite-state and collision-failure behavior is retained.

Only `PhysicsSystem.cpp` needs production changes: it contains all affected query entry points. The existing body is value-copyable and has independent value-stored caches, so no prediction API or additional helper file is necessary.

## Tests Added / Modified

Added `--pure-prediction`, with the same **341 checks** included in the full suite.

- Sphere, offset asymmetric box, and convex queries with cold, warm, and stale pose/mass caches; initial overlap, future approach, separating miss, and short-horizon miss.
- Public instantaneous query, swept dispatch, dedicated swept spheres, and direct conservative advancement.
- Thirty-two repetitions after each initial query compare all returned contact fields exactly and retain original body pointers and cleared feature keys.
- Byte snapshots compare the same live objects before/after calls, including private caches, flags, revisions and identity. The test neither compares padding between separate body copies nor writes snapshot bytes into a body.
- An observing box support map checks both live snapshots during query evaluation. This catches temporary live mutation even if a later implementation restores the state. The short-horizon miss confirms that a predicted pose was actually advanced before returning.
- Independently computed sphere impact time and anchor transforms cover spinning Static, Kinematic, and Dynamic participants and reversed input order.
- Successful rotated box/convex sweeps check analytic TOI, B-to-A normal, original body ownership, and impact-anchor reconstruction including the shape COM.
- One thousand reordered query pairs sharing a spinning asymmetric world-owned body verify exact output and state preservation. A zero-horizon conservative query verifies the early-return body-reference contract.

The preliminary red control ran against unchanged Phase 23 production code and failed 118/341 checks. Its failures included 68 live-state preservation checks, 30 output-repeatability checks, the repeated-order and zero-horizon checks, and six observations of advancement while the live body itself was being moved. Twelve additional failures were overly strong fixture assumptions that a rotating generic approach must return a hit; these were removed from acceptance before final validation. The existing generic convergence policy can miss those approaches. Their hit/miss result is printed as a diagnostic, while their purity, repeatability, finite output and pointer ownership remain gated. Separate successful generic sweeps provide positive-TOI acceptance coverage. The final suite also adds 12 independent generic-sweep/anchor checks. No previous acceptance test was relaxed and no new expected failure was introduced.

Tolerances: live body bytes and repeated contact fields are exact; sphere TOI and initial separation use `2e-6`; reconstructed world anchors use `2e-5`; generic TOI and normal use `2e-3`, consistent with the existing collision tests. All previous mathematical, contact, face-stability, lifetime, and body-type gates remain enabled.

## Validation Commands

Validation uses Class B purity/mathematical checks plus supplemental Class C/D stability and performance comparisons. The current generated Visual Studio x64 solution and profiling-enabled engine/benchmark projects were inspected. Process-local Path normalization handles the pre-existing duplicate Path/PATH environment issue. The sandbox launcher and patch helper failed before execution, so scoped file edits and validation used reviewed elevated execution. No machine-wide environment or build settings changed.

```powershell
$phase24Path = $env:Path
Remove-Item Env:Path
$env:Path = $phase24Path
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Debug /p:Platform=x64 /clp:ErrorsOnly
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Release /p:Platform=x64 /clp:ErrorsOnly
.\bin\Debug\PhysicsTests\PhysicsTests.exe --pure-prediction
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --pure-prediction
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=1
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=8
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5 --steady-state-warmup-steps=4 --steady-state-measured-steps=8
git diff --check
git diff --check -- GEngine/include/GEngine/Physics/PhysicsSystem.cpp PhysicsTests/src/main.cpp
git diff --no-index --check NUL docs/physics/PHASE_24_REVIEW.md
git status --short
```

The baseline Release build and full suite ran before production edits, followed by both stability and benchmark commands with the same options. Final builds used normal-verbosity file logs. Every required native exit code was checked; all final builds, tests, and probes returned 0. The intentionally red preliminary control returned 1.

Ignored evidence: `bin/phase24-before-*`, `bin/phase24-red-*`, `bin/phase24-final-{debug,release}-{build,focused,tests}.log`, `bin/phase24-after-stability-{1,8}.log`, `bin/phase24-after-{collision,separated}.log`, and `bin/phase24-user-hashes.log`. Extended before/after probes were run in nominal Release; Debug runs the full suite including the existing focused stability gates.

## Debug Result

x64 engine/tests/benchmark build: **pass**, exit 0. Focused **341/341** and full **3,124/3,124**, both exit 0. No previous gates failed. Existing compiler/vendor warnings remain; no build error.

## Release Result

x64 engine/tests/benchmark build: **pass**, exit 0. Focused **341/341** and full **3,124/3,124**, both exit 0. Baseline full suite: **2,783/2,783**. All extended probes and benchmarks return 0.

This is the nominal optimized configuration with the existing **Debug static CRT `/MTd` override**, not a true Release-CRT allocator/runtime comparison.

## Stability Results

**Measured**, unchanged headless runner: fixed `1/120 s`, 1,200 steps / 10 seconds, gravity `(0,-12,0)`, fixed geometry/materials/body order, one and eight solver passes, no randomness and no discarded warmup. Moving means linear or angular speed greater than 0.05. Penetration below is the floor-plane intrusion of world bounds, not inter-body depth. All eight final scenario/pass combinations remain finite.

Stack initial average height is 4.5 and initial energy is 864. Every before/after stack run peaks at **100%** of initial mechanical energy.

| State / passes | Final energy | Final average Y | Peak linear / angular speed | Final linear / angular speed | Moving bodies | Final manifolds / points | Peak / final floor intrusion |
| --- | ---: | ---: | --- | --- | ---: | --- | --- |
| Phase 23 / 1 | 860.240186 | 4.480385 | 0.577986 / 0.232504 | 0.087014 / 0.016677 | 2 | 17 / 65 | 0.040058 / 0.018786 |
| Phase 24 / 1 | 860.585892 | 4.481866 | 0.565015 / 0.205754 | 0.169505 / 0.068155 | 8 | 18 / 70 | 0.043898 / 0.018244 |
| Phase 23 / 8 | 863.821763 | 4.499072 | 0.065288 / 0.058103 | 0 / 0 | 0 | 16 / 64 | 0.000983 / 0.000891 |
| Phase 24 / 8 | 863.821763 | 4.499072 | 0.065288 / 0.058103 | 0 / 0 | 0 | 16 / 64 | 0.000983 / 0.000891 |

**Human inspection:** default one-pass residual motion and peak floor intrusion are higher, despite slightly better final height, final intrusion and lower peak speeds. This is not a general settling improvement. Removing query-induced state changes alters the later contact trajectory. Every non-timing eight-pass stack field is unchanged at printed precision, and all existing face-stability acceptance tests pass in both builds. No solver tuning or damping was added to hide the one-pass result.

Sphere lattice: initial energy 30,240; peak energy remains **100%** in every run. All 180 bodies remain moving after 10 seconds; sleeping is not implemented.

| State / passes | Final energy | Final average Y | Peak linear / angular speed | Final linear / angular speed | Final manifolds / points | Peak / final floor intrusion |
| --- | ---: | ---: | --- | --- | --- | --- |
| Phase 23 / 1 | 4298.488235 | 1.490150 | 14.400019 / 6.702827 | 6.350901 / 6.356113 | 187 / 404 | 0.181713 / 0.108359 |
| Phase 24 / 1 | 4284.630649 | 1.489803 | 14.400019 / 6.416298 | 6.058355 / 6.065278 | 183 / 380 | 0.180509 / 0.135938 |
| Phase 23 / 8 | 4159.930548 | 1.492512 | 14.300018 / 6.439843 | 5.810773 / 5.832931 | 181 / 371 | 0.192117 / 0.062374 |
| Phase 24 / 8 | 4180.014077 | 1.492569 | 14.300018 / 6.665102 | 5.646073 / 5.711784 | 182 / 373 | 0.192301 / 0.042836 |

Single-sphere and free-asymmetric-body non-timing results match the baseline in both pass configurations. The sphere ends at Y=1.499874, linear/angular speed 0.001068/0.001066, zero moving bodies, one manifold/point, intrusion 0.000126, and peak energy 100%. The unchanged free-body integrator reaches energy 5.432790 from 5.211667 (+4.242841%) and angular-momentum magnitude +2.635889% at this probe timestep. Existing Phase 13 mathematical tolerance gates pass; Phase 24 does not change the integrator.

## Benchmark Results

**Measured**, nominal Release, profiling enabled, default one solver pass, identical workload/options before and after. Each body count uses two fresh-world warmup samples and five measured samples; report median external whole-step time. Collision samples measure one step. Separated samples use four per-world warmup steps followed by eight measured steps; each sample reports mean step time, and the table reports the median of those five values.

| Workload | Bodies | Before median ms | After median ms |
| --- | ---: | ---: | ---: |
| Collision | 100 | 4.123200 | 4.164500 |
| Collision | 1,000 | 47.570500 | 49.449100 |
| Collision | 10,000 | 510.900400 | 459.109200 |
| Separated | 100 | 0.018850 | 0.018638 |
| Separated | 1,000 | 0.194000 | 0.187238 |
| Separated | 10,000 | 3.656213 | 2.380550 |

All non-timing CSV fields, including final-state fingerprints, match exactly across all six comparisons. Collision candidates/contacts/manifolds/points/constraints are 38, 375 and 3,750; separated counts are zero. Collision GJK calls and EPA calls equal those contact counts, with three average/maximum GJK iterations; support calls are 418, 4,125 and 39,958. Active bodies remain 50, 500 and 5,000; sleeping bodies remain zero. Fingerprints are respectively `5169353994866276594`, `11439712392227242364`, `606644245463582146` for collision and `18053003736431092937`, `17122425323131172573`, `16659606550767655509` for separated.

No speedup or isolated copy-cost claim is made. Host scheduling/load was not controlled, the after 10,000-body collision samples span 457.141300-580.155100 ms, and these stock workloads do not isolate rotating positive-TOI queries. Allocation/copy micro-cost is **Not available**. The purpose of this phase is correctness.

**Measured**, diagnostic arithmetic means over all 1,200 stability steps; these are not warmed benchmark medians, and changed trajectories prevent a causal performance comparison:

| State / passes | Stack step / solver ms | Lattice step / solver ms |
| --- | --- | --- |
| Phase 23 / 1 | 3.180548 / 1.778708 | 14.352826 / 10.216798 |
| Phase 24 / 1 | 3.226114 / 1.840183 | 12.833483 / 9.180348 |
| Phase 23 / 8 | 13.478040 / 11.761854 | 73.420790 / 69.204570 |
| Phase 24 / 8 | 13.210153 / 11.509429 | 70.423565 / 66.363113 |

Solver telemetry remains exactly one/eight passes for stack and lattice. Full stage timings, contact averages and sample ranges remain in the ignored logs.

## Behavior Changes

Prediction no longer normalizes, advances, rewinds or warms caches on live bodies. Query results are independent of preceding read-only queries. Contacts retain their original body references and existing B-to-A normal/COM-local anchor convention. General trajectories can change because the former query-induced physical-state drift is removed; the measured one-pass stack and sphere lattice demonstrate this.

## Known Limitations

- Positive-TOI event ordering and precomputed contact response remain unchanged. Pure prediction does not solve stale TOI response.
- Prediction preserves the existing sequence of incremental integrator calls. Nonlinear rotation over several CA increments can differ from one integration call over the total TOI; no new integration/event-time policy is introduced.
- Existing convergence limits, angular-speed estimate coordinates, tolerances and safe missed-contact behavior are retained. False-return contact fields retain their previous partial-witness contract and are not validated collision output.
- Local bodies share shape pointers; geometry must remain stable during a query. The existing global profiler still changes counters and is not thread-safe. This phase establishes body/cache purity, not parallel execution.
- Future ownership-bearing members added to `RigidBody3D` would require revisiting value-copy prediction. Current members are values and non-owning shape references.
- No ASan/Application Verifier or allocator profiler was run. Debug and Release checks cover snapshot preservation, pointer ownership, existing lifetime regressions and finite output.
- Default single-pass stack residual motion and lattice settling remain limitations, as quantified above.

## Out-of-Scope Findings

- The rotating offset-box/wall approach in the new fixture returns false with last separation 0.000199557; the analogous convex case reports 0.000200987. The same misses occur before/after the fix. `PhysicsSystem.cpp:763` retains the existing `numIters > 10` CA limit; `GJK.cpp:717-786` retains its current progress/separation policy. No new convergence acceptance policy is introduced. The diagnostic is distinct from the successful generic TOI regression.
- `PhysicsSystem.cpp:403-430` still integrates and resolves a precomputed event list; Phase 25 owns revalidation.
- `PhysicsSystem.cpp:777-778` still passes world angular velocity/direction to existing shape angular-speed estimates; `ShapeBox.cpp:244` and the convex equivalent retain their local-offset calculation. Phase 26 owns that coordinate/sweep correction.
- `PhysicsBody.cpp:334` retains its existing angular-displacement deadband, and its integrator remains numerically non-reversible. It is no longer used to rewind live bodies in these collision queries.
- Release `/MTd` is still present in `GEngine/GEngine.vcxproj:86` and `PhysicsTests/PhysicsTests.vcxproj:87`.
- `.gitignore:33` has a pre-existing extra blank line at EOF. Its initial hash remains unchanged.

## git diff --check

Literal global result:

```text
.gitignore:33: new blank line at EOF.
```

Global exit **2**, identical to preflight and entirely pre-existing. The Phase 24 tracked-file check has no output and exits **0**. The new review is separately checked with `git diff --no-index --check NUL docs/physics/PHASE_24_REVIEW.md`; it produced no whitespace diagnostics and exit 1 because a new file differs from NUL. Unrelated user work is preserved rather than absorbed into the phase.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_24_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

All **19/19** pre-existing modified/untracked file hashes match the entry snapshot. Only the three Phase 24 files above were changed/created as deliverables; generated logs/build artifacts remain ignored. The index is empty. No commit, tag, push or next-phase work was performed.

## Human Decision

PENDING

Review the local-copy boundary and contact-pointer rebinding, the unchanged collision-convergence limitations, and the default single-pass residual-motion tradeoff. The execution skill requires stopping at human review.
