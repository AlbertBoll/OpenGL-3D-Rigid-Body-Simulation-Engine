# Phase 11 Review

## Status

PHASE 11 STATUS: AWAITING HUMAN REVIEW

## Objective

Make invalid convex geometry fail safely and transactionally without changing valid convex collision behavior.

## Baseline Commit

`d3907a377b6c3e9b018c0b7d51618e522426790a` (`d3907a3 physics: phase 10 hide mutable body storage`)

## Previous Approved Tag

`physics-phase-10-approved`

The required preflight verified that the tag resolves to the baseline commit above, is an ancestor of `HEAD`, and that no local `physics-phase-11-approved` tag exists.

## Audit Findings Addressed

- `PHYS-BUG-003`: empty, insufficient, duplicate-only, coplanar, zero-volume, and non-finite convex inputs no longer reach empty-vector indexing or divide-by-zero paths.
- Convex `IsValid()` now reflects committed hull validity rather than inheriting the base implementation that always reports true.
- Invalid initial construction remains a deterministic invalid shape, while a rejected rebuild preserves the previous valid hull and revision.

## Allowed Scope

- validate finite convex input and require four affinely independent usable points;
- build and validate hull data in temporary storage;
- guard center-of-mass and inertia sampling against invalid steps and zero samples;
- commit hull points, bounds, center of mass, inertia, source points, validity, and revision only after every stage succeeds;
- make invalid support/bounds/speed queries fail safely;
- convert the Phase 06 convex diagnostic into a focused and full-suite gating regression;
- no scene creation redesign, sphere/base-shape changes, geometry scaling changes, GJK/EPA changes, or mass-property optimization.

Actual scope is 2 production files, 1 test file, and this review document: 4 files total.

## Files Changed

- `GEngine/include/GEngine/Physics/ShapeConvex.h`
- `GEngine/include/GEngine/Physics/ShapeConvex.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_11_REVIEW.md`

## Implementation Summary

- Added deterministic default convex state: convex type, zero center/inertia, empty hull, and invalid status.
- Added a scale-aware length tolerance of `max(1e-6, maximum input AABB extent * 1e-6)`.
- Replaced the assumption-heavy tetrahedron seed with a translation-independent farthest pair, farthest-from-line point, and farthest-from-plane point selection. Each stage rejects non-finite or tolerance-degenerate geometry.
- Added final hull index, triangle-degeneracy, and non-zero signed-volume validation.
- Changed center-of-mass and inertia sampling to fixed integer loop bounds and guarded all step sizes, sample counts, and outputs before division or commit.
- Made `Build()` transactional: invalid input or derived-data failure returns without altering existing live state or incrementing the geometry revision.
- Added validity/finite-input guards to support, transformed bounds, and angular-speed queries. Invalid support returns an explicit non-finite sentinel without indexing hull storage.
- Removed helper routines made obsolete by the safer tetrahedron selection.

## Tests Added / Modified

Converted the former non-gating convex XFAIL diagnostic into `TestConvexValidityContract()` and added `--convex-validity` focused mode.

The 11 focused checks cover:

- default, empty, and fewer-than-four-point construction;
- fewer than four usable unique points;
- five finite unique points on one line;
- a tilted coplanar/zero-volume point set with non-zero AABB extent on every axis;
- NaN input plus explicit positive- and negative-infinity inputs;
- no partial hull commit for invalid construction;
- safe direct support calls on invalid shapes;
- valid finite 3D hull construction and derived data;
- exactly one geometry-revision increment after one successful valid rebuild;
- preservation of all valid state and revision after subsequent rejected coplanar and non-finite rebuilds;
- safe rejection of a non-finite support direction.

The full suite test count increased from 119 to 130 checks. The convex known-issue diagnostic was removed; the remaining observed XFAIL is the contact-pair normal issue assigned to Phase 15. The human-review regression additions changed only `PhysicsTests/src/main.cpp`; they exposed no production defect and required no production-scope expansion.

## Validation Commands

The established child-process `Path` normalization was retained for both builds:

```powershell
$physicsBuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $physicsBuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Msbuild\Current\Bin\MSBuild.exe' `
  '.\GEngine.sln' /m /nologo '/t:GEngine;PhysicsTests' `
  /p:Configuration=<Debug-or-Release> /p:Platform=x64
```

