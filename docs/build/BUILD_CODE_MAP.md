# Build code locator

Open only the row/section relevant to the active phase. Audit anchors A1-A10 and
plan anchors P1-P6 are stable section identifiers, not mandatory reading lists.

| Concern | Locator | Ownership / caution |
| --- | --- | --- |
| Governance and current work | ../../AGENTS.md; BUILD_STATE.md; phases/BUILD_PHASE_XX.md | Command routing, one phase, predecessor |
| Git/source evidence | evidence/BOOTSTRAP_BASELINE.json | Baseline/index and raw hashes; no build-pass claim |
| Current graph | ../../premake5.lua; ../../external/glad/premake5.lua; evidence/PREMAKE_VS2022_GRAPH.json | Eight projects; exact source/header/config inventory |
| C++20/CRT/warnings | premake root VS overrides and config filters; audit A4 | Actual compiler proof belongs to Phase 00 |
| Current drivers | ../../cli.py; ../../tools/gensln.py; buildsln.py; globals.py; run.py; run.bat | Root-CWD assumptions, VS2022 hardcode, subprocess behavior |
| Runtime safeguard | ../../tools/postbuild.py; app postbuild.py wrappers | Optional overlays + tracked DLL existence checks |
| DLL/import provenance | evidence/RUNTIME_INVENTORY.json; ../../external/*/lib; ../../bin/Debug and Release | Assimp/TBB/FMOD findings in audit A8; do not use bin as new build input |
| Core foundation | ../../GEngine/src/Core/Log.cpp, UUID.cpp; include/GEngine/Core/Base.h, Utility.h, Assert.h, Timer.h, Timestep.h, System.h; Container/ | Core folder also contains unrelated high-level code |
| Math/bounds | ../../GEngine/src/Math/Math.cpp; include/GEngine/Math/; include/GEngine/Physics/Bounds.* | Neutral bounds prepare before Physics/Geometry split |
| Events | ../../GEngine/src/Events/; include/GEngine/Events/; Inputs/*Codes.h | Values/signals below dispatch orchestration |
| Platform/Input | ../../GEngine/src/Windows/SDLWindow.cpp; src/Core/Window.cpp; src/Managers/{Input,Window,Event}Manager.cpp | BaseApp/UI feedback; phases 22/23 |
| GPU graphics | ../../GEngine/src/Mesh/; src/Geometry/Geometry.cpp; src/Assets/; src/Core/RenderTarget.cpp, FrameBuffer.cpp; src/Managers/AssetsManager.cpp, ShaderManager.cpp | GPU buffers/textures/font cache deliberately share Graphics owner |
| Procedural geometry | ../../GEngine/include/GEngine/Shapes/; src/Shapes/; Geometry/Curve/Surface; SpatialPartition/KDTree.* | Existing Geometry base is GPU; ShapeManager registers Animation and stays higher |
| Image/model assets | ../../GEngine/src/Core/Image.cpp, RawModel.cpp | stb / Assimp / Geometry; shared asset files are resources |
| Animation | ../../GEngine/src/Animation/; include/GEngine/Animation/; Extras/AssimpGLMHelpers.h | Assimp public Bone types; model registration above this target |
| ECS Scene | ../../GEngine/src/Scene/_Scene.cpp, _Entity.cpp; include/GEngine/Scene/_Scene.h, _Entity.h; Component/Component.h; src/Component/Component.cpp | Scene scheduler/mesh/Shader coupling and RenderSystem statistics |
| Legacy render Scene | ../../GEngine/include/GEngine/Core/Scene.cpp; src/Core/Actor.cpp, Entity.cpp, SceneGraph.cpp; src/Scene/BoxEntity.cpp, SkyBoxEntity.cpp | Keep legacy virtual render family in Render; do not force into neutral Scene |
| Physics | ../../GEngine/include/GEngine/Physics/ including .cpp, Constraints/, detail/ConvexHull.h | Actual compiled sources in include; ShapeConvex also compiled by test target |
| Audio | ../../GEngine/src/Audio/; include/GEngine/Audio/; ../../external/fmod/ | Studio/Core imports, version 1.9.9; banks under shared Assets |
| UI | ../../GEngine/include/GEngine/UI/UICore.cpp; src/Windows/ImGuiWindow.cpp | Texture/UI backends, window lifecycle inversion |
| Render | ../../GEngine/src/Core/Renderer.cpp, Renderer2D.cpp, SimpleRenderer.cpp; include/GEngine/Core/RenderSystem.cpp; Material/, Light/, Sprite/, Character/, Extras/, Camera/ | Render-to-Scene/Physics/Input/UI; BaseApp back-edge preparation |
| Composition | ../../GEngine/src/Core/BaseApp.cpp, GEngine.cpp; src/Managers/EventManager.cpp; include/GEngine/EntryPoint.h | High-level startup/dispatch; apps explicitly opt into audio |
| Applications | ../../GEngineEditor/src/; ../../RigidBodySimulation/src/ and include/; ../../Breakout/src/ and include/; ../../RayTracing/src/ and include/ | Main/startup, app CWD, per-app audit A9 |
| Physics regression | ../../PhysicsTests/src/main.cpp; src/Phase32ExactBoxWorld.h; README.md | Full suite + focused selectors; real ECS integration, custom framework |
| Benchmark | ../../PhysicsBenchmark/src/main.cpp; README.md | Existing CLI/CSV; profiling and steady-state modes |
| Vendors | ../../GEngine/include/external/{glm,imgui,imguizmo,stb_image,eventpp}; ../../external/{sdl2,spdlog,entt,assimp,tbb,reflection,glad,fmod} | Versions/roles audit A6; duplicate inactive ImGuizmo tree |
| Resources | ../../GEngine/include/GEngine/Assets/{Shaders,Images,Fonts,Models,AnimatedModels,Audio}; ../../Breakout/include/{images,levels} | App-relative paths; .obj models are resources |
| Historical defects | ../../docs/physics/PHASE_39_REVIEW.md; PHASE_39_LATTICE_BOUNCE_DIAGNOSTIC.md | Read only for an identified baseline failure; old CRT/results are not current truth |
| Planned new graph | GEngine/modules/Name/CMakeLists.txt; root CMakeLists.txt; cmake/; conan/ | Future contract scope only; none created in bootstrap |
| Planned driver | scripts/gengine.py | Future orchestration; CMake remains graph owner |

When a locator uses a shared suffix such as `src/` or `include/GEngine/`, it is
relative to the preceding GEngine/app root. Exact generated source paths are in
PREMAKE_VS2022_GRAPH.json. Expand phase path families into an exact ownership list
before editing/staging; this map is navigation, not a staging list.
