# Phase 22 Review

## Status

PHASE 22 STATUS: AWAITING HUMAN REVIEW

Implementation and required automated validation are complete. Debug/nominal Release pass 1,028 focused and 2,560 full-suite checks. The default one-pass stack regressions below require human assessment. Human Decision: PENDING.

## Objective

Generate 2-4 geometrically meaningful box face contacts using reference/incident clipping, bounded reduction, and the established normal/separation convention. This addresses the face-generation part of **PHYS-BUG-005**.

## Baseline Commit

`daa59f958de7c15d21d7bbc84728b5ec866ccb37` - `physics: phase 21 extract stable box contact features`, on `physics/refactor`.

The execution preflight verified the preceding approved tag at HEAD and no Phase 22 approved tag. No Phase 22 review existed. Fifteen pre-existing modified/untracked files were hashed before editing and retained as the ownership boundary.

## Previous Approved Tag

`physics-phase-21-approved`.

## Audit Findings Addressed

**PHYS-BUG-005**, partially: a supported box face collision now supplies a geometric contact patch immediately instead of relying on one GJK/EPA witness per step to accumulate a manifold. This does not close all box-stack stability, narrow-phase robustness, or persistence findings.

## Allowed Scope

Two production files, one dedicated test file, and this review: four files total. Class B mathematical validation and Class C stability validation apply. This is a contact-generation correctness phase, not a Class D performance optimization.

## Files Changed

