# Rendering validation

## Phase 13 frozen safety/performance baseline

`python tools/rendering_baseline.py build --configuration Release --output logs/rendering/phase13/build`
generates the existing VS2022 projects with the opt-in `--render-baseline` option.
It consistently enables renderer counters and the existing Physics profiler in
all C++ consumers. Compiler, C++20, static CRT, dependencies and target ownership
stay unchanged. Regenerate without the option to restore normal build definitions.

Run `python tools/rendering_baseline.py run --output logs/rendering/phase13/run`
after a successful build. The four applications run sequentially, three fresh
processes each. The fixture uses a disposable CWD/layout, an explicit 4096 shadow
resolution, 1280x720 drawable and confirmed VSYNC=0. It records 120 untimed warm-up
frames followed by 240 samples; a final untimed frame captures the backbuffer and
queries current framebuffer attachment dimensions/formats. All GL remains on the
context thread, and normal application destruction follows collection.

The default initialized scene and camera are retained. `Update` receives zero
seconds in collection mode, so animations/game motion remain frozen while layout
and resource update paths still run. Existing frame pacing remains active and is
included in `frame_ns`; `work_ns` splits into input, update and render elapsed CPU
durations. Render time includes ImGui, swaps, CPU ray tracing and the ray tracer's
existing timer logging where applicable; it is not isolated submission time.
No GPU timer or per-pass timing exists in this baseline. Startup timing begins
before runtime asset validation and excludes executable loading by the OS.

`frames.csv`, `runtime.txt`, `targets.txt`, `diagnostic.bmp`, per-process commands,
exits and binary/DLL/layout hashes retain the evidence. `summary.json` reports
median/p95/p99, per-run medians, and exact non-timing counter signatures. More than
10% spread between run medians marks a timing metric NOISY; no speedup is inferred.
The shared scene region must contain image detail and differ by at most 0.5% of
pixels between repetitions on the same GPU/driver. These images diagnose a frozen
workload; they are not final visual golden references or cross-GPU byte tests.
Do not interact with the fixture windows while sampling.

The editor currently leaves its resolved scene offscreen because its UI render
call is disabled. Its black presented `diagnostic.bmp` is retained as a known
visual defect. The runner additionally sets `GENGINE_BASELINE_SCENE_TARGET=1`
for that application and saves the rendered texture to `scene.bmp`; this scene
image supplies the same nonblank/stability check. Presentation is not repaired.

Counters retain their Phase 06 limitations: issued requests can include failed
draws and repeated binds; ImGui's independent GL loader and texture-upload bytes
are not covered. Buffer bytes estimate observed stores; driver allocation and
total GPU memory remain unknown. Shadow diagnostics distinguish the measured
4096 configuration (768 MiB at an assumed four bytes/texel over twelve images)
from the historical 8192 configuration (3072 MiB by the same estimate), which is
not rerun. Attachment queries provide actual dimensions/formats separately.

The fixture requires zero Physics steps/time in frozen rendering samples.
`python tools/rendering_baseline.py physics --output logs/rendering/phase13/physics`
separately runs the existing deterministic headless benchmark at 50/200/1000
bodies, three warm-ups and ten reset-per-sample 1/120-second steps, repeated in
three processes. These Physics results are never subtracted from render samples.

`python tools/rendering_baseline.py verify --output logs/rendering/phase13/run`
checks retained sample completeness, timing decomposition, settings, workload
counters and diagnostic image stability without rerunning an application.
Common Phase 65 metrics compare against the sealed Phase 13 baseline. Per-pass
metrics introduced later establish their own secondary baseline at Phase 48.

## Validation target overview

`RenderingValidation` is a small C++20 console target in the existing Premake /
VS2022 workspace. It does not link GEngine or an application. Add named CPU
`TestCase` functions to its suite; `Require` and uncaught test exceptions produce
concise failures. The initial suite tests the harness, not renderer correctness.

From any working directory, use the repository's Python entry point:

```powershell
python C:/dev/GEngine-rendering/tools/rendering_validation.py --configuration Debug
python C:/dev/GEngine-rendering/tools/rendering_validation.py --configuration Release
python C:/dev/GEngine-rendering/tools/rendering_validation.py --configuration Debug --gl
python C:/dev/GEngine-rendering/tools/rendering_validation.py --configuration Debug --asan
```

