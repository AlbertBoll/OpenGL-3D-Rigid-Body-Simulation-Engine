# Phase 12 Review

## Status

PHASE 12 STATUS: AWAITING HUMAN REVIEW

## Objective

Correct default base-shape initialization and enforce finite positive sphere radii with transactional mutation and consistent geometry revisions.

## Baseline Commit

`b84632725eb9c40821b316d80ca70dd75cd6c09f` (`b846327 physics: phase 11 validate convex geometry`)

## Previous Approved Tag

`physics-phase-11-approved`

Read-only preflight verified that this tag resolves to the baseline above and is an ancestor of HEAD, and that no local Phase 12 approved tag exists. The current branch is `physics/refactor`. All pre-existing unrelated changes were preserved.

## Audit Findings Addressed

- `PHYS-BUG-020`: base center of mass and shape type now have deterministic defaults; the default sphere initializes its sphere type; explicit construction and radius mutation reject zero, negative, NaN, and infinite radii.
- Sphere validity now explicitly checks the finite-positive-radius contract.

## Allowed Scope

- deterministic base initialization;
- default and explicit sphere construction;
- finite positive radius validation;
- transactional radius mutation and revision behavior;
- focused tests and this review.

Actual scope: 3 production files, 1 test file, and this review document; 5 files total. No build-system, scene, scaling-policy, solver, or collision-algorithm changes.

## Files Changed

- `GEngine/include/GEngine/Physics/Shape.h`
- `GEngine/include/GEngine/Physics/ShapeSphere.h`
- `GEngine/include/GEngine/Physics/ShapeSphere.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_12_REVIEW.md`

## Implementation Summary

- Added `ShapeType::Invalid = -1` without changing the numeric values of Sphere, Box, or Convex.
- Both base constructors now initialize center of mass to zero and type to Invalid through member initializers. Revision remains zero. The inherited base validity check rejects the unclassified type.
- Default sphere construction delegates to radius 1.0, producing a valid unit sphere with zero center of mass, Sphere type, and revision zero.
- Explicit sphere construction throws `std::invalid_argument` for an invalid radius. This follows the existing box-construction rejection policy and prevents a malformed sphere from being returned to callers.
- `SetRadius()` remains a void API. It validates before writing; invalid or unchanged radii leave all state and revision unchanged. A different finite positive radius commits and increments revision exactly once.
- Existing valid sphere inertia, bounds, support, and collision formulas are unchanged.
- The existing scale callback still calls SetRadius. Its invalid results are now rejected, while the absolute-versus-multiplicative scaling policy remains Phase 29 work.

## Tests Added / Modified

Added `TestSphereAndBaseValidityContract()` with 21 checks and a `--sphere-validity` focused mode. The focused mode also runs the existing radius-cache regression and both degenerate-GJK checks, totaling 24 checks.

Coverage includes:

- base default and mesh constructors: zero center/revision, Invalid type, invalid status, and retained mesh source points;
- valid default and explicit spheres, including analytic inertia, support, bounds, and default-sphere contact dispatch;
- constructor rejection of +0, -0, negative radius, NaN, positive infinity, and negative infinity;
- rejection of each invalid radius after warming body bounds, inertia, inverse inertia, center, and support;
- exact preservation of committed radius, geometry revision, derived shape data, and cached body results after rejected updates;
- unchanged-radius revision behavior;
- a valid shrink after rejected mutations and the existing valid growth case, with one revision increment and refreshed analytic body data;
- invalid radius results reaching SetRadius through the existing scale callback.

The old GJK degenerate-direction test used a zero-radius sphere as a point support map. It now uses a test-only mathematical point shape marked for the generic convex query. Both original GJK assertions remain, so the new sphere contract does not remove degenerate-direction coverage.

Full-suite check count increased from 130 to 151.

Numerical comparisons use absolute tolerance `1e-5` for analytic values. Rejected-update comparisons use zero tolerance and reject non-finite values; radius and revision checks are exact. All accepted sphere geometry and contact outputs exercised by these tests remained finite.

## Validation Commands

The current generated solution uses Visual Studio/MSBuild x64. Both builds used the established child-process Path normalization:

```powershell
$physicsBuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $physicsBuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Msbuild\Current\Bin\MSBuild.exe' `
  '.\GEngine.sln' /m /nologo '/t:GEngine;PhysicsTests' `
  /p:Configuration=<Debug-or-Release> /p:Platform=x64
```

Build output was saved in ignored artifacts `bin/phase12-debug-build.log` and `bin/phase12-release-build.log`.

