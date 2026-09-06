# Phase 23 Review

## Status

PHASE 23 STATUS: AWAITING HUMAN REVIEW

The requested stack investigation, scoped correction, and final validation are complete. Debug/nominal Release each pass 223 focused and 2,783 full-suite checks; all one/eight-pass stability workloads are finite and match across configurations at printed precision. Benchmark comparisons and Git checks are recorded below. The additional 60-second sphere-lattice investigation supports gradual geometric instability. The box micro-jitter review finds decaying fixed-step motion to a numerical floor and reproduces timestep-driven jitter in both phases; no further production change was made. Human Decision: PENDING.

The submitted Phase 23 snapshot is retained in ignored `bin/phase23-revision-entry.bak/`. No commit, tag, push, or next-phase work.

## Objective

Persist contacts using stable feature/pair identity, compatible geometry, and coherent normals. Preserve useful box-face impulses while discarding invalid constraints.

## Baseline Commit

`57fa05bef2d87b2bfbac72fcd6b2791e67f62f9e` - `physics: phase 22 generate box face manifolds`, on `physics/refactor`.

Preflight verified the preceding approved tag at HEAD and no approved Phase 23 tag. No Phase 23 review existed. The 15 pre-existing modified/untracked user files were hashed before editing.

## Previous Approved Tag

`physics-phase-22-approved`.

## Audit Findings Addressed

The Phase 23 weaknesses from audit sections 10 and 15: either-anchor duplicate suppression, stale geometry/normals, absent feature identity, and unchecked shape/body compatibility. This supports the contact-persistence portion of **PHYS-BUG-005** and **PHYS-API-004**; it does not close every stack, CCD, or ownership finding.

## Allowed Scope

Five production files, one dedicated test file, and this review: seven files total. Class B mathematical and Class C stability validation. No Class D optimization or speedup claim.

`PhysicsSystem.cpp` is required to submit the existing 2-4 box contacts as a complete patch and clear new feature metadata on reused single-witness outputs. `BoxContact.h` assigns keys using existing face geometry. The manifold files implement compatibility/matching; `Contact.h` stores the labelled keys. No build files change.

## Files Changed

- `GEngine/include/GEngine/Physics/Manifold.h`
- `GEngine/include/GEngine/Physics/Manifold.cpp`
- `GEngine/include/GEngine/Physics/Contact.h`
- `GEngine/include/GEngine/Physics/BoxContact.h`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `PhysicsTests/src/main.cpp`
- `docs/physics/PHASE_23_REVIEW.md`

## Implementation Summary

Each generated box point carries a key for each labelled body. High bits encode the existing local face ID plus one; four low bits indicate membership in its ordered boundary edges. Membership uses the existing clipping tolerance (`max(1e-6, shortest face edge * 1e-5)`) and double intermediates. Keys identify geometric features rather than transient array positions. Zero means unavailable. A/B reversal swaps keys and anchors and reverses the normal. All four single-witness collision entry points clear keys when an output is reused.

Manifolds snapshot both body identities (slot/generation), current shape pointers, and shape revisions. Incompatible caches are discarded before insertion, patch refresh, expiry, or warm starting. Old shape pointers are compared but never dereferenced. World destruction callbacks remain the lifetime authority. Unused contact slots are value-initialized for safe partial-patch copies.

Incremental matching requires **both** COM-relative local anchor distances below 0.02, preserving the existing distance policy. These distances are rigid-transform invariant. The nearest maximum-of-two squared distance wins; existing order breaks exact ties. Distinct available feature pairs remain distinct even below 0.02. Matches refresh geometry; featured/unfeatured transitions start cold. The existing bounded spread reduction remains for unmatched single-witness input.

A complete 2-4 point box patch matches against prior state in a separate bounded manifold. Exact labelled keys receive priority across the entire patch. Remaining points may match only the same labelled face pair with both anchors below the unchanged 0.02 distance bound. Each pass selects the nearest eligible pair and consumes each old impulse once. Compatible stamps and coherent normals are required on both passes. Boundary-bit changes alone no longer force nearby points on the same faces to start cold. Changed faces, excessive drift, missing metadata, incompatible stamps, or incoherent normals still prevent reuse.

Matched constraints retain their previous solve order; new points append in deterministic generated order. All retained geometry and impulse bases are refreshed. Missing points retire. Clipping, face classification, maximum-area reduction, four-point capacity, and all solver policies are unchanged.

Normal coherence requires dot >= `0.9961947` (cosine of 5 degrees), both for old/new query axes and for the axis transported independently by A and B. One B-local normal per contact detects relative rotation while allowing common rigid rotation. Incoherent constraints are removed. Expiry refreshes world anchors, normal, and signed separation.

For coherent refresh, reconstruct the prior A-local impulse `p = n_old*lambda_n + u_old*lambda_u + v_old*lambda_v`. New coefficients are its dot products with the refreshed orthonormal basis; normal lambda is clamped nonnegative. Nonfinite impulses reset. Existing constraint PreSolve projects into the current Coulomb disk before application. This preserves the physical cached impulse across discontinuous tangent-basis choices. Solver equations, iteration count, damping, restitution, stabilization, and timestep policies are unchanged.

PreSolve validates stamps and normals without repeating separation expiry on newly generated contacts: tiny positive roundoff in a fresh touching witness must preserve the existing collision contract. Empty manifolds from this validation are immediately erased, preventing the body-removal callback from skipping a zero-contact pair that still holds pointers.

## Revision Root Cause and A/B Diagnostics

Two cache-continuity defects in the submitted Phase 23 implementation caused the stack regressions:

1. **Boundary classification was treated as permanent point identity.** Low key bits describe which clipping boundaries contain a point now. Almost coincident faces change these bits under tiny sliding or rotation, although the labelled supporting faces and nearby anchor pairs remain valid. Exact-key-only matching consequently threw away useful support impulses.
2. **Every patch refresh adopted clipping array order.** Clipping cyclically permutes the same geometric points as boundary/reference classification changes. Sequential constraint solving is order-sensitive, particularly at one pass. Reordering matched constraints changed load/friction distribution even when their cached impulses survived.

A trace on the second stack step shows body slots 2/6: old keys `76/51` become `72/50` at anchor distance `0.000109374604` with cached normal lambda `0.0121806264`. Both encoded face labels (`key >> 4`) remain 4/3. The same patch's other three points move only about `0.000068-0.000120` and change boundary bits, with nonzero lambdas `0.01076-0.01841`. Generated order maps to previous slots `[1,2,3,0]`. This directly demonstrates both mechanisms without relying solely on final stack metrics.

Over 1,200 steps, the submitted one/eight-pass traces record 1,891/1,757 nearby candidates with changed keys and 5,701/3,600 successful matches whose generated index differs from their previous slot. These are event counts, not unique bodies or a count of every expired constraint.

**Measured**, temporary A/B variants below use the same Release binary configuration, stack geometry, timestep, materials, gravity, duration, and requested solver passes. No damping, stabilization, restitution, iteration policy, matching radius, or angular threshold was tuned. Only the named persistence decision is changed.

