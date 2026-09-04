# Phase 06 Review

## Status

PHASE 06 STATUS: APPROVED

## Objective

Create a deterministic regression harness that records the current audited physics failure modes without changing production physics behavior.

## Baseline Commit

`76e3c09e50c4e535e76f3aa30449850c051000d4` (`76e3c09 add three test cases`)

## Previous Approved Tag

Independent rollback baseline: `physics-refactor-v2-baseline`

The tag points to the baseline commit above and was pushed to `origin` before Phase 06 files were modified.

## Audit Findings Addressed

This phase does not fix an audited physics bug. It adds baseline coverage for:

- `PHYS-BUG-001`: body deletion with a retained manifold;
- `PHYS-BUG-002`: physics stop/start with retained manifolds;
- `PHYS-BUG-003`: empty and fewer-than-four-point convex geometry;
- `PHYS-BUG-004`: torque-free asymmetric-body energy and angular-momentum drift;
- `PHYS-BUG-005`, `007`, and `008`: 4x4 stack stability/energy metrics;
- `PHYS-BUG-006`: reversed contact pair normal handling;
- sphere settling and sphere-lattice non-settling baselines used by later phases.

## Allowed Scope

- test/probe infrastructure only;
- no production physics behavior changes;
- no audited bug fixes;
- no Phase 07 work.

Actual scope is 0 production files, 2 test/probe source files, 1 strictly required build file, and this review document: 4 files total. This complies with the repository phase limits.

## Files Changed

- `PhysicsTests/src/main.cpp`
- `PhysicsBenchmark/src/main.cpp`
- `premake5.lua`
- `docs/physics/PHASE_06_REVIEW.md`

## Implementation Summary

- Added non-gating `XFAIL`/`XPASS` diagnostics for body-removal manifold invalidation, convex validity, and contact pair-order normalization.
- Added isolated opt-in unsafe repro modes for body removal, world restart, empty convex support, and degenerate convex support. These modes execute the unsafe operation directly rather than catching or suppressing the failure.
- Added a fixed-timestep `--physics-regression-baseline` benchmark mode covering an asymmetric free box, the audited 4x4 stack, a single falling sphere, and the 180-sphere lattice.
- Added mechanical energy, angular momentum, linear/angular speed, settling, contact, manifold, and finiteness output.
- Added `ShapeConvex.cpp` directly to the `PhysicsTests` target so the exact implementation can be exercised without introducing renderer/asset runtime dependencies into the headless test executable.

## Tests Added / Modified

The original 77 checks remain gating checks. Known broken behavior is reported separately and does not assert that incorrect behavior is correct.

Normal diagnostic output:

```text
XFAIL: body removal invalidates manifolds before deleting the body
BASELINE convex_empty_points=0 convex_empty_reports_valid=1 convex_degenerate_points=0 convex_degenerate_reports_valid=1
XFAIL: empty and fewer-than-four-point convex shapes report invalid
BASELINE contact_pair_order_normal_dot=-1
XFAIL: reordered contact preserves the canonical manifold normal direction
PhysicsTests diagnostics: 3 known issues observed in 3 non-gating diagnostics
PhysicsTests: 77 checks passed
```

Unsafe repro modes:

```text
PhysicsTests.exe --unsafe-body-removal
PhysicsTests.exe --unsafe-world-restart
PhysicsTests.exe --unsafe-empty-convex
PhysicsTests.exe --unsafe-degenerate-convex
```

## Validation Commands

Project generation retained physics profiling:

```powershell
& .\vendor\bin\premake\premake5.exe --physics-profiling vs2022
```

The host exposed duplicate `Path`/`PATH` environment entries, so each MSBuild invocation normalized `Path` in the child PowerShell process before running:

```powershell
$physicsBuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $physicsBuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Msbuild\Current\Bin\MSBuild.exe' `
  '.\GEngine.sln' /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' `
  /p:Configuration=<Debug-or-Release> /p:Platform=x64
