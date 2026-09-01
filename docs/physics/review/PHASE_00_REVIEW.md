# Phase 00 Review - Physics Architecture Reconstruction

## Phase

Phase 00 - Physics Architecture Reconstruction

## Objective

Fully understand and document the current execution path from the
RigidBodySimulation application through physics stepping, collision detection,
contact/constraint solving, integration, and ECS transform synchronization,
without changing production behavior.

## Status

APPROVED

Approved by the human reviewer on 2026-08-31.

Checkpoint commit message: `physics: phase 00 architecture baseline`

Checkpoint reference: `physics-phase-00-approved`

## Baseline commit

`cc40080962fc7529c8d60c1e79a4f3aa9d3c9d2b`

Branch: `physics/refactor`

The phase-entry working tree already contained these untracked inputs:

```text
?? .AGENTS.md
?? docs/physics/PHYSICS_REFACTOR_PLAN.md
```

They were preserved and are not Phase 00 deliverables.

## Files modified

None.

## Files created

- `docs/physics/PHYSICS_REFACTOR.md`
- `docs/physics/review/PHASE_00_REVIEW.md`

## Files deleted

None.

## Original problem

The physics implementation was distributed across the application loop, ECS
scene lifecycle, runtime components, physics world/system, broadphase, GJK/EPA,
manifold cache, constraint solver, body integration, and transform
synchronization. There was no single current-source architecture description
that established execution order, ownership, hot loops, update frequency, and
mutation boundaries before later refactoring.

## Root cause

The code evolved as an application-integrated custom physics subsystem. Runtime
orchestration is centralized in `PhysicsSystem::Update`, but construction and
pose synchronization live in `_Scene`, frame subdivision lives in
`RigidBodySimulationApp`, and timing originates in `BaseApp`. Raw-pointer
ownership and query-time body mutation are implicit rather than expressed as a
formal architecture contract.

## Implementation approach

- Read `.AGENTS.md`, the full master plan, and the full audit.
- Verified the audit's physics findings against baseline commit `cc400809` and
  the current workspace.
- Traced the active 64-dynamic-sphere/5-static-box demo from initialization to
  rendering.
- Inspected physics headers and implementations, ECS fixture/runtime component
  definitions, scene lifecycle, frame timing, and Premake compilation rules.
- Documented files, classes, functions, ownership, data structures, hot loops,
  update frequencies, query and mutation boundaries, and known later-phase
  risks in `docs/physics/PHYSICS_REFACTOR.md`.
- Did not edit production code or begin Phase 01 instrumentation.

## Correctness changes

None. Phase 00 is documentation-only.

Confirmed correctness risks were recorded for later phases without being
changed, including degenerate normalization/division, quaternion-space
convention mismatch, mutation-based collision prediction, and zero-inverse-mass
intermediate arithmetic.

## Performance changes

None. No production code, build setting, algorithm, or runtime parameter was
changed.

Architectural hot paths and allocation sites were identified, but no speedup is
claimed.

## Architecture changes

None to production architecture.

The new document establishes an authoritative current-state reconstruction of:

```text
RigidBodySimulation -> _Scene -> PhysicsSystem -> PhysicsWorld
  -> gravity -> broadphase -> pair filter -> narrowphase
  -> sphere CCD or GJK/EPA -> contacts/manifolds
  -> penetration constraints -> impulses -> integration
  -> ECS transform synchronization
```

## Tests added

None. Adding the test framework and permanent physics regression tests belongs
to later phases; adding a benchmark/profiling target belongs to Phase 01.

## Tests executed

- Premake Visual Studio 2022 generation: passed.
- Repository test-target discovery with `rg`: no automated test framework,
  registered test target, or physics test executable exists in project-owned
  source.
- Complete Debug x64 solution build: passed.
- Complete Release x64 solution build: passed.
- Release RigidBodySimulation startup smoke/benchmark attempt: executed; the
  program stopped before the frame/physics loop at the pre-existing ImGui font
  assertion.

There are no automated tests to report as passing or failing. The compilation
of every physics implementation file in both configurations is the available
non-runtime validation for this documentation-only phase.

## Build results

