# Phase 02 Review - Mathematical Correctness and Numerical Robustness

## Phase

Phase 02 - Mathematical Correctness and Numerical Robustness

## Objective

Correct confirmed degenerate-math and solver defects before performance
refactors, add Debug finite-state assertions, and prove the corrected behavior
with permanent headless regressions.

## Status

APPROVED

Approved by human reviewer on 2026-09-01.

Checkpoint commit: `physics: phase 02 fix math robustness`

Checkpoint reference: `physics-phase-02-approved`

## Baseline commit

`97dbadbce1fa35e11196b04e9c5d8a58e226156f`

Approved checkpoint: `physics-phase-01-approved`

Branch: `physics/refactor`

At phase entry, the checkpoint tag resolved to the approved Phase 01 commit.
The branch `HEAD` was `e200a93ad0e6cff7cdd042f89e4e8437574149bf`,
one later documentation-only commit that modifies `README.md`. That commit was
preserved and does not change the approved physics source.

Pre-existing workspace inputs/changes were also preserved and excluded from
Phase 02:

```text
 M RigidBodySimulation/imgui.ini
?? .AGENTS.md
?? docs/physics/PHYSICS_REFACTOR_PLAN.md
```

## Files modified

- `GEngine/include/GEngine/Math/Math.h`
- `GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp`
- `GEngine/include/GEngine/Physics/GJK.cpp`
- `GEngine/include/GEngine/Physics/GJK.h`
- `GEngine/include/GEngine/Physics/Manifold.cpp`
- `GEngine/include/GEngine/Physics/PhysicsBody.cpp`
- `GEngine/include/GEngine/Physics/PhysicsBody.h`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `GEngine/include/GEngine/Physics/ShapeBox.cpp`
- `GEngine/include/GEngine/Physics/ShapeConvex.cpp`
- `premake5.lua`

## Files created

- `PhysicsTests/README.md`
- `PhysicsTests/src/main.cpp`
- `docs/physics/review/PHASE_02_REVIEW.md`

## Files deleted

None.

## Original problem

Confirmed ordinary and degenerate inputs could divide by zero or normalize a
zero value in general math, sphere collision, GJK/EPA, manifold construction,
body integration, penetration constraints, and contact resolution. A single
NaN could then spread through velocities, poses, transforms, and rendering.

Specific confirmed defects were:

- zero custom-vector and quaternion normalization;
- coincident sphere centers and zero collision directions;
- degenerate GJK line, triangle, tetrahedron, and EPA barycentric geometry;
- zero/near-zero LCP pivots and effective-mass denominators;
- Baumgarte penetration correction dividing by zero/near-zero `dt`;
- gravity computing `1 / inverseMass` when inverse mass is zero;
- malformed Z comparison in `are_same_point`;
- no permanent regression target or Debug body-state finite assertions.

## Root cause

The math and physics code assumed normalized, nondegenerate inputs and positive
denominators. Those assumptions do not hold for valid collision scenarios such
as coincident centers, stationary swept spheres, collapsed simplexes, immovable
bodies, or a zero solver timestep. Existing NaN checks were indirect and
inconsistent, and most callers had no deterministic fallback policy.

## Implementation approach

- Added explicit finite predicates, shared numerical epsilons, deterministic
  vector fallbacks, and identity quaternion fallback helpers.
- Made custom `Vector2`, `Vector3`, and `Quaternion` normalization return finite
  zero/identity values for zero or invalid lengths.
- Guarded terrain and GJK/EPA barycentric denominators; degenerate cases select
  a deterministic first-point weight rather than divide by zero.
- Corrected `are_same_point` so X, Y, and Z all use the epsilon comparison.
- Made GJK/support/manifold/convex direction normalization deterministic for
  degenerate vectors without changing the GJK algorithm or iteration policy.
- Made coincident sphere contacts choose a deterministic unit axis and retain
  finite witness points, normals, and separation.
- Replaced the gravity mass reciprocal with the algebraically equivalent
  acceleration update for positive inverse mass and a no-op for zero inverse
  mass.
- Returned zero inverse inertia for immovable or singular-inertia bodies.
- Skipped invalid/near-zero LCP pivots, contact effective-mass divisions,
  friction mass divisions, and positional projection mass divisions.
- Disabled Baumgarte velocity correction for zero/near-zero/non-finite `dt`.
- Added Debug finite assertions at physics-step boundaries and body mutation
  boundaries; the assertion calls compile to inline no-ops in Release.
- Added a standalone headless `PhysicsTests` target with a simple process-exit
  regression harness.

## Correctness changes

- Zero custom vectors normalize to zero rather than NaN.
- Zero custom and GLM quaternions fall back to identity rather than NaN.
- Coincident sphere contacts now return finite deterministic contacts.
- Degenerate GJK/EPA projection weights remain finite.
- Zero/near-zero solver divisions now contribute no impulse/correction instead
  of producing NaN/Inf.
- A body with `inverseMass == 0` no longer computes infinite mass during
  gravity and remains finite.
- Singular inertia tensors produce zero angular response rather than invalid
  inverse matrices.
