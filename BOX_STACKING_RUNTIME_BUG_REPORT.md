# Box-Stacking Runtime Bug Report

## Status

AWAITING MANUAL RUNTIME VALIDATION

## Most likely failing expression

The code-confirmed invalid-index path in the pre-fix implementation was the
unchecked `triangles[idx]` access in `EPA_Expand()`, both in the expansion loop
and in the final projection. `ClosestTriangle()` initialized `idx` to `-1` and
returned it when the triangle vector was empty (or when no usable face was
found). `RemoveTrianglesFacingPoint()` could erase every triangle and the EPA
loop would then continue to its final projection, making `triangles[-1]` the
most likely expression behind MSVC's `vector subscript out of range` assertion.

The graphical assertion was not captured with a usable automated stack trace,
so this is reported as the most likely failing expression established from the
actual reachable code path, not as a debugger-confirmed instruction address.
`ShapeBox::Support()` also had an independent unchecked `m_points[0]` access,
but the active scene path normally constructed eight corners; it was still
unsafe for default-constructed or invalidly rebuilt boxes and is corrected.

## Root-cause chain

1. The box-stack scene creates exact face-to-face contacts. These contacts can
   produce duplicate, coplanar, or lower-dimensional GJK simplex points.
2. The old simplex duplicate test inspected all four array slots rather than
   only the active `[0, numPts)` range, so inactive storage could affect simplex
   growth.
3. The old GJK-to-EPA handoff filled a tetrahedron without verifying that the
   support points were finite, distinct, and affinely independent.
4. EPA used triangle point indices, normals, and signed distances without
   validating indices, face area, or finiteness.
5. EPA mutated its only face list in place. An expansion could remove all
   faces, fail to construct a valid horizon, and leave no valid prior polytope
   to use or restore.
6. `ClosestTriangle()` could consequently return `-1`; callers indexed the
   triangle vector without checking that result. EPA also had no iteration
   bound or convergence tolerance.
7. GJK ignored EPA's result and reported collision success even when contact
   witnesses were invalid or had never been initialized.
8. Separately, box construction accepted empty, non-finite, or degenerate
   geometry, and `ShapeBox::Support()` assumed `m_points[0]` existed.
9. Narrowphase trusted broadphase body indices before indexing the body vector.

## Correction

- `ShapeBox` now requires finite geometry with non-zero extents on all three
  axes, builds corners transactionally, preserves prior valid geometry on a
  failed rebuild, exposes a validity invariant, and checks that invariant
  before support mapping. The default constructor is deleted.
- `_Scene::OnPhysics3DStart()` now verifies the mesh component, geometry
  pointer, and scaled geometry before creating the runtime box body. Invalid
  scene geometry never reaches collision detection.
- Simplex `HasPoint()` now accepts `numPts` and inspects only active points.
  Exact-contact simplexes are expanded only with finite, non-duplicate points
  that increase the simplex dimension.
- EPA validates every triangle's point indices, distinct vertices,
  scale-aware non-zero area, finite unit normal, and finite signed distance.
  Every `ClosestTriangle()` result is range-checked before indexing.
- EPA requires a closed valid polytope throughout. Expansion is performed on
  candidate point/face vectors and committed only after all replacement faces
  are valid and close the hull. If an expansion removes all faces or cannot
  build a valid horizon, EPA returns explicit failure while the previous valid
  polytope remains untouched.
- EPA is limited to 64 iterations and uses an absolute/relative convergence
  tolerance. It never projects from an empty or invalid polytope, and its final
  barycentric projection and contact witnesses must be finite and geometrically
  consistent.
- GJK now returns `Separated`, `Contact`, or `Failed`. The collision caller
  propagates `Failed` as no generated contact instead of fabricating or using
  invalid witnesses.
- Physics narrowphase validates broadphase indices, distinct bodies, body
  pointers, shape pointers, and shape validity before indexing or dispatch.

## Files modified

- `GEngine/include/GEngine/Physics/GJK.cpp`
- `GEngine/include/GEngine/Physics/GJK.h`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `GEngine/include/GEngine/Physics/Shape.h`
- `GEngine/include/GEngine/Physics/ShapeBox.cpp`
- `GEngine/include/GEngine/Physics/ShapeBox.h`
- `GEngine/src/Scene/_Scene.cpp`
- `PhysicsTests/src/main.cpp`
- `PhysicsTests/README.md`
- `BOX_STACKING_RUNTIME_BUG_REPORT.md`

## Regression tests added

The headless physics suite now covers:

- rejection of empty box geometry and transactional box rebuild behavior;
- one dynamic box initially touching a static floor;
- two boxes touching face-to-face;
- two slightly penetrating boxes;
- two separated boxes with finite closest points;
- rotated box contact;
- a four-box exact-contact stack simulated for 240 steps;
- the pre-existing coincident, swept, separated, and overlapping sphere cases.

## Automated validation results

- Debug x64 `PhysicsTests`: passed, 77 checks.
- Release x64 `PhysicsTests`: passed, 77 checks.
- Debug x64 full `GEngine.sln` build: MSBuild exit code 0.
- Release x64 full `GEngine.sln` build: MSBuild exit code 0.
- Profiling-enabled Release `PhysicsBenchmark` default validation: passed for
  50, 100, 200, 500, 1000, and 2000 bodies.
- `git diff --check`: passed.

The builds continue to emit existing third-party/compiler warnings, duplicate
`ShapeConvex` link warnings, and ignored post-build Robocopy messages for
missing `GEngineEditor/PostBuildCopy*` directories. They did not fail either
solution build.

## Exact manual verification steps

1. Open `GEngine.sln` in Visual Studio.
2. Select `Release` and `x64`, and set `RigidBodySimulation` as the startup
   project.
3. In `RigidBodySimulation/src/RigidBodySimulation.cpp`, temporarily set
   `activate_boxes_stacking` to `1` and `activate_sphere_lattice` to `0`.
4. Rebuild `RigidBodySimulation` in Visual Studio.
5. Start under the Visual Studio debugger with the working directory set to
   `$(ProjectDir)` so the runtime assets resolve.
6. Let the exact-contact stack simulate for at least 60 seconds. Confirm that
   no Microsoft Visual C++ Runtime Library assertion appears, no box state
   becomes non-finite, and contacts continue to resolve as the stack settles.
7. Repeat once with a breakpoint on `_wassert` (or Visual Studio's C++ runtime
   assertion break enabled) and confirm it is not hit.
8. Stop the program and restore the two activation macros to their original
   values (`activate_boxes_stacking = 0`, `activate_sphere_lattice = 1`) before
   reviewing the working tree.

The graphical Release scene has not been claimed as passing; it remains the
required human validation.