The runner discovers Visual Studio 2022 using `vswhere -version "[17.0,18.0)"`,
rather than selecting a newer major installation. It binds MSBuild's toolset
version to the same selected v143 compiler/runtime directory,
runs the bundled `vendor/bin/premake/premake5.exe vs2022`, and builds only
`RenderingValidation/RenderingValidation.vcxproj`. Normal Debug and Release
retain `/MTd` and `/MT`. `--no-build` reuses a matching binary and checks its ASan
mode. `--output <directory>` selects the command/log/result artifact directory;
the default is `logs/rendering/validation/<configuration>[-asan]/`.

The runner requires the successful CPU self-test, two deliberate assertion /
exception failures, and an invalid-argument failure. Expected negative controls
are recorded with their actual nonzero exits. Driver exits are 0 PASS, 1 failure,
2 invalid command line, and 3 unavailable optional prerequisite/platform.

The native executable supports `--self-test` (also its default), `--gl`, `--gl-debug`, `--counters`, `--gl-counters`,
`--failure-probe`, `--exception-probe`, `--asan-failure-probe`, and `--help`.
Its normal exits use the same 0/1/2/3 contract. A sanitizer abort retains its native
process exit; the Python runner checks both a nonzero exit and the specific ASan
diagnostic. No crash or missing DLL can count as a detected memory error alone.

## Optional hidden GL fixture

CPU runs neither load SDL nor initialize a video driver: SDL2 is delay-loaded,
and the runner deliberately supplies an invalid video driver during CPU checks.
`--gl` additionally requires two hidden 64x64 OpenGL 4.6 core context lifetimes.
The main thread creates, queries and destroys each context. Move-only scope
owners delete context before window, and SDL quits after both cycles. No workers
issue GL calls. Failure to obtain a context is reported as unavailable, never PASS.

The runner supplies the existing tracked `bin/<Configuration>/GEngineEditor/SDL2.dll`
through the child process PATH, without launching the editor or copying DLLs.
The fixture loads just its GL query functions through SDL; it does not initialize
engine-global GL state, allocate shadows, load scene assets, or capture images.
For direct `RenderingValidation.exe --gl`, make that SDL2 DLL directory available
on PATH. CPU invocation does not require it.

## OpenGL debug diagnostics (Phase 05)

After building Debug or Release, run `RenderingValidation.exe --gl-debug` with the
same SDL DLL PATH described above. This focused mode uses the production
`GEngine/Core/GLDebug.h` helpers and the existing glad library in two hidden context
lifetimes; it does not link or start GEngine. Ordinary `--gl` keeps its original
context-only checks. CPU modes still perform no GL/SDL initialization.

Debug requests a Debug context, with one normal-context retry if creation fails.
After GL loading it installs the KHR_debug callback synchronously on the context
thread. Output goes to stderr with source, type, severity, numeric enums, message
ID and driver text. Notification chatter and group-entry/exit messages are
filtered; high, medium and low diagnostic messages remain enabled. No debugger
trap or throwing callback is used. Non-debug fallback contexts are reported and
may provide fewer driver messages; unavailable KHR_debug is explicitly reported.

The fixture checks activation, callback fields/thread, filtering, scoped group
balance (including exception unwind), the unavailable-capability path, and safe
application-inserted high/medium/low markers. Those markers are validation-only
messages, not real GL errors. Release verifies a non-debug context, no installed
callback and no submitted groups. Production Release compiles callback/group
instrumentation out, with no per-frame debug GL queries or logging.

Production group labels cover window initialization/teardown, application render
resource initialization/submission and main ImGui draw data. These groups remain
on their owning context and do not add renderer counters or change scheduling.
ImGui-created secondary viewport contexts remain owned by the existing backend;
this phase installs the callback on engine-owned SDL contexts only.

Reference: [KHR_debug specification](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt).

## Renderer counters (Phase 06)

Run `RenderingValidation.exe --counters` for CPU reset/accumulation/snapshot checks
and `RenderingValidation.exe --gl-counters` with the SDL DLL PATH above for two
hidden context lifetimes. The latter submits a known arrays/indexed/instanced/line/
zero-count sequence, checks a red pixel, repeated binds, null buffer allocation
versus uploads, buffer replacement, readback timing, live names, deletion and
context retirement. Run both configurations: Debug counts, Release stays at zero
while forwarding the same GL work. Neither mode links GEngine.

