# Phase 03 Review - PhysicsBody Derived Data and Transform Refactor

## Phase

Phase 03 - PhysicsBody Derived Data and Transform Refactor

## Objective

Remove repeated body-space/world-space calculations, validate the quaternion
coordinate convention with golden tests, cache derived body data with safe
invalidation, and measure the result without starting Phase 04.

## Status

AWAITING HUMAN REVIEW

## Baseline and phase-entry history

Approved checkpoint: `physics-phase-02-approved`

Checkpoint commit: `8e3a3715fe7be6aa9ea6dd9e9027e7ceb54941e2`

Branch: `physics/refactor`

Phase-entry `HEAD`: `dd2ae1fd387e2088e3aaf7e43d706885a6901a5e`

There are exactly three commits strictly between the approved checkpoint and
the Phase 03 entry commit:

```text
069d15001a4346033ea89712a8c3f988458026f7 add physics refactor plan.md
fe5eb0cb1b386451d09a099523eda6ea29647985 Revise README for GEngine physics optimization plan
b0688311056ec5962c39c0a7590a7d0fb47ef581 Refactor README structure for physics/refactor
```

The entry commit itself is:

```text
dd2ae1fd387e2088e3aaf7e43d706885a6901a5e Fix minor wording issue in README
```

The name-only diff from the checkpoint through the entry commit is:

```text
README.md
docs/physics/PHYSICS_REFACTOR_PLAN.md
```

This demonstrates that the intervening history and entry commit contain
documentation changes, not unapproved physics-source changes.

The phase-entry workspace also contained these unrelated changes. They were
preserved and excluded from Phase 03:

```text
 M RigidBodySimulation/imgui.ini
?? .AGENTS.md
?? docs/rendering/
```

## Files modified

- `GEngine/include/GEngine/Physics/Broadphase.cpp`
- `GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp`
- `GEngine/include/GEngine/Physics/Manifold.cpp`
- `GEngine/include/GEngine/Physics/PhysicsBody.cpp`
- `GEngine/include/GEngine/Physics/PhysicsBody.h`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `GEngine/include/GEngine/Physics/Shape.h`
- `GEngine/include/GEngine/Physics/ShapeBox.cpp`
- `GEngine/include/GEngine/Physics/ShapeBox.h`
- `GEngine/include/GEngine/Physics/ShapeConvex.cpp`
- `GEngine/include/GEngine/Physics/ShapeConvex.h`
- `GEngine/include/GEngine/Physics/ShapeSphere.h`
- `GEngine/include/GEngine/Shapes/Diamond.h`
- `PhysicsBenchmark/README.md`
- `PhysicsBenchmark/src/main.cpp`
- `PhysicsTests/README.md`
- `PhysicsTests/src/main.cpp`

## Files created/deleted

Created: `docs/physics/review/PHASE_03_REVIEW.md`

Deleted: none.

## Original problem and implementation

`RigidBody3D` repeatedly converted its quaternion to matrices, recomputed center
of mass transforms, recalculated/inverted body inertia, transformed inertia to
world space, and asked shapes to rebuild world AABBs. Derived values had no
body-owned cache or invalidation contract. Public body fields can be assigned
directly, and shapes can be rebuilt, so setter-only dirty flags would become
stale.

The transform convention was also inconsistent. Rendering and support mapping
used GLM's quaternion matrix as local-to-world, while several body, bounds,
constraint, center-of-mass, and inertia paths used the transpose/opposite
rotation.

Phase 03:

- Added independent caches for body/world rotation, world center of mass, body
  inertia, world inertia/inverse inertia, and world AABB.
- Compares current public pose, inverse mass, shape pointer, and shape geometry
  revision, so direct body-field assignment remains safe.
- Keeps body inertia independent from rotation-dependent world inertia, so an
  orientation-only change does not reinvert unchanged shape inertia.
- Routes broadphase, manifold, constraint, point conversion, torque response,
  and integration code through shared derived values.
- Establishes GLM's column-vector convention: `R` is local-to-world and
  `transpose(R)` is world-to-local.
- Corrects box bounds, world inverse inertia, center-of-mass transforms, and
  offset-center integration to use that convention atomically.
- Clears box local bounds before rebuilding geometry.

## Shape geometry-revision contract audit

Every supported 3D shape mutation that can alter bounds, support geometry,
center of mass, or inertia was audited:

- `ShapeSphere::m_Radius` is private. `GetRadius()` provides read access, and
  public `SetRadius()` updates the radius and calls `MarkGeometryChanged()`.
  `HandleScaleChanged()` routes through `SetRadius()`.
