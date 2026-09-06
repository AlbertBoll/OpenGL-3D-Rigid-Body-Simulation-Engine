# Phase 20 Review

## Status

PHASE 20 STATUS: AWAITING HUMAN REVIEW

Debug and nominal Release builds, 139 focused checks, 1,191 full-suite checks, and the required deterministic probes pass. The ordinary stack remains neutral against Phase 19; the deliberately penetrated stack corrects overlap without generating kinetic energy. The lattice has mixed results at eight velocity passes, documented below.

## Objective

Separate resting penetration stabilization from physical velocity response, addressing **PHYS-BUG-008** with one bounded position correction pass.

## Baseline Commit

`c141dffa5f54a047fb5b7b533a26666329a61a11` - `physics: phase 19 configure iterative solver passes`, on `physics/refactor`.

The required preflight verified the preceding approved tag at HEAD and no local Phase 20 approved tag. No phase review existed at entry.

## Previous Approved Tag

`physics-phase-19-approved`.

## Audit Findings Addressed

**PHYS-BUG-008**: the resting constraint formerly subtracted a penetration-dependent Baumgarte bias from its physical normal RHS, creating physical impulse and friction support from overlap alone.

## Allowed Scope

Three production files (the penetration constraint header/implementation and one orchestration file), one dedicated test file, and this review: five files total. Class C stability validation applies. The existing benchmark is used without modification.

## Files Changed

- `GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp`
- `GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_20_REVIEW.md`

## Implementation Summary

The physical constraint no longer computes or stores `m_Baumgarte`, and its normal RHS contains only relative physical velocity. Existing Jacobians, effective mass algebra, friction projection, warm starting, and configurable velocity iteration count remain intact.

`ConstraintPenetration::PostSolve()` now implements a translation-only position correction. `PhysicsSystem::Update()` calls the already existing manifold/constraint PostSolve traversal once after all TOI and remainder integration, inside the solver profiler timer. This uses three production files; the manifold implementation needs no change.

Let `n` be the solver's normalized A-to-B axis, `a` and `b` the current world anchors, and `wA`, `wB` the effective inverse masses:

```text
depth = -dot(b - a, n)
correction = min(0.2, 0.25 * max(depth - 0.02, 0))
deltaA = -n * correction * wA / (wA + wB)
deltaB = +n * correction * wB / (wA + wB)
```

The 0.02 slop and 0.25 fraction retain the previous stabilization policy. The new 0.2 engine-length cap limits the relative separation correction for one contact in one step; it is a displacement bound, not a damping or velocity clamp. It is not a per-body bound across multiple contacts. The fraction/cap are not multiplied by the configured velocity pass count.

These weights minimize mass-weighted translation for the requested relative separation change. For two responsive bodies they preserve the pair's center of mass. Static, kinematic, and invalid/non-positive Dynamic mass states have zero effective response through the existing `GetInverseMass()` contract. The sum and ratios use double intermediates to avoid overflow from adding finite inverse masses.

The correction changes positions only. It preserves both velocities, orientation, orientation-dependent inertia, and cached physical impulses, so it preserves kinetic energy even for non-principal spin of an asymmetric body. Position/cache accessors detect the translated pose normally. A fixed support can require raising a dynamic body's gravitational potential; total mechanical energy therefore remains a separately measured stability criterion, not a universal conservation claim for correction of an invalid initial overlap.

Anchors, normal and depth are recomputed after integration and earlier contact corrections. Non-finite/degenerate output, self/null pairs, no responsive mass, and tangential anchor drift at or beyond the existing 0.02 manifold threshold skip correction. Separation and depths within slop also skip. `PreSolve` enables correction only for finite timesteps greater than 1e-6 seconds; one PostSolve consumes that permission. Correction is neither warm started nor repeated by extra velocity iterations.

## Tests Added / Modified

Added `--position-stabilization`, also included in the full suite: **139 checks**, increasing the full total from **1,052 to 1,191**. Two existing denominator assertions now check the resulting unchanged position instead of the removed bias field. No assertion was weakened and no expected failure was added.

