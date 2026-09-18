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

## Simulation-to-render interpolation (Phase 17)

`_Scene::GetRenderTransform(entity)` is the ECS model-matrix source for scene,
cascaded/point shadow, picking, skybox and point-light mesh passes. It returns a
value containing the sampled matrix, its presentation `revision`, and the scene's
fixed-update count as `simulationRevision`. Use the live scene/entity generation
together with the presentation revision for render caches; the physics count alone
does not detect movement on frames without a tick. Repeated unchanged samples keep
the same presentation revision. Sampling changes only a runtime presentation cache.

Registered physics bodies retain previous/current poses at **each** 1/60-second
physics boundary, including both ticks of a catch-up update. Position uses linear
interpolation; rotation uses normalized shortest-arc quaternion slerp. The factor
is `clamp(pendingSeconds / PhysicsStepSeconds, 0, 1)`. Whole-tick backlog therefore
shows current state without extrapolation or modulo wrap. Ordinary interpolation
adds one fixed tick of presentation latency. `SetRenderInterpolationEnabled(false)`
shows current authoritative state for comparison; it does not change scheduling,
contacts, velocities or body sleep. Re-enabling samples the retained pair.

The existing ECS transform contract is **world space**, including parented bodies.
`RelationshipComponent` organizes ownership/editor traversal, not transform
composition. Parent rotation/non-uniform scale is not multiplied into a child's
model matrix or collision body. Each matrix is built directly as world translation
times interpolated rotation times the entity's authored scale. Scale is not a
physics-integrated channel: scale edits snap history, preserving non-uniform and
negative scale without decomposing matrices or introducing shear.

| Event | Presentation behavior |
| --- | --- |
| New entity / newly started physics body | Current pose immediately; initialize both history endpoints equally |
| Author transform or Physics pose teleport | Snap immediately; normal `Update` accepts/rejects author edits and resets history without extra ticks |
| Scale edit / reparent / detach | Snap to current world pose; subsequent ticks establish a fresh pair |
| Pause / resume | Snap endpoints to current state; preserve pending time and avoid a tickless rewind |
| Native minimize / restore | No Update/sample while suspended; retain existing pair/backlog and exclude suspended wall time |
| Destruction / entity-generation reuse | Destroy runtime cache with entity; reject stale/foreign handles; new entity gets a fresh revision |
| Runtime stop/restart / scene copy or entity duplicate | No inherited runtime interpolation history |

Call `ResetRenderInterpolation` for an explicitly signaled discontinuity whose
intermediate values are not observed by the normal update boundary. Raw relationship
or transform edits are also detected when sampled/updated, but supported parenting
uses `_Entity::SetParent`. Authoring and collision state remain authoritative;
presentation never writes sampled transforms into those components or bodies.

All current ECS model passes sample the same state on the context thread. There
is no render-bounds/visibility cache yet; future caches must use the presentation
matrix/revision, or conservatively bound the full previous/current motion interval.
Existing debug AABB/KD overlays deliberately visualize authoritative Physics data
and are not render-culling bounds. Light uniform positions remain independently
authored light-component data; this phase does not add a body-to-light binding.

```powershell
python tools/test_interpolation.py --configuration Debug --output logs/rendering/phase17/Debug
python tools/test_interpolation.py --configuration Release --output logs/rendering/phase17/Release
```

The runner builds all six consumers with the existing toolchain and checks CPU
endpoints/near-one alpha, tickless revisions, 30/60/75/120/144/240 Hz smoothing,
exact enabled/disabled kinematic and dynamic-contact states, teleports, rotation
across 180 degrees, non-uniform scale, parenting, pause/restart, destruction/reuse
and backlog clamp/drain. Existing Physics fixed-scheduling and runtime-transform
regressions also run. Two hidden GL context lifetimes verify submitted model
matrices across passes and read actual triangle pixels moving between physics
ticks. A production BaseApp fixture injects a 350 ms stall and 900 ms native
suspension, then checks retained presentation state and bounded catch-up on restore.
GL creation, submission, readback and destruction stay on the main thread; the
suspension worker only enqueues SDL events. These checks do not claim monitor
scanout timing, full-scene visual quality or rendering performance.

## Phase 19: application-owned EngineContext

`BaseApp` declares a value-owned, noncopyable/nonmovable `EngineContext` before
its other members. The root is constructed before derived application resources
and destroyed after them, the scene and renderer targets. Its legacy `GEngine`
value owns the existing window, input and event managers through unique pointers.
Shared asset/shader/shape caches retire before ImGui, GL, windows, TTF and SDL.
`EntryPoint` only owns the application; manual platform release is no longer needed
or public. Embedders destroy the application on the context's owner thread.

Initialization order is logging -> SDL -> windows/GL/ImGui -> input -> events ->
shapes -> TTF -> ready rendering services -> application scene/targets. A startup
exception immediately releases partial services and leaves an inert root until
destruction. Create a new application to retry. Root initialization may be attempted
once; rendering before readiness and concurrent live application roots are rejected.
The compatibility lookup is owner-thread-only, not a concurrent service registry.

`BaseApp::GetEngineContext()` exposes `MainWindow`, `MakeCurrent` and a `RenderScene`
facade used by `BaseApp::Render`. `BaseApp::GetEngine()` and `GEngine::Get()` resolve
the same borrowed legacy engine while the root lives and throw without a root.
Manager getters return null outside its lifetime. Existing manager caches and
static renderer calls remain compatibility implementations for Phase 20. No physics,
audio, scheduler or dependency migration is included.

```powershell
python tools/test_shutdown.py --configuration Debug --output logs/rendering/phase19/final/Debug
python tools/test_shutdown.py --configuration Release --output logs/rendering/phase19/final/Release
```

