# PhysicsBenchmark

`PhysicsBenchmark` is the Phase 01 headless, deterministic physics benchmark.
It does not initialize SDL, OpenGL, ImGui, rendering, or runtime assets.

The default workload creates isolated overlapping box pairs. Its repeating mix
is two dynamic/static pairs, one static/static pair, and one dynamic/dynamic
pair, keeping 50% of bodies dynamic while exercising both pair rejection and
surviving collision work. Every surviving pair starts overlapped for one
1/120-second physics step, exercising broadphase, GJK support mapping, EPA
contact generation, manifold creation, constraint solving, and integration.
Scene construction is outside the timed region, and each sample recreates the
same initial state.

Generate and build the normal Release configuration:

```powershell
& .\vendor\bin\premake\premake5.exe vs2022
msbuild .\GEngine.sln /m /nologo /p:Configuration=Release /p:Platform=x64
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe
```

Generate and build with permanent physics profiling enabled:

```powershell
& .\vendor\bin\premake\premake5.exe --physics-profiling vs2022
msbuild .\GEngine.sln /t:GEngine,PhysicsBenchmark /m /nologo /p:Configuration=Release /p:Platform=x64
.\bin\Release\PhysicsBenchmark\PhysicsBenchmark.exe
```

Supported arguments:

```text
--body-counts=50,100,200,500,1000,2000
--warmup=2
--samples=5
--dt=0.008333333
```

Without `--physics-profiling`, all internal profiling macros compile to no-ops
and the CSV reports external wall time only. Generate the normal project files
again after a profiling run to restore the default build definition.

Subsystem times and counters are arithmetic means over the requested samples.
External wall time also reports median, minimum, and maximum values; use the
median when comparing profiling-enabled and compile-out builds to reduce the
effect of unrelated host scheduling outliers.