The launcher also supports `python tools/rendering_validation.py --configuration
Debug --counters` (or `Release`); add `--no-build` to use an already built binary.

`Core/RenderCounters.h` exposes `Current()` and `LastFrame()` value snapshots on
the context-owning thread. `BaseApp::Run` starts a frame before input/update and
finishes it after rendering; resource counts survive frame resets. Set
`GENGINE_RENDER_COUNTERS_LOG=1` to print the last completed frame on normal exit.
Debug enables counters by default. Release compiles them out, including wrappers,
timers, resource maps and logging. An explicit `GENGINE_RENDER_COUNTERS=0` or `1`
compiler definition must be consistent across all consumers and PCHs.

Coverage is the current first-party GL entrypoints reached through `gepch.h`.
The header wraps calls without replacing glad pointers. ImGui's separate GL
loader, other third-party loaders and direct `glad_gl*` calls are outside coverage.
Sampler binds are available and zero when the engine issues none. Binds include
unbinds and repeated requests. Draws count issued calls (including zero counts
and failures); triangles estimate submitted `GL_TRIANGLES` lists including
instances, not strips/restart, geometry-shader amplification or rasterized output.
Shadow/picking counts mark their pre-render routines once per invocation.
Target reallocations mark replacement attempts in framebuffer invalidation paths,
exclude initial creation, and count each entered invalidation routine. The normal
main target/resolve pair uses one routine; the alternate `_Invalidate` path calls
a separate resolve routine and counts that separately.

Uploads count `glBufferSubData` and non-null `glBufferData`, with requested payload
bytes; null `glBufferData` counts only as allocation. Texture uploads and mapped
buffer writes are outside this buffer metric. Readback time is CPU elapsed time
around `glReadPixels`, including any driver wait, not GPU time.

Live counts are observed generated/created names minus delete requests, separated
by resource kind and creating context. They include reserved names and exclude
deferred driver destruction. This is not a leak detector or a shared-context
registry. Context retirement clears its remaining observations. Estimated bytes
cover requested stores for observed array, element, uniform and pixel pack/unpack
buffers only; binding is queried on store allocation, never per draw/bind. Failed
allocations are still requests. Texture/renderbuffer/driver overhead bytes are
unknown. Shared-context cross-owner deletion is outside this early diagnostic
model. No GL errors are consumed and no workers gain GL work.

These counters establish observability only. Phase 13 owns the performance
baseline; no trustworthy baseline or negligible *enabled* timing cost is claimed.

## VertexBuffer upload safety (Phase 08)

The separate focused probe links the production `GEngine.lib` and uses two hidden
OpenGL 4.6 context lifetimes. Run it from the repository root:

```powershell
python tools/test_vertex_buffer.py --configuration Debug
python tools/test_vertex_buffer.py --configuration Release
```

The runner builds the affected GEngine target (including its Mesh consumer) and
glad with the existing VS2022/v143, C++20 and static CRT settings, then compiles
`tools/vertex_buffer_probe.cpp` against that library. Commands, actual exits,
binary hashes and GPU diagnostics are saved under `logs/rendering/phase08/`.
It does not launch an application or run the unrelated CPU harness suite.

`VertexBuffer(std::span<const float>)` derives allocation bytes from the payload;
the former payload-plus-size constructor is removed. The allocation-only
constructor takes `capacityBytes` and rejects values beyond `GLsizeiptr` with
`std::length_error`. `SetData(span, offsetBytes = 0)` rejects out-of-capacity
ranges with `std::out_of_range` before GL calls, using subtraction/division to
avoid overflow. Empty spans are no-ops at offsets from zero through capacity;
larger offsets are rejected. A zero-capacity buffer accepts only an empty upload
at zero. All operations still require the owning GL context thread.

Tests read back full GPU contents after construction, full/partial updates,
nonzero byte offsets and exact-end writes, including untouched neighbors. They
cover empty and zero-capacity buffers, one-byte overflow, oversized payloads,
maximum offsets, signed GL capacity limits, and an unaligned byte offset. Driver
call observers verify empty/rejected operations issue no buffer calls in both
configurations. A synchronous KHR_debug callback with a health marker plus
`glGetError` checks uploads and destruction; Release diagnostics are test-only.
Missing GL prerequisites fail this mandatory phase check. No sanitizer or
performance result is implied.

## Input and control-flow safety (Phase 09)

