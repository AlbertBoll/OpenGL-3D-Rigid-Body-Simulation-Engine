# Phase 04 Review - Broadphase Refactor

## Phase

Phase 04 - Broadphase Refactor

## Objective

Improve sweep-and-prune scalability and memory behavior while preserving
candidate correctness, rejecting safe non-candidates before narrowphase, and
leaving Phase 05 untouched.

## Status

AWAITING HUMAN REVIEW

## Baseline and phase entry

Approved checkpoint: `physics-phase-03-approved`

Checkpoint commit and phase-entry `HEAD`:
`5321ef26763857d919485d78e1e0e6fb3fe05929`

Branch: `physics/refactor`

The phase-entry workspace contained these unrelated changes. They were
preserved and excluded from Phase 04:

```text
 M RigidBodySimulation/imgui.ini
?? .AGENTS.md
?? docs/rendering/
```

An isolated source tree from the approved tag was used for the profiling
baseline so Phase 04 source changes could not contaminate its measurements.

## Files modified

- `GEngine/include/GEngine/Component/Component.h`
- `GEngine/include/GEngine/Physics/Broadphase.cpp`
- `GEngine/include/GEngine/Physics/Broadphase.h`
- `GEngine/include/GEngine/Physics/PhysicsBody.h`
- `GEngine/include/GEngine/Physics/PhysicsProfile.h`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `GEngine/include/GEngine/Physics/PhysicsSystem.h`
- `GEngine/src/Scene/_Scene.cpp`
- `PhysicsBenchmark/README.md`
- `PhysicsBenchmark/src/main.cpp`
- `PhysicsTests/README.md`
- `PhysicsTests/src/main.cpp`

## Files created/deleted

Created: `docs/physics/review/PHASE_04_REVIEW.md`

Deleted: none.

## Original problem and implementation

The old broadphase rebuilt endpoints every step on stack memory using
`_alloca`, fully sorted them with `qsort`, and used a comparator that never
returned equality. It swept along a diagonal projection, admitted substantial
three-dimensional false positives, did not support collision layers/masks, and
left static/static rejection to the narrowphase. `PhysicsSystem` also rebuilt
pair/contact vectors per step and reserved `bodyCount * bodyCount` contacts.

Phase 04:

- Replaced the transient implementation with an owned
  `SweepAndPruneBroadphase` whose endpoint, swept-bound, and active-set storage
  persists across steps.
- Performs a valid deterministic full sort only when body membership or order
  changes, then uses insertion sorting to exploit temporal coherence.
- Sweeps conservative velocity-expanded, margin-expanded world AABBs on the X
  axis and confirms overlap on all three axes before emitting a pair.
- Orders minimum endpoints before maximum endpoints at equal values so touching
  intervals remain candidates.
- Rejects static/static and incompatible reciprocal collision layer/mask pairs
  before narrowphase. Existing bodies retain prior behavior through defaults
  of layer 1 and an all-bits mask; scene-authored values are copied to runtime
  bodies.
- Gives `PhysicsSystem` reusable pair and contact vectors, removing the
  quadratic contact reserve and function-static contact storage.
- Retains a defensive static/mask check at the narrowphase boundary.
- Adds profiling counters for axis overlaps, three-axis AABB rejection,
  static/mask rejection, insertion-sort swaps, and full sorts.
- Removes the dead raw-body overload, duplicate endpoint builders, stack
  allocation, and invalid `qsort` comparator.

The engine does not yet contain a sleeping-state contract: the profiler
explicitly reports zero sleeping bodies, and sleeping is planned for Phase 12.
Consequently, no speculative sleeping filter was added. Static/static filtering
is safe now; sleeping filtering remains governed by Phase 12.

## Correctness and persistence tests

The permanent suite now reports 64 checks, up from the approved Phase 03 suite's
52. New coverage verifies:

- deterministic equal-endpoint handling for touching intervals;
- static/static and reciprocal layer/mask rejection;
- secondary-axis AABB rejection after a sweep-axis overlap;
- velocity-expanded retention of a fast-moving candidate;
- endpoint and active-set capacity reuse;
- zero full sorts and zero swaps for unchanged bodies;
- insertion-sort repair after motion and full rebuild after membership change;
- exact candidate-set equality with a filtered swept-AABB brute-force oracle
  for 24 mixed static/dynamic, velocity, and layer/mask bodies, both initially
  and after movement.

The existing mathematical, transform, cache, gravity, and collision regression
checks continue to pass.

## Before/after scaling

Normal Release runs used 5 outer warmups and 25 samples. The steady-state
fixture used 4 untimed steps and 8 measured same-body steps per sample. The
table reports external median milliseconds per physics step.

| Bodies | Phase 03 approved ms | Phase 04 ms | Improvement |
|---:|---:|---:|---:|
| 50 | 0.170712 | 0.010688 | 93.74% |
| 100 | 0.344650 | 0.021600 | 93.73% |
| 200 | 0.662388 | 0.043625 | 93.41% |
| 500 | 1.653650 | 0.112150 | 93.22% |
| 1,000 | 3.454000 | 0.232825 | 93.26% |
| 2,000 | 6.993188 | 0.474237 | 93.22% |

