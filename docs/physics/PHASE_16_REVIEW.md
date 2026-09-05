# Phase 16 Review

## Status

PHASE 16 STATUS: AWAITING HUMAN REVIEW

## Objective

Replace the resting-contact friction clamp with a two-dimensional Coulomb disk bounded by the accumulated normal impulse. Address only resting friction and its cached warm-start application.

## Baseline Commit

`6f44111e4875dc8c76a135996a061babc7f1ffcd`

`physics: phase 15 enforce canonical contact orientation`

Branch: `physics/refactor`. The phase preflight verified the preceding approved tag is an ancestor of HEAD and no approved Phase 16 tag exists. The initial index was empty. Unrelated pre-existing files were preserved; no nested AGENTS.md was found.

## Previous Approved Tag

`physics-phase-15-approved`, resolving to the baseline commit above.

## Audit Findings Addressed

`PHYS-BUG-007`: the resting solver used a gravity-like friction floor, an incremental normal lambda, and separate tangent-axis clamps. This allowed unsupported friction and a square impulse region instead of a Coulomb disk.

The audit remains a historical baseline. Phase 15's approved B -> A contact convention and conversion to the resting solver's A -> B axis are unchanged.

## Allowed Scope

One production file, one dedicated test file, and this review: three files total.

The header and object layout remain unchanged. Collision dispatch, ballistic friction, restitution, normal/contact conventions, solver traversal count, stabilization, manifold feature/persistence policies, geometry, and build configuration are outside this phase.

## Files Changed

- `GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_16_REVIEW.md`

## Implementation Summary

A file-local helper projects the accumulated impulse into the valid resting-contact region:

```text
lambda_n = max(0, candidate_lambda_n)
limit = mu * lambda_n
if limit == 0:
    lambda_t = (0, 0)
else if length(candidate_lambda_t) > limit:
    lambda_t = candidate_lambda_t * limit / length(candidate_lambda_t)

applied_delta = projected_accumulated_lambda - previous_accumulated_lambda
```

The helper holds the normal impulse fixed after its existing nonnegative clamp and projects both tangent components together. Consequently:

- no normal support permits no accumulated tangential impulse;
- diagonal friction obeys the same magnitude bound as axis-aligned friction;
- a zero incremental normal impulse does not erase friction supported by accumulated normal lambda;
- reduced support retracts excess friction through the existing impulse-delta application.

The hard-coded `10 / inverseMassSum` floor and the independent tangent clamps are removed. The existing Jacobian, dense effective mass, three inner Gauss-Seidel iterations, normal/bias RHS, and physical impulse application remain.

PreSolve also projects the cached impulse after computing the current material coefficient, before applying the warm start. A reduced coefficient shrinks the cached tangents; zero friction clears them. A negative cached normal is clamped to zero before it can warm start. Existing non-finite-cache guards remain; non-finite accumulated candidates are cleared safely.

Finite positive material coefficients combine by multiplication in **double**, with both float operands converted before multiplication. The Coulomb limit is also calculated in double as `mu * double(lambda_n)`. The largest finite float coefficient pair and any finite float normal impulse fit within double's exponent range. Large finite coefficients therefore remain valid, including `1e30f * 1e30f` and `FLT_MAX * FLT_MAX`, without an arbitrary cap or a float narrowing of the product.

To preserve the existing file scope and object layout, the existing float `m_Friction` member records only whether tangent rows were enabled in PreSolve (0 or 1). A file-local helper computes the double coefficient for warm-start projection and again for each Solve projection when those rows are enabled. The flag is never used as the Coulomb coefficient or limit. Body materials remain unchanged during the normal PreSolve/Solve sequence.

Zero or negative coefficients still disable resting friction. Existing finite-input guards remain, and raw NaN/Inf body fields are still rejected by the unchanged Debug finite-state assertions. This revision does not change body-field invariants.

The projection helper also uses double-precision hypotenuse intermediates so large finite float cached tangents do not overflow a squared-length calculation. Stored lambdas remain floats. The accumulated disk, normal clamp, warm-start/retraction behavior, Jacobians, iteration counts, and stabilization are otherwise unchanged from the initial Phase 16 implementation.

## Tests Added / Modified

Added `PhysicsTests.exe --resting-friction` with **192 gating checks**, all included in the full suite. The preceding 366 checks remain unchanged; the full suite now contains **558** checks.

### Analytic impulse tests

