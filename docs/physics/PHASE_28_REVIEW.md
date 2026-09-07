# Phase 28 Review

## Status

PHASE 28 STATUS: AWAITING HUMAN REVIEW

## Objective

Make EPA failure modes explicit and testable before EPA performance optimization. Preserve the existing geometry algorithm and output validation while adding per-query reasons, bounded-budget regression coverage, and aggregate iteration/failure counters.

## Baseline Commit

`d1c1e2f179717816fd83bd7caed48070f04ceb1f` (`physics: phase 27 bound GJK queries and expose termination`), branch `physics/refactor`.

The execution preflight verified the preceding approved tag at HEAD, no approved Phase 28 tag, and an empty index. Entry status and SHA-256 hashes record unrelated work; all 24 pre-existing file entries retain their content/existence state, including deletions.

## Previous Approved Tag

`physics-phase-27-approved`, at the baseline commit above.

## Audit Findings Addressed

**PHYS-BUG-018**, failure observability and safe-output regression coverage. EPA previously returned one undifferentiated failure with no iteration/failure counters. Its caller already distinguishes `GjkContactStatus::Failed` from `Separated` internally, but the boolean collision interface drops either result. Optional nested EPA diagnostics now identify the failure stage, and aggregate profiling counts failures even when callers omit diagnostics.

This phase does not claim to eliminate every missed contact. All 18 existing safe misses in the 1,200-pair seeded corpus remain and are identified as **InvalidProjection**, after the final barycentric/projection validation. The historical audit is unchanged.

## Allowed Scope

One EPA query/result boundary: three production files, one existing dedicated test file, and this review; five files total. Each production file is necessary for implementation, the public diagnostic contract, or snapshot counters. No build-system or topology optimization is included.

## Files Changed

