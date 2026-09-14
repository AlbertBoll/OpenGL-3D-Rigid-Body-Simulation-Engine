# Rendering validation

`RenderingValidation` is a small C++20 console target in the existing Premake /
VS2022 workspace. It has no GEngine or application dependency. Add named CPU
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

The native executable supports `--self-test` (also its default), `--gl`,
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