| Variant | 1-pass final Y | 1-pass final linear/angular | 1-pass moving | 8-pass peak/final intrusion | 8-pass final linear/angular | 8-pass manifolds/points |
| --- | --- | --- | --- | --- | --- | --- |
| Submitted | 4.480363 | 0.326429/0.106521 | 9 | 0.012849/0.012109 | 0.007160/0.001507 | 20/72 |
| Anchor-only matching | 4.480516 | 0.275696/0.072116 | 11 | 0.000344/0.000281 | 0.000000/0.000000 | 16/64 |
| Keep matched old geometry | 4.477316 | 0.219448/0.088802 | 11 | 0.010264/0.010188 | 0.021584/0.004094 | 16/64 |
| No warm start | 1.619113 | 0.177040/0.134147 | 2 | 0.025230/0.023981 | 0.065132/0.009944 | 17/65 |
| Copy raw lambda coefficients | 4.478303 | 0.187218/0.038473 | 7 | 0.015965/0.015965 | 0.041974/0.007011 | 16/64 |
| Incremental patch insertion | 3.232840 | 0.058789/0.011401 | 1 | 0.032841/0.028289 | 0.513464/0.129483 | 17/64 |
| Keep prior order only | 4.475922 | 0.429647/0.090832 | 10 | 0.009463/0.009460 | 0.000000/0.000000 | 17/65 |
| Anchor-only + prior order | 4.480556 | 0.181823/0.036917 | 6 | 0.000983/0.000891 | 0.000000/0.000000 | 16/64 |
| Final scoped correction | 4.480385 | 0.087014/0.016677 | 2 | 0.000983/0.000891 | 0.000000/0.000000 | 16/64 |

`No warm start` collapses at one pass (peak intrusion 1.346909); low final moving count after collapse is not settling success. Incremental insertion also loses stack height. Keeping stale geometry or copying raw normal/tangent coefficients does not recover eight-pass intrusion. Anchor continuity alone restores eight-pass support but leaves one-pass residual motion; order preservation alone does not cure unnecessary cold starts. Together they isolate the two defects. The final implementation additionally requires the same labelled faces and reserves all exact matches before nearby fallback, rather than using unrestricted anchor-only matching. It retains the submitted physical impulse transport; the existing independent basis-crossing regression still passes.

**Retained pair explanation:** the submitted final one-pass stack contains 16 vertical support pairs with 64 points plus two side pairs (body slots 10/11 and 14/15), three points each: 18 manifolds / 70 points. At eight passes there are the same 16 support pairs plus four side pairs (11/12, 16/17, 12/13, 15/16) with 1+4+1+2 points: 20 / 72. The revised one-pass result has 16 support pairs plus one side point (8/13): 17 / 65. The revised eight-pass result has exactly the 16 support pairs / 64 points, with near-vertical normals and zero printed residual speed.

These counts reflect changed geometry/motion and current patch membership. The fix does not cap manifolds or discard valid side contacts to improve the report. Compared with Phase 22, complete-patch refresh also retires absent points instead of accumulating old spread-selected witnesses; counts alone do not establish better support. Final height, energy, speeds, intrusion, and pair orientations provide that evidence.

Temporary mode/trace hooks in `Manifold.cpp` and `PhysicsBenchmark/src/main.cpp` were removed before final builds. The benchmark source is byte-identical to its revision-entry backup and has no Git diff. A/B source evidence remains ignored in `bin/phase23-ab-instrumentation-base.cpp.bak` and the submitted backup directory. Logs are `bin/phase23-ab-{mode}-{passes}.log`, `phase23-ab-trace-*` and `phase23-ab-pairs-*`.

## Tests Added / Modified

Added `--manifold-persistence` and registered the same checks in the full suite. The A/B reversal helper now swaps keys.

- Red control against unchanged Phase 22 code: **2/18 passed**, exposing 16 refresh, duplicate, normal, and compatibility failures.
- Nearby refresh retains a measured normal warm-start impulse and uses the refreshed torque lever arm in either input order.
- Sharing only A's or B's anchor retains distinct pairs.
- Changed normal invalidates constraints; four degrees retains the expected projected coefficient, six degrees resets it.
- Small normal change across `GetOrtho`'s `z=0.9` branch preserves an independently reconstructed world impulse.
- Common rigid rotation retains contacts; relative rotation expires COM-anchor contacts without tangential drift.
- Rebuild/replacement, changed body identity at a live address, and null shape invalidate caches. Revision checks cover expiry, direct PreSolve, and insertion boundaries.
- Scales `0.001`, `1`, `1000` with aligned/45-degree patches: distinct keys, A/B ownership, common-transform invariance, and four retained points below the duplicate tolerance.
- Reordered patches preserve distinct preloaded impulses once and retain prior solve slots. Changed face, drift, absent point, invalid batch, and shape rebuild verify selective reset, retirement, and transaction safety.
- Added 68 continuity checks (`--persistence-continuity`), also registered in `--manifold-persistence` and the full suite. Actual clipped box patches slide/rotate through boundary-key changes at offsets 0.0001, 0.001, and 0.005, including reversed body and input order. Four separate seeded impulses remain attached to nearby refreshed geometry in their previous solve slots.
- A 32-refresh solver comparison cyclically permutes a persistent face patch at one/eight passes. The entire linear/angular response sequence must match an unpermuted reference exactly, covering initial cold and subsequent warm starts.
- Revision red control against submitted Phase 23: **42/68 passed**, 26 failures (24 lost/reordered impulse checks and two solver-response comparisons). Corrected behavior passes **68/68** within both final focused/full runs. These checks test cache continuity and observable solver response, not fitted final-stack numbers.
- All four single-witness query entry points clear reused feature metadata.
- Invalidate in PreSolve, remove a body, step again: no empty/stale pair remains.
- Existing geometry, gravity-supported face, solver, friction, restitution, and lifetime checks remain enabled.

Keys, counts, and unchanged cache coefficients compare exactly. Reconstructed impulse tolerance: `2e-6`; projected coefficient/normal: `1e-6`; ordinary vector response: existing `1e-5`. The 5-degree/0.02 policies are explicit, not fitted to stack measurements. Existing eight gravity-supported face fixtures retain energy <=100.1%, intrusion <=0.0201, final-second speeds <=0.05, and 2-4 contact gates. No new expected failures.

## Validation Commands

```powershell
$phase23BuildPath = $env:Path
Remove-Item Env:Path
$env:Path = $phase23BuildPath
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Debug /p:Platform=x64 /clp:ErrorsOnly
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' GEngine.sln /m /nologo '/t:GEngine;PhysicsTests;PhysicsBenchmark' /p:Configuration=Release /p:Platform=x64 /clp:ErrorsOnly
.\bin\Debug\PhysicsTests\PhysicsTests.exe --manifold-persistence
.\bin\Debug\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsTests\PhysicsTests.exe --manifold-persistence
.\bin\Release\PhysicsTests\PhysicsTests.exe
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=1
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=8
.\bin\Debug\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=1
.\bin\Debug\PhysicsBenchmark\PhysicsBenchmark.exe --physics-regression-baseline --solver-iterations=8
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe --body-counts=100,1000,10000 --warmup=2 --samples=5 --steady-state-warmup-steps=4 --steady-state-measured-steps=8
git diff --check
git status --short
```

The performance baseline executable is `bin/phase23-revision-entry.bak/PhysicsBenchmark.exe`; it uses the same benchmark arguments. The final A-B-B-A follow-up runs those two benchmark commands against saved/final/final/saved binaries in that order, with a fresh process for each command.

Process-local Path normalization handles the existing duplicate Path/PATH host problem. Sandbox setup repeatedly failed, so scoped reads/edits/builds/tests used reviewed elevated execution. No build/runtime settings changed.

Ignored evidence: `bin/phase23-initial-user-files.log`, `bin/phase23-before-*`, `bin/phase23-red-*`, `bin/phase23-first-*`, `bin/phase23-green-*`, `bin/phase23-final-*`. Revision evidence is in `bin/phase23-revision-*`, `bin/phase23-revised-*` and `bin/phase23-ab-*`. The original submitted source/binary backup directory ends in `.bak` and is ignored. Builds include normal-verbosity file logs. Every required validation stage checks its native exit code. The deliberately failing red control records its exit 1 separately.

## Debug Result