The existing eleven-mode lifecycle fixture additionally checks uninitialized root
construction/destruction, compatibility identity, a rejected second application,
pre-initialization rendering, repeated initialization, wrong-thread rendering,
ready-service ordering and real pixel output through the root's renderer facade.
Application-unwind mode also injects a derived constructor failure. SDL, window,
empty-window and ImGui failures check immediate rollback while the root still lives;
the SDL-backend allocation fault now occurs in a second window after the first has
entered the owned manager. Three successful application lifetimes per main mode
verify automatic full platform cleanup and resource deletion order on the owner
context/thread. Input/interpolation probes use the same scope-owned teardown.
These checks retain the Phase 18 diagnostic boundary for pre-existing multisample
sampler errors; they do not certify every legacy resource wrapper or rendering path.

## Phase 18: deterministic shutdown and GPU ownership

Historical Phase 18 introduced the following shutdown order. Phase 19's application
root now performs it automatically. Close/quit requests stop the application loop:

1. Destroy derived application resources and scene borrowers.
2. Destroy BaseApp renderer targets and its uniform buffer.
3. Clear shared asset, shader and geometry caches.
4. For each window, destroy its ImGui OpenGL backend, SDL backend and ImGui context
   while that window's GL context is current.
5. Delete the GL context and native window, then close controllers, TTF and SDL.

Window owners clean themselves up, including failed initialization before they
enter the manager. Removing a missing window and releasing an already released
platform are safe. ImGui cleanup uses the owned context and only shuts down
backends whose state exists. Closing a secondary window restores the surviving
current GL/ImGui contexts. Scene/renderer cleanup and nonempty GPU-owner destruction
must run on the application thread with the owning GL context current. Failure to
reacquire a window's owning context is fatal; cleanup never intentionally proceeds
against a foreign context. The existing single-live-application contract remains.

`FinalFrameBuffer` owns one framebuffer and all three attachment textures.
`UniformBufferObject<Type>` owns its GL buffer for every supported uniform type.
Both reject copying and provide `noexcept` move construction and assignment.
Moves transfer handles and metadata without reallocating GPU storage or changing
its contents. Assignment retires the destination's previous resources immediately;
self-move is a no-op, and moving an empty owner into a live owner releases the old
resources. Moved-from objects hold zero GL names and make no GL deletion calls when
destroyed. Moves do not transfer permission to use or destroy resources on another
thread/context. Framebuffer formats, allocation sizes, drawing and resize algorithms
retain their existing behavior.

`AssetsManager::m_TextureMap` owns the textures allocated by `GetTexture` and
`GetTextTexture`. Cleanup deletes each nonzero GL name before deleting its wrapper
and clearing the map. `m_FrameBufferTextures` contains borrowed shadow framebuffer
names and deletes wrappers only. These caches must be cleaned on the owning context
thread; repeated empty-cache cleanup issues no further texture deletions.

```powershell
python tools/test_shutdown.py --configuration Debug --output logs/rendering/phase18/final/Debug
python tools/test_shutdown.py --configuration Release --output logs/rendering/phase18/final/Release-recovered
```

The runner builds GEngineEditor, Breakout, RayTracing, RigidBodySimulation,
PhysicsTests and PhysicsBenchmark using the existing VS2022/v143, C++20, static-CRT
and Premake configuration. It then links a fixture against the production GEngine
library and runs eleven scenarios. The three full application modes each have a
180-second bound for three Debug initialization cycles; other modes have 60 seconds.
Each command, timeout, exit and required success marker is recorded in `results.json`.

| Scenario | Coverage |
| --- | --- |
| `--lifetimes` | Three application/platform cycles; exactly-once deletion and scene/renderer/cache ordering |
| `--minimized` | Three close cycles while native minimize is requested and minimized state is queued; no application update while suspended |
| `--application-failure` | Three exception-unwind cycles with initialized application, scene, renderer and cache resources |
| `--imgui-context-failure` | Font-path exception after ImGui context creation but before backend initialization |
| `--imgui-platform-failure` | Injected allocation failure after SDL backend state exists, before the GL backend owns resources |
| `--sdl-failure`, `--window-failure`, `--empty-windows` | Invalid SDL driver, dummy driver without OpenGL windows, and rejected empty window list; partial and repeated platform cleanup |
| `--window-owners` | Independent window contexts, secondary removal/restoration, repeated shutdown/removal and ImGui font texture retirement before context deletion |
| `--resource-moves` | Compile-time non-copyability/nothrow-move checks; all six UBO types; framebuffer attachments and pixel contents; live replacement, self-move, reuse of empty owners and exactly-once GL deletion |
| `--cache-ownership` | Two cache lifetimes with real image/text textures and live cascade/point framebuffer borrowers; owned names deleted once, borrowed names preserved through repeated cleanup/repopulation, then deleted by their actual owners |

Deletion observers verify the current context, application thread, backend/platform
liveness and ordering. The probe installs synchronous GL debug reporting in both
configurations for teardown and checks GL errors before deleting the context.
First-party GL deletion calls are observed through GLAD; ImGui uses its independent
loader and is additionally checked through actual font texture retirement and GL
diagnostics. The event worker only queues SDL events.

These are focused lifecycle checks, not a full GPU-leak audit or four-application
visual/performance benchmark. The earlier multisample sampler-state diagnostics
still occur during BaseApp initialization and belong to Phase 28; the fixture
separates them from the zero-error teardown gate. Earlier development and candidate
runs remain under `logs/rendering/phase18/development/`, `verified/`, `diagnostic/`
and `revision2/`. A directory name alone does not imply PASS: consult `results.json`
and the current Phase 18 review. Final phase validation requires all selected checks
to pass in both configurations; a surviving-resource assertion blocks sealing.

### Current Phase 18 revision results

