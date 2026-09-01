# GEngine Physics Performance Baseline

## Phase and scope

- Phase: 01 - Physics Performance Baseline
- Approved entry checkpoint: `physics-phase-00-approved`
- Checkpoint commit: `1ba94b5520fc2d03b0446e8074ead9204bbc308e`
- Branch: `physics/refactor`
- Measurement date: 2026-08-31
- Physics behavior changes: none
- Optimization claims: none

This document establishes the first repeatable, headless performance baseline
for the current physics implementation. It is the reference measurement for
later optimization phases; it does not authorize or include any later-phase
algorithm, correctness, ownership, or scheduling changes.

## Measurement environment

| Item | Value |
|---|---|
| Operating system | Windows 10 x64, build 19045 |
| Processor | AMD64 Family 25 Model 80, 16 logical processors exposed |
| Compiler | MSVC 19.44.35228 x64 |
| Generator | committed Premake, Visual Studio 2022 action |
| Build | Release x64, optimization enabled; repository's existing `/MTd` override remains |
| Physics step | 1/120 second (`0.00833333377`) |
| Samples | 5 warmups, then 25 measured samples per body count |
| Execution | single process per profiling mode, counts ascending |

The benchmark uses `std::chrono::steady_clock`. Subsystem values below are
arithmetic means over 25 samples. External step values use medians for the
profiling-overhead comparison and also retain minimum/maximum bounds in the raw
CSV output. Host power state and unrelated scheduling were not pinned, so small
between-build differences should not be treated as speedups.

## Repeatable workload

`PhysicsBenchmark` constructs a fresh scene for every sample and times exactly
one `PhysicsSystem::Update`. Construction and destruction are outside the
timed region. A shared production `ShapeBox` is used by every body.

Bodies are divided into isolated overlapping pairs spaced 10 world units apart.
The repeating four-pair pattern is:

1. dynamic/static;
2. dynamic/static;
3. static/static, rejected by pair filtering;
4. dynamic/dynamic.

This keeps 50% of bodies dynamic and active while exercising broadphase,
pair rejection, GJK, support mapping, EPA, contact/manifold creation, constraint
solving, and integration. Sleeping does not exist in the current implementation
and is therefore reported as zero. Gravity is set to zero to keep each sample's
initial state deterministic. The benchmark verifies finite body state after the
step and rejects missing or inconsistent profiling data.

The benchmark is headless. It does not initialize SDL, OpenGL, ImGui, the
renderer, or runtime assets, so the pre-existing font-path startup defect does
not block physics measurement.

## Profiling contract

`PhysicsProfileSnapshot` exposes counters and nanosecond timers. The benchmark
calls `ResetPhysicsProfile` immediately before every measured step and reads a
snapshot immediately afterward.

Production instrumentation is guarded by `GE_ENABLE_PHYSICS_PROFILING`.
Generating projects normally leaves the definition absent, making hot-loop
timers and counter updates compile to no-ops. Passing `--physics-profiling` to
Premake enables the instrumentation explicitly, including per-support timing.

Current physics execution is serial. The profile accumulator is intentionally
non-atomic to minimize measurement disturbance; a later parallel-physics phase
must replace it with per-thread aggregation before using it from worker threads.

Metric semantics:

| Metric | Definition |
|---|---|
| Body/dynamic/active/sleeping | Most recent step's body state counts; active means non-static because sleeping is not implemented. |
| Candidate pairs | Pairs emitted by broadphase before filtering. |
| Pair filter | Checks, static/static rejections, and guarded predicate time. |
| GJK time | Inclusive time for a public GJK query; includes nested support and EPA work where invoked. |
| GJK iterations | Main simplex-loop passes; maximum is the largest call in the sample. |
| Support time | Inclusive time in the Minkowski support helper, including two shape support calls. |
| EPA/contact | EPA calls/time plus successfully generated contact count. |
| Manifold | Expiry and contact insertion time, retained manifold count, and retained contact count. |
| Solver | Manifold pre-solve plus the existing one outer solve pass; constraint count is retained manifold contacts. |
| Contact resolution | Ballistic positive-TOI impulse resolution; zero in this initial-overlap workload. |
| Integration | Body `Update` calls performed by the step. |
| PhysicsWorld total | Inclusive `PhysicsSystem::Update` wall time. |
| External time/FPS | Benchmark wall time around the same update and its reciprocal throughput. |

Inclusive timers overlap by design. GJK, support, and EPA times must not be
summed, and the major subsystem times should not be expected to equal the world
total exactly.

## Commands

Normal compile-out build and benchmark:

```powershell
& .\vendor\bin\premake\premake5.exe vs2022
msbuild .\GEngine.sln /t:PhysicsBenchmark:Rebuild /m /nologo `
  /p:Configuration=Release /p:Platform=x64
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe `
  --body-counts=50,100,200,500,1000,2000 --warmup=5 --samples=25
```

Profiling-enabled build and benchmark:

```powershell
& .\vendor\bin\premake\premake5.exe --physics-profiling vs2022
msbuild .\GEngine.sln /t:PhysicsBenchmark:Rebuild /m /nologo `
  /p:Configuration=Release /p:Platform=x64
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe `
  --body-counts=50,100,200,500,1000,2000 --warmup=5 --samples=25
```

Regenerate with the normal command after profiling so the default solution
again compiles instrumentation out.