- All physics sphere consumers use `GetRadius()`; no public sphere-radius write
  can bypass the revision.
- `ShapeBox::m_points` and `m_bounds` are private. Public `Build()` increments
  the revision after rebuilding bounds, center of mass, and support points.
- `ShapeConvex::m_Points`, `m_Bounds`, and `m_InertiaTensor` are private.
  Public `Build()` increments the revision after rebuilding bounds, center of
  mass, support points, and inertia. `GetPoints()` returns a const reference for
  the existing diamond mesh consumer.
- Base `PhysicalShape::HandleScaleChanged()` dispatches through virtual
  `Build()` for box/convex shapes; sphere overrides it with the revisioned
  radius mutation.

A repository-wide direct-access audit found only `ShapeSphere.cpp` accessing
its own private radius. The `Breakout` `m_Radius` matches are a separate 2D game
object, not `GEngine::ShapeSphere`.

## Correctness and cache-reuse tests

The permanent executable now reports 52 checks. In addition to the prior 42,
ten assertions cover:

- initial expensive bounds/inertia cache population exactly once;
- repeated unchanged queries with no additional shape calculations;
- direct warm-cache `m_Orientation` mutation refreshing body/world rotations;
- refreshed asymmetric rotated world AABB;
- refreshed world inverse inertia;
- refreshed offset world center of mass;
- orientation-only refresh recomputing bounds once while reusing body inertia;
- post-orientation queries causing no additional expensive work;
- geometry revision causing exactly one new bounds/inertia calculation and a
  center-of-mass refresh;
- public sphere radius mutation revising and refreshing bounds/inertia/support.

The counting test shape counts calls to the expensive shape bounds and inertia
entry points. This proves reuse and exact invalidation scope rather than only
result equality.

## Performance conclusion

The primary general-workload conclusion remains unchanged: the existing
same-session, single-step compile-out comparison is effectively neutral. Its
Phase 03 changes ranged from a 0.75% improvement to a 1.95% regression, with
three of six differences at or below 0.05%. That benchmark constructs new
bodies for every sample and includes first-use cache population.

Previously recorded comparisons against Phase 02 were run in different
build/run sessions. They are contextual data only and are not evidence of a
Phase 03 speedup. No Phase 03 performance claim is based on those cross-session
numbers.

The new targeted same-session steady-state benchmark creates separated,
zero-gravity/zero-velocity box bodies, performs four unmeasured steps to
populate caches, then measures eight steps with the same bodies. The stable
fixture exercises broadphase and non-contact narrowphase/support work but
excludes first-use population and collision-response motion.

Back-to-back normal Release results using the identical driver in an isolated
approved-checkpoint build and the Phase 03 build were:

| Bodies | Phase 02 checkpoint ms | Phase 03 ms | Improvement |
|---:|---:|---:|---:|
| 50 | 0.177287 | 0.168588 | 4.91% |
| 100 | 0.358650 | 0.346313 | 3.44% |
| 200 | 0.691987 | 0.665438 | 3.84% |
| 500 | 1.724250 | 1.669363 | 3.18% |
| 1,000 | 3.609050 | 3.522638 | 2.39% |
| 2,000 | 7.319350 | 7.075213 | 3.34% |

This targeted result demonstrates steady-state reuse for unchanged bodies. It
is additional evidence and does not replace the neutral primary conclusion for
the original overlapping single-step workload.

## Workload-counter comparison

Profiling-enabled checkpoint and Phase 03 steady runs used 5 outer warmups, 25
samples, 4 in-sample warmup steps, and 8 measured steps. Additive counters were
normalized per measured step. Every listed workload counter matched exactly:

| Bodies | Dynamic/active | Pairs | Rejected | GJK calls | GJK avg/max | Support |
|---:|---:|---:|---:|---:|---:|---:|
| 50 | 25/25 | 49 | 12 | 74 | 4.108108/8 | 378 |
| 100 | 50/50 | 99 | 24 | 150 | 4.060000/8 | 759 |
| 200 | 100/100 | 199 | 50 | 298 | 3.953020/8 | 1,476 |
| 500 | 250/250 | 499 | 124 | 750 | 3.905333/8 | 3,679 |
| 1,000 | 500/500 | 999 | 250 | 1,498 | 4.034045/8 | 7,541 |
| 2,000 | 1,000/1,000 | 1,999 | 500 | 2,998 | 4.036358/8 | 15,099 |

Sleeping bodies, EPA calls, contacts, manifolds, manifold contacts, solver
constraints, and solver iterations were zero in both builds at every size, as
required by the separated fixture. Body and integrated-body counts matched.