```powershell
python tools/test_input_control.py --configuration Debug
python tools/test_input_control.py --configuration Release
```

This separate probe links the production GEngine library. The runner builds all
four graphical consumers with the existing VS2022/v143, C++20 and static CRT
settings, enabling `/we4715 /we4716` for return-path diagnostics on that invocation.
Logs, actual exits and binary hashes go under `logs/rendering/phase09/`.

The `--state` native mode uses SDL's dummy video driver. It checks default input
construction over nonzero storage, all 5,120 combinations of previous/current
mouse masks and the five supported buttons, invalid input codes, neutral
disconnected-controller state, and transient resets across untouched frames.
Mouse position and button history survive a frame reset; motion and wheel deltas
reset before events. Keyboard queries before initialization return `None`.

The native `--loop-input`, `--loop-resume`, `--loop-close` and `--loop-minimize`
modes initialize the real BaseApp/SDL/ImGui stack with a hidden window and a
32-pixel fixture shadow allocation. They verify polling before input sampling,
single-frame motion, active ImGui/window return values, suspended close/restore,
and skipping update/render immediately when an event minimizes the application.
A worker only queues SDL events; it never calls GL or accesses application state.
The fixture verifies that a 900 ms suspension is excluded from the resumed
simulation timestep (500 ms scheduling tolerance, not a performance benchmark).
Application overrides count update/render calls so the test covers the shared
loop without simulating a scene. Disposable runtime directories contain ImGui
settings writes. These tests do not certify framebuffer output or physics.

Input state members have explicit defaults, all mouse mask tests use nonzero
membership, and each frame resets relative motion/wheel deltas before polling
events and sampling SDL state. While minimized, the loop continues processing
events and retiring input with a 16 ms idle delay, skipping application controls,
simulation and rendering. Restore starts timing from the current counter. The
existing normal frame pacing and scene fixed-step scheduler remain in place.

## AddressSanitizer and ThreadSanitizer

