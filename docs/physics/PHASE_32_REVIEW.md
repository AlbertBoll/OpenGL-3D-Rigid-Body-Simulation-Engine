# Phase 32 Review

## Status

PHASE 32 STATUS: AWAITING HUMAN REVIEW

## Objective

Contact Solver Timestep Stability, explicitly redefined by the human. Investigate the exact exported 4x4 application stack at 60/120 Hz and 1/2/4/8 solver passes; make the smallest evidenced solver/contact correction needed for robust resting behavior.

Phase 31 is rejected. Its owned implementation was selectively reverted; its failure record remains in `docs/physics/PHASE_31_REVIEW.md`. Fixed scheduling is deferred to Phase 33, after separate human approval of Phase 32.

## Baseline Commit

7ba19255a571818a2f8f1b9deb6ddd2883e7a2e6 on `physics/refactor`.

## Previous Approved Tag

`physics-phase-30-approved`, equal to HEAD. The generic preflight helper failed because it assumes `physics-phase-31-approved`. The current human instruction explicitly overrides that prerequisite. The approved Phase 30 tag was verified independently; no tag was created, moved or deleted.

## Audit Findings Addressed

Current follow-up to PHYS-BUG-005 (resting-contact instability), including the current corrections to PHYS-BUG-007/008. The historical audit is unchanged. PHYS-BUG-014 scheduling is deferred to Phase 33.

## Allowed Scope

Three production files at the contact/manifold boundary, two test files and two documents: seven Phase 32 files. The existing one-pass default passes the acceptance gates, so no solver policy file is changed. Phase 31 reversion and its rejected review are a separate explicitly authorized operation.

## Files Changed

- GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
- GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h
- GEngine/include/GEngine/Physics/Manifold.cpp
- PhysicsTests/src/main.cpp
- PhysicsTests/src/Phase32ExactBoxWorld.h
- docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
- docs/physics/PHASE_32_REVIEW.md

## Implementation Summary

Two related defects were isolated.

1. The old contact solve ran three unconstrained Gauss-Seidel iterations, then clamped the accumulated normal and tangential impulses. Clamping changed the coupled contact response after the equations were solved. Repeated calls could converge to a supporting normal impulse while the contact was separating, with positive kinetic-energy change. The corrected single-contact path projects the accumulated normal during each local iteration, then solves the two-dimensional friction disk with its actual tangent effective-mass matrix. It applies only the accumulated impulse difference. Normal support and friction are recalculated together over the existing three local iterations.
2. Solving the face points separately left a rocking mode in the four-point support patch. Even the corrected single-contact path developed a late 60 Hz burst: peak linear/angular motion 0.206567/0.041529 in the 15-20 s window at eight passes. A manifold now solves its normal impulses as a coupled block before solving friction. At most four rows require at most sixteen active sets. Partial pivoting handles dependent coplanar rows without inverting a singular four-point matrix; free columns retain warm values. A candidate must satisfy nonnegativity and normal complementarity. Among feasible candidates, choose the one nearest the previous normal impulses. Single contacts or failed blocks use the corrected single-contact path.

The block equation is `v_n = K lambda_n - b`, where `K = J_n M^-1 J_n^T` and `b = K lambda_old - v_current`. All other currently applied impulses remain in `v_current`. Require `lambda_n >= 0`, `v_n >= 0` and complementary active rows. The rank threshold is relative to matrix scale (`1e-9`); the residual tolerance is `1e-6 * max(1, |b|)` in velocity units. These are finite-precision validation tolerances; no velocity bias or damping term is added.

With normal support fixed, friction minimizes `0.5 x^T K_t x - b_t^T x` on `|x| <= mu lambda_n`. An unconstrained interior solution uses the 2x2 inverse; a sliding solution uses `(K_t + alpha I)x = b_t` with a bounded 32-step scalar bisection. A radial clamp of `K_t^-1 b_t` is insufficient when tangent effective mass is anisotropic. Finite guards and the final Coulomb bound remain. Static bodies use their physical velocity getters.