```powershell
.\bin\Debug\PhysicsTests\PhysicsTests.exe --sphere-validity
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --sphere-validity
.\bin\Release\PhysicsTests\PhysicsTests.exe
git diff --check -- GEngine/include/GEngine/Physics/Shape.h GEngine/include/GEngine/Physics/ShapeSphere.h GEngine/include/GEngine/Physics/ShapeSphere.cpp PhysicsTests/src/main.cpp
git diff --check
git status --short
```

Git commands used the command-local `-c safe.directory=C:/dev/GEngine-physics` option. Sandbox process/file helpers repeatedly failed at setup; edits and validation ran through explicitly approved commands outside that failing helper.

## Debug Result

- GEngine and PhysicsTests build: PASS, exit 0; 0 errors, 150 project/vendor/toolchain warning reports.
- Focused: PASS, `Sphere-validity regression: 24 checks passed`.
- Full suite: PASS, `PhysicsTests: 151 checks passed`.
- No new non-finite tested output, checked-runtime failure, or exception escaped the focused tests.
- Existing contact-normal diagnostic remains XFAIL; body-removal diagnostic remains XPASS.

## Release Result

- GEngine and PhysicsTests build: PASS, exit 0; 0 errors, 154 project/vendor/toolchain/CRT-override warning reports.
- Focused: PASS, `Sphere-validity regression: 24 checks passed`.
- Full suite: PASS, `PhysicsTests: 151 checks passed`.
- Same non-gating diagnostics as Debug; no new optimized-build failure observed.

This is the nominal optimized Release configuration with the existing Debug static CRT `/MTd` override. Allocator/runtime behavior is not a true Release-CRT result.

## Stability Results

Not applicable to this Class B validity phase. The full suite's existing small-stack regression passed in both configurations. No separate 4x4 stack, settling, lattice, or energy measurement was required or claimed.

## Benchmark Results

Not applicable. This phase changes input validity and initialization, not physics performance. No performance measurement or improvement is claimed.

## Behavior Changes

- Default spheres reliably dispatch as spheres.
- An unclassified base-derived shape reports invalid until its type is assigned or a derived validity contract overrides the base check.
- Invalid explicit sphere construction now throws instead of producing malformed geometry.
- Invalid SetRadius calls are no-ops. Same-radius calls no longer increment revision.
- Different valid radii still refresh body caches through the existing revision mechanism.

## Known Limitations

- Radius validation means finite and strictly positive. It does not introduce a supported engine-scale range; extreme finite radii may still underflow/overflow the unchanged floating-point inertia or collision formulas. Tests exercise ordinary engine-scale radii, not the entire positive floating-point range.
- Sphere support retains its existing caller contract for finite query inputs and a unit direction.
- SetRadius preserves the existing void return type; rejected input is observable through unchanged radius/revision, without a new error-return API.
- No ASan or Application Verifier was configured. Validation used Debug checked-runtime execution and nominal Release.
- Repository-wide whitespace validation still reports the unrelated pre-existing .gitignore issue below.

## Out-of-Scope Findings

- `ShapeSphere.h:19-21` still multiplies current radius by the scale callback's X component. Absolute scaling and initial scene-scale synchronization remain assigned to Phase 29.
- `_Scene.cpp:464` constructs a sphere directly from fixture radius without a scene-level error report/recovery path. Invalid input now raises the constructor exception, as invalid box construction already can; scene-level handling was not added.
- Extreme-scale floating-point limitations remain in the unchanged `ShapeSphere.cpp:33-37` inertia expression and analytic sphere collision arithmetic. This phase establishes the specified radius-domain contract, not a global numerical-scale policy.
- The full suite still reports `contact_pair_order_normal_dot=-1` for the reordered contact-normal diagnostic, assigned to Phase 15.
- Pre-existing .gitignore, simulation, skill/instruction, probe, audit/planning, historical-review, and rendering files remain outside Phase 12 ownership.

## git diff --check

Phase 12 tracked-file check: PASS, exit 0, no whitespace errors. The untracked review document produced no whitespace diagnostics under git diff --no-index --check against NUL; exit 1 indicates that the new file differs from NUL. Git emitted only its line-ending conversion notices for the four tracked phase files.

Required repository-wide result: exit 2, with this literal whitespace diagnostic:

```text
.gitignore:33: new blank line at EOF.
```

This comes from the pre-existing user-owned .gitignore edit recorded in the initial preflight. It was not changed or absorbed into Phase 12.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Shape.h
 M GEngine/include/GEngine/Physics/ShapeSphere.cpp
 M GEngine/include/GEngine/Physics/ShapeSphere.h
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_12_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

Only the five paths listed under Files Changed belong to Phase 12. No files were staged, committed, tagged, or pushed.

## Human Decision

PENDING
