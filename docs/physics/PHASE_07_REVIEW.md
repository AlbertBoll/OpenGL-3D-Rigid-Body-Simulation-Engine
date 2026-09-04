# Phase 07 Review

## Status

PHASE 07 STATUS: APPROVED

## Objective

Ensure removing a rigid body cannot leave manifolds or transient contacts with dangling pointers to the deleted body.

## Baseline Commit

`1ef14e060eeea293cb90317654d355793c7c17cb` (`1ef14e0 physics: phase 06 regression baseline`)

## Previous Approved Tag

`physics-phase-06-approved`

The tag and Phase 07 starting `HEAD` both resolve to the baseline commit above.

## Audit Findings Addressed

- `PHYS-BUG-001`: body removal left raw body pointers in retained manifolds and caused an access violation on the next physics step.

## Allowed Scope

- one pre-deletion body-removal notification;
- invalidation of manifolds and transient contacts that reference the removed body;
- focused body-removal regression coverage;
- no world restart/reset, stable body IDs, RAII conversion, manifold indexing, solver, collision, sleeping, or performance work.

Actual scope is 3 production files, 1 test file, and this review document: 5 files total.

## Files Changed

- `GEngine/include/GEngine/Physics/PhysicsWorld.h`
- `GEngine/include/GEngine/Physics/PhysicsWorld.cpp`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_07_REVIEW.md`

## Implementation Summary

- Added a private `PhysicsWorld` body-removal callback that only `PhysicsSystem` can install.
- `PhysicsSystem::SetPhysicsWorld()` installs the callback for the active world.
- `PhysicsWorld::RemoveRigidBody3D()` invokes the callback before erasing or deleting the body.
- The callback removes every manifold whose body pair references the body being removed.
- The same callback removes any transient positive-TOI contact that references the body.
- Unrelated manifolds are retained.
- The review revision exposed no additional production defect, so the production implementation was not changed.

## Tests Added / Modified

The former non-gating Phase 06 body-removal diagnostic is now a gating end-to-end regression. The focused command runs 13 checks covering:

- creation of a target active manifold and a separate unrelated active manifold;
- removal of the first body in the target manifold;
- removal of the opposite body in a separate fixture;
- removal of a body that has no manifold;
- preservation of the unrelated manifold and its contact;
- continued finite physics stepping after each manifold-bearing removal.

Two additional focused regressions were added during human review:

### Multi-manifold invalidation

- Creates one body touching two different bodies, producing two active manifolds that reference the shared body.
- Creates a third active manifold for a separate unrelated body pair.
- Removes the shared body through `PhysicsWorld::RemoveRigidBody3D()` on a world registered with `PhysicsSystem`.
- Immediately verifies that both shared-body manifolds are gone and the unrelated manifold and its contact are preserved.
- Steps again and verifies all four surviving bodies remain finite.

### Transient positive-TOI contact invalidation

- Uses the real sphere/sphere swept collision path to create two finite contacts with positive time of impact: one referencing the body to remove and one unrelated.
- Queues both contacts in the system's transient positive-TOI storage using test-translation-unit-only private-state access; no production inspection API was added.
- Removes the body through the registered production `PhysicsSystem`/`PhysicsWorld` path.
- Immediately verifies that the removed body's transient contact is gone before any later update can clear state, while the unrelated transient contact is preserved.
- Steps again and verifies the transient queue is empty and all surviving bodies remain finite.

The Phase 06 `--unsafe-body-removal` command is retained as the focused comparison command. It now uses the production `PhysicsSystem`/`PhysicsWorld` path and completes safely.

Normal test output now reports:

```text
XPASS: body removal invalidates manifolds before deleting the body
BASELINE convex_empty_points=0 convex_empty_reports_valid=1 convex_degenerate_points=0 convex_degenerate_reports_valid=1
XFAIL: empty and fewer-than-four-point convex shapes report invalid
BASELINE contact_pair_order_normal_dot=-1
XFAIL: reordered contact preserves the canonical manifold normal direction
PhysicsTests diagnostics: 2 known issues observed in 3 non-gating diagnostics
PhysicsTests: 90 checks passed
```

## Validation Commands

The host exposes duplicate `Path`/`PATH` entries, so the Phase 06 child-process normalization was retained for MSBuild:

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
.\bin\Debug\PhysicsTests\PhysicsTests.exe --unsafe-body-removal
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --unsafe-body-removal
.\bin\Release\PhysicsTests\PhysicsTests.exe
```

Final repository checks:

```powershell
git diff --check
git status --short
```

## Debug Result

- Build: PASS, 0 errors, 64 existing/toolchain warnings.
- Focused body-removal regression: PASS, exit 0, `13 checks passed`.
- Full physics tests: PASS, `90 checks passed`.
- The body-removal baseline diagnostic changed from XFAIL to XPASS.
- Surviving bodies remained finite after subsequent steps.

## Release Result

- Build: PASS, 0 errors, 137 existing/toolchain warnings.
- Focused body-removal regression: PASS, exit 0, `13 checks passed`.
- Full physics tests: PASS, `90 checks passed`.
- The body-removal baseline diagnostic changed from XFAIL to XPASS.

This remains a nominal optimized Release build with the repository's documented `/MTd` override; allocator/runtime behavior is not a true Release-CRT result.

## Stability Results

Not applicable. Phase 07 changes only body-removal lifetime invalidation and does not alter integration, collision generation, or solver equations.

## Benchmark Results

Not applicable. Phase 07 is a Class A lifetime-safety phase and contains no performance optimization.

## Behavior Changes

- Removing a body from the active `PhysicsSystem` world synchronously discards manifolds and transient contacts that reference it before its memory is released.
- Manifolds for other body pairs remain available and continue to solve on later steps.
- The previous next-step use-after-free reproduction now completes safely.

## Known Limitations

- World teardown, `OnExit()`, and world replacement/restart remain unchanged for Phase 08.
- Stable body identities/generations remain deferred to Phase 09.
- Mutable body storage remains deferred to Phase 10.
- No ASan or Application Verifier configuration was available; validation used the checked Debug runtime, the prior deterministic access-violation repro, and Debug/Release regression runs.

## Out-of-Scope Findings

- `PHYS-BUG-002` remains: `PhysicsSystem::OnExit()` and replacement/reset behavior do not clear retained manifolds. This is explicitly Phase 08.
- The foreign/double-delete behavior in `PhysicsWorld::RemoveRigidBody3D()` remains unchanged as the separate `PHYS-BUG-021` ownership issue.
- The empty/degenerate convex validity and reversed contact-normal diagnostics remain expected failures for their assigned later phases.
- Pre-existing edits to `RigidBodySimulation/imgui.ini`, audit deletions, and untracked audit/planning/rendering/probe files were preserved unchanged.

## git diff --check

PASS. Git emitted only line-ending conversion notices and no whitespace errors.

## git status --short

Phase 07 consists of four modified tracked files and this new review document. The repository also retains the explicitly preserved pre-existing changes listed under `Out-of-Scope Findings`.

## Human Decision

APPROVED