The authorized nine-file candidate passes all selected Debug/Release gates. Each
configuration builds all six consumers with 0 warnings and 0 errors, compiles the
production-library probe, and completes every scenario with exit 0.

| Scenario | Checks per configuration | Result in Debug and Release |
| --- | --- | --- |
| Normal lifetime / minimized close / application unwind | 472 / 475 / 469; three full cycles per mode | PASS |
| ImGui context failure / SDL-backend partial failure | 5 / 6 | PASS |
| SDL failure / window failure / empty window list | 10 each | PASS |
| Window ownership | 9 | PASS |
| Resource moves | 238 | PASS |
| Owned/borrowed texture cache | 101; two cache lifetimes | PASS |

Final evidence is `logs/rendering/phase18/final/Debug/results.json` and
`logs/rendering/phase18/final/Release-recovered/results.json`. All 78 behavior and
protected-input fingerprints still match `final-inputs.json`. Debug completed
before the interruption and is reused on that identity basis. The interrupted
`final/Release/` contains only partial build evidence and is not counted as passed.
The resumed attempt under `final/Release-resumed/` failed on a zero-byte generated
`imgui.obj` (LNK1136). That object and its metadata were preserved under
`final/recovery/`; the unchanged toolchain regenerated it, and the recovered Release
run passed all gates. Source, assertions and the existing eight-file implementation
were unchanged during this recovery.

Historical failures remain in their original directories and the prior review
iterations. They include the original missing UBO/framebuffer resources, the later
cached-texture leak, and a standalone fixture TTF initialization mistake. No failure
was waived or relabeled. The current review records the final seal-dependent human
review status; validation completion is not phase approval.

## Phase 20 manager ownership

EngineContext owns asset, shader and shape manager instances and their caches.
Static lookup APIs are borrowed compatibility adapters: they require a live,
initialized root on its owning thread and activate the main context before cache
access. Only the root constructs or destroys managers. Shutdown remains derived
resources/scene/renderer -> asset/shader/shape managers -> ImGui/GL -> SDL/TTF.
The root exposes Uninitialized/Initializing/Ready/Releasing/Stopped/Failed state;
failed initialization rolls back immediately and cannot be retried on that root.

Global cache Free APIs are removed. Duplicate geometry registration consumes the
new candidate while retaining the original. UnRegister removes the lookup and
keeps old borrowers alive until root destruction; a new registration can then
publish another geometry under that name. Aliasing an already owned geometry into
a different key is rejected. Manager instances cannot be copied or moved.
Image lookup/insertion uses one normalized path; ordinary missing images retain
the checkerboard fallback. Text variants have separate stable entries. Framebuffer
requests return root-owned wrapper snapshots that borrow names without deleting
them or treating names as identities; callers must keep their framebuffer alive
while using its wrapper. Cache pointers remain borrowed, not typed generation
handles. Internal legacy model loader allocation/format handling is unchanged.

Shader compilation/link and font/text load failures throw without publishing a
cache entry. Pending shader objects/programs, opened fonts and SDL surfaces are
retired during unwind. Successful cached resources survive unrelated load failures.
Shader Validate remains the existing state-dependent diagnostic; Phase 20 does not
change the earlier sampler-state boundary.

```powershell
python tools/test_shutdown.py --configuration Debug --output logs/rendering/phase20/final/Debug
python tools/test_shutdown.py --configuration Release --output logs/rendering/phase20/final/Release
python tools/test_shutdown.py --configuration Debug --smoke --output logs/rendering/phase20/smoke/Debug
python tools/test_shutdown.py --configuration Release --smoke --output logs/rendering/phase20/smoke/Release
```

The twelve focused modes retain earlier shutdown/root checks. The adapted
`--cache-ownership` mode runs two actual root lifetimes and checks image aliases,
text variants, multiple framebuffer borrowers and exactly-once texture retirement.
`--manager-failures` checks duplicate registration/retirement, missing model/font,
font retry, unsupported text size, failed SDL surface, missing HDR plus fallback
retry, missing/malformed/link-failed shaders, injected program/shader allocation
failure, successful retry, existing borrower survival and derived constructor
unwind. GL allocation/deletion observers check shader object/program reclamation
and owning context/thread. Expected compiler errors are separated from teardown
diagnostics. Ready-root worker rejection covers all three manager adapters.

`--smoke` runs only the four real graphical startup/close checks against already
built binaries, in isolated runtime directories. It does not rebuild. Results,
commands, actual exits and limitations belong to the current Phase 20 review and
the selected output directory, not to this usage description. These checks do not
constitute a broad GPU-leak, performance, sanitizer or visual-comparison claim.

## Phase 21 context-thread contract

EngineContext retains its construction thread. SDLWindow now also retains its
construction thread and checks initialization, context activation/detachment,
presentation and teardown. `Core/GLContextThread.h` registers each SDL GL context
with its creation thread. All concurrently live registered contexts belong to
that same thread; the registration is removed when the context is deleted.
Sequential platform lifetimes start with fresh registrations.

Permitted transitions are owner-thread creation, switching between live contexts
(including ImGui platform viewports), temporary detachment, owner-thread
reattachment and deletion after GPU/ImGui owners have retired. SDL validates
drawable compatibility when a context is restored onto another window. Worker
threads may prepare CPU data and queue work/events, but cannot create, acquire,
upload through, submit through, read back from or destroy an owning GL context.
Moving a GPU wrapper or queueing a resource does not transfer thread permission.

The checked SDL entry points apply in Debug and Release, including standalone
fixtures that include gepch/GLDebug/RenderCounters. SDLWindow lifecycle and ImGui
initialization/frame/shutdown boundaries also enforce ownership in both builds.
Debug additionally asserts a registered current context before the existing
first-party GL entry points, including create/upload/delete/draw/readback; the
RenderCounters wrappers forward through these checks. Assertions log `[GLThread]`
and terminate before the driver call. `IsCurrentOwner()` only inspects context
identity/registration and can reject a validation worker without issuing GL.
Ordinary Release GL calls retain their direct driver path.