- Generated non-finite contacts are asserted in Debug and rejected before they
  reach manifolds in non-asserting configurations.

The existing quaternion coordinate/transposition convention was deliberately
not changed. All existing local/world multiplication and transpose choices
remain in place; only zero/invalid quaternion fallback was added.

## Performance changes

No optimization or speedup is claimed. Required finite checks and guarded
normalization add work to collision-heavy paths.

Against the approved Phase 01 profile-enabled baseline, median external step
time regressed by 5.23%-7.94% across 50-2,000 bodies. The normal compile-out
comparison regressed by 10.27%-14.48% on this run. The profile-enabled subsystem
comparison was more internally consistent and shows the added cost distributed
through support/GJK, EPA, manifold, solver, and integration work.

Correctness has priority over this measured cost. Later optimization phases may
recover it, but Phase 02 does not move or relax numerical guards merely to
improve the benchmark.

## Architecture changes

- Added shared low-level finite/fallback math helpers.
- Added `RigidBody3D::HasFiniteState` and a Debug assertion boundary.
- Added one permanent headless physics regression executable.
- Exposed the already-existing signed-volume/barycentric GJK helpers in
  `GJK.h` so degenerate denominator behavior can be tested directly.

No derived-data caching, transform-convention refactor, broadphase redesign,
GJK hot-loop optimization, support mapping optimization, timestep change,
ownership change, sleeping, islands, or parallelism was started.

## Tests added

`PhysicsTests` runs 28 checks covering:

- zero `Vector2`, `Vector3`, custom quaternion, GLM vector, and GLM quaternion
  normalization;
- zero-normal orthogonal-basis construction;
- degenerate terrain barycentric interpolation;
- degenerate GJK line/triangle/tetrahedron and EPA barycentric weights;
- corrected duplicate-point Z epsilon behavior;
- explicit rejection of NaN/Inf by finite validation;
- zero and near-zero LCP pivots plus an ordinary finite pivot control;
- coincident static and swept sphere contacts;
- ordinary separated and overlapping sphere collision controls;
- degenerate GJK closest-point/search termination;
- body integration from a zero quaternion;
- zero/near-zero penetration-constraint timesteps;
- zero effective-mass contact and constraint solving;
- zero inverse mass under gravity;
- an ordinary one-step gravity trajectory control.

## Tests executed

- Generated Visual Studio 2022 projects in normal and profiling modes: passed.
- Built and ran `PhysicsTests` in Debug x64: 28/28 checks passed.
- Built and ran `PhysicsTests` in Release x64: 28/28 checks passed.
- Ran the compile-out Release benchmark with 5 warmups and 25 samples at all
  six required body counts: passed.
- Ran the profiling-enabled Release benchmark with 5 warmups and 25 samples at
  all six required body counts: passed with unchanged workload counters.
- Regenerated normal projects and forced a normal Release benchmark rebuild so
  profiling was definitively compiled out: passed.
- Ran `git diff --check`: passed; only line-ending conversion notices were
  emitted.

A preliminary full Debug build used an intentionally minimal normalized
`PATH`; compilation completed, but existing application post-build commands
could not find Python and returned exit 9009. Repeating with `C:\Python310` in
the normalized subprocess path passed. This was an environment invocation
issue, not a source/build failure.

## Build results

Premake generation passed. MSBuild used the installed Visual Studio 2022 MSBuild
with a normalized process `Path` outside the restricted sandbox because the
host exposes duplicate `Path`/`PATH` keys and MSVC FileTracker requires system
access.

## Debug x64 result

Passed. The final normal full-solution build completed with exit code 0 and no
errors emitted by the errors-only logger. The final Debug regression executable
reported:

```text
PhysicsTests: 28 checks passed
```

## Release x64 result

Passed. The final normal full-solution build completed with exit code 0 and no
errors emitted by the errors-only logger. The final Release regression
executable reported:

```text
PhysicsTests: 28 checks passed
```

The final forced benchmark rebuild identified itself as
`physics_profiling=disabled`, confirming that the normal generated project
compiled instrumentation out.

## Benchmark before

Approved Phase 01 compile-out median external step times:

| Bodies | Median ms |
|---:|---:|
| 50 | 1.1788 |
| 100 | 2.3957 |
| 200 | 4.8769 |
| 500 | 11.8962 |
| 1,000 | 24.5528 |
| 2,000 | 51.0928 |

Approved Phase 01 profile-enabled median external step times were 1.2191,
2.5228, 5.0938, 12.4219, 25.4657, and 53.6605 ms respectively.

## Benchmark after

Final Phase 02 compile-out Release results (5 warmups, 25 samples):

| Bodies | Median ms | Change vs Phase 01 |
|---:|---:|---:|
| 50 | 1.3495 | +14.48% |
| 100 | 2.7001 | +12.71% |
| 200 | 5.3778 | +10.27% |
| 500 | 13.1842 | +10.83% |
| 1,000 | 27.5329 | +12.14% |
| 2,000 | 56.8740 | +11.32% |

Final Phase 02 profile-enabled Release results:

| Bodies | Median ms | Change vs Phase 01 | GJK avg/max | Contacts |
|---:|---:|---:|---:|---:|
| 50 | 1.2829 | +5.23% | 3.00 / 3 | 19 |
| 100 | 2.6910 | +6.67% | 3.00 / 3 | 38 |
| 200 | 5.4303 | +6.61% | 3.00 / 3 | 75 |
| 500 | 13.4077 | +7.94% | 3.00 / 3 | 188 |
| 1,000 | 27.3827 | +7.53% | 3.00 / 3 | 375 |
| 2,000 | 57.0386 | +6.30% | 3.00 / 3 | 750 |

Candidate pairs, rejections, GJK/support/EPA calls, contacts, manifolds, solver
constraints, and solver passes exactly matched the Phase 01 fixture at every
body count. This is evidence that the benchmark collision workload did not
unexpectedly change.

## Percentage improvement

Not applicable. Phase 02 is a correctness phase and produced a measured
regression rather than an improvement. The profile-enabled external regression
range is 5.23%-7.94%; the compile-out external regression range is
10.27%-14.48% in the recorded run.

## Known behavior changes

- Perfectly coincident/zero-direction cases use deterministic X-axis fallbacks.
- Degenerate barycentric interpolation uses the first point's weight/value.
- Zero or near-zero solver pivots/effective masses apply no impulse.
- Zero or near-zero `dt` applies no Baumgarte velocity correction.
- Zero inverse mass receives no gravity acceleration.
- Invalid generated contacts are discarded after a Debug assertion boundary.

Ordinary sphere overlap/separation and the one-step gravity trajectory are
covered and unchanged. Quaternion coordinate convention is unchanged.

## Known risks

- The deterministic X-axis fallback can introduce directional bias in perfectly
  symmetric coincident configurations; it is stable and finite but not unique.
- The shared `1e-6` epsilon is absolute. Later scale-aware GJK work remains a
  Phase 05 requirement and was not started here.
- Finite guards measurably slow the contact-heavy benchmark.
- Debug assertions use the engine's existing logging/debug-break mechanism and
  therefore assume normal Debug logger initialization.
- GJK/EPA iteration limits remain governed by Phase 05 and were not added.

## Remaining issues

All Phase 03 and later requirements remain unchanged and unstarted. In
particular, quaternion/transposition convention validation, derived-data
caching, broadphase persistence, GJK allocation/iteration optimization, support
mapping specialization, pure prediction, persistent-manifold changes, solver
data-layout work, fixed stepping, sleeping, ownership, and parallel execution
remain future work.

## git diff --stat

Command:

```powershell
git -c safe.directory=C:/dev/GEngine-physics diff --stat
```

Output:

```text
 GEngine/include/GEngine/Math/Math.h                | 196 ++++++++++++++++-----
 .../Physics/Constraints/ConstraintPenetration.cpp  |  45 ++++-
 GEngine/include/GEngine/Physics/GJK.cpp            |  44 ++++-
 GEngine/include/GEngine/Physics/GJK.h              |   5 +
 GEngine/include/GEngine/Physics/Manifold.cpp       |   8 +-
 GEngine/include/GEngine/Physics/PhysicsBody.cpp    | 100 +++++++++--
 GEngine/include/GEngine/Physics/PhysicsBody.h      |   8 +-
 GEngine/include/GEngine/Physics/PhysicsSystem.cpp  | 149 +++++++++++-----
 GEngine/include/GEngine/Physics/ShapeBox.cpp       |   6 +-
 GEngine/include/GEngine/Physics/ShapeConvex.cpp    |   6 +-
 RigidBodySimulation/imgui.ini                      |   6 +-
 premake5.lua                                       |  49 ++++++
 12 files changed, 497 insertions(+), 125 deletions(-)
```

The stat includes the preserved, pre-existing `RigidBodySimulation/imgui.ini`
change. Ordinary `git diff --stat` does not include the three untracked Phase 02
files. At capture time, `PhysicsTests/README.md` had 29 lines and
`PhysicsTests/src/main.cpp` had 279 lines.

## git status --short

```text
 M GEngine/include/GEngine/Math/Math.h
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
 M GEngine/include/GEngine/Physics/GJK.cpp
 M GEngine/include/GEngine/Physics/GJK.h
 M GEngine/include/GEngine/Physics/Manifold.cpp
 M GEngine/include/GEngine/Physics/PhysicsBody.cpp
 M GEngine/include/GEngine/Physics/PhysicsBody.h
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M GEngine/include/GEngine/Physics/ShapeBox.cpp
 M GEngine/include/GEngine/Physics/ShapeConvex.cpp
 M RigidBodySimulation/imgui.ini
 M premake5.lua
?? .AGENTS.md
?? PhysicsTests/README.md
?? PhysicsTests/src/main.cpp
?? docs/physics/PHYSICS_REFACTOR_PLAN.md
?? docs/physics/review/PHASE_02_REVIEW.md
```

`RigidBodySimulation/imgui.ini`, `.AGENTS.md`, and the refactor plan are not
Phase 02 deliverables and remain unmodified/untracked as applicable. Nothing is
staged or committed.
