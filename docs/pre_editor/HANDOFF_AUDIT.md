# Post-Rendering handoff audit

**PROPOSED / NOT_STARTED. Audit: COMPLETE_FOR_REVIEW.** This document is inspection and planning, not implementation, approval, a receipt, or activation.

Inspected 2026-09-27 in `C:/dev/GEngine-rendering` under the [master instruction](POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md), sections 1–20. No builds, applications, probes, tests, benchmarks, captures, or agents were run. Evidence comes from read-only Git/source inspection, existing records, and hashing. Only audit/proposal documents under `docs/pre_editor/` were written.

Revision provenance: [Revision 01](revisions/REVISION_01.md) records the later documentation clarification; original audit observations and their timestamps are retained unchanged.

## Entry gate and exact baselines

| Identity | Verified observation |
| --- | --- |
| Repository / destination | `AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine`; `https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git` |
| Rendering branch | `rendering/refactor` |
| Immutable approved Phase 68 commit | `9995074e7af161db76c5bf79a5ede030702fb4ca` |
| Annotated tag | `render-refactor-phase-68-approved`; object `48dbfd3430413492120eb551e761028b94c6f9c6` |
| Publication | Successful `git ls-remote`: branch and peeled tag equal the full commit; tag object equals local object |
| Completed approval | `C:/dev/GEngine/.git/worktrees/GEngine-rendering/rendering-approval/PHASE_68.json`: COMPLETE; matching transaction commit; branch_pushed and tag_pushed true |
| Workflow records | REFACTOR_STATE: approved 68, next COMPLETE; bytes equal completed transaction state_after. All 68 present phase journals COMPLETE; none unfinished |
| Proposed PRE_EDITOR entry | Same full Phase 68 commit; no corrective checkpoint established as necessary by this audit |
| Actual worktree | HEAD equals Phase 68; index has no staged changes; existing local work remains separate |
| Local differences | `.gitignore`, `GEngineEditor/imgui.ini`, 43 deleted Physics documents, untracked master instruction and two render-world probe/runner files; full inventory in observation |
| Preservation baseline | 47 sealed protected paths; 561 existing/protected/governance/input paths fingerprinted; raw index SHA-256 `6b8a0d0b94ec358a838f358758f6e7e77d450f5126c38740aa1addddc75e8c49` |
| Source identity | All 392 Phase 68 validation-input fingerprints match, including the complete canonical RBS source |
| Toolchain | Windows x64; Premake/VS2022 v143; MSVC 14.44.35207; SDK 10.0.26100.0; C++23/c++23preview; Debug MTd, Release MT |
| Maintained apps | RBS, Editor, Breakout, RayTracing; only RBS for new application execution |

Evidence: [baseline observation](evidence/baseline-observation.json), [source observation](evidence/source-observation.json), tracked `rendering-checkpoints/phase-68.json`, completed approval journal, local workflow state. No approval helper or lock/seal/finish operation was executed.

Sealed reviews preserve their pre-approval wording: Phase 68's review mentions awaiting review and Phase 67 HEAD. Phase 65's old review says BLOCKED; its final tracked receipt, final review and owner closure amendment supersede that historical status. These are not current unfinished transactions and were not rewritten.

The Phase 68 owner amendment replaces its older four-app/performance rerun requirements with RBS-only functional acceptance. Existing `logs/rendering/phase68/integration/{debug,release}-final-build/results.json` records actual exit 0, compiler/CRT and build commands. `runtime-summary.json` records RBS success/failure/native-close checks and retained GL advisories. `native-boundary-audit.json` covers 105 compiled owned headers and RBS without PCH/native include roots. `final-source-audit/exception-audit.json` records zero explicit exception/transport/custom exception sites over 374 owned source files, excluding 482 attributed dependency files. Matching fingerprints permit reuse of these input-specific conclusions, not a new runtime or live-binary performance claim.

## Source evidence map

Paths below omit the common `GEngine/include/GEngine/` header root where clear. Implementations use `GEngine/src/`; RBS means `RigidBodySimulation/src/RigidBodySimulation.cpp`.

