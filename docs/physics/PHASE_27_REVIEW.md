# Phase 27 Review

## Status

PHASE 27 STATUS: AWAITING HUMAN REVIEW

## Objective

Bound all GJK simplex searches, expose per-query termination and iteration counts, and define a relative progress tolerance while preserving existing valid query outputs over the validated engine scale range.

## Baseline Commit

`7a3187f33b1851631ca23ac1ddcf449bf0682653` (`7a3187f physics: phase 26 add conservative angular swept bounds`), branch `physics/refactor`.

The execution preflight verified the previous approved tag exists at HEAD and no approved Phase 27 tag exists. Initial status and SHA-256 hashes were captured before implementation. The index was empty.

## Previous Approved Tag

`physics-phase-26-approved`, at the baseline commit above.

## Audit Findings Addressed

**PHYS-BUG-017**: the three GJK search loops had no hard iteration cap and used an absolute initial squared-distance bound of `1e10`. Their termination/progress policy was not exposed to callers. This phase adds a bounded, observable search contract and a relative squared-distance progress test.

The supported regression envelope is box/sphere/valid convex dimensions scaled by 0.1, 1, 10 and 100, with 0.5 also covered for analytic sphere distance. This is not a claim of arbitrary-scale collision robustness: existing simplex/contact/duplicate geometric thresholds remain as explicitly documented below. The historical audit is unchanged.

## Allowed Scope

One GJK correctness boundary: two production files, one existing test file, and this review; four files total. No EPA algorithm, profiler implementation, collision dispatcher, build configuration, or solver change.

## Files Changed