Final x64 build succeeded, exit 0. Focused **223/223** and full **2,783/2,783** checks pass, exit 0. All prior gravity-supported face, solver, friction, restitution, and lifetime gates remain enabled and pass. No new expected failures. Both extended one/eight-pass stability commands exit 0; every non-timing CSV field matches nominal Release at printed precision.

## Release Result

Final x64 build succeeded, exit 0. Focused **223/223** and full **2,783/2,783** checks pass, exit 0. All prior gravity-supported face, solver, friction, restitution, and lifetime gates remain enabled and pass. No new expected failures. Both final stability commands and both benchmark commands exit 0.

Phase 22 baseline: build succeeded; full tests passed **2,560/2,560**. Submitted Phase 23: **155/155** focused and **2,715/2,715** full. Nominal Release remains optimized with the existing **Debug static CRT /MTd override**; this is not a true Release-CRT allocator/runtime comparison.

## Stability Results

Before/submitted/revised use the unchanged runner: fixed 1/120 s, 1,200 steps / 10 seconds, gravity (0,-12,0), fixed order/geometry/materials, no random input or discarded warmup. Default one-pass and eight-pass configurations run all four scenarios. Moving means speed >0.05 on either channel. Floor intrusion is max(0, 0.5-lowest_world_bound_y), not inter-body depth. Initial stack energy is 864. All final Debug/Release scenarios remain finite, and all non-timing CSV fields match between configurations at printed precision.

**Measured**, nominal Release stack metrics (speeds in engine units/s and rad/s):

| State / passes | Peak energy % | Final energy | Final average Y | Peak linear | Final linear | Peak angular | Final angular | Moving |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Phase 22 / 1 | 100.000000 | 861.021452 | 4.483962 | 0.605674 | 0.271478 | 0.368470 | 0.043431 | 6 |
| Submitted 23 / 1 | 100.000000 | 860.384702 | 4.480363 | 0.805710 | 0.326429 | 0.290454 | 0.106521 | 9 |
| Revised 23 / 1 | 100.000000 | 860.240186 | 4.480385 | 0.577986 | 0.087014 | 0.232504 | 0.016677 | 2 |
| Phase 22 / 8 | 100.000000 | 863.622859 | 4.498036 | 0.067272 | 0.000000 | 0.059410 | 0.000000 | 0 |
| Submitted 23 / 8 | 100.000000 | 861.350178 | 4.486198 | 0.127711 | 0.007160 | 0.064639 | 0.001507 | 0 |
| Revised 23 / 8 | 100.000000 | 863.821763 | 4.499072 | 0.065288 | 0.000000 | 0.058103 | 0.000000 | 0 |

| State / passes | Manifolds | Points | Mean generated contacts | Peak intrusion | Final intrusion |
| --- | --- | --- | --- | --- | --- |
| Phase 22 / 1 | 19 | 68 | 64.111667 | 0.050950 | 0.009650 |
| Submitted 23 / 1 | 18 | 70 | 68.493333 | 0.045123 | 0.023711 |
| Revised 23 / 1 | 17 | 65 | 63.909167 | 0.040058 | 0.018786 |
| Phase 22 / 8 | 16 | 64 | 63.339167 | 0.004167 | 0.004137 |
| Submitted 23 / 8 | 20 | 72 | 66.857500 | 0.012849 | 0.012109 |
| Revised 23 / 8 | 16 | 64 | 63.353333 | 0.000983 | 0.000891 |

The requested eight-pass intrusion regression is corrected: peak/final intrusion are below Phase 22, the stack retains 16/64 support constraints, and final linear/angular speeds print as zero. At one pass, peak/final linear and angular speeds and moving count improve relative to both submitted Phase 23 and Phase 22. Peak intrusion also improves. No stack exceeds initial mechanical energy.

**Remaining one-pass limitation:** final intrusion 0.018786 is lower than submitted 0.023711 but above Phase 22's 0.009650. Final average Y 4.480385 remains below Phase 22's 4.483962, and two bodies still exceed the movement threshold. The unchanged positional solver permits 0.02 slop (`ConstraintPenetration.cpp:205`); this explains why sub-slop compression need not be corrected, but is not a new acceptance tolerance or a claim of complete settling. No solver policy was adjusted.

**Measured**, sphere lattice (180 bodies; sleeping not implemented). Revised results equal submitted Phase 23 for every non-timing CSV field at both pass counts; the revision changes only complete box-patch matching/order.

| State / passes | Peak energy % | Final energy | Final average Y | Peak linear | Final linear | Peak angular | Final angular | Moving |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Phase 22 / 1 | 100.000000 | 4618.230531 | 1.475437 | 14.400019 | 9.149487 | 11.252431 | 6.806019 | 180 |
| Submitted = revised 23 / 1 | 100.000000 | 4298.488235 | 1.490150 | 14.400019 | 6.350901 | 6.702827 | 6.356113 | 180 |
| Phase 22 / 8 | 100.000000 | 4249.059561 | 1.486900 | 14.300018 | 5.695103 | 6.719536 | 5.710816 | 180 |
| Submitted = revised 23 / 8 | 100.000000 | 4159.930548 | 1.492512 | 14.300018 | 5.810773 | 6.439843 | 5.832931 | 180 |

| State / passes | Manifolds | Points | Mean generated contacts | Peak intrusion | Final intrusion |
| --- | --- | --- | --- | --- | --- |
| Phase 22 / 1 | 181 | 700 | 253.837500 | 1.788265 | 1.788265 |
| Submitted = revised 23 / 1 | 187 | 404 | 269.990833 | 0.181713 | 0.108359 |
| Phase 22 / 8 | 185 | 699 | 264.871667 | 0.196673 | 0.039583 |
| Submitted = revised 23 / 8 | 181 | 371 | 280.194167 | 0.192117 | 0.062374 |

At one pass the Phase 23 lattice has lower final speeds and intrusion than Phase 22, with fewer retained constraints. At eight passes, final linear/angular speeds and final intrusion remain worse than Phase 22 as reported in the original handoff, despite lower final energy and peak angular speed. All 180 bodies remain moving. This box-persistence revision does not claim to fix generic-contact lattice settling.

The single sphere is unchanged by the revision at both pass counts: initial/peak energy 120, final energy 17.998483, final Y 1.499874, zero moving bodies, one manifold/point, peak/final floor intrusion 0.000126. Final linear/angular speed is 0.001068/0.001066, versus Phase 22's 0.001041/0.001039; peak linear speed is 14.200018.

The free asymmetric-body probe is also unchanged: initial/final energy 5.211667/5.432790 (+4.242841%), angular momentum +2.635889%, finite throughout. This is an existing integration limitation unaffected by persistence.

All eight existing gravity-supported face fixtures pass unchanged gates in both final test configurations. Debug/Release face summaries match at printed precision. The non-gating diagnostic reports zero known issues.


## Long-Horizon Sphere-Lattice Diagnostic

**Disposition: no production change.** The evidence supports gradual instability of the aligned sphere lattice seeded by small perturbations. No specific new Phase 23 persistence defect is demonstrated. Revised Phase 23 delays drift and collapse relative to approved Phase 22 at both pass counts. This is a conclusion about the observed collapse mechanism, not a claim of perfectly energy-conserving impact response.

### Conditions and diagnostic definitions

The four main comparisons each run **60 simulated seconds / 7,200 steps** at fixed 1/120 s, gravity (0,-12,0), radius 1, mass 1, identity orientations and zero initial velocities. All bodies have friction 0.5 and elasticity 0.5 (existing pair products 0.25). One-pass and eight-pass configurations are compared separately. No randomness, renderer timing, damping, stabilization, restitution, solver policy or contact tolerance is changed.

The exact 6x6x5 demo positions are x,z in {-2,0,2,4,6,8}, y in {10,12,14,16,18}. The floor is 100x1x100 at the origin, top y=0.5. Four existing 100x10x1 walls at x/z=+/-49.5, y=4.5 use the demo rotations. They are distant before collapse but affect late motion. `ShapeBox::Build` canonicalizes corners.

