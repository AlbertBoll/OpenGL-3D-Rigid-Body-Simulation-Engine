# Phase 30 Review

## Status

PHASE 30 STATUS: AWAITING HUMAN REVIEW

Implementation and required automated validation are complete. Human Decision: PENDING. The global whitespace check retains the documented pre-existing `.gitignore` error; all phase-owned paths pass.

## Objective

Define and implement runtime pose authority at the Scene/physics boundary. Entity translation and quaternion edits must reach collision bodies without physics output being replayed as a new edit.

## Baseline Commit

`0854ab310b7428728431937ec1adce08ec958122` (`physics: phase 29 enforce absolute geometry scaling`), branch `physics/refactor`.

## Previous Approved Tag

`physics-phase-29-approved`, verified at HEAD by the execution preflight. No Phase 30 approved tag exists. The index and HEAD are unchanged.

## Audit Findings Addressed

The runtime **pose** portion of **PHYS-BUG-012** and the corresponding scene synchronization weakness described by **PHYS-API-003**. Static entity edits previously left colliders behind; dynamic/kinematic entity edits were overwritten by physics output. Startup pose came from fixture copies, so edits to the actual entity transform could also be lost on start/restart.

The audit remains an unchanged historical record. This phase does not claim to close the geometry-import or live fixture-property portions of PHYS-BUG-012.

## Allowed Scope

Three production files, one test file and this review: five phase files total. One tightly coupled Scene/physics pose boundary. Runtime-only bridge state lives inside `_Scene.cpp`; no shared component schema, serialization, editor, renderer or general ECS redesign is needed.

The pre-existing Scene timestep edit is preserved byte-for-byte. The other 23 entry files/deletions are unchanged by SHA-256/existence comparison. Only the phase-owned hunks in `_Scene.cpp` are deliverables; its pre-existing timestep hunk must not be staged or included in a later phase commit. The phase-only comparison against the entry Scene is saved as ignored evidence in `bin/phase30-scene-only.log`.

## Files Changed

- GEngine/src/Scene/_Scene.cpp
- GEngine/include/GEngine/Physics/PhysicsSystem.h
- GEngine/include/GEngine/Physics/PhysicsSystem.cpp
- PhysicsTests/src/main.cpp
- docs/physics/PHASE_30_REVIEW.md

## Implementation Summary

- `_Scene.cpp` imports translation and quaternion from the entity transform at startup for every fixture branch. Fixture velocity, mass, material and filter import retains its existing behavior. Pose validation occurs before binding the scale callback; an invalid startup pose releases the just-created shape/body and leaves no runtime link or bridge.
- A private `RuntimePhysicsPose` component records the last published entity pose, runtime body pointer and stable identity. It is excluded from authoring-component copies and serialization and cleared on physics stop. Pointer/identity checks prevent a stale bridge from acting after body deletion, generation reuse or world replacement.
- Before the existing Scene substeps, a changed entity pose is submitted as a teleport. A static body's pose is also restored from its transform if physics-side fields were changed directly. Unchanged static poses do not invoke the mutation API. After stepping, accepted body poses are published and recorded; rejected edits restore the accepted pose, including Euler display data.
- `PhysicsSystem::SetBodyPose` checks the active world and pointer membership before dereferencing, validates the complete pose, and normalizes the quaternion. An effective change removes only this body's manifolds and transient TOIs, then clears candidate/SAP state. Existing body caches observe pose changes lazily. Unrelated manifolds and impulses survive. No-op and sign-equivalent resubmissions preserve cache state.

The API is synchronous and intended for calls **between** physics updates. It preserves linear and angular velocity. Pose edits are teleports, not swept movement or kinematic targets; no velocity is inferred from edit distance or frame duration.

## Tests Added / Modified

Added `--runtime-transform`, also registered in the full suite: **57 focused checks**, increasing the suite from **17,378 to 17,435** checks.

- Startup transforms override conflicting fixture pose copies for Static, Kinematic and Dynamic bodies.
- Runtime translation and Euler/quaternion setter edits; direct `Translation`/`QuatRotation` assignment; finite non-unit quaternion normalization.
- Static authority, prescribed-velocity kinematic motion, dynamic integration, physics-side moving-body edits and publication without feedback. Both stored velocities survive teleports.
- NaN/Inf translation, zero/non-finite/tiny/overflow-length quaternion rejection, transactional preservation, accepted-pose restoration and recovery.
- Null/foreign body and inactive-world API rejection; invalid startup poses produce no runtime body.
- Existing warm-start/TOI state survives no-ops and quaternion sign equivalence, including repeated nontrivial accepted quaternions. Changed poses retire only affected contacts and preserve unrelated cached impulses.
- Warmed COM, rotated box AABB and inverse-world-inertia refresh; the next SAP query excludes the old neighbor and includes the new one; narrow phase reports a finite contact at the new position.
- Stop/start imports the current entity pose. Deleted/reused body identities and replaced worlds leave old bridge state inert.

