# Current build audit

Bootstrap observation: 2026-09-12, Windows worktree `C:\dev\GEngine-build`.
This is source/project/binary inspection, not a fresh build or runtime certification.
Phase 00 has not started. Sections are independent lookup units for phase work.

## A1. Baseline and evidence

| Check | Observed |
| --- | --- |
| Branch | `build/cmake-conan-migration` |
| HEAD and source tag peel | `b3b043b15ee814133e8419466a1bf3a985a3c958` |
| Tag at HEAD | `render-refactor-phase-00-approved` |
| Remote | `origin`, fetch/push `https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git` |
| Entry status/index | Clean; no staged, unstaged, untracked or ignored entries |
| Tracked files at entry | 1,568 |
| Worktree metadata | `.git` points to `C:/dev/GEngine/.git/worktrees/GEngine-build` |
| Existing governance | No root AGENTS/MANIFEST/.codex or local Rendering governance; tracked historical Physics documents exist |

Git needed a command-scoped safe.directory override for the sandbox account.
No global Git configuration or shared excludes were changed. The source tag and
configured remote were checked locally; remote publication was not changed.

Captured evidence:

- [BOOTSTRAP_BASELINE.json](evidence/BOOTSTRAP_BASELINE.json): identities, index
  SHA-256 and 1,265 tracked source/build/binary input hashes (not an exhaustive
  resource-content hash set and not a phase execution seal).
- [PREMAKE_VS2022_GRAPH.json](evidence/PREMAKE_VS2022_GRAPH.json): all eight generated
  projects, exact compiled/header file ownership, project references, property
  groups and per-configuration compiler/linker/postbuild settings.
- [RUNTIME_INVENTORY.json](evidence/RUNTIME_INVENTORY.json): all 85 tracked DLL
  paths, SHA-256, machine type, normal PE imports, and 13 library DLL-name inventories.
  Delay loads and executable import closures remain Phase 00 work.

`vendor/bin/premake/premake5.exe --version` reported 5.0.0-beta1.
`vendor/bin/premake/premake5.exe vs2022` exited 0, generating one solution and
eight project/filter pairs. Generated files are ignored. No engine build/test/
benchmark/application was run. No production CMake or Conan was created.
The tracked vendor `GEngine/include/external/glm/CMakeLists.txt` already existed;
it is not the engine build and was neither invoked nor modified.

## A2. Premake and generated target graph

Root `premake5.lua` plus `external/glad/premake5.lua` are the complete discovered
Premake definition set. Solution startup project is Breakout. Configuration matrix
is Debug/Release x64; all eight projects select v143.

| Target | Kind/language | Compiled files / header entries | Generated project references |
| --- | --- | --- | --- |
| GEngine | StaticLib / C++20 | 116 / 502 | None |
| glad | StaticLib / C | 1 / 2 | None |
| GEngineEditor | ConsoleApp / C++20 | 5 / 3 | GEngine, glad |
| RigidBodySimulation | ConsoleApp / C++20 | 2 / 1 | GEngine, glad |
| Breakout | ConsoleApp / C++20 | 4 / 3 | GEngine, glad |
| RayTracing | ConsoleApp / C++20 | 2 / 1 | GEngine, glad |
| PhysicsTests | ConsoleApp / C++20 | 2 / 0 | GEngine, glad |
| PhysicsBenchmark | ConsoleApp / C++20 | 1 / 0 | GEngine |

Header counts are project entries, not a complete transitive include inventory.
PhysicsTests also includes `PhysicsTests/src/Phase32ExactBoxWorld.h`, which its
Premake file glob does not list as a project header. No other actual target or
dedicated third-party test framework target was found.

```text
Editor / RigidBodySimulation / Breakout / RayTracing --> GEngine.lib + glad.lib
PhysicsTests ---------------------------------------> GEngine.lib + glad.lib
PhysicsBenchmark -----------------------------------> GEngine.lib
GEngine.lib: first-party subsystems + compiled vendor implementations
```

GEngine has no project reference to glad even though its sources use GLAD;
applications supply the missing link closure. Static archives defer external
symbol resolution to consumers. Linking GEngine.lib does not establish that all
archive members or all linked import libraries become runtime dependencies.

## A3. Source/header ownership and hidden coupling

