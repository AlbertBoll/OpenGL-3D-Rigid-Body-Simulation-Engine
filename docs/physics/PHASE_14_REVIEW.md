# Phase 14 Review

## Status

PHASE 14 STATUS: AWAITING HUMAN REVIEW

## Objective

Enforce consistent Static, Dynamic, and Kinematic mass, motion, gravity, and impulse semantics at rigid-body, constraint, and collision-response boundaries. Preserve the approved Phase 13 negative gyroscopic equation, cached inverse inertia, and angular regression coverage.

## Baseline Commit

`dce54bf6ee10814c5c457cc50001a94cdb0a628b`

`physics: phase 13 correct torque-free angular dynamics`

Branch: `physics/refactor`. The phase preflight verified the preceding approved tag is an ancestor of HEAD and no Phase 14 approved tag exists. The index was empty; all unrelated initial modifications and untracked files were preserved.

## Previous Approved Tag

`physics-phase-13-approved`, resolving to the baseline commit above.

## Audit Findings Addressed

`PHYS-BUG-013`: Kinematic gravity/impulse response, Static velocity integration, and solver mass/inertia inconsistent with body type.

The historical audit remains unchanged. Phase 13 already restricted gyroscopic acceleration to Dynamic bodies; this phase retains that fix and extends body-type consistency to the other physics consumers.

## Allowed Scope

Five production files, one dedicated test file, and this review: seven files total. `Constraints/Constraint.cpp` is necessary because its inverse-mass matrix and velocity vector feed the resting solver directly. `Constraints/ConstraintPenetration.cpp` has one expression changed so its existing friction mass denominator follows the same effective-mass policy.

No changes to contact conventions, friction equations, restitution policy, iteration count, Baumgarte, manifold persistence, CCD rewind/event architecture, broad-phase algorithm, geometry, scene synchronization, rendering, or build configuration.

## Files Changed

- `GEngine/include/GEngine/Physics/PhysicsBody.h`
- `GEngine/include/GEngine/Physics/PhysicsBody.cpp`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `GEngine/include/GEngine/Physics/Constraints/Constraint.cpp`
- `GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_14_REVIEW.md`

## Implementation Summary

### Body-type contract

| Type / configuration | Effective inverse mass and inverse inertia | Pose integration | Gravity / impulses | Contact velocity |
|---|---|---|---|---|
| Static | Zero | None, including nonzero stored velocities | None | Zero |
| Dynamic, finite positive inverse mass | Configured mass and corresponding cached inertia | Existing translation and corrected torque-free rotation | Existing Dynamic response | Stored linear/angular velocity |
| Kinematic | Zero | Prescribed translation and rotation about COM | None | Prescribed linear/angular velocity |
| Dynamic, zero/negative/non-finite legacy inverse mass | Zero numerical fallback | None | None | Zero |

`SetBodyTypeAndInverseMass(type, inverseMass)` validates the pair before changing either field. Dynamic requires finite positive inverse mass. Static/Kinematic accept finite nonnegative input and store zero inverse mass. Invalid enum values, negative/non-finite mass, and zero Dynamic inverse mass return false without changing the body. The default body is now Static with stored inverse mass zero.

Existing public `Type`, `m_InvMass`, and velocity fields remain compatible configuration storage. Legacy direct writes can still store a positive mass on a Static/Kinematic body, but **no physics solver consumes that value as effective mass**. `GetInverseMass()` derives physical response from type and numerical validity; both inverse-inertia getters follow the same policy. `GetLinearVelocity()` and `GetAngularVelocity()` expose the motion appropriate to the type. Stored Static velocities are retained but ignored by integration, narrow-phase prediction, and both solvers. Changing type does not invent or erase prescribed velocities.

Directly assigned invalid Dynamic mass is guarded at effective mass, velocity, inertia, impulse, and integration boundaries. This does not repair arbitrary invalid raw state: existing `HasFiniteState()` / Debug assertions still diagnose a NaN/Inf written directly into a world body. Supported configuration should use the validated setter and check its result.

The inertia cache now keys its inverse-mass source on **effective** inverse mass. A direct Dynamic-to-Static/Kinematic type change therefore refreshes a warmed nonzero inverse tensor to zero; returning to a valid Dynamic configuration refreshes it back. Pose, shape identity, and geometry revision checks remain intact.