`--asan` uses MSBuild `/p:EnableASAN=true` for this target only, in Debug or Release,
with distinct `bin/<Configuration>-asan/RenderingValidation` and `bin-int` outputs.
Runtime checks, incremental linking and Edit-and-Continue are disabled only in
this small target. C++20, the static CRT policy and all existing targets stay
unchanged. These incompatible options and static-CRT support are documented in
[Microsoft's ASan limitations](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-known-issues?view=msvc-170)
and [build reference](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-building?view=msvc-170).

The runner adds the installed compiler's matching ASan runtime directory to the
child PATH. It requires a clean instrumented CPU run and a separate intentional
heap-buffer-overflow to prove the runtime detects errors; the latter is compiled
only with `__SANITIZE_ADDRESS__`. It clears inherited ASan debugger/dump/options
settings so the negative control cannot open an IDE or silently continue. These
controls do not sanitize GEngine, third-party binaries or the GPU driver.

MSVC on the supported Windows x64 toolchain does not provide ThreadSanitizer;
Microsoft lists `/fsanitize=thread` as a future sanitizer, not supported tooling.
[Microsoft ASan overview](https://learn.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-170).
No TSan PASS is claimed and no alternate compiler/platform is introduced. Other
platforms are explicitly unavailable through this runner until separately
validated. Sanitizers complement focused tests; they do not establish rendering
quality, GPU correctness or absence of races in uninstrumented code.
# Phase 10: entity API regression

Run `python tools/test_entity_api.py --configuration Debug` and repeat with
`--configuration Release`. The runner builds GEngine and its six executable
consumers using the existing VS2022/v143, C++20 and static CRT configuration, then
links `tools/entity_api_probe.cpp` and the existing program-grouping regression to
the production library. Commands, exits, toolchain and binary hashes are recorded
in `logs/rendering/phase10/<Configuration>/results.json`.

The CPU fixture covers null/invalid/cross-scene/self/cyclic parents, reciprocal
reparent/detach and UUID setters, non-owning wrapper lifetimes, lvalue/rvalue/UUID
destruction, generation and UUID reuse, recursive destruction and surviving
children, duplicate/scene-copy relationships, and const transform/component/name/
children access. Existing program-grouping checks cover render-list cleanup.
There are no GL calls, graphical smokes, physics workload changes or performance
claims in this fixture.

`_Entity` borrows a live scene; handles must not outlive that scene. `SetParent`
throws `std::invalid_argument` for invalid inputs before changing links; a null
handle or UUID zero detaches. Relationships are created lazily. Mutable component
and `Children()` access remain low-level escape hatches whose callers must keep
links consistent. Const component and child/name references borrow registry storage
and must not be retained across structural mutation; const `Transform()` returns
an independent matrix value from `Transform3DComponent`. Explicit scene destruction
includes descendants by default; `excludeChildren=true` detaches surviving children.
The legacy `first` argument remains accepted, and all calls clean reciprocal links.
Single-entity duplicates become childless siblings of their source.

## Phase 11: light component dispatch

Run `python tools/test_light_components.py --configuration Debug` and repeat with
`--configuration Release`. The runner builds GEngine, GEngineEditor (legacy scene
consumer) and RigidBodySimulation (ECS consumer), then links the production library
to `tools/light_components_probe.cpp` using the existing VS2022/v143, C++20 and
static CRT policy. Commands, actual exits, toolchain and binary hashes are saved
in `logs/rendering/phase11/<Configuration>/results.json`.

Two hidden OpenGL 4.6 context lifetimes exercise `SceneRender` and
`CascadedShadowSceneRender` with every component-presence combination, individual
and mixed light entities, and directional/point/spot priority on one entity.
Distinct uniform values and untouched sentinels are read from the real driver to
detect wrong-type uploads. Removed components with retained light-list membership,
re-added components, destroyed entities and unpublished lights are covered.
Point-light visualization excludes spot/directional uploads. The legacy
`Renderer::RenderScene` / `Scene::UploadLightsUniform` / `LightEntity` path is checked
with empty, single and multiple light scenes, including position/color/attenuation.

There is no ECS light enable flag in the current API. Disabled submission coverage
uses absent components and unpublished entities; zero-intensity coverage verifies
that directional/point ambient and legacy color overwrite preceding nonzero
values. Skipping an upload does not clear persistent shader uniforms. Existing
spot uploads contain direction only; this phase does not expand the lighting
equations, uniform schema or introduce an enable API. Actor visibility is not a
light-contribution switch. RenderFrame light extraction remains Phase 41.

The fixture verifies uniform storage and GL errors using real empty VAOs, without
claiming pixel-image or shadow-quality validation. All GL work, uniform readback
and destruction occur on the context thread; diagnostics and teardown are checked
in both configurations. This is focused GL integration coverage, not an application
lifecycle suite, benchmark or sanitizer run.

## Phase 12: ray tracer concurrency and lifetime

Run each configuration with the existing VS2022/v143, C++20 and static CRT policy:

```powershell
python tools/test_ray_tracing.py --configuration Debug --smoke
python tools/test_ray_tracing.py --configuration Release --smoke
python tools/test_ray_tracing.py --configuration Debug --asan
python tools/test_ray_tracing.py --configuration Release --asan
```

The normal runner builds GEngine and RayTracing and links a production-library
probe. `--smoke` adds the existing real RayTracing startup/close fixture, with
disposable ImGui settings. Commands, exits, toolchain, binary hashes, full logs and
checksums live under `logs/rendering/phase12/<Configuration>[-asan]/` (or `--output`).

`SimpleRenderer` owns its RGBA bytes and floating-point accumulation in vectors and
is move-only. Render/resize/settings changes/destruction run on the context thread;
scene and camera inputs must remain unchanged until synchronous `Render` returns.
TBB tasks own disjoint pixels and local color/RNG state, then join before the single
texture upload. Worker count 1 runs the same pixel function serially; larger values
limit a local TBB arena. Nonpositive worker counts use the serial path.

RNG streams use a fixed SplitMix64 integer transform keyed by `Settings::Seed`,
linear pixel index and one-based accumulation sample. An explicit 24-bit mapping
produces each roughness coordinate in `[-0.5, 0.5)`, in x/y/z order. Reset restarts
sample 1; disabling accumulation renders sample 1 immediately. Repeatability is
defined for identical scene/camera/settings/dimensions and the same build/platform,
independent of worker count/scheduling. It does not promise byte identity across
compilers, floating-point configurations or platforms. Call `ResetFrameIndex` after
changing scene/camera/bounce/seed inputs during an accumulated sequence.

Resize replaces both CPU buffers and restarts accumulation; unchanged dimensions
preserve pending allocation and existing samples. Zero dimensions suspend rendering
and release renderer storage. Camera zero-size handling skips projection work.
Render rejects mismatched camera dimensions, and uploads explicitly bind the final
texture after workers finish. Dormant raw-thread, alternate double-render and
in-place neighbor-filter branches have been removed.

The hidden GL fixture compares complete RGBA output and checksums across three
repeats at 1/2/3/8 workers and four accumulated samples. It checks seed changes,
reset/accumulation-off, nonuniform hit/miss output, small/odd/zero/restored sizes,
repeated pending resize, untouched unrelated textures, camera mismatch and oversized
resize rejection, move ownership and repeated renderer/context destruction.
Texture-call observers reject worker GL calls. Debug CRT heap checkpoints after
warm-up require zero retained normal blocks/bytes across 24 renderer lifetimes.

`--asan` builds an isolated **instrumented GEngine library and probe**, retaining
MSVC vector/string annotations. A generated local MSBuild import selects `/Zi`
instead of Edit-and-Continue and disables incompatible runtime checks only for
that sanitizer build. Normal projects/outputs and CRT policy are unchanged.
The same workload runs under ASan; a separate deliberate heap overflow must be
detected. Third-party DLLs and the GPU driver remain uninstrumented. Windows MSVC
does not provide ThreadSanitizer or LeakSanitizer in this toolchain; ASan covers
memory errors, the Debug CRT check covers storage reclamation, and deterministic
worker comparisons plus source ownership inspection cover the accumulator race.
These checks make no performance or visual-quality claim.

## Explicit frame clock (Phase 14)

`BaseApp` measures frames with `std::chrono::steady_clock`. `GetFrameTime()` exposes
typed `Seconds` durations: `rawDelta` includes actual pacing and stalls;
`clampedDelta` caps that sample at 250 ms; `renderDelta` currently equals the clamped
duration. Input receives this presentation duration in seconds. `Update` still
receives the raw duration so the existing scene scheduler retains all overload
accounting. The opt-in frozen baseline still deliberately passes zero to `Update`.
The scene's typed `PhysicsStep` is 1/60 second; its legacy numeric boundary, two-step
limit and backlog policy are unchanged. No interpolation or new scheduler is added.

`Timestep` stores a chrono duration and keeps the existing numeric-seconds API at
consumer boundaries. `Timer` uses a steady clock and returns seconds from
`ElapsedSeconds`/`Elapsed`, milliseconds from `ElapsedMilliSeconds`, or a typed
duration from `ElapsedDuration`.

```powershell
python tools/test_input_control.py --configuration Debug --frame-clock --output logs/rendering/phase14/Debug
python tools/test_input_control.py --configuration Release --frame-clock --output logs/rendering/phase14/Release
```

This builds all four graphical applications and both Physics consumers with the
existing toolchain. Injected clock samples cover fractional duration conversions,
short/long/equal/backwards samples, monotonic anchors, elapsed-time conservation and
reset after suspension. The real hidden application loop checks measured pacing,
a 350 ms render stall, capped input versus raw update time, and existing input,
close, minimize and restore behavior. `PhysicsTests --fixed-scheduling` checks the
existing scheduler and direct-step equivalence. Real-clock checks use lower bounds
and suspension tolerance, not exact sleep timing. No performance claim is made.

## Event and minimized loop (Phase 15)

`BaseApp::Run` owns event polling and SDL input sampling before virtual application
controls. Native minimized, hidden, and zero-size states are independent and apply
only to the main window. SDL minimize/restore/maximize, hide/show, resize/size-change,
close and quit events remain live while suspended. Suspended iterations retain the
existing 16 ms idle delay and reset the frame clock; controls, update and render are
skipped. A resize or a foreign-window event cannot clear another suspension reason.

Docked viewport visibility is separate from native window suspension. Collapsed or
empty scene viewports skip scene passes while their UI continues running, so they
can reopen. Simulation visibility takes effect on the next scene frame; the ray
tracer uses the current UI frame's visibility/extent before generating an image.
The Editor's previously disabled UI presentation call remains outside this phase.

```powershell
python tools/test_input_control.py --configuration Debug --event-loop --output logs/rendering/phase15/Debug
python tools/test_input_control.py --configuration Release --event-loop --output logs/rendering/phase15/Release
```

The runner builds all four graphical consumers and retains the clock and input
regressions. Native-state tests cover minimize/restore, minimize/close, hidden/quit,
hide/show, zero-size/positive-size restore and unrelated window events. The fixture
keeps its native GL window hidden and injects actual SDL window-event types through
the production event manager. Native startup-hidden state is read from SDL flags.
The control override deliberately omits the base call, proving events cannot be
starved by a virtual override. Idle CPU observations exclude initialization, sample
900 ms of suspension, and require less than 25% of one logical CPU (all process
threads counted) plus zero suspended application work and bounded event response.
These observations detect a busy loop; they are not rendering performance metrics.
Reported zero CPU samples are limited by the OS counter's resolution.

Two additional probe builds include the actual simulation and ray application
implementations with renamed entry points. Real ImGui collapse/reopen cycles verify
that scene draws/image uploads stop while hidden and resume when reopened. GL hooks
in the fixture count uploads and forward every call on the context-owning thread.
Logs, native exits and executable hashes are recorded under each configuration.

## Fixed cadence and frame pacing (Phase 16)

The scene remains the sole elapsed-time accumulator. Simulation runs at 60 Hz,
with one `PhysicsSystem::Update(1/60 s)` per fixed tick and at most two ticks per
application update. Solver iterations and collision time-of-impact subdivisions
are internal to that tick, not another time accumulator or a higher fixed rate.
The existing solver equations, iteration settings and body sleep policy are retained.
Previously, application frames were limited by an unconditional 16 ms wait
(at most 62.5 updates/second before work and VSYNC). Physics was already 60 Hz.
Application controls, animation/UI and rendering now remain variable rate; the
application forwards raw elapsed time once per visible frame to the scene.
There is no second application accumulator and no presentation interpolation yet.

The existing bounded-backlog policy is deliberate: retain fractional and whole
pending ticks up to 250 ms **before** executing the current update's maximum of two
ticks. Account for every excess positive second in `discardedSeconds` and the
lifetime total. Subsequent positive updates drain retained backlog. Sustained rates
below 30 application frames/second cannot keep up with 60 physics ticks/second;
they keep the same work limit and report overflow. Native suspension adds no new
elapsed time; previously pending physics time survives minimize/restore. Runtime
stop/restart resets the scene clock. Invalid/nonpositive samples do not drain it.

Window initialization explicitly requests swap interval 1 or 0. Failure logs the
request error and actual interval. Before each active frame, `BaseApp` makes the
main context current and queries its actual interval on the owning thread:

| Actual swap interval | Manual cap | Pacing |
| --- | --- | --- |
| Nonzero (including adaptive -1) | Any | Swap/VSYNC owns pacing; no manual wait |
| 0 | Positive | Wait until the previous measured frame start plus `1/cap` seconds |
| 0 | 0 | Uncapped; no manual wait |

`SetManualFrameRateLimit(fps)` selects the fallback cap; the default is 60 FPS.
Work, stalls and oversleep count toward the next interval. A missed deadline adds
no pacing debt. Cap/interval changes apply on the next active iteration. Suspended
event polling retains its independent 16 ms idle delay. A lower cap is intentionally
ignored while VSYNC is active; combining two independent pacers is unsupported.

```powershell
python tools/test_input_control.py --configuration Debug --frame-pacing --output logs/rendering/phase16/Debug
python tools/test_input_control.py --configuration Release --frame-pacing --output logs/rendering/phase16/Release
```

The runner builds all six application/Physics consumers with the recorded toolchain.
Production-scene samples at 30/60/75/120/144/240 Hz must each produce 600 ticks in
ten seconds without loss. Existing `PhysicsTests --fixed-scheduling` adds exact
body-state equivalence against direct fixed stepping, fractional remainder,
invalid inputs, pause/restart, stalls and sustained overload. Hidden SDL/GL loop
checks cover VSYNC on/off crossed with capped/uncapped settings, runtime changes,
one render/swap per active update, a 350 ms stall, and a 900 ms minimize/restore
cycle that retains the prior backlog without adding suspended time. Existing
clock/input/close regressions also run. All GL and scene operations stay on main;
the event worker only enqueues SDL events.

Live pacing checks use a deliberately slow 2 FPS cap to distinguish manual waits
from VSYNC/uncapped operation with a 400 ms upper tolerance. They verify actual
SDL interval selection and application pacing, not monitor scanout or exact OS
sleep precision. Refresh-rate sweeps inject elapsed samples rather than changing
the monitor. No performance benchmark, new sanitizer run or visual-quality claim
is required by this phase.