The engine glob compiles `.cpp` under both `GEngine/src/` and
`GEngine/include/`. Of 116 TUs, 21 are under `include/GEngine`, 12 under
`include/external`, and 83 under `src` (including the PCH creator). There are
104 first-party TUs including the PCH creator. Exact paths are in A1 evidence.

Compiled vendor TUs are GLM `detail/glm.cpp`; eight ImGui core/demo/stdlib/SDL/
OpenGL3 files; ImGuizmo; stb image and image-write implementations. Their local
sources include `gepch.h` except the ImGuizmo NoPCH exception. GLM is normally
consumed as headers, but this checkout contains a compiled explicit-instantiation
TU with local PCH integration; dropping it needs an ownership/equivalence decision.
Do not regenerate GLAD or upgrade these vendors incidentally.

PhysicsTests compiles `GEngine/include/GEngine/Physics/ShapeConvex.cpp` as a target
source while GEngine's glob compiles it too. This is duplicate ownership, not proof
of a duplicate-symbol linker error: archive extraction can conceal it. Assign it
once when the Physics target and tests migrate; preserve the current test cases.

`gepch.h` injects GLAD, SDL/SDL_ttf, logging, GLM and extensive standard-library
headers into all engine areas, including Physics. Some public headers omit their
own standard/type/macro includes (e.g. Texture's NONCOPYMOVABLE and Event's string/
vector usage). A PCH-free module consumer must catch this. Do not replicate the
monolithic PCH as a PUBLIC dependency. PCHs may be PRIVATE optimizations after
explicit include correctness is proven.

The folder named Core includes BaseApp, renderer, framebuffers, scene, entities,
ray tracing, image import and logging. It is not one coherent foundation target.
Two scene families coexist: legacy `Core/Scene`/Actor/Entity and ECS `Scene/_Scene`/
`_Entity`. Legacy scene/entity Render methods must remain with the Render family;
splitting virtual-method definitions across archives can preserve a hidden cycle.
The proposed Scene module therefore centers on the ECS family.

## A4. Compiler, language, CRT and linker settings

Generated C++ projects specify `LanguageStandard=stdcpp20`; the root override fixes
Premake beta1's default mapping of C++20 to `c++latest`. No C language standard is
specified for GLAD; retain its C translation mode, not forced C++20.

| Setting | Debug | Release |
| --- | --- | --- |
| RuntimeLibrary, all eight targets | MultiThreadedDebug (`/MTd`) | MultiThreaded (`/MT`) |
| Optimization | Disabled | Full; intrinsic functions, function-level linking, string pooling |
| Debug information | EditAndContinue; linker debug information on | None; linker debug information off |
| Release link options | Default | COMDAT folding and reference optimization |
| First-party warning level | Level3 | Level3 |

All C++ projects define GENGINE_PLATFORM_WINDOWS, the configuration-specific
GENGINE_CONFIG_DEBUG/RELEASE, and
`_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING` (its name does not select C++23).
GEngine adds GLFW_INCLUDE_NONE and GENGINE_WINDOW_SDL. Tests/benchmark add
SDL_MAIN_HANDLED. `--physics-profiling` adds GE_ENABLE_PHYSICS_PROFILING to GEngine
and PhysicsBenchmark, not PhysicsTests; preserve and examine this exact behavior.

Root VS overrides add `/external:I` for repository `external`,
`GEngine/include/external`, and MSVC STL includes; ExternalWarningLevel is off,
ExternalTemplatesDiagnostics is true. GLAD C has Level3 without these C++ overrides.
There is no project-level warning-as-error switch. A zero-warning *requirement*
is not a measured zero-warning result in this bootstrap.

Exception/RTTI flags and many linker/system defaults are not explicitly emitted;
record resolved MSBuild defaults and actual commands in Phase 00. No first-party
resource `.rc` target or explicit application `opengl32.lib` entry was found.
GL calls resolve through GLAD/SDL loaders; Windows import defaults/SDK and OpenGL
driver requirements still belong in the final system dependency model.

`systemversion "lastest"` is misspelled on GEngine. Generated engine/application
projects lack a WindowsTargetPlatformVersion element where the Lua might suggest
one; glad/tests/benchmark emit 10.0. Inspect evaluated SDK selection instead of
copying the typo. The four apps' Windows filters also need evaluated verification.