The actual EnTT view order is reproduced with repository headers: walls 4,3,2,1, floor, then spheres in reverse creation order (logical sphere 179 through 0, top layer first). Source: `RigidBodySimulation.cpp:288`, `:503`, `_Scene.cpp:442`. All four main initial manifests are byte-identical, including body slots, geometry, ordering and materials. The older benchmark instead uses floor first and bottom-up spheres without walls. Separate 10-second controls reproduce its previously recorded speeds/counts, with one-pass collapse at 3.766667 s (Phase 22) and 4.875000 s (Phase 23).

Phase 22 is `physics-phase-22-approved` / `57fa05b`; Phase 23 is the current revised, unapproved source. Isolated copies of physics translation units use v143 nominal Release optimization, profiling and the existing `/MTd` override. Observation hooks record eight stage boundaries, actual cache decisions, and individual applied impulses. Numerical jobs may run concurrently; **no wall-time performance comparison is made**. Main-tree production/test/build files were never edited.

These are observation thresholds, not solver tolerances:

- **Noticeable drift:** first maximum horizontal displacement >=0.01 from each sphere's own initial position.
- **Important contact transition:** at least 15 of the original 144 vertical pairs absent from the cache for 12 consecutive steps after the first cached floor contact. The reported time starts that sustained interval. The first such transition is a landing rebound, not itself a collapse trigger.
- **Collapse:** original top-layer mean center height <7.5 (one diameter below ideal resting top height 9.5) with maximum horizontal displacement >=0.5. This excludes the initial vertical drop.
- Resolution is one step, 0.008333 s. First substantial ballistic floor impact is 1.191667 s; first cached floor contact is 1.208333 s.
- Independent sphere energy is sum(0.5|v|? + 0.2|omega|? + 12y), initially 30,240. The old float-rotation-based energy calculation differs by only about 0.00003 in the 10-second controls. Speeds are maxima across spheres; moving retains the >0.05 criterion.

### Main 60-second results

**Measured**, event times and retained constraints:

| Phase / passes | First drift s | First important transition s | Collapse s | 60-s manifolds | 60-s points |
| --- | --- | --- | --- | --- | --- |
| Phase 22 / 1 | 1.841667 | 1.291667 | 3.641667 | 197 | 716 |
| Revised 23 / 1 | 2.941667 | 1.291667 | 4.966667 | 237 | 244 |
| Phase 22 / 8 | 1.841667 | 1.241667 | 3.566667 | 230 | 761 |
| Revised 23 / 8 | 2.608333 | 1.241667 | 4.575000 | 264 | 266 |

**Measured**, final energy and speed (linear units/s; angular rad/s). Peak energy is **30,240 / 100% of initial** in every main and perturbation run.

| Phase / passes | 60-s energy | Peak linear | 60-s linear | Peak angular | 60-s angular | 60-s moving |
| --- | --- | --- | --- | --- | --- | --- |
| Phase 22 / 1 | 3861.820160 | 14.600019 | 4.275403 | 9.257596 | 4.267438 | 178 |
| Revised 23 / 1 | 3244.854978 | 14.500019 | 0.810608 | 6.818031 | 0.936417 | 156 |
| Phase 22 / 8 | 3342.712905 | 14.300018 | 2.401243 | 6.245805 | 2.424283 | 162 |
| Revised 23 / 8 | 3247.222720 | 14.300018 | 0.885788 | 6.418462 | 1.167730 | 150 |

All states remain finite. The final scene is a spread-out layer near the floor, not an intact or sleeping lattice. Phase 23's lower late residual motion is not successful lattice settling.

### Motion, contact changes and impulses

The revised eight-pass run reproduces the quiet interval and subsequent growth:

| Time s | Lateral displacement | Lateral speed | Top mean Y | Vertical supports | Manifolds/points | Energy |
| --- | --- | --- | --- | --- | --- | --- |
| 2.000 | 0.000983 | 0.003945 | 9.426876 | 144 | 480/480 | 11776.754203 |
| 2.250 | 0.002443 | 0.009703 | 9.426942 | 144 | 472/472 | 11776.859665 |
| 2.500 | 0.006577 | 0.025391 | 9.426938 | 144 | 453/453 | 11776.862261 |
| 2.750 | 0.017480 | 0.067090 | 9.426892 | 144 | 422/422 | 11776.859978 |
| 3.000 | 0.046256 | 0.176854 | 9.426571 | 144 | 382/382 | 11776.842539 |
| 3.250 | 0.121723 | 0.461396 | 9.424366 | 144 | 356/356 | 11776.737843 |
| 3.500 | 0.313396 | 1.137970 | 9.409427 | 144 | 310/310 | 11776.152383 |
| 3.750 | 0.731488 | 2.185744 | 9.320470 | 144 | 280/280 | 11773.281239 |
| 4.000 | 1.415349 | 3.203003 | 9.002605 | 137 | 232/257 | 11704.475190 |
| 4.575 | 3.530587 | 5.470294 | 7.499019 | 67 | 208/251 | 10462.079256 |

All 144 original vertical pairs persist through 3.858333 s. The first lost vertical pair is at **3.866667 s**, after displacement reaches **1.014370** and max speed **3.413382**. Sustained loss of at least 15 supports starts at **4.075000 s**; collapse follows at **4.575000 s**.

At 2 s, 480 points comprise 144 vertical neighbors, 300 horizontal neighbors and 36 floor contacts. At 3.75 s only 280 points remain, while all 144 vertical supports survive. The early count reduction mainly follows lateral neighbors separating; it is not an abrupt removal of the vertical support network that initiates motion.

**Derived**, a log-displacement fit over 2.25-3.25 s gives growth rate 3.904154 /s, doubling time 0.177541 s and R?=0.999989. This describes the measured trajectory and does not set any solver constant or test expectation.

From 2.2 through 3.75 s, energy falls by **3.577991**. The largest positive single-step change is **0.0001124**, about 9.5e-9 of the roughly 11,777 energy level. Maximum individual impulses are warm start **0.506794**, iterative solver **0.008221**, ballistic **0.015771**. Five spheres need about 5*12*(1/120)=0.5 support impulse per step, consistent with the warm-start scale.

At the drift crossing (2.608333 s), warm start offsets gravity with max body delta-v 0.100108; the following solver correction is only 0.000560. Query/insertion changes neither velocity nor energy in that step. No sudden velocity/impulse spike or mass cache reset appears at this onset. Motion grows to displacement 0.731488 by 3.75 s with all vertical supports still present.

Large impulses occur after substantial rolling and contact rearrangement. During the last 0.25 s before the eight-pass Phase 23 collapse threshold, maxima are warm **8.059993**, iterative **4.245271**, ballistic **7.855823**; energy falls **692.651964**, and max speed reaches **9.165111**. Phase 22's corresponding interval also contains large impulses (warm **9.045456**, iterative **4.142608**, ballistic **7.587674**) and loses **693.606954** energy. These events follow developed motion in the trace; they do not initiate the quiet-state departure.

**Landing limitation:** energy is not monotonic over the full run. The largest positive pre-collapse step is +1939.776649 / +1917.347187 at 1.258333 s for Phase 22/23 with one pass, and +699.855893 / +693.990336 at 1.225000 s with eight passes. Stage traces show a shared warm-start/finite-pass rebound overshoot, followed by integration and smaller positional-potential correction. At 1.225 s in revised eight-pass, max speed goes 1.703699 -> 9.286177 after warm start -> 4.165540 after solving. This transient exists in Phase 22 and decays before the later quiet interval. It is not a newly demonstrated Phase 23 collapse trigger, but remains a numerical impact-response limitation. Staying below initial energy alone would not establish physical correctness.