This is a transitional call boundary, not a RenderDevice or resource-identity
migration. New GL entry points must be added to the explicit checked list. Direct
`glad_gl*` calls bypass it and are reserved for loader/probe instrumentation. ImGui
uses its private GL loader; its first-party boundaries and SDL context transitions
are guarded, rather than replacing that loader. Context ownership does not prove
that a resource name belongs to the selected context or establish GPU lifetime
safety for every legacy wrapper.

```powershell
python tools/test_shutdown.py --configuration Debug --output logs/rendering/phase21/final/Debug
python tools/test_shutdown.py --configuration Release --output logs/rendering/phase21/final/Release
python tools/test_shutdown.py --configuration Debug --smoke --output logs/rendering/phase21/smoke/Debug
python tools/test_shutdown.py --configuration Release --smoke --output logs/rendering/phase21/smoke/Release
```

The runner builds six consumers, retains the twelve lifecycle/manager modes and
adds `--context-thread`: two real root lifetimes, worker-only permission queries,
detach/restore, real buffer upload/readback/destruction and ImGui submission.
Disposable rejection processes cover context creation/switch/deletion, window/root
teardown and ImGui submission in both configurations. Debug additionally rejects
create/upload/delete/draw/readback from workers and readback with a detached
context. Those processes must exit 86 from the validation termination handler;
GL rejection cases have driver sentinels that exit 87 if reached. A returned
operation exits 89. A guard diagnostic is mandatory except for the pre-existing
EngineContext teardown termination. Release never attempts illegal direct GL.
The milestone smokes check all four applications for responsive startup and clean
native close; they do not claim image equivalence or benchmark performance.

## Uniform and renderbuffer ownership (Phase 23)

```powershell
python tools/test_uniform_renderbuffer.py --configuration Debug --output logs/rendering/phase23/final/Debug
python tools/test_uniform_renderbuffer.py --configuration Release --output logs/rendering/phase23/final/Release
```

The runner builds all six consumers with the existing toolchain, then links the
production library into `uniform_renderbuffer_probe.cpp`. Two hidden GL 4.6
context lifetimes per configuration check all six UBO types, RBO single/multisample
storage, compile-time ownership traits, moves, self/empty assignment, container
relocation, readback, indexed bindings, replacement, resize and exactly-once
deletion on the owning thread. Debug also runs two worker-destruction rejection
processes: exit 86 and a guard diagnostic are required; driver forwarding exits
87 and an unexpected return exits 89. Ordinary Release GL guards remain disabled.

UBO element counts must be nonzero and binding points valid for the context.
Allocation byte multiplication uses the GL signed size type. RBO dimensions and
sample counts must be positive and within context limits; multisample storage may
round the requested sample count upward. Successful initialization is confirmed
through storage queries, leaving pending driver errors observable to the caller.
Failed creation releases partial names and restores the generic binding. Failed
replacement preserves the old owner; indexed UBO bindings publish only after
storage exists. Empty/moved-from owners do not issue deletion calls.

The fixture injects zero-name and omitted-storage failures plus a synthetic
out-of-memory error at the glad boundary. It checks cleanup, unchanged bindings,
no premature framebuffer/texture allocation, recovery, and full-width large UBO
requests without actually exhausting GPU memory. These are deterministic failure
controls, not evidence of recovery from every driver/device-loss condition.
Live Debug counters are checked against observed names and return to zero;
Release deletion observers remain active while production counters stay disabled.

RenderTarget uses the RBO owner on both existing allocation paths. Its legacy
`RenderSize` convenience entry now uses the normal attachment resize path instead
of mutating whichever RBO was bound. Failed RBO allocation preserves its existing
attachments and restores dimensions/sample settings. Integration fixtures use
single-sample targets to isolate ownership from existing multisample texture
sampler diagnostics; standalone RBO checks cover real multisample storage.
Framebuffer ownership/format redesign, visual equivalence, benchmarks and
sanitizers are outside this phase. The existing application smoke runner can check
all four default configurations after these builds.

## Shader program ownership (Phase 24)

```powershell
python tools/test_shader_program.py --configuration Debug --output logs/rendering/phase24/final/Debug
python tools/test_shader_program.py --configuration Release --output logs/rendering/phase24/final/Release
```

The runner builds all six consumers with the existing C++20/static CRT toolchain,
then runs the production Shader implementation through two real GL 4.6 context
lifetimes per configuration. It also runs the existing ShaderManager failure/cache
fixture. The four default application smokes use `tools/test_shutdown.py --smoke`
after these builds. Runtime shader fixtures are written into disposable log folders.

`CreateShaderProgram` and `CreateShaderProgramFromFiles` return a C++20 variant
containing either a completely linked/reflected move-only Shader or a structured
error (code, optional stage, source label and diagnostic log). Incremental builders
and the existing manager boundary throw `ShaderCreationException` with the same
error fields. Standard allocation failures can still propagate as `std::bad_alloc`;
all partial shader/program names are retired during unwinding. Failed incremental
builds reset immediately. Compiling into a linked owner is rejected without
changing it; build a separate candidate for replacement.

Link retires all intermediate shaders and discovers uniforms once. The manager
publishes only a successful candidate and no longer repeats discovery. Missing
uniforms cache signed -1 and GL ignores uploads at that location. Moves, replacement
and destruction allocate no memory, including with MSVC Debug container proxies;
CPU containers transfer through unique ownership. GL operations and destruction
still require the owning context thread and a live context.