| Key | Inspected implementation and callers |
| --- | --- |
| S01 | `Core/GEngine.h:20,67`, `src/Core/GEngine.cpp:157,210`: root owns managers/publication/platform; static access borrows root; startup rollback; make context current, retire owners, require publication drained, then release platform |
| S02 | `Managers/AssetsManager.h:21–66`, `.cpp:12,46–167`: texture/sampler registries, file/image cache, legacy bindings/text/fonts/attachments, lazy async loader; synchronous load combines decode/GPU creation/cache/publication |
| S03 | `Managers/ShapeManager.h:64–103`, `.cpp:40–103,181–229`: eager legacy Geometry primitive cache, CPU export and private Physics attachment, RawModel/AnimatedModel import |
| S04 | `Managers/ShaderManager.h:16–90`, `.cpp:7–27`: ordered file-key cache of Shader owners; compile on miss via CreateShaderProgramFromFiles; root lookup; no reload API. `Assets/ShaderCompilation.cpp:170` enumerates uniform names |
| S05 | `Renderer/SceneRenderResources.h:42–75`, `.cpp:40–95,126–252`: mesh/program/pipeline/template/material registries, named mesh sharing, per-PublishMaterial compile plus four-version publication/rollback; public Publication/ForFrame/Meshes/Materials/Pipelines |
| S06 | `Assets/AssetHandle.h`, `AssetRegistry.h:99–218`, `AssetPublication.h:26–134`: slot/generation/domain identity; replace retains old version; CPU leases/fences gate collection; no generation resurrection; publication/read exclusion |
| S07 | `Renderer/RenderFrame.h:100–194`, `FrameScheduler.cpp:97–126`, extraction/submission: owner upload drain -> preparation/freeze -> immutable frame -> submission; const spans/exact-version owners |
| S08 | RBS `:64–230,695–740`: string material properties and samplers; named shapes; Acquire/copy/SetTexture/Replace; explicit frame camera, targets, resource scopes |
| S09 | `_Scene.cpp:186–241,665–709`, `Component.h:596–598`: modern handles copy with new entity identity; runtime Physics bindings cleared; duplicate is sibling and shares mesh/material. `_Entity.cpp:27–84`: validated parenting, no preserve-world option |
| S10 | `_Scene.cpp:246–419`, `_Scene.h:63–100`: sole 1/60 scheduler, two steps/update, 0.25 s backlog; static authoring versus dynamic/kinematic authority; previous/current poses and presentation interpolation; rigid-body root restrictions |
| S11 | `Events/Event.h:51–95,121–149`, EventManager `.cpp:168–300`, InputManager `.h:51–177`, `.cpp:153,185–280`: unsubscribe by callable type; live callback iteration; string/dynamic-cast dispatch; selected SDL-to-engine events; sampled input. RBS `:478–501` registers raw event owners capturing application pointer |
| S12 | `Camera/SceneCamera.cpp:29–49`, `src/Camera/EditorCamera.cpp:364–407`, `RenderComponents.h:37–64`, RBS `:720–738`: multiple camera models; legacy unchecked height division; manual editor view/frame snapshot; no common conversion/lifecycle contract |
| S13 | `PhysicsWorld.h:13–47`, `.cpp:48–110`, `PhysicsBody.h:58–140`, `Component.h:492–544`, `_Scene.cpp:897–1071`: raw body ownership/identity slots, Scene-owned shapes, public mutable fields/bool setters, RuntimeBody pointer; box/convex startup depends on retained Geometry CPU data |
| S14 | `PhysicsSystem.cpp:689–735,1137–1205`, `Constraints/ConstraintPenetration.cpp:253–292`, Manifold ordered solve: warm start; persistent inelastic normal/friction passes; subsequent refreshed positive-TOI restitution, threshold 1 unit/s and material product; sleep/islands/spin/rolling already implemented |
| S15 | `Constraints/Constraint.h:10–35`, constraints file inventory and World/System stepping: contact base/math exists; no managed joints, distance/ball/hinge/limits/ragdoll/capsule implementation found in owned source |
| S16 | `RenderExtraction.cpp:224,317`, `RenderFrame.h:192`, `FrameSubmission.cpp:534,805–848`: debug-line payload exists but submission rejects it; static helper meshes use Debug pass |
| S17 | `RenderComponents.h:52–64`, extraction -> `FrameSubmission.cpp:535–568,694–708`: one Directional, one Point, one unshadowed Spot; extra lights/Spot shadows fail explicitly; shadow targets/signatures/culling/cache exist |
| S18 | `Assets/Shaders/pbr_cascade_shadow.frag:205–326`: GGX/Smith/Schlick pieces, duplicated evaluations, point 80/distance, Spot range/cone falloff, correctly separated point/directional visibility; arbitrary ambient 0.05, F0 vector, gamma 1/1.8, commented tone map |
| S19 | `FrameSubmission.cpp:549`, `RenderTarget.h:28–44`, `aa_post.frag:9–12`: submission requires RGBA8 scene target, post shader copies image; no owned IBL convolution/LUT, Bloom or view-exposure pipeline found |
| S20 | ShadowQuality and PassTiming header/source: High4096 default, explicit lower/custom tiers/fallback, paired allocation/payload-byte accounting; delayed GPU queries retain missing state; UI/present CPU only |
| S21 | `_Scene.h:106,151`, `_Entity.h:141`, Scene copy/lifecycle, UUID/RuntimeAssets/async code: runtime IDs, roots, cloning, revisions, tasks exist; no Project/AssetDatabase/SceneSerializer/transactions/Undo/Redo/persistent-reference system found in owned-source searches and lifecycle inspection |

