# Phase 17 Review

## Status

PHASE 17 STATUS: AWAITING HUMAN REVIEW

Focused acceptance and full tests pass. The sphere lattice now peaks at **102.332471% of initial energy**, versus 100% at the approved baseline, and has higher final residual speed. This measured stability regression requires explicit human assessment. Temporary stage measurements found no kinetic-energy gain inside ballistic responses and did find energy injection in the unchanged resting solver. This phase does not claim general stability improvement.

## Objective

Reject currently separating ballistic contacts and bound tangential impact impulse by the repulsive normal impulse actually applied. Address the positive-TOI response only, without redesigning the event list.

## Baseline Commit

`26017a9930bc629cbf6ffc3f9bcf4a82d57e58ee`

`physics: phase 16 enforce accumulated Coulomb friction`

Branch: `physics/refactor`. The Phase 17 preflight verified that the preceding approved tag is an ancestor of HEAD and no approved Phase 17 tag exists. The initial index was empty; no nested AGENTS.md or existing Phase 17 review was found. Unrelated initial work remains untouched.

## Previous Approved Tag

`physics-phase-16-approved`, resolving to the baseline commit above.

## Audit Findings Addressed

- `PHYS-BUG-010`, separating-response portion only: a stored positive-TOI contact could apply an attractive impulse after an earlier event changed its current velocity.
- `PHYS-BUG-022`: ballistic friction cancelled a material-scaled fraction of tangential velocity independently of normal impulse.

The audit is unchanged as a historical baseline. Remaining TOI geometry/event revalidation belongs to Phase 25.

## Allowed Scope

One production file, one dedicated test file, and this review: three files total. No header, build configuration, resting solver, restitution threshold, event ordering, or integration changes.

## Files Changed

- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_17_REVIEW.md`

## Implementation Summary

The contact normal retains Phase 15's world-space B -> A convention. Relative velocity includes each body's current linear velocity and `omega cross leverArm`. A contact closes only when `dot(vA - vB, n) < 0`.

The resolver applies a normal impulse only for a finite closing speed, valid positive effective-mass denominator, and finite repulsive candidate. Separating or exactly tangential contacts therefore receive neither normal nor friction impulse. A rejected or zero normal impulse gives friction no support. Existing body finite-state assertions remain intact.

For normal magnitude `Jn`, tangential unit direction `t`, tangential speed `s`, and inverse effective masses `Kn` and `Kt`, the response is:

```text
vn = dot(current_contact_velocity_A - current_contact_velocity_B, n)
Jn = -(1 + elasticityA * elasticityB) * vn / Kn    // closing contacts only
apply +n * Jn to A, -n * Jn to B

recompute current relative contact velocity after the normal response
vt = relative_velocity - n * dot(relative_velocity, n)
s = length(vt)
t = vt / s
Kt = invMassA + invMassB
   + dot(cross(ra, t), inverseInertiaA * cross(ra, t))
   + dot(cross(rb, t), inverseInertiaB * cross(rb, t))