The probe checks real valid/invalid GLSL, link interface mismatch, missing source,
unsupported input, missing-uniform caching, uniform readback, moves/relocation,
exact deletion and context-thread ownership. It injects zero-name creation,
allocation denial after compilation, and log/reflection exceptions, then checks
cleanup and recovery. These are deterministic controls, not physical GPU-memory
exhaustion or device-loss tests. Expected compiler diagnostics are isolated to the
negative GLSL cases; unexpected GL errors/high/medium diagnostics fail the probe.
A marker confirms that diagnostics are active. Debug additionally requires worker
destruction rejection with exit 86 before the driver sentinel (87).

Program validation retains its existing pipeline-dependent diagnostic behavior.
This phase does not introduce resource handles, shader hot reload, pipeline-state
redesign, C++23, benchmark or sanitizer gates. Application smokes establish startup
and clean native close, not visual equivalence.

## Stable asset identity and publication (Phase 25)

```powershell
python tools/test_asset_registry.py --configuration Debug --output logs/rendering/phase25/final/Debug
python tools/test_asset_registry.py --configuration Release --output logs/rendering/phase25/final/Release
```

`Assets/AssetHandle.h` defines the common `AssetHandle<Tag>` model and reserves
MeshHandle, TextureHandle, SamplerHandle, ShaderProgramHandle, PipelineHandle,
MaterialTemplateHandle and MaterialInstanceHandle. Every handle contains a 32-bit
slot index, 64-bit generation and 64-bit registry lifetime identity. `{}` is null:
index UINT32_MAX, generation zero, registry zero. Non-null syntax alone does not
establish validity; resolution checks all fields. GL names and storage addresses
are payload, never engine identities. Handles are process-local, not serialized
asset keys. Registry identities are unique across types and root lifetimes in the
current executable; a future DLL/plugin boundary must share that allocator.

`AssetRegistry<Handle, Resource>` is stationary and owns immutable published
resource versions. Create publishes revision 1. Replace keeps the handle and
increments its revision; existing leases keep the old resource and revision.
Consumers propagate changes by comparing `(handle, lease.Revision())` and rebuild
dependent derived state before its next publication. A generation check alone is
not cache invalidation for replacement. Destroy invalidates resolution immediately;
slot reuse increments generation and resets revision to 1. Exhausted generations
quarantine slots permanently. Revision exhaustion rejects replacement without
changing the entry; destroy/create supplies a new identity. Exhausting the global
registry identity allocator also fails permanently without wrapping. Configurable
positive slot/generation/revision limits can reduce these ceilings; defaults use
the full integer ranges. Exhaustion never revives a stale handle.

The application-owned EngineContext exposes `AssetPublications()` only while its
managers are available, on the owner thread with its main context current. New
registry consumers use its common `AssetPublication` domain. Before extraction,
enter `BeginPublication()` and create/replace/destroy/collect entries. End that
scope before `BeginFrame()`, which covers serial extraction and all CPU submission
using that frame. Publication and frame scopes cannot overlap, nest or come from
another domain. Workers prepare CPU data; they do not access the registry or issue
GL. The application chooses this serial boundary until the scheduler assumes it
without changing its timing. Existing pointer-based managers/components are an
explicit compatibility boundary; this phase supplies the shared identity system
without migrating those consumers or adding a second scheduler.

```cpp
using Programs = GEngine::Asset::AssetRegistry<
    GEngine::Asset::ShaderProgramHandle, GEngine::Asset::Shader>;
auto& publication = context.AssetPublications();
Programs programs(publication); // Must retire before the EngineContext.
GEngine::Asset::ShaderProgramHandle handle;
{
    auto publish = publication.BeginPublication();
    handle = programs.Create(publish, std::move(completeShader));
}
{
    auto frame = publication.BeginFrame();
    auto lease = programs.Acquire(frame, handle);
    if (lease) lease->Bind();
    // Keep the lease through every submission using this resolved version.
}
{
    auto publish = publication.BeginPublication();
    // Close returns false while CPU leases or required GPU fences remain.
    if (!programs.Close(publish)) { /* finish users, then retry at a safe point */ }
}
```

A lease provides const payload access and retains stable heap storage across slot
vector growth, replacement and destruction. Copying it copies shared ownership,
not heavy assets. Pointers/references obtained from it may not outlive the lease.
Separate lease copies may be released by workers: the registry always retains a
strong current/retirement reference, so the final GL deletion remains on the owner
at collection or shutdown. Const access is not permission for worker GL calls or
concurrent mutation of external storage. Any mutable side channels in a payload
remain that resource system's responsibility.

Storage whose GPU lifetime exceeds CPU submission must attach an owned
`AssetRetirementFence` using `ProtectGpuUse(frame, lease, fence)` before queuing
that GPU use. Fence registration can allocate; submit only after it succeeds. A
fence remains incomplete until its associated submission completes. Multiple uses
can attach separate fences, all of which must complete. Polling/fence destruction
and resource destruction occur only on the context owner at a publication/retirement
safe point. Ordinary GL object deletion may use the driver's deferred deletion;
mapped/ring/storage reuse must supply the explicit fence contract. Collection never
waits for GPU completion. Registry Close is retryable; destruction with live leases,
incomplete fences, an active frame or a foreign owner terminates before releasing
resources. EngineContext requires registries/scopes drained before context teardown.
Resources and fences must have noexcept destructors and may not reenter their own
registry while it mutates. Failed construction/allocation preserves published state.