A zero-lever-arm dynamic/static constraint fixture isolates the normal and tangent equations. It uses inverse masses 0.5 and 2, friction coefficients 0, 0.25, and 2, closing speeds 0, 0.02, and 40, and axis-aligned/diagonal sliding plus low-speed sticking. The dynamic body frame is rotated around the contact normal.

The tests compare normal impulse, total tangent magnitude, linear response, zero angular response, and unchanged support velocity against analytic values. Three additional solve calls verify that friction continues to use accumulated support when the incremental normal impulse is zero.

### Warm starting and retraction

- Acquire a real saturated friction impulse, then reduce/zero the material coefficient.
- Check the cached vector and the actual warm-started body velocity.
- Remove normal support and require both normal and tangent impulses to retract using their deltas.
- Partially reduce support from normal lambda 40 to 30; the tangent limit must shrink from 20 to 15 and return precisely the excess impulse.
- Keep zero/negative coefficients frictionless, while preserving supported cached friction for large finite products involving `1e30f` and `FLT_MAX`.
- Project `1e30` cached tangents without norm overflow.
- Prevent a negative cached normal and unsupported tangents from warm starting.

### Large finite material coefficients

New focused regressions use pairs of `1e30f` and pairs of `FLT_MAX`:

- A supported body with velocity `(6, -2, 8)` must stop sliding with normal impulse 2 and tangential impulse magnitude 10, remaining finite.
- A cached normal impulse of `1e-30f` must retain tangents `(1e20f, -1e20f)` exactly. Their magnitude is below the true double Coulomb limit; capping the coefficient to float range would incorrectly shrink them.
- The warm-start/retraction cases also include a `FLT_MAX` coefficient and check that large finite coefficients enable tangent rows, retain supported friction, and still clear friction when normal support disappears.

### Deterministic sliding/sticking tests

An analytic unit-radius sphere of mass 2 contacts a static plane for two seconds at 120 Hz and 240 Hz, with gravity magnitude 10, mu 0.25, fixed contact geometry, and one solve per step.

One case starts with diagonal linear speed 10 and zero spin, then slides into rolling. The other starts at rest and receives horizontal acceleration 2; static friction maintains no-slip rolling. With sphere inertia `2/5 m r^2`:

- unforced final rolling speed is `5/7 * 10 = 50/7`;
- driven final rolling speed after two seconds is `5/7 * 2 * 2 = 20/7`;
- angular speed equals linear speed for unit radius;
- initial sliding deceleration is `mu * g = 2.5`.

COM position advances with solved velocity. Sphere orientation is kept fixed in this isolated fixture because its geometry and inertia are isotropic; angular velocity and rotational kinetic energy are measured. The contact anchors are updated to the analytic plane point each step. This tests the resting constraint directly, without CCD or GJK contact noise.

### Failure-before/fix-after evidence

The complete focused suite, built against the unchanged approved production solver, failed **101 of 185** checks, exit 1 (`bin/phase16-red-focused.log`). At 120/240 Hz the old sliding fixture exceeded the Coulomb limit by approximately 5.67262/5.69345 impulse units.

The initial Phase 16 implementation passed 185 focused checks, but its large-finite-coefficient test incorrectly expected overflow to disable friction. This review revision corrects that expectation.

Before the revision fix, the updated tests failed **6 of 192** checks against the initial Phase 16 solver, exit 1 (`bin/phase16-revision-red-focused.log`). These failures cover supported warm starts, cold sliding response, and tiny-normal-support cases for large finite coefficients. The revised implementation passes **192/192** focused and **558/558** full checks in Debug and nominal Release. No body finite-state assertion was changed or suppressed.

### Tolerances

- Analytic impulse and velocity comparisons: absolute component tolerance `2e-5`.
- Repeated accumulated solves: `3e-5`.
- Cache/retraction comparisons: existing `Near` default `1e-5`; zero-support tangent magnitude below `1e-6`.
- Two-second sphere linear/angular velocity: component tolerance `2e-4`.
- Final no-slip residual and driven maximum slip: below `2e-5`.
- Sphere/plane penetration: below `1e-5`; Coulomb-limit excess below `1e-6`.
- Unforced peak kinetic energy: at most 100.0001 for initial energy 100.
- Valid test contact/body outputs must remain finite.

## Validation Commands

Current Visual Studio solution, x64. The generated project files and premake settings were inspected. The engine/benchmark retain physics profiling.

```powershell
$physicsBuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $physicsBuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  '.\GEngine.sln' /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' `
  /p:Configuration=<Debug-or-Release> /p:Platform=x64 /clp:ErrorsOnly `
  /fl '/flp:logfile=bin\phase16-revision-<configuration>-build.log;verbosity=normal'

.\bin\Debug\PhysicsTests\PhysicsTests.exe --resting-friction
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --resting-friction
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline
```