The existing scene lifecycle regression now sets entity translations explicitly to retain its original two-sphere layout under the new startup authority rule. No existing numerical acceptance threshold was relaxed.

The first 28 scene checks were built against Phase 29 production code: **14 failed**, reproducing missing startup/runtime synchronization. Those checks then passed after implementation. A later quaternion cache test initially counted only its inserted contact despite a deliberately preserved unrelated TOI; its expectation was corrected to preserve/count both. Final results below use the corrected fixture.

### Numerical contract and tolerances

Positions and velocities in exactly representable teleport fixtures use exact comparisons. Integrated positions, analytic box bounds, COM and inverse inertia use **1e-5** absolute tolerance. Quaternion length/component checks use **1e-6**. API quaternion squared length must be finite and greater than `Math::NumericalEpsilonSquared` (currently `1e-12`); otherwise the entire pose is rejected. Exact/sign-equivalent resubmission of an already accepted unit quaternion allows **4 float epsilons** in squared length, avoiding last-bit renormalization and contact invalidation. This tolerance does not suppress a distinct authored rotation.

## Validation Commands

Class B pose/cache correctness, plus focused Class A lifecycle checks. No Class D performance work or solver-policy change. Inspected the current generated Visual Studio/MSBuild x64 projects. Sandbox process/patch helpers failed Windows setup; scoped reviewed shell execution completed reads, edits and validation. Process-local Path normalization handles the existing duplicate Path/PATH environment; system settings and build configuration were not changed.

```powershell
& .agents/skills/physics-phase-execution/scripts/Get-PhysicsPhasePreflight.ps1 -RepositoryRoot (Get-Location).Path -Phase 30
$phase30Path = $env:Path
Remove-Item Env:Path
$env:Path = $phase30Path
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Debug /p:Platform=x64 /clp:ErrorsOnly
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Release /p:Platform=x64 /clp:ErrorsOnly
.\bin\Debug\PhysicsTests\PhysicsTests.exe --runtime-transform
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --runtime-transform
.\bin\Release\PhysicsTests\PhysicsTests.exe
git diff --check
git diff --check -- GEngine/src/Scene/_Scene.cpp GEngine/include/GEngine/Physics/PhysicsSystem.h GEngine/include/GEngine/Physics/PhysicsSystem.cpp PhysicsTests/src/main.cpp
git diff --no-index --check NUL docs/physics/PHASE_30_REVIEW.md
git status --short
```

Build commands additionally use `/fl /flp:logfile=...;verbosity=normal`. Ignored evidence: `bin/phase30-entry-{status,hashes,scene}.log`, `bin/phase30-scene-only.log`, `bin/phase30-red-{build,focused}.log` and `bin/phase30-final-{debug,release}-{build,focused,tests}.log`.

## Debug Result

x64 GEngine, PhysicsTests and PhysicsBenchmark build: **pass**, exit 0, zero errors. Focused **57/57** and full **17,435/17,435**, exit 0. The existing diagnostic reports zero observed known issues. Existing compiler/vendor warnings remain.

## Release Result

x64 GEngine, PhysicsTests and PhysicsBenchmark build: **pass**, exit 0, zero errors. Focused **57/57** and full **17,435/17,435**, exit 0. The existing diagnostic reports zero observed known issues. Existing compiler/vendor warnings remain. The nominal optimized configuration still has the **Debug static CRT `/MTd` override**, confirmed by D9025 warnings; this is not a true Release-CRT allocator/runtime comparison.

## Stability Results

Existing angular-dynamics, box manifold/persistence, position-stabilization, sphere-drop and friction checks pass in both configurations without changed tolerances.

**Measured**, identically in final Debug and nominal Release: the existing penetrated-stack fixture reports peak kinetic energy **0**, final maximum depth **0.0200024**, final average Y **4.45**, with identical repeated runs. The 120 Hz high sphere drop reports peak/initial energy **18/18**, maximum penetration **0.000192761**, final-window linear/angular speed **0.000394938/0.00039415**, one manifold/point and finite state. These match Phase 29 and are existing regression fixtures, not standalone audit workload benchmarks.

The existing 1,200-pair collision corpus retains fingerprint **2991584723466465015**, maximum GJK iterations **34** and **18** safe contact failures, matching Phase 29's unchanged-geometry reference.

Runtime teleports are external pose edits and may change potential energy; this phase does not claim energy conservation across those edits.