## Profile-enabled workload counts

All values are per step and are stable across the 25 samples.

| Bodies | Dynamic | Active | Sleeping | Candidates | Rejected | GJK calls | GJK avg/max iterations | Support calls | EPA calls | Contacts | Manifolds | Solver constraints/passes |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 50 | 25 | 25 | 0 | 25 | 6 | 19 | 3.00 / 3 | 209 | 19 | 19 | 19 | 19 / 1 |
| 100 | 50 | 50 | 0 | 50 | 12 | 38 | 3.00 / 3 | 418 | 38 | 38 | 38 | 38 / 1 |
| 200 | 100 | 100 | 0 | 100 | 25 | 75 | 3.00 / 3 | 825 | 75 | 75 | 75 | 75 / 1 |
| 500 | 250 | 250 | 0 | 250 | 62 | 188 | 3.00 / 3 | 2,068 | 188 | 188 | 188 | 188 / 1 |
| 1,000 | 500 | 500 | 0 | 500 | 125 | 375 | 3.00 / 3 | 4,125 | 375 | 375 | 375 | 375 / 1 |
| 2,000 | 1,000 | 1,000 | 0 | 1,000 | 250 | 750 | 3.00 / 3 | 8,250 | 750 | 750 | 750 | 750 / 1 |

## Profile-enabled subsystem timing baseline

All times are mean milliseconds per physics step. `World` is inclusive.

| Bodies | Gravity | Broadphase | Filter | GJK | Support | EPA | Narrowphase | Manifold | Solver | Integration | World | External median | Median FPS |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 50 | 0.0001 | 0.0086 | 0.0009 | 0.2925 | 0.0366 | 0.2686 | 0.2976 | 0.4105 | 0.4937 | 0.0040 | 1.2175 | 1.2191 | 820.28 |
| 100 | 0.0002 | 0.0171 | 0.0020 | 0.5943 | 0.0749 | 0.5451 | 0.6046 | 0.9068 | 0.9802 | 0.0083 | 2.5232 | 2.5228 | 396.38 |
| 200 | 0.0005 | 0.0353 | 0.0041 | 1.1524 | 0.1490 | 1.0556 | 1.1729 | 1.9320 | 1.9325 | 0.0167 | 5.1018 | 5.0938 | 196.32 |
| 500 | 0.0012 | 0.0850 | 0.0123 | 2.8903 | 0.3742 | 2.6466 | 2.9414 | 4.4673 | 4.8350 | 0.0428 | 12.4035 | 12.4219 | 80.50 |
| 1,000 | 0.0023 | 0.1717 | 0.0341 | 5.6660 | 0.7568 | 5.1723 | 5.7670 | 9.6776 | 9.6942 | 0.0897 | 25.4737 | 25.4657 | 39.27 |
| 2,000 | 0.0052 | 0.3376 | 0.0868 | 11.3089 | 1.5189 | 10.3125 | 11.5109 | 21.4694 | 20.0042 | 0.1890 | 53.6808 | 53.6605 | 18.64 |

Contact-resolution time was `0.0000 ms` at every scale because the workload
starts overlapped and routes contacts through persistent manifolds rather than
the positive-time-of-impact ballistic path.

## Profiling overhead control

The same optimized Release workload was built twice from regenerated projects.
The normal build omitted `GE_ENABLE_PHYSICS_PROFILING`; the enabled build added
only that definition. Both used 5 warmups and 25 samples. The comparison uses
median external wall time.

| Bodies | Compile-out median ms | Profile-enabled median ms | Overhead |
|---:|---:|---:|---:|
| 50 | 1.1788 | 1.2191 | 3.42% |
| 100 | 2.3957 | 2.5228 | 5.31% |
| 200 | 4.8769 | 5.0938 | 4.45% |
| 500 | 11.8962 | 12.4219 | 4.42% |
| 1,000 | 24.5528 | 25.4657 | 3.72% |
| 2,000 | 51.0928 | 53.6605 | 5.03% |

Explicit profiling overhead is bounded at 5.31% in this workload. The default
Release build has the expensive instrumentation compiled out; its benchmark
reported `physics_profiling=disabled` and zero for every internal counter and
timer, while still validating finite body state and external timing.

## Baseline interpretation

The measured workload is intentionally diagnostic rather than a gameplay scene.
It provides linear, deterministic amounts of broadphase candidates, GJK/EPA
calls, contacts, manifolds, and solver constraints at every body scale.

The baseline shows manifold maintenance and constraint solving as the largest
exclusive top-level costs for this contact-heavy fixture. GJK is inclusive and
is dominated by EPA for these overlapping boxes. These observations identify
where later phases should measure; they are not permission to begin those
phases or change their requirements.

## Limitations

- Results apply to the paired-overlapping-box fixture and this host/toolchain.
- Scene setup, body allocation, and teardown are intentionally excluded.
- The persistent transient-contact vector is warmed before measurement, so its
  capacity-growth allocation is not represented in measured samples.
- No sleeping system exists, so sleeping-body performance cannot yet be measured.
- The benchmark does not exercise positive-TOI contact resolution.
- The profiler is serial-only until a later parallel phase adds safe aggregation.
- Existing numerical, ownership, broadphase, solver-allocation, and timestep
  issues remain unchanged and governed by later phases.
