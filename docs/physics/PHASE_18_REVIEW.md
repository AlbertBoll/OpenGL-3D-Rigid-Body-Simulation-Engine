# Phase 18 Review

## Status

PHASE 18 STATUS: AWAITING HUMAN REVIEW

Revision validation is complete: the exact final audit-floor fixture fails 66/176 checks against the approved Phase 17 production baseline and passes 176/176 after byte-for-byte restoration of Phase 18, in both Debug and nominal Release. Full suites pass 914/914 and ballistic suites pass 180/180. Only this review changed permanently during the revision. Global settling is unresolved: the ten-second lattice's final maximum speed increases from 11.629561 to 15.614874 units/s, and stack residual motion also increases. These regressions require human assessment; this phase does not establish general stack/lattice stability.

## Objective

Prevent low-speed ballistic contacts from repeatedly receiving restitution while preserving higher-speed bounce, with a documented threshold and material combination rule.

## Baseline Commit

`b0b9788e6e28fceb082f5954c4c5e5ce2e148c35`

`physics: phase 17 guard ballistic impacts and bound friction`

Branch: `physics/refactor`. The required preflight verified the previous approved tag is an ancestor of HEAD and no Phase 18 approved tag exists. No existing Phase 18 review or nested AGENTS.md was found. The initial index was empty.

## Previous Approved Tag

`physics-phase-17-approved`, resolving to the baseline commit above.

## Audit Findings Addressed

`PHYS-BUG-015`: positive-TOI contacts previously received material restitution at every closing speed. The audit remains a historical baseline; remaining TOI revalidation and resting solver findings are not resolved here.

## Allowed Scope

One production file, one dedicated test file, and this review: three files total. Class B mathematical validation and Class C deterministic stability evidence. No configuration header is needed for this compile-time policy.

## Files Changed

- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_18_REVIEW.md`

## Implementation Summary

The resolver uses a named compile-time threshold of **1 world unit/s**. The normal remains world-space B -> A. With current contact-point velocities, including angular velocity on both participants:

~~~text
vn = dot((vA + omegaA cross ra) - (vB + omegaB cross rb), normal)
effectiveRestitution = elasticityA * elasticityB   when -vn > 1
effectiveRestitution = 0                           otherwise
Jn = -(1 + effectiveRestitution) * vn / normalEffectiveInverseMass
~~~

Existing finite closing-speed and effective-mass guards still control whether the repulsive impulse can be applied. Equality at the threshold is inelastic. Low-speed impacts still receive the normal impulse needed to stop approach.

Material combination remains symmetric multiplication. For ordinary coefficients in [0, 1], the product is in [0, 1]; 0.5 and 0.8 give 0.4. Public elasticity fields are not newly clamped or validated. Higher-speed product behavior remains unchanged.

Friction continues to use the normal impulse actually applied, so its low-speed Coulomb support reflects the inelastic response. No damping, sleeping, solver iteration, event ordering, or geometry changes were made.

**Derived:** an impact speed of 1 corresponds to a free-fall height of `1 / (2g)`, about 0.0417 world units at gravity 12 for a stationary support without angular coupling. The threshold is an explicit scene-scale policy independent of timestep. It was not scaled from gravity or tuned during testing.

## Tests Added / Modified

Added `PhysicsTests.exe --restitution-threshold`, also included in the full suite:

- **144 analytic checks:** closing speeds 0, 0.5, 0.9999, 1, 1.0001, and 4; Static/Dynamic/Kinematic support; both A/B orders; unequal masses; materials 0.5/0.8, 0.8/0.5, 0/1, and 1/1. Check finite state, analytic impulses, and outgoing relative normal speed. Large tangential speed and shared translation must not activate restitution.
- **8 angular checks:** spin raises a slow COM approach above the threshold or reduces a fast COM approach below it. Cover spinning dynamic A, spinning kinematic B, and reversed order.
- **24 world-drop checks:** low/high release heights 0.02/0.5, unit sphere, gravity -12, 120/240 Hz, both body creation orders, four simulated seconds. Use the established audit floor half-extents (50, 0.5, 50), translated so its top is y=0.

The existing 180-check ballistic suite now expects inelastic normal impulse at closing speeds 0.001 and 0.5. Coulomb, finite-state, mass, and energy checks retain their tolerances; higher-speed expectations remain unchanged.

Full suite: **738 -> 914 checks**, with no new expected-failure exemptions.

### Acceptance tolerances

- Analytic velocities: absolute `2e-5`; all new outputs finite.
- Every sample in the final second of a drop: linear speed <= 0.05 units/s, angular speed <= 0.05 rad/s, absolute sphere-bottom/floor gap <= 0.005 units. Final manifold contact count must be positive.
- Maximum penetration <= 0.02 units; peak mechanical energy <= initial + 0.005.
- Low drop, elasticity 1/1: first impact below 1 unit/s, maximum upward speed <= 0.05.
- High drop, material product 0.4: first approach and rebound both above 1 unit/s; rebound/approach ratio within 0.02 of 0.4.

### Same-fixture red -> green validation

**Measured during the requested revision.** The exact final tests, including floor half-extents **(50, 0.5, 50)**, were run against the production source from `physics-phase-17-approved`, then against the restored Phase 18 source. Neither tests, geometry, tolerances, nor production policy changed during this revision.

| Configuration | Phase 17 baseline (red) | Restored Phase 18 (green) |
|---|---|---|
| Debug x64 | 66 of 176 checks failed, exit 1 | 176 of 176 passed, exit 0 |
| Nominal Release x64 | 66 of 176 checks failed, exit 1 | 176 of 176 passed, exit 0 |

Both baseline builds succeeded. The 66 failures in each configuration are:

- 54 relative-normal-speed/material threshold checks.
- 4 angular contact-speed threshold checks.
- 4 low-drop mechanical-energy bounds.
- 4 low-drop rebound suppression checks.

The low-drop energy checks failed on energy growth, not non-finite state or excessive penetration: peak energy was 12.2849 at 120 Hz and 12.2674 at 240 Hz, versus initial 12.24 and the unchanged +0.005 tolerance. All baseline drop rows report finite = 1. The baseline already passed all eight final-window settling checks on this floor, and its high-speed rebound checks passed. The red result therefore demonstrates unwanted low-speed bounce/energy and incorrect threshold selection, not a baseline failure to reach the final resting criterion.

Both creation orders give these first-rebound results:

| Drop / rate | Phase 17 rebound | Phase 18 rebound |
|---|---:|---:|
| Low / 120 Hz | 0.7 | -1.91928e-8 |
| Low / 240 Hz | 0.7 | -1.91733e-8 |
| High / 120 Hz | 1.4 | 1.4 |
| High / 240 Hz | 1.38 | 1.38 |

All eight printed drop rows match between Debug and nominal Release within each red/green state.

The baseline source came directly from `git show physics-phase-17-approved:GEngine/include/GEngine/Physics/PhysicsSystem.cpp`. During the red runs, `git diff --quiet physics-phase-17-approved -- GEngine` returned 0. The final test source stayed in place. After both baseline builds/tests, a `finally` block restored the saved Phase 18 source byte-for-byte before either green build.

SHA-256 evidence:

- Phase 17 source bytes loaded from the tag: `f1f42324c038069be36d0df8795c7cc274c259c0a604966980c3a5d40f1f15c3`.
- Phase 18 source before/after the temporary substitution: `d02e853f6634c5f13df45344a106e60b8b3fad0bc05891742ffd59a2a383b141`.
- Identical final test source throughout red and green: `a091b92502f7782966d7694edbe12a48bc59739d253384a3e3d183850625429f`.

No implementation defect was exposed. Only this review document changed permanently during the revision.

### Separate small-floor limitation (original implementation run)

The initial new suite against unchanged Phase 17 production code failed **74 of 176** checks, exit 1 (`bin/phase18-red-focused.log`). That version used a smaller floor with half-extents (5, 0.5, 5). After threshold implementation, direct response and rebound checks passed, but eight full-settling checks still failed on that floor (`bin/phase18-Debug-focused.log`).

With the threshold, the small-floor low drop had effectively zero initial rebound yet retained final-window linear speed 0.351535 at 120 Hz and 0.126829 at 240 Hz, with almost equal angular speed and four persistent manifold points. High drops also retained rolling. Baseline small-floor drops already showed residual rolling. No contact-path correction was attempted.

The final drop fixture uses the established audit single-sphere floor geometry. Speed, gap, penetration, energy, and rebound tolerances were unchanged. The same-fixture 66/176 red -> 176/176 green evidence above supersedes the original small-floor result as Phase 18 failure-before evidence. The small-floor limitation remains documented; full-settling acceptance is limited to the final stated geometry.

## Validation Commands

Inspected the current generated Visual Studio solution/project settings. Built GEngine, PhysicsTests, and PhysicsBenchmark in x64 Debug and nominal Release.

~~~powershell
$physicsBuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $physicsBuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' '.\GEngine.sln' /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=<Debug-or-Release> /p:Platform=x64 /clp:ErrorsOnly /fl '/flp:logfile=bin\phase18-<stage>-<configuration>-build.log;verbosity=normal'

.\bin\Debug\PhysicsTests\PhysicsTests.exe --restitution-threshold
.\bin\Debug\PhysicsTests\PhysicsTests.exe --ballistic-contact
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --restitution-threshold
.\bin\Release\PhysicsTests\PhysicsTests.exe --ballistic-contact
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline
~~~

Baseline nominal Release build/full suite (738 checks) and all four probes passed before changes. Final tests use the final source and floor geometry.

Ignored evidence logs:

- `bin/phase18-before-Release-build.log`, `bin/phase18-before-Release-tests.log`
- `bin/phase18-before-stability.log`, `bin/phase18-after-stability.log`
- `bin/phase18-red-Debug-build.log`, `bin/phase18-red-focused.log`
- `bin/phase18-Debug-build.log`, `bin/phase18-Debug-focused.log` (small floor)
- `bin/phase18-audit-floor-Debug-build.log`, `bin/phase18-audit-floor-Debug-focused.log` (final Debug source)
- `bin/phase18-final-Debug-ballistic.log`, `bin/phase18-final-Debug-tests.log`
- `bin/phase18-final-Release-build.log`, `bin/phase18-final-Release-focused.log`
- `bin/phase18-final-Release-ballistic.log`, `bin/phase18-final-Release-tests.log`
- `bin/phase18-unrelated-before.log` (initial unrelated-file SHA-256 snapshot)

The sandbox launcher and patch helper failed before filesystem access. Scoped approved operations outside the sandbox performed the work. No build settings changed.


### Revision rerun commands and logs

The saved validation helper performed the guarded production-source substitution, red builds/tests in both configurations, and automatic source restoration, then the green builds and all focused/full suites:

~~~powershell
python bin/phase18-revision-validation.log red
python bin/phase18-revision-validation.log green
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline
.\bin\Debug\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline
~~~

The `.log` helper contains Python source and is an ignored validation artifact. It stores the entry-state hashes and a recovery copy before substitution, asserts that the engine production tree matches the approved tag during red testing, and asserts source/test hashes after restoration. Baseline and restored builds each use the same inspected x64 project configuration. The revision ran the probe configurations sequentially after the test suites.

Revision evidence logs:

- `bin/phase18-revision-red-{Debug,Release}-build.log` and corresponding `-build-msbuild.log`.
- `bin/phase18-revision-red-{Debug,Release}-focused.log`.
- `bin/phase18-revision-green-{Debug,Release}-build.log` and corresponding `-build-msbuild.log`.
- `bin/phase18-revision-green-{Debug,Release}-{focused,ballistic,full}.log`.
- `bin/phase18-revision-{Debug,Release}-stability.log`.
- `bin/phase18-revision-snapshot.log`, `bin/phase18-revision-results.log`, `bin/phase18-revision-phase18-source.log`.

## Debug Result

PASS, exit 0:

- Revision final GEngine/PhysicsTests/PhysicsBenchmark build: 0 errors, 4 existing warning reports. The unchanged test object was reused; its prior compilation reported 64 warnings.
- `Restitution-threshold regression: 176 checks passed`.
- `Ballistic-contact regression: 180 checks passed`.
- `PhysicsTests: 914 checks passed`.
- Existing non-gating lifetime diagnostic: zero observed known issues.
- Revision deterministic stability probes: all four finite, exit 0.

## Release Result

PASS, exit 0:

- Revision final GEngine/PhysicsTests/PhysicsBenchmark build: 0 errors, 5 existing warning reports, including the CRT override. The unchanged test object was reused; its prior compilation reported 72 warnings.
- `Restitution-threshold regression: 176 checks passed`.
- `Ballistic-contact regression: 180 checks passed`.
- `PhysicsTests: 914 checks passed`.
- All four world probes: finite = 1.

Release remains optimized with the documented Debug static CRT `/MTd` override. Allocator/runtime behavior is not a true Release-CRT comparison.

## Stability Results

### Focused sphere drops

**Measured**, final Debug and nominal Release; printed physical metrics match. Both creation orders produce the same listed values.

| Rate / drop | First closing speed | First rebound | Final-second max linear speed | Final-second max angular speed | Max penetration |
|---|---:|---:|---:|---:|---:|
| 120 Hz / low | 0.7 | -1.91928e-8 | 0.000405510 | 0.000404700 | 0.00000613928 |
| 240 Hz / low | 0.7 | -1.91733e-8 | 0.000405721 | 0.000404911 | 0.00000566244 |
| 120 Hz / high | 3.5 | 1.4 | 0.000328953 | 0.000328296 | 0.000192761 |
| 240 Hz / high | 3.45 | 1.38 | 0.000421978 | 0.000421135 | 0.00000625849 |

Low-drop initial/peak energy is 12.24; high-drop initial/peak energy is 18. All eight runs retain one manifold with one point. Maximum final-second gap is 0.000192761. Peak linear speed is 0.6/0.65 for low drops and 3.4 for high drops; maximum angular speed across all focused runs is 0.0005937 rad/s.

### Existing ten-second world probes

Revision reruns completed successfully in both Debug and nominal Release (exit 0). All 80 non-timing CSV fields across the four probes match each other and the original Phase 18 after-run at printed precision; all finite flags are 1. The table below is preserved, including stack residual-motion growth and lattice final linear/angular speed increases. Probe timings are not used as a correctness equivalence criterion.

**Measured**, approved Phase 17 -> Phase 18, nominal Release. Identical existing geometry, materials, creation order, gravity, fixed `1/120` timestep, 1,200 steps, no randomness, no discarded warmup, and moving thresholds 0.05 units/s or rad/s.

| Metric | 4x4 box stack | Single sphere | 180-sphere lattice |
|---|---:|---:|---:|
| Peak energy / initial | 100% -> 100% | 100% -> 100% | 102.332471% -> 102.332486% |
| Final total energy | 862.684857 -> 862.686012 | 18.013751 -> 17.998483 | 5570.047855 -> 5769.001126 |
| Final average Y | 4.493115 -> 4.493112 | 1.501146 -> 1.499874 | 1.472727 -> 1.458899 |
| Peak linear speed | 0.215006 -> 0.215006 | 14.200018 -> 14.200018 | 21.224272 -> 20.800062 |
| Final max linear speed | 0.081224 -> 0.097994 | 0.001042 -> 0.001041 | 11.629561 -> 15.614874 |
| Peak angular speed | 0.090607 -> 0.090607 | 0.001040 -> 0.001039 | 11.389524 -> 12.364333 |
| Final max angular speed | 0.015761 -> 0.019983 | 0.001040 -> 0.001039 | 9.088459 -> 10.979907 |
| Final moving bodies | 1/16 -> 2/16 | 0/1 -> 0/1 | 180/180 -> 180/180 |
| Average generated contacts/step | 15.894167 -> 15.940833 | 0.003333 -> 0.808333 | 205.105833 -> 207.580833 |
| Final manifolds / points | 16 / 52 -> 16 / 52 | 1 / 1 -> 1 / 1 | 177 / 652 -> 174 / 626 |

The free asymmetric body's physical fields remain unchanged at printed precision: initial energy 5.211667, peak/final 5.432790 (104.242841%), final angular speed 2.038718. Existing integration drift remains.

The lattice still gains energy above its initial value and does not settle. Its final residual speeds and the stack's residual motion worsen. Thresholding changes trajectories through an otherwise unchanged solver/contact pipeline; these results do not isolate a single cause. Phase 17's diagnostic identified an existing resting-stage energy source, but no new stage-isolation claim is made here.

World-wide penetration, solver residual, and per-scenario solver stage timing are **Not available** in this CSV. The new drop tests separately measure floor penetration. One outer solver pass remains; sleeping does not exist.

## Benchmark Results

Class B/C correctness/stability work. Controlled Class D performance results are **Not available**; collision-heavy/separated benchmarks were not required or run. Probe logs contain **Measured** mean trajectory timings, not repeated-sample medians. Some probe execution overlapped independent suite execution, and trajectories/contact counts changed. No performance claim is made.

## Behavior Changes

- Closing normal contact speeds <= 1 world unit/s use zero restitution; faster impacts retain the symmetric elasticity product.
- Tangential/shared translation cannot activate restitution for slow normal approach; spin at either anchor can affect activation.
- Coulomb friction uses the resulting applied normal impulse.
- Normal routing still sends zero-TOI resting contacts to manifolds. The shared resolver's existing direct zero-TOI projection remains.

## Known Limitations

- Fixed compile-time scene-scale threshold, not a runtime world/material setting.
- Elasticity values outside [0, 1] retain legacy behavior.
- Full settling is demonstrated for the stated audit-sized floor, not arbitrary extents. The small floor retains rolling after its low-speed rebound is eliminated.
- Stack/lattice stability, warm starting, velocity stabilization, and stale TOI geometry remain unresolved.
- No sanitizer, Application Verifier, or interactive renderer validation was run.

## Out-of-Scope Findings

Current source evidence, left unchanged:

- `PhysicsSystem.cpp:554` uses generic GJK/EPA sphere/box witnesses; `Manifold.cpp:8` accumulates contacts. The small-floor fixture retained four points and rolling motion. This demonstrates geometry-sensitive residual motion, not a proven GJK/EPA or manifold root cause.
- `PhysicsSystem.cpp:375`: one outer solver traversal (`PHYS-BUG-005`, Phase 19).
- `Constraints/ConstraintPenetration.cpp:170,188`: Baumgarte enters the physical velocity RHS (`PHYS-BUG-008`, Phase 20).
- `PhysicsSystem.cpp:357,415`: TOIs sorted once and resolved without full event revalidation (`PHYS-BUG-010`, Phase 25).
- `PhysicsSystem.cpp:727,770`: conservative advancement advances/rewinds live bodies (`PHYS-BUG-009`, Phase 24).
- `PhysicsTests/PhysicsTests.vcxproj:87`: Release `/MTd` override (`PHYS-BUG-024`).
- `.gitignore:33`: pre-existing added blank line at EOF.

Production basenames above are under `GEngine/include/GEngine/Physics/` unless explicitly repository-relative.

## git diff --check

Git uses command-local `-c safe.directory=C:/dev/GEngine-physics`.

Phase-source result:

~~~text
git diff --check -- GEngine/include/GEngine/Physics/PhysicsSystem.cpp PhysicsTests/src/main.cpp
exit 0
~~~

No whitespace diagnostics. LF-to-CRLF notices are informational. The new review is separately checked with `git diff --no-index --check -- NUL docs/physics/PHASE_18_REVIEW.md`. Result: exit 1, no whitespace diagnostics; the nonzero status represents the new-file difference against NUL.

Repository-wide result:

~~~text
git diff --check
.gitignore:33: new blank line at EOF.
exit 2
~~~

The initial unrelated issue is preserved under the mandatory ownership boundary, consistent with prior phase workflows. This is not reported as a clean repository-wide check.

## git status --short

~~~text
 M .gitignore
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_18_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
~~~

The index remains empty. All entry-state files except this review match the revision snapshot byte-for-byte. The pre-existing RigidBodySimulation scene edits differ from the original implementation-turn snapshot and were preserved exactly as found at revision entry. Nothing was staged, committed, tagged, or pushed; Phase 19 was not started.

## Human Decision

PENDING

Review the same-fixture red -> green evidence, unchanged production policy, settling tolerances and geometry limitation, and preserved stack/lattice residual-motion regressions.
