# Legacy renderer policy - Phase 64

Owner-selected policy: **freeze/deprecate legacy raster APIs; maintain existing applications**. The owner selected this option during Phase 64 execution: "Freeze/deprecate legacy raster APIs; maintain existing applications (Recommended)". This is an explicit product choice, not a formal deferral. Phase checkpoint approval remains separate. No application migration, deprecation attribute, removal, or backend implementation is part of Phase 64.

Inventory baseline: `03f6cdb674625b68d96e0f042f6e4b1f165dd6e5`, tag `render-refactor-phase-63-approved`, branch `rendering/refactor`. The repository remains on the approved C++23, MSVC v143, Premake/VS2022, static CRT and existing dependency policy.

## Current users and dependencies

Paths below are relative to the repository root. The inventory distinguishes live application drawing, initialization, retained internal coupling, and test use; declarations and commented calls are not proof of a live draw path.

| Consumer | Current rendering path | Dependencies and consequence |
| --- | --- | --- |
| GEngineEditor | `GEngineEditor/src/SceneApp.cpp` authors an Actor `Scene`; inherited `GEngine/src/Core/BaseApp.cpp::Render` supplies `legacyScene` to FrameScheduler, then calls `EngineContext::RenderScene` -> `Renderer::RenderScene` | `Core/Scene.cpp`, `Core/Group.h`, Entity/Actor, Geometry, legacy Material subclasses and LightEntity. The sample also creates AnimatedModel/Animation and uses ShapeManager model imports. Removing Renderer alone breaks the application. |
| Breakout | `Breakout/src/BreakoutApp.cpp::Render` supplies `legacyScene`; Renderer2D draws background/player/ball and `Breakout/src/GameLevel.cpp::Render` draws surviving bricks | SpriteEntity, SpriteMaterial, SpriteGeometry, orthographic camera, shape/texture managers. Sprite ordering, blending, coordinates, transforms and gameplay must survive any later migration. |
| RayTracing | `RayTracing/src/RayTracing.cpp::Render` supplies `editorUI`; OnUIRender drives SimpleRenderer and presents its Image | SimpleRenderer uses synchronous TBB CPU pixel tasks, then owner-thread image upload. Image::GetTexID feeds ImGui::Image in the application. Renderer::Initialize/SetSurfaceSize remain startup dependencies; it does not use Renderer for scene drawing. |
| RigidBodySimulation | `RigidBodySimulation/src/RigidBodySimulation.cpp::Render` supplies FrameSceneInput to FrameScheduler; SceneRenderResources -> extraction -> immutable RenderFrame -> FrameSubmission | This is the modern raster consumer. The old RenderSystem::OnMouseClicked text is inside a block comment. ShapeManager's private semantic mesh export and physics attachment still depend on legacy Geometry/Component implementation; the application does not consume their native names. |
| Residual RenderSystem | `GEngine/include/GEngine/Core/RenderSystem.h` and `.cpp` retain old ECS drawing/shadow/picking helpers | No active RenderSystem draw call was found in the four application sources after comment filtering. `GEngine/src/Component/Component.cpp` still updates its statistics, and validation probes call its legacy operations. This is not proof that the translation unit is safely removable. |

The shared platform, EngineContext, texture/sampler/shader owners, framebuffer/target owners, asset publication, and FrameScheduler already serve more than one path. **Shared scheduling does not mean the raster frontends share FrameSubmission.** Editor and Breakout still issue legacy drawing through their callbacks; the CPU ray path has its own image presentation boundary. No separate RenderDevice class or alternate backend is implied.

Current retained implementation locations include:

- Actor renderer: `GEngine/src/Core/Renderer.cpp`, `GEngine/include/GEngine/Core/Scene.cpp`, `GEngine/include/GEngine/Core/Group.h`, `GEngine/src/Core/Entity.cpp`, `GEngine/src/Core/Actor.cpp`.
- Sprite renderer: `GEngine/src/Core/Renderer2D.cpp`, `GEngine/src/Sprite/SpriteEntity.cpp`, `GEngine/src/Material/SpriteMaterial.cpp`.
- Shared legacy resources: `GEngine/include/GEngine/Geometry/Geometry.h`, `GEngine/src/Geometry/Geometry.cpp`, `GEngine/include/GEngine/Material/Material.h`, `GEngine/src/Material/Material.cpp`, `GEngine/include/GEngine/Component/Component.h`, `GEngine/src/Component/Component.cpp`.
- Import/animation: `GEngine/include/GEngine/Core/RawModel.h`, `GEngine/src/Core/RawModel.cpp`, `GEngine/include/GEngine/Animation/AnimatedModel.h`, `GEngine/src/Animation/AnimatedModel.cpp`, `GEngine/include/GEngine/Animation/Animation.h`, `GEngine/src/Animation/Animation.cpp`, and ShapeManager's header/source.
- CPU ray/image: `GEngine/include/GEngine/Core/SimpleRenderer.h`, `GEngine/src/Core/SimpleRenderer.cpp`, `GEngine/include/GEngine/Core/Image.h`, `GEngine/src/Core/Image.cpp`.