### Scale mutation and collider behavior (S22)

Revision 01 clarifies the original scale observation from read-only authoritative Phase 68 source; it establishes no new source defect or implementation authorization. Existing Event/Signal lifetime findings remain scoped as recorded. Signal.h:311–445 supplies typed delegates; Property.h assignment notifications are not a validated authoritative transaction.

| Mutation / lifecycle path | Observed behavior and supported conditions |
| --- | --- |
| Transform3DComponent::SetScale(Vec3f) or SetScale(float) | Component/Component.h:209–225 compares the requested scale to Scale. A changed value synchronously emits OnScaleChanged with the new value, then stores Scale; the scalar overload broadcasts a uniform vector. An unchanged value emits nothing. The callback argument supplies the new scale while the component still holds the previous value |
| Connected runtime Physics bridge | _Scene.cpp:897–1084 connects the component signal to virtual PhysicalShape::HandleScaleChanged only after runtime body/shape creation and accepted body pose. RuntimePhysicsPose retains the connection; stop and startup rollback disconnect it. Copy/duplicate cleanup removes the copied runtime binding (_Scene.cpp:86–94). A setter call reaches Physics only while that live connection exists |
| Direct Scale assignment, construction or component replacement | These do not invoke SetScale or emit OnScaleChanged. Initial shape construction is separate: the sphere uses fixture Radius; the box uses Geometry::GetPoints(transform.Scale); the convex uses GetUniquePoints() with default unit scale. Geometry.cpp:255–273 applies the supplied component-wise scale to its copied points. RBS constructor examples at :324 and :422 supply Transform scale; they are not runtime setter calls. Actor.cpp:291–300 directly writes its own scale; Editor SceneApp.cpp:1125 likewise directly writes Scale |
| Sphere callback | ShapeSphere.cpp:23–40 computes radius = baseRadius * new_scale.x. Finite positive resulting radius is required; unchanged or invalid results leave shape/revision unchanged. This preserves the existing X-axis policy for nonuniform input, not a general ellipsoid contract. A successful changed SetRadius is an explicit collider edit that establishes a new base radius |
| Box/convex callback | Shape.cpp:8–32 scales the shape-owned base points component-wise, rejects non-finite input/output, then calls the concrete Build while restoring the base point source on every exit. ShapeBox.cpp:61–90 and ShapeConvex.cpp:596–621 install successful finite, nondegenerate geometry and its revision; invalid rebuilds retain the prior shape. Signed/nonuniform scaling is supported when the resulting point set/hull passes these checks. Repeated scales are relative to the stored source, not the previous scaled result; that source may already include the box's initial Transform scale |
| Mesh regeneration / proposed Apply-Bake | These change CPU mesh data/bounds/revisions, not the above callback by themselves. They must not silently replace a collider. If a future Bake also resets Transform through SetScale, that setter's existing connected callback behavior must be accounted for explicitly |
| Explicit collider change | Sphere SetRadius or a successful point-shape Build changes the collider's base geometry independently of render mesh authoring. Future typed collider APIs belong to C; no new implicit mesh-to-collider synchronization is proposed |