Actual compiler commands were NOT captured here. Historical Physics review 39
describes a nominal Release using /MTd; that older narrative is superseded by the
current generated Release /MT setting. Runtime/test expectations involving the
Debug CRT must be re-established at the current source tag.

## A5. Include/library paths and outputs

Engine IncludePath contains `GEngine/include/GEngine`, `GEngine/include/external`,
and external SDL2, spdlog, glad, TBB, rttr, reflection, EnTT, Assimp and FMOD include
roots. `external/rttr` is absent. App paths broadly duplicate engine/public/vendor
roots, add their own include directory where configured, and rely on copied SDL
headers under ImGui because app paths omit the primary SDL include root.
PhysicsBenchmark narrows paths to engine, spdlog and EnTT; PhysicsTests adds SDL/glad.
Mixed `GEngine/...`, `Core/...`, `external/...` and case variants reflect broad
include search and Windows case-insensitivity, not portable target ownership.

All four GUI-capable console applications link the same explicit import/static
library list in BOTH configurations: SDL2main, SDL2, SDL2test, tbb12, tbb12_debug,
tbb, tbb_debug, SDL2_ttf, assimp, fmod64_vc, fmodL64_vc, fmodstudio64_vc,
fmodstudioL64_vc, plus project references GEngine/glad. Library roots are external
SDL2/TBB/rttr/Assimp/FMOD. Both release/debug TBB and FMOD variants are offered
simultaneously; actual selected imports need executable inspection, not guessing.

Engine/apps/test/benchmark outputs: `bin/<Config>/<Target>/<Target>.lib|exe`;
intermediates: `bin-int/<Config>/<Target>/`. GLAD is relative to its own script:
`external/glad/bin/<Config>/glad/glad.lib` and
`external/glad/bin-int/<Config>/glad/`. Keep CMake output roots separate during
coexistence. `RayTracing/x64/Debug` contains tracked historical .obj/build files;
they are protected baseline artifacts, not inputs for a new build.

## A6. Dependency ownership and version evidence

Decisions below describe the target policy. Package availability, recipe revisions,
options and binary ABI matching are unresolved until Phase 01; do not silently
substitute a newer available version.

| Dependency | Current evidence/usage | Target ownership |
| --- | --- | --- |
| GLM | Local setup.hpp says 0.9.9.9; Math and public value types; compiled glm.cpp | Conan-managed, exact compatibility/provenance gate; preserve instantiation TU until equivalence proven |
| spdlog/fmt | spdlog 1.10.0, bundled fmt 8.1.1; Core/Log public header | Conan-managed; choose one fmt owner and matching header-only/compiled mode |
| EnTT | Amalgamated 3.10.0; ECS Scene/public entity templates | Conan-managed, exact identity reviewed |
| SDL2 | Headers and DLL 2.0.20; Platform/Input/UI | Conan-managed; retain SDL2 API |
| SDL_ttf | Primary header 2.0.15, DLL file version 2.0.18; Font/TextTexture | Conan-managed, explicit mismatch resolution |
| Assimp | Older header tree lacks reliable release identifier; RawModel/Animation; multiple DLL generations | Conan-managed after ABI/version/import audit; no invented version |
| oneTBB | Headers 2021.5.0; active parallel_for in SimpleRenderer, public Core/Parallel utility | Conan-managed; pin recipe/options/config runtime; resolve nonstandard tbb/tbb include layout |
| FreeType | libfreetype-6.dll tracked, no direct first-party include; version unproven | Conan-managed transitively if chosen SDL_ttf recipe requires it; no gratuitous direct dependency |
| zlib | Tracked zlib1.dll version 1.2.8; FreeType imports it | Conan-managed through verified package closure |
| GLAD | Generator 0.1.35, May 2022, GL 4.6 compatibility API header; one C TU | Vendored STATIC; preserve generated loader |
| ImGui | 1.88 WIP / numeric 18724, SDL/OpenGL3 backends, copied SDL headers | Vendored STATIC targets; no incidental upgrade |
| ImGuizmo | Active local header 1.61 WIP; another unused external/imguizmo tree exists | Vendored STATIC, select active copy only |
| stb | stb_image 2.27 plus image-write implementation | Vendored STATIC implementations, headers exposed only where needed |
| eventpp/reflection | Present vendor headers; no active first-party include found | Vendored dormant inventory; do not create linked targets without usage |
| rttr | Premake include/lib roots but directory absent; no active include found | Stale declared path, not a dependency to invent |
| FMOD | Headers 0x00010909 and four DLLs 1.9.9 build 91295; Audio and app calls | Local/proprietary verified SDK, FMOD::Core and FMOD::Studio imported targets |
| Test/benchmark framework | Custom main/assertion/count/CSV code; no Catch2/GTest/doctest/Google Benchmark | No framework package required; register existing executable behavior |
| Windows SDK/MSVC/CRT/OpenGL | Toolchain/OS/driver and standard facilities | System/toolchain, explicitly recorded |