The runner first compiles a standalone CPU probe without SDL or engine linkage,
then checks invalid/stale/foreign handles, 30,001 allocations and slot reuse, stable
retained addresses, version propagation, generation/revision/domain exhaustion,
wrong-owner/scoped access, worker lease release and allocation failure atomicity.
Two real compiler-negative cases require wrong-type access/assignment diagnostics.
Disposable teardown controls require exit 86 before resource destruction (87).
It then builds six consumers and links the production library into the same probe:
two EngineContext lifetimes publish/replace/destroy real Shader programs, submit a
triangle, retain a real GL sync fence, and verify owner-thread deletion before
context teardown. Root teardown rejection also requires a still-live context.
The existing default application smoke runner covers all four applications after
these builds. Known startup framebuffer diagnostics are separated from the focused
registry GL checks. These are lifetime checks, not image, performance, device-loss,
physical GPU exhaustion or sanitizer evidence. Toolchain/C++20/static CRT remain.

# Phase 27 sampler validation

Run `python tools/test_sampler.py --configuration Debug --output logs/rendering/phase27/final-v2/Debug`
and the corresponding `Release` command. The runner builds the six maintained consumers
with the existing VS2022/v143, C++23 and static CRT settings, then links the real engine
library. `--no-build` requires an already built matching candidate. Commands, actual
exits, compiler include dependencies and binary hashes are recorded in `results.json`.

The normal contract is `Assets/Samplers/Sampler.h`: `SamplerDesc`, `GpuSampler`,
`SamplerCache`, `SampledTextureBinding` and `MaterialTextureBindings`. Consumers can use
`AssetsManager::GetSampler/ResolveSampler/SampleTexture` without backend headers. A
binding retains the existing typed TextureHandle and SamplerHandle version leases;
publish/resolve before submission and release bindings before root/context teardown.
The concrete legacy Material implements the neutral MaterialTextureBindings interface.
Materials submit in ascending texture-unit order and replace an existing unit in place.

Equivalent effective descriptions share a sampler. Disabled anisotropy becomes 1;
ClampToLimit explicitly clamps to the device maximum (or 1 without support), while
RequireExact returns Unsupported above that maximum. Non-finite values and invalid
semantic enums return InvalidDescription before publication. Inactive border colors
are canonicalized. Sampler/cache owner allocations use non-throwing allocation;
the approved registry retains its standard-container allocation behavior.

The GL probe checks real pixels for repeat/clamp, nearest/linear, depth comparison and
border sampling, plus cache hits, typed identity, invalid/unsupported policy, injected
allocation/driver failure rollback, slot exhaustion, move/relocation/destruction,
worker lease release and deterministic material ordering. Two context/root lifetimes
check exactly-once deletion; isolated worker destruction and live-lease cache teardown
must exit 86. Compiler dependency inspection rejects native headers in the normal
sampler/manager/material-binding contract. Existing startup target diagnostics are
reported separately; sampler operations must produce no GL errors.

Texture-only adapters explicitly clear the sampler at their unit. The backend-only
TextureSamplingDefaults adapter reads existing image/attachment policy once during
material setup, preserving legacy defaults without changing image ownership. Raw
target allocation, general shader/material migration and the remaining ECS/platform
surface retain their roadmap ownership. Graphical startup/close integration uses
`python tools/test_shutdown.py --configuration Debug --no-build --smoke --output <dir>`
and the corresponding Release command. No benchmark or sanitizer gate is implied.

## Phase 28/29 framebuffer ownership, description and resize

Run `python tools/test_framebuffer.py --configuration Debug --output <directory>`
and the corresponding Release command. The runner builds maintained affected
consumers, compiles the native-free framebuffer/target/startup-error/UI declarations
without backend include paths, and links probes to the production GEngine library.
`--no-build` reuses matching libraries; `--build-only`, `--mode` and `--regression`
support targeted development checks. A selected mode is not a full phase result.

`FrameBuffer::Create` owns its framebuffer, texture attachments and optional depth
renderbuffer. Descriptors use engine formats, attachment counts, image/cube/array
kinds, dimensions, layers and exact sample counts. Unsupported descriptions return
typed errors. Specialized targets and RenderTarget expose semantic binds, views,
readback and resolve. Native names are available only in the private backend/probe
header. Framebuffers are move-only; destruction/replacement require their creating
context on the owning thread. Moved-from objects are empty and report zero sizes.

Creation and resize prepare complete replacements before retiring changed
storage. Integer clear/readback validates the requested attachment and coordinates.
Resolve uses nearest filtering with matching dimensions/formats and a single-sample
destination. Integer textures use nearest sampling. Readback isolates pixel-pack
state and PBO bindings; clear and resolve isolate scissor state, and integer clear
also preserves color write masks and draw selectors. Depth-only targets enable no
color read/draw buffers. Cube faces are square; cascade resize retains its layers.

Attachment views observe the same owner object across resize. They do not own or
delete framebuffer storage. That owner object must outlive all uses of its views;
moving its ownership does not redirect previously issued views to the destination.
Publish and resolve resources before use, and finish submission before retirement.
This phase does not introduce a new attachment identity or lifetime registry.

Phase 29 adds the fixed-size, native-free `RenderTargetDesc`: `Storage` describes
extent, exact samples, formats, attachment slots, depth texture/renderbuffer intent,
kind and layers; `Usage` declares attachment, sampled, readback and presentation
intent. All storage is owned. The legacy specification and size overloads adapt to
the same implementation. Attachment-only targets omit resolve storage; multisample
color output usages require it. Sampling/readback methods enforce their usage.
This is allocation/output policy, not a render graph or pass load/store API.

A zero width or height is a successful deferred description with no GPU storage
and a false target readiness conversion. Structural validation still applies;
hardware limits/sample support are checked on the next nonzero allocation. Passing
zero to an allocated target releases its storage. Bind requires a ready target;
views/readback return typed errors while deferred. Equal descriptions preserve
names and pixels. Resize replaces size-dependent attachments; format/depth changes
retain unrelated attachments, and a sample-only change retains compatible resolve
storage. Scene and resolve replacements publish together or roll back together.
Retained attachments keep their contents; newly allocated attachments require the
caller's normal clear/render before use. No implicit load or clear is introduced.