The unperturbed Phase 23/eight-pass repeat matches **all 1,200 per-step rows and 9,600 stage rows exactly** through 10 s, including motion, energy, counts and impulse metrics.

### Cache telemetry

**Measured**, event totals over 60 seconds:

| Phase / passes | Reuse | Nonzero reuse | Cold | Removed |
| --- | --- | --- | --- | --- |
| Phase 22 / 1 | 717719 | 580543 | 812539 | 386763 |
| Revised 23 / 1 | 1248851 | 1113524 | 238896 | 237487 |
| Phase 22 / 8 | 987048 | 825820 | 642209 | 331195 |
| Revised 23 / 8 | 1342118 | 1147615 | 262275 | 250931 |

Reuse counts accepted existing-cache matches, including zero lambda; nonzero reuse excludes all-zero prior lambda. Phase 22 keeps old geometry on a duplicate, while Phase 23 refreshes it. Cold includes new points and overwritten slots, not exclusively lost warm starts. Removed counts expiry/normal-guard removals; overwritten slots instead count as cold. These are events, not unique contacts or peak constraint counts.

Most removals are expiry (separation, anchor drift or Phase 23 relative-normal coherence; reasons are not split further). Revised one-pass has no query/PreSolve guard removals. Revised eight-pass has one query-normal removal at 28.216667 s, long after collapse, and none in PreSolve. Bodies/shapes are never replaced. Sphere contacts have unavailable feature keys; the revised box patch feature/order path is not used by sphere/sphere or sphere/floor pairs.

### Deterministic perturbation results

Only logical sphere 179 at (8,18,8) receives an outward positive-x offset. It creates no horizontal overlap and leaves initial potential energy unchanged. Drift is measured from the perturbed initial position. Each nonzero-offset run continues through collapse to 10 s.

| Passes | Offset | Drift s | Collapse s | 10-s energy | 10-s linear | 10-s angular | 10-s manifolds/points |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 0 | 2.941667 | 4.966667 | 4227.505572 | 5.930935 | 5.927761 | 185/378 |
| 1 | 0.001 | 2.158333 | 4.941667 | 4214.189219 | 6.080073 | 6.070618 | 180/394 |
| 1 | 0.01 | 1.325000 | 4.891667 | 4224.853341 | 6.507589 | 6.512286 | 184/400 |
| 8 | 0 | 2.608333 | 4.575000 | 4148.144509 | 6.358231 | 6.417938 | 182/380 |
| 8 | 1e-05 | 2.616667 | 4.583333 | 4157.554757 | 6.399096 | 6.503937 | 185/375 |
| 8 | 0.0001 | 2.616667 | 4.575000 | 4154.518996 | 5.909869 | 5.929772 | 184/407 |
| 8 | 0.001 | 2.108333 | 4.558333 | 4107.671346 | 6.021814 | 6.097655 | 185/364 |
| 8 | 0.003 | 1.666667 | 4.508333 | 4101.691823 | 6.248426 | 6.270647 | 187/391 |
| 8 | 0.01 | 1.350000 | 4.441667 | 4137.673396 | 5.849639 | 5.884268 | 185/365 |

Offsets 1e-5 and 1e-4 do **not** consistently advance whole-lattice collapse relative to zero; differences are at most one timestep. Offsets 0.001, 0.003 and 0.01 at eight passes advance drift and collapse in order. One-pass 0.001 and 0.01 also advance both relative to zero. Larger resolved perturbations therefore support geometric sensitivity, but the full tested range does not establish a strictly monotonic collapse law. Every tested amplitude is reported.

**Derived physical interpretation:** for a sphere balanced above another equal sphere, h(x)=sqrt((2r)?-x?)?2r-x?/(4r). The aligned state is a local potential-energy maximum for an allowed outward rolling mode. Unbraced outer columns in this aligned lattice permit such motion. Ordinary point-contact friction does not make this arrangement equivalent to a flat box stack. Numerical/impact perturbations seed the mode; the solver trajectory affects its timing.

**Conclusion:** this is consistent with gradual aligned-lattice instability. Revised Phase 23 delays collapse and reduces 60-second residual speed; no specific new persistence defect is demonstrated, so **no production fix is proposed or applied**. Exact times depend on fixture, build, order and timestep. The live demo still uses variable frame-derived half-steps; simulated time must not be equated with wall-clock observation.

### Validation, commands and cleanup

Both isolated Release builds, four 60-second comparisons, two benchmark-order controls, seven nonzero perturbation runs, and the deterministic repeat exit 0. The four main manifests match byte-for-byte. Production/test hashes remain identical to diagnostic entry. Earlier formal Debug/Release 223-focused / 2,783-full results above remain applicable; this investigation adds no production/test implementation.

Commands used before removing the isolated diagnostic builds:

~~~powershell
# VERSION = 22 or 23; PASSES = 1 or 8.
.\bin\phase23-long.bak\pVERSION\out\LatticeVERSION.exe PASSES 0 scene 60 bin/phase23-long-pVERSION-scene-PASSES
# Offsets: 0.00001, 0.0001, 0.001, 0.003, 0.01 at 8 passes;
# 0.001 and 0.01 also at 1 pass.
.\bin\phase23-long.bak\p23\out\Lattice23.exe 8 OFFSET scene 10 bin/phase23-long-p23-offset-LABEL-8
# Benchmark-order controls and deterministic repeat:
.\bin\phase23-long.bak\pVERSION\out\LatticeVERSION.exe 1 0 benchmark 10 bin/phase23-long-pVERSION-benchmark-1
.\bin\phase23-long.bak\p23\out\Lattice23.exe 8 0 scene 10 bin/phase23-long-p23-scene-8-repeat
~~~

Evidence is in `bin/phase23-long-*-{initial,steps,stages,summary}.log` and `bin/phase23-long-build-{22,23}.log`. Step logs record every motion/energy/count metric plus cache/impulse counters; stage logs record energy, kinetic energy, speeds, velocity changes and max cached lambda at eight observation points. An inactive source/build recipe is retained in `bin/phase23-long-diagnostic-source.bak`. Temporary instrumented sources/executables were removed; no production instrumentation remains.


## Box-Stack Micro-Jitter Diagnostic

**Measured conclusion:** the fixed-step demo box stack has a decaying visible transient followed by a persistent, extremely small numerical residual. Revised Phase 23 is not exactly motionless, but no sustained visible-amplitude limit cycle or new load-bearing cache-reset trigger is demonstrated. A deterministic change in frame timestep repeatedly excites visible motion in both Phase 22 and Phase 23. No production correction is justified by these measurements; this review update makes no production or test changes.

The fixture is the demo's 4x4 array of mass-one boxes with half extents (1,1,1), positions `(2.01*x, 1.5+2*y, 0)`, identity orientations, zero initial velocity, and friction/elasticity 0.5 on every body. Gravity is (0,-12,0). The floor and four distant walls match the lattice diagnostic above. EnTT view iteration creates physics bodies in reverse insertion order: walls, floor, then boxes 15 through 0. This reproduces the box demo order, which differs from the floor-first, bottom-up benchmark order in the earlier ten-second stability table.

Both approved Phase 22 and revised Phase 23 run 7,200 steps at float `1/120`, separately at the existing one- and eight-pass settings. The production default remains one pass. No material, damping, stabilization, restitution, friction, contact tolerance, or solver policy is changed. These are isolated nominal Release builds with the existing /MTd caveat; timing is not a performance claim.

