# Phase 29 Review

## Status

PHASE 29 STATUS: AWAITING HUMAN REVIEW

## Objective

Fix absolute scaling at the shape geometry boundary: scale `1 -> 2 -> 3` produces three times the base geometry. Preserve valid revision/cache behavior and distinguish explicit base replacement from scale callbacks.

## Baseline Commit

`edb6775104823278e18edcc819a87c4f34bb7f44` (`physics: phase 28 expose EPA failure telemetry and bounds`), branch `physics/refactor`.

## Previous Approved Tag

`physics-phase-28-approved`, verified at HEAD by the execution preflight. No Phase 29 approved tag exists. Entry status and SHA-256 records cover 24 pre-existing files, including deleted files; all remain unchanged.

## Audit Findings Addressed

The shape-level absolute-scaling portion of **PHYS-BUG-012**. The audit describes compounded sphere radii and inconsistent geometry sources. Current source inspection additionally found that `ShapeConvex::Build` replaces `m_MeshPoints` on every successful rebuild, causing subsequent inherited scale callbacks to compound their inputs. The historical audit remains unchanged.

## Allowed Scope

Five production files, one test file, one review document: seven files total. One objective at the shape construction/mutation boundary. Scene import, transform synchronization, solver equations, mass-property algorithms and performance optimization are outside this phase.

## Files Changed

- GEngine/include/GEngine/Physics/Shape.h
- GEngine/include/GEngine/Physics/Shape.cpp
- GEngine/include/GEngine/Physics/ShapeSphere.h
- GEngine/include/GEngine/Physics/ShapeSphere.cpp
- GEngine/include/GEngine/Physics/ShapeBox.cpp
- PhysicsTests/src/main.cpp
- docs/physics/PHASE_29_REVIEW.md

## Implementation Summary

- `Shape.h` documents the base-geometry contract. Successful explicit point-shape `Build` calls establish the unscaled source; scale callbacks preserve it.
- `Shape.cpp` calculates scaled points from the owned base, rejects non-finite scale/input products, and restores that base after virtual `Build`, including exceptional exits. The existing derived builders still decide geometry validity and publish revisions. This also corrects convex scaling without changing its hull or mass-property implementation.
- `ShapeSphere.h/.cpp` retain a separate base radius. Scale callbacks use `baseRadius * scale.x`; a changed `SetRadius` installs a new base and current radius. Invalid or unchanged setters remain no-ops. A changed effective radius advances the geometry revision once.
- `ShapeBox.cpp` makes a successful explicit `Build` replace the source points, matching the existing convex contract. The source copy is allocated before committing geometry. Scale callbacks restore the original source afterward.

Successful box/convex rebuilds retain their existing one-revision-per-build behavior, including repeated equal scales. Sphere callbacks retain their unchanged-radius no-op behavior. No build files changed.

## Tests Added / Modified

Added `--absolute-scaling` with **147 checks**, also registered in the full suite. The suite increases from **17,231 to 17,378 checks**.

- Sphere, box and convex `1 -> 2 -> 3`, repeated `3`, shrink and reset sequences.
- Nonuniform point scaling and signed reflection; sphere's existing X-axis-only nonuniform policy.
- Exact expected radius/local AABB values; finite support, rotated world bounds, COM and inverse-inertia refresh after warming body caches.
- Caller-owned point mutation cannot change the shape's source.
- Explicit radius/point replacement establishes the new base; unchanged radius and rejected radius/point replacements preserve it. Offset replacement geometry scales about the model origin.
- Zero, NaN, infinity and overflowed scale rejection, unchanged revisions/caches, and successful recovery using the original source. Negative sphere scale is rejected; negative nondegenerate point scale remains supported.
- Injected virtual rebuild exception propagates and preserves the original point source for the next successful scale.