mu = double(frictionA) * double(frictionB)         // both finite and positive
Jt = min(s / Kt, mu * double(Jn))
apply -t * Jt to A, +t * Jt to B
```

The tangent candidate cancels slip when the Coulomb bound allows it. Otherwise the impulse saturates at `mu * Jn`. Recomputing slip matters for off-center contacts: the normal impulse can create or reverse tangential contact motion through angular velocity. Friction then opposes the updated slip rather than its obsolete pre-impact value.

**Derived:** for a fixed contact pose, friction changes kinetic energy by `-Jt*s + 0.5*Kt*Jt^2`. With `0 <= Jt <= s/Kt`, this is nonpositive for a closed dynamic pair or stationary infinite-mass support. This argument does not claim global simulation stability or include work done by a moving kinematic boundary.

Material combination remains multiplication, consistent with approved Phase 16. The product and Coulomb limit use double, covering all finite positive float coefficient pairs without an arbitrary cap. Zero/negative inputs disable friction, including two negative coefficients. Raw NaN/Inf body fields remain outside the body invariants; assertions were not weakened.

Existing restitution multiplication is unchanged, with no new low-speed threshold. Existing zero-TOI positional projection remains in the shared resolver after the impulse logic; the normal engine pipeline still routes zero-TOI contacts into manifolds. No early return bypasses that projection or the final finite-state assertions.

## Tests Added / Modified

Added `PhysicsTests.exe --ballistic-contact`, with **180 gating checks**, also included in the full suite. The previous 558 checks remain unchanged; the full suite now has **738 checks**.

- **Separating guard (10 checks):** separating translation, exactly tangent motion, closing COM motion made separating by spin, both A/B orders, unchanged positions/velocities, and an earlier bounce making a later stored contact separating. Restitution product 1 still produces the expected first bounce.
- **Analytic Coulomb response (144 checks):** dynamic/static and unequal-mass dynamic/dynamic pairs, both A/B orders, closing speeds 0.001/0.5/4, friction 0/0.25/2, and tangent speeds 0.01/10 along a diagonal direction. Check analytic linear response, normal repulsion, Coulomb magnitude, finite output, zero angular response for zero levers, and non-increasing pair kinetic energy with restitution product 0.25.
- **Materials and zero effective mass (10 checks):** `1e30f` and `FLT_MAX` coefficient pairs with ordinary and `1e-30f` normal closing speeds; zero/negative coefficients on either body; a kinematic/static pair preserving prescribed motion.
- **Off-center friction (12 checks):** a unit-mass unit sphere, lever `(0.5, -1, 0)`, normal impulse `16/13`, rotational inverse inertia `5/2`, and tangent inverse effective mass `7/2`. Test initial tangent speeds -1/0/3 and friction 0.1/1 in both A/B orders. Compare linear/angular response and kinetic energy against the normal-only intermediate state.
- **Generated swept grazing contact (4 checks):** analytic sphere/sphere dispatch must produce a positive TOI; advancing to that TOI and resolving either body order must saturate the Coulomb bound and dissipate energy.

The coincident-center impulse fixture intentionally isolates equations from geometry. The swept fixture separately exercises collision-generated positive-TOI anchors and normal.

### Failure-before/fix-after evidence

The new focused tests, built against unchanged approved Phase 16 production code, failed **109 of 180** checks, exit 1 (`bin/phase17-red-focused.log`). After implementation, all 180 pass in both configurations. No expected-failure exemptions or relaxed pre-existing checks were added.

### Tolerances

- Analytic linear/angular velocities and swept normal/tangent impulses: absolute tolerance `2e-5`.
- Measured Coulomb excess after extracting impulses from float velocities: at most `5e-6`.
- Kinetic energy comparisons: absolute tolerance `2e-5`.
- Separating-contact state and prescribed immovable motion: exact float equality.
- Ordinary material checks: existing `Near` default `1e-5`.
- All tested body outputs remain finite.

## Validation Commands

Current Visual Studio/MSBuild x64 solution and generated project settings were inspected. The engine and benchmark retain physics profiling.

```powershell
$physicsBuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $physicsBuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  '.\GEngine.sln' /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' `
  /p:Configuration=<Debug-or-Release> /p:Platform=x64 /clp:ErrorsOnly `
  /fl '/flp:logfile=bin\phase17-final-<configuration>-build.log;verbosity=normal'

.\bin\Debug\PhysicsTests\PhysicsTests.exe --ballistic-contact
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --ballistic-contact
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline
```

Before implementation, nominal Release build/full tests passed (558 checks), and all four world probes ran. Final validation repeated after removing temporary diagnostic instrumentation, rebuilding both configurations from the restored source.

Ignored evidence logs:

- `bin/phase17-before-Release-{build,tests}.log`
- `bin/phase17-before-stability.log`
- `bin/phase17-red-Debug-build.log`, `bin/phase17-red-focused.log`
- `bin/phase17-{Debug,Release}-{build,focused,tests}.log`
- `bin/phase17-after-stability.log`
- `bin/phase17-energy-diagnostic-build.log`, `bin/phase17-energy-diagnostic.log`
- `bin/phase17-final-{Debug,Release}-{build,focused,tests}.log`
- `bin/phase17-final-stability.log`

The sandbox launcher failed before execution; approved PowerShell operations performed the scoped work outside it. No repository/build settings were changed.

## Debug Result

PASS, exit 0:

- Final GEngine / PhysicsTests / PhysicsBenchmark build: 0 errors, 4 existing warning reports.
- `Ballistic-contact regression: 180 checks passed`.
- `PhysicsTests: 738 checks passed`.
- One existing non-gating lifetime diagnostic reports zero observed known issues.

## Release Result