RMS is `sqrt(mean(|velocity|^2))` across all 16 dynamic bodies and every end-of-step sample in the named window; maxima cover every body/sample. Position peak-to-peak is the largest Cartesian component range of any body. Orientation peak-to-peak uses the largest component range of the shortest quaternion-relative rotation vector, referenced to that body's first window sample, in radians. These are component excursions, not a Euclidean diameter. Windows are labelled by nominal simulation seconds; the final 40-60 s window contains 2,400 samples per body.

| Phase / passes | Window s | Linear RMS / max | Angular RMS / max | Position p-p | Orientation p-p rad |
| --- | --- | --- | --- | --- | --- |
| 22 / 1 | 10-20 | 0.012646 / 0.118498 | 0.00313451 / 0.0229207 | 0.0210116 | 0.00405091 |
| 22 / 1 | 20-40 | 0.00228098 / 0.0169171 | 0.000555086 / 0.00333896 | 0.00305212 | 0.000600978 |
| 22 / 1 | 40-60 | 0.000297412 / 0.00218932 | 7.23072e-05 / 0.000431289 | 0.000392363 | 1.07092e-10 |
| 23 / 1 | 10-20 | 0.0130853 / 0.124507 | 0.00616291 / 0.0893594 | 0.0225573 | 0.0131741 |
| 23 / 1 | 20-40 | 7.039e-05 / 0.00116629 | 1.73539e-05 / 0.000229447 | 0.000157483 | 7.41154e-19 |
| 23 / 1 | 40-60 | 4.21786e-08 / 2.51438e-07 | 1.44719e-08 / 1.48978e-07 | 8.9407e-08 | 7.41154e-19 |
| 22 / 8 | 10-20 | 3.80974e-08 / 1.88743e-07 | 1.12557e-08 / 3.94958e-08 | 9.12696e-08 | 1.82138e-10 |
| 22 / 8 | 20-40 | 3.59937e-08 / 1.94592e-07 | 1.06431e-08 / 4.11496e-08 | 2.10479e-07 | 1.82138e-10 |
| 22 / 8 | 40-60 | 3.476e-08 / 1.87985e-07 | 1.03218e-08 / 4.14212e-08 | 2.17929e-07 | 1.82138e-10 |
| 23 / 8 | 10-20 | 1.18731e-05 / 0.00029153 | 3.06255e-06 / 5.74023e-05 | 1.65184e-05 | 5.7446e-11 |
| 23 / 8 | 20-40 | 6.04382e-08 / 2.15969e-07 | 1.69492e-08 / 4.54209e-08 | 2.42493e-07 | 5.7446e-11 |
| 23 / 8 | 40-60 | 6.72642e-08 / 2.69688e-07 | 1.89101e-08 / 5.52756e-08 | 3.78583e-07 | 5.7446e-11 |

The single-pass speed-envelope fit over 20-35 s is approximately exponential: log slopes -0.102600/s (Phase 22, R-squared 0.999693) and -0.501267/s (Phase 23, R-squared 0.999354). Phase 23's final maximum speed at 60 s is 7.83198e-8, versus 2.63482e-4 for Phase 22. The initial 10-20 s window remains a transient: Phase 23 has larger angular excursions there and its max speed is slightly larger than Phase 22. This is not presented as an improvement at every instant.

At the numerical floor, Phase 23's one-pass strongest linear component has autocorrelation 0.906 at a 69-step lag (about 0.575 s), declining to 0.762 at three periods. Thus the residual contains a small oscillatory mode; exact convergence to zero or an exactly repeating limit cycle is not claimed. Its position excursion is under 9e-8 units over 40-60 s. Phase 22 at eight passes also retains a comparable speed floor without any slot churn. At eight passes Phase 23 has no comparably strong repeat peak within 240 steps. There is no evidence here of a persistent *visible* fixed-step limit cycle.

The extremely small orientation excursions require a qualification: `PhysicsBody.cpp:334` already substitutes the identity rotation when `|omega*dt| < 1e-5`. Quaternion normalization can still vary low bits. Tiny angular speed therefore does not imply an equally accumulating stored-orientation excursion. This pre-existing integration deadband was not adjusted.

**Contact identity and impulse evidence, 40-60 s.** Passive diagnostic IDs follow each cached constraint through real matches and slot moves. Cold writes receive new IDs. Counts below measure actual impulse lineage, not feature-key changes or a manifold vector moving in memory. Normal variation uses cached scalar lambda; friction variation uses the reconstructed world-space tangent impulse, avoiding false changes from tangent-basis reparameterization. Consecutive-step differences include only surviving IDs. Peak-to-peak and temporal SD below include only IDs present for the entire window.

| Phase / passes | Manifolds / points | New / retired slots | Surviving slot moves | Full-window slots | Normal delta RMS / max | Friction delta RMS / max |
| --- | --- | --- | --- | --- | --- | --- |
| 22 / 1 | 18 / 68 | 0 / 0 | 0 | 68 | 7.85154e-06 / 9.12771e-05 | 4.43711e-06 / 4.97462e-05 |
| 23 / 1 | 20 / 74 | 8631 / 8631 | 0 | 70 | 5.99309e-09 / 9.91859e-08 | 5.25002e-09 / 5.07261e-07 |
| 22 / 8 | 17 / 68 | 0 / 0 | 0 | 68 | 5.4406e-09 / 5.2155e-08 | 6.62677e-09 / 5.20417e-08 |
| 23 / 8 | 17 / 65 | 9600 / 9600 | 0 | 61 | 7.74206e-09 / 7.4506e-08 | 6.98509e-09 / 6.99705e-08 |

| Phase / passes | Normal p-p max | Friction p-p max | Normal temporal SD RMS | Friction temporal SD RMS |
| --- | --- | --- | --- | --- |
| 22 / 1 | 0.00196552 | 0.000841077 | 8.4676e-05 | 4.77104e-05 |
| 23 / 1 | 1.17393e-06 | 1.06995e-06 | 1.22567e-07 | 8.77906e-08 |
| 22 / 8 | 3.12925e-07 | 1.07847e-06 | 3.42395e-08 | 1.2684e-07 |
| 23 / 8 | 5.88968e-06 | 1.54339e-05 | 4.48043e-07 | 1.37678e-06 |

All four runs have zero manifold-pair additions/removals in this final window. Phase 23 also has zero feature-key changes and zero moves of surviving slots. It nevertheless has more point expiry/recreation: 8,631/9,600 cold writes at one/eight passes, respectively. This is explicitly **not** zero contact-slot churn. Seventy of 74 one-pass points and 61 of 65 eight-pass points survive the full window.

The one-pass churn occurs on pairs (6,10), (10,14), (11,15), and (14,15), using logical bottom-up box indices. The largest retiring normal impulse across these pairs in the late trace is 7.13439e-8; the largest retiring friction magnitude is 3.51456e-9. A direct expiry trace at step 4,800 finds positive separation of 7.53390e-11 to 3.66886e-10, tangent distance squared at most 3.55271e-15, and normal dot 1.0-1.00000012. The existing strict `separation <= 0` expiry rule removes these negligible-load witnesses; the next complete geometric patch includes them again. This is not a feature mismatch, normal-coherence rejection, or useful support-impulse reset. Changing the separation tolerance to suppress these counts would be tolerance tuning, and is not part of this review.

A final causal A/B stops all manifold expiry and contact insertion/refresh **only in the isolated diagnostic**, after step 4,800. It preserves the same first 40 seconds exactly and leaves solver iterations, impulses, gravity, integration, and all constants unchanged. During 40-60 s this control has zero new/retired slots, zero slot moves, and the same 20 manifolds/74 points. Linear RMS/max is 4.27938e-8 / 3.27587e-7, angular RMS/max 1.41355e-8 / 7.86868e-8, and position peak-to-peak 4.47035e-8. Normal/friction delta RMS is 5.91581e-9 / 4.43316e-9. The residual therefore persists at essentially the same scale without feature matching, geometry/basis transport, or contact-count changes. The same body/component still has lag-69 autocorrelation 0.894 (versus 0.906 normally), so ongoing persistence changes are not required for that microscopic oscillatory mode. This diagnostic freeze is not a proposed production policy. Logs: `bin/phase23-jitter-p23-frozen-1-*.log` and `bin/phase23-jitter-frozen-integrity.log`.