Accumulated impulse projection and warm starting follow the sequential-impulse formulation described by [Box2D Solver2D](https://box2d.org/posts/2024/02/solver2d/). The coupled contact counterexample, patch solve and measurements here are repository-specific evidence.

## Tests Added / Modified

`--contact-convergence`: 188 checks. Tests off-center sliding at both rates, three inverse masses, reversed body order and an oblique contact with anisotropic tangent mass. Checks normal complementarity, the independently derived impulse, maximum-dissipation direction, Coulomb bound and kinetic energy. Four-point coplanar patch tests cover static/dynamic support, reversed order, a rotated frame, cold/warm support and retraction; one pass must balance force and torque. Nonzero stored static velocities must be ignored.

Red evidence: the initial coupled-contact regression failed 72/108 checks on Phase 30. After correcting single contacts, the new patch regression still failed 32/188 checks. The final implementation passes 188/188.

`--timestep-stability`: 50 checks using the exact exported world at the unchanged default one pass, both rates and both measurement windows. Checks finite state, energy, motion, penetration, excursion and minimum average height. Both additions also run in the full suite. The second test file embeds all exported numbers and body creation order.

Original export: `bin-int/phase31-regression/fixed60-investigation/p31-60-boxes-1-world.txt`.
SHA256: `2b7efcbeb4a3ea3eba9c77da53baa1ed698f5aa285f1b604737088f72465f2b4`.
There are 16 dynamic size-two boxes, five static boundaries, all original 36-point meshes, original poses/materials/masks/velocities, and gravity `(0,-12,0)`. This is the actual application export, not a reconstructed benchmark stack.

## Validation Commands

MSBuild:
`C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`

`PhysicsTests/PhysicsTests.vcxproj /p:Configuration=Debug /p:Platform=x64 /m` and the same command with `Release`. Run `bin/<configuration>/PhysicsTests/PhysicsTests.exe --contact-convergence`, `--timestep-stability` and the executable with no arguments for the full suite.

Local investigation artifacts and commands are under `bin-int/phase32-stability/`:
- `SolverReplay.vcxproj` links the independent approved engine; `production/SolverReplay.vcxproj` links the actual final production engine.
- `SolverReplay.exe <exact-export> fixed <1/60 or 1/120> <output-prefix> <1/2/4/8>` runs 20 simulated seconds and exports every tick/body.
- `python bin-int/phase32-stability/finalrun.py observe` captures residual stages and cache/block telemetry.
- `python bin-int/phase32-stability/finalrun.py cost` runs three sequential cost samples for every revision/rate/count, alternating revision order.
- `PhysicsBenchmark.exe --body-counts=50,100,200,500,1000,2000 --warmup=2 --samples=5` on both Release engines.
- `git diff --check`, a Phase 32 production/test scoped check, `git status --short` and the final ownership hash audit.

## Debug Result

Build passed. Contact convergence 188/188; exact-world stability 50/50; full PhysicsTests 17,673/17,673. Exact-world physical metrics match Release.

## Release Result

Build passed. Contact convergence 188/188; exact-world stability 50/50; full PhysicsTests 17,673/17,673. The previous existing resting-friction checks also passed 192/192, and are included in the full suite.
This is the nominal optimized Release configuration with the existing `/MTd` Debug static CRT override. It is not a fully representative true Release CRT allocator/runtime result. Existing build warnings were not changed.

## Stability Results

**Measured.** Fixed creation order, geometry, gravity and materials; no random inputs; 20 simulated seconds, 1,200 or 2,400 ticks. Windows use tick index divided by the requested rate, avoiding endpoint errors from float accumulation. The primary 5-18 s window has 781/1,561 samples; the 15-20 s window has 301/601.

Acceptance gates were documented before the matrix and remain unchanged: no collapse/nonfinite state; peak post-settling linear speed <= 0.05 units/s, angular speed <= 0.02 rad/s, live-anchor penetration <= 0.035 units, per-body excursion <= 0.05 units; average body Y >= 4.45; peak total energy <= 1.005 times initial. The permanent test also checks the minimum average Y throughout each window. All eight candidate rate/count combinations pass; default one pass is retained.

Excursion is the largest body's length of componentwise position ranges within the window. Movement is the largest consecutive-tick center displacement in that window. Penetration is recomputed from live manifold anchors, matching the earlier regression investigation. Values are units, units/s and rad/s as appropriate.

| Revision | Hz | Passes | Peak linear | Peak angular | Max penetration | Excursion | Max movement/tick | Avg Y at 18 s | Manifolds | Contacts |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| Phase 30 | 60 | 1 | 12.7547 | 7.02295 | 0.591525 | 15.1694 | 0.212579 | 3.63292 | 15-23 | 56-86 |
| Phase 32 | 60 | 1 | 2.72497e-06 | 1.21586e-06 | 0.0190289 | 1.89937e-06 | 3.52973e-08 | 4.464 | 16-16 | 64-64 |
| Phase 30 | 60 | 2 | 0.120567 | 0.0968233 | 0.0234804 | 0.0502594 | 0.00200943 | 4.47799 | 17-20 | 64-74 |
| Phase 32 | 60 | 2 | 4.99814e-06 | 1.20987e-06 | 0.0174913 | 4.48458e-06 | 8.33016e-08 | 4.46997 | 16-16 | 64-64 |
| Phase 30 | 60 | 4 | 10.2713 | 5.99728 | 1.24127 | 8.10992 | 0.799979 | 4.10921 | 16-28 | 62-102 |
| Phase 32 | 60 | 4 | 7.84124e-07 | 7.17264e-07 | 0.00188684 | 3.24032e-07 | 1.05497e-08 | 4.49678 | 16-16 | 64-64 |
| Phase 30 | 60 | 8 | 0.108901 | 0.0415963 | 0.0207197 | 0.0656418 | 0.00181499 | 4.47867 | 20-23 | 69-77 |
| Phase 32 | 60 | 8 | 7.49875e-07 | 6.66692e-07 | 0.00107145 | 4.12229e-07 | 1.24825e-08 | 4.49817 | 16-16 | 64-64 |
| Phase 30 | 120 | 1 | 0.33043 | 0.0970196 | 0.0261434 | 0.219863 | 0.0027536 | 4.47308 | 17-23 | 63-81 |
| Phase 32 | 120 | 1 | 2.03366e-06 | 6.88324e-07 | 0.0171272 | 1.17294e-06 | 1.63448e-08 | 4.46967 | 16-16 | 64-64 |
| Phase 30 | 120 | 2 | 0.125003 | 0.0509178 | 0.0212788 | 0.0256169 | 0.00104208 | 4.48427 | 20-20 | 68-70 |
| Phase 32 | 120 | 2 | 8.665e-07 | 7.20016e-07 | 0.00216627 | 1.00077e-06 | 4.92557e-09 | 4.49638 | 16-16 | 64-64 |
| Phase 30 | 120 | 4 | 0.000136174 | 2.87998e-05 | 0.0195364 | 1.27184e-05 | 1.1249e-06 | 4.48902 | 18-18 | 66-66 |
| Phase 32 | 120 | 4 | 4.97073e-06 | 1.38285e-06 | 0.0198221 | 3.48356e-06 | 4.14226e-08 | 4.46578 | 16-16 | 64-64 |
| Phase 30 | 120 | 8 | 0.00592178 | 0.00112989 | 0.0194484 | 0.000556341 | 4.93508e-05 | 4.48748 | 16-16 | 64-64 |
| Phase 32 | 120 | 8 | 7.97648e-07 | 4.52397e-07 | 0.0199795 | 2.16482e-07 | 4.82191e-09 | 4.4657 | 16-16 | 64-64 |


At the retained default one pass, the late window confirms continued resting behavior:

| Revision | Hz | Peak linear, 15-20 s | Peak angular | Penetration | Excursion | Final linear | Final angular | Avg Y at 20 s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Phase 30 | 60 | 11.8711 | 7.17282 | 0.230962 | 10.9124 | 1.41913 | 0.732991 | 2.73317 |
| Phase 32 | 60 | 2.0124e-06 | 7.06905e-07 | 0.0190289 | 7.81607e-07 | 3.01891e-07 | 8.93129e-08 | 4.464 |
| Phase 30 | 120 | 0.271288 | 0.0557209 | 0.0218024 | 0.0494135 | 0.271288 | 0.0538824 | 4.47297 |
| Phase 32 | 120 | 4.78537e-07 | 9.10824e-08 | 0.0171272 | 8.76463e-08 | 1.73656e-07 | 3.97734e-08 | 4.46967 |


Initial mechanical energy is 864. The candidate never exceeds that initial energy at any tested rate/count. No damping or sleeping is used. At rest, every candidate cell retains 16 manifolds and 64 contacts. All 48 cost runs completed successfully. Each of the sixteen revision/rate/pass-count cells has three identical body traces, and each matches its separate observer trace. Nine significant digits round-trip the recorded float body state. Every candidate repeat passes both physical windows.

Including initial settling (0-20 s), default candidate peak linear speeds are 0.750000 at 60 Hz and 0.486165 at 120 Hz; angular peaks remain 1.21586e-6 and 6.88324e-7. Maximum live penetration over the entire run is 0.0216671 and 0.0207958. Both runs execute exactly 20 nominal simulated seconds; float accumulation reports 20.0000010431 seconds. The headless replay discards or skips no time.

## Contact Convergence and Persistence

**Measured**, using an observer separate from cost runs. Capture before warm start, after warm start and after every configured global pass. The observer baseline body traces match the independent Phase 30 executable at all eight rate/count combinations.

The normal residual is the scalar projected normal-impulse correction multiplied by normal inverse effective mass, in velocity units. The tangential residual is a projected-gradient step onto the Coulomb disk, scaled back to velocity units with the tangent-matrix trace. Zero residual means the corresponding projected contact fixed point; merely being inside the disk does not guarantee convergence.

At one pass, median normal RMS residual after solving falls from 0.109142 to 9.20225e-8 at 60 Hz, and from 0.00713873 to 3.52912e-8 at 120 Hz. Median tangent RMS residual falls from 0.0728791 to 4.37180e-8, and from 0.00597122 to 1.50409e-8 respectively.

At 60 Hz the candidate's median normal RMS residual is 0.1 before warm start, 1.44123e-7 after warm start and 9.20225e-8 after its single pass. At 120 Hz: 0.05, 5.63269e-8, 3.52912e-8. Warm-start normal-impulse sums are approximately 8 and 4, consistent with the halved per-tick gravity load. No timestep-ratio scaling is needed for these constant-dt replays.

Over 5-18 s, actual refreshed-point reuse is 78.50%/94.24% on Phase 30 at 60/120 Hz, and 100% at both rates on the candidate. Candidate slot removals are zero. Default-policy block attempts/successes are 12,496/12,496 and 24,976/24,976. The maximum post-solve Coulomb excess is at float roundoff. Per-stage results for all counts are in `convergence-summary.json` and the residual CSV files.

## Bias, Warm Starting and Ablations

The current approved solver already has no `1/dt` physical-velocity bias. Position correction remains a once-per-tick, translation-only pass with 0.02 slop, 0.25 fraction and 0.2 cap. Its rate per simulated second depends on dt, but it was not the demonstrated velocity-energy defect. These equations are unchanged.

Warm starting remains once per tick. Cached impulses remain accumulated and retractable, and are projected to the current material disk before application. Manifold matching, refresh order, normal coherence and expiry thresholds remain unchanged. The improved cache retention follows from removing the solver-generated rocking.

Controlled cold-cache and no-position-correction runs on the approved and first projected candidates failed stability: removing warm start caused collapse or drift; removing correction could become quiet only after collapse with large penetration. Neither was retained. An exact tangent solve alone passed 5-18 s at 60 Hz/eight passes but failed 15-20 s, so it was not treated as sufficient. The final manifold block solves the patch's coupled normal force/torque balance.

An isolated small-angle integration ablation changed trajectories but did not pass both rates. It was not retained. Exploration sources and outputs are preserved under `exploration-source/` and the `ablation-*` files.

## Benchmark Results

**Measured.** Final Release cost runs use the actual production library and an independent approved Phase 30 library. No other simulation or build ran concurrently. Each cell has three fresh-process runs, alternating revision order. Warm up five simulated seconds, then measure 900 ticks at 60 Hz or 1,800 at 120 Hz. Each value is the median of three per-run medians; parentheses give their minimum-maximum. Whole tick is the physics-world profile scope, excluding export/file I/O. Expensive convergence observation is excluded.

| Hz | Passes | Phase 30 solver ms | Phase 32 solver ms | Phase 30 whole tick ms | Phase 32 whole tick ms |
|---:|---:|---:|---:|---:|---:|
| 60 | 1 | 1.8862 (1.8660-1.9959) | 1.7492 (1.6775-1.7953) | 3.0678 (3.0472-3.2184) | 3.3012 (3.1573-3.3719) |
| 60 | 2 | 3.5521 (3.5169-3.5587) | 3.2070 (3.0808-3.2287) | 4.8064 (4.7598-4.8289) | 4.7687 (4.5820-4.8056) |
| 60 | 4 | 9.7129 (9.3455-9.7913) | 6.0572 (5.9062-6.1945) | 11.3666 (10.9551-11.4499) | 7.5109 (7.3366-7.6829) |
| 60 | 8 | 14.3648 (14.0487-14.5864) | 12.0694 (11.9616-12.1430) | 16.2319 (15.8512-16.4050) | 13.6696 (13.5424-13.7382) |
| 120 | 1 | 1.9108 (1.8754-1.9259) | 1.7378 (1.7189-1.7597) | 3.5703 (3.5234-3.5739) | 3.3045 (3.2662-3.3384) |
| 120 | 2 | 3.6709 (3.6141-3.6944) | 3.2069 (3.1452-3.3140) | 5.3373 (5.2597-5.3824) | 4.7659 (4.6828-4.9163) |
| 120 | 4 | 6.4526 (6.3604-6.6518) | 6.1296 (6.0438-6.1619) | 8.1984 (8.0755-8.4449) | 7.7100 (7.6150-7.7477) |
| 120 | 8 | 12.5727 (12.2941-13.0009) | 12.1495 (11.8425-12.1893) | 14.0246 (13.7080-14.4565) | 13.7790 (13.4421-13.8284) |


At the retained one-pass default, 60 Hz solver cost improves from 1.88625 to 1.74925 ms, while whole-tick cost rises from 3.0678 to 3.3012 ms. At 120 Hz these are 1.91075 to 1.7378 ms and 3.5703 to 3.3045 ms. The workloads share the exact initial world and configuration; their later physical trajectories and collision work differ. These are not isolated instruction-cost speedups or application FPS measurements.

The unchanged paired-overlapping-box benchmark also passed, with two whole-workload warmups and five measured samples per body count, default 1/120 step and one pass. Final state fingerprints match across revisions at all six counts. Both executables validate repeat determinism, finite state and counters.

| Bodies | Phase 30 whole tick median ms | Phase 32 whole tick median ms | Phase 30 solver mean ms | Phase 32 solver mean ms |
|---:|---:|---:|---:|---:|
| 50 | 2.0840 | 2.1364 | 0.5140 | 0.6006 |
| 100 | 4.1523 | 4.3139 | 1.0313 | 1.2259 |
| 200 | 8.3707 | 9.0161 | 2.0074 | 2.4716 |
| 500 | 21.0994 | 22.7756 | 5.0784 | 6.5046 |
| 1000 | 45.7925 | 47.5434 | 10.8429 | 12.9682 |
| 2000 | 96.0034 | 97.7506 | 21.5630 | 25.5201 |


This benchmark has one contact per manifold, so it exercises the corrected single-contact fallback. At 2,000 bodies its mean solver cost rises 18.4% (21.5630 to 25.5201 ms), and whole-tick median rises 1.8% (96.0034 to 97.7506 ms). Whole-tick ranges overlap: 93.0466-96.2672 ms before, 94.9650-102.8592 ms after. This measured cost regression is retained and reported; no unrelated optimization was added.

Detailed evidence under `bin-int/phase32-stability/`: `final-cost-matrix.json`, `final-comparison-summary.json`, `final-observation-matrix.json`, `convergence-summary.json`, `benchmark-summary.json` and per-run body/frame/residual files.

## Behavior Changes

Resting contact response changes intentionally to enforce normal complementarity and friction coupling, and to balance a manifold's normal support as a block. One global iteration now includes that block and its friction traversal. Global pass count, warm-start count, integration count and scheduling are unchanged. Scheduling work belongs to Phase 33.

## Known Limitations

These results establish acceptance for the exact exported world, the two requested fixed rates and four requested pass counts over 20 seconds. They do not prove stability for arbitrary geometry, friction, mass ratios, durations or variable timesteps. Different rates produce slightly different settled compression within the existing slop. Live-anchor penetration describes detected contacts; it is not a general independent all-pairs penetration oracle. The single-contact benchmark has a measured solver cost regression described above. A failed block safely falls back to the corrected single-contact solve, whose finite iteration budget is not a universal convergence guarantee.

## Out-of-Scope Findings

- Existing Release `/MTd` override.
- Existing `RigidBody3D::Update` in `PhysicsBody.cpp` skips orientation increments smaller than `1e-5` radians. This creates a timestep-dependent angular integration threshold; the diagnostic ablation did not solve both-rate acceptance. Production integration remains unchanged.
- The restored Phase 30 application retains its old frame-dependent scheduling until Phase 33.
- Pre-existing `.gitignore:33` blank line at EOF remains user-owned and unchanged.
- No audit history, rendering behavior, application non-timing edits, sleeping policy or damping equations were modified.

## git diff --check

Phase 32 production/test scoped check: passed, no output. New fixture/review whitespace checked as well.

Literal whole-worktree result (exit 2):
```text
.gitignore:33: new blank line at EOF.
```
This is the unchanged user-owned entry condition, preserved as instructed.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp
 M GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h
 M GEngine/include/GEngine/Physics/Manifold.cpp
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
?? PhysicsTests/src/Phase32ExactBoxWorld.h
?? docs/audit/
?? docs/physics/PHASE_31_REVIEW.md
?? docs/physics/PHASE_32_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/rendering/
```

## Ownership and Phase 31 Reversion

`final-ownership-verification.json` audits all 1,570 entry paths. Unrelated hashes match. The application's non-timing prefix and complete remaining user-only diff match their snapshots. Removing only the new plan amendment reproduces its original byte hash, including original LF formatting. The four timing production files equal Phase 30. PhysicsTests was first restored to Phase 30, then received only Phase 32 regressions.

Phase 31's rejected record remains in `docs/physics/PHASE_31_REVIEW.md`. The staged index is empty. HEAD and the Phase 30 approved tag remain `7ba19255a571818a2f8f1b9deb6ddd2883e7a2e6`. No commit, approved tag, push, reset or next-phase start was performed.

## Human Decision

PENDING
