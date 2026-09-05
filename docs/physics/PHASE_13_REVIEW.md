# Phase 13 Review

## Status

PHASE 13 STATUS: AWAITING HUMAN REVIEW

Human Decision: PENDING.

The requested revisions are complete. The negative torque-free sign and cached inverse remain. Gyroscopic acceleration now explicitly requires a Dynamic body. Final Debug and Release pass 40 focused / 191 full checks.

**Isolation conclusion:** the contact stability failure reproduces with the correct sign and an uncached world inverse. The stack results are essentially identical; both lattice variants retain high motion. No cache invalidation/revision defect was found in focused mutation tests or 3,680,196 live angular updates. Float differences between equivalent inverse calculations affect the unstable lattice trajectory, but cache reuse is not the source of the reproduced stability failure. The existing contact/CCD pipeline remains problematic; the separate rewind diagnostic confirms state drift but does not establish rewind as the sole cause.

Free-body tolerance approval remains pending. The retained first-order method still gains about 4.24% angular energy at 120 Hz over ten seconds. Repository-wide whitespace checking still finds an unrelated pre-existing `.gitignore` error; Phase 13 files pass.

## Objective

Correct torque-free angular dynamics and remove the redundant world inverse computation. This revision additionally verifies body-type gating, isolates cached versus uncached behavior with the same correct equation, and measures the existing CCD rewind residuals. No solver or CCD architecture change is included.

## Baseline Commit

`3c208ad1a90c67cb294437874f79ae3a72641c33`

`3c208ad physics: phase 12 validate sphere and base shapes`

Branch: `physics/refactor`.

## Previous Approved Tag

`physics-phase-12-approved`, resolving to the baseline commit. Original preflight verified ancestry and absence of a Phase 13 approved tag. Revision preflight confirmed unchanged HEAD/branch and preserved the existing working-tree ownership boundary.

No commit, approved tag, push, or Phase 14 work was performed.

## Audit Findings Addressed

- `PHYS-BUG-004`: correct the negative sign in the torque-free Euler equation.
- `PHYS-PERF-011`: reuse the cached world inverse inertia.
- Human revision feedback: explicitly gate gyroscopic acceleration on Dynamic body type, isolate cache substitution, and diagnose rewind state drift.

This does not claim to finish `PHYS-BUG-013` body-type invariants or `PHYS-BUG-009` pure CCD queries. The historical audit remains unchanged.

## Allowed Scope

One production file, one existing dedicated test file, and this review. Class B validation plus the requested existing benchmarks and deterministic stability diagnostics.

Temporary uncached/instrumented variants were built only for isolation, then removed from the production source. Their source snapshots and results are ignored `bin/*.log` artifacts. The final tree has no diagnostic mode or instrumentation in production code.

## Files Changed

- `GEngine/include/GEngine/Physics/PhysicsBody.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_13_REVIEW.md`

All unrelated pre-existing files remain untouched.

## Implementation Summary

### Equation and mass cancellation

The retained corrected world-space equation is:

`I * omega_dot = -omega x (I * omega)`.

`inertiaWorld` is unit-mass inertia; `inverseInertiaWorld` includes inverse mass. Therefore the cached update is:

`alpha = -(inverseInertiaWorld * cross(omega, inertiaWorld * omega)) / inverseMass`.

The division cancels the cached mass factor, preserving mass-independent free precession. The cache refresh and existing finite/singular inverse safeguards remain.

### Body-type verification

The exact `physics-phase-12-approved` version of `RigidBody3D::Update()` was inspected with `git show`. Its gyroscopic block had **no body-type guard**; Phase 13 did not remove an original Dynamic guard.

The final block now requires:

`Type == BodyType::Dynamic && IsFinite(inverseMass) && inverseMass > 0`.

Body type controls whether gyroscopic dynamics are applied. Positive finite inverse mass is only numerical protection for the cached mass cancellation. Static and Kinematic bodies receive no gyroscopic acceleration even with positive inverse mass.

| Body type | Gyroscopic acceleration in final Update | Existing prescribed-velocity pose integration |
|---|---|---|
| Dynamic | Correct negative term, subject to safe mass | Retained |
| Static | None, regardless of mass | Existing path retained |
| Kinematic | None, regardless of mass | Existing path retained |

This deliberately does not claim complete Static/Kinematic semantics. `Update()` still advances position/orientation from stored velocities for all three types, and other gravity/impulse paths have their existing policies. Broad type/mass enforcement belongs to Phase 14 and was not started.