In the fixed-step 40-60 s window, total mechanical energy is constant at printed 12-digit precision for revised Phase 23: 859.206234455 (one pass) and 861.181036949 (eight). Peak kinetic energy is 5.66141e-14 / 8.43897e-14. Phase 22's eight-pass total is similarly constant at 861.806277752; its single-pass energy spans 858.539340002-858.539723442 while the residual decays. Cube rotational energy uses `I=2/3` for unit mass and side length two. No late energy or impulse burst accompanies the microscopic Phase 23 residual.

**Controlled frame-cadence intervention.** The demo passes two equal halves of a variable frame timestep to physics (`RigidBodySimulation.cpp:721-723`); `BaseApp.cpp:297-325` bounds frame duration between 16 and 33 ms. An additional paired run keeps the exact fixed-step history through 60 s, alternates two 8 ms and two 10 ms physics steps until approximately 80 s (16/20 ms frames), then restores 1/120. Both runs continue to 121.481005 s. This is a deterministic cadence example, not a recording of the human's actual rendering frame times. All first-60-second body, contact, step, and stage records match their fixed-step control exactly in both phases.

| Phase / window s | Linear RMS / max | Angular RMS / max | Position p-p | Orientation p-p rad |
| --- | --- | --- | --- | --- |
| 22 / 60-80 | 0.0141436 / 0.166779 | 0.00456147 / 0.105516 | 0.0491358 | 0.0134479 |
| 22 / 80-100 | 0.0177817 / 0.20421 | 0.00523756 / 0.0624755 | 0.0388394 | 0.00990605 |
| 22 / 100-120 | 0.00704932 / 0.033409 | 0.00168716 / 0.00644826 | 0.00631136 | 0.00162764 |
| 23 / 60-80 | 0.0154168 / 0.196956 | 0.00726309 / 0.122484 | 0.0365366 | 0.0142632 |
| 23 / 80-100 | 0.00818885 / 0.17176 | 0.00405071 / 0.108696 | 0.0195723 | 0.00924503 |
| 23 / 100-120 | 7.95774e-07 / 9.73798e-06 | 2.02494e-07 / 2.16806e-06 | 1.86265e-06 | 8.26704e-19 |

The first 8 ms step is decisive: gravity changes velocity by 0.096 while the old cached support impulse still cancels approximately 0.100. In Phase 23 the warm-start stage leaves max speed 0.00400005, and the single solve pass leaves 0.00400003. The preceding fixed step ended at 7.83198e-8. All 20 manifolds/74 points remain, with the same four negligible-load expiries already occurring before the intervention. The normal/tangent cache does not receive a discontinuous geometry transport at this onset. Phase 22, with **zero** cold writes or expiries on this step, likewise ends at 0.00400821. Both have zero TOI impulse at onset. Later contact changes occur after the motion has been excited.

The exact mechanism is shared: `ConstraintPenetration::PreSolve` applies cached impulses without scaling by the timestep ratio, and finite sequential iterations do not immediately eliminate the resulting support mismatch. Repeated timestep changes keep forcing the response. During 60-80 s, Phase 23's normal/friction step-delta RMS is 0.00351410/0.00199633, compared with approximately 6e-9/5e-9 in its fixed settled window. Its cadence-window motion is somewhat larger than Phase 22's in RMS velocity and angular excursion; no claim of uniformly better response under variable timesteps is made.

Once fixed stepping resumes, revised Phase 23 settles again: maximum linear speed is 3.12067e-6 at 101.481 s and 1.23474e-7 at 111.481 s. Phase 22 still has 0.0167329 at 111.481 s, including a new transient following the intervention. A fixed-step policy, timestep-aware warm starting, solver convergence policy, and eventual sleeping are broader work; none is introduced or tuned in Phase 23.

**Validation and artifacts.** Every diagnostic build/run exits 0 and every simulated body remains finite. The production/test source hashes match this diagnostic's entry snapshot, so the previously completed Debug/nominal Release 223 focused / 2,783 full checks, one/eight-pass stack/lattice regressions, and benchmarks still apply to the identical Phase 23 source. They were not rerun for a documentation-only update. Determinism checks also confirm the extra expiry observer reproduces all 4,812 steps and corresponding body/contact/stage rows exactly.

Raw measurements are `bin/phase23-jitter-p{22,23}-scene-{1,8}-{initial,bodies,contacts,steps,stages,summary,analysis}.log` and `bin/phase23-jitter-p{22,23}-cadence-switch-1-*.log`. Additional evidence is in `phase23-jitter-cadence-onset.log`, `phase23-jitter-expiry-detail.log`, `phase23-jitter-p23-scene-1-churn-detail.log`, `phase23-jitter-floor-correlation.log`, and `phase23-jitter-integrity.log` under `bin/`. In variable-cadence runs, stage rows retain a nominal `step/120` time field; join by step to the accumulated actual time and dt in the step/body/contact files. The inactive source/build/analysis recipe is preserved as ZIP archive `bin/phase23-jitter-diagnostic-source.bak`. All extracted diagnostic instrumentation, project copies, objects, and executables are removed before handoff.

The human changed demo-selection flags during this investigation (05:53:55 UTC, now selecting the mixed sphere/box scene). That edit is preserved and hashed in `bin/phase23-jitter-concurrent-user-state.log`. The measurements above deliberately cover the requested pure 4x4 box stack; they do not characterize the separately selected mixed scene or reproduce an unrecorded render-frame history.

## Benchmark Results

No Class D speedup claim. **Measured**, same-host submitted/revised nominal Release comparison: 100/1,000/10,000 bodies, fixed creation order, dt=1/120, one solver pass, two discarded warmup samples and five measured samples. Collision samples recreate the overlapping-box workload and measure one step. Separated steady-state samples recreate the workload, warm it for four steps, and average eight measured steps; the table reports the median of five sample averages. Runs are sequential, with no concurrent agent build or test.

| Workload | Bodies | Submitted median ms | Revised median ms | Contacts/manifolds/points/constraints (each) | Identical final-state fingerprint |
| --- | --- | --- | --- | --- | --- |
| collision | 100 | 4.029500 | 4.025800 | 38 | 5169353994866276594 |
| collision | 1000 | 40.547000 | 40.933400 | 375 | 11439712392227242364 |
| collision | 10000 | 441.042400 | 492.855300 | 3750 | 606644245463582146 |
| separated | 100 | 0.018475 | 0.018812 | 0 | 18053003736431092937 |
| separated | 1000 | 0.187413 | 0.197000 | 0 | 17122425323131172573 |
| separated | 10000 | 2.040250 | 2.771387 | 0 | 16659606550767655509 |

Every non-timing benchmark field, including candidates, GJK/EPA calls/iterations, contacts, retained constraints and fingerprint, is identical before/after. Collision contact/manifold/point/constraint counts are 38, 375, and 3,750; separated counts are zero. Collision 10,000-body timing increased from 441.042400 to 492.855300 ms and separated timing from 2.040250 to 2.771387 ms in these first measurements. These workloads do not exercise multi-point patch refresh (collision emits one witness per pair; separated has no contacts). A saved-binary A-B-B-A follow-up below resolves whether this first comparison supports an implementation-cost conclusion. Full profiler counters/stage means and sample ranges are in `bin/phase23-revision-before-{collision,separated}.log` and `bin/phase23-revised-{collision,separated}.log`.