`premake5.lua` builds both include-tree and src-tree implementation files into GEngine and retains all four applications. In particular, Core/Scene.cpp and Core/RenderSystem.cpp really live under the include tree. Their presence there does not make their GL implementation a normal consumer contract.

Known direct legacy test users are `tools/input_control_probe.cpp`, `tools/interpolation_probe.cpp`, `tools/light_components_probe.cpp`, and `tools/shutdown_probe.cpp`; `tools/ray_tracing_probe.cpp` covers SimpleRenderer. Keep or replace their coverage when changing the relevant contract. The inventory does not certify every unreferenced method as dead code.

## Options and planning estimates

These are estimates of distinct production files touched or added, based on the dependencies above, not an approved implementation scope or a promise of feature parity. Test/build/documentation files are additional. Shared files overlap between rows and must not be summed blindly.

| Option or migration slice | Estimated production files | Risk and likely work |
| --- | --- | --- |
| Freeze/deprecate legacy raster API growth; keep current applications | 0 in this decision phase | Low immediate runtime risk. Continuing maintenance and mandatory Phase 66/final architecture cleanup remain; freeze does not make that cleanup free. |
| Migrate Breakout's raster submission | 8-14, plus 2-4 validation/docs | BreakoutApp and GameLevel header/source pairs, sprite/material adaptation or new semantic frontend, resource/ordering integration. Medium risk: 2D ordering, alpha, screen coordinates and ownership. At least two bounded implementation slices. |
| Migrate Editor's full maintained scene | 20-35+, plus 4-8 validation/docs | SceneApp header/source, scene hierarchy panel, Actor/Entity/Group/Scene, material variants, lights/helpers, Geometry/import/animation adapters and submission integration. High risk: skeletal animation, terrain materials and editor manipulation need a capability/parity decision first. Static-only migration is a different, narrower product choice. |
| Isolate RayTracing image/presentation and errors | 4-8, plus 1-3 validation/docs | SimpleRenderer header/source, Image header/source, RayTracing caller and semantic UI bridge. Medium lifecycle/error risk; retain CPU sampling, TBB scheduling and ray equations. This is not a conversion to the raster renderer. |
| Retain supported Actor/sprite/CPU frontends over a shared lower backend | 30-50+, plus 5-10 validation/docs | Combine frontend adapters with shared typed resource/error contracts and backend-private submission. High integration/parity risk; current scheduling reuse is only the starting point. No need for per-draw virtual dispatch or a second backend. |
| Formal deferral with a temporary feature freeze | 0 in this decision phase | Preserves applications while postponing the product direction. Requires owner acceptance of the temporary boundary and follow-up condition below, followed by Phase 64 checkpoint approval. |

Any implementation exceeding the normal 2-6 production and 1-3 test/build/documentation file budget requires an explicit owner-approved split/scope amendment. Independent additional phases require a coordinated roadmap/state/helper extension; these estimates do not invent phase IDs or authorize changes to Phases 65-68. If migration is selected, a separate appendix must identify the selected applications and small sequential deliverables before this candidate is sealed.

## Selected freeze and retention boundary

The selected policy is:

1. Freeze new consumers and feature expansion of Renderer, Renderer2D, RenderSystem and their legacy Actor/Geometry/Material drawing interfaces. Deprecation is a documented development policy; it does not remove applications or insert compiler annotations in Phase 64. Correctness, lifecycle and architecture-policy repairs remain allowed in their explicitly scoped owning phases.
2. Retain Editor and Breakout behind the existing FrameScheduler legacyScene callback. Retain RayTracing's CPU algorithm and existing editorUI/image path as a transitional frontend. Retain the simulation's private mesh-export/physics attachment bridge until its actual callers and ownership are safely replaced; do not change physics equations or simulation scheduling.
3. New raster consumers use semantic SceneRenderResources, render handles/descriptors/components, extraction, RenderFrame and FrameSubmission. Publish before extraction, retain resolved versions through submission, and keep finalized frames immutable. Do not route new contracts through a legacy getter, raw program/texture/buffer name, or throwing compatibility API.
4. FrameScheduler continues to own frame sequencing, resolve, UI backend draw and presentation. Callback bodies do not acquire that ownership. Native GL/SDL state remains backend-private and context-thread owned; caches end across external callbacks/context changes. CPU workers do not upload, read back, draw or destroy GPU objects. Resource retirement precedes context teardown.
5. No engine-wide compiler exception switch, compatibility shim, alternative graphics backend, toolchain/dependency update, or normal runtime logging replacement is authorized. Recoverable migrated failures use typed std::expected with structured diagnostics and transactional cleanup; invariants use the established assertion/debug-fatal policy. Standard concepts/requires apply where an actively migrated generic API has a meaningful eligibility contract.