- `GEngine/include/GEngine/Physics/BoxContact.h`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_22_REVIEW.md`

## Implementation Summary

`BuildBoxFaceContacts` consumes an existing zero-TOI contact and reuses Phase 21's stable reference/incident face extraction. It accepts a reference face whose alignment cosine differs from one by at most `1e-4` (approximately 0.81 degrees). This restricts normal adjustment to near-face witnesses; unsupported edge/corner directions retain the original collision contact.

The incident quad is clipped against the reference quad's four inward side planes, then against its depth plane. Double intermediates and coordinates relative to one reference vertex reduce cancellation. The distance tolerance is `max(1e-6, shortest edge of either face * 1e-5)`. Adjacent and closing duplicate vertices within that distance are removed. A quad clipped by five planes has at most nine vertices; a fixed 12-element array bounds storage without heap allocation. Empty or single-point output requests fallback; a line can supply two contacts.

For more than four vertices, reduction retains a deepest vertex (depth ties within the distance tolerance use clipping order) and chooses the cyclic quadrilateral with greatest projected area. Bounded enumeration visits at most 126 combinations. Twice-area ties within `shortestEdgeSquared * 1e-5` preserve the first candidate in clipping order. The first valid candidate initializes selection unconditionally, including large geometry with a small overlap. This deterministic order and Phase 21's stable physical reference ownership preserve output under A/B reversal.

For incident point `q`, reference origin `o`, and outward unit reference normal `r`, its reference anchor is `q - r * dot(r, q-o)`. The normal is `-r` when A is reference and `r` when B is reference, preserving **B-to-A** convention. Separation is recomputed as `dot(pointA-pointB, normal)`: negative penetration, zero touching, positive separation. A tiny accepted positive gap keeps its positive sign. Both anchors are transformed to the engine's COM-relative body space. Finite checks cover the seed and all emitted fields; failure leaves the output array unchanged.

Only the existing zero-TOI manifold insertion in `PhysicsSystem::Update` uses the helper. It inserts the resulting 2-4 contacts or keeps the original witness on failure. Generated-contact telemetry counts emitted points, while manifold/solver counts continue to report accepted persistent points. GJK/EPA overlap detection, conservative advancement, positive-TOI response, manifold retention, solver parameters/equations, and integration are unchanged.

## Tests Added / Modified

Added `--box-manifolds`, also registered in the full suite: **1,028 focused checks**; the full count increases from **1,532 to 2,560**. Existing assertions and diagnostics remain enabled.

- Red control: the first eight zero-gravity world fixtures passed only **8/168** checks on unchanged Phase 21 production code; 160 failures demonstrated missing four-point manifold generation. Baseline full tests passed 1,532/1,532.
- Analytical partial-overlap rectangles for all six face directions, scales `0.001`, `1`, and `1000`, identity/arbitrary common rotation, translation, and off-origin model geometry. The independent corner oracle checks the complete point set, finite fields, normals, signed depth, COM-anchor round trips, exact ordered A/B reversal, and repeated-query output.
- A square/45-degree diamond intersection produces eight geometric vertices reduced to four. The tests check analytic boundary membership and maximum inscribed quadrilateral area. A tilted octagon independently checks deepest-plane penetration and exact reversal after reduction.
- A tilted face crossing the reference plane retains two deepest corners and two zero-depth intersections. Line overlap produces two distinct points; point-only overlap, disjoint projections, and separated planes fall back. Tolerance-sized positive gaps retain positive separation.
- Positive TOI, null bodies, zero/nonfinite or oblique edge normals, invalid seed anchors/separation, invalid orientation, and non-box shapes request fallback without changing any output field.
- Eight zero-gravity integration fixtures: aligned/45-degree yaw, either body creation order, equal-width/wide support. Each supplies four generated points and four solver constraints for ten consecutive steps, retaining its unforced pose.
- Eight gravity fixtures: the same two yaw/creation-order choices with 1/8 solver passes, fixed 1/120 s for 1,200 steps. Every final-second manifold must retain 2-4 points and speeds <=0.05; peak energy must be <=100.1% of initial; floor intrusion <=0.0201 and final center Y >=1.4799. These thresholds derive from the existing 0.02 position slop, a 0.0001 geometric allowance, the benchmark moving thresholds, and a stated 0.1% energy allowance; they are not exact measured-value targets.

Rectangle geometry/depth/anchor tolerances are `scale * 2e-5`; normals and analytic unit-scale boundaries use `1e-5`. Reversal/repeat comparisons are exact. All finite checks are gating. No new expected failures were added.

## Validation Commands

Build the actual solution targets in both x64 configurations:

```powershell
$phase22BuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $phase22BuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Debug /p:Platform=x64 /clp:ErrorsOnly
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Release /p:Platform=x64 /clp:ErrorsOnly
.\bin\Debug\PhysicsTests\PhysicsTests.exe --box-manifolds
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --box-manifolds
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=1
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=8
git diff --check
git status --short
```

The process-local Path normalization avoids the existing duplicate Path/PATH build-host error. Commands log exit codes/results through the runner; builds additionally use `/fl` with normal-verbosity logs. Sandbox helper failures required escalated local reads, scoped edits, and build/test execution. No build configuration was changed.

Ignored local evidence: `bin/phase22-initial-user-files.log`, `bin/phase22-before-*`, `bin/phase22-red-*`, `bin/phase22-green-*`, `bin/phase22-geometry-*`, `bin/phase22-after-*`, and `bin/phase22-final-*`. The final production reduction guard is included in the final build/test/probe rerun.

## Debug Result

Final x64 build succeeded. Focused tests passed **1,028/1,028** and the full suite passed **2,560/2,560**, exit 0. All gravity-supported face gates pass; no new expected failures.

## Release Result

Before: build succeeded and full tests passed **1,532/1,532**. Final x64 build succeeded; focused tests passed **1,028/1,028** and the full suite passed **2,560/2,560**, exit 0. Gravity-supported face output matches Debug at printed precision.

Nominal Release remains optimized with the existing **Debug static CRT `/MTd` override**, including MSVC D9025. These results are not a representative true Release-CRT allocator/runtime comparison.

## Stability Results

### Deterministic benchmark configuration

Before and after use the unchanged baseline runner: gravity (0,-12,0), fixed timestep 1/120 s, 1,200 steps / 10 seconds, fixed geometry/materials/body creation order, and no random input or discarded warmup. The stack is four columns by four unit-half-extent boxes; floor half-extents (50,0.5,50), friction/elasticity 0.5. Single sphere, 180-sphere lattice, and torque-free asymmetric body retain their existing fixtures. Each solver-count comparison uses the same nominal Release build settings.

**Measured**, stack values below. Energy includes linear/angular kinetic plus gravitational potential. Speeds are engine units/s and rad/s. A moving body exceeds 0.05 in either speed. All four scenarios remain finite.

| State / passes | Peak energy % | Final energy | Final average Y | Peak linear | Final linear | Peak angular | Final angular | Moving |
|---|---|---|---|---|---|---|---|---|
| Before / 1 | 100.000000 | 862.686012 | 4.493112 | 0.215006 | 0.097994 | 0.090607 | 0.019983 | 2 |
| After / 1 | 100.000000 | 861.021452 | 4.483962 | 0.605674 | 0.271478 | 0.368470 | 0.043431 | 6 |
| Before / 8 | 100.000000 | 863.878419 | 4.499367 | 0.083085 | 0.000112 | 0.104020 | 0.000144 | 0 |
| After / 8 | 100.000000 | 863.622859 | 4.498036 | 0.067272 | 0.000000 | 0.059410 | 0.000000 | 0 |

| State / passes | Manifolds | Points | Mean generated contacts | Peak floor intrusion | Final floor intrusion |
|---|---|---|---|---|---|
| Before / 1 | 16 | 52 | 15.940833 | 0.008241 | 0.005632 |
| After / 1 | 19 | 68 | 64.111667 | 0.050950 | 0.009650 |
| Before / 8 | 17 | 50 | 16.880000 | 0.002292 | 0.002263 |
| After / 8 | 16 | 64 | 63.339167 | 0.004167 | 0.004137 |

**Explicit regressions requiring review:** at the default one pass, peak/final speeds, moving-body count, and floor intrusion increase; average height decreases. At eight passes, speeds improve, but intrusion increases and height decreases slightly. The stack does not gain total energy above its initial value. The phase establishes immediate geometric face patches; it does not claim uniformly improved stack stability. No solver count or damping was changed to mask these differences.

Floor intrusion is the runner's `max(0, 0.5-lowest_world_bound_y)`, not inter-body contact depth. Contact counts can include retained witnesses; generation count alone is not a persistence-quality metric.

### Gravity-supported face fixtures

**Measured**, identical Debug/Release output at printed precision in the completed test run. All eight fixtures end with four contacts. At one pass, peak energy is at most 18.0017 from initial 18 (approximately 0.0095% growth), peak linear/angular speed at most 0.160562/0.0602121, peak floor intrusion at most 0.00232446, and final Y at least 1.49853. Final-second linear/angular speed is <=4.24e-9/5.12e-9. At eight passes, peak energy prints as 18, peak linear/angular speed <=1.71e-7/1.35e-7, floor intrusion is zero, and final Y is 1.5. Final-second speeds stay below 4.13e-9. These pass the stated energy, slop, manifold, and settling gates.

### Unaffected baseline fixtures

Before/after non-timing fields for the single sphere, sphere lattice, and free asymmetric body match exactly at printed precision for both solver counts. The single sphere ends with energy 17.998483, center Y 1.499874, speed 0.001041/0.001039, zero moving bodies, one manifold/point, and 0.000126 peak/final floor intrusion. The lattice retains 180 moving bodies: at one pass final energy 4618.230531, peak/final linear speed 14.400019/9.149487 and intrusion 1.788265/1.788265; at eight passes energy 4249.059561, speed 14.300018/5.695103 and intrusion 0.196673/0.039583. Peak energy is 100% in both. The torque-free body retains +4.242841% energy change from the baseline fixture. These retained limitations were not modified.

## Benchmark Results

No Class D speedup claim. The stability runner reports arithmetic mean step/solver times over 1,200 steps, not warmed repeated-sample medians. The initial probes overlap some independent Debug compilation/testing, so their timing is diagnostic only. Final probes run sequentially after final builds/tests. Controlled performance medians: **Not available**; this phase changes numerical box-contact behavior and is not a performance optimization.

**Measured**, diagnostic means only:

| State / passes | Mean stack step ms | Mean stack solver ms |
|---|---|---|
| Before / 1 | 2.585823 | 1.349694 |
| After (final) / 1 | 3.011850 | 1.777380 |
| Before / 8 | 10.152631 | 8.846321 |
| After (final) / 8 | 13.035774 | 11.504390 |

Both final 1/8-pass probes match every non-timing field from their first after-run exactly at printed precision, including after the reduction-initialization guard. All unaffected baseline scenarios retain identical non-timing fields before/after. Each probe invocation exits 0.

## Behavior Changes

Supported zero-TOI box faces now immediately emit 2-4 surface contacts, with a geometric face normal and individually computed penetration. Wider contact support changes sequential impulse response and measured stack behavior. Ordinary seed fallback and positive-TOI behavior remain available. Existing persistence may merge, retain, or replace the generated candidates independently.

## Known Limitations

- No SAT replacement; generic GJK/EPA still decides whether there is a collision and supplies the seed direction. Missed collisions and unsupported near-edge/corner directions are not repaired by clipping.
- The absolute clipping floor is 1e-6; small polygons can reduce to a line or single-witness fallback. Output is bounded and finite, not a guarantee of scale-independent solver behavior.
- The existing 0.02 manifold duplicate threshold can merge contacts on small boxes even when the helper emits distinct points. Rotating/moving patches can retain older anchors and normals. Feature-based persistence and warm-start coherence belong to Phase 23.
- Default one-pass stack stability regresses in the metrics above; eight-pass penetration also regresses. The existing position slop is retained. General box stacking remains an open audit concern.
- Multi-point contacts increase solver work. No performance conclusion or true Release-CRT claim is made.

## Out-of-Scope Findings

- `GEngine/include/GEngine/Physics/Manifold.cpp:38` and `:43` suppress a candidate when either anchor is within 0.02 of an existing anchor, without refreshing that contact's geometry/normal. `:51` applies the existing full-manifold average-distance replacement policy. These are concrete persistence limitations for Phase 23; they were not changed or established as the sole cause of the measured stack regressions.
- `GEngine/include/GEngine/Physics/ShapeBox.cpp:207` retains the off-origin parallel-axis tensor with positive off-diagonal products; `:244` uses local corner offsets with the supplied angular velocity/direction in the sweep-speed calculation. These previously recorded issues remain outside face generation.
- `GEngine/GEngine.vcxproj:86` and `PhysicsTests/PhysicsTests.vcxproj:87` retain the nominal Release `/MTd` override.
- Pre-existing `.gitignore:33` produces the whitespace diagnostic below. The file is outside Phase 22 and remains unchanged.

## git diff --check

```text
.gitignore:33: new blank line at EOF.
```

Global native exit code: **2**, from the pre-existing `.gitignore` change. The phase-owned tracked diff has no diagnostics and exit **0**. The new review is checked separately with `git diff --no-index --check NUL docs/physics/PHASE_22_REVIEW.md`; no whitespace diagnostics are permitted (native exit 1 denotes that the new file differs from NUL).

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/BoxContact.h
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_22_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

All **15/15** pre-existing user files retain their entry SHA-256 hashes. Only the four phase-owned files are changed/created by this phase. The index remains empty; HEAD remains the approved Phase 21 commit.

## Human Decision

PENDING

The execution skill stops at human review. No Phase 22 files have been staged or committed; no approved tag or push is performed, and Phase 23 is not started.
