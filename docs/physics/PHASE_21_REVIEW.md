# Phase 21 Review

## Status

PHASE 21 STATUS: AWAITING HUMAN REVIEW

Debug and nominal Release builds, 341 focused checks, and 1,532 full-suite checks pass. The feature extractor is ready for review; it is not connected to the running contact/solver pipeline.

## Objective

Provide stable box contact feature extraction as the foundation for later face manifolds, addressing the feature-identification part of **PHYS-BUG-005**. This phase extracts geometry only.

## Baseline Commit

`526c848f2eab25b14a9af32c063d6f144652b17f` - `physics: phase 20 separate position stabilization`, on `physics/refactor`.

The mandatory preflight verified the preceding approved tag at HEAD and no local Phase 21 approved tag. No Phase 21 review existed at entry. The existing modified/untracked files were recorded and hashed before editing.

## Previous Approved Tag

`physics-phase-20-approved`.

## Audit Findings Addressed

Part of **PHYS-BUG-005**: boxes previously exposed support points and bounds but no stable, normal-compatible face geometry for reference/incident selection. The extractor now supplies that foundation. This does not close the remaining lack of clipped multi-point box contacts.

## Allowed Scope

Three production files, one dedicated test file, and this review: five changed files total. The ShapeBox header and implementation expose individual face geometry; the small header-only BoxContact helper selects a pair's reference/incident features. No build-file change is needed because ShapeBox.cpp already belongs to GEngine and tests include the new helper directly.

Class B mathematical validation applies, along with the mandatory Debug/Release builds and focused/full physics suites. Phase 21 has no Class C/D benchmark requirement; the feature query has no production simulation caller yet.

## Files Changed

- `GEngine/include/GEngine/Physics/ShapeBox.h`
- `GEngine/include/GEngine/Physics/ShapeBox.cpp`
- `GEngine/include/GEngine/Physics/BoxContact.h`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_21_REVIEW.md`

## Implementation Summary

`BoxFaceId` identifies the six local box faces as negative/positive X, Y, and Z, independent of input vertex order, translation, and body rotation. IDs survive valid box rebuilds; the returned shape revision changes with the geometry. `BoxFaceFeature` contains the ID, shape revision, outward unit world normal, four world vertices in counterclockwise order when viewed from outside, and the alignment with the normalized selection direction.

`ShapeBox::GetContactFace` transforms a world direction into the box's local rotation frame and selects the signed axis most aligned with it. It builds the face directly from the current local bounds, including any model-space offset. It transforms points using the supplied model-origin position and orientation, preserving the engine's geometry coordinate convention. Negative faces reverse winding. It does not alter existing support, inertia, bounds, geometry construction, or revision behavior.

Direction normalization first rescales by the largest absolute component, avoiding overflow/underflow for supported finite nonzero float directions. Orientation must be finite with squared length within `1e-4` of one; accepted small drift is normalized for extraction. Invalid shapes, zero/nonfinite directions, invalid poses, nonfinite world vertices, and faces collapsed by world-coordinate rounding fail without changing the output. Double intermediate cross products validate outward winding even when the finite face area exceeds float range.

### Deterministic selection policy

- Face scores are dimensionless dot products of unit directions. Faces within `1e-5` of the maximum alignment use local X, then Y, then Z priority. This is a geometry tie policy, not a solver parameter or linear-distance tolerance.
- `ExtractBoxContactFeatures` consumes the approved **B-to-A** contact normal. A's candidate points along `-normal`, and B's along `+normal`.
- The more aligned candidate becomes the reference face. Scores within `1e-5` use the smaller stable body `(slot, generation)` identity. Input pointer addresses and A/B traversal order are not tie breakers.
- The other box's incident face is selected against the **negative reference face normal**, which can differ from the original contact direction. The oblique regression proves this distinction.
- The returned reference and incident identities preserve physical ownership when A/B and the contact normal are reversed. Both bodies must carry distinct valid world identities. Runtime-checked box casts reject null/non-box shapes and misleading mutable shape-type tags.
- Pair output is assigned only after every extraction succeeds. Queries read body pose and shape geometry without touching bodies, derived caches, manifolds, contacts, or solver state.

## Tests Added / Modified

Added `--box-features`, also registered in the full suite: **341 checks**. Full-suite count increases from **1,191 to 1,532**. Existing assertions and diagnostics are unchanged.

- All six faces of translated, off-origin asymmetric boxes at scales `0.001`, `1`, and `1000`, under identity, a quarter-turn, and an arbitrary rotation. An independent oracle filters the original source corners by the requested plane and compares the complete vertex set; it also checks cyclic outward winding and normals.
- Exact edge/corner ties, near ties on both sides of the specified tolerance, negative faces, and direction rescaling through `1e-30`, `1`, and `1e30`. Axis directions additionally cover the smallest subnormal, smallest normal, and largest finite float.
- Rotated ties, equivalent quaternion signs, small accepted quaternion drift, reordered/duplicated source corners, valid offset/asymmetric rebuilds, and rejected rebuilds.
- Aligned pairs, perturbed normals, A/B reversal, reference ownership on either body, near-equal versus clearly different alignment, and oblique incident reselection. A common rigid transform preserves local feature IDs and transforms the ordered geometry as expected.
- Body pose/velocity and shape revision preservation. Invalid directions, quaternions, world positions, world-coordinate collapse, float-overflow output, missing identity, self pairs, failure on the second box, null/non-box shapes, and spoofed type tags preserve prior output.

Geometry oracle position tolerance is `scale * 1e-5` per component; unit normals/alignment and rigid-transform comparisons use `1e-5`. Local IDs, ownership, revisions, unchanged-output checks, and pair reversal are exact. Quaternion-sign equivalence is exact on this build pair. No expected failures were added. The API is new, so an absent-API compile failure is not reported as a behavioral red control.

## Validation Commands

Inspected the current Visual Studio x64 solution and generated projects. Used MSBuild v143 from Visual Studio 2022 Community, building GEngine, PhysicsTests, and PhysicsBenchmark. Generated artifacts and phase logs remain under ignored `bin/` paths.

```powershell
& '.agents/skills/physics-phase-execution/scripts/Get-PhysicsPhasePreflight.ps1' -RepositoryRoot (Get-Location).Path -Phase 21