The existing explicit angular-velocity step, exponential-map quaternion step, normalization, COM handling, and linear integration remain. No damping or solver constants changed.

## Tests Added / Modified

All original Phase 13 analytic, momentum-vector, mass-independence, convergence, and cache tests remain. The focused mode is `PhysicsTests.exe --angular-dynamics`, and all its checks also run in the full suite.

The final focused set has **40 checks**: the original 22 plus 18 revision checks.

- Six body-type checks: Static and Kinematic, each with inverse mass 0, 0.37, and 4; warmed cache followed by type/mass edits; 1,200 steps with angular velocity required to remain exactly unchanged.
- Six cache checks: inverse masses 0.37, 1, and 2.5; each runs 1,200 fresh-reference comparisons. At step 300 the same shape is rebuilt, at 600 a distinct shape/inertia replaces it, and at 900 mass and orientation change together. The reference recomputes inertia directly from current shape/pose without a body cache.
- Six diagnostic checks: three TOIs, each verifying finite forward/backward runs and a temporary snapshot/restore comparison. Rewind residual magnitudes are non-gating measurements; tests do not require the existing defect to persist.

Original coverage retains six analytic sign/frame/mass comparisons, zero-mass safety, free-body energy/momentum convergence at 120/240 Hz, normalized finite state, isotropic spin, principal-axis spin, and zero-spin behavior. The original pre-fix regression failed 15 of its first 21 checks, demonstrating that it detects the wrong sign.

### Tolerances

Unchanged original free-body fixture: centered half-extents `(1,2,3)`, body-frame omega `(0.7,1.1,1.6)`, ten seconds, no forces/collisions. Rotated cases use 0.73 radians about normalized `(1,-2,3)`.

| Check | Bound |
|---|---:|
| Analytic one-step component error | 2e-6 rad/s |
| Quaternion length error | 2e-6 |
| 120 Hz peak relative energy / momentum-vector errors | 5% / 3% |
| 240 Hz peak relative energy / momentum-vector errors | 2.5% / 1.5% |
| Fine-step error divided by coarse-step error | <= 0.6 for each invariant |
| Constant-spin velocity | 1e-5 rad/s |
| New fresh-inverse relative column error | 5e-6 |
| New one-step omega-vector / orientation errors | 3e-6 rad/s / 3e-6 radians |
| Static/Kinematic omega change | Exactly zero |

Energy error is `abs(E(t)/E(0)-1)`; momentum error is `length(L(t)-L(0))/length(L(0))`. Direction changes therefore cannot hide behind conserved magnitude.

The energy/momentum bounds remain proposed for human acceptance, not exact-conservation guarantees. The old sign passes the energy bound but fails analytic and momentum-vector checks.

## Isolation Results

### Controlled cached versus uncached builds

Both variants use the same negative sign, Dynamic/mass guards, integration method, fixtures, and solver. The temporary reference changes only the alpha expression to:

`alpha = -InverseOrZero(inertiaWorld) * gyroscopicTerm`.

That is a fresh inverse of the unit-mass world tensor, so no mass division is needed. The source snapshots differ only in this expression. Both variants passed 40 focused and 191 full checks before their existing benchmark runs.

After isolation, the cached source was restored byte-for-byte. SHA-256:

`A48F4A6BD8B2B688EE52537A64BD93DB781516B4F8CD9F06E10A7F14CBAF5ED7`.

### Angular equivalence — Measured

Worst free-body errors across the tested masses/frames:

| Frequency | Cached energy error | Uncached energy error | Cached momentum-vector error | Uncached momentum-vector error |
|---|---:|---:|---:|---:|
| 120 Hz | 4.24299% | 4.24292% | 2.65486% | 2.65486% |
| 240 Hz | 2.08342% | 2.08329% | 1.31205% | 1.31202% |

For the delivered cached path, the independent cache mutation regression measured maximum relative inverse-column error `2.18948e-6`, one-step omega error `1.78814e-7 rad/s`, and one-step orientation error `1.47749e-7 radians`. All are below the documented bounds.

These results confirm equivalent angular equations at the tested precision, including mass/pose/geometry changes. They do not claim bitwise equality of arbitrary full-world trajectories.

### Live cache investigation — Measured

Because the lattice trajectories differed, temporary instrumentation checked every eligible Dynamic angular update during all four existing stability scenarios. It recomputed world inertia from the current shape and normalized pose, inverted it afresh, and compared both the cache and resulting angular acceleration.