Local tools observed: Python C:/Python310, CMake 4.4.3, Conan 2.2.3; VS2022 MSBuild
exists and its installed VC tools directory is 14.44.35207. `vswhere -latest`
selects Visual Studio 18 Community on this machine, so unrestricted discovery
would choose the wrong reference toolchain. Pin the VS2022 v143 instance and actual
compiler/SDK versions after verification. No tool installation occurred.

## A7. Dependency cycles and smallest boundary work

Arrows here mean actual include/call/type coupling under naive candidate ownership,
not already-existing CMake dependencies. PCH-only pollution is distinguished from
symbol cycles. The plan deliberately groups tightly coupled GPU types in Graphics
and legacy rendering entities in Render rather than inventing extra libraries.

| Boundary | Evidence | Classification and minimum action |
| --- | --- | --- |
| Scene / Render | Component.cpp:5,48-52 calls RenderSystem statistics; RenderSystem.h:3 and .cpp:3 consume ECS Scene; legacy Core/Scene.cpp:31-56 renders | Real bidirectional coupling under naive split. Separate ECS component data/stat accounting/render helpers; keep legacy scene/actor render family in Render. Phase 21 before Scene extraction |
| Scene / Physics | _Scene.cpp constructs/steps PhysicsSystem; Shape.h:6 and PhysicsBody.h:7 include Component.h, whose camera/bounds/render declarations spread back | Real header cycle, largely removable unused includes. Phase 13 narrows Physics headers; Phase 20 validates one-way Scene-to-Physics lifecycle. Keep existing scheduler in Scene; no solver refactor |
| Assets / Render | AssetsManager.h:6 includes RenderTarget; Render code calls assets managers; TextTexture.cpp:3/17 calls AssetsManager | Real naive-folder cycle. Phase 15 assigns Texture/Shader/RenderTarget/font cache/TextTexture together to Graphics. Assets means image/model import, not the entire Assets folder |
| Graphics / Render | Geometry.cpp and Mesh files issue GL calls; RenderTarget uses Texture; Component.cpp mixes GPU and Render stats | Low-level buffers/textures need no high-level renderer. Group GPU primitives together; remove Render stats edge in Phase 21. No evidence requires broad Graphics-to-Render linking |
| Platform / Graphics/Render | SDLWindow.cpp includes BaseApp, creates GL context and ImGuiWindow; Core/Window.h includes ImGuiWindow.h; Texture.cpp takes Window* | Actual UI/window feedback plus high-level include reach. Phases 14/22/23 isolate screenshot dimensions, application services and UI lifecycle; Platform retains SDL context, UI consumes it |
| Animation / Assets/Scene | AnimatedModel.cpp uses Assimp and Geometry; ShapeManager.cpp:22 imports AnimatedModel; Material/AnimatedMaterial consumes animation | No direct Animation-to-Scene edge found. Keep importer in Animation, low-level Geometry below it, and ShapeManager/model registration in Render/composition. Do not place that manager in low-level Geometry and create a cycle |
| UI / Render | UICore.cpp uses Texture; ImGuiWindow.cpp uses SDL+GL backends; RenderSystem.cpp includes ImGui | UI-to-Graphics/Platform is real; no UI-to-Render requirement found in UICore. Keep render diagnostics in Render (Render-to-UI), window UI hooks inverted in Phase 24 |
| Input / Platform | InputManager.cpp uses SDLWindow and BaseApp::GetEngine; EventManager.cpp reaches BaseApp and dispatches ImGui events | Real app/service feedback, not a need for platform-to-input link. Phase 23 injects existing service references/event sinks; Platform remains below Input; EventManager orchestration belongs in Application |
| Geometry / Physics | Shapes/Diamond.h constructs ShapeConvex and calls detail::BuildConvexHull; Shapes/Convex.h exposes ShapeConvex | Real one-way Geometry-to-Physics requirement after Phase 13 removes the reverse component/PCH reach. Retain the dependency; do not refactor convex algorithms to make the graph look cleaner |