PASS, exit 0:

- Final GEngine / PhysicsTests / PhysicsBenchmark build: 0 errors, 5 existing warning reports. The initial implementation build recompiled the tests and reported 72 existing warnings; the final incremental build reused that test object.
- `Ballistic-contact regression: 180 checks passed`.
- `PhysicsTests: 738 checks passed`.
- All four deterministic world probes report finite = 1.

Release remains optimized with the documented Debug static CRT `/MTd` override. Its allocator/runtime behavior is not a true Release-CRT measurement.

## Stability Results

**Measured**, nominal Release, approved Phase 16 -> final Phase 17. Identical materials, geometry, gravity, fixed creation order, timestep `1/120`, 1,200 steps (ten seconds), and no randomness or discarded warmup. Moving thresholds remain 0.05 units/s and 0.05 rad/s.

| Metric | 4x4 box stack | Single sphere | 180-sphere lattice |
|---|---:|---:|---:|
| Peak energy / initial | 100% -> 100% | 100% -> 100% | 100% -> **102.332471%** |
| Final total energy | 862.682569 -> 862.684857 | 18.000989 -> 18.013751 | 5373.984189 -> 5570.047855 |
| Final average Y | 4.493114 -> 4.493115 | 1.500082 -> 1.501146 | 1.478156 -> 1.472727 |
| Peak linear speed | 0.215006 -> 0.215006 | 14.200018 -> 14.200018 | 21.625690 -> 21.224272 |
| Final maximum linear speed | 0.063917 -> 0.081224 | 0.000816 -> 0.001042 | 8.612429 -> 11.629561 |
| Peak angular speed | 0.090607 -> 0.090607 | 0.024529 -> 0.001040 | 11.220728 -> 11.389524 |
| Final maximum angular speed | 0.012439 -> 0.015761 | 0.000814 -> 0.001040 | 7.404088 -> 9.088459 |
| Final moving bodies | 1/16 -> 1/16 | 0/1 -> 0/1 | 180/180 -> 180/180 |
| Average generated contacts/step | 15.918333 -> 15.894167 | 0.795000 -> 0.003333 | 181.220000 -> 205.105833 |
| Final manifolds / points | 16 / 52 -> 16 / 52 | 1 / 1 -> 1 / 1 | 177 / 669 -> 177 / 652 |
| Mean step ms | 2.609025 -> 2.579797 | 0.054116 -> 0.025939 | 15.164234 -> 14.324744 |

The asymmetric free-body physical fields remain unchanged at printed precision: initial energy 5.211667, peak/final 5.432790 (104.242841%), and final angular speed 2.038718. Its mean step time is 0.000198 -> 0.000204 ms. This existing angular drift is not corrected here.

The lattice peak rises from 30240 to 30945.339133 energy units. Its final maximum linear and angular speeds worsen, and all 180 bodies remain moving. The stack also retains slightly higher final speed. These results are disclosed as regressions, not accepted as a general stability improvement. The later Phase 20/23 stability gates remain necessary.

World penetration depth, solver residual, and per-scenario solver stage timing are **Not available** in this CSV. No sleeping exists. Existing analytic resting sphere/plane checks in the full suite continue to pass, including penetration and energy tolerances.

### Energy regression investigation

A temporary, uncommitted diagnostic measured double-precision kinetic energy immediately before/after each ballistic resolver call, including both dynamic participants. It also measured total lattice kinetic energy before/after the existing resting PreSolve plus Solve stage. These stages do not change positions in the measured positive-TOI pipeline, so a kinetic-energy increase there also increases mechanical energy.

**Measured**, across the same four world probes:

```text
ballistic_samples=3028
ballistic_positive=0
ballistic_max_gain=0
resting_samples=1200
resting_positive=43
resting_max_gain=7005.03024
```

The positive-step diagnostic tolerance was `1e-6 * max(1, abs(beforeEnergy))`; the maximum-gain field records raw positive change, starting at zero. Ballistic counts cover all four probes; resting counts cover only the 181-body lattice world.

All 80 non-timing CSV fields matched between the original Phase 17 run, the instrumented run, and the final uninstrumented run. The source was restored byte-for-byte, verified by SHA-256, and the temporary source snapshot was removed. Final builds contain no diagnostic instrumentation.