Shape revisions feed body bounds/COM/inertia caches (PhysicsBody.cpp:164–256) and external-change/sleep handling on the next positive Physics update (PhysicsSystem.cpp:449–478). Existing PhysicsTests source at :660–786 asserts absolute/repeated scaling, the sphere X-axis policy, signed/nonuniform point-shape scaling, rejected-input preservation and explicit base replacement. Those assertions were inspected, not executed.

The existing setter/void-callback sequence is not a typed atomic Transform-plus-collider transaction: a handler may reject an invalid shape update before the setter stores the requested Transform value. This is a description of source semantics, not a defect inference or permission to alter them.

Phase 08 preserves supported connected-setter behavior while distinguishing Transform scale from mesh regeneration/Bake and explicit collider edits. Phase 13 migrates connection lifetime without changing shape policy; Phase 14 deliberately separates internal shape-update ordering from public notifications after successful authoritative mutation. Phase 24 integrates the reviewed collider/Transform contract, including base-source initialization, shape revisions, cache/wake behavior and connection teardown. Future invalid/nonuniform/negative/zero-scale policy choices remain applicable OPEN_DECISION items; no behavior is changed by this audit revision. E extends the same providers.

## Rendering completion/residue

**VERIFIED within matching approved evidence and traced paths:** generation/version identity, context ownership, resource retirement, immutable frame/retained versions, publication before extraction, worker CPU/GPU separation, deterministic material slots/alpha state, typed normal boundary and final explicit-exception policy. This is not exhaustive new correctness certification.

**Accepted retained legacy:** Phase 64's `tools/LEGACY_RENDERER_POLICY.md` freezes Editor Actor, Breakout sprite, Ray CPU frontend and private ShapeManager bridge. Current source confirms them; RawModel removal was not the selected completion contract. SceneRenderResources' shader wrapping preserves approved lighting/coverage. These are not growth paths for new authoring APIs.

**PRE_EDITOR gaps:** resource/authoring simplicity, working shared debug submission, Spot shadows, complete PBR/IBL/HDR/Bloom, persistence/transactions. **Core Editor gaps:** selection/gizmo/Inspector/Content Browser UI, topology editing and later animation integration. Area lights/shadows, motors, active ragdoll, auto-exposure and many-light algorithms remain deferred unless separately authorized.

No new **RENDERING_COMPLETION_DEFECT** is established. If one is later proven, propose a separately authorized corrective checkpoint without rewriting Phase 68 or disguising it as a capability gap.

## Manager/resource boundary audit

| Service | Actual ownership / work | Error, thread, callers and proposed disposition |
| --- | --- | --- |
| AssetsManager (actual plural name) | Texture registry, sampler cache, CPU decode/GPU creation/cache/publication, async loader, fonts/text, legacy bindings/attachments; loader closes before registry retirement | Typed expected/variants; root/context thread. Separate CPU request/import and runtime lifetime from backend publication internally; E alone owns persistent AssetDatabase. No new duplicate TextureManager |
| ShapeManager | Eager Geometry primitive/GPU owners, retired owners, static/animated imports, modern CPU export and Physics attachment | Typed Platform/shape/import errors; owner context. Retain frozen legacy users; A ordinary geometry must use CPU templates -> MeshAsset -> MeshHandle -> approved upload, no competing ShapeManager architecture |
| ShaderManager | Ordered file-key Shader cache; compilation delegated; borrowed Shader pointers; owner destruction | Typed ShaderError; context owner. Small legacy cache, not proven god object. Modern path independently compiles in SceneRenderResources. Consolidate description/variant cache and backend compiler responsibilities; do not hide duplicate work behind Materials::Standard |
| SceneRenderResources/material services | Five dependent registries, role list, shape sharing; program/pipeline/template/instance creation and rollback; frame binding views | Typed domain errors; publication safe point. Narrow semantic authoring changes feed revisions; renderer owns scopes/preparation/leases. Preserve advanced declarations separately from Standard API |
| AssetRegistry/AssetPublication/SamplerCache | Runtime identity, exact revisions, equivalence caching, GPU-safe retirement/fences | Typed recoverable errors; assertions for thread/phase invariants. Advanced runtime infrastructure, not persistent metadata |
| AsyncUploadQueue/Texture/Mesh | Bounded CPU workers, alias coalescing, tickets/cancellation, payload accounting, scheduler-only GPU publication | Typed states/errors. One upload can exceed time budget; Assimp internals excluded from payload cap. Reuse scheduler; E projects task status instead of adding another job system |
| FrameResources/RenderStateResources | Prepared frame leases and extraction inputs | Renderer responsibility. RBS's m_FrameResources is actually a SceneRenderResources owner; distinguish that from RenderFrame's FrameResources record. Naming plus registry/scope exposure need treatment, not a prettier public frame API |
| EngineContext | Root lifetime/platform/managers/publication | Static current-root borrowing and broad include dependencies remain. Inject narrow services; no demonstrated runtime ownership cycle is asserted |