`ReallocationCount()` counts successful transactions that allocate replacement
storage after the first live allocation, including resumption after zero extent.
It is available in both configurations; the existing Debug frame counter records
the same transactions. Initial allocation, zero-size release/deferral, metadata-only
changes, repeated size/samples and failed preparation do not count. Moves transfer
the accumulated count. A format/sample/kind change can invalidate an old view;
observers never return a name with an incompatible cached sampling target/format.
Reacquire views when changing output surface or attachment interpretation. Compatible
views recover across zero/nonzero transitions on the same owner object.

The focused GL probe checks two context lifetimes, rectangular/square targets,
actual integer texture sampling, MSAA color/integer resolve pixels, depth/cube/array
targets, views, moves/relocation and exact driver deletion. Phase 29 adds zero/nonzero
transitions, repeated same size, usage/format/sample variants, dependent attachment
preservation and exact reallocation counters. Fault injection covers
owner/name/storage/completeness failures and replacement rollback. Separate modes
check worker and foreign-context deletion before driver forwarding, plus typed
startup failures at early/late framebuffer-owner and wrapper allocations. Startup
publishes the target set only after all its allocations succeed.

The runner also executes affected uniform/renderbuffer, shadow, texture-attachment,
shutdown/move/cache and startup-rollback regressions. Input-control and interpolation
probes are compile/link checks for the changed initialization result; their unrelated
scheduler/physics suites are not rerun. Four default application startup/native-close
checks use `python tools/test_framebuffer.py --configuration Debug --no-build --smoke
--output <directory>` and its Release equivalent.

The approved C++23/toolset/static-CRT/build/dependency policy is unchanged. Newly
migrated framebuffer failures use expected results. Existing standalone UBO/RBO
exception APIs and unrelated platform, file-output, shader/material and application
contracts retain their documented future-phase ownership. The current Phase 28
review and current Phase 29 review record actual coverage, exceptions (if any),
evidence and seal status.
# Phase 30 viewport / platform boundary

`python tools/test_viewport.py --configuration Debug --output <directory> --smoke`
(and `Release`) compiles 16 normal platform/window/input/event/application headers
individually without backend include roots and inspects compiler dependency traces.
It builds the six maintained consumers with the approved C++23/v143/static-CRT
configuration and links a focused probe to the production library. `--no-build`
reuses matching builds; `--rebuild-library` explicitly rebuilds the affected library.

The probe covers separate native logical/framebuffer and editor logical/pixel
dimensions, actual target aspect, explicit fixed/native/editor size sources,
fractional and 2x conversion, quantized no-ops, coalesced drag requests, zero/reopen,
integer picking edges/readback, real ImGui docking/detached native viewport/redocking,
native visibility events, typed initialization/allocation failures, batch rollback,
two platform lifetimes and isolated worker rejection. Scale sweeps supply deterministic
scale inputs to production conversion and real GL storage; docking additionally
checks the actual backing-window drawable/logical ratio. They do not claim a
physical mixed-DPI monitor traversal. `--smoke` exercises startup and native close
of all four graphical applications from disposable working directories.

`python tools/test_input_control.py --configuration Debug --event-loop --no-build --output <directory>`
(and `Release`) checks the migrated event loop and real simulation/ray viewport
collapse/reopen. Its compiler mode now follows the already approved C++23 baseline.
`tools/test_framebuffer.py --configuration Debug --no-build --output <directory>`
(and `Release`) retains actual framebuffer/ownership/failure regressions and adapted
root lifecycle fixtures. Always fingerprint the finalized source and protected
inputs before the final runs. These commands do not seal or approve a checkpoint.

# Phase 31 CPU mesh data

`python tools/test_mesh_asset.py --configuration Debug --output <directory>`
(and `Release`) builds the six maintained consumers and links a CPU probe against
the production GEngine library. The consumer compiles with only engine and standard
library include roots, without a PCH or backend libraries. Compiler dependency
traces verify the header boundary. No window or graphics context is created.

`Mesh/MeshAsset.h` defines an immutable, move-only CPU owner, borrowed
`MeshSourceData` spans and explicit single-stream interleaved layouts. Creation
copies vertices, indices, attributes and submeshes; caller storage can then expire.
Returned views expire on owner move/assignment/destruction. Dynamic intent is
metadata for future uploads. Existing Geometry/import/rendering paths are unchanged.

Semantic slots are fixed: position 0, UV0 1, normal 2, tangent 3, bitangent 4,
joint indices 5, joint weights 6, entity ID 7, color0 8. Position/normal/bitangent
are Float32x3, UV0 Float32x2, tangent Float32x3/x4; joint indices accept signed or
unsigned 8/16/32-bit integer x4, weights Float32x4 or unsigned normalized 8/16-bit
x4, entity ID Int32x1, color Float32 or unsigned normalized 8/16-bit x3/x4.
Other combinations, duplicate semantics/slots, overlapping fields and fields
outside the record are rejected. Optional fields occupy no mandatory bytes.
Typed helpers constrain records to standard-layout/trivially-copyable types;
use `sizeof` and `offsetof`, including padding. Scalar/index bytes use host byte
order; reads do not require alignment. Indices are absent, UInt16 or UInt32.

Submesh ranges address indices for indexed meshes and vertices otherwise, with
absolute vertex ordinals and explicit material slots. Ranges may overlap or leave
gaps; order is preserved. Nonempty draw data requires explicit submeshes. Empty
meshes require a valid position layout and produce empty bounds. AABB and sphere
cover every vertex, including unreferenced vertices, reject nonfinite positions,
and use double precision for finite Float32 extremes. The sphere uses the AABB
midpoint, with radius rounded outward; it is not a minimum enclosing sphere.