Additional blockers: common PCH hides missing includes; exported public headers
use implicit GLM/macros; vendor PCH code reaches engine logging; naive RawModel /
ShapeManager / AnimatedModel grouping cycles; archive ownership duplicates
ShapeConvex. Each extraction must validate actual symbol closure without link-group,
repeat-library, whole-archive or PUBLIC-everything escapes. If the listed small
preparation is insufficient, stop and propose the smallest extra phase.

## A8. Runtime DLLs, resources and CWD

85 DLL paths contain 12 distinct SHA-256 contents, all inspected as x64 PE files.
They are tracked under the four applications' Debug/Release bin directories;
FMOD has no separate authoritative DLL directory under external/fmod in this
checkout. Import libraries do not establish ownership/provenance on their own.

`tools/postbuild.py`, invoked through all four app wrappers, optionally copies
PostBuildCopy and PostBuildCopy_windows, accepts robocopy return codes 0-7 and
raises on >=8. Neither optional overlay directory exists here. It then checks
required runtime files and fails on missing files. It verifies existence, not
hash/architecture/ABI/import closure. Preserve these safeguards during migration.

Required every config: SDL2, SDL2_ttf, assimp.dll, all four FMOD DLLs,
libfreetype-6, zlib1. Debug also requires assimp-vc140-mt. Release requires
assimp-vc143-mt and assimp-vc143-mtd; Editor Release additionally requires vc140.

Concrete risks from binary inspection:

- `external/assimp/lib/assimp.lib` contains import name **assimp-vc140-mt.dll**.
  This DLL is absent from Release Breakout/RigidBodySimulation/RayTracing, though
  their postbuild list accepts other names. Whether a given executable pulls an
  Assimp symbol must be checked after a fresh link. This is a likely loader risk,
  not an observed startup failure in this bootstrap.
- assimp-vc140-mt imports MSVCP140D, VCRUNTIME140D, VCRUNTIME140_1D and ucrtbased:
  a Debug runtime despite its filename. assimp.dll imports MSVCP110/MSVCR110.
  vc143-mtd also imports Debug CRT; vc143-mt imports release CRT. Application /MT
  does not remove CRT requirements of third-party DLLs.
- TBB libraries name tbb12.dll and tbb12_debug.dll; neither is tracked/staged by
  the safeguard. SimpleRenderer has active parallel_for calls. Fresh executable
  import/loader checks must resolve this without relying on global PATH.
- FMOD Studio imports matching Core (studioL to fmodL); select coherent debug/
  release SDK pairs. FreeType imports zlib1. SDL_ttf's normal import list does not
  itself name FreeType; preserve the reference inventory until full closure is
  proven rather than declaring every tracked DLL essential.

Runtime roots are mostly relative to the application source CWD.
`tools/run.bat` explicitly pushd's to `<repo>/<App>` before invoking the exe and
preserves its exit code. VS projects have no explicit LocalDebuggerWorkingDirectory;
verify IDE startup in Phase 00. `tools/run.py` assumes invocation from repository
root; its non-Windows path uses a different CWD. Windows is this track's scope.

| Owner | Resource evidence |
| --- | --- |
| Shared graphics | `GEngine/include/GEngine/Assets/Shaders` (.vert/.frag/.gs), Images, Fonts, Models, AnimatedModels |
| Managers | AssetsManager.cpp image root and ShapeManager.cpp model roots use `../GEngine/include/GEngine/Assets/...` |
| UI backend | ImGuiWindow.cpp loads OpenSans-Regular/Bold from shared Fonts |
| Audio | AudioSystem.cpp loads Master Bank.strings.bank, Master Bank.bank, ZeldaTheme.bank from shared Audio/Bank |
| Breakout | Four .lvl files and local images under Breakout/include; relative paths are passed through shared managers and need startup verification |
| Editor | dancing_vampire.dae plus shared models/material images/audio/fonts |
| RigidBodySimulation | shadow shaders, icons, PBR images, skybox, model-derived collision fixtures; audio initialization is active |
| RayTracing | Procedural scene plus shared application/UI startup fonts; TBB renderer; no direct audio initialization found |