For every service, registration/resolution/publication/retirement not listed is delegated to its named registry or backend; no independent persistent database exists. Normal application calls currently cross services and registries (S08). Proposed dependency direction: authoring intent -> narrow runtime operations -> approved publication/registries -> renderer preparation/backend. CPU geometry/import must not require root/context; standard material instances must not trigger repeated compilation. Both external API simplicity and internal dependency direction gate A.

## Representative operations

Call counts are approximate explicit calls after setup, excluding error branches/field assignments/loops; unavailable operations have no invented count. No timing is implied.

| Operation | Current path / calls | Required knowledge, cost and ownership | Finding |
| --- | --- | --- | --- |
| Primitive creation | PublishShape + CreateEntity + AddOrReplaceComponent: 3 | SceneRenderResources/_Scene/_Entity/RenderComponents; name cache + ShapeManager export + mesh registry; first use uploads, later shares | API-02 |
| Standard material | RBS lambda, five image loads, five samples, PublishMaterial: about 12 | AssetsManager/MaterialTemplate/SceneRenderResources; string names, texture/sampler identities, compile and four-version publication | API-03/04 |
| Material modification | BeginFrame/Acquire/copy/SetParameter or SetTexture/BeginPublication/Replace: 6–8 | Instance registry/scopes and old-frame lifetime; no semantic property operation | API-01/04 |
| Assign mesh/material | AddOrReplaceComponent<MeshRendererComponent>: 1 | Two already published handles, registry domain; resolves later | API-05 |
| Custom shader/material | Shader create/register, pipeline create/register/acquire, template create/register/acquire, instance create/register: about 10 plus scopes | Shader/Pipeline/Template/Instance/Binding, reflection/string semantics and explicit ownership; ShaderManager is optional legacy route | API-03 |
| Share mesh | Reuse MeshHandle: 1 assignment | Same registry domain, no heavy copy/upload | API-02 |
| Duplicate Entity sharing mesh | _Scene::DuplicateEntity: 1 | New entity ID, copied handles, cleared runtime body, sibling not subtree | API-05, existing provider |
| Explicit mesh copy | No normal API | Reconstruct MeshSourceData then publish; immutable move-only MeshAsset, CPU data provenance needed | API-02 |
| MakeUnique | Absent | Explicit CPU copy/new handle/reassignment; not GPU readback | API-02 |
| Resize/stretch | Transform SetScale call or direct Scale write: distinct paths | SetScale emits the existing connected Physics bridge under S22 conditions; direct writes do not. Neither path regenerates render topology; mesh Bake and explicit collider edits remain separate operations | API-02/TRN-01 |
| Scene Camera | CreateEntity + RenderCameraComponent + pose: about 3 | Modern intent separate from legacy CameraComponent/SceneCamera; consumer selects viewport/view | CAM-01 |
| Editor Camera/view | Construct _EditorCamera + viewport/view update + FrameCamera/request: 4–6 | Camera, renderer, logical/pixel extents and matrix snapshot | CAM-01 |
| Directional Light | Entity + RenderLightComponent + pose: about 3 | Semantic kind/color/intensity; one supported, separate shadow ownership | LIT-01 |
| Point Light | Same about 3; helper separate | Position/range, one point shadow; attenuation defined in shader | LIT-01 |
| Spot Light | Same about 3 | Radians -> packed cosine; only unshadowed currently | LIT-02 |
| Subscribe/unsubscribe | new Events + Subscribe + RegisterEvent: 3; Unsubscribe: 1 | Event/EventManager; string/signature, raw event owner, captured borrowers; unsubscribe is type-based and no RAII token | EVT-01 |
| Input consumption | GetInputManager -> state -> pressed/held: about 3 | Semantic key/mouse enums, polled state, no complete lossless/text/focus/capture contract | INP-01 |
| Publish/replace | Read scope/Acquire/edit/publication/Replace: 5+ | Root/domain/registry/scopes; exclusive phase, retain old versions, GPU owner thread | API-01 |
| Rigid body | Entity + body component + fixture + runtime start: 4+ | Scene/Component/Physics; RuntimeBody raw pointer, distinct shape ownership, some bool setters | PHY-02 |
| Collider | Sphere fixture: 1; box/convex fixture + AttachPhysicsShape: 2 | Box/convex consume Geometry CPU vertices; no Capsule; mesh and collider should be independent | PHY-02/05 |
| Physics/render transform sync | Scene::Update: 1, extraction later | One scheduler; authoritative poses published, interpolation in presentation copies; edits become teleports | TRN-01 |
| Basic joint | No managed public operation | Contact-internal ConstraintPenetration/base raw bodies/anchors are not distance/hinge/chain support | PHY-04 |