Instrumentation was removed afterward. A single diagnostic binary selected cached/uncached alpha using a temporary process-local environment flag; both runs reproduced the corresponding uninstrumented physical metrics at printed precision. Instrumented timings are not used as performance evidence.

| Live check | Cached trajectory | Uncached trajectory |
|---|---:|---:|
| Dynamic angular update calls | 1,867,960 | 1,812,236 |
| Non-finite comparisons | 0 | 0 |
| Relative matrix errors above 5e-6 | 0 | 0 |
| Maximum relative forward-inertia error | 0 | 0 |
| Maximum relative inverse-inertia error | 2.362201e-6 | 2.191256e-6 |
| Maximum resulting omega-step difference | 2.439046e-8 rad/s | 2.406844e-8 rad/s |

Relative matrix error uses the Frobenius norm. Combined calls = **3,680,196 (Derived)**.

There is no evidence of stale cache data in these paths. The remaining difference is finite-precision arithmetic between rotating an inverse and inverting a rotated tensor. Such small differences alter the unstable lattice trajectory; they do not explain away the common high-motion failure or justify claiming the two trajectories are identical.

### Contact-regression attribution

The stack's 119.622471% peak energy and high residual motion reproduce without cache reuse. The lattice's high final speed also reproduces without it: 27.660252 versus 28.570679 units/s. Therefore the reported contact failure is **not specific to the Phase 13 cache substitution**.

The measured evidence supports pre-existing downstream contact/CCD instability and numerical sensitivity exposed by the corrected dynamics. No cache invalidation/revision defect was found. The exact contribution of each downstream defect remains unisolated; in particular, the rewind diagnostic below proves non-reversibility, not that rewind alone causes the stack/lattice failure.

## CCD Rewind Diagnostic

The test records a centered asymmetric box before `Update(+toi)` followed by `Update(-toi)`.

Initial state (printed precision):

- Position `(1,2,3)`.
- Quaternion `(w,x,y,z) = (0.934124,0.0953987,-0.190797,0.286196)`.
- Linear velocity `(0.25,-0.5,0.75)`.
- Angular velocity `(-0.576796,0.788674,1.81805)`.
- Dynamic, inverse mass 1; warmed inertia/bounds caches.

The orientation residual is the sign-independent relative-quaternion rotation angle, calculated with `2*atan2(length(xyz),abs(w))` to resolve small angles.

**Measured, delivered cached path:**

| TOI, seconds | Query pairs | Position residual | Orientation residual, rad | Linear-velocity residual | Angular-velocity residual, rad/s |
|---|---:|---:|---:|---:|---:|
| 1/240 | 1 | 0 | 2.11822e-5 | 0 | 3.62657e-5 |
| 1/240 | 1,000 | 0 | 0.0212664 | 0 | 0.0366794 |
| 1/120 | 1 | 5.96046e-8 | 8.46787e-5 | 0 | 0.000145353 |
| 1/120 | 1,000 | 5.96046e-8 | 0.0870288 | 0 | 0.152281 |
| 1/60 | 1 | 0 | 0.000340406 | 0 | 0.000583660 |
| 1/60 | 1,000 | 0 | 0.378089 | 0 | 0.701627 |

At 1/120 s, the uncached path produces one-pair orientation/omega residuals `8.46787e-5 rad / 0.000145302 rad/s`, and 1,000-pair residuals `0.0870287 rad / 0.152281 rad/s`. Position and linear-velocity residuals match the cached path. The non-reversibility therefore persists without cached inverse reuse.

Temporary snapshot/restore gives zero position, orientation, linear-velocity, and angular-velocity residuals for every TOI in both variants. It exists only in the test; production collision prediction still uses its original rewind architecture.

## Validation Commands

Current generated Visual Studio solution, x64, physics profiling enabled. Each build used:

```powershell
$physicsBuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $physicsBuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Msbuild\Current\Bin\MSBuild.exe' `
  '.\GEngine.sln' /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' `
  /p:Configuration=<Debug-or-Release> /p:Platform=x64 /clp:ErrorsOnly `
  /fl '/flp:logfile=bin\<phase13-revision-build-log>;verbosity=normal'
```

```powershell
.\bin\Debug\PhysicsTests\PhysicsTests.exe --angular-dynamics
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --angular-dynamics
.\bin\Release\PhysicsTests\PhysicsTests.exe
```

After restoring the final source and removing instrumentation, both configurations were rebuilt and their focused/full suites rerun.

Identical existing Release benchmarks for cached and uncached variants:

```powershell
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5 --steady-state-warmup-steps=4 --steady-state-measured-steps=8
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline
```