# Normalize the existing duplicate Path/PATH environment issue before MSBuild.
$phase21BuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $phase21BuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=<Debug-or-Release> /p:Platform=x64 /clp:ErrorsOnly

.\bin\<configuration>\PhysicsTests\PhysicsTests.exe --box-features
.\bin\<configuration>\PhysicsTests\PhysicsTests.exe

git diff --check -- GEngine/include/GEngine/Physics/ShapeBox.h GEngine/include/GEngine/Physics/ShapeBox.cpp PhysicsTests/src/main.cpp
git diff --no-index --check -- NUL GEngine/include/GEngine/Physics/BoxContact.h
git diff --no-index --check -- NUL docs/physics/PHASE_21_REVIEW.md
git diff --check
git status --short
```

The local sandbox process/patch helper failed during setup. Authorized repository reads, scoped edits, and builds used reviewed out-of-sandbox command execution; no external application or Git publication was performed.

## Debug Result

**Pass**, x64 build exit 0; focused **341/341**, full suite **1,532/1,532**, test exits 0. Full suite reports zero known issues observed in its one existing non-gating diagnostic. An initial new test counter conversion warning was corrected, then Debug was rebuilt and both test modes rerun. Final build has no warning in the changed source files; existing vendor/linker warnings remain.

Logs: `bin/phase21-Debug-build.log`, `bin/phase21-Debug-focused.log`, `bin/phase21-Debug-full.log`.

## Release Result

**Pass**, nominal optimized x64 build exit 0; focused **341/341**, full suite **1,532/1,532**, test exits 0. Full suite reports zero known issues observed in its one existing non-gating diagnostic. No warning names the changed source files. Existing vendor/linker warnings and D9025 CRT override diagnostics remain.

The documented Debug static CRT `/MTd` override remains; this is not a true Release allocator/runtime comparison.

Logs: `bin/phase21-Release-build.log`, `bin/phase21-Release-focused.log`, `bin/phase21-Release-full.log`.

## Stability Results

The extractor is not called by the production simulation pipeline, so it changes no integration, collision dispatch, contact count, solver equations, iterations, or stabilization policy. Existing full-suite stack, position stabilization, friction, restitution, lifetime, and angular regressions remain enabled.

**Measured**, existing Debug and nominal Release penetrated-stack regression (two repeats in each configuration): peak kinetic energy `0`, final maximum pair depth `0.0200024`, final average Y `4.45`. These match the approved Phase 20 review's recorded values. This is inherited regression coverage, not an improvement caused by feature extraction.

Dedicated 10-second box-stack/sphere/lattice benchmark trajectories were not rerun for this geometry-only phase. No new global settling or energy claim is made.

## Benchmark Results

**Not available / not required** for this Class B foundation phase. No timing, allocation, or performance benefit is claimed. The benchmark target builds in both configurations; it is not modified or used for a before/after performance measurement.

## Behavior Changes

New opt-in geometry APIs expose stable box faces and a reference/incident pair. Current simulation behavior remains as approved in Phase 20. Face clipping, contact reduction, multiple solver contacts, and manifold persistence are deferred to their authorized phases.

## Known Limitations

- This consumes an existing contact normal; it does not establish overlap, compute contact depth, perform SAT, or distinguish face/edge/vertex collision classes. An edge/corner direction produces a deterministic candidate face, not proof of a valid face manifold.
- Selection is stateless. The tolerance handles numerical ties but does not provide temporal hysteresis after a score crosses the tolerance boundary. Persistence policy belongs to Phase 23.
- The pair helper requires valid distinct world identities; standalone shapes can use GetContactFace, but unregistered bodies cannot use pair reference selection. Copying body identities is not a substitute for world registration.
- World vertices are pose snapshots. Callers must re-extract after a pose change; caching must also account for body identity, shape replacement, and geometry revision. Local face IDs alone do not validate cached world geometry.
- Nonunit/invalid rotations and world geometry that becomes nonfinite or collapses at float precision fail safely. This does not expand the engine's global coordinate-scale or shape-validity contract.
- No automatic connection to manifold/solver code is introduced. The remaining underconstrained-contact portion of PHYS-BUG-005 is still open.

## Out-of-Scope Findings

No newly discovered production defect requires scope expansion. Relevant existing findings remain unchanged:

- **PHYS-BUG-005:** `PhysicsSystem.cpp:557` still obtains generic GJK/EPA witnesses, `PhysicsSystem.cpp:338` inserts one contact, and `Manifold.cpp:8` accumulates persistent witnesses. There is no face clipping in the running pipeline.
- **PHYS-BUG-016:** `ShapeBox.cpp:196-213` still shifts inertia with the audited parallel-axis terms. The new off-origin tests check geometry only and do not imply off-origin dynamics are corrected.
- **PHYS-BUG-011:** `ShapeBox.cpp:240` still estimates speed from local vertices and supplied angular velocity/direction without an orientation argument. Angular CCD is outside feature extraction.
- **PHYS-BUG-024:** `GEngine/GEngine.vcxproj:86` and `PhysicsTests/PhysicsTests.vcxproj:87` retain the Release `/MTd` additional option.
- The entry `.gitignore` change has a new blank line at EOF. It is preserved as unrelated user work and still affects the repository-wide whitespace check.

## git diff --check

Phase-owned tracked files: **exit 0, no output**. The new helper and review have **no whitespace diagnostics** from `git diff --no-index --check -- NUL <path>`; those commands return native Git status 1 because the new files differ from NUL.

Repository-wide `git diff --check`: **native Git exit 2**, literal output:

```text
.gitignore:33: new blank line at EOF.
```

This diagnostic was present at entry and belongs to preserved user work. It is not introduced by Phase 21.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/ShapeBox.cpp
 M GEngine/include/GEngine/Physics/ShapeBox.h
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? GEngine/include/GEngine/Physics/BoxContact.h
?? docs/audit/
?? docs/physics/PHASE_21_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

All **15 pre-existing modified/untracked files match their entry SHA-256 hashes**. Phase-owned generated artifacts and logs are ignored; no user work is staged, changed, cleaned, or absorbed into the phase. HEAD remains `526c848`; Phase 21 is uncommitted and has no approved tag.

## Human Decision

PENDING

Review the local ID/winding convention, dimensionless tie policy, stable reference ownership, and reference-normal incident selection. No commit, tag, push, or next phase is authorized by this execution turn.