Revisit the product policy when the owner requests a new legacy-dependent capability. Phase 66 independently owns the mandatory cleanup and must identify any isolation prerequisite to final compliance. Any application migration or withdrawal of support needs a separately approved scope and parity/disposition plan before implementation. The owner selected a freeze, so no application-migration appendix is required or authorized by this checkpoint.

## Required transitional disposition before the final gate

These are current-source findings, not newly introduced interfaces and not an exhaustive Phase 66 inventory. Backend implementation may use native APIs; normal applications/editor/scene/ECS/assets/cross-module contracts may not inherit them. Moving a declaration behind a legacy label alone does not establish isolation.

| Retained boundary | Verified evidence | Owning disposition |
| --- | --- | --- |
| Raw mesh/program names and native draw constants | Geometry::GetVAO; Attribute/IndexBuffer::GetBufferRef; Material::GetShaderID -> Group::GetProgramID -> Scene's program grouping; RenderSystem::ElementsDraw uses native mode/type semantics and default `0x1405` | Phase 66 disposition: remove genuinely unused operations or isolate native details and replace retained normal contracts with semantic handles/descriptors. Preserve live application behavior and the private shape bridge. Final normal-surface isolation must pass Phase 68. |
| Assimp declarations in legacy import/animation headers | RawModel, AnimatedModel and Animation forward-declare aiMesh/aiScene/aiNode/aiAnimation and use them in declarations | Phase 66 disposition: keep Assimp behind implementation boundaries or remove paths only after caller/feature decisions. Existing modern MeshImporter remains the normal import API; animation parity is not assumed. |
| Ray image-to-UI native escape and errors | Image::GetTexID reaches ImGui::Image in RayTracing.cpp; SimpleRenderer throws for excessive image dimensions and camera-size mismatch | Phase 66 disposition: semantic image/UI bridge and typed failure transport with cleanup, retaining CPU sampling and joined-worker upload timing. Explicitly classify any remaining UI/native boundary in the final surface check. |
| Retained ECS renderer and shape loader errors | RenderSystem.cpp throws for framebuffer extent/debug line count; ShapeManager.cpp throws for null/duplicate registration, missing model and empty model geometry | Phase 66 owns engine-wide exception inventory and cleanup, including deciding invariant versus recoverable outcomes and adapting actual callers. The typed scheduler callback signature does not convert exceptions inside a legacy body. |
| Related remaining root/resource/UI compatibility | Existing root manager access, GLContextThread creation cleanup, legacy buffer owners and UI/UICore declarations remain outside the migrated normal API closure | Include in Phase 66 inventory and explicit disposition. Do not infer their removal or final compliance from this decision-only inventory. |

Phase 66 must perform the engine-wide residual exception inventory/cleanup and record concrete disposition of retained legacy/native dependencies. If required isolation is too broad, stop for the existing scope-amendment/split process; do not quietly defer prerequisite work past Phase 68. Phase 68 requires zero explicit throw/try/catch and no custom recoverable exception control flow across GEngine-owned production source, and no native leakage in the final normal rendering/platform surface. Freeze, retention and formal deferral grant no waiver of either gate.

## Evidence and validation scope

Local execution evidence: `logs/rendering/phase64/preflight.json` and `logs/rendering/phase64/dependencies.json`. The dependency command is `python logs/rendering/phase64/inventory.py`; it removes comments before locating references, then the application call chains above are checked by source inspection. It is not a compiler dependency or linker reachability proof.

The Phase 64 review and compact receipt bind the selected policy, source fingerprints and documentation checks. Documentation-only execution needs path/reference/contract checks, protected-content and index verification, and governance validation. Builds, graphical smokes, benchmarks and sanitizers are not required solely for this decision document, and none is claimed here. Phase 63's approved runtime evidence retains its original scope and does not certify legacy final-architecture compliance.