Default dt is 1/120 s. Both performance workloads discard two warmup samples and collect five samples at each size. Collision samples measure one step. Separated samples use four warmup steps plus eight measured steps. Stability uses 1,200 fixed steps over ten seconds, fixed body order/materials/gravity, no randomness, and no discarded trajectory window.

Reproducible ignored artifacts:

- `bin/phase13-revision-{cached,uncached}-{focused,tests,collision,separated,stability}.log`
- `bin/phase13-revision-{cached,uncached}-source.cpp.log`
- `bin/phase13-revision-live-audit-source.cpp.log`
- `bin/phase13-revision-live-{cached,uncached}.log`
- `bin/phase13-revision-final-{debug,release}-{build,focused,tests}.log`
- Earlier Phase 13 before/after measurements remain in `bin/phase13-{before,after}-{collision,separated,stability}.csv.log`.

Temporary source edits used guarded restoration and source equality checks. Sandbox helpers failed at setup; approved PowerShell commands handled the scoped work outside that failing helper. No build/repository settings were changed.

## Debug Result

- Final GEngine / PhysicsTests / PhysicsBenchmark build: PASS, exit 0, 0 errors; 68 existing project/vendor/toolchain warning reports.
- Focused: PASS, `Angular-dynamics regression: 40 checks passed`.
- Full: PASS, `PhysicsTests: 191 checks passed`.
- Finite-state checks pass; original non-gating contact-normal XFAIL and body-removal XPASS remain.

## Release Result

- Final restored-source build: PASS, exit 0, 0 errors; 5 existing warning reports.
- Focused: PASS, `Angular-dynamics regression: 40 checks passed`.
- Full: PASS, `PhysicsTests: 191 checks passed`.
- Uncached isolation also passes 40 focused / 191 full checks.
- Both benchmark groups and both live-cache diagnostic runs exit 0 with finite states.
- Final Debug and Release diagnostic values agree at printed precision.

Release is still the nominal optimized configuration with the existing Debug static CRT `/MTd` override. Allocator/runtime behavior is not a true Release-CRT result.

## Stability Results

**Measured:** existing fixtures, nominal Release, ten seconds at 120 Hz. These are individual deterministic trajectories; timing means are not used as acceptance criteria.

| Metric | Stack uncached | Stack cached | Sphere uncached | Sphere cached | Lattice uncached | Lattice cached |
|---|---:|---:|---:|---:|---:|---:|
| Peak energy / initial | 119.622471% | 119.622471% | 100% | 100% | 100% | 100% |
| Final energy change | +12.143273% | +12.143275% | -84.999176% | -84.999176% | -86.201397% | -86.640505% |
| Final average Y | 4.167445 | 4.167445 | 1.500082 | 1.500082 | 1.286368 | 1.340196 |
| Peak linear speed | 17.619677 | 17.619677 | 14.200018 | 14.200018 | 27.660252 | 28.570679 |
| Final max linear speed | 8.054067 | 8.054067 | 0.000816 | 0.000816 | 27.660252 | 28.570679 |
| Peak angular speed | 14.888249 | 14.888249 | 0.024529 | 0.024529 | 10.427541 | 10.351630 |
| Final max angular speed | 14.888247 | 14.888247 | 0.000814 | 0.000814 | 8.337908 | 8.533093 |
| Final moving bodies (0.05 thresholds) | 13/16 | 13/16 | 0/1 | 0/1 | 177/180 | 176/180 |
| Average generated contacts | 16.406667 | 16.406667 | 0.795000 | 0.795000 | 199.207500 | 202.089167 |
| Final manifolds / points | 17 / 52 | 17 / 52 | 1 / 1 | 1 / 1 | 193 / 711 | 201 / 734 |

The original wrong-sign baseline measured stack peak energy 105.167249%, final stack maximum speed 0.080448 units/s, and final lattice maximum speed 12.628589 units/s. Both correct-sign variants worsen those metrics. The corrected sign is retained as explicitly requested; the regression is not hidden or treated as passing stable-contact acceptance.

The existing harness gates finiteness, not settling/energy for these known-unstable scenarios. Penetration, constraint residuals, and per-scenario solver timing are **Not available** in this CSV. No sleeping or solver changes were introduced.

## Benchmark Results

**Measured:** nominal Release x64, profiling enabled, identical workload/guards/correct sign. Whole-step values are external medians over five samples; integration values are profiler means. The reference is uncached and the delivered implementation is cached.