The approved baseline Release build and full suite passed (366 checks) before the source change. The deterministic world-probe command ran before and after using the same materials, gravity, geometry, creation order, 1/120-second timestep, and 1,200 steps (ten seconds). There is no randomness or discarded warmup window. The movement thresholds are 0.05 units/s and 0.05 rad/s.

Ignored evidence logs:

- `bin/phase16-before-Release-{build,tests}.log`
- `bin/phase16-red-Debug-build.log`
- `bin/phase16-red-focused.log`
- `bin/phase16-{Debug,Release}-{build,focused,tests}.log`
- `bin/phase16-{before,after}-stability.log`
- `bin/phase16-revision-red-{build,focused}.log`
- `bin/phase16-revision-{Debug,Release}-{build,focused,tests}.log`
- `bin/phase16-revision-stability.log`

The sandbox launcher repeatedly failed before commands ran. Approved PowerShell operations performed the scoped reads/edits/validation; repository and build settings were not changed.

## Debug Result

PASS, exit 0:

- GEngine / PhysicsTests / PhysicsBenchmark build: 0 errors, 68 existing project/vendor/toolchain warning reports.
- `Resting-friction regression: 192 checks passed`.
- `PhysicsTests: 558 checks passed`.
- All four analytic sphere/plane runs remain finite.

## Release Result

PASS, exit 0:

- GEngine / PhysicsTests / PhysicsBenchmark build: 0 errors, 72 existing project/vendor/toolchain warning reports.
- `Resting-friction regression: 192 checks passed`.
- `PhysicsTests: 558 checks passed`.
- All four deterministic world probes report finite = 1.

Release remains the nominal optimized configuration with the documented Debug static CRT `/MTd` override. Allocator/runtime behavior is not a true Release-CRT measurement.

## Stability Results

### Focused analytic sphere/plane

**Measured**, matching in Debug and Release at printed precision:

| Rate | Case | Peak kinetic energy | Final speed | Final angular speed | Final slip | Maximum penetration | Maximum cone excess |
|---:|---|---:|---:|---:|---:|---:|---:|
| 120 Hz | Sliding into rolling | 100 | 7.14287 | 7.14287 | 0 | 0 | 1.49015e-9 |
| 240 Hz | Sliding into rolling | 100 | 7.14289 | 7.14289 | 0 | 0 | 9.31347e-10 |
| 120 Hz | Driven no-slip rolling | 11.4286 | 2.85715 | 2.85715 | 0 | 0 | 0 |
| 240 Hz | Driven no-slip rolling | 11.4286 | 2.85715 | 2.85715 | 0 | 0 | 0 |

Each fixture has one persistent constraint, two bodies, and one solve per step. All finite flags are true. Tiny positive cone excess is float rounding after projection and is below the declared tolerance.

### Deterministic world probes

**Measured**, nominal Release, approved Phase 15 baseline -> revised Phase 16:

| Metric | 4x4 box stack | Single sphere | 180-sphere lattice |
|---|---:|---:|---:|
| Peak energy / initial | 100% -> 100% | 100% -> 100% | 100% -> 100% |
| Final total energy | 862.311111 -> 862.682569 | 18.000989 -> 18.000989 | 4078.307824 -> 5373.984189 |
| Final average Y | 4.491204 -> 4.493114 | 1.500082 -> 1.500082 | 1.472101 -> 1.478156 |
| Peak linear speed | 0.216104 -> 0.215006 | 14.200018 -> 14.200018 | 15.825293 -> 21.625690 |
| Final maximum linear speed | 0.002784 -> 0.063917 | 0.000816 -> 0.000816 | 11.203113 -> 8.612429 |
| Peak angular speed | 0.059539 -> 0.090607 | 0.024529 -> 0.024529 | 11.950723 -> 11.220728 |
| Final maximum angular speed | 0.000534 -> 0.012439 | 0.000814 -> 0.000814 | 7.047541 -> 7.404088 |
| Final moving bodies | 0/16 -> 1/16 | 0/1 -> 0/1 | 179/180 -> 180/180 |
| Average generated contacts/step | 14.680833 -> 15.918333 | 0.795000 -> 0.795000 | 197.503333 -> 181.220000 |
| Final manifolds / points | 17 / 66 -> 16 / 52 | 1 / 1 -> 1 / 1 | 192 / 689 -> 177 / 669 |
| Mean step ms | 2.935119 -> 2.603148 | 0.053265 -> 0.053407 | 16.893539 -> 15.073779 |

