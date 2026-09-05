# Phase 15 Review

## Status

PHASE 15 STATUS: AWAITING HUMAN REVIEW

## Objective

Define and enforce the contact A/B/normal convention, fixing normal reversal when an incoming contact is reordered into an existing manifold. Address only Phase 15, canonical contact orientation.

## Baseline Commit

`89cad7be551ac335404b5b52a264511e34f3aff6`

`physics: phase 14 enforce body type and mass invariants`

Branch: `physics/refactor`. The execution-skill preflight verified the preceding approved tag is an ancestor of HEAD and no approved Phase 15 tag exists. Unrelated initial modifications and untracked files were preserved. No nested AGENTS.md was found.

## Previous Approved Tag

`physics-phase-14-approved`, resolving to the baseline commit above.

## Audit Findings Addressed

`PHYS-BUG-006`: manifold pair reordering swaps bodies and anchors without reversing the normal. This inverted the derived normal constraint and could expire a still-penetrating contact.

The historical audit and the pre-existing untracked plan were left unchanged. The selected Phase 15 contract is specified below and in Contact.h.

## Allowed Scope

Two production files, one dedicated test file, and this review: four files total.

The production change is one normal negation plus comments documenting the existing contact and solver conventions. Collision generation and the existing constraint equations already agree through the conversion described below; neither requires a code change.

## Files Changed

- `GEngine/include/GEngine/Physics/Contact.h`
- `GEngine/include/GEngine/Physics/Manifold.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_15_REVIEW.md`

## Implementation Summary

### Contact convention

For a successful contact, `contact_t::normal` is a unit **world-space B -> A** normal: the direction of a repulsive normal impulse on A. A/B labels on both local and world anchors identify the owning body.

Reversing the body labels requires all of:

- swap A and B body pointers;
- swap A and B world-space anchors;
- swap A and B local-space anchors;
- negate the normal;
- preserve signed separation and time of impact.

Canonical here means consistent orientation relative to the stored body labels. Manifolds retain their first contact's body order; this phase introduces no identity sorting or pair-index policy.

`Manifold::AddContact()` now negates the incoming normal in the same branch that swaps its bodies and anchors. Duplicate suppression, replacement, and warm-start storage policies are unchanged.

### Solver conversion and mathematical justification

The resting solver uses an internal A -> B axis `s = -normal`. The manifold rotates this axis into body A's frame; PreSolve rotates it back to world space. The existing normal Jacobian is:

```text
J = [-s, -(rA x s), +s, +(rB x s)]
impulseA = -s * lambda = normal * lambda
impulseB = +s * lambda = -normal * lambda
```

For nonnegative normal lambda this is repulsive. The existing signed penetration calculation `dot(pointB - pointA, s)` is negative for penetration. Both remain invariant under a consistent body/anchor/normal swap. Correcting only the incoming normal therefore repairs both impulse orientation and expiry depth without changing solver equations.

The ballistic resolver uses the B -> A contact normal directly. For a closing contact its signed scalar impulse is negative and the existing opposite applications to A/B yield repulsion. Focused tests verify its off-center response against analytic impulses in both body orders. Separating guards and friction policy remain later phases.

## Tests Added / Modified

Added `PhysicsTests.exe --contact-convention`: **96 gating checks**, also included in the full suite.

- Promoted the Phase 06 contact-order diagnostic from non-gating XFAIL to a required regression.
- Sphere/sphere, box/box, and sphere/box queries, each with aligned and rotated frames, in both static-overlap and positive-TOI paths. Both A/B orders must produce finite contacts, correct body labels, unit B -> A normals, compatible separation/TOI, and local anchors that reconstruct the world anchors at impact.
- The dedicated swept-sphere entry point additionally checks reversed normals and swapped witnesses.
- Resting and ballistic single-contact responses with unequal masses, different body orientations, and off-center lever arms must match analytic linear and angular impulses in both orders. Friction is zero to isolate the normal equation. Resting uses zero penetration bias; ballistic uses material restitution product 0.25.
- Two synthetic contact anchors test insertion into an existing manifold with canonical input, alternating input order, and reversed initial manifold order. These are solver-boundary fixtures, not sphere-surface generation tests.
- Stored bodies, both anchor spaces, normal, separation, and TOI are checked. Penetrating contacts survive expiry; moving the bodies apart expires them.
- Three repeated solver preparations capture both cold response and actual cached warm-start response, including angular velocity. Four test-local solve passes isolate permutation equivalence; the production solver iteration count remains one.

### Failure-before/fix-after evidence

After adding tests but before editing production files, the Debug build succeeded and the focused suite failed **7 of 96** checks, exit 1 (`bin/phase15-red-focused.log`). Failures were the original normal reversal, incorrect stored normal, premature expiry, and changed cold/warm response for reordered contacts. Collision-output and analytic single-contact tests already passed.

