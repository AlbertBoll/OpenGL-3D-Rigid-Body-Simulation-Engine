# Revision 02 root causes, established before source edits

Lineage: Revision 01 seal `f2094ad1bbddaa7cde782adcf2a1f0a28bd5249cd55dc8897589ef73b797230e`, preserved in `revision-01-snapshot.json`; source HEAD `b3b043b15ee814133e8419466a1bf3a985a3c958`. All four investigated source files matched the sealed baseline before ownership receipt 02. The accepted TBB distribution and staging helper are unchanged.

## B00-VALIDATION-003

`TestBoxFaceManifoldWorld` unconditionally expects generated-contact and solver-constraint telemetry equal to four (80 iterations). The physical manifold/contact and resting-state assertions are independent. `PhysicsProfile.cpp` returns `{}` when profiling is disabled; all profiling macros compile to no-ops. `IsPhysicsProfilingEnabled()` reports the linked engine's actual policy. The original profiling introduction, commit `97dbadb`, and current `PhysicsBenchmark/README.md` explicitly document no-op normal builds and external wall-time-only CSV. Commit `57fa05b` introduced the unconditional assertion without changing that policy.

Root cause: the test assumes profiling is enabled, contrary to the existing normal-build contract. Correction: expect four when the runtime reports profiling enabled, zero otherwise. Keep the physical checks, counters, macro policy, and simulation untouched. Validate both runtime policies using the existing normal/profiling Premake option.

## B00-RUNTIME-004

Five Breakout callers (background, paddle, awesomeface_r, block, block_solid) pass `../Breakout/include/images/<name>.png` from the Breakout source CWD. The loader consumes a complete filename; its extension parameter is separately used for cubemap faces. The original AssetsManager API (`02865ec`) passed caller filenames directly. Commit `bd1d2b4` added the shared image prefix and extension for logical names without adapting these existing explicit-path callers. Current logical-name callers include `Icons/PlayButton` and `PBR/rustediron/rustediron2_basecolor`.

Root cause: explicit relative filenames are treated as extensionless logical names, producing `../GEngine/include/GEngine/Assets/Images/../Breakout/include/images/background.png.png`. Revision 01 captured all five malformed requests and eleven fallback warnings. All five intended resources exist, with protected hashes.

Correction: distinguish an explicitly marked relative path (`./` or `../`) from a logical asset name; pass the former verbatim, preserving shared-prefix/extension handling for the latter. This restores the historical caller contract without aliases, copied resources, content edits, directory probing, or a global CWD change.

## B00-RUNTIME-005

Independent unchanged Debug Breakout reproduction: source CWD, constrained system PATH, hidden SDL window, normal WM_CLOSE sent after 45 seconds. CDB first-chance AV stack in `commands/baseline-Breakout-Debug-cdb-retry.txt.gz`:

`std::exchange` -> iterator orphaning -> `std::string::~string` -> `Texture::~Texture` -> `AssetsManager::FreeTextureResource` (119) -> `FreeAllResources` (135) -> `BaseApp::~BaseApp` (28) -> `BreakoutApp::~BreakoutApp` (71) -> entry-point delete.

The invalid container pointer has freed-heap pattern `0xfeeefeeefeeefef6`. Breakout's destructor already freed the same manager textures, shaders, and shapes before its base destructor repeated all three operations. Texture and shader maps retain dangling pointers after freeing; the second texture destruction is the observed crash.

Correction: remove the three duplicate shared-manager cleanup calls from Breakout's destructor. The existing BaseApp destructor still performs all shared cleanup once; application-object destruction and the normal shutdown chain remain. No exception is caught, shutdown skipped, or manager architecture changed. This defect occurs regardless of whether textures contain loaded pixels or fallback pixels and is independent of telemetry.

## B00-VALIDATION-006

Independent unchanged Debug benchmark reproduction uses the fixed six body counts, warmup 2, samples 5, dt 0.008333333. CDB first-chance AV stack in `commands/baseline-benchmark-Debug-cdb-retry.txt.gz`:

`std::_Atomic_storage<int,4>::load` -> `spdlog::logger::should_log` -> logger info -> `ShapeBox::Build` (76) -> `ShapeBox` constructor -> `main` (822).

Attempted read is address `0x50`, the log-level member of a null logger. The ordinary main path constructs ShapeBox before `Log::Initialize`; only the separate regression-baseline path initializes logging. ShapeBox's existing Debug log dereferences `GetLogger()`. Release logging macros compile out, explaining its independent success. The crash occurs before sampling or telemetry collection and has no dependency on application texture resources.

Correction: initialize logging and apply the existing benchmark client-log-off policy before normal ShapeBox construction. Preserve shape construction, sampling, numerical behavior, validation, and engine logging policy.

## Capture limits

The first two debugger attempts did not retain debugger stdout and are preserved as incomplete harness evidence. The separately named retries retain full exception records, source-resolved stacks, registers, and disassembly. Their metadata reports FAIL because the optional dump creation used a Windows path interpreted incorrectly by CDB; the raw output explicitly records that error. No dump is claimed. The required failing stack/location was successfully captured live in each retry before edits; no further crash reproduction is needed to manufacture a passing harness exit. All original and Revision 01 raw captures remain unchanged.