Radius and tested local AABB comparisons use **zero tolerance** on exactly representable fixtures. Sphere analytic/cache comparisons use **1e-5**. Point-shape support, COM, rotated bounds and inertia comparisons use **2e-4**. Convex COM/inertia compare against independently built equivalent geometry, preserving the existing approximate mass-property algorithm; this is not an analytic convex inertia acceptance test.

Before production edits, the first 145 checks were built and executed against Phase 28: **50 failed**, reproducing sphere/convex compounding and box explicit-base replacement failures. An initial test draft assumed exact centered convex COM; source inspection showed existing biased sampling, so the regression was corrected to fresh-build equivalence before the recorded red run. The additional two checks exercise source restoration on exception.

## Validation Commands

Class B mathematical/geometry validation. Inspected current generated Visual Studio/MSBuild x64 projects. Sandbox process and patch helpers failed setup; scoped reviewed shell execution performed phase edits and validation. Process-local Path normalization handles the existing duplicate Path/PATH environment; no system environment or build configuration changed.

```powershell
& .agents/skills/physics-phase-execution/scripts/Get-PhysicsPhasePreflight.ps1 -RepositoryRoot (Get-Location).Path -Phase 29
$phase29Path = $env:Path
Remove-Item Env:Path
$env:Path = $phase29Path
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Debug /p:Platform=x64 /clp:ErrorsOnly
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Release /p:Platform=x64 /clp:ErrorsOnly
.\bin\Debug\PhysicsTests\PhysicsTests.exe --absolute-scaling
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --absolute-scaling
.\bin\Release\PhysicsTests\PhysicsTests.exe
git diff --check
git diff --check -- GEngine/include/GEngine/Physics/Shape.h GEngine/include/GEngine/Physics/Shape.cpp GEngine/include/GEngine/Physics/ShapeSphere.h GEngine/include/GEngine/Physics/ShapeSphere.cpp GEngine/include/GEngine/Physics/ShapeBox.cpp PhysicsTests/src/main.cpp
git diff --no-index --check NUL docs/physics/PHASE_29_REVIEW.md
git status --short
```

Ignored evidence: `bin/phase29-entry-status.log`, `bin/phase29-entry-hashes.log`, `bin/phase29-red-{build,focused}.log`, and `bin/phase29-final-{debug,release}-{build,focused,tests}.log`. Final build commands also record normal-verbosity file logs with `/fl /flp:logfile=...;verbosity=normal`.

## Debug Result

x64 GEngine, PhysicsTests and PhysicsBenchmark build: **pass**, exit 0, zero build errors. Focused **147/147** and full **17,378/17,378**, both exit 0. Existing diagnostic reports zero observed known issues. Existing compiler/vendor warnings remain.

## Release Result

Nominal optimized x64 GEngine, PhysicsTests and PhysicsBenchmark build: **pass**, exit 0. Focused **147/147** and full **17,378/17,378**, exit 0. Existing diagnostic reports zero observed known issues. The **Debug static CRT `/MTd` override remains**, confirmed by D9025 warnings; allocator/runtime results are not a true Release-CRT comparison. Existing compiler/vendor warnings remain.

## Stability Results

The existing full-suite box contact, position stabilization, friction, sphere-drop and angular-dynamics regressions remain enabled and pass in both Debug and nominal Release. No solver constants or stability thresholds changed.

**Measured**, identically in the Debug and nominal Release full suites: the existing penetrated-stack fixture reports peak kinetic energy **0**, final maximum depth **0.0200024**, final average Y **4.45**, with identical repeated runs. The 120 Hz high sphere drop reports peak/initial energy **18/18**, maximum penetration **0.000192761**, final-window linear/angular speed **0.000394938/0.00039415**, one manifold/point and finite state. These are existing regression fixtures, not the separate audit benchmark workloads.

The existing 1,200-pair collision corpus retains fingerprint **2991584723466465015**, matching the Phase 28 review, with EPA **1,596 calls**, **19,766 iterations**, maximum **55**, and **36** safe failures across repeated queries. Shape scaling changes are intentional; unchanged-geometry collision behavior retains the existing reference.