## Benchmark Results

**Not available:** no performance benchmark or speedup claim. No standalone audit stack/lattice benchmark is required for this pose synchronization phase. The SAP algorithm and physics stepping policy are unchanged.

## Behavior Changes

| Body type | Startup | Runtime authority |
|---|---|---|
| Static | Entity transform supplies the pose | Entity pose remains authoritative; no motion from stored velocity. |
| Kinematic | Entity transform supplies the pose | Existing prescribed linear/angular velocity integrates without gravity/impulse response. Entity pose edits teleport before stepping; resulting motion is published. |
| Dynamic | Entity transform supplies the pose | Physics owns ordinary simulated motion. An entity pose edit explicitly teleports before stepping, preserving velocity; resulting motion is published. |

Runtime behavior follows the body's effective `Type`, not a later unsynchronized edit to the authoring component. A synchronization boundary also runs for `Scene::Update(0)` without advancing time. If both entity and physics-side poses are edited between updates, the changed entity pose wins. Fixture pose copies are no longer startup authority. Entity pose setters/direct translation-quaternion writes remain usable while stopped and are imported on restart.

## Known Limitations

- Teleports have no swept-path collision guarantee and do not infer velocity or add kinematic target interpolation. Sleeping/wake behavior remains for the later phases.
- The supported rotation source is `QuatRotation`; use `SetRotation(Euler)` to keep it synchronized when editing Euler angles. Direct writes to `EulerRotation` alone do not change the existing render transform either.
- Direct public physics pose writes still bypass contact invalidation; callers needing teleport semantics must use `SetBodyPose`. Raw body pointers are required to refer to currently owned bodies; Scene bridges additionally validate the stored identity generation.
- This phase does not add live body-type, mass, material, radius or filter synchronization, initial-scale import correction, shape ownership conversion, arbitrary component replacement/duplication lifetime repair, or hierarchy/world-transform redesign.
- Scene integration is exercised headlessly with sphere fixtures; the shared API additionally tests asymmetric box pose caches. All three production fixture branches use the same final pose import, but renderer-backed box/convex Scene mesh construction was not exercised interactively.
- A runtime invalid edit is rejected and the accepted pose is republished; there is no new editor error UI. Invalid startup poses are logged and produce no runtime body.

## Out-of-Scope Findings

- Initial geometry scale remains inconsistent (`GEngine/src/Scene/_Scene.cpp:497`, `:518`, `:563`): spheres use fixture radius, boxes import already-scaled mesh points, convexes import unscaled unique points. These are distinct geometry-import semantics left from PHYS-BUG-012 and recorded in Phase 29; this phase is restricted to pose synchronization.
- Shape ownership and accumulated scale-signal connections remain existing lifetime work: successful startup allocates shapes, connects the transform at `_Scene.cpp:594`, and stop clears links/world without owning/deleting those successful shapes at `:599`. No new pose callback captures an entity, component or body pointer. Test fixture shapes are explicitly owned and released after Scene destruction.
- Fixture properties/body type/filter fields remain startup-only imports (`_Scene.cpp:486` onward). The old generic authoring-component copy path still copies `RigidBody3DComponent::RuntimeBody` (`Component.h:556` / `_Scene.cpp:61`); the new runtime bridge is excluded, but broader duplication/removal ownership is outside Phase 30.
- The Release CRT override and `.gitignore:33` trailing blank line remain pre-existing. The original Scene substep block was preserved, so this phase does not implement Phase 31 timestep work.

## git diff --check

Global output (exit **2**, pre-existing whitespace only):

```text
warning: in the working copy of 'GEngine/src/Scene/_Scene.cpp', LF will be replaced by CRLF the next time Git touches it
.gitignore:33: new blank line at EOF.
```

Phase-owned tracked paths: exit **0**, no whitespace errors; Git emits the same pre-existing Scene line-ending warning. The new review passes the separate `--no-index --check` without whitespace errors (exit **1** denotes the new-file difference; Git also warns that LF will become CRLF). Unrelated whitespace is preserved.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M GEngine/include/GEngine/Physics/PhysicsSystem.h
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
?? docs/physics/PHASE_30_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/rendering/
```

Only the five paths listed under Files Changed are Phase 30 deliverables. No files were staged; no commit, approved tag, push or next-phase work was performed.

## Human Decision

PENDING

Review the startup authority change, velocity-preserving teleport semantics, selective contact invalidation and preserved user timestep hunk. The [physics-phase-execution skill](../../.agents/skills/physics-phase-execution/SKILL.md) requires: "Then stop. Never stage files for approval, commit, create an approved tag, push, approve the implementation, invoke the approval skill, or begin Phase XX+1."
