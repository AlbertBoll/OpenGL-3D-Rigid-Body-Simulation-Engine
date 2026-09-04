# Phase 09 Review

## Status

PHASE 09 STATUS: APPROVED

## Objective

Introduce a stable internal rigid-body identity and generation mechanism for future cached-pair references without converting existing raw-pointer collision, manifold, or broad-phase storage.

## Baseline Commit

`76211a41fc11f805a0c2e860fa53ed458dcb35fe` (`76211a4 physics: phase 08 safe world lifecycle`)

## Previous Approved Tag

`physics-phase-08-approved`

The tag and Phase 09 starting `HEAD` both resolve to the baseline commit above.

## Audit Findings Addressed

- Enables `PHYS-API-004`: collision and future cached-contact data now have a stable world-managed body identity available in addition to the existing raw pointer.
- Establishes the identity prerequisite for future `PHYS-PERF-002` stable-key manifold pair indexing.

Phase 09 does not replace raw pointers or implement the future manifold hash/index.

## Allowed Scope

- add an internal rigid-body identity value containing a reusable slot and a generation;
- assign identities only through `PhysicsWorld::CreateRigidBody3D()`;
- invalidate an identity during `PhysicsWorld::RemoveRigidBody3D()` before body memory is released;
- validate whether a copied identity still denotes a live body in a particular world;
- reuse released slots with a fresh generation so allocator or slot reuse cannot impersonate an older body;
- focused identity creation, stability, removal, reuse, and cross-world tests;
- no conversion of collision, manifold, broad-phase, scene, or solver pointers.

Actual scope is 3 production files, 1 test file, and this review document: 5 files total.

## Files Changed

- `GEngine/include/GEngine/Physics/PhysicsBody.h`
- `GEngine/include/GEngine/Physics/PhysicsWorld.h`
- `GEngine/include/GEngine/Physics/PhysicsWorld.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_09_REVIEW.md`

## Implementation Summary

- Added `RigidBodyIdentity`, a small value type containing a nonzero one-based slot and nonzero 64-bit generation. Its default value is explicitly invalid and it supports value equality.
- Added a read-only `RigidBody3D::GetIdentity()` accessor; only `PhysicsWorld` can assign or clear the private identity.
- Added a world-owned identity-slot table and reusable free-slot list alongside the unchanged owning body vector.
- New world bodies receive a valid slot/generation identity. Slot storage can be reused after removal, while generations come from a process-wide atomic monotonic source so live identities remain distinct across simultaneous worlds.
- Added `PhysicsWorld::IsBodyIdentityValid()` to validate a copied identity against the current occupant and generation of that world's slot.
- Body removal keeps the approved Phase 07 cache-invalidation callback order, then releases the identity before deleting body memory.
- World destruction clears each body's identity before deletion and releases all identity bookkeeping.
- No raw pointer in contacts, manifolds, broad phase, solver state, or scene runtime components was converted in this phase.

## Tests Added / Modified

Added a focused `--body-identity` regression with 6 checks covering:

- directly constructed/non-world bodies retain the invalid identity and cannot impersonate world-managed bodies;
- two live bodies receive valid, distinct identities recognized only by their owner world;
- a live body's identity remains unchanged while body/slot storage grows;
- removal invalidates only the removed identity and preserves surviving identities;
- the released slot is deliberately reused with a different generation, while the old identity remains invalid;
- simultaneous worlds cannot validate or collide with each other's identities.

The identity regression also runs in the normal full suite. Full test count increased from 110 to 116 checks.

Existing Phase 07/08 focused lifetime regressions were rerun because identity release is now part of body/world teardown.

## Validation Commands