| Workload | Bodies | Uncached whole step, ms | Cached whole step, ms | Uncached integration, ms | Cached integration, ms |
|---|---:|---:|---:|---:|---:|
| Collision-heavy | 100 | 4.079700 | 4.284200 | 0.007280 | 0.006900 |
| Collision-heavy | 1,000 | 41.193600 | 42.387400 | 0.100240 | 0.099460 |
| Collision-heavy | 10,000 | 456.299800 | 471.897000 | 1.446660 | 1.389640 |
| Separated | 100 | 0.020588 | 0.020025 | 0.004620 | 0.004012 |
| Separated | 1,000 | 0.222025 | 0.207950 | 0.050735 | 0.045420 |
| Separated | 10,000 | 2.331612 | 2.272350 | 0.621495 | 0.542052 |

Collision candidate/contact/manifold/constraint counts match at 38, 375, and 3,750. Solver iterations remain one. Separated counts remain zero. Half the bodies are dynamic and sleeping count is zero.

Cached integration measurements are lower, but collision-heavy whole-step medians are higher in this sample. Thus no overall speedup is claimed. These measurements include ordinary timing variation in dominant unchanged stages. Instrumented live-audit timings are excluded. Benchmark checks cover finite state and workload counters; independent angular/reference tests provide the numerical comparison.

## Behavior Changes

- Correct negative gyroscopic acceleration applies only to Dynamic bodies with numerically safe inverse mass.
- Static and Kinematic bodies receive no gyroscopic acceleration even with positive mass.
- Free precession is mass independent and uses the cache.
- Rewind remains non-reversible; the new diagnostic exposes its residuals.
- Stack/lattice failures persist with either inverse strategy; the sphere settles identically at printed precision.

## Known Limitations

- Original free-body tolerance approval remains pending; first-order energy drift is still about 4.24% at 120 Hz, slightly above the wrong-sign baseline's 4.07%.
- Full contact trajectories are not bitwise equivalent between inverse calculations. The lattice remains numerically sensitive and unsettled.
- No cache defect was observed in the tested states; this is bounded evidence, not proof over arbitrary shapes/inputs.
- No solver, CCD query architecture, Static pose policy, Kinematic gravity/impulse policy, or higher-order integrator was changed.
- No sanitizer, Application Verifier, or renderer acceptance run was performed.
- Existing `/MTd` and unrelated global whitespace caveats remain.

## Out-of-Scope Findings

Current source evidence, unchanged:

- `GEngine/include/GEngine/Physics/PhysicsBody.cpp:296` and the quaternion update below the gyroscopic block still advance prescribed poses for all types. `PhysicsSystem.cpp:253` applies gravity to any non-Static positive-mass body, including Kinematic. Phase 14 remains unstarted.
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp:724` and `:767` rewind live bodies with `Update(-toi)`. The new diagnostic measures the existing non-reversibility (`PHYS-BUG-009`, Phase 24).
- `GEngine/include/GEngine/Physics/Manifold.cpp:12` reorders bodies/anchors without negating the normal; the full-suite diagnostic remains dot=-1 (`PHYS-BUG-006`, Phase 15).
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp:374` still uses `maxIters = 1`; `Constraints/ConstraintPenetration.cpp:151` still injects Baumgarte bias into physical velocity (`PHYS-BUG-005/008`).
- `GEngine/include/GEngine/Physics/ShapeBox.cpp:118` contains off-center inertia behavior outside these centered-body regressions (`PHYS-BUG-016`).
- `.gitignore:33` contains a pre-existing new blank line at EOF. The file was not changed by this phase.

## git diff --check

Git commands use command-local `-c safe.directory=C:/dev/GEngine-physics`.

```text
git diff --check -- GEngine/include/GEngine/Physics/PhysicsBody.cpp PhysicsTests/src/main.cpp
exit 0
```

No phase-source whitespace errors. Git emits existing LF-to-CRLF conversion notices for the two edited files. The untracked review is checked separately using `git diff --no-index --check -- NUL docs/physics/PHASE_13_REVIEW.md`; exit 1 represents the new-file difference, with no whitespace diagnostic.

Repository-wide checking retains the unrelated diagnostic:

```text
.gitignore:33: new blank line at EOF.
exit 2
```

The user explicitly requested phase-scoped checking for this revision; it passes. The unrelated global failure remains disclosed and untouched.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/PhysicsBody.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_13_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

## Human Decision

PENDING

Review the Dynamic-only acceleration guard, preserved free-body tolerances, controlled isolation results, and downstream stability limitations. Phase 13 remains at the human-review gate.
