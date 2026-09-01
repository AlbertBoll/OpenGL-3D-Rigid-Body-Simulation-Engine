# Phase 01 Review - Physics Performance Baseline

## Phase

Phase 01 - Physics Performance Baseline

## Objective

Establish reliable measurements for every major physics subsystem, with
special GJK/support metrics and a repeatable headless benchmark, before any
physics optimization is attempted.

## Status

APPROVED

Approved by the human reviewer on 2026-08-31.

Checkpoint commit message: `physics: phase 01 establish performance baseline`

Checkpoint reference: `physics-phase-01-approved`

## Baseline commit

`1ba94b5520fc2d03b0446e8074ead9204bbc308e`

Approved checkpoint: `physics-phase-00-approved`

Branch: `physics/refactor`

The checkpoint tag and `HEAD` matched at phase entry. The only pre-existing
untracked inputs were:

```text
?? .AGENTS.md
?? docs/physics/PHYSICS_REFACTOR_PLAN.md
```

They remain preserved and are not Phase 01 deliverables.

## Files modified

- `GEngine/include/GEngine/Physics/GJK.cpp`
- `GEngine/include/GEngine/Physics/Manifold.cpp`
- `GEngine/include/GEngine/Physics/Manifold.h`
- `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`
- `premake5.lua`

## Files created

- `GEngine/include/GEngine/Physics/PhysicsProfile.cpp`
- `GEngine/include/GEngine/Physics/PhysicsProfile.h`
- `PhysicsBenchmark/README.md`
- `PhysicsBenchmark/src/main.cpp`
- `docs/physics/PHYSICS_OPTIMIZATION.md`
- `docs/physics/review/PHASE_01_REVIEW.md`

## Files deleted

None.

## Original problem

The repository had no runnable physics benchmark and no permanent subsystem
profiling. The graphical RigidBodySimulation path remained blocked before its
first physics frame by the existing ImGui font-path assertion, so there were no
repeatable numbers for broadphase, filtering, GJK/support, EPA/contact,
manifolds, solver, integration, or total physics time.

## Root cause

Physics evolved as part of the interactive application. Timing comments in
`PhysicsSystem.cpp` were disabled and ad hoc, there was no headless entry point,
and no compile-time facility exposed stable counters or controlled hot-loop
timers. As a result, later optimization phases lacked a comparable baseline.

## Implementation approach

- Added `PhysicsProfileSnapshot`, reset/read APIs, guarded nanosecond scopes,
  and guarded GJK-call iteration aggregation.
- Instrumented total physics update, body state, gravity, broadphase, pair
  filtering, narrowphase, GJK, support, EPA, contacts, manifold maintenance,
  solver constraints/passes, contact resolution, and integration.
- Added a Premake `--physics-profiling` option defining
  `GE_ENABLE_PHYSICS_PROFILING` only when explicitly requested.
- Added a native-console benchmark target with `SDL_MAIN_HANDLED`; it performs
  no window, renderer, UI, or asset initialization.
- Built a deterministic workload with isolated overlapping dynamic/static,
  static/static, and dynamic/dynamic box pairs; setup is outside timing and the
  scene is recreated for every sample.
- Added finite-state and counter consistency checks that fail the benchmark
  process on invalid results.
- Collected 5 warmups and 25 samples at 50, 100, 200, 500, 1,000, and 2,000
  bodies in both compile-out and profiling-enabled Release builds.
- Recorded metric definitions, commands, results, overhead, and limitations in
  `docs/physics/PHYSICS_OPTIMIZATION.md`.

## Correctness changes

None. No collision, broadphase, GJK/EPA, manifold, solver, integration, body,
shape, timestep, or ECS behavior was intentionally changed.

`ManifoldCollector::GetContactCount` is a read-only observation helper. All
production instrumentation mutations are confined to separate profiling state
when explicitly enabled.

## Performance changes

No physics optimization was made and no speedup is claimed.

Normal generated projects omit `GE_ENABLE_PHYSICS_PROFILING`; expensive
instrumentation compiles to no-ops. Explicit profiling produced 3.42%-5.31%
median overhead in the controlled benchmark. This cost is accepted only in the
opt-in measurement build.

## Architecture changes

- Added an optional serial profiling boundary around the existing physics
  architecture.
- Added one independent headless benchmark product to the Premake workspace.
- Did not change production step order, ownership, algorithms, data layout, or
  requirements assigned to later phases.

## Tests added

`PhysicsBenchmark` is a permanent benchmark with validation checks for:

- finite position, linear velocity, angular velocity, and orientation;
- one recorded physics step;
- expected body, dynamic, active, and sleeping counts;
- candidate and rejected pair activity;
- nonzero narrowphase, GJK, support, EPA, contact, manifold, solver,
  integration, and total-time metrics in a profiling build;
- finite positive external step time in both profiling modes.

No general unit-test framework was added; that remains governed by later test
phases.

## Tests executed

- Verified `physics-phase-00-approved` and `HEAD` both resolved to `1ba94b5`.
- Generated normal Visual Studio 2022 projects: passed.
- Built the benchmark and engine in Debug x64 during implementation: passed.
- Built the benchmark and engine in Release x64 with profiling disabled: passed.
- Built the benchmark and engine in Release x64 with profiling enabled: passed.
- Ran profiling-enabled smoke validation at 10 bodies: passed.
- Ran compile-out benchmark validation at all six required body counts: passed.
- Ran profiling-enabled benchmark validation at all six required body counts:
  passed, including nonzero static/static rejection and all required subsystem
  metrics.