- `GEngine/include/GEngine/Physics/GJK.cpp`
- `GEngine/include/GEngine/Physics/GJK.h`
- `GEngine/include/GEngine/Physics/PhysicsProfile.h`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_28_REVIEW.md`

## Implementation Summary

- Add `EpaTermination` and `EpaDiagnostics`, exposed as `GjkDiagnostics::epa`. Reasons are `NotRun`, `Converged`, `DuplicateSupport`, `InvalidInput`, `InvalidSimplex`, `InvalidFace`, `InvalidTopology`, `InvalidSupport`, `InvalidHorizon`, `InvalidProjection`, `InvalidWitness`, and `IterationLimit`.
- `NotRun` explicitly separates GJK-only exits, including separation and tetrahedron-seeding failure, from an attempted EPA query. Query entry resets prior diagnostics. Updating the outer GJK result preserves completed EPA diagnostics.
- Route every EPA guarded return through a typed outcome. Successful duplicate/convergence termination is published only after the existing final topology, barycentric, projection and witness checks pass. An output-validation failure replaces the provisional success reason.
- Expose the existing **64-iteration EPA cap** and an optional smaller unsigned budget on contact queries and the contact-returning boolean wrapper. Oversized requests clamp to 64. The count includes each entered expansion-loop attempt, including its failing/converging attempt; initial tetrahedron setup and GJK work are excluded.
- Initialize both EPA witness outputs to finite zero and retain transactional publication of validated witnesses. Budget exhaustion or any guard failure returns `Failed` through `GJK_GetContact` with cleared witnesses. No partial contact is used as a fallback.
- Add `epaIterationCount`, `epaMaxIterations`, and `epaFailureCount` to `PhysicsProfileSnapshot`. A local query guard publishes totals on every EPA return, including zero-iteration setup failures, independently of whether diagnostics were requested. Existing call/time counters and snapshot reset behavior remain.
- Per-query diagnostics do not depend on `GE_ENABLE_PHYSICS_PROFILING`. Aggregate counters use the existing profiler macros and existing single-thread ownership.

### Tolerance decision

The plan permits scale-aware duplicate/convergence changes **where justified**. No new numerical threshold was justified by the evidence in this phase: the analytic scaled contact regression passed against unchanged Phase 27, the seeded failures are final projection rejections, and the final contact fingerprint matches that baseline. Accordingly, this phase retains:

- relative convergence: `max(1e-6, 1e-4 * max(abs(originDistance), bias))`, in world-distance units;
- approximate duplicate distance: **0.001 world units**;
- existing face/volume degeneracy, barycentric-weight and projection-error guards.

`DuplicateSupport` is documented as approximate termination, distinct from satisfying the expansion-gap convergence test. This is an observability baseline for subsequent work, not a claim of arbitrary-scale robustness. No validation threshold was relaxed to recover a failed contact. Transactional topology copies, horizon construction and closure validation remain unchanged.

## Tests Added / Modified

Added `--epa-robustness`; its **13,370 checks** also run in the full suite. The full suite increases from **13,461 to 17,231 checks** (3,770 added checks).

- Added **80 analytic box-contact checks before production changes**. They passed against Phase 27. Scales are 0.1, 1, 10 and 100; offsets cover deep overlap, shallow overlap, exact touching and positive separation in both body orders. Accepted penetration error is **`5e-4 * scale`** world units, fixed before implementation.
- Added successful-result diagnostics, zero/one-step EPA budgets, wrapper forwarding, cleared outputs, diagnostics reset, invalid GJK bypass, oversized-budget clamping and profiling with omitted per-query diagnostics.
- Inject non-finite support at the first EPA support call after measuring the GJK/seeding call count. It returns `InvalidSupport` at iteration 1 with cleared outputs.
- A finite but extreme bias produces overflowed seed arithmetic and safely reports `InvalidSimplex` at iteration 0.
- Coincident smooth spheres explicitly exercise the nonconverging path: both default and oversized requests return `IterationLimit` at exactly **64** with cleared outputs.
- Added **72 thin-shape checks**: valid boxes with thickness ratios 1e-4, 1e-3 and 1e-2 at the four scales, with translation, rotation and both body orders. Check finite outputs, repeatability, bounded diagnostics, successful-output gating and byte-for-byte preservation of live bodies. These are fail-safe checks, not a requirement that every thin overlap produce a contact.
- Extended the existing **1,200-pair** deterministic box/convex/sphere corpus (seed `0x270027`, all nine ordered shape combinations, four scales, independent rotations and offsets). Compare repeated EPA diagnostics, require success/failure/bypass agreement with contact status, and reconcile aggregate profiler totals with all individual queries. Existing witness repeatability, GJK classification, distance and live-state checks remain.
- Add an FNV-1a fingerprint of every corpus contact status and all six witness float bit patterns, for baseline comparison. Before and after: **2991584723466465015**. This is a regression fingerprint, not a proof outside this corpus.

Measured in both final configurations:

```text
EPA_FUZZ queries=1596 iterations=19766 max_iterations=55 failures=36 reasons=402,452,328,0,0,0,0,0,0,18,0,0,
```

The histogram counts each of the 1,200 pairs once: 402 EPA bypasses, 452 relative-convergence successes, 328 duplicate-support successes, and 18 projection failures. Each contact query runs twice for repeatability, hence 1,596 actual EPA calls and 36 recorded failures. The separately forced smooth-sphere cap case is excluded from this corpus histogram.

## Validation Commands

Class B mathematical/robustness validation, plus supplemental deterministic stability compatibility checks. Inspected the current generated Visual Studio x64 projects and profiling/compiler options. The process and patch sandbox helpers repeatedly failed setup; scoped reviewed execution performed reads, phase edits and validation. Process-local Path normalization handles the existing duplicate Path/PATH environment; no system environment or build configuration was changed.

```powershell
& .agents/skills/physics-phase-execution/scripts/Get-PhysicsPhasePreflight.ps1 -RepositoryRoot (Get-Location).Path -Phase 28
$phase28Path = $env:Path
Remove-Item Env:Path
$env:Path = $phase28Path
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Debug /p:Platform=x64 /clp:ErrorsOnly
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Release /p:Platform=x64 /clp:ErrorsOnly
.\bin\Debug\PhysicsTests\PhysicsTests.exe --epa-robustness
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --epa-robustness
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=1
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=8
git diff --check
git diff --check -- GEngine/include/GEngine/Physics/GJK.cpp GEngine/include/GEngine/Physics/GJK.h GEngine/include/GEngine/Physics/PhysicsProfile.h PhysicsTests/src/main.cpp
git diff --no-index --check NUL docs/physics/PHASE_28_REVIEW.md
git status --short
```

The Release baseline build/full test ran before changes; the new analytic tests and contact fingerprint were then run against unchanged Phase 27 using `--gjk-robustness`. Baseline and final stability commands use identical configurations and fixtures. Build logs retain normal-verbosity compiler warnings.

Evidence in ignored artifacts: `bin/phase28-entry-{status,hashes}.log`, `bin/phase28-before-{build,tests,stability-1,stability-8}.log`, `bin/phase28-geometry-{build,before}.log`, `bin/phase28-final-{debug,release}-{build,focused,tests}.log`, and `bin/phase28-final-stability-{1,8}.log`.

## Debug Result

x64 engine, PhysicsTests and PhysicsBenchmark build: **pass**, exit 0. Focused **13,370/13,370**; full **17,231/17,231**, both exit 0. Existing non-gating diagnostic reports zero observed known issues. All existing focused physics regressions remain registered.

## Release Result

Nominal optimized x64 engine, PhysicsTests and PhysicsBenchmark build: **pass**, exit 0. Focused **13,370/13,370**; full **17,231/17,231**, both exit 0. The existing **Debug static CRT `/MTd` override remains**; these are not true Release-CRT allocator/runtime results. Existing compiler/vendor/linker warnings remain; no build configuration change was made.

## Stability Results

**Measured**, fixed headless geometry/materials/body creation order/gravity, dt 1/120 s for 1,200 steps (10 seconds), at one and eight solver passes. All four scenarios remain finite. **Every non-timing CSV field matches the approved Phase 27 baseline exactly** in both configurations, including energy, penetration, speeds, contact/manifold counts and solver iterations. This is a compatibility comparison; no acceptance tolerance was widened.

| Scenario / passes | Peak energy % | Final energy | Final average Y | Peak linear / angular speed | Final linear / angular speed | Moving bodies | Final manifolds / points | Peak / final floor penetration |
| --- | ---: | ---: | ---: | --- | --- | ---: | --- | --- |
| Stack / 1 | 100 | 860.015904 | 4.478818 | 0.703708 / 0.245699 | 0.322788 / 0.058755 | 3 | 19 / 72 | 0.047577 / 0.008523 |
| Stack / 8 | 100 | 863.821763 | 4.499072 | 0.065288 / 0.058103 | 0.000000 / 0.000000 | 0 | 16 / 64 | 0.000983 / 0.000891 |
| Lattice / 1 | 100 | 4278.979598 | 1.490630 | 14.400019 / 6.611325 | 6.408699 / 6.454880 | 180 | 182 / 371 | 0.198102 / 0.078580 |
| Lattice / 8 | 100 | 4168.224381 | 1.492660 | 14.300018 / 6.217107 | 5.687394 / 5.832269 | 180 | 188 / 375 | 0.181385 / 0.020635 |

The single sphere has identical results at both pass counts: peak energy 100%, final energy 17.998483, final Y 1.499874, final linear/angular speed 0.001068/0.001066, zero moving bodies, one manifold/point and peak/final floor penetration 0.000126. Moving-body thresholds are 0.05 linear/angular speed.

The free asymmetric body also matches: initial/final angular energy 5.211667/5.432790 (+4.242841%), angular-momentum magnitude change +2.635889%, peak/final angular speed 2.158473/2.038718. Existing angular-math gates pass. This existing timestep-dependent integration drift is outside EPA scope.

Extended probes use nominal Release; both configurations also run all existing stability regressions in the full physics suite. The eight-pass stack settles, while the default stack retains motion and all 180 lattice bodies remain moving, exactly as in the baseline.

## Benchmark Results

**Not available:** no performance benchmark or speedup claim. This is a failure-contract phase; iteration/failure measurements are reported above. Supplemental stability step/solver timings are diagnostic means and are not controlled performance comparisons.

## Behavior Changes

Callers can inspect the EPA failure stage separately from GJK separation/seeding, and optionally reduce the existing EPA iteration budget. Aggregate profiling exposes failures even in unchanged engine callers. Successful ordinary contact geometry retains the baseline fingerprint. No solver, geometry tolerance, topology, dispatcher or fallback policy is changed.

## Known Limitations

- Safe false negatives remain: the seeded corpus still has 18 projection failures and coincident smooth spheres can hit the 64-step cap. The phase makes these observable, without bypassing validation or inventing fallback contacts.
- The duplicate threshold remains absolute. Validated regular-shape scales are 0.1 through 100, with additional thin-shape safety cases. Arbitrarily tiny shapes or huge coordinate offsets are not covered by a scale-invariance guarantee.
- `InvalidFace`, `InvalidTopology`, `InvalidHorizon`, `InvalidProjection` and `InvalidWitness` identify guard stages; they are not a mathematical diagnosis of the underlying hull. Invalid projection combines barycentric and projection-error rejection. Not every defensive internal guard is individually forced through the public API.
- GJK tetrahedron-seeding failure remains `ContactExpansionFailed` with EPA `NotRun`; it is excluded from EPA counters. Individual support-map execution must itself terminate. Allocator failure/exception recovery is not a new contract.
- The boolean collision dispatcher still returns false for failed queries (`PhysicsSystem.cpp:620`); callers wanting a specific EPA reason use GJK diagnostics. No new collision fallback is introduced. Engine failures are visible in enabled aggregate profiling.
- Global profiler state remains single-threaded; this phase does not add reason histograms to the runtime snapshot or change profiler ownership. The test corpus prints its own reason histogram. No separate profiling-disabled build or sanitizer run was performed.

## Out-of-Scope Findings

- The existing failed-query drop remains in `GEngine/include/GEngine/Physics/PhysicsSystem.cpp:620`; detailed diagnostics do not resolve failed geometry or redesign dispatch. The following void closest-point query remains at line 626, for the later unified-query phase.
- EPA retains full transactional vector copies and quadratic edge/closure scans (`GJK.cpp:1200`, `GJK.cpp:1320`, `GJK.cpp:1492`). Scratch and topology optimizations remain later phases.
- The baseline stability limitations remain: default one-pass stack residual motion, unsettled sphere lattice and timestep-dependent free-body angular drift. No solver/integrator/sleeping change is included.
- Release CRT overrides remain in `GEngine/GEngine.vcxproj:86`, `PhysicsTests/PhysicsTests.vcxproj:87`, and `PhysicsBenchmark/PhysicsBenchmark.vcxproj:87`.
- Pre-existing `.gitignore:33` has an extra blank line at EOF; the unrelated Scene file emits the existing LF-to-CRLF warning. Both files retain their entry hashes.

## git diff --check

Literal global output:

```text
warning: in the working copy of 'GEngine/src/Scene/_Scene.cpp', LF will be replaced by CRLF the next time Git touches it
.gitignore:33: new blank line at EOF.
```

Global exit **2**, entirely pre-existing. The four phase-owned tracked files have no output, exit **0**. The new review passes the separate no-index whitespace check without whitespace diagnostics.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/GJK.cpp
 M GEngine/include/GEngine/Physics/GJK.h
 M GEngine/include/GEngine/Physics/PhysicsProfile.h
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
?? docs/physics/PHASE_28_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/rendering/
```

The index and HEAD remain unchanged. Only the five listed Phase 28 files are deliverables; ignored validation logs are retained. No commit, tag, push or next-phase work was performed.

## Human Decision

PENDING

Review the typed stage/iteration contract, safe cap behavior, unchanged duplicate/convergence policy and existing projection misses. The [physics-phase-execution skill](../../.agents/skills/physics-phase-execution/SKILL.md) requires: "Then stop. Never stage files for approval, commit, create an approved tag, push, approve the implementation, invoke the approval skill, or begin Phase XX+1."