The final benchmark reported `physics_profiling=disabled`. Profiling-enabled
runs explain the result: the approved implementation sent 49, 99, 199, 499,
999, and 1,999 candidates per step into pair filtering and then made 74, 150,
298, 750, 1,498, and 2,998 GJK calls. Phase 04 emits zero candidates and makes
zero GJK calls for this separated fixture. After warmup it also reports zero
full sorts and zero insertion-sort swaps, proving stable endpoint reuse.

## Overlapping-workload preservation

The isolated-pair workload confirms that early filtering does not remove real
collision work. For 50 through 2,000 bodies, Phase 04 sends 19, 38, 75, 188,
375, and 750 final candidates to narrowphase. The broadphase rejects the same
6, 12, 25, 62, 125, and 250 static/static pairs formerly rejected later.
GJK, EPA, contact, manifold, constraint, iteration, and support-call counts
match the approved checkpoint exactly at every size.

Normal compile-out medians for the overlapping fixture changed from
1.277800/2.618500/5.264300/12.599600/26.186800/56.294800 ms to
1.281700/2.536500/5.078500/12.212100/25.617900/53.091600 ms. The resulting
range is a 0.31% regression to a 5.69% improvement. Each sample recreates its
world and measures only one collision-heavy step, so this fixture cannot reuse
persistent state and remains dominated by unchanged narrowphase/contact work.
No speedup claim is based on it.

Profiling shows the safer broadphase bookkeeping itself costs more in the
unchanged separated fixture: 0.007831 to 0.334660 ms versus 0.006018 to
0.229552 ms at 50 to 2,000 bodies. This is a known tradeoff of retaining full
swept AABBs and applying safe filters. The end-to-end result is still a
94.03%-94.23% profiling-build improvement because false GJK work is removed.

## Validation executed

- Verified `HEAD` exactly matches `physics-phase-03-approved`: passed.
- Generated profiling-enabled Visual Studio 2022 projects: passed.
- Built and ran isolated approved-checkpoint profiling Release benchmarks in
  both overlapping and steady-state modes: passed.
- Built and ran current profiling Release benchmarks in both modes: passed;
  workload validation accepted every body count.
- Restored normal Visual Studio 2022 generation: passed.
- Built the complete normal solution in Debug x64 and Release x64: passed.
- Ran Debug x64 and Release x64 `PhysicsTests`: 64/64 passed in each.
- Ran final normal Release benchmarks in both modes with 5 warmups and 25
  samples: passed; both reported `physics_profiling=disabled`.
- Ran `git diff --check`: passed; only line-ending conversion notices appeared.

The full builds retain pre-existing compiler/linker warnings, including spdlog
checked-iterator deprecations, numeric-conversion warnings, duplicate
`ShapeConvex` definitions, and missing optional editor post-build copy folders.
Both MSBuild invocations exited successfully. As in prior phases, the host's
duplicate `Path`/`PATH` entries were normalized for MSBuild subprocesses.

## Known behavior changes and risks

- Collision layers/masks are now runtime behavior. Defaults preserve legacy
  all-pair behavior; a pair is accepted only when both masks accept the other
  body's layer.
- Broadphase output order is deterministic under the current body ordering but
  can differ from the old invalid-comparator `qsort` order. Contact sorting and
  solver behavior remain unchanged.
- The X sweep is conservative because a full swept-AABB check follows it. Its
  efficiency depends on scene distribution; a later phase may evaluate axis
  selection only with profiling evidence.
- Directly reordering the physics body's pointer vector triggers a safe full
  rebuild. Stable membership uses incremental repair.
- The compatibility `BroadPhase` free function creates temporary state for
  external callers; `PhysicsSystem`, the production owner, uses persistent
  state.
- Broadphase and reusable scratch state remain serial. Parallel ownership and
  synchronization remain future-phase work.

## Remaining issues

All Phase 05 and later requirements remain unchanged and unstarted. GJK core
optimization, support mapping, prediction, manifold policy, solver layout,
fixed timestep, sleeping, ownership, islands, data layout, and parallel
execution remain future phases.

## git diff --stat

Phase 04 tracked paths, excluding the preserved pre-existing
`RigidBodySimulation/imgui.ini` and this untracked review file:

```text
12 files changed, 476 insertions(+), 196 deletions(-)
```

## Expected final git status

```text
 M GEngine/include/GEngine/Component/Component.h
 M GEngine/include/GEngine/Physics/Broadphase.cpp
 M GEngine/include/GEngine/Physics/Broadphase.h
 M GEngine/include/GEngine/Physics/PhysicsBody.h
 M GEngine/include/GEngine/Physics/PhysicsProfile.h
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M GEngine/include/GEngine/Physics/PhysicsSystem.h
 M GEngine/src/Scene/_Scene.cpp
 M PhysicsBenchmark/README.md
 M PhysicsBenchmark/src/main.cpp
 M PhysicsTests/README.md
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/imgui.ini
?? .AGENTS.md
?? docs/physics/review/PHASE_04_REVIEW.md
?? docs/rendering/
```

Nothing is staged or committed. No tag was created, nothing was pushed, and
Phase 05 was not started.