- Re-ran both full normal solution configurations after the final source-only
  formatting adjustment: Debug and Release passed.
- Ran `git diff --check`: passed; only existing line-ending conversion notices
  were emitted.

## Build results

Premake generation passed in normal and `--physics-profiling` modes. MSBuild
used the installed Visual Studio MSBuild executable with a normalized `PATH`
outside the restricted sandbox because MSVC FileTracker requires system access.

The new benchmark follows the repository's existing unconditional `/MTd`
Windows build option so its Release link ABI matches the GEngine static library.
The existing `/MT` overridden-by-`/MTd` warning remains; Phase 01 does not alter
the repository-wide runtime-library policy.

## Debug x64 result

Passed. The normal, profiling-disabled full-solution build completed with
exit code 0 and no errors emitted by the errors-only console logger. Elapsed
wall time was approximately 15 seconds for the incremental validation build.
The final incremental recheck completed in 7.08 seconds.

The repository's existing warning baseline remains; Phase 01 does not attempt
warning remediation.

## Release x64 result

Passed. The normal, profiling-disabled full-solution build completed with
exit code 0 and no errors emitted by the errors-only console logger. Rebuilding
the engine after removing the profiling definition took approximately 89
seconds.
The final incremental recheck completed in 2.12 seconds.

The final Release benchmark identified itself as
`physics_profiling=disabled`, returned exit code 0 at every required body count,
reported zero internal instrumentation values, and retained finite body state.

## Benchmark before

No repeatable physics benchmark or subsystem profile existed. The only prior
runtime attempt used RigidBodySimulation and stopped at the pre-existing ImGui
font assertion before a measurable physics frame.

## Benchmark after

The headless benchmark completed at 50, 100, 200, 500, 1,000, and 2,000 bodies.
Profile-enabled median external step time ranged from 1.2191 ms to 53.6605 ms.
Every surviving pair generated one GJK query, one EPA call, one contact, one
manifold, and one solver constraint in this deterministic fixture.

The complete counts, subsystem times, compile-out timings, commands, metric
semantics, and environment are recorded in
`docs/physics/PHYSICS_OPTIMIZATION.md`.

## Percentage improvement

Not applicable. Phase 01 establishes measurement infrastructure and baseline
numbers; it does not optimize physics. Profiling overhead is reported separately
and is not a performance improvement.

## Known behavior changes

None in physics behavior.

The workspace gains a new opt-in profiler API, a Premake option, and a headless
benchmark executable. These additions do not alter normal application behavior.

## Known risks

- Profiling accumulation is non-atomic and valid only for the current serial
  physics execution; later parallel work must use per-thread aggregation.
- Explicit per-support timers intentionally perturb profiled performance by up
  to 5.31% in this workload; normal Release builds compile them out.
- The benchmark is contact-heavy and is not a substitute for representative
  gameplay workloads or separated-body broadphase studies.
- Positive-time-of-impact contact resolution is tracked but not exercised by
  the initial-overlap fixture.
- Existing numerical, timestep, ownership, broadphase allocation, solver
  allocation, and application startup issues remain unchanged.

## Remaining issues

All Phase 02 and later requirements remain unchanged and unstarted. In
particular, Phase 01 makes no numerical robustness, transform convention,
derived-data caching, broadphase, GJK/EPA, prediction, manifold, solver,
timestep, sleeping, island, ownership, data-layout, or parallelism changes.

Future performance phases must run comparable before/after measurements against
this baseline or document why a changed workload is required.

## git diff --stat

Command:

```powershell
git -c safe.directory=C:/dev/GEngine-physics diff --stat
```

Output:

```text
 GEngine/include/GEngine/Physics/GJK.cpp           | 13 ++++-
 GEngine/include/GEngine/Physics/Manifold.cpp      | 10 ++++
 GEngine/include/GEngine/Physics/Manifold.h        |  1 +
 GEngine/include/GEngine/Physics/PhysicsSystem.cpp | 68 +++++++++++++++++++++--
 premake5.lua                                      | 66 +++++++++++++++++++++-
 5 files changed, 150 insertions(+), 8 deletions(-)
```

Ordinary `git diff --stat` does not include untracked new files. Their final
line counts are recorded separately:

```text
GEngine/include/GEngine/Physics/PhysicsProfile.cpp       95 lines
GEngine/include/GEngine/Physics/PhysicsProfile.h        118 lines
PhysicsBenchmark/README.md                               47 lines
PhysicsBenchmark/src/main.cpp                           363 lines
docs/physics/PHYSICS_OPTIMIZATION.md                    197 lines
docs/physics/review/PHASE_01_REVIEW.md                  293 lines
```

## git status --short

```text
 M GEngine/include/GEngine/Physics/GJK.cpp
 M GEngine/include/GEngine/Physics/Manifold.cpp
 M GEngine/include/GEngine/Physics/Manifold.h
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M premake5.lua
?? .AGENTS.md
?? GEngine/include/GEngine/Physics/PhysicsProfile.cpp
?? GEngine/include/GEngine/Physics/PhysicsProfile.h
?? PhysicsBenchmark/README.md
?? PhysicsBenchmark/src/main.cpp
?? docs/physics/PHYSICS_OPTIMIZATION.md
?? docs/physics/PHYSICS_REFACTOR_PLAN.md
?? docs/physics/review/PHASE_01_REVIEW.md
```

`.AGENTS.md` and `docs/physics/PHYSICS_REFACTOR_PLAN.md` are the pre-existing
untracked inputs. All other entries are Phase 01 deliverables. Nothing is
staged or committed.