Focused and full tests:

```powershell
.\bin\Debug\PhysicsTests\PhysicsTests.exe --convex-validity
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --convex-validity
.\bin\Release\PhysicsTests\PhysicsTests.exe
```

Repository checks:

```powershell
git diff --check -- GEngine/include/GEngine/Physics/ShapeConvex.h GEngine/include/GEngine/Physics/ShapeConvex.cpp PhysicsTests/src/main.cpp
git diff --check
git status --short
```

## Debug Result

- Build: PASS, exit 0, with existing project/vendor/toolchain/linker warnings.
- Focused convex-validity regression: PASS, `11 checks passed`.
- Full physics tests: PASS, `130 checks passed`.
- Diagnostics: one later-phase contact-normal XFAIL observed; no convex XFAIL remains.
- No NaN/Inf escaped from accepted convex geometry, and invalid direct support requests returned the expected non-finite failure sentinel without a checked-runtime failure.

## Release Result

- Build: PASS, exit 0, with existing project/vendor/toolchain/linker and CRT-override warnings.
- Focused convex-validity regression: PASS, `11 checks passed`.
- Full physics tests: PASS, `130 checks passed`.
- Diagnostics: one later-phase contact-normal XFAIL observed; no convex XFAIL remains.
- No optimized-build crash or invalid convex state commit was observed.

This remains the nominal optimized Release configuration with the repository's documented `/MTd` override; allocator/runtime behavior is not a true Release-CRT result.

## Stability Results

Not applicable. Phase 11 changes convex construction/query safety only and does not alter integration, contact constraints, friction, stabilization, or solver iteration policy.

## Benchmark Results

Not applicable. Phase 11 is a Class A/B safety and mathematical-validity phase, not a performance phase.

## Behavior Changes

- Invalid convex shapes now report `IsValid() == false` and retain no partial hull data.
- Valid convex shapes continue to provide finite support, bounds, center-of-mass, and inertia data.
- Rebuilding a valid convex shape with invalid data is a no-op, including no revision increment.
- Direct invalid support requests return a NaN sentinel instead of indexing an empty hull.
- Validity rejects dimensions or affine separation at or below the documented scale-aware tolerance.

## Known Limitations

- `_Scene.cpp:541` may still attach an invalid `ShapeConvex` to a body without surfacing a creation error. The shape is now observable as invalid and its direct query paths are safe; scene-level error reporting is outside the Phase 11 two-file production boundary.
- Convex center-of-mass and inertia still use the existing 100 x 100 x 100 sampling method. Its startup cost/accuracy is unchanged and remains future `PHYS-PERF-009` work.
- Absolute runtime geometry scaling remains assigned to Phase 29.
- Base and sphere default/validity semantics remain assigned to Phase 12.
- No ASan or Application Verifier configuration was available; validation used the checked Debug runtime plus deterministic regressions.

## Out-of-Scope Findings

- The reordered contact-normal diagnostic remains an expected failure with `contact_pair_order_normal_dot=-1`; it is assigned to Phase 15 and was not modified.
- `ShapeConvex.cpp` retains a pre-existing `size_t` to `int` warning in `FindPointFurthestInDir`; changing broad container/index conventions is not required for the Phase 11 safety contract.
- Pre-existing `.gitignore`, `RigidBodySimulation.cpp`, repo-local skill, audit/planning/rendering, and probe changes were preserved unchanged.

## git diff --check

Phase 11 scoped result: PASS, exit 0. Git emitted only line-ending conversion notices for the three tracked Phase 11 files; no whitespace error was reported.

The required repository-wide command returned exit 2 solely for the pre-existing user-owned `.gitignore` edit:

```text
.gitignore:33: new blank line at EOF.
```

That file was present in the initial Phase 11 ownership boundary and was preserved unchanged.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/ShapeConvex.cpp
 M GEngine/include/GEngine/Physics/ShapeConvex.h
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_11_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

The Phase 11 entries are the two convex production files, the physics test file, and this review document. Every other entry was present in the initial preflight and was preserved unchanged.

## Human Decision

PENDING