The host exposes duplicate `Path`/`PATH` entries, so the established child-process normalization was retained for MSBuild:

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
.\bin\Debug\PhysicsTests\PhysicsTests.exe --body-identity
.\bin\Debug\PhysicsTests\PhysicsTests.exe --unsafe-body-removal
.\bin\Debug\PhysicsTests\PhysicsTests.exe --unsafe-world-restart
.\bin\Debug\PhysicsTests\PhysicsTests.exe --scene-runtime-lifecycle
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --body-identity
.\bin\Release\PhysicsTests\PhysicsTests.exe --unsafe-body-removal
.\bin\Release\PhysicsTests\PhysicsTests.exe --unsafe-world-restart
.\bin\Release\PhysicsTests\PhysicsTests.exe --scene-runtime-lifecycle
.\bin\Release\PhysicsTests\PhysicsTests.exe
```

Final repository checks:

```powershell
git diff --check
git status --short
```

## Debug Result

- Build: PASS, 0 errors, 84 existing vendor/toolchain warnings.
- Focused body-identity regression: PASS, exit 0, `6 checks passed`.
- Focused body-removal regression: PASS, exit 0, `13 checks passed`.
- Focused world-reset/restart regression: PASS, exit 0, `11 checks passed`.
- Focused Scene/runtime lifecycle regression: PASS, exit 0, `9 checks passed`.
- Full physics tests: PASS, `116 checks passed`.
- The full suite retained the two expected later-phase XFAIL diagnostics for invalid convex geometry and reordered contact normals.
- No NaN/Inf or checked-runtime failure was observed.
- Final approval rerun: PASS; incremental build reported 0 warnings and 0 errors, with the same `6`, `13`, `11`, `9`, and `116` passing check counts.

## Release Result

- Build: PASS, 0 errors, 88 existing vendor/toolchain and CRT-override warnings.
- Focused body-identity regression: PASS, exit 0, `6 checks passed`.
- Focused body-removal regression: PASS, exit 0, `13 checks passed`.
- Focused world-reset/restart regression: PASS, exit 0, `11 checks passed`.
- Focused Scene/runtime lifecycle regression: PASS, exit 0, `9 checks passed`.
- Full physics tests: PASS, `116 checks passed`.
- The full suite retained the two expected later-phase XFAIL diagnostics for invalid convex geometry and reordered contact normals.
- No NaN/Inf or optimized-build failure was observed.
- Final approval rerun: PASS; incremental build reported 0 warnings and 0 errors, with the same `6`, `13`, `11`, `9`, and `116` passing check counts.

This remains a nominal optimized Release build with the repository's documented `/MTd` override; allocator/runtime behavior is not a true Release-CRT result.

## Stability Results

Not applicable. Phase 09 changes identity bookkeeping only and does not alter integration, collision generation, manifold equations, or solver behavior.

## Benchmark Results

Not applicable. Phase 09 is a lifetime/identity prerequisite and contains no performance optimization.

## Behavior Changes

- Every body created by a `PhysicsWorld` now has a stable valid identity for its complete live interval.
- Removing a body invalidates its copied identity in the owner world before the body is deleted.
- A later body may reuse the released identity slot, but it receives a distinct generation and cannot compare equal to or validate as the removed body.
- Identities from one live world do not validate in another live world.
- Directly constructed bodies remain valid for existing math/collision tests but have an invalid world identity until/unless created through the world API.

## Known Limitations

- Existing contacts, manifolds, broad-phase entries, and scene runtime components still store raw body pointers; the plan explicitly defers their conversion.
- The identity mechanism does not yet implement feature identity, shape generation compatibility, canonical pair hashing, or O(1)-average manifold lookup.
- Public mutable access to `PhysicsWorld` body storage remains unchanged and is assigned to Phase 10; clients can still bypass the creation/removal API and desynchronize both ownership and identity bookkeeping.
- The foreign/double-delete behavior of `PhysicsWorld::RemoveRigidBody3D()` remains the separate unaddressed part of `PHYS-BUG-021`; Phase 09 only releases identity state when the argument is recognized as a live identity in that world.
- Shape ownership and scale-signal lifetime remain unchanged.
- No ASan or Application Verifier configuration was available; validation used the checked Debug runtime and existing deterministic lifetime regressions.

## Out-of-Scope Findings

- Empty/degenerate convex validity and reversed contact-normal diagnostics remain expected failures for their assigned later phases.
- Mutable body-vector insertion of null or stale pointers remains possible until Phase 10.
- Shape allocation/ownership and scale-signal disconnection remain future ownership work.
- Pre-existing `.gitignore` edits and untracked audit/planning/rendering/probe files were preserved unchanged.

## git diff --check

Phase 09 scoped result: PASS, exit 0 for the four tracked implementation/test files. A direct trailing-whitespace scan of this new untracked review document also passed.

The required repository-wide command was also run and returned exit 2 solely for the pre-existing user-owned `.gitignore` edit:

```text
.gitignore:33: new blank line at EOF.
```

That file was already modified before Phase 09 began and is outside the authorized phase scope, so Phase 09 preserved it unchanged instead of silently altering unrelated work. Git also emitted only line-ending conversion notices for the Phase 09 tracked files; those are not whitespace errors.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/PhysicsBody.h
 M GEngine/include/GEngine/Physics/PhysicsWorld.cpp
 M GEngine/include/GEngine/Physics/PhysicsWorld.h
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_09_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

The Phase 09 entries are the three modified production files, `PhysicsTests/src/main.cpp`, and `docs/physics/PHASE_09_REVIEW.md`. The `RigidBodySimulation.cpp` edit appeared as unrelated user work after Phase 09 entered review; all other non-phase entries were already present before Phase 09. Every unrelated entry was preserved unchanged and excluded from the Phase 09 commit.

## Human Decision

APPROVED