- Rotated A/B reversal with unequal dynamic masses and Static/Kinematic partners, including contradictory legacy inverse masses: analytic correction direction/weights, no physical normal impulse from penetration, and warmed COM/bounds refresh.
- Asymmetric non-principal spin: exact unchanged velocities, orientation and inverse inertia across the position pass.
- Separated/touching/slop/deep overlap at negative, zero, tiny, 1/240 and 1/60 second steps, including the displacement cap.
- World orchestration at 1, 8 and 32 velocity passes: a separating body is integrated first, then corrected from its current depth once. Repeated correction converges without bounce or cached stabilization impulse.
- Invalid/zero normals, invalid anchors, tangential drift, unprepared PostSolve, self-pair and non-responsive participants fail safely.
- Supported and unsupported tangential motion retain the analytic Coulomb response; position correction does not supply friction support, change physical cached impulses, or run twice.
- A deliberately penetrated 4x4 box stack with fixed center witnesses, zero gravity, fixed body/contact order and 1/120 s for 1,200 steps, repeated twice. Every initial vertical gap has 0.12 overlap. Masks suppress new narrow-phase queries to isolate stabilization of those persistent contacts; ordinary collision generation is exercised by the unchanged gravity-driven benchmark.

Position/vector tolerance is **2e-6 absolute** in analytic tests. The penetrated stack requires **zero measured kinetic energy**, final maximum pair depth <= **0.02001**, and final average height >= **4.44998** (the ideal height with retained slop is 4.45). This depth tolerance accounts for accumulated float translations. Deterministic repeats, preserved velocities/inertia and cached impulses use exact equality where stated.

### Failure-before / pass-after

With the original 128 new checks and unchanged Phase 19 production code, **66/128 failed** (exit 1). The same checks then passed with this implementation. Eleven additional guard/friction assertions were added during review; the final 139 checks pass in both configurations. The 66/128 red control is not a claim that all 139 final checks were run against the old implementation.

**Measured** penetrated-stack control, each of two runs:

| State | Peak kinetic energy | Final max pair depth | Final average Y |
|---|---:|---:|---:|
| Initial fixture | 0 | 0.12 | 4.2 |
| Phase 19 velocity bias | 670.361 | 0 (boxes launched away) | 92.5021 |
| Phase 20 position pass | 0 | 0.0200024 | 4.45 |

The before-run zero penetration is ejection, not successful stabilization. The after-run improves overlap relative to the initial penetrated stack while retaining its structure and zero kinetic energy.

## Validation Commands

Inspected the current Visual Studio x64 solution and generated projects. GEngine and PhysicsBenchmark retain profiling. MSBuild v143 from Visual Studio 2022 Community builds the GEngine, PhysicsTests and PhysicsBenchmark targets in both configurations.

```powershell
& '.agents/skills/physics-phase-execution/scripts/Get-PhysicsPhasePreflight.ps1' -RepositoryRoot (Get-Location).Path -Phase 20

# Normalize duplicate Path/PATH host environment entries before MSBuild.
$phase20BuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $phase20BuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=<Debug-or-Release> /p:Platform=x64 /clp:ErrorsOnly

.\bin\<configuration>\PhysicsTests\PhysicsTests.exe --position-stabilization
.\bin\<configuration>\PhysicsTests\PhysicsTests.exe
.\bin\<configuration>\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=<1-or-8>
```

The local Python runner performs the same environment normalization, logs each child command, checks exit status, and launches no visible helper windows. Before probes: Release 1/8 and Debug 1. After probes: Release 1/8 twice and Debug 1/8 once. Builds/tests and stability timing runs execute sequentially. The final test-only additions require the final build/test rerun; production and benchmark sources do not change between the recorded probes and final build.

Local ignored evidence: `bin/phase20-validation.log`, `bin/phase20-entry.log`, `bin/phase20-before-*`, `bin/phase20-red-*`, `bin/phase20-green-*`, and `bin/phase20-review-writer.log`. Each build has an accompanying `-build-details.log`. Sandbox launcher/file-editor failures required escalated local reads, edits and validation; no repository build setting was changed.

## Debug Result

Before: build succeeded, full suite **1,052/1,052**. After: build succeeded, focused **139/139**, full suite **1,191/1,191**. Both 10-second probe configurations finish with every finite flag set. No new expected failures.

## Release Result

Before: build succeeded, full suite **1,052/1,052**. After: build succeeded, focused **139/139**, full suite **1,191/1,191**. Repeated probes finish with finite output and identical non-timing CSV fields.

Nominal Release remains optimized with the **Debug static CRT override `/MTd`**, including MSVC D9025. This is not a representative true Release-CRT allocator/runtime comparison.

## Stability Results

### Workloads and acceptance