Gravity and all three impulse entry points require Dynamic type. `Update()` skips Static and invalid-mass Dynamic integration. Kinematic rotation retains prescribed angular velocity, including on asymmetric shapes. The negative gyroscopic expression and its explicit Dynamic guard are unchanged.

Both constraint mass blocks and the complete constraint velocity vector use effective values. Ballistic normal/friction denominators, contact-point velocity, and mass-weighted projection do likewise. Both sphere prediction entry points and generic conservative advancement use effective velocity; their existing forward/negative-time rewind architecture is retained.

## Tests Added / Modified

Added `PhysicsTests.exe --body-types`, with **79 checks**, all also included in the full suite. All 191 earlier full-suite checks and all 40 Phase 13 angular checks remain.

- Static/Kinematic with configured inverse mass 0, 0.5, and 3; warm Dynamic cache followed by type changes; zero effective body/world inverse inertia; linear, angular, and off-center impulses; one second of prescribed asymmetric rotation and translation; return to Dynamic inertia.
- World gravity with separated Static, Kinematic, and Dynamic bodies and nonzero prescribed velocity; analytic semi-implicit Dynamic trajectory.
- Resting and ballistic contacts, each with Static/Kinematic drivers and configured inverse mass 0 or 4. Check the complete non-dynamic mass block, contact velocity, analytic normal response, unchanged driver velocity, and equivalent normal/friction response across configured masses.
- Sphere and generic CCD: false motion from Static velocity must disappear; prescribed Kinematic motion retains the expected 0.2-second impact. Both sphere entry points are covered.
- Default configuration, analytic Dynamic linear/angular impulse scaling, validated type/mass transitions, rejected zero/negative/Inf/NaN Dynamic mass and unknown enum, and guarded legacy invalid mass writes.
- A real cached contact impulse followed by both participants changing to non-dynamic types; warm starting and solving must leave their velocities unchanged and finite.
- Full-world CCD and ballistic response with a moving Dynamic sphere against Static/Kinematic drivers; analytic final position and velocity for both participants.

Before production edits, the initial 61-check subset failed **34 checks** against the approved Phase 13 source (`bin/phase14-red-tests.log`, exit 1). After implementation, all 79 checks pass. This failure-before/fix-after comparison demonstrates that the tests detect the assigned defect.

### Tolerances

- Non-dynamic inverse inertia, mass block, and unchanged stored velocities: exact zero/equality where stated.
- One-second prescribed pose: position components within `2e-5` units; sign-independent relative-quaternion angle below `2e-5` radians.
- Analytic contact, gravity, and impulse comparisons: existing `Near` default `1e-5` component tolerance.
- Generic CCD TOI: `0.002` seconds around the analytic 0.2-second impact, allowing the existing narrow-phase contact bias.
- Existing angular analytic, momentum-vector, energy, convergence, and cache tolerances are unchanged.

## Validation Commands

Current generated Visual Studio solution, x64, physics profiling enabled. The actual premake/project configuration was inspected. Each configuration was built with:

```powershell
$physicsBuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $physicsBuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  '.\GEngine.sln' /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' `
  /p:Configuration=<Debug-or-Release> /p:Platform=x64 /clp:ErrorsOnly `
  /fl '/flp:logfile=bin\phase14-<configuration>-build.log;verbosity=normal'
```

```powershell
.\bin\Debug\PhysicsTests\PhysicsTests.exe --body-types
.\bin\Debug\PhysicsTests\PhysicsTests.exe --angular-dynamics
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --body-types
.\bin\Release\PhysicsTests\PhysicsTests.exe --angular-dynamics
.\bin\Release\PhysicsTests\PhysicsTests.exe
```

Baseline and final measurements used identical Release commands:

```powershell
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5 --steady-state-warmup-steps=4 --steady-state-measured-steps=8
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline
```

Default timestep is 1/120 seconds. Each benchmark size discards two warmup samples and measures five samples. Collision samples measure one step; separated samples have four warmup steps and eight measured steps per sample. Stability uses 1,200 fixed steps over ten seconds with fixed geometry, materials, gravity, and body order, no randomness, and no discarded trajectory window.

Ignored local logs:

- `bin/phase14-before-tests.log`
- `bin/phase14-red-{build,tests}.log`
- `bin/phase14-{Debug,Release}-{build,focused,angular,tests}.log`
- `bin/phase14-{before,after}-{collision,separated,stability}.log`