- `GEngine/include/GEngine/Physics/GJK.cpp`
- `GEngine/include/GEngine/Physics/GJK.h`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_27_REVIEW.md`

## Implementation Summary

- Each boolean, contact and closest-point simplex search permits at most **64 iterations**. An optional per-query unsigned budget can reduce this bound, including to zero; oversized requests are clamped to 64. The contact-returning boolean wrapper forwards both budget and diagnostics.
- Optional `GjkDiagnostics` reports an iteration count and typed termination: invalid input, invalid support, invalid simplex, separating axis, duplicate support, no progress, origin reached, iteration limit, or contact expansion failure. Diagnostics reset on entry and are independent of profiling configuration. No global diagnostic state is added.
- Budget exhaustion returns false or `GjkContactStatus::Failed`; distance and contact witness outputs remain zero. Partial unconverged witnesses are not published. Existing null/invalid geometry, bias and finite-support guards remain, and projected directions/barycentric weights are validated before further search.
- Replace the fixed initial squared-distance bound with infinity. Measure progress using matching squared-length units and a relative tolerance:
  `previous - current > (float_epsilon / 4) * previous`, with strict decrease also required.
- A quarter float epsilon is below one ULP of a normal float squared distance. This deliberately preserves representable progress and existing witness selection; the hard cap bounds sequences that keep making tiny improvements. This is a conservative convergence policy, not a performance shortcut.
- Preserve the existing float squared-distance and support-plane arithmetic when finite. Use double precision for those two scalar comparisons if the float operation overflows despite finite vector components. Simplex projection, support mapping, geometric tolerances and witness construction remain unchanged.
- Existing duplicate/no-progress exits retain their approximate separation semantics. Contact expansion failures are observable as one coarse reason; tetrahedron seeding and EPA internals are unchanged and remain bounded separately. Detailed EPA telemetry belongs to Phase 28.

## Tests Added / Modified

Added `--gjk-robustness`; all **10,052 checks** also run in the full suite.

- **429 geometry checks** were added and run first against unchanged Phase 26 GJK: all passed. Analytic sphere distances use scales 0.1/0.5/1/10/100. A seed of `0x27a91` generates 256 rotated/transformed box pairs over four scales with independent local-axis overlap and face-gap oracles, including both body orders.
- **23 termination checks** exercise zero/one-iteration exhaustion through all four public entry points, default and oversized budgets, invalid bodies/bias, initial and iterative non-finite support, exact diagnostic counts/reset, finite cleared outputs, duplicate degeneracy and separating-axis termination.
- **9,600 seeded query checks** cover 1,200 box/convex/sphere pairs, all nine ordered shape combinations, independent rotations, overlaps/separations and four scales. Seed: `0x270027`. They check exact repeated contact/distance witnesses and termination, boolean/contact overlap agreement, safe observable contact expansion failure, bounded iteration counts, finite outputs and byte-for-byte preservation of live bodies.
- All earlier contact, lifetime, angular, friction, stabilization, manifold, prediction and angular-sweep regressions remain registered and pass.

Analytic distance tolerance is **`5e-4 * scale`** in world units. Repeatability and live-state comparisons are exact. This tolerance was not increased after implementation.

Both final configurations report:

`GJK_FUZZ pairs=1200 max_iterations=34 safe_contact_failures=18 reasons=0,0,0,764,185,338,2295,0,18,`

The histogram counts one contact, boolean and distance query per pair (3,600 results): 764 separating-axis, 185 duplicate, 338 no-progress, 2,295 origin-reached and 18 contact-expansion-failure exits. No invalid-input/support/simplex or iteration-limit exit occurred for these valid seeded shapes. Budget-exhaustion paths are separately forced by the focused contract checks.

A temporary reference harness compiled the exact approved `GJK.cpp` into a separate nested namespace and compared it directly against the final implementation on the same 1,200 seeded pairs. **All contact statuses, contact witnesses and distance witnesses matched exactly**: maximum witness difference **0**, maximum iterations **34**, identical **18** safe contact failures. The comparison required witness differences no greater than `5e-4 * scale`.

An initial one-epsilon/double-comparison version produced two distance-witness differences above that unchanged tolerance. The production progress policy was corrected before final validation; no acceptance bound was relaxed. The final reference comparison passed. The temporary source/project artifacts are validation-only and are not phase deliverables.

## Validation Commands

Class B mathematical correctness plus supplemental deterministic stability comparisons. The current generated Visual Studio x64 projects and compiler flags were inspected. The sandbox process/patch helper failed to start repeatedly; scoped, reviewed execution performed reads, phase edits and builds. Process-local Path normalization handles the existing duplicate Path/PATH environment. No machine environment or project configuration changed.

```powershell
& .agents/skills/physics-phase-execution/scripts/Get-PhysicsPhasePreflight.ps1 -RepositoryRoot (Get-Location).Path -Phase 27
$phase27Path = $env:Path
Remove-Item Env:Path
$env:Path = $phase27Path
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Debug /p:Platform=x64 /clp:ErrorsOnly
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Release /p:Platform=x64 /clp:ErrorsOnly
.\bin\Debug\PhysicsTests\PhysicsTests.exe --gjk-robustness
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --gjk-robustness
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=1
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=8
git diff --check
git diff --check -- GEngine/include/GEngine/Physics/GJK.cpp GEngine/include/GEngine/Physics/GJK.h PhysicsTests/src/main.cpp
git diff --no-index --check NUL docs/physics/PHASE_27_REVIEW.md
git status --short
```

Baseline nominal Release build/full suite: **3,409/3,409**, before production changes. Final native build/test exits are 0. The final reference executable also exits 0. Both build configurations include engine, physics tests and benchmark targets.

Evidence logs: `bin/phase27-entry-status.log`, `bin/phase27-entry-hashes.log`, `bin/phase27-before-{build,tests,stability-1,stability-8}.log`, `bin/phase27-red-{build,tests}.log`, `bin/phase27-final-{debug,release}-{build,focused,tests}.log`, `bin/phase27-comparison-final.log`, and `bin/phase27-final-stability-{1,8}.log`. Normal-verbosity MSBuild file logs retain compiler/runtime warnings.

## Debug Result

x64 engine/tests/benchmark build: **pass**, exit 0. Focused **10,052/10,052**; full **13,461/13,461**, both exit 0. Zero observed known issues in the full suite's existing one non-gating diagnostic.

## Release Result

Nominal optimized x64 engine/tests/benchmark build: **pass**, exit 0. Focused **10,052/10,052**; full **13,461/13,461**, both exit 0. Existing `/MTd` overrides remain; this is not a true Release-CRT allocator/runtime result. Existing vendor/compiler/linker warnings remain.

## Stability Results

**Measured**, headless fixed geometry/materials/body order/gravity, fixed dt 1/120 s and 1,200 steps (10 seconds), separate default one-pass and eight-pass runs. Every scenario remains finite. **All non-timing CSV fields match the approved baseline exactly** in both pass configurations, including energy, speeds, penetration, contacts and solver iterations.

| Scenario / passes | Peak energy % | Final energy | Final average Y | Peak linear / angular speed | Final linear / angular speed | Moving bodies | Final manifolds / points | Peak / final floor penetration |
| --- | ---: | ---: | ---: | --- | --- | ---: | --- | --- |
| Stack / 1 | 100 | 860.015904 | 4.478818 | 0.703708 / 0.245699 | 0.322788 / 0.058755 | 3 | 19 / 72 | 0.047577 / 0.008523 |
| Stack / 8 | 100 | 863.821763 | 4.499072 | 0.065288 / 0.058103 | 0.000000 / 0.000000 | 0 | 16 / 64 | 0.000983 / 0.000891 |
| Lattice / 1 | 100 | 4278.979598 | 1.490630 | 14.400019 / 6.611325 | 6.408699 / 6.454880 | 180 | 182 / 371 | 0.198102 / 0.078580 |
| Lattice / 8 | 100 | 4168.224381 | 1.492660 | 14.300018 / 6.217107 | 5.687394 / 5.832269 | 180 | 188 / 375 | 0.181385 / 0.020635 |

The single sphere also matches exactly in both configurations: peak energy 100% of initial, final energy 17.998483, final Y 1.499874, final linear/angular speeds 0.001068/0.001066, zero moving bodies, one manifold/point, and peak/final penetration 0.000126. The moving threshold is 0.05 linear/angular speed. The eight-pass stack settles; default stack residual motion and all 180 moving lattice bodies remain baseline limitations.

The unchanged free asymmetric body reaches energy 5.432790 from 5.211667 (+4.242841%), with angular-momentum magnitude +2.635889%, peak angular speed 2.158473 and final angular speed 2.038718. This existing timestep-dependent integrator drift is unchanged, and the existing angular mathematical gates pass. It is not a GJK result.

These are measured compatibility results, not new physics acceptance tolerances. Extended probes use nominal Release; both configurations run all existing focused stability tests in the full suite.

## Benchmark Results

**Not available:** no performance benchmark or speedup claim is made. This is a GJK robustness phase. Iteration/termination measurements and exact numerical comparison are reported above. The supplemental stability probes' step/solver timings are diagnostic means, not controlled performance comparisons.

## Behavior Changes

Pathological GJK searches now stop after at most 64 iterations, or a smaller caller budget. Callers can inspect per-query termination without enabling the global profiler. Failed or exhausted searches return finite cleared outputs rather than partial witnesses. Ordinary validated contact and distance outputs match the approved implementation exactly in the reference corpus.

## Known Limitations

- The 64-step cap bounds simplex search work, assuming individual shape support calls terminate. Initial support, bounded tetrahedron seeding and the separately bounded EPA stage are excluded from the iteration count.
- This phase makes **progress** tolerance relative. The existing origin threshold `1e-4`, GJK duplicate threshold `1e-6`, dimensionless active-weight threshold and simplex degeneracy policy remain. Arbitrarily tiny shapes, huge coordinate offsets, near-degenerate hulls and full scale invariance are not claimed; the validated envelope is stated above.
- A no-progress or duplicate result is an approximate separation, not a mathematical proof for adversarial support maps. The diagnostics distinguish those exits from a separating axis. The cap may safely miss an actual collision that needs more than the budget.
- The legacy closest-point API still returns void; its optional diagnostics distinguish failed cleared outputs from valid coincident witnesses. Existing callers that omit diagnostics retain their interface and do not gain a new status channel. A unified query/dispatcher redesign remains Phase 40.
- `ContactExpansionFailed` groups existing tetrahedron-seeding and EPA failures. It deliberately does not claim a specific EPA cause or alter its topology/output validation. The 18 seeded safe misses are identical to the approved baseline.
- No sleeping, solver/integrator changes, profiler redesign, allocation optimization or parallel execution is included. No sanitizer/Application Verifier run was performed.

## Out-of-Scope Findings

- EPA still has its separate 64-iteration cap and absolute `0.001` duplicate threshold (`GJK.cpp:1093,1248,1418`). Its safe failure can suppress an overlapping contact; the seeded corpus measures 18 such coarse expansion failures, exactly matching Phase 26. Detailed causes and tolerance policy are Phase 28 work.
- The collision dispatcher still calls the void closest-point API after a separated contact query without a result-status return (`PhysicsSystem.cpp:623`). This phase preserves its existing failed-output convention and adds optional diagnostics only; combining searches/dispatch is Phase 40.
- Nominal Release still receives the Debug CRT override in `GEngine/GEngine.vcxproj:86`, `PhysicsTests/PhysicsTests.vcxproj:87`, and `PhysicsBenchmark/PhysicsBenchmark.vcxproj:87`.
- `.gitignore:33` has the pre-existing extra blank line at EOF. The unrelated Scene file emits the existing LF-to-CRLF warning. Both are unchanged.

## git diff --check

Literal global output:

```text
warning: in the working copy of 'GEngine/src/Scene/_Scene.cpp', LF will be replaced by CRLF the next time Git touches it
.gitignore:33: new blank line at EOF.
```

Global exit **2**, entirely pre-existing. The three phase-owned tracked-file check has no output and exit **0**. The new review passes the separate no-index whitespace check with no diagnostics; native exit 1 indicates a new file differs from NUL.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/GJK.cpp
 M GEngine/include/GEngine/Physics/GJK.h
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
?? docs/physics/PHASE_27_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/rendering/
```

All **24/24** pre-existing entries retain their entry hash/existence state, including existing deletions. Only the four listed Phase 27 files are deliverables. Temporary comparison source is retained in ignored bin/phase27-comparison-source.log and bin/phase27-reference-gjk-source.log; its temporary project was removed. Build/log artifacts remain ignored. HEAD and the empty index are unchanged. No commit, approved tag, push or next-phase work was performed.

## Human Decision

PENDING

Review the capped-query failure behavior, optional diagnostics/legacy void-distance limitation, supported scale envelope and conservative relative progress threshold.

The [physics-phase-execution skill](../../.agents/skills/physics-phase-execution/SKILL.md) requires: "Then stop. Never stage files for approval, commit, create an approved tag, push, approve the implementation, invoke the approval skill, or begin Phase XX+1." This implementation stops at that human-review gate.