The unchanged benchmark retains the original geometry/materials, creation order, gravity (0,-12,0), fixed 1/120 s, and 1,200 steps. There is no random input or discarded warmup. The floor half-extents are (50,0.5,50). The stack contains four columns by four unit-half-extent boxes; the single sphere drops from y=10; the 180 radius-1 spheres retain the original 5x6x6 lattice. Per-body friction/elasticity are 0.5. The free asymmetric body runs without gravity/contact.

**Measured**, first nominal Release run at each count. Energy is linear plus angular kinetic energy plus gravitational potential. Linear speed uses engine-length units/s; angular speed uses rad/s. Moving-body thresholds are 0.05 for either speed.

All non-timing fields match between each Release repeat and its Debug counterpart at printed precision. The ordinary stack, single sphere and free body also match the corresponding Phase 19 before-run fields exactly at counts 1 and 8. The ordinary stack therefore retains the approved **100% initial-energy peak** with no numerical allowance needed at recorded precision. Its penetration is neutral, not improved; improvement of active stabilization is demonstrated by the intentionally penetrated fixture above. This phase does not claim a new global stack-settling result or a reduction of the retained slop.

### Ordinary 4x4 stack

| State / passes | Peak energy % | Final energy | Final avg Y | Peak linear | Final linear | Peak angular | Final angular | Moving |
|---|---|---|---|---|---|---|---|---|
| Before / 1 | 100.000000 | 862.686012 | 4.493112 | 0.215006 | 0.097994 | 0.090607 | 0.019983 | 2 |
| After / 1 | 100.000000 | 862.686012 | 4.493112 | 0.215006 | 0.097994 | 0.090607 | 0.019983 | 2 |
| Before / 8 | 100.000000 | 863.878419 | 4.499367 | 0.083085 | 0.000112 | 0.104020 | 0.000144 | 0 |
| After / 8 | 100.000000 | 863.878419 | 4.499367 | 0.083085 | 0.000112 | 0.104020 | 0.000144 | 0 |

| State / passes | Manifolds | Points | Mean generated contacts | Peak floor intrusion | Final floor intrusion | Mean solver ms | Mean step ms |
|---|---|---|---|---|---|---|---|
| Before / 1 | 16 | 52 | 15.940833 | 0.008241 | 0.005632 | 1.292265 | 2.540471 |
| After / 1 | 16 | 52 | 15.940833 | 0.008241 | 0.005632 | 1.342006 | 2.587388 |
| Before / 8 | 17 | 50 | 16.880000 | 0.002292 | 0.002263 | 8.755310 | 10.117234 |
| After / 8 | 17 | 50 | 16.880000 | 0.002292 | 0.002263 | 8.793435 | 10.106953 |

### Sphere lattice

| State / passes | Peak energy % | Final energy | Final avg Y | Peak linear | Final linear | Peak angular | Final angular | Moving |
|---|---|---|---|---|---|---|---|---|
| Before / 1 | 102.332486 | 5769.001126 | 1.458899 | 20.800062 | 15.614874 | 12.364333 | 10.979907 | 180 |
| After / 1 | 100.000000 | 4618.230531 | 1.475437 | 14.400019 | 9.149487 | 11.252431 | 6.806019 | 180 |
| Before / 8 | 100.000000 | 4224.305124 | 1.487536 | 14.300018 | 5.600746 | 6.746700 | 5.626596 | 180 |
| After / 8 | 100.000000 | 4249.059561 | 1.486900 | 14.300018 | 5.695103 | 6.719536 | 5.710816 | 180 |

| State / passes | Manifolds | Points | Mean generated contacts | Peak floor intrusion | Final floor intrusion | Mean solver ms | Mean step ms |
|---|---|---|---|---|---|---|---|
| Before / 1 | 174 | 626 | 207.580833 | 5.180741 | 5.180741 | 10.947990 | 14.662659 |
| After / 1 | 181 | 700 | 253.837500 | 1.788265 | 1.788265 | 13.988855 | 18.341185 |
| Before / 8 | 182 | 695 | 253.815833 | 0.158858 | 0.017013 | 94.654667 | 98.716140 |
| After / 8 | 185 | 699 | 264.871667 | 0.196673 | 0.039583 | 103.213864 | 107.448427 |

At one pass, the lattice's peak energy falls from 102.332486% to 100%, and final linear/angular speed and floor intrusion fall. All 180 bodies still move. At eight passes, peak energy stays at 100% and peak angular speed decreases slightly, while final linear/angular speed and floor intrusion increase. These are explicit regressions in those metrics; no monotonic or universal settling improvement is claimed.