The measurement establishes energy injection in the existing resting stage for this trajectory, with none inside the measured ballistic responses. It does not isolate Baumgarte, warm starting, coupled projection, or all contributions from integration/CCD. The inference is that corrected ballistic response changes the trajectory and exposes the remaining solver instability; this is not proof that Phase 17 alone resolves the lattice's energy behavior.

## Benchmark Results

Class B mathematical correctness work, with additional before/after deterministic world stability probes. Collision-heavy/separated Class D performance benchmarks were not required or run.

Mean step times above are **Measured** trajectory timings, not multi-sample benchmark medians. Contact counts and trajectories changed; these numbers do not isolate resolver cost. No performance speedup is claimed.

## Behavior Changes

- Separating and exactly tangent contacts apply no attractive normal impulse or unsupported friction.
- Friction uses current post-normal slip and a normal-supported Coulomb magnitude, permitting sticking without reversing slip through excessive material scaling.
- Finite positive friction products and limits remain valid in double; zero/negative coefficients disable friction.
- Existing restitution coefficient multiplication, zero-TOI projection code, finite-state assertions, resting friction, and event ordering remain in place.

## Known Limitations

- This is a sequential normal/tangent response, not a fully coupled block solve. Tangential response can affect normal contact velocity for off-center contacts.
- The separating guard uses the current velocity at stored anchors/normal; it does not prove that the stored event geometry or time is still valid.
- Global lattice stability worsens in the measured run despite locally dissipative ballistic responses. Resting solver stabilization/convergence remains unresolved.
- Existing effective-mass and tiny tangential-speed guards remain. Extremely large velocities can still cause the float tangential squared-speed guard to reject friction; only the material product/limit is widened here.
- Restitution values are not newly validated or thresholded; ordinary inelastic/elastic tests use products in [0, 1].
- NaN/Inf body fields remain unsupported by existing body assertions. No sanitizer, Application Verifier, or renderer acceptance run was performed.

## Out-of-Scope Findings

Current source evidence, left unchanged:

- `PhysicsSystem.cpp:357,415`: TOIs are sorted once and subsequently resolved without geometric event revalidation (`PHYS-BUG-010`, remaining Phase 25 work).
- `PhysicsSystem.cpp:612,641`: restitution product has no low-speed threshold (`PHYS-BUG-015`, Phase 18).
- `PhysicsSystem.cpp:375`: one outer solver traversal remains (`PHYS-BUG-005`, Phase 19).
- `Constraints/ConstraintPenetration.cpp:170`: Baumgarte enters physical velocity (`PHYS-BUG-008`, Phase 20). The diagnostic identifies the resting stage as an energy source without isolating each solver contribution.
- `PhysicsSystem.cpp:722,765`: conservative advancement still rewinds live bodies (`PHYS-BUG-009`, Phase 24).
- `PhysicsSystem.cpp:688`: direct zero-TOI projection remains outside normal manifold routing (`PHYS-BUG-023`).
- `PhysicsBody.cpp:291`: existing angular impulse application caps angular speed at 30 rad/s; focused fixtures stay below it.
- `PhysicsTests/PhysicsTests.vcxproj:87`: nominal Release retains `/MTd` (`PHYS-BUG-024`).
- `.gitignore:33`: pre-existing new blank line at EOF.

Production basenames above are under `GEngine/include/GEngine/Physics/` unless an explicit repository-relative path is given.

## git diff --check

Git commands use command-local `-c safe.directory=C:/dev/GEngine-physics`.

Phase-source result:

```text
git diff --check -- GEngine/include/GEngine/Physics/PhysicsSystem.cpp PhysicsTests/src/main.cpp
exit 0
```

The new review is checked with `git diff --no-index --check -- NUL docs/physics/PHASE_17_REVIEW.md`. A new-file difference can return 1 without whitespace diagnostics. LF-to-CRLF conversion notices are informational.

Repository-wide result:

```text
git diff --check
.gitignore:33: new blank line at EOF.
exit 2
```

The phase-scoped check passes. The unrelated repository-wide issue is preserved as disclosed and accepted in prior phase workflows.

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
?? docs/physics/PHASE_17_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

The index remains empty. No commit, approved tag, push, or Phase 18 work was performed.

## Human Decision

PENDING

Review the separating guard, normal-supported tangential impulse, post-normal slip calculation, material-range handling, focused tolerances, and especially the measured lattice energy and residual-motion regression with its stage diagnostic.
