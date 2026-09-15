# Rendering validation

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