```

Tests and probes:

```powershell
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5 --steady-state-warmup-steps=4 --steady-state-measured-steps=8
```

## Debug Result

- Build: PASS, 0 errors (70 existing/toolchain warnings reported by MSBuild).
- Physics tests: PASS, `77 checks passed`.
- Known-issue diagnostics: 3 expected failures observed.
- Body-removal unsafe repro: access violation, process exit `-1073741819` (`0xC0000005`).
- World-restart unsafe repro: access violation, process exit `-1073741819` (`0xC0000005`).

## Release Result

- Build: PASS, 0 errors (202 existing/toolchain warnings reported by MSBuild).
- Physics tests: PASS, `77 checks passed`.
- Known-issue diagnostics: the same 3 expected failures as Debug.
- No NaN/Inf occurred in the safe regression probes.

This remains a nominal optimized Release build with the repository's documented `/MTd` override; allocator/runtime behavior is not a true Release-CRT result.

## Stability Results

All scenarios used 1,200 fixed steps at `1/120` second (10 simulated seconds). State and counter results were bit-for-bit identical across two consecutive runs; wall-clock timings varied slightly.

### Asymmetric free body

- half-extents: `(1, 2, 3)`;
- initial angular velocity: `(0.7, 1.1, 1.6)`;
- gravity: zero;
- initial angular kinetic energy: `5.211667`;
- final/peak angular kinetic energy: `5.423933`;
- energy change: `+4.072900%`;
- initial angular momentum magnitude: `5.454967`;
- final angular momentum magnitude: `5.594113`;
- angular momentum magnitude change: `+2.550810%`;
- finite: yes.

### 4x4 box stack

- 16 unit-half-extent dynamic boxes, 2.01 horizontal spacing, gravity `-12`;
- initial energy: `864.000000`;
- peak energy: `908.645032` (`105.167249%` of initial);
- final energy: `648.556632`;
- final average Y: `3.377841` (partial collapse);
- peak/final maximum linear speed: `11.860434` / `0.080448`;
- peak/final maximum angular speed: `8.350631` / `0.012314`;
- moving bodies after 10 seconds: `3/16` at the documented 0.05 linear/angular thresholds;
- final manifolds/points: `23/62`;
- average generated contacts per step: `18.321667`;
- finite: yes.

### Single sphere

- radius 1, initial center Y 10, gravity `-12`;
- initial/peak energy: `120.000000` / `120.000000`;
- final energy: `18.000989`;
- final center Y: `1.500082`;
- peak/final maximum linear speed: `14.200018` / `0.000816`;
- peak/final maximum angular speed: `0.024529` / `0.000814`;
- moving bodies after 10 seconds: `0/1`;
- final manifolds/points: `1/1`;
- finite: yes.

### 180-sphere lattice

- 5 vertical layers of 6x6 radius-1 spheres at exact 2-unit spacing, gravity `-12`;
- initial/peak energy: `30240.000000` / `30240.000000`;
- final energy: `3932.502345`;
- final average Y: `1.444308`;
- peak/final maximum linear speed: `14.521975` / `12.628589`;
- peak/final maximum angular speed: `9.354573` / `6.760431`;
- moving bodies after 10 seconds: `175/180`;
- final manifolds/points: `194/710`;
- average generated contacts per step: `199.723333`;
- finite: yes.

Release probe wall-clock time per simulated step, including metric capture, ranged across the two repetitions as follows:

- asymmetric body: `0.000190-0.000194 ms`;
- box stack: `2.242955-2.274022 ms`;
- single sphere: `0.052976-0.053141 ms`;
- sphere lattice: `16.933336-16.957183 ms`.

## Contact Pair-Order Diagnostic

The direct A/B normal dotted with the reordered contact's stored canonical normal is `-1.0`. The reordered anchors/bodies are canonicalized, but the normal remains reversed. This is a non-gating expected failure for Phase 15.

## Benchmark Results

All following values are measured in the profiling-enabled nominal Release configuration. Internal stage values are arithmetic means; whole-step values are external medians over 5 measured samples after 2 warmups.

### Collision-heavy paired overlapping boxes

| Bodies | Whole-step median | Broad phase | Narrow phase | EPA | Manifold | Solver |
|---:|---:|---:|---:|---:|---:|---:|
| 100 | 4.118800 ms | 0.035920 ms | 2.210540 ms | 2.096980 ms | 0.881360 ms | 0.967660 ms |
| 1,000 | 41.747000 ms | 0.365100 ms | 21.056360 ms | 19.891780 ms | 10.120360 ms | 9.769900 ms |
| 10,000 | 448.878700 ms | 4.374600 ms | 192.562200 ms | 180.354800 ms | 145.653800 ms | 100.090380 ms |

### Separated steady-state boxes

| Bodies | Whole-step median | Broad phase | Integration | Candidates |
|---:|---:|---:|---:|---:|
| 100 | 0.021225 ms | 0.015075 ms | 0.005715 ms | 0 |
| 1,000 | 0.223500 ms | 0.158417 ms | 0.062205 ms | 0 |
| 10,000 | 2.451800 ms | 1.639337 ms | 0.838117 ms | 0 |

## Unsafe / Blocked Tests

- Body-removal and world-restart repro modes were executed in Debug and both terminated with the expected access violation. They are intentionally excluded from the normal suite.
- Empty-convex support reached the checked out-of-range path and entered the interactive MSVC diagnostic handler. The isolated test process was terminated after 30 seconds. It is blocked from unattended execution without a debugger or exception-handler configuration.
- Degenerate-convex support uses the same empty-hull access and was not separately executed after the empty case demonstrated the handler behavior.
- No sanitizer, Application Verifier, or page-heap configuration is available in this phase. Completion without a detector would not prove lifetime safety.

## Behavior Changes

No production physics behavior changed. Default benchmark behavior remains unchanged unless `--physics-regression-baseline` is supplied. Normal physics tests retain the 77-check gating result and now print non-gating known-issue diagnostics.

## Known Limitations

- Current failures are diagnostic baselines, not correctness thresholds for future implementations.
- Wall-clock timings include metric collection and must not be treated as isolated physics-step timings.
- Profiling state is global and single-threaded.
- EPA iteration/failure counts, allocation counts, solver residuals, and sleeping behavior are not available.
- Moving-body classification uses documented fixed thresholds of `0.05` units/s and `0.05` rad/s.

## Out-of-Scope Findings

- Production lifetime, convex validity, angular dynamics, contact normal ordering, solver, and stabilization defects remain unchanged for their assigned later phases.
- The pre-existing staged audit deletions, untracked audit/planning files, rendering documentation, debugger scripts, and probe artifacts were preserved unchanged.
- The duplicate `Path`/`PATH` host issue and Release `/MTd` configuration remain out of scope.

## git diff --check

PASS. Git emitted only line-ending conversion notices and no whitespace errors.

## git status --short

Phase 06 files are the three modified tracked files plus this new review document. The repository also retains the explicitly preserved pre-existing staged deletions and untracked files listed in the final review report.

## Human Decision

APPROVED