The probe checks distinct multi-vertex position/normal/UV layouts, padded/aligned
records, additional attributes, both index formats, submesh/material ordering,
payload/range/overflow rejection, bounds, empty meshes, source lifetime, move and
self-move ownership, worker construction/destruction and injected failure at all
three allocation steps with zero retained arrays. Expected errors carry an error
code and the offending ordinal. No performance or GPU behavior is claimed;
allocation/upload belongs to Phase 32 and importer normalization to Phase 33.

# Phase 32 GPU mesh resource

`python tools/test_gpu_mesh.py --configuration Debug --output <directory>`
(and `Release`) builds the six maintained consumers, compiles the normal
`Mesh/GpuMesh.h` surface without backend headers, and runs a real OpenGL fixture
against the production library. `--no-build` reuses matching builds. It also runs
the existing vertex-buffer/Geometry ownership and CPU registry regressions.

`GpuMesh::Create` takes a validated `MeshAsset` and retains only layout/submesh/count
metadata. It owns one interleaved VBO and an optional EBO through the Phase 22
VertexBuffer/IndexBuffer owners, plus a Mesh-owned VAO. Private empty construction
bypasses legacy upload/error APIs. Initial upload is one vertex batch and one
separate index batch. Static/Dynamic selects the driver usage hint, while the
public update contract enforces immutability for static meshes. Integer attributes
use the integer pointer path; normalization is applied only when declared.
Creation verifies device limits, buffer storage and actual VAO state before success.

`VertexRecordUpdate` supplies the exact original layout (including attribute order),
first vertex, vertex count and complete contiguous records. Partial position edits
must reconstruct those records and retain other fields. Updates preserve capacity,
layout and index content. Dynamic zero-count updates are no-ops at offsets through
vertexCount; static updates always fail. Invalid ranges, byte counts, layout changes
and nonfinite positions fail before writes. Recreate/replace for layout/capacity
changes. BufferSubData retains normal driver synchronization; no mapping or custom
streaming is introduced. GpuMesh does not retain or expose stale CPU bounds; callers
own updated spatial data separately. Driver failures return structured errors;
pre-existing GL errors are reported before any new upload/draw is issued.

`PublishMesh` creates the complete resource before publishing the existing typed
MeshHandle. `MeshRegistry` uses the existing publication/lease/retirement model.
`UpdateMesh` runs during publication only and rejects updates while CPU leases or
unfinished GPU fences remain. Successful callbacks advance the version revision;
failure preserves it. Standard-container allocation behavior in the existing
wrappers/registry remains unchanged, consistent with the approved shared registry
policy; new explicit owner/metadata allocations use nothrow results and clean up
partial resources. A registry and all live resources must retire before context
teardown. Worker lease release cannot delete GPU owners.

The GL fixture uses transform feedback to check distinct shader-read position,
normal and UV values across multiple vertices, including a padded record and
position-only/position-normal layouts. It verifies normalized color and integer
joint inputs, indexed/non-indexed submesh ranges, full/partial updates, unchanged
records/fields, static/empty/invalid cases, binding restoration, moves, and source
payload independence. Injected owner/metadata/name/upload/attribute failures leave
no published handle or leaked resource. Two context lifetimes check exactly-once
deletion and no unexpected GL debug errors. Isolated child processes reject worker
creation/destruction and destruction under another context in both configurations.
GPU APIs stay private; importer/async loading/application migration remain in their
assigned phases. No performance gain is claimed.


## Phase 38: presentation bounds and revision sources

`python tools/test_render_state.py --configuration Debug|Release --output <directory>`
builds the six maintained consumers, checks the native-free scene header closure,
and runs the bounds/resource/interpolation fixture over two hidden GL lifetimes.
Use `--no-build` only with matching binaries. The phase review records the selected
hierarchy, interpolation and GPU mesh regressions and actual final evidence.

After authoring/physics and asset publication, open `AssetPublication::FrameAccess`
and call `_Scene::UpdateRenderState(resources)` before `RenderData().BeginExtraction()`.
The result contains presentation world matrices, conservative bounds, typed resource
resolution diagnostics, per-entity revisions and aggregate scene revisions. It is
an invalidation snapshot, not the future finalized draw frame. Mesh and prepared
material leases retain exact versions; retain the separately resolved target owner
through submission. Old entity IDs still require normal generation/lifetime checks.

Revision sources are effective presentation matrices (including tickless alpha and
ancestor motion), mesh identity/publication/submesh/flags, material identity and
published/authored/template/program/texture/sampler versions, light/camera component
values and poses, and target identity/publication/description/storage revision.
`CaptureRenderTargetRevision` copies the latter from a resolved target with its
caller-supplied typed registry identity/version. Target no-op resize leaves storage
revision unchanged. A deliberate resource publication creates a new version even
when bytes are equal; publishers skip replacement on no-op authoring (for example,
when `MaterialInstance::Revision()` is unchanged). Visibility edits and entity or
component removal also invalidate the aggregate scene. No-op observed state does
not advance counters. Counters are scoped to a scene lifetime and category.

`WorldBounds::CanCull()` is true only for valid bounds. Invalid or unavailable bounds
must remain conservatively visible; Empty means a known empty mesh. Affine interval
bounds cover non-uniform/negative scale, rotation and hierarchy shear, with outward
rounding. The sphere encloses the world AABB. Bounds cover uploaded mesh positions,
not arbitrary shader displacement. GPU owners retain local bounds; partial dynamic
updates conservatively union old/new extents without retaining the CPU vertex
payload. Replacement restores tight bounds. An attempted driver update retains that
safe union even when the driver reports failure. No physics collision bounds,
equations, fixed-step timing, legacy submission or culling algorithm is changed.