Resources in the scanned asset/local-Breakout inventory include 17 .vert, 17 .frag,
2 .gs, 11 .ttf, 127 PNG/JPG, 17 OBJ, 1 DAE and 3 banks. A `.obj` in Assets/Models
is a resource, not an object file. Default Carlito path in AssetsManager differs
from the shared root; classify missing-path behavior by actual reference use.

Final CMake runtime staging must use approved SDK/package/asset origins and verify
names, hashes, architecture and transitive non-system closure in empty output trees.
Do not copy from a previously built bin tree. A staged sandbox may reproduce the
existing relative directory layout and CWD without source-path modernization.
Separate output/build/clean roots must protect tracked baseline DLLs and assets.

## A9. Applications, tests and benchmark closures

All four apps use EntryPoint/BaseApp/SDL/OpenGL startup and require shared UI fonts.
Their current Premake external lists are broad (A5); the table is the proposed
source-supported closure, subject to link/runtime proof.

| Consumer | Internal roots and real additional requirements | Runtime/validation |
| --- | --- | --- |
| GEngineEditor | Application, Render, Geometry, Assets, Animation, Audio, UI; editor panels, ImGuizmo, FMOD direct calls, TBB utility headers | Source CWD; models, animation, textures, banks/fonts; startup, panel/render/audio/animation smoke |
| RigidBodySimulation | Application, Scene, Physics, Render, Geometry, Assets, Audio, UI | Source CWD; shaders/PBR/skybox/icons/banks; start/stop/restart/picking and unchanged physics results |
| Breakout | Application, Render 2D/sprites, Graphics/Assets, Audio, Input | Source CWD; levels/images/banks/fonts; level load, input and audio smoke |
| RayTracing | Application, Render's SimpleRenderer, Assets image support, Input/UI; oneTBB | Source CWD; procedural scene/UI fonts; image creation and TBB closure; no gratuitous Audio/FMOD |
| PhysicsTests | Physics plus ECS Scene integration tests; Math/Core/EnTT and current geometry/GLAD compile/link leakage | Custom runner; full suite and focused selectors, no intended SDL/OpenGL initialization; preserve integration coverage |
| PhysicsBenchmark | Physics, Math, Core logging/profiling | Custom CSV runner, no assets/SDL/OpenGL/UI/audio initialization; timing descriptive, deterministic counters/results gate |

PhysicsTests main is about 8,100 lines with many focused selectors including
`--solver-storage`, `--runtime-transform`, `--scene-runtime-lifecycle`,
`--fixed-scheduling`, `--absolute-scaling`, contact/sleep/TOI/GJK/EPA cases.
The README is narrower than current source. PhysicsBenchmark supports body counts,
warmup/samples/dt and steady-state controls; source has additional evolved modes.
Record exact source-supported commands at Phase 00 rather than treating old README
examples as the entire suite. No new test framework is needed for this migration.

## A10. Validation limits and baseline-known issues

The approved source tag is established; fresh build warning counts, compiler
command truth, full test results and application startup are **NOT RUN here**.
Do not label them PASS from tag names or historical reviews. Phase 00 must capture
both configurations using current flags before equivalence claims.

Historical Physics phase 39 reports 18,898 checks in an older environment and a
separate lattice-bounce/contact-convergence diagnostic. Those are historical
observations, not a promised current count or Build repair scope. No local Rendering
failure registry exists. New reference failures must be reproduced and recorded
with exact command/result; only known, unchanged failures can be compared as such.
Build/CRT/warning requirements cannot be silently waived as legacy defects.

Highest risks: unknown Assimp ABI/provenance; SDL_ttf header/DLL drift; missing TBB
runtime; host toolchain drift; common-PCH dependencies; component/renderer cycles;
duplicate ownership; CWD resource paths and stale tracked binaries masking failures.
The roadmap provides targeted preparation, package/runtime gates, per-module
reviews, full equivalence, an authoritative switch and a separate retirement gate.