**Measured**, follow-up using the same saved submitted binary (A) and final revised binary (B), same configuration/options, run sequentially A1-B1-B2-A2. Each entry remains a five-sample median after two warmups; this is a repeated comparison, not pooled samples.

| Workload | Bodies | A1 median ms | B1 median ms | B2 median ms | A2 median ms |
| --- | --- | --- | --- | --- | --- |
| collision | 100 | 4.507300 | 4.327500 | 4.385500 | 4.651900 |
| collision | 1000 | 47.297400 | 47.999400 | 48.532600 | 45.802700 |
| collision | 10000 | 538.812400 | 530.258700 | 516.175600 | 461.103800 |
| separated | 100 | 0.020037 | 0.019387 | 0.019762 | 0.018937 |
| separated | 1000 | 0.212300 | 0.198425 | 0.206675 | 0.189562 |
| separated | 10000 | 4.139475 | 3.838700 | 3.188525 | 2.176712 |

The unchanged submitted binary itself varies from 538.812400 to 461.103800 ms for collision 10,000 and from 4.139475 to 2.176712 ms for separated 10,000. Thus the first before/after timing increase cannot be isolated from host timing variation by these samples. Both revised 10,000-body medians lie inside the submitted range. All counters and fingerprints match across all runs. **No speedup or precise overhead bound is claimed.** Logs: `bin/phase23-revised-paired-{A1,B1,B2,A2}-{collision,separated}.log`. Every run exits 0. These stock benchmarks do not directly measure the bounded complete-patch refresh; stack timing above/below reflects different contact trajectories.

**Measured**, regression costs below are arithmetic means over 1,200 steps, not warmed benchmark medians. Different simulated contact trajectories make these diagnostic costs, not a speedup claim.

| State / passes | Stack step ms | Stack solver ms | Lattice step ms | Lattice solver ms |
| --- | --- | --- | --- | --- |
| Phase 22 / 1 | 2.993721 | 1.767738 | 19.630455 | 14.883664 |
| Submitted 23 / 1 | 3.292807 | 1.804643 | 12.909180 | 9.216432 |
| Revised 23 / 1 | 3.083803 | 1.724517 | 12.865053 | 9.311364 |
| Phase 22 / 8 | 13.196660 | 11.638269 | 108.933581 | 104.597320 |
| Submitted 23 / 8 | 13.780868 | 12.048625 | 71.029816 | 66.882036 |
| Revised 23 / 8 | 12.931757 | 11.293899 | 70.309124 | 66.390256 |

**Measured**, final Debug regression means (reported separately from Release):

| Passes | Stack step ms | Stack solver ms | Lattice step ms | Lattice solver ms |
| --- | --- | --- | --- | --- |
| 1 | 17.959409 | 6.098005 | 60.482063 | 32.576742 |
| 8 | 51.969364 | 38.360071 | 254.131153 | 223.918310 |

Solver telemetry remains exactly the requested one/eight passes for stack/lattice. No allocator, solver storage, profiling, or benchmark implementation change is included.

## Behavior Changes

Geometry refresh, feature matching, invalidation, and whole-patch replacement change retention and warm starts for boxes and generic contacts such as sphere lattices. Stable box patches preserve compatible impulses and retire absent geometric points. The revision preserves support impulses across nearby same-face boundary classification changes and keeps surviving constraints in prior solve order.

## Known Limitations

- Generic sphere/convex/unsupported-box witnesses have no keys and use anchor pairs and normal coherence.
- The 0.02 tolerance remains absolute. Distinct small-box keys do not guarantee scale-independent solver stability.
- Five degrees is a reuse policy; accumulated relative rotation can make valid contacts start cold.
- Stamps require live bodies and destruction callbacks. Shapes lack global lifetime generations; same-address/same-revision shape reconstruction is not detectable.
- No timestep-ratio scaling, general body-property revisions, sleeping, or islands.
- Temporary patch storage is bounded but still contains the existing heap-backed solver algebra; no allocation or lookup optimization.
- Generic GJK/EPA/CCD and the default single-pass solver still limit stability.

## Out-of-Scope Findings

- The box micro-jitter diagnostic reproduces a shared variable-timestep support mismatch: demo two-half-frame stepping (`RigidBodySimulation.cpp:721-723`, `BaseApp.cpp:297-325`) changes gravity impulse while `ConstraintPenetration::PreSolve` reuses unscaled cached impulses. Finite-pass solving leaves repeatedly excited motion. Phase 23 settles again under fixed stepping; no timestep, solver, or sleeping policy is changed.
- `PhysicsBody.cpp:334` retains its existing 1e-5 per-update angular-displacement deadband. It explains why tiny angular velocities can coexist with practically constant orientation; changing it is outside this review.

- Long-horizon traces expose landing/rebound warm-start overshoot already present in Phase 22. At eight passes the positive energy increment at 1.225 s is +699.855893 (Phase 22) / +693.990336 (Phase 23). Stage evidence above locates it in warm start and incomplete iterative correction (PhysicsSystem.cpp:382-399, ConstraintPenetration.cpp:161). It decays before the quiet-state collapse mechanism; no impact-solver change is authorized by this diagnostic.
- `PhysicsSystem.cpp`, `ConservativeAdvance` still temporarily advances and rewinds live bodies: **PHYS-BUG-009**, Phase 24. Only feature-output initialization changes there.
- Its positive-TOI loop still uses precomputed contacts; revalidation remains Phase 25.
- `GEngine/include/GEngine/Physics/ShapeBox.cpp:196` retains off-origin parallel-axis inertia; `:244` uses local corner offsets in angular sweep estimates.
- `GEngine/GEngine.vcxproj:86` and `PhysicsTests/PhysicsTests.vcxproj:87` retain Release /MTd.
- Pre-existing `.gitignore:33` whitespace remains untouched.

## git diff --check

```text
.gitignore:33: new blank line at EOF.
```

Global exit **2**, entirely pre-existing. Phase-owned tracked diff: no diagnostics, exit **0**. New review: `git diff --no-index --check NUL docs/physics/PHASE_23_REVIEW.md`; exit 1 denotes a new file differing from NUL, with no whitespace diagnostics allowed.

## git status --short

~~~text
 M .gitignore
 M GEngine/include/GEngine/Physics/BoxContact.h
 M GEngine/include/GEngine/Physics/Contact.h
 M GEngine/include/GEngine/Physics/Manifold.cpp
 M GEngine/include/GEngine/Physics/Manifold.h
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? docs/audit/
?? docs/physics/PHASE_23_REVIEW.md
?? docs/physics/PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
?? docs/physics/review/PHASE_05_REVIEW.md
?? docs/rendering/
~~~

Fourteen of the 15 pre-existing user files retain their original START snapshot hashes. The untouched `RigidBodySimulation.cpp` was modified before this revision (last write 03:57:43 UTC; revision backup created 04:03:07 UTC). Its newer content is preserved; `bin/phase23-revision-user-files.log` records the current 15-file hashes. Only the seven phase files are changed/created by this phase. During the long-horizon investigation the agent changed only this review. Physics production/test hashes match entry. The human concurrently edited RigidBodySimulation.cpp at 05:09:40 UTC to select the lattice demo; its geometry/material values still match the diagnostic fixture. That edit was preserved, with its hash recorded in bin/phase23-long-concurrent-user-state.log. The index remains empty; HEAD remains the approved Phase 22 commit.

During the box micro-jitter review, only this review document was changed by the agent. All six Phase 23 production/test files retain their entry hashes. The additional concurrent human demo edit at 05:53:55 UTC was preserved; current ownership evidence is in `bin/phase23-jitter-entry-hashes.log` and `bin/phase23-jitter-concurrent-user-state.log`.

## Human Decision

PENDING

Stop at human review under the execution skill. No staging, commit, approved tag, push, or Phase 24 start.