## Physics baseline and findings

Authoritative Physics is Phase 68 RBS code: Physics tree `829629515a5363bdcf23d6f1654e7714a5fa9f18` plus Phase 68 Scene/Component integration. Physics Phase 39 `89030282c1b2fd21a55eae2934e7f080eff7950d` is already an ancestor. Rendering later added typed shape errors and integration changes. Current solver default is **1**.

Separate worktree `C:/dev/GEngine-physics`, branch `physics/refactor`, HEAD and approved Phase 39 tag both resolve to `89030282c1b2fd21a55eae2934e7f080eff7950d`; tree `cc0fad3decd658c8ec6cfd55ce6a3103cda420bb`. Its dirty header now sets default **2**, superseding the old rollback document's clean-header observation. That edit, local RBS settings and other dirty work are not approved entry changes. Full read-only status/diff and diagnostic hashes are in source-observation.json; nothing imported.

Phase 40 is explicitly **REJECTED / ROLLED BACK**. Its attribution was accepted as diagnostic evidence; iteration-policy implementation was rejected for suppressing impact response. Its old future Physics numbering does not create a second PRE_EDITOR series. Reuse existing approved fixed-storage contact math, manifolds, islands, sleep/wake, spin/rolling resistance.

Frozen historical input: SHA-256 `79eb5a76b06726b77333fff93ddb47999671df025ef1c75322494b8ac62116c6`, 185 bodies (180 unit-mass radius-1 spheres, five static surfaces), gravity `(0,-12,0)`, per-body friction/elasticity 0.5, combined 0.25, spin/rolling lengths 0.05. Phase 39's 60/120 Hz, 1/2/4/8-pass diagnosis shows contact-network convergence, upward moving-support impulses, earlier TOI restitution, later inelastic micro-hops, and separate position correction. Phase 40 implicates persistent inelastic normal response plus restitution ownership/order. This is not current RBS reproduction or a new definitive root-cause claim.

Historical 120 s acceptance matched prior trajectories and required 171/180 continuously quiet/sleeping identities during 110–120 s, penetration <=0.035, peak energy <=30391.2 including initial 30240. Historical sphere, box-stack, friction, spin, sleep and solver-storage checks exist. These criteria apply only to the exact historical input/protocol; late settling equivalence does not certify impact correctness. Current RBS inputs differ and need their own identity and acceptance.

C must assess lattice, sphere-on-ground, box stacks, frictional rest, low-speed/off-center impacts, multi-contact/spin, timestep sensitivity, sleep/wake, then chains/hinges/ragdoll. Core resting/impact behavior must be resolved or explicitly accepted with quantified bounds before joint expansion. Raising sleep thresholds or adding passes alone cannot close PHY-01.

