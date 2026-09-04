# Phase 08 Review

## Status

PHASE 08 STATUS: APPROVED

## Objective

Make physics shutdown, world replacement, and runtime restart idempotent and safe without retaining state that references a destroyed world.

## Baseline Commit

`26bb041688506c4f1c776103940ffa1b760e7f4a` (`26bb041 physics: phase 07 safe body removal`)

## Previous Approved Tag

`physics-phase-07-approved`

The tag and Phase 08 starting `HEAD` both resolve to the baseline commit above.

During human review, the unrelated user commit `49cf4af` (`delete files`) was added on top of the Phase 07 baseline. It does not overlap any Phase 08 file and was preserved as the Phase 08 commit parent.

## Audit Findings Addressed

- `PHYS-BUG-002`: physics shutdown and world replacement retained manifolds containing pointers to bodies deleted with the prior world.
- Part of `PHYS-BUG-021`: `PhysicsSystem::SetPhysicsWorld()` replaced its owned world pointer without deleting the prior world.

## Allowed Scope

- clear manifolds before destroying the active world;
- clear broad-phase, collision-pair, and transient positive-TOI contact state during reset;
- release the previously owned world during replacement;
- make repeated system shutdown and runtime stop/start safe;
- clear scene `RuntimeBody` links when physics stops;
- focused system reset/restart and Scene runtime lifecycle regression coverage;
- no shape ownership conversion, stable body handles, mutable body-storage redesign, solver/collision changes, or later-phase work.

Actual scope is 2 production files, 1 test file, 1 tracked test-build definition, its ignored generated Visual Studio project, and this review document: 6 filesystem files total and 5 tracked/new review artifacts.

## Files Changed

- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `GEngine/src/Scene/_Scene.cpp`
- `PhysicsTests/src/main.cpp`
- `premake5.lua`
- `PhysicsTests/PhysicsTests.vcxproj` (ignored generated project synchronized locally for validation)
- `docs/physics/PHASE_08_REVIEW.md`

`PhysicsSystem.h` did not require modification because the existing public lifecycle API and private state were sufficient.

## Implementation Summary

- `PhysicsSystem::~PhysicsSystem()` now uses the same idempotent `OnExit()` reset path as explicit shutdown.
- `PhysicsSystem::OnExit()` clears manifolds, broad-phase state, collision pairs, and transient contacts before it deletes the active world and nulls the owned pointer.
- `PhysicsSystem::SetPhysicsWorld()` treats the currently active pointer as an idempotent no-op; otherwise it resets and deletes the prior owned world before installing the replacement and its Phase 07 body-removal callback.
- `_Scene::OnRuntimeStop()` now marks the scene stopped and shuts physics down.
- `_Scene::OnPhysics3DStop()` clears every `RigidBody3DComponent::RuntimeBody` link before the corresponding world bodies are destroyed.
- `_Scene::OnPhysics3DStart()` first performs the idempotent stop path so direct/repeated runtime starts cannot retain an older world or component link.
- `_Scene` destruction now invokes the same physics stop path before deleting `PhysicsSystem`.
- The validation-only review revision made no production-code changes.
- The `PhysicsTests` build definition now links the repository's existing `glad` static library so the headless test can instantiate `_Scene`; the local ignored Visual Studio project was synchronized with that dependency for the required Debug/Release runs.

## Tests Added / Modified

The former isolated `--unsafe-world-restart` crash reproducer is now a gating focused regression containing 11 checks. It covers:

- an active sphere/sphere manifold, broad-phase pair, and injected transient positive-TOI contact before reset;
- clearing the world pointer, manifolds, broad-phase counters/state, collision-pair output, and transient contact vector on stop;
- a repeated `OnExit()` followed by a stopped `Update()`;
- recreating a world, generating a new finite collision, and stopping it again;
- setting the already-active world pointer as a safe no-op;
- replacing a live populated world without an explicit preceding stop;
- confirming replacement clears all state associated with the prior world before the new world is used.

The same regression runs in the normal full suite.

A separate `--scene-runtime-lifecycle` focused regression adds 9 checks covering the exact human-review sequence:

- `OnRuntimeStart()` creates non-null runtime bodies with non-null shapes and an active physics world;
- the first scene update leaves both bodies finite;
- `OnRuntimeStop()` clears every fixture's `RuntimeBody` link, clears the running flag, and shuts down the physics world;
- repeated `OnRuntimeStop()` remains safe;
- updating the stopped scene leaves the world and runtime-body links cleared without a crash;
- a second `OnRuntimeStart()` recreates valid runtime body links and an active world;
- the second update leaves both recreated bodies finite;
- the second stop clears the world and links again;
- destruction of the stopped scene completes safely.

Both focused regressions run in the normal full suite. Full test count increased from 90 to 110 checks.

The focused regression checks finite body state after each collision cycle; no NaN/Inf was observed.

## Validation Commands

Pre-fix crash confirmation:

```powershell
.\bin\Debug\PhysicsTests\PhysicsTests.exe --unsafe-world-restart
.\bin\Release\PhysicsTests\PhysicsTests.exe --unsafe-world-restart
```

Both pre-fix binaries exited with `-1073741819` (`0xC0000005`) immediately after announcing the retained-manifold step.

The host exposes duplicate `Path`/`PATH` entries, so the existing child-process normalization was retained for MSBuild:

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
.\bin\Debug\PhysicsTests\PhysicsTests.exe --scene-runtime-lifecycle
.\bin\Debug\PhysicsTests\PhysicsTests.exe --unsafe-world-restart
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --scene-runtime-lifecycle
.\bin\Release\PhysicsTests\PhysicsTests.exe --unsafe-world-restart
.\bin\Release\PhysicsTests\PhysicsTests.exe
```

Final repository checks:

```powershell
git diff --check
git status --short
```

## Debug Result

- Build: PASS, 0 errors; existing vendor/toolchain warnings remain.
- Focused Scene/runtime lifecycle regression: PASS, exit 0, `9 checks passed`.
- Focused world-reset/restart regression: PASS, exit 0, `11 checks passed`.
- Full physics tests: PASS, `110 checks passed`.
- The Phase 06 restart reproducer changed from an access violation to a gating pass.
- Final approval rerun: PASS; incremental build reported 0 warnings and 0 errors, with the same `9`, `11`, and `110` passing check counts.

## Release Result

- Build: PASS, 0 errors, 68 existing/toolchain warnings.
- Focused Scene/runtime lifecycle regression: PASS, exit 0, `9 checks passed`.
- Focused world-reset/restart regression: PASS, exit 0, `11 checks passed`.
- Full physics tests: PASS, `110 checks passed`.
- The Phase 06 restart reproducer changed from an access violation to a gating pass.
- Final approval rerun: PASS; incremental build reported 0 warnings and 0 errors, with the same `9`, `11`, and `110` passing check counts.

This remains a nominal optimized Release build with the repository's documented `/MTd` override; allocator/runtime behavior is not a true Release-CRT result.

## Stability Results

Not applicable. Phase 08 changes only lifetime/reset behavior and does not alter integration, collision generation, or solver equations.

## Benchmark Results

Not applicable. Phase 08 is a Class A lifetime-safety phase and contains no performance optimization.

## Behavior Changes

- Explicit shutdown, repeated shutdown, system destruction, and live world replacement use one reset behavior.
- No manifold, broad-phase body cache, collision pair, or transient contact from the prior world survives reset.
- Replacing a world releases the prior world rather than overwriting and leaking its owned pointer.
- Runtime stop now actually stops physics, clears scene runtime-body links, and marks the scene as not running.
- A later runtime start creates a fresh world and fresh body links.

## Known Limitations

- Shape ownership and scale-signal connection lifetime remain unchanged, as explicitly excluded from Phase 08. Runtime restart can therefore still leak separately allocated shapes even though the prior `PhysicsWorld` and its bodies are now released.
- Stable body identities/generations remain deferred to Phase 09.
- Mutable body storage remains deferred to Phase 10.
- The foreign/double-delete behavior of `PhysicsWorld::RemoveRigidBody3D()` remains the separate unaddressed portion of `PHYS-BUG-021`.
- Scene lifecycle is now exercised headlessly through the real `_Scene` API. The test target links the already-existing `glad` project because pulling `_Scene` from the engine archive also pulls scene-adjacent renderer objects; the test does not create a renderer or OpenGL context.
- No ASan or Application Verifier configuration was available; validation used the checked Debug runtime, the deterministic pre-fix access-violation repro, and Debug/Release regression runs.

## Out-of-Scope Findings

- Shape allocation/ownership and scale-signal disconnection remain future ownership work; Phase 08 does not convert them to RAII.
- `PhysicsWorld::RemoveRigidBody3D()` still deletes foreign or already removed arguments; this was not changed because the plan assigns only the world-replacement leak portion of `PHYS-BUG-021` to Phase 08.
- Empty/degenerate convex validity and reversed contact-normal diagnostics remain expected failures for their assigned later phases.
- Pre-existing edits to `RigidBodySimulation/imgui.ini`, audit deletions, and untracked audit/planning/rendering/probe files were preserved unchanged.

## git diff --check

PASS, exit 0. Git emitted only line-ending conversion notices for modified tracked Phase 08 files and no whitespace errors. A direct trailing-whitespace scan also found no issues in the new untracked review document.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M GEngine/src/Scene/_Scene.cpp
 M PhysicsTests/src/main.cpp
 M premake5.lua
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_08_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

The Phase 08 entries are the two modified production files, `PhysicsTests/src/main.cpp`, `premake5.lua`, and `docs/physics/PHASE_08_REVIEW.md`. The ignored generated `PhysicsTests/PhysicsTests.vcxproj` was synchronized with the `premake5.lua` dependency solely to execute this validation. All other entries were present before Phase 08 and were preserved unchanged.

## Human Decision

APPROVED