All physical CSV fields for the free asymmetric body and single sphere are unchanged at printed precision. The free body's peak/final angular energy remains 104.242841% of initial, and its final angular speed remains 2.038718 rad/s. Its mean step time changes from 0.000191 to 0.000195 ms.

The overflow revision was compared directly with the original Phase 16 output in `bin/phase16-after-stability.log`: **all 80 non-timing CSV fields across all four scenarios match exactly at printed precision**. All four focused analytic sphere/plane metric lines also match the original Phase 16 results in both Debug and Release. Only measured wall-clock timings differ in these workloads.

Stack and lattice energy never exceed their initial energy in these runs. Settling is mixed: the stack retains one box above the motion threshold; the lattice has a higher peak linear speed and remains entirely moving, despite lower final maximum linear speed. These differences are disclosed for review, not treated as general stability improvement. The Phase 20/23 stability gates remain future work.

Penetration depth and per-scenario solver timing/residuals are **Not available** in the world-probe CSV. The focused analytic fixtures measure penetration separately as above. Production still performs one solver traversal per step and has no sleeping.

## Benchmark Results

This is Class B/C correctness and focused stability work. Collision-heavy/separated performance benchmarks were not required or run.

Trajectory mean step times above are **Measured**, not warmup/multi-sample benchmark medians. Changing friction changes the trajectory and contact counts, so these timings do not isolate algorithm cost. No performance speedup is claimed.

## Behavior Changes

Resting tangential impulses now require accumulated normal support and obey one circular bound. Cached tangents respect the current coefficient before warm starting, and excessive previously applied friction retracts when normal support shrinks.

Zero/negative coefficients clear cached tangents and disable resting friction. Every finite positive coefficient pair retains the existing multiplication policy through a double product and double Coulomb limit, with no cap. NaN/Inf body-field assertions and invariants remain unchanged.

## Known Limitations

- The existing coupled three-row candidate solve remains. This phase enforces the requested accumulated Coulomb bound; it does not introduce a new block solver or claim full nonlinear convergence for arbitrary coupled contacts.
- The normal lambda still includes existing velocity Baumgarte stabilization. Its energy/support semantics remain Phase 20.
- Ballistic friction is unchanged and remains outside this resting-friction contract.
- General feature/normal coherence and variable-timestep warm-start scaling remain later work.
- Direct NaN/Inf body fields remain unsupported by existing Debug assertions; local coefficient fallback does not make arbitrary invalid bodies supported.
- Focused isolated fixtures establish analytic friction behavior; the ten-second world probes do not establish general stacking/lattice stability.
- No sanitizer, Application Verifier, or renderer acceptance run was performed.

## Out-of-Scope Findings

Current source evidence, left unchanged:

- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp:678` computes ballistic friction independently of the normal impulse (`PHYS-BUG-022`, Phase 17).
- `GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp:170` retains velocity Baumgarte (`PHYS-BUG-008`, Phase 20).
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp:374` retains one outer solver iteration (`PHYS-BUG-005`, Phase 19).
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp:724` and `:767` retain live-body negative-time CCD rewind (`PHYS-BUG-009`, Phase 24).
- `PhysicsTests/PhysicsTests.vcxproj:87` retains the Release `/MTd` override (`PHYS-BUG-024`).
- `.gitignore:33` retains the pre-existing new blank line at EOF.

## git diff --check

Git commands use command-local `-c safe.directory=C:/dev/GEngine-physics`.

Phase-source check:

```text
git diff --check -- GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp PhysicsTests/src/main.cpp
exit 0
```

The new review is checked separately with `git diff --no-index --check -- NUL docs/physics/PHASE_16_REVIEW.md`. A new-file difference can return 1 without a whitespace diagnostic. LF-to-CRLF conversion notices are informational.

Repository-wide result:

```text
git diff --check
.gitignore:33: new blank line at EOF.
exit 2
```

Phase-scoped whitespace validation passes. The repository-wide failure is the unrelated, previously disclosed user-file issue; preserving that file follows AGENTS.md and the reviewed workflow from prior phases.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_16_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

The index remains empty. No commit, approved tag, push, or Phase 17 work was performed.

## Human Decision

PENDING

Review the accumulated disk projection and cache handling, full-range finite-coefficient multiplication, analytic test tolerances, and the mixed changes in stack/lattice residual motion.