Floor intrusion is the existing `max(0, 0.5 - lowest_world_bound_y)` metric. It is neither inter-body penetration nor limited to bodies above the finite floor footprint. The one-pass 1.788265 final value therefore must not be interpreted as a validated contact depth. The fixture-specific pair depths above measure actual known vertical gaps independently.

### Single sphere and free body

The single sphere is unchanged in both configurations/counts: peak energy 100%; final energy 17.998483; final Y 1.499874; final linear/angular speed 0.001041/0.001039; zero moving bodies; one manifold/point; peak/final floor intrusion 0.000126. The free asymmetric body is unchanged: peak/final energy 5.432790, +4.242841%, within the existing Phase 13 first-order integration tolerance. It has no position correction contacts.

## Benchmark Results

This is Class C correctness/stability work, not a performance optimization. The tables report **Measured** mean solver and whole-step times over the 1,200-step stability trajectories, including the new PostSolve work in solver time. They are not multiple-sample medians and do not isolate correction overhead because the lattice contact trajectories/counts change. No performance gain is claimed. Dedicated 100/1,000/10,000-body Class D median benchmarks and allocation counts are **Not available** for this phase and are not required by its validation class.

## Behavior Changes

Resting overlap no longer generates physical rebound or extra friction capacity. Valid persistent overlaps beyond slop receive a bounded mass-weighted translation after physical stepping. Velocity iteration defaults and physical solver equations other than removal of bias retain their approved settings. External users of the old public `m_Baumgarte` field must stop accessing it; the repository has no remaining production caller.

## Known Limitations

- Ordinary gravity-driven stack penetration remains unchanged; the active correction improvement is demonstrated on the penetrated regression fixture.
- The eight-pass lattice has higher final motion and floor intrusion, and both measured lattice configurations retain 180 moving bodies.
- Translation-only correction does not rotate an embedded/tilted body or generate missing face contacts. It can move one contact into another; every correction pass is bounded per contact, not per body or island.
- The 0.02 slop, 0.25 fraction and 0.2 absolute cap are an engine-scale policy, not a scale-independent guarantee. No parameter tuning or scale contract is introduced.
- Correction can restore gravitational potential against a fixed support. Kinetic invariance of this pass is exact; total energy stability is established only for the measured fixtures.
- Only existing valid resting witnesses receive position correction. Drift checks skip stale anchors; general feature coherence, pure CCD, TOI revalidation and sleeping remain later phases.
- Repeat equivalence is established on this machine/build pair at output precision, not across machines.

## Out-of-Scope Findings

No newly discovered production defect required a scope expansion. Existing issues relevant to interpretation remain:

- Remaining **PHYS-BUG-005**: `Manifold::AddContact` in `Manifold.cpp` accumulates at most four witnesses, with no box-face clipping. Position projection cannot provide missing contact rank.
- **PHYS-BUG-009/010**: `Collision::ConservativeAdvance` and the sorted TOI loop in `PhysicsSystem.cpp` still mutate/rewind prediction state and resolve precomputed events without rebuilding the event list. The correction is scheduled after that existing sequence.
- **PHYS-PERF-003**: `ConstraintPenetration::Solve` still allocates heap-backed small matrices each velocity pass. This phase does not optimize the equations/storage.
- **PHYS-BUG-024**: generated Release projects retain `/MTd`; no build file changed.
- The previously documented Debug paired-box benchmark logger initialization issue remains (`PhysicsBenchmark/src/main.cpp:783` constructs ShapeBox without the initialization in regression mode at line 493). The required regression mode initializes logging and passes.

## git diff --check

Phase-owned tracked files: **exit 0, no output**. The new review also passes `git diff --no-index --check -- NUL docs/physics/PHASE_20_REVIEW.md` with no whitespace diagnostics.

Repository-wide `git diff --check`: **native Git exit 2** (PowerShell command exit 1), literal output:

```text
.gitignore:33: new blank line at EOF.
```

This is the preserved pre-existing `.gitignore` change, also documented in Phase 19. It is not a Phase 20 whitespace defect.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_20_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

All 15 pre-existing modified/untracked files match their entry SHA-256 hashes. No unrelated work is touched, staged, cleaned or included. HEAD remains the approved Phase 19 commit; Phase 20 is uncommitted and untagged.

## Human Decision

PENDING

Review the translation-only correction contract and bounds, the ordinary-stack neutrality, and the lattice's eight-pass regressions before approval. No commit, tag, push or next phase is performed.