After the one-line fix, all 96 focused checks pass in Debug and Release. The normal dot diagnostic changes from -1 to +1. The full suite increases from the preceding phase's 270 gating checks to **366**; the one remaining non-gating lifetime diagnostic reports XPASS.

### Numerical tolerances

- Analytic linear/angular impulses and stored anchor comparisons: component tolerance `1e-5`.
- Cold/warm permutation response and anchor reconstruction at impact: component tolerance `2e-5`.
- Generated normal magnitude: `1e-4` from unit length.
- Generated normal orientation: dot with the expected axis > 0.99; direct/reversed normal dot < -0.99. This allows existing GJK/EPA witness variation without requiring a particular feature witness.
- Separation/TOI comparison: `0.002` units/seconds. Positive-TOI fixtures also require the analytic 0.5-second impact within 0.002 seconds.
- Reordered stored separation/TOI: exact equality.
- Valid tested contact and body outputs must be finite.

## Validation Commands

The current Visual Studio solution and generated x64 projects were inspected. Physics profiling is enabled in the engine/benchmark. Each configuration uses:

```powershell
$physicsBuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $physicsBuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  '.\GEngine.sln' /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' `
  /p:Configuration=<Debug-or-Release> /p:Platform=x64 /clp:ErrorsOnly `
  /fl '/flp:logfile=bin\phase15-<configuration>-build.log;verbosity=normal'
```

```powershell
.\bin\Debug\PhysicsTests\PhysicsTests.exe --contact-convention
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --contact-convention
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline
```

The stability command ran before rebuilding the Release benchmark with Phase 15 and again after the final Release build. Both runs use identical fixed geometry, creation order, materials, gravity, 1/120-second timestep, and 1,200 steps (ten seconds). There is no randomness or discarded trajectory window. Moving thresholds are 0.05 units/s and 0.05 rad/s.

Ignored local evidence:

- `bin/phase15-before-Release-build.log`
- `bin/phase15-red-Debug-build.log`
- `bin/phase15-red-focused.log`
- `bin/phase15-{Debug,Release}-{build,focused,tests}.log`
- `bin/phase15-{before,after}-stability.log`

The first redirected baseline Release-test attempt produced no usable result: the executable was subsequently found missing despite MSBuild's up-to-date report. No baseline full-Release test pass is claimed for that attempt. The baseline benchmark executable remained available and produced the recorded before measurements; all non-timing baseline values match the preceding review at printed precision. Rebuilding the final Release sources produced an executable that passed both required test modes.

The sandbox process/patch helper repeatedly failed before launching operations. Approved PowerShell commands performed the scoped reads, edits, and validation. This did not change repository/build settings.

## Debug Result

PASS, exit 0:

- GEngine / PhysicsTests / PhysicsBenchmark build: 0 errors, 134 existing vendor/project warning reports.
- `Contact-convention regression: 96 checks passed`.
- `PhysicsTests: 366 checks passed`.
- No new non-finite output observed.

## Release Result

PASS, exit 0:

- GEngine / PhysicsTests / PhysicsBenchmark build: 0 errors, 139 vendor/project/toolchain warning reports, including the existing CRT override.
- `Contact-convention regression: 96 checks passed`.
- `PhysicsTests: 366 checks passed`.
- All four stability scenarios report finite = 1, exit 0.

Release remains the nominal optimized configuration with the pre-existing Debug static CRT `/MTd` override. These results do not represent true Release-CRT allocator/runtime behavior.

## Stability Results

**Measured**, before -> after, using the identical nominal Release deterministic ten-second workload:

| Metric | 4x4 box stack | Single sphere | 180-sphere lattice |
|---|---:|---:|---:|
| Peak energy / initial | 119.622471% -> 100% | 100% -> 100% | 100% -> 100% |
| Final total energy | 968.917900 -> 862.311111 | 18.000989 -> 18.000989 | 4039.911347 -> 4078.307824 |
| Final average Y | 4.167445 -> 4.491204 | 1.500082 -> 1.500082 | 1.340196 -> 1.472101 |
| Peak linear speed | 17.619677 -> 0.216104 | 14.200018 -> 14.200018 | 28.570679 -> 15.825293 |
| Final maximum linear speed | 8.054067 -> 0.002784 | 0.000816 -> 0.000816 | 28.570679 -> 11.203113 |
| Peak angular speed | 14.888249 -> 0.059539 | 0.024529 -> 0.024529 | 10.351630 -> 11.950723 |
| Final maximum angular speed | 14.888247 -> 0.000534 | 0.000814 -> 0.000814 | 8.533093 -> 7.047541 |
| Final moving bodies | 13/16 -> 0/16 | 0/1 -> 0/1 | 176/180 -> 179/180 |
| Average generated contacts/step | 16.406667 -> 14.680833 | 0.795000 -> 0.795000 | 202.089167 -> 197.503333 |
| Final manifolds / points | 17 / 52 -> 17 / 66 | 1 / 1 -> 1 / 1 | 201 / 734 -> 192 / 689 |
| Average step ms | 2.074902 -> 2.918862 | 0.053295 -> 0.053867 | 17.152178 -> 17.338193 |