The sandbox helper failed for some reads and patching. Approved PowerShell operations performed the scoped work; no repository or build settings were changed.

## Debug Result

PASS, all commands exit 0:

- GEngine / PhysicsTests / PhysicsBenchmark build: 0 errors; 147 existing vendor/project warning reports.
- `Body-type regression: 79 checks passed`.
- `Angular-dynamics regression: 40 checks passed`.
- `PhysicsTests: 270 checks passed`.

## Release Result

PASS, all commands exit 0:

- GEngine / PhysicsTests / PhysicsBenchmark build: 0 errors; 67 existing vendor/project/toolchain warning reports.
- `Body-type regression: 79 checks passed`.
- `Angular-dynamics regression: 40 checks passed`.
- `PhysicsTests: 270 checks passed`.

Release remains the nominal optimized configuration with the pre-existing Debug static CRT `/MTd` override; allocator/runtime behavior is not a true Release-CRT measurement.

The existing non-gating contact-normal XFAIL and body-removal XPASS remain. No new non-finite output was observed in valid-input tests or benchmark/stability workloads.

## Stability Results

**Measured:** every non-timing CSV field is identical before and after at printed precision: **zero differences**, across all four scenarios. This is a comparison of recorded metrics, not a bitwise trace of every intermediate state. No tolerance relaxation was needed.

| Metric, identical before/after | Free asymmetric body | 4x4 box stack | Single sphere | 180-sphere lattice |
|---|---:|---:|---:|---:|
| Peak energy / initial | 104.242841% | 119.622471% | 100% | 100% |
| Final energy change | +4.242841% | +12.143275% | -84.999176% | -86.640505% |
| Final average Y | 0 | 4.167445 | 1.500082 | 1.340196 |
| Peak linear speed | 0 | 17.619677 | 14.200018 | 28.570679 |
| Final maximum linear speed | 0 | 8.054067 | 0.000816 | 28.570679 |
| Peak angular speed | 2.158473 | 14.888249 | 0.024529 | 10.351630 |
| Final maximum angular speed | 2.038718 | 14.888247 | 0.000814 | 8.533093 |
| Final moving bodies | 1/1 | 13/16 | 0/1 | 176/180 |
| Average generated contacts | 0 | 16.406667 | 0.795000 | 202.089167 |
| Final manifolds / points | 0 / 0 | 17 / 52 | 1 / 1 | 201 / 734 |
| Average step ms, before -> after | 0.000188 -> 0.000212 | 2.075885 -> 2.100532 | 0.053434 -> 0.053617 | 17.289765 -> 17.588150 |

All finite flags are 1. Moving thresholds are 0.05 units/s or rad/s. Penetration, constraint residuals, and per-scenario solver time are **Not available** in this CSV. Sleeping is not implemented.

The stack and lattice remain unstable as documented in approved Phase 13; this phase neither worsens nor resolves their recorded physical metrics. The sphere still settles. The diagnostic only gates finiteness for these known-unstable workloads, not stable-contact acceptance.

Preserved angular tests report worst 120 Hz energy/momentum-vector errors 4.24299% / 2.65486%, and 240 Hz errors 2.08342% / 1.31205%. The CCD rewind diagnostic remains unchanged: at 1/120 s one forward/backward pair has orientation residual `8.46787e-5` radians and angular-velocity residual `0.000145353` rad/s; 1,000 pairs give `0.0870288` radians and `0.152281` rad/s. No rewind fix was made.

## Benchmark Results

**Measured:** external whole-step medians, same nominal Release x64 profiling configuration and workloads.

| Workload | Bodies | Before ms | After ms |
|---|---:|---:|---:|
| Collision-heavy | 100 | 4.174000 | 4.173400 |
| Collision-heavy | 1,000 | 42.530300 | 42.173600 |
| Collision-heavy | 10,000 | 460.687400 | 468.982100 |
| Separated | 100 | 0.020075 | 0.020000 |
| Separated | 1,000 | 0.207000 | 0.200288 |
| Separated | 10,000 | 2.214637 | 2.197138 |

Candidate/contact/manifold/constraint counts remain 38, 375, and 3,750 in collision-heavy workloads, with one solver iteration. All separated contact counts remain zero. Half the bodies are Dynamic; sleeping count remains zero.