Solution generation command:

```powershell
& .\vendor\bin\premake\premake5.exe vs2022
```

Result: passed; generated `GEngine.sln` and all project files.

MSBuild required two environment accommodations that do not modify the
repository:

- a single normalized `PATH` entry to avoid the sandbox's duplicate
  `Path`/`PATH` process block;
- execution outside the restricted sandbox so MSVC FileTracker could access
  its system tracking resources.

The first in-sandbox attempts failed before meaningful compilation for those
environment reasons. The final approved build commands completed normally.

## Debug x64 result

Passed.

```text
Build succeeded.
894 Warning(s)
0 Error(s)
Time Elapsed 00:01:17.92
```

The warning baseline includes known project/vendor warnings. Physics-relevant
examples observed in this build include integer narrowing and duplicate
`ShapeConvex` definitions (`LNK4006`). No warning was introduced by Phase 00,
which changes Markdown only.

## Release x64 result

Passed.

```text
Build succeeded.
966 Warning(s)
0 Error(s)
Time Elapsed 00:01:36.49
```

## Benchmark before

No usable physics timing baseline exists. The repository has no physics
benchmark target or permanent physics profiler.

A bounded Release RigidBodySimulation launch was executed as the available
benchmark/smoke attempt. It exited with code 3 after approximately 5 seconds.
A direct output capture confirmed:

```text
Assertion failed: (0) && "Could not load font file!",
file C:\dev\GEngine-physics\GEngine\include\external\imgui\imgui_draw.cpp,
line 2161
```

This is the pre-existing clean-package font-path defect GE-003 documented by
the audit. The failure happens before scene initialization reaches a measurable
physics frame.

## Benchmark after

Not applicable. Phase 00 does not alter production code, and the same startup
blocker prevents a comparable post-change measurement.

Creating a repeatable physics benchmark and subsystem counters is explicitly
reserved for Phase 01 and was not started.

## Percentage improvement

Not applicable. There is no performance change and no percentage claim.

## Known behavior changes

None.

## Known risks

- Runtime validation cannot reach the physics loop until the existing font
  asset-path/staging defect is addressed in an authorized phase.
- There is no automated regression suite to validate physics behavior.
- Architecture statements describe the current active demo and source paths;
  commented experimental fixtures are identified as inactive.
- Existing raw ownership, numerical robustness, timestep, and performance risks
  remain intentionally unchanged.
- The warning baseline is large and contains known project defects, but both
  required configurations build with zero errors.

## Remaining issues

All implementation work remains for later phases. In particular:

- Phase 01 must establish a runnable, repeatable physics performance baseline
  and low-overhead subsystem/GJK counters.
- Numerical correctness, transform convention, derived-data caching,
  broadphase, GJK/EPA, pure prediction, manifolds, solver, fixed timestep,
  sleeping, islands, ownership, data layout, and parallelism remain governed by
  their respective future phases.
- The pre-existing startup/font-path defect blocks runtime benchmarking.

No later phase has been started.

## git diff --stat

Command:

```powershell
git -c safe.directory=C:/dev/GEngine-physics diff --stat
```

Output: no output. Both Phase 00 deliverables are untracked new files, and
ordinary `git diff --stat` does not include untracked files.

Phase 00 deliverable sizes at review creation:

```text
docs/physics/PHYSICS_REFACTOR.md            514 lines
docs/physics/review/PHASE_00_REVIEW.md      284 lines
```

## git status --short

Expected post-approval output after committing the two Phase 00 deliverables:

```text
?? .AGENTS.md
?? docs/physics/PHYSICS_REFACTOR_PLAN.md
```

The expanded review-time status distinguished the pre-existing inputs and the
then-untracked Phase 00 deliverables:

```text
?? .AGENTS.md
?? docs/physics/PHYSICS_REFACTOR.md
?? docs/physics/PHYSICS_REFACTOR_PLAN.md
?? docs/physics/review/PHASE_00_REVIEW.md
```

`.AGENTS.md` and `docs/physics/PHYSICS_REFACTOR_PLAN.md` remain intentionally
uncommitted because they predated Phase 00 and were excluded from the approved
deliverables.