All physical CSV fields for the asymmetric free body and single sphere are unchanged at printed precision. The free body's peak/final angular energy remains 104.242841% of initial; final angular speed remains 2.038718 rad/s. Its mean step time changes from 0.000251 to 0.000199 ms.

The tested stack has no energy peak above its initial value and finishes below both motion thresholds. The lattice remains moving: its peak angular speed and final moving-body count increase even as its maximum linear speeds decrease. This phase establishes orientation correctness; it does not claim general stack/lattice stability acceptance or fix the remaining solver/CCD findings.

Penetration depth, per-scenario solver timing, and constraint residuals are **Not available** in this harness. There is one production solver traversal per step and no sleeping. Future phase stability gates still apply. No regression thresholds were loosened to accommodate these changes.

## Benchmark Results

Phase 15 is Class B mathematical correctness, not a performance optimization. Collision-heavy/separated performance benchmarks were not required or run.

The mean trajectory step times above are **Measured**, from 1,200 steps without a discarded warmup window. They are not multi-sample benchmark medians and no speedup is claimed. In particular, the stack run takes longer while retaining more valid manifold points; this measurement does not isolate the cost of individual solver/manifold operations.

## Behavior Changes

Incoming contacts whose A/B order differs from their existing manifold now retain the correct physical orientation after reordering. Their stored normal, solver axis, expiry depth, and applied/warm-started impulses are consistent.

Same-order insertion, narrow-phase generation, ballistic response equations, friction, restitution, Baumgarte, solver iteration policy, and contact reduction logic are unchanged.

## Known Limitations

- This locks the contact convention for successful supported queries and explicitly tests rotated primitive cases. It does not define a unique geometric normal for coincident centers or replace GJK/EPA degeneracy/failure policy. Failed collision queries can still contain partial output.
- Friction is disabled in analytic/permutation response fixtures to isolate the normal contract; friction-law correctness remains Phase 16/17.
- Manifold feature matching, normal coherence after geometry changes, and general warm-start validity remain later work.
- The stack result is one fixed ten-second workload, not proof of general resting stability. The lattice still has substantial residual motion.
- Angular integration drift, live-body CCD rewind, and Release CRT limitations remain.
- No sanitizer, Application Verifier, or renderer acceptance run was performed.

## Out-of-Scope Findings

Current source evidence, left unchanged:

- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp:374` retains one solver iteration (`PHYS-BUG-005`, Phase 19).
- `GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp:133` retains velocity Baumgarte and `:171` retains the gravity-like friction floor (`PHYS-BUG-008/007`, Phases 20/16).
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp:724` and `:767` still rewind live bodies with negative time (`PHYS-BUG-009`, Phase 24).
- `PhysicsTests/PhysicsTests.vcxproj:87` still applies `/MTd` in Release (`PHYS-BUG-024`).
- `.gitignore:33` contains the same pre-existing extra blank line at EOF. This unrelated user file was preserved.

## git diff --check

Git commands use command-local `-c safe.directory=C:/dev/GEngine-physics`.

Phase-source result:

```text
git diff --check -- GEngine/include/GEngine/Physics/Contact.h GEngine/include/GEngine/Physics/Manifold.cpp PhysicsTests/src/main.cpp
exit 0
```

No phase-source whitespace diagnostics. The untracked review is checked separately using `git diff --no-index --check -- NUL docs/physics/PHASE_15_REVIEW.md`; a new-file difference can return 1 without a whitespace diagnostic. Existing LF-to-CRLF notices are informational.

Repository-wide result:

```text
git diff --check
.gitignore:33: new blank line at EOF.
exit 2
```

Only the unrelated pre-existing whitespace issue remains. It is not repaired because AGENTS.md requires preserving unrelated work. Phase-scoped whitespace validation passes; repository-wide whitespace validation is not clean.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Contact.h
 M GEngine/include/GEngine/Physics/Manifold.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_15_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
```

The index remains empty. No commit, approved tag, push, or Phase 16 work was performed.

## Human Decision

PENDING

Review the B -> A contact contract and explicit solver conversion, the permutation regression coverage, and the measured stack/lattice behavior. Human approval remains required before any commit, approved tag, or push.