No standalone 10-second stack/lattice benchmark was required or run for this geometry contract phase. No new settling or energy-conservation claim is made for bodies resized during motion.

## Benchmark Results

**Not available:** no performance benchmark or speedup claim. This is a correctness phase. Convex rebuilding retains its existing sampled mass-property cost.

## Behavior Changes

Absolute scale is relative to the most recently established base geometry, not the previously scaled result. Changed explicit radius/point replacement starts a new base. Revision updates continue to invalidate body caches and existing manifold metadata through the approved mechanisms.

## Known Limitations

- The base is the geometry supplied by the caller. Already-scaled scene input cannot be converted back to its original mesh by this shape API.
- Spheres remain spheres using `scale.x`; ellipsoid/nonuniform radius policy is unchanged.
- Zero/invalid shape rebuilds retain the last valid geometry. Point-shape repeated equal scales still rebuild and increment revision. Exception coverage proves source restoration; it does not introduce a universal rollback guarantee for arbitrary derived builders that throw after committing their own geometry.
- Existing convex approximation and off-center box inertia semantics remain. Tests verify scaling and fresh-build equivalence without approving unrelated mass-property equations or arbitrary extreme finite scales.

## Out-of-Scope Findings

- Scene initial-scale import is still inconsistent: `GEngine/src/Scene/_Scene.cpp:464` constructs a sphere from fixture radius without transform scale; line 485 passes already-scaled mesh points into the box constructor; line 534 obtains unscaled convex points. Therefore a box imported at non-unit scale still treats that imported geometry as its base. Scene import/synchronization requires separate authorized work; the pre-existing Scene edits were preserved.
- `GEngine/include/GEngine/Component/Component.h:198` emits absolute scale, while runtime pose authority remains separate from these shape callbacks (the later Phase 30 boundary).
- Convex COM/inertia use 100 samples per axis starting at the lower bound, excluding the upper endpoint (`GEngine/include/GEngine/Physics/ShapeConvex.cpp:423`, `:439`, `:477`, `:495`). A symmetric point set therefore need not return exact zero COM. No quadrature changes were made.
- The audited off-center box parallel-axis tensor remains in `GEngine/include/GEngine/Physics/ShapeBox.cpp:215` (`PHYS-BUG-016`). Offset tests check geometry, without changing or approving that tensor.
- The Release CRT override and `.gitignore:33` trailing blank line remain pre-existing. Hash comparison confirms all 24 entry files/deletions were preserved.

## git diff --check

Literal global output:

```text
warning: in the working copy of 'GEngine/src/Scene/_Scene.cpp', LF will be replaced by CRLF the next time Git touches it
.gitignore:33: new blank line at EOF.
```

The global check exits **2** on the pre-existing `.gitignore` issue. The six phase-owned tracked files have no output, exit **0**. The new review passes the separate no-index check without whitespace diagnostics (exit 1 denotes the new-file difference). No unrelated whitespace was edited.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Shape.cpp
 M GEngine/include/GEngine/Physics/Shape.h
 M GEngine/include/GEngine/Physics/ShapeBox.cpp
 M GEngine/include/GEngine/Physics/ShapeSphere.cpp
 M GEngine/include/GEngine/Physics/ShapeSphere.h
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
?? docs/physics/PHASE_29_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/rendering/
```

The index and HEAD remain unchanged. Only the seven listed files are Phase 29 deliverables; validation logs are ignored. No commit, tag, push or next-phase work was performed.

## Human Decision

PENDING

Review the explicit base-replacement semantics, sphere X-axis policy, shape-level scope and validation evidence. The [physics-phase-execution skill](../../.agents/skills/physics-phase-execution/SKILL.md) requires: "Then stop. Never stage files for approval, commit, create an approved tag, push, approve the implementation, invoke the approval skill, or begin Phase XX+1."