The original overlapping profiling workload retained its expected Phase 02
signature at every size: 50% dynamic/active bodies, one candidate per two
bodies, the same static/static rejections, three GJK iterations per call, and
matching GJK/support/EPA/contact/manifold/constraint counts. Phase 03 medians
were 1.2564, 2.5448, 5.1032, 12.2992, 26.0587, and 55.6262 ms for 50 through
2,000 bodies. These profiling timings are diagnostic, not the primary result.

## Final benchmark validation

After restoring normal project generation and rebuilding the final Release
engine/benchmark, both modes reported `physics_profiling=disabled` and passed
with 5 warmups and 25 samples.

| Bodies | Original median ms | Steady median ms |
|---:|---:|---:|
| 50 | 1.301700 | 0.173563 |
| 100 | 2.678300 | 0.351612 |
| 200 | 5.333900 | 0.675813 |
| 500 | 12.967600 | 1.682737 |
| 1,000 | 27.618400 | 3.540987 |
| 2,000 | 58.066800 | 6.968525 |

Performance conclusions use the back-to-back paired table above, not these
standalone final-run values.

## Validation executed

- Generated normal and profiling-enabled Visual Studio 2022 projects: passed.
- Built the complete normal solution in Debug x64 and Release x64: passed.
- Ran Debug x64 and Release x64 `PhysicsTests`: 52/52 passed in each.
- Built and ran isolated checkpoint/current normal Release steady benchmarks
  with the same driver: passed.
- Built and ran isolated checkpoint/current profiling steady benchmarks and
  compared workload counters: passed.
- Ran the current profiling original overlapping benchmark: passed.
- Restored normal generation, rebuilt Release engine/benchmark sequentially,
  and reran both compile-out benchmark modes: passed.
- Ran the supported-shape public mutation/direct-access audit: passed.
- Ran `git diff --check`: passed; only line-ending conversion notices appeared.

The host exposes both `Path` and `PATH`. As in earlier approved phases, MSBuild
subprocesses used one normalized path entry. The first final benchmark launch
after changing from profiling to normal generation exposed a mixed-artifact
Windows side-by-side error; a sequential normal Release rebuild of GEngine and
PhysicsBenchmark resolved it, and both final modes passed.

## Known behavior changes and risks

- Physics now uses GLM's positive quaternion/local-to-world convention.
- Rotated box AABBs can differ because the old bounds path used the inverse
  rotation relative to support/rendering.
- Rotated offset-center bodies and asymmetric inertia responses can follow
  corrected trajectories.
- Rebuilding a box replaces rather than accumulates prior local bounds.
- Mutable caches are safe for current serial execution. A later parallel phase
  must establish synchronization or prepare derived data before concurrent
  reads.
- Cache source comparisons are exact by design.
- `RigidBody3D` is larger because derived matrices/source snapshots are stored
  per body; later layout work remains governed by the plan.

## Remaining issues

All Phase 04 and later requirements remain unchanged and unstarted.
Broadphase persistence/filtering, GJK/support optimization, prediction,
manifold policy, solver layout, fixed timestep, sleeping, ownership, islands,
data layout, and parallel execution remain future phases.

## git diff --stat

Phase 03 tracked paths, excluding the preserved pre-existing
`RigidBodySimulation/imgui.ini` and this untracked review file:

```text
17 files changed, 681 insertions(+), 98 deletions(-)
```

## git status --short

```text
 M GEngine/include/GEngine/Physics/Broadphase.cpp
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
 M GEngine/include/GEngine/Physics/Manifold.cpp
 M GEngine/include/GEngine/Physics/PhysicsBody.cpp
 M GEngine/include/GEngine/Physics/PhysicsBody.h
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M GEngine/include/GEngine/Physics/Shape.h
 M GEngine/include/GEngine/Physics/ShapeBox.cpp
 M GEngine/include/GEngine/Physics/ShapeBox.h
 M GEngine/include/GEngine/Physics/ShapeConvex.cpp
 M GEngine/include/GEngine/Physics/ShapeConvex.h
 M GEngine/include/GEngine/Physics/ShapeSphere.h
 M GEngine/include/GEngine/Shapes/Diamond.h
 M PhysicsBenchmark/README.md
 M PhysicsBenchmark/src/main.cpp
 M PhysicsTests/README.md
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/imgui.ini
?? .AGENTS.md
?? docs/physics/review/PHASE_03_REVIEW.md
?? docs/rendering/
```

Nothing is staged or committed. No tag was created, nothing was pushed, and
Phase 04 was not started.