At 10,000 bodies, measured mean integration time changes from 1.314280 to 1.017780 ms in collision-heavy work and from 0.535612 to 0.365837 ms in separated work. Static bodies now skip pose integration. No overall speedup is claimed: the collision-heavy 10,000-body median increased about 1.80% (**Derived** from the measured medians), and these single before/after sample groups include timing variation in unchanged dominant stages.

## Behavior Changes

- Static bodies have zero effective mass/inertia/contact velocity and do not integrate stored velocity.
- Kinematic bodies integrate prescribed velocity and can drive Dynamic contacts, while gravity and impulses leave the driver unchanged.
- Type changes invalidate effective inverse inertia, including after direct public-field edits.
- Invalid Dynamic mass is rejected through the validated setter; legacy invalid mass cannot supply an effective solver mass or integrate a body.
- Default Static bodies now store inverse mass zero. Legacy callers changing only Type to Dynamic must also provide positive inverse mass, or use the validated setter.

## Known Limitations

- Public fields still permit contradictory raw configuration storage. Enforcement is at the effective physics interface; widespread field privatization/call-site migration was not needed for the solver exit criterion. The new setter provides consistent stored configuration for new callers.
- Existing finite-state assertions remain; arbitrary NaN/Inf writes to raw world state are not a supported configuration path.
- Kinematic motion uses prescribed velocity or direct body pose edits; deriving velocity from ECS pose changes and runtime Scene synchronization remain later work.
- Type changes do not clear manifold lambdas. Response is type-safe, including warm starting, but general cache/feature persistence quality remains deferred.
- Existing solver/contact instability, first-order angular drift, CCD non-reversibility, and Release CRT caveat remain.
- No sanitizer, Application Verifier, or renderer acceptance run was performed.

## Out-of-Scope Findings

Current source evidence:

- `GEngine/include/GEngine/Physics/Broadphase.cpp:79` still expands bounds using stored linear velocity. Nonzero legacy Static velocity can enlarge candidate bounds unnecessarily; current bounds are retained, so this is conservative over-generation. Narrow-phase prediction and response now ignore that Static velocity. No broad-phase optimization was added.
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp:724` and `:767` still rewind live bodies with `Update(-toi)` (`PHYS-BUG-009`, Phase 24).
- `GEngine/include/GEngine/Physics/Manifold.cpp:12` swaps pair bodies/anchors without reversing the normal (`PHYS-BUG-006`, Phase 15); the full-suite XFAIL remains.
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp:374` retains `maxIters = 1`; `Constraints/ConstraintPenetration.cpp:151` retains velocity Baumgarte; `:172` retains the non-Coulomb friction floor. Their equations and policies were not modified.
- `_Scene.cpp:456`, `:496`, and `:525` still copy fixture mass to public fields at startup. Effective body accessors make these legacy writes type-safe for physics; runtime property synchronization remains deferred.
- `.gitignore:33` has the same pre-existing new blank line at EOF. The file remains untouched.

## git diff --check

Git commands use command-local `-c safe.directory=C:/dev/GEngine-physics`.

```text
git diff --check -- GEngine/include/GEngine/Physics/PhysicsBody.h GEngine/include/GEngine/Physics/PhysicsBody.cpp GEngine/include/GEngine/Physics/PhysicsSystem.cpp GEngine/include/GEngine/Physics/Constraints/Constraint.cpp GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp PhysicsTests/src/main.cpp
exit 0
```

No phase-source whitespace diagnostics. Existing Git LF-to-CRLF conversion notices are informational. The untracked review was checked separately with `git diff --no-index --check -- NUL docs/physics/PHASE_14_REVIEW.md`: exit 1 for the new-file difference, with no whitespace diagnostic.

Repository-wide result retains only the unrelated, previously disclosed issue:

```text
git diff --check
.gitignore:33: new blank line at EOF.
exit 2
```

Phase-scoped validation passes. The unrelated file is preserved according to the repository ownership rule and the existing session's phase-scoped whitespace policy.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Constraints/Constraint.cpp
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
 M GEngine/include/GEngine/Physics/PhysicsBody.cpp
 M GEngine/include/GEngine/Physics/PhysicsBody.h
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_14_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

The index remains empty. No commit, tag, push, or Phase 15 work was performed.

## Human Decision

PENDING

Review the effective-versus-stored compatibility contract, invalid Dynamic mass fallback, prescribed Kinematic motion, and unchanged downstream stability limitations.