Historical Physics nominal Release diagnostics used an MTd override, unlike Phase 68 Release MT. Current Physics performance and tails are **EVIDENCE_INSUFFICIENT**: broad/narrow phase, contacts/islands, solver/pass scaling, active bodies, sleep savings, transient allocations need bounded RBS evidence separated from rendering. No benchmark executable ran.

## Code quality and shared prerequisites

Sampled source-observation.json: RBS 70 lines over 120 columns (raw max 234); SceneRenderResources 11; FrameSubmission 58; PhysicsSystem 28. Multiple significant actions per line and repeated expected/error handling obscure ownership/cost. Large mixed setup/stepping/submission functions, public using-namespace, inconsistent old/new naming and commented-out blocks need scoped treatment. No root .clang-format exists. These samples are not an engine-wide style invariant.

Before A: policy adoption, then separate mechanical formatting of initial A touchpoints, then bounded semantic readability work. Other code is FIX_WHEN_TOUCHED; frozen legacy formatting is DEFER. Formatting is not behavioral proof. Proposed style files remain inactive.

Existing Scene create/duplicate/share and Transform/fixed-step behavior provide minimum runtime foundations. A needs a narrow assignment/mutation surface and typed material semantics. B needs successful-change notifications and explicit runtime transform ownership before camera/input consumers. C needs shared semantic debug submission before diagnostics. E extends these with persistent identity, hierarchy authoring, transactions and commit-time notifications; A–D do not depend on E. Exact providers and dependency phases are in PRE_EDITOR_PLAN.md.

## Coverage dispositions

| Required topic | Disposition | Trace |
| --- | --- | --- |
| Phase 68 commit/tag/publication/completion and local preservation | VERIFIED | Baseline observation; preservation check |
| Final identity/lifetime/publication/backend/error invariants | VERIFIED for reused exact-input evidence and inspected paths | S01/S06/S07; no new completion defect |
| Retained adapters/managers/legacy overlap and representative operations | GAP_FOUND for future authoring; retained boundary VERIFIED | API-01–05, service/operation tables |
| Code Quality | GAP_FOUND | CQ-01/02 |
| Five performance residuals and historical attribution | EVIDENCE_INSUFFICIENT for closure | PERF65-01/02/03/04/05-06; original carry-forward retained |
| A resource/shader/material/geometry | GAP_FOUND | API-01–05 |
| B events/input/camera and mutation/transform foundation | GAP_FOUND | EVT-01/INP-01/CAM-01/TRN-01 |
| C provenance | VERIFIED identity; experimental edits separated | S13/S14; source observation |
| C resting/impact stability and performance | EVIDENCE_INSUFFICIENT for current acceptance | PHY-01/03 |
| C API/colliders/constraints/chain/hinge/limits/ragdoll/debug | GAP_FOUND | PHY-02/04/05, DBG-01 |
| D existing Directional/Point shadow/visibility architecture | VERIFIED existing capability; broader quality unproven | S17/S18/S20 |
| D Spot shadow, shared PBR, IBL, HDR, Bloom, exposure/display | GAP_FOUND | LIT-01–04 |
| D visual quality, temporal stability, CPU/GPU/memory/tails | EVIDENCE_INSUFFICIENT | LIT-05/PERF65-04; no fresh timing/image claim |
| E Scene/ECS, Project, persistent IDs, serialization, database, import/tasks, metadata, transactions, Undo, dirty state, Edit/Play | GAP_FOUND; existing providers reused | AUT-01–05, DBG-01 |
| F real RBS adoption and integrated handoff | GAP_FOUND | INT-01; A–E integrate as they progress |
| Core Editor UI, topology editing, Area lights/shadows, active ragdoll/motors/animation blending | NOT_APPLICABLE_WITH_REASON | Explicit scope exclusions, not silently dropped requirements |
| Focused existing tests and other consumer compatibility | VERIFIED source inventory/affected invariants only | tools material/mesh/submission/retention/interpolation/async probes and PhysicsTests assertions; no execution |

Evidence gaps constrain future behavior/acceptance, not these source conclusions. No fresh diagnostic was needed or admitted under sections 1.1/1.2. Smallest future diagnostic proposals, isolated locations and START-blocking OPEN_DECISION items are in VALIDATION_CONTRACTS.md. No gap is labeled PASS.

